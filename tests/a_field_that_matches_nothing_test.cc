// The same subjects down both walks: the one that watches the length and the
// one that leans on a sentinel.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct five {
  std::string_view a, b, c, d, e;
};
struct two {
  std::string_view a, b;
};



TEST(BothPathsTest, AFieldThatMatchesNothing) {
  const two value = scan::scan<"{[a-z]}{[0-9]*}">(std::string_view("x"));
  EXPECT_EQ(value.a, "x");
  EXPECT_TRUE(value.b.empty());
}

}  // namespace
