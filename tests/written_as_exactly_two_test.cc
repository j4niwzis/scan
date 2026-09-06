// A field that is a range takes as many elements as the input turned out to
// have, and lives under the ordinary repetition syntax.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct row { std::vector<int> values; };


TEST(ListTest, WrittenAsExactlyTwo) {
  const std::string text = "4,5";
  const row value = scan::scan<"{{}{*,?}}{2}">(text);
  ASSERT_EQ(value.values.size(), 2u);
  EXPECT_EQ(value.values[0], 4);
}

TEST(ListTest, AndRefusedAThird) {
  const std::string text = "4,5,6";
  EXPECT_THROW(
      {
        const row value = scan::scan<"{{}{*,?}}{2}">(text);
        (void)value;
      },
      std::exception);
}

}  // namespace
