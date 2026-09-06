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


TEST(Piped, ASubjectHandedToASplit) {
  const auto pieces =
      "Hello brave world!"sv | scan::split<" "> | std::ranges::to<std::vector>();
  EXPECT_EQ(pieces, std::vector<std::string_view>(
                        {"Hello"sv, "brave"sv, "world!"sv}));
}

TEST(Piped, TheSameSplitCalled) {
  const auto pieces = scan::split<" ">("Hello brave world!"sv) |
                      std::ranges::to<std::vector>();
  EXPECT_EQ(pieces, std::vector<std::string_view>(
                        {"Hello"sv, "brave"sv, "world!"sv}));
}

}  // namespace
