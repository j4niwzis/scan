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
