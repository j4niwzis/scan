// A field that is a range takes as many elements as the input turned out to
// have, and lives under the ordinary repetition syntax.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct row { std::vector<int> values; };

TEST(list_test, as_many_as_the_input_turned_out_to_have) {
  const std::string text = "1,2,3";
  const row value = scan::scan<"{{}{*,?}}">(text);
  ASSERT_EQ(value.values.size(), 3u);
  EXPECT_EQ(value.values[0], 1);
  EXPECT_EQ(value.values[1], 2);
  EXPECT_EQ(value.values[2], 3);
}

TEST(list_test, just_the_one) {
  const std::string text = "7";
  const row value = scan::scan<"{{}{*,?}}">(text);
  ASSERT_EQ(value.values.size(), 1u);
  EXPECT_EQ(value.values[0], 7);
}

TEST(list_test, written_as_one_or_more) {
  const std::string text = "1,2,3";
  const row value = scan::scan<"{{}{*,?}}+">(text);
  ASSERT_EQ(value.values.size(), 3u);
  EXPECT_EQ(value.values[2], 3);
}

TEST(list_test, written_as_any_number_and_given_none) {
  const std::string text;
  const row value = scan::scan<"{{}{*,?}}*">(text);
  EXPECT_TRUE(value.values.empty());
}

TEST(list_test, written_as_exactly_two) {
  const std::string text = "4,5";
  const row value = scan::scan<"{{}{*,?}}{2}">(text);
  ASSERT_EQ(value.values.size(), 2u);
  EXPECT_EQ(value.values[0], 4);
}

TEST(list_test, and_refused_a_third) {
  const std::string text = "4,5,6";
  EXPECT_THROW((void)scan::scan<"{{}{*,?}}{2}">(text), std::exception);
}

}  // namespace
