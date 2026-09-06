// The same reading said either way: called, or handed the subject by a pipe.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

struct pair {
  int left;
  int right;
};

TEST(piped_test, a_subject_handed_to_a_split) {
  const auto pieces =
      "Hello brave world!"sv | scan::split<" "> | std::ranges::to<std::vector>();
  EXPECT_EQ(pieces, std::vector<std::string_view>(
                        {"Hello"sv, "brave"sv, "world!"sv}));
}

TEST(piped_test, the_same_split_called) {
  const auto pieces = scan::split<" ">("Hello brave world!"sv) |
                      std::ranges::to<std::vector>();
  EXPECT_EQ(pieces, std::vector<std::string_view>(
                        {"Hello"sv, "brave"sv, "world!"sv}));
}

TEST(piped_test, a_subject_handed_to_a_search) {
  std::string found;
  for (const auto& one : "ab12cd345"sv | scan::search_all<"[0-9]+">) {
    found += one.to_view();
    found += '|';
  }
  EXPECT_EQ(found, "12|345|");
}

TEST(piped_test, piped_on_to_the_ranges_library) {
  const auto middle = "one,two,three,four"sv | scan::split<","> |
                      std::views::drop(1) | std::views::take(2) |
                      std::ranges::to<std::vector>();
  EXPECT_EQ(middle, std::vector<std::string_view>({"two"sv, "three"sv}));
}

TEST(piped_test, a_subject_handed_to_each) {
  int total = 0;
  for (const pair& one : ("1:2 3:4 5:6"sv | scan::each<"{}:{}{* ?}">).of<pair>()) {
    total += one.left * one.right;
  }
  EXPECT_EQ(total, 2 + 12 + 30);
}

TEST(piped_test, the_same_each_called) {
  int total = 0;
  for (const pair& one : scan::each<"{}:{}{* ?}">("1:2 3:4 5:6"sv).of<pair>()) {
    total += one.left * one.right;
  }
  EXPECT_EQ(total, 2 + 12 + 30);
}

TEST(piped_test, a_tokenize_that_is_a_search_under_another_name) {
  const auto tokens = "a  bb  ccc"sv | scan::tokenize<"[a-z]+"> |
                      std::ranges::to<std::vector>();
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[2].to_view(), "ccc"sv);
}

}  // namespace
