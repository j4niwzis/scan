cmake_minimum_required(VERSION 4.2 FATAL_ERROR)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/import-std.cmake)
set(CMAKE_CXX_EXTENSIONS OFF)
# Said as a property of the build and not only of the targets, because the
# dependencies read it. Boost.PFR's module unit asks `import std` of itself
# when the build says both that `import std` works and which standard it works
# for -- `CMAKE_CXX_MODULE_STD` and `CMAKE_CXX_STANDARD`. Left unsaid, the
# second answer is empty, and the module falls back to including `<functional>`
# and the rest of the standard library textually into its global module
# fragment.
#
# Which would be nothing but a slower build, except that the standard library
# then exists twice in one program: once in the module `std` everything here
# imports, and once in that fragment. Clang miscompiles a lambda in an
# out-of-line member template that is merged from two such copies -- it drops
# the capture and reads an uninitialised local in its place
# (llvm/llvm-project#221548), and `std::vector::emplace_back` in libc++ is
# written exactly that way. A `push_back` in a program built the other way
# writes through a pointer nobody set.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# scan -- regular expressions and scanning, as C++23 modules.
#
# A pattern becomes a machine while the program is compiled, and the machine is
# written out as code rather than walked as a table. Everything here is one
# library and eight module partitions of it; the tests and the benchmarks are
# what this file builds beside it.

# Where everything this needs comes from.
#
# cmake-everywhere answers find_package: from the machine when the machine has
# the library, and from a pinned source when it does not. Named before
# project(), because that is the only place a dependency provider may be
# installed from -- and only when nothing is answering already, which is the
# case where this library is built inside somebody else's build and that build
# brought its own.
if(NOT COMMAND cme_declare_port AND NOT CMAKE_PROJECT_TOP_LEVEL_INCLUDES)
  set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES
      ${CMAKE_CURRENT_LIST_DIR}/cmake/get_cme.cmake)
endif()

project(scan VERSION 0.1 LANGUAGES CXX)

# CMAKE_PROJECT_TOP_LEVEL_INCLUDES is read by the top-level project() call and
# by no other, so a build configured from a subdirectory of this has no
# provider and no sign of it: find_package(boost_pfr) then falls through to
# whatever the machine happens to have, and what finally says something is a
# compiler error about Boost rather than about this.
if(NOT COMMAND cme_provider)
  message(FATAL_ERROR
    "cmake-everywhere is not installed in this build, so nothing answers "
    "find_package(boost_pfr). CMAKE_PROJECT_TOP_LEVEL_INCLUDES is only read "
    "by the top-level project() call: configure this directory, and not a "
    "subdirectory of it.")
endif()

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds not allowed.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SCAN_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
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

# ---- What this reads a shape with ----
#
# Boost.PFR, as a module: the fields of an aggregate, by nothing but the type.
# It is asked for as modules, and a copy of it installed on the machine is
# headers -- so this one is built rather than taken, because what it is taken
# as decides whether `import boost.pfr` means anything.
# Asked for as a module, which is a thing the port can be rather than an
# option set beside it: whoever resolves the port first is the one an option
# would have had to come before, and that is not always this file -- a build
# that fetches this library resolves what it needs before its CMakeLists is
# added at all.
# And not the copy on the machine, whatever it is: what is installed anywhere
# is headers, and a module is what this compiles against. Said as a rule of
# this build rather than of the port, because a Boost.PFR built as a module is
# a perfectly good answer if a machine ever has one.
set(CME_SYSTEM_boost_pfr OFF CACHE BOOL
    "Boost.PFR is built here, because it is wanted as a module")
# Said to the port and not only around it. Boost.PFR is built on its own, in
# a configure of its own, and what reaches that one is what is named here --
# everything else is the build this file is in, which the port never sees. It
# is also what names the result: the options are part of the identity a built
# port is stored under, so a build that says something different gets a
# different port rather than the one already lying there.
#
# And what it has to be told is which standard this is and that `import std`
# works, because those are what its module unit asks before deciding whether
# to import the standard library or to include it. Left unanswered it
# includes -- and then the library exists twice in one program, once in the
# module `std` everything here imports and once in that global module
# fragment. Clang drops the capture of a lambda in an out-of-line member
# template merged from two such copies and reads an uninitialised local in
# its place (llvm/llvm-project#221548); `std::vector::emplace_back` in libc++
# is written that way, so a `push_back` writes through a pointer nobody set.
cme_options(boost-pfr
    "BUILD_TESTING OFF"
    "CMAKE_CXX_STANDARD 23"
    "CMAKE_CXX_MODULE_STD 1"
    "CMAKE_EXPERIMENTAL_CXX_IMPORT_STD ${CMAKE_EXPERIMENTAL_CXX_IMPORT_STD}")
