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

}  // namespace
