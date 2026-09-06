// One match after another, found as they are asked for.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;


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

}  // namespace
