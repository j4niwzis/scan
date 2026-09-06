import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

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
static_assert(!scan::match<"(?:a|b|c)">("d"sv));
static_assert(scan::match<"(?:xy)?">("xy"sv));
static_assert(scan::match<"(?:xy)?">(""sv));
static_assert(scan::match<"a*">("aaa"sv));
static_assert(scan::match<"a+">("aaa"sv));
static_assert(scan::match<"a*">(""sv));
static_assert(scan::match<"a+">("a"sv));
static_assert(scan::match<"a*xb">("aaxb"sv));
static_assert(scan::match<"a+xb">("axb"sv));
static_assert(scan::match<"a{2,5}ab">("aaaaaab"sv));
static_assert(!scan::match<"a{2,5}ab">("aaaaaaab"sv));

TEST(ctre_compat_test, preserves_official_capture_and_range_contract) {
  const auto result =
      scan::match<"([a-z]++),([a-z]++),([a-z]++),([a-z]++),([a-z]++)">(
          "one,two,three,four,five"sv);

  ASSERT_TRUE(result);
  EXPECT_EQ(result.get<1>().to_view(), "one"sv);
  EXPECT_EQ(result.get<2>().to_view(), "two"sv);
  EXPECT_EQ(result.get<3>().to_view(), "three"sv);
  EXPECT_EQ(result.get<4>().to_view(), "four"sv);
  EXPECT_EQ(result.get<5>().to_view(), "five"sv);

  // Found as they are asked for, so what is wanted as a container is said to
  // be one.
  const auto matches = scan::search_all<"([0-9])[0-9]++">("123,456,768"sv) |
                       std::ranges::to<std::vector>();
  ASSERT_EQ(matches.size(), 3);
  EXPECT_EQ(matches[0].get<1>().to_view(), "1"sv);
  EXPECT_EQ(matches[1].get<1>().to_view(), "4"sv);
  EXPECT_EQ(matches[2].get<1>().to_view(), "7"sv);

  std::string leading;
  for (const auto& one : scan::search_all<"([0-9])[0-9]++">("123,456,768"sv)) {
    leading += one.get<1>().to_view();
  }
  EXPECT_EQ(leading, "147");
}

TEST(ctre_compat_test, exposes_search_split_and_tokenize_surface) {
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
