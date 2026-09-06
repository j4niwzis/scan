// Reading a head off the front of a subject, and being told what follows it.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct pair { int left; int right; };


TEST(PrefixTest, AHeadAndWhatFollowsIt) {
  const std::string text = "12 34 and the rest";
  const auto [value, rest] = scan::scan_prefix<"{}{*\\s*}{}">(text).take<pair>();
  EXPECT_EQ(value.left, 12);
  EXPECT_EQ(value.right, 34);
  EXPECT_EQ(rest, " and the rest");
}

TEST(PrefixTest, OneRecordAfterAnother) {
  std::string_view text = "1,2;3,4;5,6;";
  int sum = 0;
  int records = 0;
  while (!text.empty()) {
    const auto step = scan::scan_prefix<"{},{};">(text).take<pair>();
    sum += step.value.left + step.value.right;
    text = step.rest;
    ++records;
  }
  EXPECT_EQ(sum, 21);
  EXPECT_EQ(records, 3);
}

}  // namespace
