// One match after another, found as they are asked for.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

TEST(SearchView, EveryMatchInTurn) {
  std::string found;
  for (const auto& one : "ab12cd345e6"sv | scan::search_all<"[0-9]+">) {
    found += one.to_view();
    found += '|';
  }
  EXPECT_EQ(found, "12|345|6|");
}

TEST(SearchView, NothingWhereNothingMatches) {
  const auto all = "nothing here"sv | scan::search_all<"[0-9]+"> |
                   std::ranges::to<std::vector>();
  EXPECT_TRUE(all.empty());
}

TEST(SearchView, ThePiecesBetween) {
  const auto pieces =
      "a,bb,,ccc"sv | scan::split<","> | std::ranges::to<std::vector>();
  EXPECT_EQ(pieces, std::vector<std::string_view>(
                        {"a"sv, "bb"sv, ""sv, "ccc"sv}));
}

TEST(SearchView, NoDelimiterIsOnePiece) {
  const auto pieces =
      "nothing"sv | scan::split<","> | std::ranges::to<std::vector>();
  EXPECT_EQ(pieces, std::vector<std::string_view>({"nothing"sv}));
}

TEST(SearchView, TheLeftmostAndLongest) {
  const auto found = scan::search<"[0-9]+">("id=4210x"sv);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.to_view(), "4210"sv);
}

}  // namespace
