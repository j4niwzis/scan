cmake_minimum_required(VERSION 4.2 FATAL_ERROR)

# What was asked of this, where a provider has already said it.
#
# A project that cannot take modules asks for them as headers, and the switch
# that decides that has to be answered before the next line -- which turns
# `import std` on for everything in this build, including that project's own
# targets. A provider puts the request here before adding this file; a build
# saying it with -D says the same thing under the same name, and either way it
# is read before anything is decided.
#
# Asked for, the headers are what this build makes: not the headers as well,
# and not the headers only where nothing else had an opinion. A copy carrying
# both shapes is what a build of this library makes for whoever installs it,
# and a project that asked for one shape is not that. So the ask decides it,
# over anything this build was told by other means -- and says so where it
# overrules something, because a switch that was set and did not hold is worth
# a line in a log.
if("headers" IN_LIST CME_FEATURES_scan AND
   NOT "modules" IN_LIST CME_FEATURES_scan)
  if(SCAN_MODULES)
    message(STATUS
      "scan: the headers were asked for, so the module interface units are "
      "not built here, and SCAN_MODULES is off for this build whatever it was "
      "set to.")
  endif()
  set(SCAN_MODULES OFF)
endif()

# And what this build can compile at all.
#
# `import std` is behind a gate CMake reads once, while it scans the toolchain
# -- which happens where CXX is enabled, in a consumer's project() call, before
# this file is read at all. Opening it below is then opening it too late: the
# targets ask for the module `std`, the scan that would have found it never
# ran, and the build stops at generate time saying that the support "must be
# set before CXX is enabled", about a project() call that is not in this file.
#
# CXX already loaded means an enclosing project enabled it. The gate already
# open means that project opened it first, which is what a project that wants
# these modules does -- scan's own cmake/import-std.cmake, read through
# CMAKE_PROJECT_TOP_LEVEL_INCLUDES. Loaded and closed is a build where the
# interface units cannot be compiled whatever anyone sets, and the only
# question left is whether they were asked for: something nobody asked for is
# not worth stopping a build over, when the headers are right here and are the
# same library.
if(CMAKE_CXX_COMPILER_LOADED AND NOT CMAKE_EXPERIMENTAL_CXX_IMPORT_STD)
  if(SCAN_MODULES)
    message(FATAL_ERROR
      "scan: the modules were asked for and this build cannot compile them. "
      "`import std` is behind a gate CMake reads while it scans the toolchain, "
      "and CXX was enabled in this build before scan was added, so the gate "
      "can no longer be opened for it. Read scan's cmake/import-std.cmake "
      "before project(), through CMAKE_PROJECT_TOP_LEVEL_INCLUDES, or ask for "
      "the headers instead, with "
      "find_package(scan REQUIRED COMPONENTS headers). If this build "
      "configured with the modules once already, what it was told then is "
      "still in its CMakeCache.txt.")
  elseif(NOT DEFINED SCAN_MODULES)
    message(STATUS
      "scan: this build enabled CXX before scan was added, so `import std` "
      "cannot be turned on for it and the module interface units cannot be "
      "compiled here; the headers are used instead, which are the same "
      "library. Read scan's cmake/import-std.cmake before project() to build "
      "the modules.")
    set(SCAN_MODULES OFF)
  endif()
endif()

# Only where the modules are being built.
#
# `import std` is what the interface units are written against, and saying so
# turns it on for every target in this build. A build making only the headers
# asks the toolchain for nothing of the sort -- and asking makes a consumer's
# project answer a question it never had, because the gate CMake keeps that
# support behind has to be set before its own project() call. What it gets
# instead is a refusal about a toolchain that was never scanned, and a demand
# for a generator that can build modules it is not building.
#
# Read out of the cache rather than from the option below, which is declared
# after project() and does not exist yet. What a build was told with -D, and
# what a provider set before adding this, are both there already.
if(NOT DEFINED SCAN_MODULES OR SCAN_MODULES)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/import-std.cmake)
endif()
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

