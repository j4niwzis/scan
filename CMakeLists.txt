cmake_minimum_required(VERSION 4.3.4 FATAL_ERROR)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
    "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
set(CMAKE_CXX_MODULE_STD ON)

project(tre_tdfa LANGUAGES CXX)

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

add_library(tre_tdfa)
target_sources(tre_tdfa PUBLIC FILE_SET CXX_MODULES FILES src/tre.cc)
target_compile_features(tre_tdfa PUBLIC cxx_std_23)
target_compile_options(tre_tdfa PRIVATE -Wall -Wextra -Wpedantic)

add_library(scan)
target_sources(
  scan
  PUBLIC FILE_SET CXX_MODULES FILES
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
target_link_libraries(scan PUBLIC tre_tdfa Boost::pfr)
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
  target_compile_options(scan PUBLIC
    $<$<CXX_COMPILER_ID:Clang>:-fexperimental-new-constant-interpreter>)
endif()

enable_testing()
add_executable(tre_tdfa_test tests/tre_test.cc tests/ctre_compat_test.cc)
target_link_libraries(tre_tdfa_test PRIVATE tre_tdfa scan gtest_main)
add_test(NAME tre_tdfa_test COMMAND tre_tdfa_test)

add_executable(tre_runtime_test tests/tre_runtime_test.cc)
target_link_libraries(tre_runtime_test PRIVATE tre_tdfa gtest_main)
add_test(NAME tre_runtime_test COMMAND tre_runtime_test)

if(SCAN_BUILD_BENCHMARKS)
  set(re2c_benchmark_source
      "${CMAKE_CURRENT_BINARY_DIR}/generated/regex_re2c.cc")
  add_custom_command(
    OUTPUT "${re2c_benchmark_source}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND $<TARGET_FILE:re2c> -W -o "${re2c_benchmark_source}"
            "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/regex_re2c.re"
    DEPENDS re2c benchmarks/regex_re2c.re
    VERBATIM
  )
  add_executable(regex_benchmark
    benchmarks/regex_benchmark.cc
    "${re2c_benchmark_source}"
  )
  target_include_directories(regex_benchmark PRIVATE benchmarks)
  target_link_libraries(regex_benchmark PRIVATE
    scan benchmark::benchmark_main ctre
  )
  target_compile_options(regex_benchmark PRIVATE
    $<$<CONFIG:Release>:-O3>
  )

  set(codegen_re2c_source
      "${CMAKE_CURRENT_BINARY_DIR}/generated/codegen_re2c.cc")
  add_custom_command(
    OUTPUT "${codegen_re2c_source}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND $<TARGET_FILE:re2c> -W -o "${codegen_re2c_source}"
            "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/codegen_re2c.re"
    DEPENDS re2c benchmarks/codegen_re2c.re
    VERBATIM)
  add_executable(codegen_benchmark
    benchmarks/codegen_benchmark.cc
    "${codegen_re2c_source}")
  target_include_directories(codegen_benchmark PRIVATE benchmarks)
  target_link_libraries(codegen_benchmark PRIVATE
    scan benchmark::benchmark_main ctre)
  target_compile_options(codegen_benchmark PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-O3;-flto>)
  target_link_options(codegen_benchmark PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-flto>)

  add_executable(bounded_re2c_benchmark
    benchmarks/bounded_re2c_benchmark.cc
    "${re2c_benchmark_source}")
  target_include_directories(bounded_re2c_benchmark PRIVATE benchmarks)
  target_link_libraries(bounded_re2c_benchmark PRIVATE benchmark::benchmark_main)
  target_compile_options(bounded_re2c_benchmark PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-O3;-flto>)
  target_link_options(bounded_re2c_benchmark PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-flto>)

  add_executable(sentinel_email_benchmark
    benchmarks/sentinel_email_benchmark.cc
    "${re2c_benchmark_source}")
  target_include_directories(sentinel_email_benchmark PRIVATE benchmarks)
  target_link_libraries(sentinel_email_benchmark PRIVATE
    scan benchmark::benchmark_main)
  target_compile_options(sentinel_email_benchmark PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-O3;-flto>)
  target_link_options(sentinel_email_benchmark PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-flto>)

  add_executable(capture_benchmark
    benchmarks/capture_benchmark.cc
    "${re2c_benchmark_source}")
  target_include_directories(capture_benchmark PRIVATE benchmarks)
  target_link_libraries(capture_benchmark PRIVATE
    scan benchmark::benchmark_main)
  target_compile_options(capture_benchmark PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-O3;-flto>)
  target_link_options(capture_benchmark PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-flto>)
endif()
