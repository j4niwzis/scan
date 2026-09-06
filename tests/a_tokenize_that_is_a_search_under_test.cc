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


TEST(Piped, ATokenizeThatIsASearchUnderAnotherName) {
  const auto tokens = "a  bb  ccc"sv | scan::tokenize<"[a-z]+"> |
                      std::ranges::to<std::vector>();
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[2].to_view(), "ccc"sv);
}

}  // namespace
