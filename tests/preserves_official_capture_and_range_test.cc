import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

// Ported from CTRE's official tests/matching.cpp and tests/range.cpp:
// https://github.com/hanickadot/compile-time-regular-expressions/tree/main/tests
// Only the namespace is changed where the regex syntax is supported here.

using namespace std::string_view_literals;


TEST(CtreCompatTest, PreservesOfficialCaptureAndRangeContract) {
  const auto result =
      scan::match<"([a-z]++),([a-z]++),([a-z]++),([a-z]++),([a-z]++)">(
          "one,two,three,four,five"sv);

  ASSERT_TRUE(result);
  EXPECT_EQ(result.get<1>().to_view(), "one"sv);
  EXPECT_EQ(result.get<2>().to_view(), "two"sv);
  EXPECT_EQ(result.get<3>().to_view(), "three"sv);
  EXPECT_EQ(result.get<4>().to_view(), "four"sv);
  EXPECT_EQ(result.get<5>().to_view(), "five"sv);

  // Found as they are asked for, so what is wanted as a container is said to
  // be one.
  const auto matches = scan::search_all<"([0-9])[0-9]++">("123,456,768"sv) |
                       std::ranges::to<std::vector>();
  ASSERT_EQ(matches.size(), 3);
  EXPECT_EQ(matches[0].get<1>().to_view(), "1"sv);
  EXPECT_EQ(matches[1].get<1>().to_view(), "4"sv);
  EXPECT_EQ(matches[2].get<1>().to_view(), "7"sv);

  std::string leading;
  for (const auto& one : scan::search_all<"([0-9])[0-9]++">("123,456,768"sv)) {
    leading += one.get<1>().to_view();
  }
  EXPECT_EQ(leading, "147");
}