find_package(boost_pfr REQUIRED COMPONENTS modules)

# Said out loud, because getting this wrong is not a build error.
#
# Without it the module unit includes the standard library into its own global
# module fragment, the library exists twice in one program, and clang drops the
# capture of a lambda merged from the two copies. Nothing fails to compile; a
# `std::vector` push_back writes through a pointer nobody set. So the answer is
# printed where a build log will show it.
if(TARGET Boost::pfr)
  get_target_property(scan_pfr_definitions Boost::pfr
                      INTERFACE_COMPILE_DEFINITIONS)
  message(STATUS "scan: Boost.PFR was built with: ${scan_pfr_definitions}")
  if(NOT scan_pfr_definitions MATCHES "BOOST_PFR_USE_STD_MODULE")
    message(WARNING
      "scan: Boost.PFR is not using `import std`, so the standard library is "
      "in this program twice. See llvm/llvm-project#221548.")
  endif()
endif()

add_library(scan)
add_library(scan::scan ALIAS scan)
target_sources(
  scan
  PUBLIC FILE_SET CXX_MODULES FILES
    src/scan_tre.cc
    src/scan_core.cc
    src/scan_views.cc
    src/scan_compiler.cc
    src/scan_regex.cc
    src/scan_runtime.cc
    src/scan_shape.cc
    src/scan_range.cc
    src/scan_scanners.cc
    src/scan.cc
)
target_compile_features(scan PUBLIC cxx_std_23)
# In this build only. A Boost.PFR the provider built is a target of this build
# directory, and install(EXPORT) refuses to export something that links it --
# rightly, since what would be installed is a library whose dependency exists
# nowhere else. An installed copy names it the way a machine can answer for it,
# which is what the config file below writes.
target_link_libraries(scan PUBLIC $<BUILD_INTERFACE:Boost::pfr>)
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
  target_compile_definitions(scan PUBLIC
    $<BUILD_INTERFACE:SCAN_AUTOMATA_AT_RUNTIME=1>)
endif()

# ---- What this library is, said by the library ----
#
# A consumer writes find_package(scan) and this says the rest: where it comes
# from, what it needs, and what it produces. The declaration is installed
# beside the library, so a prefix that has scan has the description of scan --
# which is what lets a machine answer for it instead of everyone fetching it
# again.
if(COMMAND cme_declare_port)
  cme_declare_port(
    NAME scan
    PROVIDES scan
    VERSION ${PROJECT_VERSION}
    GITHUB_REPOSITORY j4niwzis/scan
    GIT_TAG main
    LICENSE GPL-3.0-only
    # Boost.PFR as a module, said here as well as where it is asked for.
    #
    # Whoever fetches this resolves what it needs before this file is added,
    # so a build that learns about the module only from the find_package
    # below has already answered for Boost.PFR by then -- as headers, which
    # is not the same library -- and has to be configured a second time. Said
    # in the port, the first answer is the right one.
    DEPENDS "boost-pfr[modules]"
    TARGETS scan::scan
  )
endif()

# What this can optionally be, and what each of those needs.
#
# A feature rather than a dependency: what a library needs to run its own
# tests is not what a program linking it needs, and a port that said so would
# have every consumer of this fetch a testing framework. Said as a feature, it
# is recorded as a fact about this copy and not about the library.
if(COMMAND cme_port_feature)
  cme_port_feature(scan tests
    SUMMARY "the test suite"
    DEPENDS googletest-modules
    OPTIONS "SCAN_BUILD_TESTS ON")
  cme_port_feature(scan benchmarks
    SUMMARY "the comparison against CTRE, RE2 and re2c"
    DEPENDS benchmark ctre re2
    OPTIONS "SCAN_BUILD_BENCHMARKS ON")
  cme_port_feature(scan fuzzer
    SUMMARY "the differential fuzzer against RE2"
    DEPENDS re2
    OPTIONS "SCAN_BUILD_FUZZER ON")
  set(scan_features "")
  if(SCAN_BUILD_TESTS)
    list(APPEND scan_features tests)
  endif()
  if(SCAN_BUILD_BENCHMARKS)
    list(APPEND scan_features benchmarks)
  endif()
  if(SCAN_BUILD_FUZZER)
    list(APPEND scan_features fuzzer)
  endif()
  if(scan_features)
    cme_features(scan ${scan_features})
  endif()
