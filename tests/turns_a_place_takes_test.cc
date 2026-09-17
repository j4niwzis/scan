// How many turns a place is written to take.
//
// A star, a plus and a question mark are counts said shorter, and they are said
// as counts here: `*` is `{0,}`, `+` is `{1,}`, `?` is `{0,1}`. One shape
// downstream means a list reads the same however its turns were written -- and
// a question mark, which used to be the odd one out, is a list of at most one.
//
// What the count says, the machine keeps: too few turns or too many is not a
// list to be trimmed afterwards, it is a subject that does not match.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

struct row {
  std::vector<int> values;
};

struct pair_row {
  int head;
  std::vector<int> tail;
};

}  // namespace

namespace {

TEST(TurnsAPlaceTakes, AQuestionMarkIsAListOfAtMostOne) {
  const auto none = scan::scan<"{} {{}{*,?}}?">("7 "sv).try_of<pair_row>();
  ASSERT_TRUE(none.has_value());
  EXPECT_EQ(none->head, 7);
  EXPECT_TRUE(none->tail.empty());

  const auto one = scan::scan<"{} {{}{*,?}}?">("7 42"sv).try_of<pair_row>();
  ASSERT_TRUE(one.has_value());
  ASSERT_EQ(one->tail.size(), 1u);
  EXPECT_EQ(one->tail[0], 42);
}

TEST(TurnsAPlaceTakes, ACountIsKeptByTheMachine) {
  const auto two = scan::scan<"{{}{*,?}}{2,3}">("1,2"sv).try_of<row>();
  ASSERT_TRUE(two.has_value());
  EXPECT_EQ(two->values.size(), 2u);

  const auto three = scan::scan<"{{}{*,?}}{2,3}">("1,2,3"sv).try_of<row>();
  ASSERT_TRUE(three.has_value());
  EXPECT_EQ(three->values.size(), 3u);
}

TEST(TurnsAPlaceTakes, FewerOrMoreIsNotAMatch) {
  EXPECT_FALSE(scan::scan<"{{}{*,?}}{2,3}">("1"sv).try_of<row>().has_value());
  EXPECT_FALSE(
      scan::scan<"{{}{*,?}}{2,3}">("1,2,3,4"sv).try_of<row>().has_value());
}

}  // namespace
