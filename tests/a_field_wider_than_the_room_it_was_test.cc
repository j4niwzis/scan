// The incremental reader: characters are offered one at a time, and the groups
// being gathered can be looked at while they are still growing.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct set_command {
  scan::held<16> name;
  int value;
};


TEST(ReaderTest, AFieldWiderThanTheRoomItWasGiven) {
  scan::reader<set_command, "set {[a-z]+} {[0-9]+}"> reading;
  for (char symbol : std::string_view("set abcdefghijklmnopqrstuvwxyz 5")) {
    if (!reading.offer(symbol)) break;
  }
  const set_command value = reading.take();
  EXPECT_TRUE(value.name.overflowed);
  EXPECT_EQ(value.name.view().size(), 16u);
}

TEST(ReaderTest, TheNameAsItIsBeingTyped) {
  scan::reader<set_command, "set {[a-z]+} {[0-9]+}"> reading;
  std::vector<std::string> seen;
  for (char symbol : std::string_view("set speed 42")) {
    if (!reading.offer(symbol)) break;
    if (reading.reading<0>()) {
      seen.emplace_back(reading.gathering<0>().view());
    }
  }
  ASSERT_EQ(seen.size(), 5u);
  EXPECT_EQ(seen.front(), "s");
  EXPECT_EQ(seen.back(), "speed");

  // and closed once the value begins
  EXPECT_TRUE(reading.accepting());
  EXPECT_EQ(reading.gathering<0>().view(), "speed");
  EXPECT_FALSE(reading.reading<0>());

  const set_command whole = reading.take();
  EXPECT_EQ(whole.name.view(), "speed");
  EXPECT_EQ(whole.value, 42);
}

}  // namespace
