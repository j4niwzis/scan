// Where a group is written with the pattern a type declares for its own
// values, the type is built from the groups the machine has already found.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct point {
  int x;
  int y;
};

}  // namespace

template <>
struct scan::scanner<point> : scan::aggregate_scanner<"({},{})"> {};

namespace {

inline constexpr scan::fixed_string spelled_out =
    "at=(\\(([+-]?[0-9]{1,}),([+-]?[0-9]{1,})\\))";

TEST(CollectorReuse, TheGroupSpellsTheTypeOut) {
  static_assert(scan::detail::group_spells_out<point, spelled_out, 1>(),
                "the group is the type's own pattern");
}

TEST(CollectorReuse, WhatTheReadingMakesTheSameIsTheSame) {
  // The trees are compared, not the characters -- so the spellings the reading
  // itself makes identical agree. `+` is `{1,}`, and a group that captures
  // nothing is not a node at all.
  static_assert(scan::detail::group_spells_out<
                    point, "at=(\\(([+-]?[0-9]{1,}),([+-]?[0-9]{1,})\\))", 1>(),
                "`+` and `{1,}` are one repetition");
  static_assert(scan::detail::group_spells_out<
                    point, "at=((?:\\(([+-]?[0-9]+),([+-]?[0-9]+)\\)))", 1>(),
                "a group that captures nothing leaves nothing behind");
}

TEST(CollectorReuse, ACountIsWrittenOutWhereThatIsSafe) {
  // `a{2}` and `aa` are the same two symbols, so long as there is no tag
  // inside the count: writing one out by hand cannot move what is not there.
  static_assert(scan::detail::group_spells_out<
                    point, "at=(\\(([+-]?[0-9]+),([+-]?[0-9]{1}[0-9]*)\\))", 1>(),
                "a count with nothing marked inside it is written out");
}

TEST(CollectorReuse, AndACountAroundAGroupIsNot) {
  // Here it would move them: `(a){2}` is one group that took two turns and
  // `(a)(a)` is two groups, which is a different answer and not a different
  // spelling. So a count around anything that marks a place is compared as a
  // count.
  struct pair_of_points { point first; point second; };
  static_assert(!scan::detail::group_spells_out<
                    point, "at=(\\(([+-]?[0-9]+),([+-]?[0-9]+)\\)\\(\\))", 1>(),
                "a different expression is a different expression");

  // And a class of one symbol stays a class. This one could be had and is
  // not: missing it costs one reading of the text, and being wrong about it
  // would cost the answer.
  static_assert(!scan::detail::group_spells_out<
                    point, "at=([(]([+-]?[0-9]+),([+-]?[0-9]+)[)])", 1>(),
                "a class of one symbol is read as a class");
}

TEST(CollectorReuse, AGroupWrittenSomeOtherWayIsNotReused) {
  static_assert(!scan::detail::group_spells_out<
                    point, "at=(\\(([0-9]+),([0-9]+)\\))", 1>(),
                "only the pattern the type declares is reused");
}

TEST(CollectorReuse, BuiltFromTheGroupsAlreadyFound) {
  const std::string text = "at=(3,-4)";
  const auto found = scan::match<spelled_out>.into(
      scan::as<point>(), scan::skip(), scan::skip())(text);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.get<1>().x, 3);
  EXPECT_EQ(found.get<1>().y, -4);
}

}  // namespace
