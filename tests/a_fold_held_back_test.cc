// A fold kept in one place where the machine cannot say what it is reading.
//
// The other test of one place reads a machine whose every move says what its
// character lies inside; there the turns go straight through. Here the first
// letter could belong to either group and only the second one settles it, so
// the turns are held back until the machine stands in one reading and are told
// then -- the same hooks in the same order, said later.
//
// What this says is that the two answer the same.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

struct as_they_go {
  std::string first;
  std::string second;
};

struct in_one_place {
  std::string first;
  std::string second;
};

struct both_ways {
  as_they_go left;
  in_one_place right;
};

// The caller's own thing, so the braced form has something to say.
struct room {
  int mark = 0;
};

}  // namespace

template <>
struct scan::scanner<as_they_go> {
  static constexpr std::string_view pattern() { return "([a-z]?)([a-z]+)"; }
  struct state {
    std::string first;
    std::string second;
  };
  static state begin_groups() { return {}; }
  static void push_group(state& one, std::size_t which, char letter) {
    (which == 0 ? one.first : one.second).push_back(letter);
  }
  static as_they_go finish_groups(state one) {
    return {std::move(one.first), std::move(one.second)};
  }
};

template <>
struct scan::scanner<in_one_place> {
  // The only difference between the two.
  static constexpr bool gathers_in_one_place = true;

  static constexpr std::string_view pattern() { return "([a-z]?)([a-z]+)"; }
  struct state {
    std::string first;
    std::string second;
  };
  static state begin_groups() { return {}; }
  static void push_group(state& one, std::size_t which, char letter) {
    (which == 0 ? one.first : one.second).push_back(letter);
  }
  static in_one_place finish_groups(state one) {
    return {std::move(one.first), std::move(one.second)};
  }
};

namespace {

TEST(AFoldHeldBack, ItReadsWhatTheOtherReads) {
  const room here{1};
  const room there{2};
  const both_ways got =
      scan::scan<"{} {}">("abc def"sv).of<both_ways>({here, there});
  // Each reads its own word, and the one kept in one place reads its word the
  // way the other reads its own: the first letter could have gone either way
  // and the second settles it.
  EXPECT_EQ(got.left.first, "a");
  EXPECT_EQ(got.left.second, "bc");
  EXPECT_EQ(got.right.first, "d");
  EXPECT_EQ(got.right.second, "ef");
}

// Told nothing at all, which is the road most callers are on: the one state
// still has somewhere to live, because one is made for it where the reading is
// asked for.
TEST(AFoldHeldBack, ToldNothingItIsReadTheSame) {
  const both_ways got = scan::scan<"{} {}">("ab cd"sv).of<both_ways>();
  EXPECT_EQ(got.right.first, "c");
  EXPECT_EQ(got.right.second, "d");
}

TEST(AFoldHeldBack, AndALongerOneToldNothing) {
  const both_ways got = scan::scan<"{} {}">("abcdef ghijkl"sv).of<both_ways>();
  EXPECT_EQ(got.right.first, "g");
  EXPECT_EQ(got.right.second, "hijkl");
}

}  // namespace
