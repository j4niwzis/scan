import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

// Literals, the dot, and classes of characters.
//
// Ported from CTRE's official tests/matching.cpp and tests/range.cpp:
// https://github.com/hanickadot/compile-time-regular-expressions/tree/main/tests
// Only the namespace is changed where the regex syntax is supported here.

using namespace std::string_view_literals;

static_assert(scan::match<"a">("a"sv));
static_assert(scan::search<"a">("abc"sv));
static_assert(scan::search<"b">("abc"sv));
static_assert(scan::search<"c">("abc"sv));
static_assert(scan::starts_with<"a">("abc"sv));
static_assert(!scan::starts_with<"b">("abc"sv));
static_assert(!scan::match<"b">("abc"sv));
static_assert(!scan::match<"b">("a"sv));
static_assert(scan::match<".">("a"sv));
static_assert(scan::match<"[a-z]">("a"sv));
static_assert(scan::match<"[a-z]">("f"sv));
static_assert(scan::match<"[a-z]">("z"sv));
static_assert(!scan::match<"[a-z]">("Z"sv));
static_assert(scan::match<"[a-z0-9]">("0"sv));
static_assert(!scan::match<"[a-z0-9]">("A"sv));
static_assert(scan::match<"abcdef">("abcdef"sv));
static_assert(!scan::match<"abcdef">("abcgef"sv));
static_assert(scan::match<"">(""sv));
static_assert(scan::match<"(?:a|b|c)">("a"sv));
static_assert(scan::match<"(?:a|b|c)">("b"sv));
static_assert(scan::match<"(?:a|b|c)">("c"sv));
static_assert(scan::match<"(?:xy)?">(""sv));
static_assert(scan::match<"a*">(""sv));
static_assert(scan::match<"a+">("a"sv));

TEST(CtreCompatLiterals, TheyAllHold) {
  // Every one of them is answered while this file is compiled; that they
  // are is the whole of the test.
  SUCCEED();
}