endif()

# ---- The fuzzer, and the engine it is compared against ----
#
# Neither of these is in anybody's registry, so the ports are written here,
# which is where they are used. A name and where it is from is the whole of a
# port for a library that says the rest about itself.
if(SCAN_BUILD_FUZZER)
  cme_declare_port(NAME abseil PROVIDES absl
    GITHUB_REPOSITORY abseil/abseil-cpp
    GIT_TAG 20250127.0
    LICENSE Apache-2.0)
  cme_options(abseil "ABSL_ENABLE_INSTALL ON" "ABSL_PROPAGATE_CXX_STD ON")
  cme_declare_port(NAME re2 PROVIDES re2
    GITHUB_REPOSITORY google/re2
    GIT_TAG 2024-07-02
    LICENSE BSD-3-Clause
    DEPENDS absl
    TARGETS re2::re2)
  cme_options(re2 "RE2_BUILD_TESTING OFF")
  find_package(re2 REQUIRED)

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

# ---- The tests, and what runs them ----
#
# Not the googletest everybody has: this one is a fork that builds it as a
# module, because a test here imports `std` and a header that includes the
# standard library textually gives the translation unit two views of it.
if(SCAN_BUILD_TESTS)
  # And never the one on the machine. A googletest installed anywhere is the
  # one that is a header, and what these tests import is a module -- so there
  # is no copy to find, whatever a prefix has under that name, and looking is
  # how a build ends up asking FindGTest about a library it was never going
  # to accept.
  cme_declare_port(NAME googletest-modules PROVIDES GTest
    GITHUB_REPOSITORY j4niwzis/googletest-modules
    GIT_TAG 706920f1d230f0316d271466d8885735be3e300e
    LICENSE BSD-3-Clause
    SYSTEM NEVER
    TARGETS gtest_main)
  cme_options(googletest-modules
    "BUILD_GMOCK OFF" "INSTALL_GTEST OFF"
    "gtest_build_tests OFF" "gtest_build_samples OFF")
  find_package(GTest REQUIRED)
  # The fork builds gtest as a module and names the target the way googletest
  # named it before the aliases: a copy from a prefix is `GTest::gtest_main`,
  # and one built here is `gtest_main`.
  if(TARGET GTest::gtest_main)
    set(scan_gtest GTest::gtest_main)
  elseif(TARGET gtest_main)
    set(scan_gtest gtest_main)
  else()
    # Said here rather than found out one file at a time. A copy that answered
    # for googletest without leaving a target behind is one the tests link by
    # name and compile without the headers of, and what says so is every test
    # failing to find `gtest/gtest-macros.h`.
    message(FATAL_ERROR
      "googletest answered find_package, and neither GTest::gtest_main nor "
      "gtest_main is a target in this build. A stored copy built by another "
      "compiler is the usual reason: clear the cmake-everywhere store.")
  endif()
  # And that what it left behind can be compiled against.
  #
  # Not of that target alone: a target carries only what it was told to carry,
  # and gtest_main was told nothing -- the headers belong to gtest, which it
  # links, and a build resolves that on the way to compiling. So what is asked
  # here is whether anything in the link interface has them, which is what a
  # test would find.
  #
  # A copy where nothing does is one whose headers are somewhere this build was
  # not told about, and every test then fails on the same missing header --
  # which says nothing about where it came from.
  set(scan_gtest_reachable ${scan_gtest})
  set(scan_gtest_includes "")
  while(scan_gtest_reachable AND NOT scan_gtest_includes)
    list(POP_FRONT scan_gtest_reachable scan_gtest_one)
    if(NOT TARGET ${scan_gtest_one})
      continue()
    endif()
    get_target_property(scan_gtest_includes ${scan_gtest_one}
                        INTERFACE_INCLUDE_DIRECTORIES)
    if(scan_gtest_includes STREQUAL "scan_gtest_includes-NOTFOUND")
      set(scan_gtest_includes "")
    endif()
    get_target_property(scan_gtest_linked ${scan_gtest_one}
                        INTERFACE_LINK_LIBRARIES)
    if(scan_gtest_linked AND NOT scan_gtest_linked STREQUAL
                             "scan_gtest_linked-NOTFOUND")
      list(APPEND scan_gtest_reachable ${scan_gtest_linked})
    endif()
  endwhile()
  if(NOT scan_gtest_includes)
    message(FATAL_ERROR
      "the googletest this build answered with (${scan_gtest}) carries no "
      "include directories, and neither does anything it links, so nothing "
      "can compile against it. A stored copy that lost them is the usual "
      "reason: clear the cmake-everywhere store, or configure with "
      "-DCME_STORE_ENABLED=OFF.")
  endif()
