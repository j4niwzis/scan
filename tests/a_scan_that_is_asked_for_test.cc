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


TEST(ExpectedForm, AScanThatIsAskedFor) {
  const std::string text = "12,34";
  const pair value = scan::scan<"{},{}">(text).of<pair>();
  EXPECT_EQ(value.left, 12);
  EXPECT_EQ(value.right, 34);
}

TEST(ExpectedForm, AScanThatTookTheInput) {
  const std::string text = "12,34";
  const auto value = scan::scan<"{},{}">(text).try_of<pair>();
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value->left, 12);
  EXPECT_EQ(value->right, 34);
}

}  // namespace
