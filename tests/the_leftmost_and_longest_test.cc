// One match after another, found as they are asked for.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;


TEST(SearchView, TheLeftmostAndLongest) {
  const auto found = scan::search<"[0-9]+">("id=4210x"sv);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.to_view(), "4210"sv);
}

}  // namespace
