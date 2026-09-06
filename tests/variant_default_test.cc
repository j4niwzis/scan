// A variant place left empty asks each alternative how it reads itself, which
// it can answer if it has a scanner or a format of its own.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct coordinates { int x; int y; };

}  // namespace

template <> struct scan::scanner<coordinates>
    : scan::aggregate_scanner<"({}, {})"> {};

namespace {

struct row { std::variant<coordinates, int> value; };

TEST(VariantDefaultTest, TheShape) {
  const std::string text = "(1, 2)";
  const row value = scan::scan<"{}">(text);
  ASSERT_EQ(value.value.index(), 0u);
  EXPECT_EQ(std::get<0>(value.value).x, 1);
}

TEST(VariantDefaultTest, TheValue) {
  const std::string text = "-42";
  const row value = scan::scan<"{}">(text);
  ASSERT_EQ(value.value.index(), 1u);
  EXPECT_EQ(std::get<1>(value.value), -42);
}

}  // namespace
