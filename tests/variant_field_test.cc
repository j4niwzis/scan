// A variant standing in a field: the branches of the place, one per
// alternative, and a mark on each saying which one ran.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct word { std::string_view text; };
struct number { std::string_view digits; };
struct line { int id; std::variant<word, number> value; };

TEST(VariantFieldTest, AWord) {
  const std::string text = "7: hello";
  const line value = scan::scan<"{}: {{[a-z]+}|{[0-9]+}}">(text);
  EXPECT_EQ(value.id, 7);
  ASSERT_EQ(value.value.index(), 0u);
  EXPECT_EQ(std::get<0>(value.value).text, "hello");
}

TEST(VariantFieldTest, ANumber) {
  const std::string text = "42: 1234";
  const line value = scan::scan<"{}: {{[a-z]+}|{[0-9]+}}">(text);
  EXPECT_EQ(value.id, 42);
  ASSERT_EQ(value.value.index(), 1u);
  EXPECT_EQ(std::get<1>(value.value).digits, "1234");
}

}  // namespace
