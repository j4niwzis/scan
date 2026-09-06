// A type that declares the format it is read by, spread into the automaton of
// whatever contains it -- no second pass over the text it matched.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct coordinates { int x; int y; };
struct rectangle { coordinates corner; coordinates opposite; };

}  // namespace

template <> struct scan::scanner<coordinates>
    : scan::aggregate_scanner<"({}, {})"> {};
template <> struct scan::scanner<rectangle>
    : scan::aggregate_scanner<"[{} -> {}]"> {};

namespace {

struct wrapped_rectangle { rectangle bounds; };

TEST(ShapeFormatTest, AShapeOfShapes) {
  const std::string text = "[(1, 2) -> (3, 4)]";
  const wrapped_rectangle value = scan::scan<"{}">(text);
  EXPECT_EQ(value.bounds.corner.x, 1);
  EXPECT_EQ(value.bounds.corner.y, 2);
  EXPECT_EQ(value.bounds.opposite.x, 3);
  EXPECT_EQ(value.bounds.opposite.y, 4);
}

}  // namespace
