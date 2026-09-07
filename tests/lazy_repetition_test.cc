// A repetition that prefers to stop.
//
// `a*` takes as many as it can and `a*?` takes as few as it can, and the
// difference is which of the two ways out of the loop is tried first. Both are
// always there, so laziness decides which parse wins and never whether one
// exists -- which is why the reading anchored at both ends matches the same
// subjects either way, and only the groups move.
//
// This was read as `a*` followed by a question mark to match, which is a
// pattern that means something else and says nothing about it. A fuzzer found
// it in a minute.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;


TEST(LazyRepetition, TheSameSubjectsMatchEitherWay) {
  EXPECT_TRUE(static_cast<bool>(scan::match<"[ab]*">("b"sv)));
  EXPECT_TRUE(static_cast<bool>(scan::match<"[ab]*?">("b"sv)));
  EXPECT_TRUE(static_cast<bool>(scan::match<"[ab]*?">("abab"sv)));
  EXPECT_TRUE(static_cast<bool>(scan::match<"[ab]*?">(""sv)));
  EXPECT_FALSE(static_cast<bool>(scan::match<"[ab]*?">("c"sv)));

  EXPECT_TRUE(static_cast<bool>(scan::match<"a+?">("aaa"sv)));
  EXPECT_TRUE(static_cast<bool>(scan::match<"a??">(""sv)));
  EXPECT_TRUE(static_cast<bool>(scan::match<"a??">("a"sv)));
  EXPECT_TRUE(static_cast<bool>(scan::match<"a{1,3}?">("aa"sv)));
}

TEST(LazyRepetition, ItDecidesWhereTheGroupsAre) {
  // Anchored at both ends there is only one way to divide "aaa" between a
  // greedy and a lazy half, and it is the other way round each time.
  const auto greedy = scan::match<"(a*)(a*)">("aaa"sv);
  ASSERT_TRUE(static_cast<bool>(greedy));
  EXPECT_EQ(greedy.get<1>().to_view(), "aaa"sv);
  EXPECT_EQ(greedy.get<2>().to_view(), ""sv);

  const auto lazy = scan::match<"(a*?)(a*)">("aaa"sv);
  ASSERT_TRUE(static_cast<bool>(lazy));
  EXPECT_EQ(lazy.get<1>().to_view(), ""sv);
  EXPECT_EQ(lazy.get<2>().to_view(), "aaa"sv);
}

TEST(LazyRepetition, AndWhereAHeadEnds) {
  // Here it does change the answer: a head stops where the walk that wins
  // stops, and the lazy one wins by stopping.
  const auto greedy = scan::starts_with<"[ab]*">("abab!"sv);
  ASSERT_TRUE(static_cast<bool>(greedy));
  EXPECT_EQ(greedy.whole().to_view(), "abab"sv);

  const auto lazy = scan::starts_with<"[ab]*?">("abab!"sv);
  ASSERT_TRUE(static_cast<bool>(lazy));
  EXPECT_EQ(lazy.whole().to_view(), ""sv);
}

TEST(LazyRepetition, APlusStillTakesOne) {
  const auto lazy = scan::starts_with<"a+?">("aaa"sv);
  ASSERT_TRUE(static_cast<bool>(lazy));
  EXPECT_EQ(lazy.whole().to_view(), "a"sv);
}

}  // namespace
