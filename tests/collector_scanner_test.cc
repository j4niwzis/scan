// A type of the caller's own joins in by declaring what any type declares.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct weight {
  int grams = 0;
};

}  // namespace

template <>
struct scan::scanner<weight> {
  static constexpr std::string pattern(std::string_view) { return "[0-9]+"; }
  static constexpr weight parse(std::string_view text, std::string_view) {
    int made = 0;
    for (const char letter : text) made = made * 10 + (letter - '0');
    return weight{made};
  }
};

namespace {

TEST(CollectorScanner, ATypeThatSaysHowItReadsItself) {
  const std::string text = "500g";
  const auto found = scan::match<"([0-9]+)g">.into(scan::as<weight>())(text);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.get<1>().grams, 500);
}

}  // namespace
