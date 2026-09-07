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

TEST(CollectorReuse, AndWhatItDoesNotIsNot) {
  // Deliberately not equal, though a reader might call them the same thing.
  //
  // `a{2}` and `aa` are the same characters and not the same expression: put
  // a group around them and they stop being alike at all -- `(a){2}` is one
  // group that took two turns, `(a)(a)` is two groups. Writing a count out by
  // hand can move the groups, so the count is compared as a count.
  static_assert(!scan::detail::group_spells_out<
                    point, "at=(\\(([+-]?[0-9]+),([+-]?[0-9]+)\\)\\(\\))", 1>(),
                "a different expression is a different expression");

  // And a class of one symbol is a class, not that symbol. Reuse could be had
  // here and is not: what it costs to miss it is that the text is read the
  // ordinary way, and what it would cost to be wrong about it is a wrong
  // answer.
  static_assert(!scan::detail::group_spells_out<
                    point, "at=([(]([+-]?[0-9]+),([+-]?[0-9]+)[)])", 1>(),
                "a class of one symbol is written differently and read "
                "differently");
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
