// The same reading off a subject that can only be read once: `for`, then
// `each`, with the words owned because there is nothing left to point at.
//
// The pattern with `foreach` in it is not offered here at all, and says why
// where it is compiled: from the accepting place after `for` it can walk on,
// and a subject read once cannot be walked back to where it should have
// stopped. `for|each` cannot walk past an ending of its own, so it is read as
// it arrives.
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

using any_of_three = std::variant<whole_word, for_word, each_word>;
struct wide_row { any_of_three value; };

static_assert(!scan::detail::can_walk_past_the_end<
              scan::detail::streaming_automaton<row, "{{for}|{each}}">>());
static_assert(scan::detail::can_walk_past_the_end<
              scan::detail::streaming_automaton<
                  wide_row, "{{foreach}|{for}|{each}}">>());


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

}  // namespace