endif()

enable_testing()
# One pattern to a translation unit. A pattern is compiled while this file is
# being compiled, so two patterns in one file is two waits in a row, and
# separate files are two waits at once.
set(scan_tests
  a_field_that_matches_nothing_test
  a_group_taken_over_and_over_test
  a_field_wider_than_the_room_it_was_test
  a_head_and_what_follows_it_test
  a_head_of_what_is_read_once_test
  a_head_of_what_is_walked_test
  a_head_off_a_stream_and_what_stopped_it_test
  a_scan_that_did_not_test
  a_scan_that_is_asked_for_test
  a_shape_inside_a_record_test
  a_shape_off_a_stream_test
  a_leaf_that_reads_its_groups_in_a_format_test
  a_numeral_read_by_its_groups_test
  a_fold_of_your_own_test
  a_failure_of_your_own_test
  a_list_of_your_own_test
  a_shape_of_shapes_test
  a_sum_of_your_own_test
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
  read_once_and_in_pieces_agree_on_a_list_test
  read_once_and_in_pieces_agree_on_a_row_test
  read_once_and_in_pieces_agree_on_a_shape_test
  record_after_record_off_one_stream_test
  scanner_by_group_test
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
if(SCAN_BUILD_TESTS)
  foreach(one IN LISTS scan_tests)
    add_executable(${one} tests/${one}.cc)
    target_link_libraries(${one} PRIVATE scan ${scan_gtest})
    add_test(NAME ${one} COMMAND ${one})
  endforeach()
endif()

if(SCAN_BUILD_BENCHMARKS)
  # Built the way the thing being measured is meant to be built.
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options($<$<CONFIG:Release>:-flto>)
    add_link_options($<$<CONFIG:Release>:-flto>)
  endif()

  # The harness, the two engines and the generator. Ports written here because
  # they are used here, and each is a name and where it comes from.
  cme_declare_port(NAME benchmark PROVIDES benchmark
    GITHUB_REPOSITORY google/benchmark
    GIT_TAG v1.9.4
    LICENSE Apache-2.0
    TARGETS benchmark::benchmark benchmark::benchmark_main)
  cme_options(benchmark
    "BENCHMARK_ENABLE_TESTING OFF" "BENCHMARK_ENABLE_GTEST_TESTS OFF"
    "BENCHMARK_ENABLE_INSTALL OFF" "BENCHMARK_ENABLE_WERROR OFF")
  find_package(benchmark REQUIRED)
  foreach(one IN ITEMS benchmark benchmark_main)
    if(TARGET ${one})
      target_compile_options(${one} PRIVATE
        $<$<CXX_COMPILER_ID:Clang>:-Wno-c2y-extensions>)
    endif()
  endforeach()

  cme_declare_port(NAME ctre PROVIDES ctre
    GITHUB_REPOSITORY hanickadot/compile-time-regular-expressions
    GIT_TAG v3.10.0
    LICENSE Apache-2.0
    TARGETS ctre::ctre)
  find_package(ctre REQUIRED)

  if(NOT SCAN_BUILD_FUZZER)
    cme_declare_port(NAME abseil PROVIDES absl
      GITHUB_REPOSITORY abseil/abseil-cpp
      GIT_TAG 20250127.0
      LICENSE Apache-2.0)
    cme_options(abseil "ABSL_ENABLE_INSTALL ON" "ABSL_PROPAGATE_CXX_STD ON")
    cme_declare_port(NAME re2 PROVIDES re2
      GITHUB_REPOSITORY google/re2
      GIT_TAG 2024-07-02
      LICENSE BSD-3-Clause
      DEPENDS absl
      TARGETS re2::re2)
    cme_options(re2 "RE2_BUILD_TESTING OFF")
    find_package(re2 REQUIRED)
  endif()

  # The generator, which is a program and not a library: what is wanted is a
  # command to run, so a copy on the machine is as good as one built here and
  # is looked for first.
  find_program(SCAN_RE2C_COMMAND re2c)
  if(NOT SCAN_RE2C_COMMAND)
    cme_declare_port(NAME re2c PROVIDES re2c
      GITHUB_REPOSITORY skvadrik/re2c
      GIT_TAG 4.3
      LICENSE Unlicense
      TARGETS re2c)
    cme_options(re2c
      "RE2C_BUILD_RE2D OFF" "RE2C_BUILD_RE2GO OFF" "RE2C_BUILD_RE2HS OFF"
      "RE2C_BUILD_RE2JAVA OFF" "RE2C_BUILD_RE2JS OFF" "RE2C_BUILD_RE2OCAML OFF"
      "RE2C_BUILD_RE2PY OFF" "RE2C_BUILD_RE2RUST OFF" "RE2C_BUILD_RE2SWIFT OFF"
      "RE2C_BUILD_RE2V OFF" "RE2C_BUILD_RE2ZIG OFF")
    find_package(re2c REQUIRED)
    set(SCAN_RE2C_COMMAND $<TARGET_FILE:re2c>)
    set(SCAN_RE2C_DEPENDS re2c)
  else()
    set(SCAN_RE2C_DEPENDS "")
  endif()

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
        COMMAND ${SCAN_RE2C_COMMAND} -W -o "${generated}"
                "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/re2c/${name}.re"
        DEPENDS ${SCAN_RE2C_DEPENDS} "benchmarks/re2c/${name}.re"
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
  # The same fields off a subject that can only be read once, gathered three
  # ways: into strings, into room said in advance, and into numbers.
  scan_add_benchmark(read_once NO_RE2C)
  # Neither: two loops written by hand, to price the shape itself.
  scan_add_benchmark(shape NO_RE2C)
  # And against the one in the standard library, which is what a C++ program
  # reaches for when it wants numbers out of a string.
  scan_add_benchmark(sscanf NO_RE2C)

  add_custom_target(benchmarks DEPENDS
    word_benchmark timestamp_benchmark csv_benchmark address_benchmark
    captures_benchmark read_once_benchmark shape_benchmark sscanf_benchmark)

endif()

# ---- Installing it ----
#
# The module interface units go with it: a consumer of a C++20 module library
# compiles its own binary module interface from the source, so an installed
# library that keeps its sources to itself cannot be consumed at all.
# Installing is what a build of this library does; a build that embeds it does
# not.
#
# Inside another build its dependency is that build's target -- Boost.PFR
# comes from a port -- and exporting a target that links one of those is an
# error, rightly: what would be installed is a library whose dependency exists
# only in the build directory it came from.
option(SCAN_INSTALL "Install scan and its CMake package" ${PROJECT_IS_TOP_LEVEL})
if(SCAN_INSTALL)
  include(GNUInstallDirs)
  install(TARGETS scan
    EXPORT scanTargets
    FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_LIBDIR}/scan/modules
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(EXPORT scanTargets
    NAMESPACE scan::
    CXX_MODULES_DIRECTORY modules
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/scan)
  # Whatever the exported target names has to be found again by whoever
  # includes this, in the same words -- and whoever includes this may have no
  # provider at all, which is the case this file is written for: a consumer
  # with nothing but a prefix.
  # And Boost.PFR put back on the target, since the exported one carries it
  # only in the build interface. A consumer of this compiles the module
  # interface units that are installed beside it, and those say
  # `import boost.pfr`.
  # Asked for only where nothing has answered for it already: a consumer with
  # a provider has the port added by the provider, and adding it again is two
  # copies of the same target.
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/scanConfig.cmake"
    "include(CMakeFindDependencyMacro)\n"
    "if(NOT TARGET Boost::pfr)\n"
    "  find_dependency(boost_pfr)\n"
    "endif()\n"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/scanTargets.cmake\")\n"
    "set_property(TARGET scan::scan APPEND PROPERTY\n"
    "             INTERFACE_LINK_LIBRARIES Boost::pfr)\n")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/scanConfig.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/scan)
endif()
