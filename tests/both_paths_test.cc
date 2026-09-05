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


TEST(both_paths_test, bounded_five_fields) {
  const std::string text = "alpha,bravo,charlie,delta,echo";
  const five value =
      scan::scan<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}">(
          std::string_view(text));
  EXPECT_EQ(value.a, "alpha");
  EXPECT_EQ(value.c, "charlie");
  EXPECT_EQ(value.e, "echo");
}

TEST(both_paths_test, sentinel_five_fields) {
  const std::string text = "alpha,bravo,charlie,delta,echo";
  const five value =
      scan::scan_sentinel<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}">(
          std::string_view(text));
  EXPECT_EQ(value.a, "alpha");
  EXPECT_EQ(value.c, "charlie");
  EXPECT_EQ(value.e, "echo");
}

TEST(both_paths_test, states_that_lead_back) {
  const std::string text = "ababab,42";
  const two bounded = scan::scan<"{(?:ab)+},{[0-9]+}">(std::string_view(text));
  EXPECT_EQ(bounded.a, "ababab");
  EXPECT_EQ(bounded.b, "42");
  const two sentinel =
      scan::scan_sentinel<"{(?:ab)+},{[0-9]+}">(std::string_view(text));
  EXPECT_EQ(sentinel.a, "ababab");
  EXPECT_EQ(sentinel.b, "42");
}

TEST(both_paths_test, a_subject_that_does_not_match) {
  // Read into the type, not cast away: a scan is done when something asks it
  // for a value, so a discarded one never runs and never refuses.
  EXPECT_THROW(
      {
        const five value =
            scan::scan<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}">(
                std::string_view("alpha,bravo"));
        (void)value;
      },
      std::exception);
}

TEST(both_paths_test, a_field_that_matches_nothing) {
  const two value = scan::scan<"{[a-z]}{[0-9]*}">(std::string_view("x"));
  EXPECT_EQ(value.a, "x");
  EXPECT_TRUE(value.b.empty());
}

}  // namespace
