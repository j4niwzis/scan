// A type of the caller's own joins in by declaring what any type declares.
//
// And the declaration is the same one either way: a scanner and a collector
// speak the same four hooks -- `parse`, `begin`, `push`, `finish` -- so a
// scanner handed where a collector is wanted is a collector of its own type,
// and nothing has to be written twice to have both.
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

TEST(CollectorScanner, TheScannerItselfIsACollector) {
  const std::string text = "500g";
  const auto found = scan::match<"([0-9]+)g">.into(scan::scanner<weight>{})(text);
  ASSERT_TRUE(found);
  EXPECT_EQ(found.get<1>().grams, 500);
}

TEST(CollectorScanner, AndBesideOneThatKeepsTheCharacters) {
  const std::string text = "500g-heavy";
  const auto found = scan::match<"([0-9]+)g-([a-z]+)">.into(
      scan::scanner<weight>{}, scan::text())(text);
  ASSERT_TRUE(found);
  EXPECT_EQ(found.get<1>().grams, 500);
  EXPECT_EQ(found.get<2>(), "heavy");
}

}  // namespace
