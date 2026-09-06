// Where a record ends: at the first place the machine accepts, not at the
// furthest one it could have reached.
//
// `foreach|for|each` over "foreach" could be one record. It is two, because
// nothing here looks for the longest match: the head is taken where the
// machine first stands in an accepting state, and from there the next record
// begins. So "foreach" reads as `for` and then `each`.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

struct the_whole_word { std::string_view text; };
struct the_short_word { std::string_view text; };
struct the_rest_word { std::string_view text; };

using keyword = std::variant<the_whole_word, the_short_word, the_rest_word>;
struct row { keyword value; };


TEST(FirstAccept, ForAndEachRatherThanForeach) {
  std::vector<std::string_view> seen;
  std::vector<std::size_t> which;
  for (const row& one :
       scan::each<"{{foreach}|{for}|{each}}">("foreach"sv).of<row>()) {
    which.push_back(one.value.index());
    std::visit([&](const auto& what) { seen.push_back(what.text); }, one.value);
  }
  EXPECT_EQ(seen, std::vector<std::string_view>({"for"sv, "each"sv}));
  EXPECT_EQ(which, std::vector<std::size_t>({1u, 2u}));
}

TEST(FirstAccept, TheWholeWordStillWinsWhereNothingElseFits) {
  std::vector<std::size_t> which;
  for (const row& one :
       scan::each<"{{foreach}|{for}|{each}}">("each"sv).of<row>()) {
    which.push_back(one.value.index());
  }
  EXPECT_EQ(which, std::vector<std::size_t>({2u}));
}

}  // namespace
