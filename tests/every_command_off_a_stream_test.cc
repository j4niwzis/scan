// Every match of one pattern, over a subject held in memory and over a stream.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct pair { int left; int right; };
struct set_command { scan::held<16> name; int value; };


TEST(EachTest, EveryCommandOffAStream) {
  std::istringstream source("set speed 42\nset gain 7\nset trim 3\n");
  auto letters = std::ranges::subrange(std::istreambuf_iterator<char>(source),
                                       std::istreambuf_iterator<char>());
  int count = 0;
  scan::held<16> last{};
  for (const set_command& command :
       scan::each<"set {[a-z]+} {[0-9]+}\n">(std::move(letters))
           .of<set_command>()) {
    last = command.name;
    ++count;
  }
  EXPECT_EQ(count, 3);
  EXPECT_EQ(last.view(), "trim");
}

}  // namespace
