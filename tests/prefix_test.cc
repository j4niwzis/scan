// Reading a head off the front of a subject, and being told what follows it.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct pair { int left; int right; };

TEST(prefix_test, a_head_and_what_follows_it) {
  const std::string text = "12 34 and the rest";
  const auto [value, rest] = scan::scan_prefix<"{}{*\\s*}{}">(text).take<pair>();
  EXPECT_EQ(value.left, 12);
  EXPECT_EQ(value.right, 34);
  EXPECT_EQ(rest, " and the rest");
}

TEST(prefix_test, one_record_after_another) {
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

TEST(prefix_test, assigned_with_the_rest_dropped) {
  const std::string text = "7 8 tail";
  const pair value = scan::scan_prefix<"{}{*\\s*}{}">(text);
  EXPECT_EQ(value.left, 7);
  EXPECT_EQ(value.right, 8);
}

TEST(prefix_test, an_input_that_does_not_begin_with_it) {
  const std::string text = "nothing here";
  EXPECT_THROW((void)scan::scan_prefix<"{},{}">(text).take<pair>(),
               std::exception);
}

}  // namespace
