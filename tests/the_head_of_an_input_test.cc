// A scan that is asked rather than assigned, and a scan that hands back what
// went wrong instead of throwing it.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct pair {
  int left;
  int right;
};


TEST(ExpectedForm, TheHeadOfAnInput) {
  const std::string text = "12,34 and the rest";
  const auto got = scan::scan_prefix<"{},{}">(text).try_take<pair>();
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->value.left, 12);
  EXPECT_EQ(got->rest, " and the rest");
}

TEST(ExpectedForm, AHeadThatIsNotThere) {
  const std::string text = "nothing of the sort";
  const auto got = scan::scan_prefix<"{},{}">(text).try_take<pair>();
  EXPECT_FALSE(got.has_value());
}

}  // namespace
