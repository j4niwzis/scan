// A list whose elements have parts of their own, and a list beside a field.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct point { int x; int y; };
struct path { std::vector<point> points; };
struct named { scan::held<8> name; std::vector<int> marks; };

TEST(ListShapeTest, AListOfThingsWithParts) {
  const std::string text = "1:2 3:4 5:6";
  const path value = scan::scan<"{{}:{}{* ?}}+">(text);
  ASSERT_EQ(value.points.size(), 3u);
  EXPECT_EQ(value.points[0].x, 1);
  EXPECT_EQ(value.points[2].x, 5);
  EXPECT_EQ(value.points[2].y, 6);
}

TEST(ListShapeTest, AListBesideSomethingElse) {
  const std::string text = "run 1,2,3";
  const named value = scan::scan<"{[a-z]+} {{}{*,?}}+">(text);
  EXPECT_EQ(value.name.view(), "run");
  ASSERT_EQ(value.marks.size(), 3u);
  EXPECT_EQ(value.marks[1], 2);
}

}  // namespace
