// Every match of one pattern, over a subject held in memory and over a stream.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct pair { int left; int right; };
struct set_command { scan::held<16> name; int value; };


TEST(EachTest, EveryRecordOfAContiguousInput) {
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

TEST(EachTest, AndWhatItWouldNotTake) {
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

}  // namespace
