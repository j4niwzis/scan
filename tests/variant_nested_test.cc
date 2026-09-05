// A variant inside a variant, and a variant inside an aggregate inside a
// variant.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct word { std::string_view text; };
struct number { std::string_view digits; };
struct shout { std::string_view text; };

using deeper = std::variant<number, shout>;
using deep = std::variant<word, deeper>;
struct row { deep value; };

TEST(variant_nested_test, the_outer_branch) {
  const std::string text = "hello";
  const row value = scan::scan<"{{[a-z]+}|{{[0-9]+}|{[A-Z]+}}}">(text);
  EXPECT_EQ(value.value.index(), 0u);
}

TEST(variant_nested_test, the_first_inner_branch) {
  const std::string text = "1234";
  const row value = scan::scan<"{{[a-z]+}|{{[0-9]+}|{[A-Z]+}}}">(text);
  ASSERT_EQ(value.value.index(), 1u);
  EXPECT_EQ(std::get<1>(value.value).index(), 0u);
}

TEST(variant_nested_test, the_second_inner_branch) {
  const std::string text = "LOUD";
  const row value = scan::scan<"{{[a-z]+}|{{[0-9]+}|{[A-Z]+}}}">(text);
  ASSERT_EQ(value.value.index(), 1u);
  EXPECT_EQ(std::get<1>(value.value).index(), 1u);
}

}  // namespace
