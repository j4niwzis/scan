// The same reading off a subject that can only be read once: `for`, then
// `each`, and the words owned because there is nothing left to point at.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct the_whole_word { scan::held<8> text; };
struct the_short_word { scan::held<8> text; };
struct the_rest_word { scan::held<8> text; };

using keyword = std::variant<the_whole_word, the_short_word, the_rest_word>;
struct row { keyword value; };


TEST(FirstAccept, ForAndEachOffAStream) {
  std::istringstream source("foreach");
  source >> std::noskipws;
  std::vector<std::string> seen;
  std::vector<std::size_t> which;
  for (const row& one : scan::each<"{{foreach}|{for}|{each}}">(
                            std::views::istream<char>(source))
                            .of<row>()) {
    which.push_back(one.value.index());
    std::visit([&](const auto& what) {
      seen.push_back(std::string(what.text.view()));
    }, one.value);
  }
  EXPECT_EQ(seen, std::vector<std::string>({"for", "each"}));
  EXPECT_EQ(which, std::vector<std::size_t>({1u, 2u}));
}

}  // namespace
