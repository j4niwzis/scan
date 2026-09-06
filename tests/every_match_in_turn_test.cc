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

}  // namespace
