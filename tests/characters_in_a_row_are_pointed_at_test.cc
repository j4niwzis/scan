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

}  // namespace
