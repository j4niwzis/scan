cmake_minimum_required(VERSION 4.3.4 FATAL_ERROR)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
    "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
set(CMAKE_CXX_MODULE_STD ON)

project(scan LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/CPM.cmake)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(BOOST_USE_MODULES ON CACHE BOOL "" FORCE)
CPMAddPackage(
  NAME googletest_modules
  GITHUB_REPOSITORY j4niwzis/googletest-modules
  GIT_TAG 706920f1d230f0316d271466d8885735be3e300e
  OPTIONS
    "BUILD_GMOCK OFF"
    "gtest_build_tests OFF"
    "gtest_build_samples OFF"
)

option(SCAN_BUILD_BENCHMARKS "Build regex engine comparison benchmarks" OFF)

# The engine against another engine, on patterns nobody wrote.
#
# Everything that happens while a program is compiled -- reading the pattern,
# building the machine, cutting the walks below a match, minimizing -- is
# ordinary code that also runs, so the pattern can be a string made up at run
# time and the answers can be compared against an engine that was not written
# here. RE2 in its default mode is leftmost-first, which is the rule this
# library settled on, so a disagreement is a bug in one of the two.
#
# Off by default: RE2 brings abseil with it, and that is a heavy thing to make
# every build carry.
option(SCAN_BUILD_FUZZER "Build the differential fuzzer against RE2" OFF)

if(SCAN_BUILD_FUZZER)
  set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(ABSL_ENABLE_INSTALL ON CACHE BOOL "" FORCE)
  set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
  CPMAddPackage(
    NAME abseil
    GITHUB_REPOSITORY abseil/abseil-cpp
    GIT_TAG 20250127.0
  )
  CPMAddPackage(
    NAME re2_source
    GITHUB_REPOSITORY google/re2
    GIT_TAG 2024-07-02
  )
  # The other engine is a header, and a translation unit that includes one
  # textually while importing modules built against the `std` module gets two
  # views of the standard library. So it is wrapped once, the way the
  # benchmarks wrap theirs, and nothing else here names an RE2 type.
  add_library(fuzz_oracle)
  target_sources(fuzz_oracle PUBLIC FILE_SET CXX_MODULES FILES
    fuzz/oracle.cppm)
  target_link_libraries(fuzz_oracle PUBLIC re2::re2)
  target_compile_features(fuzz_oracle PUBLIC cxx_std_23)

  add_executable(differential_fuzz fuzz/differential.cc)
  target_link_libraries(differential_fuzz PRIVATE scan fuzz_oracle)
  target_compile_features(differential_fuzz PRIVATE cxx_std_23)

  # And the same thing under libFuzzer, where the bytes are chosen to reach
  # places rather than at random.
  if(SCAN_FUZZER_LIBFUZZER)
    add_executable(differential_libfuzzer fuzz/differential.cc)
    target_link_libraries(differential_libfuzzer PRIVATE scan fuzz_oracle)
    target_compile_definitions(differential_libfuzzer PRIVATE SCAN_FUZZ_ENTRY)
    target_compile_options(differential_libfuzzer PRIVATE
      -fsanitize=fuzzer,address)
    target_link_options(differential_libfuzzer PRIVATE
      -fsanitize=fuzzer,address)
  endif()
endif()

