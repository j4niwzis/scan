// Reading a head off the front of a subject, and being told what follows it.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct pair { int left; int right; };


TEST(PrefixTest, AssignedWithTheRestDropped) {
  const std::string text = "7 8 tail";
  const pair value = scan::scan_prefix<"{}{*\\s*}{}">(text);
  EXPECT_EQ(value.left, 7);
  EXPECT_EQ(value.right, 8);
}

TEST(PrefixTest, AnInputThatDoesNotBeginWithIt) {
  const std::string text = "nothing here";
  EXPECT_THROW((void)scan::scan_prefix<"{},{}">(text).take<pair>(),
               std::exception);
}

}  // namespace
