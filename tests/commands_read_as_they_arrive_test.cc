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


TEST(ReaderTest, CommandsReadAsTheyArrive) {
  static constexpr std::string_view arriving = "set speed 42\nset gain 7\n";
  scan::reader<set_command, "set {[a-z]+} {[0-9]+}"> reading;
  std::array<set_command, 4> got{};
  std::size_t taken = 0;
  std::size_t echoed = 0;
  for (char symbol : arriving) {
    ++echoed;  // the side effect, as it arrives
    if (reading.offer(symbol)) continue;
    if (reading.accepting() && taken < got.size()) got[taken++] = reading.take();
    reading.restart();
    (void)reading.offer(symbol);  // the separator
  }
  if (reading.accepting() && taken < got.size()) got[taken++] = reading.take();
  EXPECT_EQ(taken, 2u);
  EXPECT_EQ(got[0].name.view(), "speed");
  EXPECT_EQ(got[0].value, 42);
  EXPECT_EQ(got[1].name.view(), "gain");
  EXPECT_EQ(got[1].value, 7);
  EXPECT_EQ(echoed, arriving.size());
}

TEST(ReaderTest, ACommandThatHasJustBecomeWhole) {
  scan::reader<set_command, "set {[a-z]+} {[0-9]+}"> reading;
  for (char symbol : std::string_view("set x 1")) {
    if (!reading.offer(symbol)) break;
  }
  EXPECT_TRUE(reading.accepting());
}

}  // namespace