project(scan VERSION 0.1.0 LANGUAGES CXX)

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

# ---- Modules, or the headers made from them ----
#
# A project that cannot take modules -- an older CMake, a compiler whose
# support for them is not there yet, or a dependency graph that is not modules
# -- gets the same library as headers. They are generated from these very
# interface units, and they travel in the tree of a release rather than of a
# branch: a checkout of `main` does not carry them, and this says so rather
# than failing later with something about a missing file.
#
# There is one set of them, not two: the generated headers keep the condition
# around the import they came from, so the same files read Boost.PFR or take
# an aggregate apart with a binding pack -- whichever
# `SCAN_FIELDS_BY_BINDING_PACK` says here. A consumer on C++26 whose project
# is not modules wants the second, and nothing about that is unusual.
option(SCAN_MODULES
       "Build and install the module interface units, beside the headers" ON)

# The tests and the benchmarks are programs that import this library, so
# neither is a thing a build without the modules can make. Said here, where
# the answer is known, rather than failing later with something about a module
# that is not there.
if(NOT SCAN_MODULES AND (SCAN_BUILD_TESTS OR SCAN_BUILD_BENCHMARKS))
  message(FATAL_ERROR
    "scan: SCAN_MODULES is off, and the tests and the benchmarks import this "
    "library -- there is nothing for them to import. Turn those off, or "
    "leave SCAN_MODULES on.")
endif()

# ---- What this reads a shape with ----
#
# How a shape's fields are found, which is the one thing this library asks of
# anybody else.
#
# Boost.PFR reads an aggregate's members by probing what it can be built from,
# and works on every compiler this library otherwise needs. A structured
# binding pack names them outright -- `auto&& [...parts] = value;` -- which is
# C++26, needs no library at all, and makes this the only dependency-free
# answer. Off by default, because the compilers that have it are the newest
# ones and the other answer costs nothing to keep.
option(SCAN_FIELDS_BY_BINDING_PACK
       "Take an aggregate apart with a structured binding pack (C++26) rather than with Boost.PFR"
       OFF)

if(NOT SCAN_FIELDS_BY_BINDING_PACK AND NOT SCAN_MODULES)
  # The ordinary Boost.PFR: a header build reads its headers, which is what
  # every machine that has Boost already has. Nothing to build here, and
  # nothing to say about modules.
  find_package(boost_pfr REQUIRED)
elseif(NOT SCAN_FIELDS_BY_BINDING_PACK)
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
      # Said here as well, because the port decides it in a configure of its
      # own and there is no arguing with a decision already made. This build
      # imports the standard library as a module throughout -- it is what the
      # library itself is written against -- so the answer is not in doubt,
      # and the module unit is compiled in this build, where this is read.
      message(STATUS
        "scan: telling Boost.PFR to import std, which its own build did not")
      # Through the alias, where there is one: a build that took Boost.PFR
      # some other way names it `Boost::pfr` and means a target called
      # something else, and a property cannot be set on the name.
      get_target_property(scan_pfr_target Boost::pfr ALIASED_TARGET)
      if(NOT scan_pfr_target)
        set(scan_pfr_target Boost::pfr)
      endif()
      set_property(TARGET ${scan_pfr_target} APPEND
                   PROPERTY INTERFACE_COMPILE_DEFINITIONS
                            BOOST_PFR_USE_STD_MODULE)
    endif()
  endif()
endif()

