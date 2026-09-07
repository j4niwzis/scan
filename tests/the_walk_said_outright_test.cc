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
  EXPECT_TRUE(static_cast<bool>(scan::match<stamp>.scalar()(yes)));
  EXPECT_TRUE(static_cast<bool>(scan::match<stamp>.vec()(yes)));
  EXPECT_TRUE(static_cast<bool>(scan::match<stamp>.sentinel()(yes)));
  EXPECT_TRUE(static_cast<bool>(scan::match<stamp>.sentinel().scalar()(yes)));
  EXPECT_TRUE(static_cast<bool>(scan::match<stamp>.sentinel().vec()(yes)));

  EXPECT_FALSE(static_cast<bool>(scan::match<stamp>.scalar()(no)));
  EXPECT_FALSE(static_cast<bool>(scan::match<stamp>.vec()(no)));
  EXPECT_FALSE(static_cast<bool>(scan::match<stamp>.sentinel().scalar()(no)));
  EXPECT_FALSE(static_cast<bool>(scan::match<stamp>.sentinel().vec()(no)));

  // The one the length used to answer by itself: shorter than the shortest
  // match. A walk that never asks the length has to walk into the end of it.
  EXPECT_FALSE(static_cast<bool>(scan::match<stamp>.scalar()(cut)));
  EXPECT_FALSE(static_cast<bool>(scan::match<stamp>.sentinel().scalar()(cut)));
  EXPECT_FALSE(static_cast<bool>(scan::match<stamp>.vec()(cut)));
}

TEST(TheWalkSaidOutright, LongEnoughToBeWorthWordsAndShortEnoughNotToBe) {
  const std::string small = "alpha,bravo,charlie,delta,echo";
  const std::string large = std::string(200, 'a') + ",b,c,d,e";

  // A character at a time over a field of two hundred, and in words over one
  // of five: each of them reading what the other was chosen for.
  EXPECT_TRUE(static_cast<bool>(scan::match<row>.scalar()(large)));
  EXPECT_TRUE(static_cast<bool>(scan::match<row>.vec()(small)));
  EXPECT_TRUE(static_cast<bool>(scan::match<row>.sentinel().scalar()(large)));
  EXPECT_TRUE(static_cast<bool>(scan::match<row>.sentinel().vec()(small)));

  const std::string missing = "alpha,bravo,charlie,delta";
  EXPECT_FALSE(static_cast<bool>(scan::match<row>.scalar()(missing)));
  EXPECT_FALSE(static_cast<bool>(scan::match<row>.vec()(missing)));
}

TEST(TheWalkSaidOutright, AFormatReadsTheSameWayRoundToo) {
  const std::string text = "12:34";
  const pair by_length = scan::scan<"{}:{}">(text);
  const pair one_at_a_time = scan::scan<"{}:{}">.scalar()(text);
  const pair in_words = scan::scan<"{}:{}">.vec()(text);
  EXPECT_EQ(by_length.left, 12);
  EXPECT_EQ(one_at_a_time.left, 12);
  EXPECT_EQ(in_words.left, 12);
  EXPECT_EQ(by_length.right, 34);
  EXPECT_EQ(one_at_a_time.right, 34);
  EXPECT_EQ(in_words.right, 34);

  // And the same things said after the subject rather than before it: a scan
  // runs when the output type is named, so until then it is only a
  // description of one and saying more about it is free.
  const pair after = scan::scan<"{}:{}">(text).scalar().of<pair>();
  const pair after_terminated =
      scan::scan<"{}:{}">(text).sentinel().vec().of<pair>();
  EXPECT_EQ(after.left, 12);
  EXPECT_EQ(after.right, 34);
  EXPECT_EQ(after_terminated.left, 12);
  EXPECT_EQ(after_terminated.right, 34);

  // And the type named before the subject, which finishes the reading: what
  // comes back then is a whole scan with nothing left to say, and it can be
  // kept and used again.
  constexpr auto read = scan::scan<"{}:{}">.sentinel().of<pair>();
  const pair ahead = read(text);
  EXPECT_EQ(ahead.left, 12);
  EXPECT_EQ(ahead.right, 34);

  const auto maybe = scan::scan<"{}:{}">.try_of<pair>()(text);
  ASSERT_TRUE(maybe.has_value());
  EXPECT_EQ(maybe->right, 34);
  const std::string nothing = "nope";
  EXPECT_FALSE(scan::scan<"{}:{}">.try_of<pair>()(nothing).has_value());

  const pair terminated = scan::scan<"{}:{}">.sentinel()(text);
  const pair terminated_scalar = scan::scan<"{}:{}">.sentinel().scalar()(text);
  const pair terminated_vec = scan::scan<"{}:{}">.sentinel().vec()(text);
  EXPECT_EQ(terminated.right, 34);
  EXPECT_EQ(terminated_scalar.right, 34);
  EXPECT_EQ(terminated_vec.right, 34);
}

TEST(TheWalkSaidOutright, ATerminatedSubjectKeepsItsGroups) {
  // What the terminator saves is the end test, and a group is written by the
  // same operations whether the end is tested or not. This used to be refused
  // outright.
  const std::string text = "42-abc";
  const auto found = scan::match<"([0-9]+)-([a-z]+)">.sentinel()(text);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.get<1>().to_view(), "42"sv);
  EXPECT_EQ(found.get<2>().to_view(), "abc"sv);

  const auto one_at_a_time =
      scan::match<"([0-9]+)-([a-z]+)">.sentinel().scalar()(text);
  ASSERT_TRUE(static_cast<bool>(one_at_a_time));
  EXPECT_EQ(one_at_a_time.get<2>().to_view(), "abc"sv);

  const auto in_words = scan::match<"([0-9]+)-([a-z]+)">.sentinel().vec()(text);
  ASSERT_TRUE(static_cast<bool>(in_words));
  EXPECT_EQ(in_words.get<1>().to_view(), "42"sv);
}

}  // namespace
