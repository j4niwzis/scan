// Where a record ends: as far as the machine can walk, and not one character
// further.
//
// `for|each` over "foreach" is two records. After `for` there is nowhere to go
// -- no branch of this pattern has an `e` there -- so the record ends and the
// next one begins where the machine stopped.
//
// `foreach|for|each` over the same subject is one record, `foreach`. From the
// accepting place after `for` the letter `e` leads on, and the machine takes
// it, because nothing here looks for the best reading or goes back to a
// shorter one: it walks while it can and answers where it stopped.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

struct for_word { std::string_view text; };
struct each_word { std::string_view text; };
struct whole_word { std::string_view text; };

using either = std::variant<for_word, each_word>;
struct row { either value; };

using any_of_three = std::variant<whole_word, for_word, each_word>;
struct wide_row { any_of_three value; };


TEST(WhereARecordEnds, ForAndThenEach) {
  std::vector<std::string_view> seen;
  std::vector<std::size_t> which;
  for (const row& one : scan::each<"{{for}|{each}}">("foreach"sv).of<row>()) {
    which.push_back(one.value.index());
    std::visit([&](const auto& what) { seen.push_back(what.text); }, one.value);
  }
  EXPECT_EQ(seen, std::vector<std::string_view>({"for"sv, "each"sv}));
  EXPECT_EQ(which, std::vector<std::size_t>({0u, 1u}));
}

TEST(WhereARecordEnds, TheBranchThatGoesOnTakesItAll) {
  std::vector<std::string_view> seen;
  std::vector<std::size_t> which;
  for (const wide_row& one :
       scan::each<"{{foreach}|{for}|{each}}">("foreach"sv).of<wide_row>()) {
    which.push_back(one.value.index());
    std::visit([&](const auto& what) { seen.push_back(what.text); }, one.value);
  }
  EXPECT_EQ(seen, std::vector<std::string_view>({"foreach"sv}));
  EXPECT_EQ(which, std::vector<std::size_t>({0u}));
}

}  // namespace