# One set of headers, and it answers to both switches: the condition around an
# import is kept in the generated header, so the same files read Boost.PFR or
# take an aggregate apart with a binding pack, by what the build that reads
# them defines.
set(scan_headers_dir "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(NOT SCAN_MODULES)
  if(NOT EXISTS "${scan_headers_dir}/scan.hpp")
    message(FATAL_ERROR
      "scan: SCAN_MODULES is off and there is nothing at "
      "${scan_headers_dir}. They are generated from the module interface "
      "units by the run that builds this, and committed -- so a checkout has "
      "them. One that has not is a tree from before that was so, or one "
      "whose generation has not run yet.")
  endif()
  add_library(scan INTERFACE)
  add_library(scan::scan ALIAS scan)
  include(GNUInstallDirs)
  target_include_directories(scan INTERFACE
    $<BUILD_INTERFACE:${scan_headers_dir}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  # Everything below says PUBLIC where there is something to compile and
  # INTERFACE where there is not; the word is chosen once, here.
  set(scan_reaches INTERFACE)
else()
set(scan_reaches PUBLIC)
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
endif()

# What this target is called where it is installed. A copy can carry both
# shapes, so neither of them may take the name a consumer asks for: the
# package config makes `scan::scan` out of whichever was asked.
if(NOT SCAN_MODULES)
  set_target_properties(scan PROPERTIES EXPORT_NAME headers)
else()
  set_target_properties(scan PROPERTIES EXPORT_NAME modules)
endif()

# And the headers beside the modules, where a build made both. One copy that
# answers to either is what lets a project that cannot take modules use a
# library already installed, instead of building its own.
if(SCAN_MODULES AND EXISTS "${scan_headers_dir}/scan.hpp")
  include(GNUInstallDirs)
  add_library(scan_headers INTERFACE)
  set_target_properties(scan_headers PROPERTIES EXPORT_NAME headers)
  target_include_directories(scan_headers INTERFACE
    $<BUILD_INTERFACE:${scan_headers_dir}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_compile_features(scan_headers INTERFACE cxx_std_23)
  target_compile_options(scan_headers INTERFACE
    $<$<CXX_COMPILER_ID:Clang>:-fconstexpr-steps=300000000>
    $<$<CXX_COMPILER_ID:Clang>:-fconstexpr-depth=4096>)
endif()

target_compile_features(scan ${scan_reaches} cxx_std_23)

# Direct jumps out of a computed goto.
#
# The walk is one function with a label for every state and a computed goto
# between them, which is what keeps the marks it writes in machine registers:
# one frame, no call, nothing whose address is taken. What it costs is the jump
# itself -- an indirect jump is a jump the processor has to guess, and it
# guesses a scanner's next state badly, because the answer is the next
# character.
#
# LLVM will thread such a jump into a direct one per place it is reached from,
# which is exactly the switch a generator would have written, but it will not
# do it across the header of a loop unless it is told it may. A machine walked
# in a loop is nothing but that case.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mllvm -jump-threading-across-loop-headers=true"
                        SCAN_THREADS_JUMPS)
if(SCAN_THREADS_JUMPS)
  target_compile_options(scan ${scan_reaches}
    "SHELL:-mllvm -jump-threading-across-loop-headers=true")
endif()
# In this build only. A Boost.PFR the provider built is a target of this build
# directory, and install(EXPORT) refuses to export something that links it --
# rightly, since what would be installed is a library whose dependency exists
# nowhere else. An installed copy names it the way a machine can answer for it,
# which is what the config file below writes.
if(SCAN_FIELDS_BY_BINDING_PACK)
  # PUBLIC, and not only for this build: a module library installs its
  # interface units and whoever links it compiles them itself, so the answer
  # has to reach that compile as well as this one.
  target_compile_definitions(scan ${scan_reaches} SCAN_FIELDS_BY_BINDING_PACK=1)
else()
  target_link_libraries(scan ${scan_reaches} $<BUILD_INTERFACE:Boost::pfr>)
endif()
if(SCAN_MODULES)
  target_compile_options(scan PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_compile_options(scan ${scan_reaches}
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
  target_compile_definitions(scan ${scan_reaches}
    $<BUILD_INTERFACE:SCAN_AUTOMATA_AT_RUNTIME=1>)
endif()

# ---- What this library is, said by the library ----
#
# A consumer writes find_package(scan) and this says the rest: where it comes
# from, what it needs, and what it produces. The declaration is installed
# beside the library, so a prefix that has scan has the description of scan --
# which is what lets a machine answer for it instead of everyone fetching it
# again.
if(SCAN_FIELDS_BY_BINDING_PACK)
  set(scan_port_depends "")
elseif(NOT SCAN_MODULES)
  # The ordinary one. A header build reads Boost.PFR's headers, which is what
  # a machine that has Boost already carries -- there is nothing here that has
  # to be a module.
  set(scan_port_depends "boost-pfr")
else()
  set(scan_port_depends "boost-pfr[modules]")
endif()
if(COMMAND cme_declare_port)
  cme_declare_port(
    NAME scan
    PROVIDES scan
    VERSION ${PROJECT_VERSION}
    GITHUB_REPOSITORY j4niwzis/scan
    GIT_TAG main
    LICENSE GPL-3.0-only
    # Boost.PFR as a module, said here as well as where it is asked for --
    # and nothing at all where the fields come from a binding pack, which is
    # the whole of that switch: a copy built that way needs nobody.
    #
    # Whoever fetches this resolves what it needs before this file is added,
    # so a build that learns about the module only from the find_package
    # below has already answered for Boost.PFR by then -- as headers, which
    # is not the same library -- and has to be configured a second time. Said
    # in the port, the first answer is the right one.
    DEPENDS ${scan_port_depends}
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
  # The same library, for a project that cannot take modules. What it needs
  # is not said here: the two switches decide between three answers -- the
  # module units with Boost.PFR as a module, the headers with Boost.PFR as
  # headers, and either of those with neither -- and the library asks for
  # whichever it turned out to be, in its own CMakeLists, where the switches
  # have been read. A feature that named one of them would be naming it for
  # all three.
  cme_port_feature(scan headers AT_USE
    SUMMARY "the generated headers rather than the module interface units"
    OPTIONS "SCAN_MODULES OFF")

  cme_port_feature(scan binding-pack AT_USE
    SUMMARY "an aggregate's fields from a structured binding pack (C++26), which needs no Boost.PFR"
    OPTIONS "SCAN_FIELDS_BY_BINDING_PACK ON")

  # And what this build turned out to be, said as the features rather than
  # only as the options.
  #
  # A feature sets an option, and an option can also be set on its own -- so a
  # copy built with `-DSCAN_MODULES=OFF` and nothing else would install a
  # port recording no features, and look to the next project like an ordinary
  # copy. What is installed beside a library is a record of what that copy is,
  # and it has to say this.
  if(NOT SCAN_MODULES)
    cme_features(scan headers)
  endif()
  if(SCAN_FIELDS_BY_BINDING_PACK)
    cme_features(scan binding-pack)
  endif()

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
  cme_declare_port(NAME absl PROVIDES absl
    GITHUB_REPOSITORY abseil/abseil-cpp
    GIT_TAG 20250127.0
    LICENSE Apache-2.0)
  cme_options(absl "ABSL_ENABLE_INSTALL ON" "ABSL_PROPAGATE_CXX_STD ON")
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
  a_leaf_read_from_its_groups_beside_a_fold_test
  a_piece_beside_a_fold_test
  a_scan_that_did_not_test
  a_scanner_that_hands_the_state_back_test
  a_scan_that_is_asked_for_test
  a_shape_inside_a_record_test
  a_shape_off_a_stream_test
  a_leaf_that_reads_its_groups_in_a_format_test
  a_numeral_read_by_its_groups_test
  a_fold_of_your_own_test
  a_fold_whose_turn_has_groups_test
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

  # And with the pass a walk written out as labels wants -- aimed at this
  # library and at nothing else in the comparison.
  #
  # An `-mllvm` option handed to a link is handed to one code generation, and
  # under LTO that code generation is the whole program: every engine in it.
  # Measured, the option is worth two to four per cent here, nineteen to the
  # scanner re2c generates, and it costs CTRE twenty-two -- enough to reverse
  # which of the two is in front. A comparison cannot be made of numbers like
  # that.
  #
  # So each engine's code is generated by a backend run of its own. Their half
  # of a benchmark is compiled to bitcode like everything else and then taken
  # through a relocatable link, which runs the LTO backend over it and emits
  # machine code; what comes out is an object like any other, and the option on
  # the final link cannot reach inside it. Both halves get whole-program
  # optimisation. Only this one gets the option.
  #
  # RE2's own library is not covered by that -- it is bitcode in the final link
  # like anything else, and the option reaches it. Measured, it moves RE2 by
  # about one per cent, which is said here rather than left to be found.
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Asked of the linker, because the linker is who is given it. Put to the
    # compiler the question is answered by the wrong program: `clang` takes
    # `-mllvm` and the link then passes it on as `-Xlinker -mllvm`, which GNU
    # ld reads as an emulation mode named llvm and stops there. lld takes it
    # and hands it to the code generation, which is what it was asked for.
    include(CheckLinkerFlag)
    check_linker_flag(CXX
                      "LINKER:-mllvm,-jump-threading-across-loop-headers=true"
                      SCAN_THREADS_ACROSS_LOOP_HEADERS)
  endif()

  # The ports below are not module libraries, and some of them say C++11.
  # `CMAKE_CXX_MODULE_STD` is on for this project, and on means every target
  # gets the `std` module -- including theirs, where CMake then refuses:
  # "has C++ sources that use modules, but does not include cxx_std_20". So it
  # is off while they are configured and on again for what is ours.
  set(CMAKE_CXX_MODULE_STD 0)
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
    cme_declare_port(NAME absl PROVIDES absl
      GITHUB_REPOSITORY abseil/abseil-cpp
      GIT_TAG 20250127.0
      LICENSE Apache-2.0)
    cme_options(absl "ABSL_ENABLE_INSTALL ON" "ABSL_PROPAGATE_CXX_STD ON")
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
  # RE2 and abseil are ports above, answered by find_package like everything
  # else. Fetching them again here fetches them twice, and where the port is
  # answered from source the second copy cannot make its targets: `add_library
  # cannot create target "atomic_hook"`, and the same for every one of
  # abseil's, and for re2 itself. It stayed quiet in two places -- a build
  # directory that already had them, and a machine with RE2 installed, where
  # the port answers from there and builds nothing -- and nowhere else. The
  # benchmarks have never configured from clean.
  set(CMAKE_CXX_MODULE_STD 1)
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
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/ctre.cppm")
  # By the name the port says it produces, and not by the bare one.
  #
  # ctre's own CMake makes a target called `ctre` and an alias beside it, so
  # linking the bare name works for as long as ctre is built here as a
  # subproject. Answered any other way -- from the machine, from a prefix, from
  # anywhere that leaves only what was installed -- there is no target of that
  # name, CMake reads it as a library to look for, and the link fails on
  # -lctre: a library that is a header has no archive to find.
  target_link_libraries(ctre_module PUBLIC ctre::ctre)
  target_compile_features(ctre_module PUBLIC cxx_std_23)

  # One pattern per benchmark, one benchmark per executable: compiling a
  # pattern is a constant evaluation and costs seconds, so a translation unit
  # holding ten of them costs ten times that whenever one is touched.
  function(scan_add_benchmark name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "NO_RE2C;MIXED" "" "")
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/${name}_benchmark.cc")
    set(generated "")
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
    endif()

    if(arg_MIXED)
      # One file, two translation units. A code generation option is a property
      # of what reaches the backend together, so the engines are told apart
      # there and nowhere else: each half compiles only its own functions and
      # imports only its own modules, and each registers the rows it defines.
      add_library(${name}_theirs OBJECT "${source}" ${generated})
      target_compile_definitions(${name}_theirs PRIVATE SCAN_BENCH_THEIRS=1)
      target_link_libraries(${name}_theirs
        PRIVATE bench_harness bench_inputs ctre_module bench_re2)
      target_compile_options(${name}_theirs PRIVATE $<$<CONFIG:Release>:-O3>)

      set(prelinked "${CMAKE_CURRENT_BINARY_DIR}/${name}_theirs.o")
      add_custom_command(
        OUTPUT "${prelinked}"
        COMMAND ${CMAKE_CXX_COMPILER} $<$<CONFIG:Release>:-flto> -O3 -r -nostdlib
                $<TARGET_OBJECTS:${name}_theirs> -o "${prelinked}"
        DEPENDS ${name}_theirs
        COMMAND_EXPAND_LISTS VERBATIM
        COMMENT "their code generation, theirs")

      add_executable(${name}_benchmark "${source}" "${prelinked}")
      target_compile_definitions(${name}_benchmark PRIVATE SCAN_BENCH_OURS=1)
    else()
      add_executable(${name}_benchmark "${source}" ${generated})
    endif()

    target_link_libraries(${name}_benchmark
      PRIVATE scan bench_harness bench_inputs ctre_module bench_re2
              benchmark::benchmark_main)
    target_compile_options(${name}_benchmark PRIVATE $<$<CONFIG:Release>:-O3>)
    if(SCAN_THREADS_ACROSS_LOOP_HEADERS)
      target_link_options(${name}_benchmark PRIVATE
        "$<$<CONFIG:Release>:LINKER:-mllvm,-jump-threading-across-loop-headers=true>")
    endif()
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
  scan_add_benchmark(word MIXED)
  scan_add_benchmark(timestamp MIXED)
  scan_add_benchmark(csv MIXED)
  scan_add_benchmark(address MIXED)
  # Extraction: the fields are wanted, not only the answer.
  scan_add_benchmark(captures MIXED)
  # The same fields off a subject that can only be read once, gathered three
  # ways: into strings, into room said in advance, and into numbers.
  scan_add_benchmark(read_once NO_RE2C)
  # Neither: two loops written by hand, to price the shape itself.
  scan_add_benchmark(shape NO_RE2C)
  # And against the one in the standard library, which is what a C++ program
  # reaches for when it wants numbers out of a string.
  scan_add_benchmark(sscanf NO_RE2C)
  # The walk itself, against the same machine written out and against the same
  # reading written by hand. Not re2c: what is wanted here is this library's
  # own automaton written out, state for state, and re2c would generate its
  # own.
  scan_add_benchmark(fold NO_RE2C)

  add_custom_target(benchmarks DEPENDS
    word_benchmark timestamp_benchmark csv_benchmark address_benchmark
    captures_benchmark read_once_benchmark shape_benchmark sscanf_benchmark
    fold_benchmark)

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
  if(NOT SCAN_MODULES)
    # Nothing is compiled and nothing is archived: what is installed is the
    # headers and a target that points at them.
    install(TARGETS scan EXPORT scanTargets)
    install(DIRECTORY "${scan_headers_dir}/"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT scanTargets
      NAMESPACE scan::
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/scan)
  else()
    install(TARGETS scan
      EXPORT scanTargets
      FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_LIBDIR}/scan/modules
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    # And the headers with them. A copy that carries both is one a project
    # that cannot take modules can use as it stands.
    if(TARGET scan_headers)
      install(TARGETS scan_headers EXPORT scanTargets)
      install(DIRECTORY "${scan_headers_dir}/"
              DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    endif()
    install(EXPORT scanTargets
      NAMESPACE scan::
      CXX_MODULES_DIRECTORY modules
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/scan)
  endif()
  # What a consumer gets is chosen here, out of what this copy has.
  #
  # Neither shape may be called `scan::scan` where it is installed: a copy can
  # carry both, and the name has to be free for whichever was asked for. So
  # they are installed as `scan::modules` and `scan::headers`, and this makes
  # the name out of one of them -- by `COMPONENTS headers` if that was said,
  # and by the modules otherwise.
  #
  # A copy that has not got what was asked reports itself not found, with the
  # reason, which is what lets a provider go and build one that has.
  set(scan_config "include(CMakeFindDependencyMacro)\n")
  string(APPEND scan_config
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/scanTargets.cmake\")\n"
    "set(_scan_taken_as modules)\n"
    "if(\"headers\" IN_LIST scan_FIND_COMPONENTS)\n"
    "  set(_scan_taken_as headers)\n"
    "endif()\n"
    "if(NOT TARGET scan::\${_scan_taken_as})\n"
    "  set(scan_NOT_FOUND_MESSAGE\n"
    "      \"the scan installed here does not carry \${_scan_taken_as}\")\n"
    "  set(scan_FOUND FALSE)\n"
    "  return()\n"
    "endif()\n"
    "foreach(_scan_asked IN LISTS scan_FIND_COMPONENTS)\n"
    "  set(scan_\${_scan_asked}_FOUND TRUE)\n"
    "endforeach()\n")
  if(NOT SCAN_FIELDS_BY_BINDING_PACK)
    # The modules, and only them. Their interface units are installed beside
    # this and are compiled in the consumer's own build, and they say
    # `import boost.pfr`: there has to be a target of that name for the import
    # to resolve to, so whatever the exported one names is found again here, in
    # the same words -- and whoever reads this may have no provider at all,
    # which is the case this file is written for. Asked for only where nothing
    # has answered already, since adding it twice is two copies of one target.
    string(APPEND scan_config
      "if(_scan_taken_as STREQUAL \"modules\")\n"
      "  if(NOT TARGET Boost::pfr)\n"
      "    find_dependency(boost_pfr)\n"
      "  endif()\n"
      "  set_property(TARGET scan::modules APPEND PROPERTY\n"
      "               INTERFACE_LINK_LIBRARIES Boost::pfr)\n"
      "endif()\n")
  endif()
  # The headers are text, and which of the two ways of taking an aggregate
  # apart they read is decided by what the build reading them defines -- not by
  # how this copy was built. So it is the consumer's to choose, and the choice
  # is made the way everything else here is asked for: `COMPONENTS
  # binding-pack` says the structured binding pack, which is C++26 and needs no
  # library, and saying nothing says Boost.PFR.
  #
  # And Boost.PFR for a header is a header. A package to find is the tidy way
  # to have it and is used where there is one, but an include path that already
  # reaches it is just as good, and a consumer with nothing but a prefix and a
  # -I is not a consumer whose build should be stopped. So it is looked for,
  # used if it is there, and said once if it is not.
  string(APPEND scan_config
    "if(_scan_taken_as STREQUAL \"headers\")\n"
    "  if(\"binding-pack\" IN_LIST scan_FIND_COMPONENTS)\n"
    "    set_property(TARGET scan::headers APPEND PROPERTY\n"
    "                 INTERFACE_COMPILE_DEFINITIONS\n"
    "                 SCAN_FIELDS_BY_BINDING_PACK=1)\n"
    "  else()\n"
    "    if(NOT TARGET Boost::pfr)\n"
    "      find_package(boost_pfr QUIET)\n"
    "    endif()\n"
    "    if(TARGET Boost::pfr)\n"
    "      set_property(TARGET scan::headers APPEND PROPERTY\n"
    "                   INTERFACE_LINK_LIBRARIES Boost::pfr)\n"
    "    elseif(NOT scan_FIND_QUIETLY)\n"
    "      message(STATUS\n"
    "        \"scan: the headers read <boost/pfr.hpp>, and there is no \"\n"
    "        \"boost_pfr package here to say where it is; they will read it \"\n"
    "        \"from wherever this build's include paths point. Ask for \"\n"
    "        \"COMPONENTS binding-pack to take aggregates apart with a \"\n"
    "        \"structured binding pack instead, which needs C++26 and no \"\n"
    "        \"library at all.\")\n"
    "    endif()\n"
    "  endif()\n"
    "endif()\n")
  string(APPEND scan_config
    "if(NOT TARGET scan::scan)\n"
    "  add_library(scan::scan INTERFACE IMPORTED)\n"
    "  set_property(TARGET scan::scan PROPERTY\n"
    "               INTERFACE_LINK_LIBRARIES scan::\${_scan_taken_as})\n"
    "endif()\n")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/scanConfig.cmake" "${scan_config}")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/scanConfig.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/scan)
endif()
