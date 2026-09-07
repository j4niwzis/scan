// Where a record ends: at the first alternative under which the whole pattern
// matches, which is leftmost-first and is what the order of the branches is
// for.
//
// `for|each|foreach` over "foreach" is two records, `for` and `each`. The
// match of `for` is the first walk in that state, so the walks under it were
// cut where the automaton was built and there is nowhere to go: the record
// ends after three characters, and nothing further is read to find that out.
//
// `foreach|for|each` over the same subject is one record. There the walk of
// `foreach` sits above the match of `for` and is still alive, so the machine
// goes on and finds the longer branch. Over "fore" it goes on in the same way,
// finds nothing, and answers with the place it kept -- `for`.
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

// The order of the branches is the order of the alternatives, and it is what
// decides where a record ends.
using shortest_first = std::variant<for_word, each_word, whole_word>;
struct row_of_three { shortest_first value; };

using longest_first = std::variant<whole_word, for_word, each_word>;
struct wide_row { longest_first value; };


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

TEST(WhereARecordEnds, TheShorterBranchFirstEndsTheRecordAtIt) {
  // `for` is the first alternative under which the whole pattern matches, so
  // the record ends there and nothing below it is read: leftmost-first, and
  // the state has nowhere to go because the walks under the match were cut
  // where the automaton was built.
  std::vector<std::string_view> seen;
  std::vector<std::size_t> which;
  for (const row_of_three& one :
       scan::each<"{{for}|{each}|{foreach}}">("foreach"sv).of<row_of_three>()) {
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

TEST(WhereARecordEnds, AndGoesBackToTheMatchItPassed) {
  // `foreach` is preferred, so the walk goes past the match of `for` on the
  // `e` -- and then the input runs out with `foreach` unfinished. The answer
  // is the place it kept: `for`, and the `e` after it is the next record's
  // problem.
  std::vector<std::string_view> seen;
  std::vector<std::size_t> which;
  for (const wide_row& one :
       scan::each<"{{foreach}|{for}|{each}}">("fore"sv).of<wide_row>()) {
    which.push_back(one.value.index());
    std::visit([&](const auto& what) { seen.push_back(what.text); }, one.value);
  }
  EXPECT_EQ(seen, std::vector<std::string_view>({"for"sv}));
  EXPECT_EQ(which, std::vector<std::size_t>({1u}));
}

}  // namespace
