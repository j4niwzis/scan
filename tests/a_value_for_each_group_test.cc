// A type for each group, and its own arguments with it.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;


TEST(CollectorTypes, AValueForEachGroup) {
  const std::string text = "42-abc";
  const auto found = scan::match<"([0-9]+)-([a-z]+)">.into(
      scan::as<int>(), scan::as<std::string>())(text);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.get<1>(), 42);
  EXPECT_EQ(found.get<2>(), "abc");
}

TEST(CollectorTypes, TheWholeMatchIsStillThere) {
  const std::string text = "42-abc";
  const auto found = scan::match<"([0-9]+)-([a-z]+)">.into(
      scan::as<int>(), scan::as<std::string>())(text);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.whole().to_view(), "42-abc"sv);
}

}  // namespace
