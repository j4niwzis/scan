import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

// One of several, and one that need not be there at all.
//
// Ported from CTRE's official tests/matching.cpp and tests/range.cpp:
// https://github.com/hanickadot/compile-time-regular-expressions/tree/main/tests
// Only the namespace is changed where the regex syntax is supported here.

using namespace std::string_view_literals;

static_assert(!scan::match<"(?:a|b|c)">("d"sv));
static_assert(scan::match<"(?:xy)?">("xy"sv));

TEST(CtreCompatAlternation, TheyAllHold) {
  // Every one of them is answered while this file is compiled; that they
  // are is the whole of the test.
  SUCCEED();
}
