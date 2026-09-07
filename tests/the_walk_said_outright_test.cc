// Which walk reads the subject is a question about speed, not about meaning.
//
// A reading that is not told picks by how much there is: a subject long enough
// for words is read in them, a short one a character at a time. Told outright,
// it does not look at the length at all -- and the walk that was not asked for
// is not written, which for a subject of a few dozen characters is the whole
// of the code that was there for the other one.
//
// Whichever is chosen, the answer is the same. That is what this holds down.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

constexpr auto stamp = "[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}";
constexpr auto row = "[a-z]+,[a-z]+,[a-z]+,[a-z]+,[a-z]+";

struct pair { int left; int right; };


TEST(TheWalkSaidOutright, TheSameAnswerWhicheverWalks) {
  const std::string yes = "2024-05-17T13:45:59";
  const std::string no = "2024-05-17X13:45:59";
  const std::string cut = "2024-05";

  EXPECT_TRUE(static_cast<bool>(scan::match<stamp>(yes)));
  EXPECT_TRUE(static_cast<bool>(scan::match_scalar<stamp>(yes)));
  EXPECT_TRUE(static_cast<bool>(scan::match_vec<stamp>(yes)));
  EXPECT_TRUE(static_cast<bool>(scan::match_sentinel<stamp>(yes)));
  EXPECT_TRUE(static_cast<bool>(scan::match_sentinel_scalar<stamp>(yes)));
  EXPECT_TRUE(static_cast<bool>(scan::match_sentinel_vec<stamp>(yes)));

  EXPECT_FALSE(static_cast<bool>(scan::match_scalar<stamp>(no)));
  EXPECT_FALSE(static_cast<bool>(scan::match_vec<stamp>(no)));
  EXPECT_FALSE(static_cast<bool>(scan::match_sentinel_scalar<stamp>(no)));
  EXPECT_FALSE(static_cast<bool>(scan::match_sentinel_vec<stamp>(no)));

  // The one the length used to answer by itself: shorter than the shortest
  // match. A walk that never asks the length has to walk into the end of it.
  EXPECT_FALSE(static_cast<bool>(scan::match_scalar<stamp>(cut)));
  EXPECT_FALSE(static_cast<bool>(scan::match_sentinel_scalar<stamp>(cut)));
  EXPECT_FALSE(static_cast<bool>(scan::match_vec<stamp>(cut)));
}

TEST(TheWalkSaidOutright, LongEnoughToBeWorthWordsAndShortEnoughNotToBe) {
  const std::string small = "alpha,bravo,charlie,delta,echo";
  const std::string large = std::string(200, 'a') + ",b,c,d,e";

  // A character at a time over a field of two hundred, and in words over one
  // of five: each of them reading what the other was chosen for.
  EXPECT_TRUE(static_cast<bool>(scan::match_scalar<row>(large)));
  EXPECT_TRUE(static_cast<bool>(scan::match_vec<row>(small)));
  EXPECT_TRUE(static_cast<bool>(scan::match_sentinel_scalar<row>(large)));
  EXPECT_TRUE(static_cast<bool>(scan::match_sentinel_vec<row>(small)));

  const std::string missing = "alpha,bravo,charlie,delta";
  EXPECT_FALSE(static_cast<bool>(scan::match_scalar<row>(missing)));
  EXPECT_FALSE(static_cast<bool>(scan::match_vec<row>(missing)));
}

TEST(TheWalkSaidOutright, AFormatReadsTheSameWayRoundToo) {
  const std::string text = "12:34";
  const pair by_length = scan::scan<"{}:{}">(text);
  const pair one_at_a_time = scan::scan_scalar<"{}:{}">(text);
  const pair in_words = scan::scan_vec<"{}:{}">(text);
  EXPECT_EQ(by_length.left, 12);
  EXPECT_EQ(one_at_a_time.left, 12);
  EXPECT_EQ(in_words.left, 12);
  EXPECT_EQ(by_length.right, 34);
  EXPECT_EQ(one_at_a_time.right, 34);
  EXPECT_EQ(in_words.right, 34);

  const pair terminated = scan::scan_sentinel<"{}:{}">(text);
  const pair terminated_scalar = scan::scan_sentinel_scalar<"{}:{}">(text);
  const pair terminated_vec = scan::scan_sentinel_vec<"{}:{}">(text);
  EXPECT_EQ(terminated.right, 34);
  EXPECT_EQ(terminated_scalar.right, 34);
  EXPECT_EQ(terminated_vec.right, 34);
}

}  // namespace
