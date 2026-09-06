// A field that is a range takes as many elements as the input turned out to
// have, and lives under the ordinary repetition syntax.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct row { std::vector<int> values; };


TEST(ListTest, AsManyAsTheInputTurnedOutToHave) {
  const std::string text = "1,2,3";
  const row value = scan::scan<"{{}{*,?}}">(text);
  ASSERT_EQ(value.values.size(), 3u);
  EXPECT_EQ(value.values[0], 1);
  EXPECT_EQ(value.values[1], 2);
  EXPECT_EQ(value.values[2], 3);
}

TEST(ListTest, JustTheOne) {
  const std::string text = "7";
  const row value = scan::scan<"{{}{*,?}}">(text);
  ASSERT_EQ(value.values.size(), 1u);
  EXPECT_EQ(value.values[0], 7);
}

}  // namespace
