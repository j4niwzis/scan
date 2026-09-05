// Every match of one pattern, over a subject held in memory and over a stream.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct pair { int left; int right; };
struct set_command { scan::held<16> name; int value; };

TEST(each_test, every_record_of_a_contiguous_input) {
  const std::string text = "1,2;3,4;5,6;";
  int sum = 0;
  int count = 0;
  auto reading = scan::each<"{},{};">(text).of<pair>();
  for (const pair& value : reading) {
    sum += value.left + value.right;
    ++count;
  }
  EXPECT_EQ(sum, 21);
  EXPECT_EQ(count, 3);
  EXPECT_TRUE(reading.rest().empty());
}

TEST(each_test, and_what_it_would_not_take) {
  const std::string text = "1,2;3,4;oops";
  int count = 0;
  auto reading = scan::each<"{},{};">(text).of<pair>();
  for (const pair& value : reading) {
    (void)value;
    ++count;
  }
  EXPECT_EQ(count, 2);
  EXPECT_EQ(reading.rest(), "oops");
}

TEST(each_test, every_command_off_a_stream) {
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
