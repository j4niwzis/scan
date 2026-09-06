// A true input range: characters are taken one at a time and never buffered,
// so what stops the match is reported rather than pushed back.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct pair { int left; int right; };
struct one { int value; };
struct two_words { std::string left; std::string right; };


TEST(StreamTest, AHeadOffAStreamAndWhatStoppedIt) {
  std::istringstream source("12,34rest");
  auto got =
      scan::scan_prefix<"{},{}">(std::views::istream<char>(source)).take<pair>();
  EXPECT_EQ(got.value.left, 12);
  EXPECT_EQ(got.value.right, 34);
  EXPECT_EQ(got.stopped, 'r');
}

TEST(StreamTest, AStreamThatRunsOutExactly) {
  std::istringstream source("77,88");
  auto got =
      scan::scan_prefix<"{},{}">(std::views::istream<char>(source)).take<pair>();
  EXPECT_EQ(got.value.left, 77);
  EXPECT_EQ(got.value.right, 88);
  EXPECT_FALSE(got.stopped);
}

}  // namespace
