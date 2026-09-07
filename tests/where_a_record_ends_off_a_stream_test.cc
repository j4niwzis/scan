// The same reading off a subject that can only be read once, and the same
// answers: nothing here is walked back, so nothing here is refused.
//
// A head is where the machine stopped. It never returns to a place it liked
// better, so a subject that is gone once it is read costs nothing at all --
// the one character that ended the record is handed back to the next one, and
// that is the whole of it. Both patterns give what they gave over characters
// in a row, word for word.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct for_word { scan::held<8> text; };
struct each_word { scan::held<8> text; };
struct whole_word { scan::held<8> text; };

using either = std::variant<for_word, each_word>;
struct row { either value; };

using shortest_first = std::variant<for_word, each_word, whole_word>;
struct row_of_three { shortest_first value; };

using longest_first = std::variant<whole_word, for_word, each_word>;
struct wide_row { longest_first value; };

TEST(WhereARecordEnds, ForAndThenEachOffAStream) {
  std::istringstream source("foreach");
  source >> std::noskipws;
  std::vector<std::string> seen;
  std::vector<std::size_t> which;
  for (const row& one :
       scan::each<"{{for}|{each}}">(std::views::istream<char>(source))
           .of<row>()) {
    which.push_back(one.value.index());
    std::visit([&](const auto& what) {
      seen.push_back(std::string(what.text.view()));
    }, one.value);
  }
  EXPECT_EQ(seen, std::vector<std::string>({"for", "each"}));
  EXPECT_EQ(which, std::vector<std::size_t>({0u, 1u}));
}

TEST(WhereARecordEnds, TheBranchThatGoesOnTakesItAllOffAStream) {
  std::istringstream source("foreach");
  source >> std::noskipws;
  std::vector<std::string> seen;
  std::vector<std::size_t> which;
  for (const wide_row& one :
       scan::each<"{{foreach}|{for}|{each}}">(std::views::istream<char>(source))
           .of<wide_row>()) {
    which.push_back(one.value.index());
    std::visit([&](const auto& what) {
      seen.push_back(std::string(what.text.view()));
    }, one.value);
  }
  EXPECT_EQ(seen, std::vector<std::string>({"foreach"}));
  EXPECT_EQ(which, std::vector<std::size_t>({0u}));
}

TEST(WhereARecordEnds, TheShorterBranchFirstOffAStream) {
  std::istringstream source("foreach");
  source >> std::noskipws;
  std::vector<std::string> seen;
  std::vector<std::size_t> which;
  for (const row_of_three& one : scan::each<"{{for}|{each}|{foreach}}">(
                                     std::views::istream<char>(source))
                                     .of<row_of_three>()) {
    which.push_back(one.value.index());
    std::visit([&](const auto& what) {
      seen.push_back(std::string(what.text.view()));
    }, one.value);
  }
  EXPECT_EQ(seen, std::vector<std::string>({"for", "each"}));
  EXPECT_EQ(which, std::vector<std::size_t>({0u, 1u}));
}

TEST(WhereARecordEnds, AndGoesBackToTheMatchItPassedOffAStream) {
  // The walk goes past `for` on the `e` because `foreach` is preferred, the
  // stream runs out, and the answer is the machine that was kept where the
  // match was -- with the characters read after it handed to whatever reads
  // next.
  std::istringstream source("fore");
  source >> std::noskipws;
  std::vector<std::string> seen;
  std::vector<std::size_t> which;
  for (const wide_row& one : scan::each<"{{foreach}|{for}|{each}}">(
                                 std::views::istream<char>(source))
                                 .of<wide_row>()) {
    which.push_back(one.value.index());
    std::visit([&](const auto& what) {
      seen.push_back(std::string(what.text.view()));
    }, one.value);
  }
  EXPECT_EQ(seen, std::vector<std::string>({"for"}));
  EXPECT_EQ(which, std::vector<std::size_t>({1u}));
}

}  // namespace
