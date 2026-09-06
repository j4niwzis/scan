// The same reading said either way: called, or handed the subject by a pipe.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

struct pair {
  int left;
  int right;
};


TEST(Piped, ASubjectHandedToEach) {
  int total = 0;
  for (const pair& one : ("1:2 3:4 5:6"sv | scan::each<"{}:{}{* ?}">).of<pair>()) {
    total += one.left * one.right;
  }
  EXPECT_EQ(total, 2 + 12 + 30);
}

TEST(Piped, TheSameEachCalled) {
  int total = 0;
  for (const pair& one : scan::each<"{}:{}{* ?}">("1:2 3:4 5:6"sv).of<pair>()) {
    total += one.left * one.right;
  }
  EXPECT_EQ(total, 2 + 12 + 30);
}

}  // namespace
