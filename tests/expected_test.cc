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

TEST(ExpectedForm, AScanThatDidNot) {
  const std::string text = "12;34";
  const auto value = scan::scan<"{},{}">(text).try_of<pair>();
  EXPECT_FALSE(value.has_value());
  EXPECT_NE(std::string_view(value.error().what()).size(), 0u);
}

TEST(ExpectedForm, AFieldThatWillNotConvert) {
  const std::string text = "12,abc";
  const auto value = scan::scan<"{},{}">(text).try_of<pair>();
  EXPECT_FALSE(value.has_value());
}

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

TEST(ExpectedForm, TheThrowingFormStillThrows) {
  const std::string text = "12;34";
  EXPECT_THROW(
      {
        const pair value = scan::scan<"{},{}">(text).of<pair>();
        (void)value;
      },
      scan::scan_error);
}

}  // namespace
