import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

// As many as there are, and as many as were asked for.
//
// Ported from CTRE's official tests/matching.cpp and tests/range.cpp:
// https://github.com/hanickadot/compile-time-regular-expressions/tree/main/tests
// Only the namespace is changed where the regex syntax is supported here.

using namespace std::string_view_literals;

static_assert(scan::match<"a*">("aaa"sv));
static_assert(scan::match<"a+">("aaa"sv));
static_assert(scan::match<"a*xb">("aaxb"sv));
static_assert(scan::match<"a+xb">("axb"sv));
static_assert(scan::match<"a{2,5}ab">("aaaaaab"sv));
static_assert(!scan::match<"a{2,5}ab">("aaaaaaab"sv));

TEST(CtreCompatRepetition, TheyAllHold) {
  // Every one of them is answered while this file is compiled; that they
  // are is the whole of the test.
  SUCCEED();
}
