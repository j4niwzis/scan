// Whitespace in front of a place, said once instead of written out.
//
// `{}` takes what it is given and no more, which is what a format written for
// this library wants. `%d` skips whatever whitespace is in front of it, which
// is what a format written for `sscanf` expects. `past_space` is the second
// one asked for by name: every place in the format begins past whatever
// whitespace is there, and the whitespace is not kept.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

struct pair { int left; int right; };

inline constexpr auto strict = scan::fixed_string("{}:{}");
inline constexpr auto lenient = scan::fixed_string("{}:{}").past_space();


TEST(PastSpace, TheSameFormatSaidTheOtherWay) {
  const std::string tight = "12:34";
  const pair by_the_letter = scan::scan<strict>(tight);
  EXPECT_EQ(by_the_letter.left, 12);
  EXPECT_EQ(by_the_letter.right, 34);

  const pair skipping = scan::scan<lenient>(tight);
  EXPECT_EQ(skipping.left, 12);
  EXPECT_EQ(skipping.right, 34);
}

TEST(PastSpace, WhitespaceInFrontOfEveryPlace) {
  const std::string loose = "  12:   34";
  // Written as it stands, the format does not allow for it.
  EXPECT_FALSE(scan::scan<strict>(loose).try_of<pair>().has_value());

  const pair value = scan::scan<lenient>(loose);
  EXPECT_EQ(value.left, 12);
  EXPECT_EQ(value.right, 34);

  // Any amount of it, including none and including newlines.
  const std::string spread = "\n\t12:\n34";
  const pair across = scan::scan<lenient>(spread);
  EXPECT_EQ(across.left, 12);
  EXPECT_EQ(across.right, 34);
}

TEST(PastSpace, TheSpaceIsNotKept) {
  // What is skipped is not part of any field: the numbers are the numbers, and
  // a place that gathers text gathers none of the whitespace in front of it.
  struct words { std::string_view left; std::string_view right; };
  constexpr auto text_format = scan::fixed_string("{[a-z]+},{[a-z]+}").past_space();
  const std::string loose = " alpha,  bravo";
  const words value = scan::scan<text_format>(loose);
  EXPECT_EQ(value.left, "alpha"sv);
  EXPECT_EQ(value.right, "bravo"sv);
}

TEST(PastSpace, ItIsPartOfTheKey) {
  // The two formats are two formats: what is built for one cannot be handed to
  // a reading of the other, because the value that keys them differs.
  static_assert(!std::same_as<decltype(strict), decltype(lenient)> ||
                strict.space_before_places != lenient.space_before_places);
  static_assert(!strict.space_before_places);
  static_assert(lenient.space_before_places);
}

}  // namespace
