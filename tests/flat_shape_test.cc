// A structure of structures is written out flat, because a product of products
// is flat and the nesting needs no saying.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct point { int x; int y; };
struct segment { point from; point to; };

TEST(flat_shape_test, a_structure_of_structures) {
  const std::string text = "1,2 3,4";
  const segment value = scan::scan<"{},{} {},{}">(text);
  EXPECT_EQ(value.from.x, 1);
  EXPECT_EQ(value.from.y, 2);
  EXPECT_EQ(value.to.x, 3);
  EXPECT_EQ(value.to.y, 4);
}

}  // namespace
