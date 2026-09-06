// Three subjects and three answers: pointed at, walked between, owned.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;


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

}  // namespace
