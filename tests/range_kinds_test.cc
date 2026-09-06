// Three subjects and three answers: pointed at, walked between, owned.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

TEST(RangeKinds, CharactersInARowArePointedAt) {
  const std::string text = "abc12";
  const auto found = scan::match<"[a-z]+[0-9]+">(text);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.to_view(), "abc12"sv);
}

TEST(RangeKinds, CharactersWalkedAreHeldAsIterators) {
  const std::list<char> text{'a', 'b', 'c', '1', '2'};
  const auto found = scan::match<"[a-z]+[0-9]+">(text);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(std::string(found.begin(), found.end()), "abc12");
}

TEST(RangeKinds, AHeadOfWhatIsWalked) {
  const std::list<char> text{'a', 'b', 'c', '1', '2'};
  const auto found = scan::starts_with<"[a-z]+">(text);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(std::string(found.begin(), found.end()), "abc");
}

TEST(RangeKinds, WhatIsReadOnceIsOwned) {
  std::istringstream input("abc12");
  input >> std::noskipws;
  const auto found =
      scan::match<"[a-z]+[0-9]+">(std::views::istream<char>(input));
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.whole().held(), "abc12");
  EXPECT_TRUE(
      (std::is_same_v<std::remove_cvref_t<decltype(found.whole().held())>,
                      std::string>));
}

TEST(RangeKinds, AHeadOfWhatIsReadOnce) {
  std::istringstream input("abc12");
  input >> std::noskipws;
  const auto found =
      scan::starts_with<"[a-z]+">(std::views::istream<char>(input));
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.whole().held(), "abc");
}

TEST(RangeKinds, CollectedOffWhatIsReadOnce) {
  std::istringstream input("42-abc");
  input >> std::noskipws;
  const auto found = scan::match<"([0-9]+)-([a-z]+)">.into(
      scan::as<int>(), scan::as<std::string>())(std::views::istream<char>(input));
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.get<1>(), 42);
  EXPECT_EQ(found.get<2>(), "abc");
}

}  // namespace
