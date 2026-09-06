// A true input range: characters are taken one at a time and never buffered,
// so what stops the match is reported rather than pushed back.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct pair { int left; int right; };
struct one { int value; };
struct two_words { std::string left; std::string right; };


TEST(StreamTest, AWholeSubjectOffAStream) {
  std::istringstream source("4711");
  const one value = scan::scan<"{}">(std::views::istream<char>(source));
  EXPECT_EQ(value.value, 4711);
}

TEST(StreamTest, TwoFieldsOffAStream) {
  std::istringstream source("ab-cd");
  const two_words value =
      scan::scan<"{[a-z]+}-{[a-z]+}">(std::views::istream<char>(source));
  EXPECT_EQ(value.left, "ab");
  EXPECT_EQ(value.right, "cd");
}

}  // namespace