if(SCAN_BUILD_BENCHMARKS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options($<$<CONFIG:Release>:-flto>)
    add_link_options($<$<CONFIG:Release>:-flto>)
  endif()
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
  CPMAddPackage(
    NAME google_benchmark
    GITHUB_REPOSITORY google/benchmark
    GIT_TAG v1.9.4
  )
  target_compile_options(benchmark PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-Wno-c2y-extensions>
  )
  target_compile_options(benchmark_main PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-Wno-c2y-extensions>
  )
  CPMAddPackage(
    NAME ctre
    GITHUB_REPOSITORY hanickadot/compile-time-regular-expressions
    GIT_TAG v3.10.0
  )
  set(RE2C_BUILD_RE2D OFF CACHE BOOL "" FORCE)
  set(RE2C_BUILD_RE2GO OFF CACHE BOOL "" FORCE)
  set(RE2C_BUILD_RE2HS OFF CACHE BOOL "" FORCE)
  set(RE2C_BUILD_RE2JAVA OFF CACHE BOOL "" FORCE)
  set(RE2C_BUILD_RE2JS OFF CACHE BOOL "" FORCE)
  set(RE2C_BUILD_RE2OCAML OFF CACHE BOOL "" FORCE)
  set(RE2C_BUILD_RE2PY OFF CACHE BOOL "" FORCE)
  set(RE2C_BUILD_RE2RUST OFF CACHE BOOL "" FORCE)
  set(RE2C_BUILD_RE2SWIFT OFF CACHE BOOL "" FORCE)
  set(RE2C_BUILD_RE2V OFF CACHE BOOL "" FORCE)
  set(RE2C_BUILD_RE2ZIG OFF CACHE BOOL "" FORCE)
  CPMAddPackage(
    NAME re2c_source
    GITHUB_REPOSITORY skvadrik/re2c
    GIT_TAG 4.3
  )
endif()
CPMAddPackage(
  NAME boost_pfr
  GITHUB_REPOSITORY boostorg/pfr
  GIT_TAG d64c6993fee26d39cac7d77ed0efe3e3b052a9f9
  OPTIONS "BOOST_USE_MODULES ON" "BUILD_TESTING OFF"
)

add_library(scan)
target_sources(
  scan
  PUBLIC FILE_SET CXX_MODULES FILES
    src/scan_tre.cc
    src/scan_core.cc
    src/scan_views.cc
    src/scan_compiler.cc
    src/scan_regex.cc
    src/scan_runtime.cc
    src/scan_range.cc
    src/scan_scanners.cc
    src/scan.cc
)
target_compile_features(scan PUBLIC cxx_std_23)
target_link_libraries(scan PUBLIC Boost::pfr)
target_compile_options(scan PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(scan PUBLIC
  $<$<CXX_COMPILER_ID:Clang>:-fconstexpr-steps=300000000>
  $<$<CXX_COMPILER_ID:Clang>:-fconstexpr-depth=4096>
)

# Clang evaluates constant expressions by walking the syntax tree, and is
# replacing that with a bytecode interpreter. A pattern is compiled entirely in
# constant evaluation here, so the difference is the difference between one
# build and another: measured on the five-group pattern this project's tests
# use, the evaluation takes 9.3 seconds by the tree walker and 2.9 by the
# interpreter -- a little over three times.
#
# It is still called experimental, and it is: it declines constructs the tree
# walker accepts. So it is a choice, on where it works.
option(SCAN_BYTECODE_CONSTANT_INTERPRETER
       "Evaluate constant expressions with clang's bytecode interpreter" OFF)
if(SCAN_BYTECODE_CONSTANT_INTERPRETER)
  # PRIVATE, not PUBLIC: it is a choice about how this library is compiled, and
  # it cannot be made for whoever uses it. A translation unit that imports this
  # and also includes a header textually -- a benchmark harness, another
  # header-only library -- gives the interpreter two views of the same standard
  # entity, and everything this library evaluates while compiling stops being a
  # constant expression.
  target_compile_options(scan PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-fexperimental-new-constant-interpreter>)
endif()

# The automaton, built while the program runs rather than while it is compiled.
#
# A pattern here becomes code: one constant evaluation of the determiniser for
# each pattern, and one instantiation for each state of the machine that walks
# it. That is what makes a scan cost nothing when it runs, and it is what makes
# a file with several patterns in it slow to build -- minutes, for a handful of
# fields. Turned around, the same determiniser is called as an ordinary
# function on first use and the walk is a loop over what it returns: the
# compiler is asked for one function instead of one per state, and a scan costs
# what reading a table costs.
#
# On when this repository is built by itself with the benchmarks off, which is
# the build that runs the tests. Off for anyone who consumes the library, and
# off wherever the benchmarks are -- there the compiled form is the thing being
# measured.
if(PROJECT_IS_TOP_LEVEL AND NOT SCAN_BUILD_BENCHMARKS)
  set(scan_runtime_automata_default ON)
else()
  set(scan_runtime_automata_default OFF)
endif()
option(SCAN_AUTOMATA_AT_RUNTIME
       "Build automata while the program runs, not while it is compiled"
       ${scan_runtime_automata_default})
if(SCAN_AUTOMATA_AT_RUNTIME)
  # PUBLIC: the choice is written into the module this builds, and whoever
  # imports it is reading the same one.
  target_compile_definitions(scan PUBLIC SCAN_AUTOMATA_AT_RUNTIME=1)
endif()

enable_testing()
# One pattern to a translation unit. A pattern is compiled while this file is
# being compiled, so two patterns in one file is two waits in a row, and
# separate files are two waits at once.
set(scan_tests
  a_field_that_matches_nothing_test
  a_field_wider_than_the_room_it_was_test
  a_head_and_what_follows_it_test
  a_head_of_what_is_read_once_test
  a_head_of_what_is_walked_test
  a_head_off_a_stream_and_what_stopped_it_test
  a_scan_that_did_not_test
  a_scan_that_is_asked_for_test
  a_shape_inside_a_record_test
  a_shape_off_a_stream_test
  a_shape_of_shapes_test
  a_subject_handed_to_a_search_test
  a_subject_handed_to_a_split_test
  a_subject_handed_to_each_test
  a_tokenize_that_is_a_search_under_test
  a_value_for_each_group_test
  a_walk_that_finds_nothing_test
  a_whole_subject_off_a_stream_test
  accepts_bounded_repetition_from_test
  applies_scanner_parameters_to_test
  as_many_as_the_input_turned_out_to_have_test
  assigned_with_the_rest_dropped_test
  borrows_vector_and_span_storage_test
  bounded_five_fields_test
  captures_nested_groups_in_opening_order_test
  characters_in_a_row_are_pointed_at_test
  collector_arguments_test
  collector_pusher_test
  collector_reuse_test
  collector_scanner_test
  commands_read_as_they_arrive_test
  ctre_compat_alternation_test
  ctre_compat_literals_test
  ctre_compat_repetition_test
  differential_short_input_corpus_test
  every_command_off_a_stream_test
  every_match_in_turn_test
  every_record_of_a_contiguous_input_test
  exposes_search_split_and_tokenize_test
  flat_shape_test
  handles_bounded_greedy_repetition_test
  in_pieces_test
  lazy_repetition_test
  leftmost_alternative_wins_inside_test
  list_own_test
  list_shape_test
  nothing_where_nothing_matched_test
  optimized_register_program_preserves_test
  past_space_test
  preserves_official_capture_and_range_test
  reads_no_further_than_the_record_test
  record_after_record_off_one_stream_test
  scanner_can_build_an_owning_pattern_test
  scanner_from_groups_test
  scans_at_compile_time_test
  scans_borrowed_contiguous_range_test
  splits_a_stream_on_spaces_test
  states_that_lead_back_test
  streams_sscanf_like_floating_test
  streams_unbounded_custom_value_test
  tagged_automata_test
  the_head_of_an_input_test
  the_walk_against_the_interpreter_test
  the_walk_said_outright_test
  the_leftmost_and_longest_test
  the_outer_branch_test
  the_pieces_between_test
  the_second_inner_branch_test
  the_throwing_form_still_throws_test
  unkept_place_test
  uses_custom_scanner_test
  uses_leftmost_greedy_for_adjacent_test
  values_call_test
  variant_default_test
  variant_field_test
  where_a_record_ends_off_a_stream_test
  where_a_record_ends_test
  written_as_exactly_two_test
  written_as_one_or_more_test)
foreach(one IN LISTS scan_tests)
  add_executable(${one} tests/${one}.cc)
  target_link_libraries(${one} PRIVATE scan gtest_main)
  add_test(NAME ${one} COMMAND ${one})
endforeach()

if(SCAN_BUILD_BENCHMARKS)
  # Everything a benchmark uses is a module.
  #
  # A translation unit that imports a module built against the `std` module and
  # also includes a standard header textually gives clang's bytecode
  # interpreter two views of the same entity, and nothing in that file
  # evaluates afterwards. The harness and the other engine are headers, so they
  # are wrapped once each and imported from then on -- which is also what lets
  # the benchmarks be compiled with the same evaluator as the library.
  # Every benchmark measures the same question of four engines, so all four
  # are here: this library, CTRE, RE2 and re2c. RE2 reads its pattern while
  # the program runs and brings abseil with it, which is a weight -- but the
  # benchmarks already build a whole scanner generator, and a comparison that
  # leaves out the engine most programs would reach for is not one.
  set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(ABSL_ENABLE_INSTALL ON CACHE BOOL "" FORCE)
  set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
  CPMAddPackage(
    NAME abseil
    GITHUB_REPOSITORY abseil/abseil-cpp
    GIT_TAG 20250127.0
  )
  CPMAddPackage(
    NAME re2_source
    GITHUB_REPOSITORY google/re2
    GIT_TAG 2024-07-02
  )
  add_library(bench_re2)
  target_sources(bench_re2 PUBLIC FILE_SET CXX_MODULES FILES
    benchmarks/re2_engine.cppm)
  target_link_libraries(bench_re2 PUBLIC re2::re2)
  target_compile_features(bench_re2 PUBLIC cxx_std_23)

  add_library(bench_harness)
  target_sources(bench_harness PUBLIC FILE_SET CXX_MODULES FILES
    benchmarks/harness.cppm)
  target_link_libraries(bench_harness PUBLIC benchmark::benchmark)
  target_compile_features(bench_harness PUBLIC cxx_std_23)

  add_library(bench_inputs)
  target_sources(bench_inputs PUBLIC FILE_SET CXX_MODULES FILES
    benchmarks/inputs.cppm)
  target_compile_features(bench_inputs PUBLIC cxx_std_23)

  add_library(ctre_module)
  target_sources(ctre_module PUBLIC FILE_SET CXX_MODULES FILES
    "${ctre_SOURCE_DIR}/ctre.cppm")
  target_link_libraries(ctre_module PUBLIC ctre)
  target_compile_features(ctre_module PUBLIC cxx_std_23)

  # One pattern per benchmark, one benchmark per executable: compiling a
  # pattern is a constant evaluation and costs seconds, so a translation unit
  # holding ten of them costs ten times that whenever one is touched.
  function(scan_add_benchmark name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "NO_RE2C" "" "")
    set(sources "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/${name}_benchmark.cc")
    if(NOT arg_NO_RE2C)
      set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated/${name}_re2c.cc")
      add_custom_command(
        OUTPUT "${generated}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND $<TARGET_FILE:re2c> -W -o "${generated}"
                "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/re2c/${name}.re"
        DEPENDS re2c "benchmarks/re2c/${name}.re"
        VERBATIM)
      list(APPEND sources "${generated}")
    endif()
    add_executable(${name}_benchmark ${sources})
    target_link_libraries(${name}_benchmark
      PRIVATE scan bench_harness bench_inputs ctre_module bench_re2
              benchmark::benchmark_main)
    target_compile_options(${name}_benchmark PRIVATE $<$<CONFIG:Release>:-O3>)
    # And not the bytecode interpreter, whatever the option says.
    #
    # A benchmark imports this library, which is built against the `std`
    # module, and also the harness and the other engine, which are modules
    # wrapping their own headers. Two views of the standard library reach the
    # translation unit that way, and the interpreter then reads values made
    # inside a module as uninitialised -- every pattern in the file stops being
    # a constant expression. Nothing here can arrange otherwise: those headers
    # are what those libraries are.
    #
    # It costs nothing measured. Which evaluator compiles a pattern decides how
    # long the compilation takes, not what runs afterwards.
  endfunction()

  # Recognition: the whole input, matched and not taken apart.
  scan_add_benchmark(word)
  scan_add_benchmark(timestamp)
  scan_add_benchmark(csv)
  scan_add_benchmark(address)
  # Extraction: the fields are wanted, not only the answer.
  scan_add_benchmark(captures)
  # Neither: two loops written by hand, to price the shape itself.
  scan_add_benchmark(shape NO_RE2C)
  # And against the one in the standard library, which is what a C++ program
  # reaches for when it wants numbers out of a string.
  scan_add_benchmark(sscanf NO_RE2C)

  add_custom_target(benchmarks DEPENDS
    word_benchmark timestamp_benchmark csv_benchmark address_benchmark
    captures_benchmark shape_benchmark sscanf_benchmark)

endif()
