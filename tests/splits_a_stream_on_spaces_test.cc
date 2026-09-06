// Splitting and searching a subject that can only be read once, on a pattern
// that cannot say how long a match is -- which is most patterns, and which
// used to be refused.
//
// `\s+` holds nothing at all while it looks: every space is already a whole
// match, so there is never anything to give back. The words themselves are
// the answer, and they go where the answer goes.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;


TEST(ReadOnceSplit, OnRunsOfSpace) {
  std::istringstream source("alpha beta   gamma");
  source >> std::noskipws;
  const auto words = scan::split<"\\s+">(std::views::istream<char>(source)) |
                     std::ranges::to<std::vector>();
  EXPECT_EQ(words, std::vector<std::string>({"alpha", "beta", "gamma"}));
}

TEST(ReadOnceSplit, LeadingAndTrailingRunsAreEmptyPieces) {
  std::istringstream source(" a b ");
  source >> std::noskipws;
  const auto words = scan::split<"\\s+">(std::views::istream<char>(source)) |
                     std::ranges::to<std::vector>();
  EXPECT_EQ(words, std::vector<std::string>({"", "a", "b", ""}));
}

TEST(ReadOnceSplit, ADelimiterThatMustBeGivenBack) {
  // `ab` takes the `a` before it knows what follows, and where what follows is
  // not a `b` that `a` belongs to the piece.
  std::istringstream source("1a2ab3aab4");
  source >> std::noskipws;
  const auto pieces = scan::split<"ab">(std::views::istream<char>(source)) |
                      std::ranges::to<std::vector>();
  EXPECT_EQ(pieces, std::vector<std::string>({"1a2", "3a", "4"}));
}

TEST(ReadOnceSearch, EveryRunOfLetters) {
  std::istringstream source("12 abc,, de 9 f");
  source >> std::noskipws;
  std::vector<std::string> found;
  for (const auto& one :
       scan::search_all<"[a-z]+">(std::views::istream<char>(source))) {
    found.push_back(std::string(one.held()));
  }
  EXPECT_EQ(found, std::vector<std::string>({"abc", "de", "f"}));
}

}  // namespace
