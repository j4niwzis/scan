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

enable_testing()
add_executable(tre_tdfa_test tests/tre_test.cc tests/ctre_compat_test.cc)
target_link_libraries(tre_tdfa_test PRIVATE scan gtest_main)
add_test(NAME tre_tdfa_test COMMAND tre_tdfa_test)

add_executable(tre_runtime_test tests/tre_runtime_test.cc)
target_link_libraries(tre_runtime_test PRIVATE scan gtest_main)
add_test(NAME tre_runtime_test COMMAND tre_runtime_test)

if(SCAN_BUILD_BENCHMARKS)
  # Everything a benchmark uses is a module.
  #
  # A translation unit that imports a module built against the `std` module and
  # also includes a standard header textually gives clang's bytecode
  # interpreter two views of the same entity, and nothing in that file
  # evaluates afterwards. The harness and the other engine are headers, so they
  # are wrapped once each and imported from then on -- which is also what lets
  # the benchmarks be compiled with the same evaluator as the library.
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
      PRIVATE scan bench_harness bench_inputs ctre_module
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

  add_custom_target(benchmarks DEPENDS
    word_benchmark timestamp_benchmark csv_benchmark address_benchmark
    captures_benchmark shape_benchmark)
endif()
