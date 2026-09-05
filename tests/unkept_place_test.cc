// A place that is matched and thrown away, which is what the star of `%*d` has
// always meant -- and the only way to say a run of spaces of no fixed length.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct pair { int left; int right; };

TEST(unkept_place_test, a_run_of_spaces) {
  for (std::string_view text : {"1 2", "1   2", "1\t2", "12"}) {
    const std::string subject(text);
    const pair value = scan::scan<"{}{*\\s*}{}">(subject);
    EXPECT_EQ(value.left, 1);
    EXPECT_EQ(value.right, 2);
  }
}

TEST(unkept_place_test, a_field_nobody_wants) {
  const std::string text = "keep=7 drop=abc keep=9";
  const pair value = scan::scan<"keep={} {*drop=[a-z]+} keep={}">(text);
  EXPECT_EQ(value.left, 7);
  EXPECT_EQ(value.right, 9);
}

}  // namespace
