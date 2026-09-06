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


TEST(Piped, ASubjectHandedToASearch) {
  std::string found;
  for (const auto& one : "ab12cd345"sv | scan::search_all<"[0-9]+">) {
    found += one.to_view();
    found += '|';
  }
  EXPECT_EQ(found, "12|345|");
}

TEST(Piped, PipedOnToTheRangesLibrary) {
  const auto middle = "one,two,three,four"sv | scan::split<","> |
                      std::views::drop(1) | std::views::take(2) |
                      std::ranges::to<std::vector>();
  EXPECT_EQ(middle, std::vector<std::string_view>({"two"sv, "three"sv}));
}

}  // namespace
