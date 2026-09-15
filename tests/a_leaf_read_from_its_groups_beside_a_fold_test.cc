// A leaf built from its own groups, standing beside one that folds.
//
// Beside a fold the shape is read by the machine that gathers, and there the
// groups of such a leaf are found through the reading -- which is where a tag
// stands while the walk is still going. Two things follow, and both were
// wrong. A mark is written as the walk stands on the character, so what a
// group stood on is what lies between its marks and nothing is taken off; the
// step that used to be taken off here was a character of every group. And a
// group still open where the match ends is closed by the commands that end a
// match and by nothing else, which write registers of their own -- so through
// the reading alone it looked like a group that never closed, and came back
// empty.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

// Told its own groups as the walk passes them, so that the shape below is read
// by the machine that gathers rather than out of the marks alone.
struct tally {
  unsigned long value = 0;
};

// Built from its own groups once the match is over.
struct pair_of {
  int a = 0;
  int b = 0;
};

}  // namespace

template <>
struct scan::scanner<tally> {
  [[nodiscard]] static constexpr std::string_view pattern() {
    return R"(\(((_+)((A)|(B)))*\))";
  }

  struct state_type {
    unsigned long total = 0;
    unsigned place = 0;
    unsigned digit = 0;
  };

  [[nodiscard]] static constexpr state_type begin_groups() { return {}; }

  static constexpr void opened_group(state_type& one, scan::group_at<0>) {
    one.place = 0;
    one.digit = 0;
  }

  static constexpr void closed_group(state_type& one, scan::group_at<0>) {
    unsigned long weight = 1;
    for (unsigned step = 1; step < one.place; ++step) weight *= 10;
    one.total += weight * one.digit;
  }

  static constexpr void push_group(state_type& one, scan::group_at<1>, char) {
    ++one.place;
  }

  static constexpr void push_group(state_type& one, scan::group_at<3>, char) {
    one.digit = 1;
  }

  static constexpr void push_group(state_type& one, scan::group_at<4>, char) {
    one.digit = 2;
  }

  static constexpr void push_group(state_type&, std::size_t, char) {}

  [[nodiscard]] static constexpr tally finish_groups(state_type one) {
    return {one.total};
  }
};

template <>
struct scan::scanner<pair_of> {
  [[nodiscard]] static constexpr std::string_view pattern() {
    return R"(([0-9]+)-([0-9]+))";
  }

  [[nodiscard]] static constexpr pair_of from_groups(
      std::span<const std::string_view> given) {
    const auto number = [](std::string_view text) {
      int made = 0;
      for (const char letter : text) made = made * 10 + (letter - '0');
      return made;
    };
    return {number(given[0]), number(given[1])};
  }
};

namespace {

struct beside {
  tally number;
  pair_of pair;
};

TEST(ALeafReadFromItsGroupsBesideAFold, TheLastGroupRunsToTheEndOfTheInput) {
  const auto got = scan::scan<"value={}{}">("value=(_A__B)12-345"sv).of<beside>();
  EXPECT_EQ(got.number.value, 21u);
  EXPECT_EQ(got.pair.a, 12);
  EXPECT_EQ(got.pair.b, 345);
}

TEST(ALeafReadFromItsGroupsBesideAFold, EveryGroupClosesInsideTheInput) {
  const auto got =
      scan::scan<"value={}{}!">("value=(_A__B)12-345!"sv).of<beside>();
  EXPECT_EQ(got.number.value, 21u);
  EXPECT_EQ(got.pair.a, 12);
  EXPECT_EQ(got.pair.b, 345);
}

}  // namespace
