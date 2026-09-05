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

TEST(reader_test, commands_read_as_they_arrive) {
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

TEST(reader_test, a_command_that_has_just_become_whole) {
  scan::reader<set_command, "set {[a-z]+} {[0-9]+}"> reading;
  for (char symbol : std::string_view("set x 1")) {
    if (!reading.offer(symbol)) break;
  }
  EXPECT_TRUE(reading.accepting());
}

TEST(reader_test, a_field_wider_than_the_room_it_was_given) {
  scan::reader<set_command, "set {[a-z]+} {[0-9]+}"> reading;
  for (char symbol : std::string_view("set abcdefghijklmnopqrstuvwxyz 5")) {
    if (!reading.offer(symbol)) break;
  }
  const set_command value = reading.take();
  EXPECT_TRUE(value.name.overflowed);
  EXPECT_EQ(value.name.view().size(), 16u);
}

TEST(reader_test, the_name_as_it_is_being_typed) {
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
