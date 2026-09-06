// A value of any type at all, filled by a call of the caller's own.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct counted {
  std::size_t letters = 0;
  std::size_t digits = 0;
};

TEST(CollectorPusher, CountingInsteadOfKeeping) {
  const std::string text = "abcd-99";
  const auto found = scan::match<"([a-z]+)-([0-9]+)">.into(
      scan::collecting<std::size_t>(
          [](std::size_t& into, char) { ++into; }),
      scan::as<int>())(text);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.get<1>(), 4u);
  EXPECT_EQ(found.get<2>(), 99);
}

TEST(CollectorPusher, SomethingThatIsNoStringAtAll) {
  const std::string text = "ab12";
  const auto found = scan::match<"([a-z0-9]+)">.into(
      scan::collecting<counted>([](counted& into, char letter) {
        if (letter >= '0' && letter <= '9') {
          ++into.digits;
        } else {
          ++into.letters;
        }
      }))(text);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.get<1>().letters, 2u);
  EXPECT_EQ(found.get<1>().digits, 2u);
}

}  // namespace
