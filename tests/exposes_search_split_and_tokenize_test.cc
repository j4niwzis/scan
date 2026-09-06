import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

// Ported from CTRE's official tests/matching.cpp and tests/range.cpp:
// https://github.com/hanickadot/compile-time-regular-expressions/tree/main/tests
// Only the namespace is changed where the regex syntax is supported here.

using namespace std::string_view_literals;


TEST(CtreCompatTest, ExposesSearchSplitAndTokenizeSurface) {
  EXPECT_EQ(scan::search<"[0-9]+">("id=42"sv).to_view(), "42"sv);
  EXPECT_EQ(scan::starts_with<"[a-z]+">("abc42"sv).to_view(), "abc"sv);

  const auto pieces =
      scan::split<",+">("a,b,,c"sv) | std::ranges::to<std::vector>();
  EXPECT_EQ(pieces,
            std::vector<std::string_view>({"a"sv, "b"sv, "c"sv}));

  const auto tokens =
      scan::tokenize<"[a-z]+">("a  bb  ccc"sv) | std::ranges::to<std::vector>();
  ASSERT_EQ(tokens.size(), 3);
  EXPECT_EQ(tokens[0].to_view(), "a"sv);
  EXPECT_EQ(tokens[1].to_view(), "bb"sv);
  EXPECT_EQ(tokens[2].to_view(), "ccc"sv);
}
