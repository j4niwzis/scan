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


TEST(VariantNestedTest, TheOuterBranch) {
  const std::string text = "hello";
  const row value = scan::scan<"{{[a-z]+}|{{[0-9]+}|{[A-Z]+}}}">(text);
  EXPECT_EQ(value.value.index(), 0u);
}

TEST(VariantNestedTest, TheFirstInnerBranch) {
  const std::string text = "1234";
  const row value = scan::scan<"{{[a-z]+}|{{[0-9]+}|{[A-Z]+}}}">(text);
  ASSERT_EQ(value.value.index(), 1u);
  EXPECT_EQ(std::get<1>(value.value).index(), 0u);
}

}  // namespace
