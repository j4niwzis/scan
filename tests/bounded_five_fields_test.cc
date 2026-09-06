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



TEST(BothPathsTest, BoundedFiveFields) {
  const std::string text = "alpha,bravo,charlie,delta,echo";
  const five value =
      scan::scan<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}">(
          std::string_view(text));
  EXPECT_EQ(value.a, "alpha");
  EXPECT_EQ(value.c, "charlie");
  EXPECT_EQ(value.e, "echo");
}

TEST(BothPathsTest, SentinelFiveFields) {
  const std::string text = "alpha,bravo,charlie,delta,echo";
  const five value =
      scan::scan_sentinel<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}">(
          std::string_view(text));
  EXPECT_EQ(value.a, "alpha");
  EXPECT_EQ(value.c, "charlie");
  EXPECT_EQ(value.e, "echo");
}

}  // namespace
