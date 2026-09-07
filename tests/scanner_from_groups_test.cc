// A type handed the groups the match already found, rather than the text.
//
// Where a type says a pattern of its own and that pattern has groups in it,
// those groups are groups of the big match like any others -- the machine
// found them on its way past. A type that says `from_groups` is handed exactly
// its own, in the order it wrote them, and nothing is read a second time.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

struct version {
  int major = 0;
  int minor = 0;
  int patch = 0;
};

// What was handed over, so that the test can say the text was not read again.
inline int times_read_as_text = 0;

}  // namespace

template <>
struct scan::scanner<version> {
  static constexpr std::string_view pattern() {
    return "([0-9]+)\\.([0-9]+)\\.([0-9]+)";
  }

  // The groups this pattern opens, in the order it opened them.
  static constexpr version from_groups(
      std::span<const std::string_view> groups) {
    const auto number = [](std::string_view text) {
      int made = 0;
      for (char letter : text) made = made * 10 + (letter - '0');
      return made;
    };
    return version{number(groups[0]), number(groups[1]), number(groups[2])};
  }

  // And the way it would be read if the groups were not there to be had.
  static version parse(std::string_view text) {
    ++times_read_as_text;
    version made;
    const auto stop = text.find('.');
    const auto next = text.find('.', stop + 1);
    const auto number = [&](std::size_t from, std::size_t to) {
      int value = 0;
      for (std::size_t at = from; at < to; ++at) value = value * 10 + (text[at] - '0');
      return value;
    };
    made.major = number(0, stop);
    made.minor = number(stop + 1, next);
    made.patch = number(next + 1, text.size());
    return made;
  }
};

namespace {

TEST(ScannerFromGroups, HandedTheGroupsItWrote) {
  times_read_as_text = 0;
  const std::string text = "v=1.22.333!";
  const auto found =
      scan::match<"v=(([0-9]+)\\.([0-9]+)\\.([0-9]+))!">.into(
          scan::as<version>(), scan::skip(), scan::skip(), scan::skip())(text);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.get<1>().major, 1);
  EXPECT_EQ(found.get<1>().minor, 22);
  EXPECT_EQ(found.get<1>().patch, 333);
  EXPECT_EQ(times_read_as_text, 0);
}

TEST(ScannerFromGroups, WrittenAnotherWayItReadsTheText) {
  // The same language, a different expression: the groups the type wrote are
  // not the groups that are there, so there is nothing to hand over and the
  // text is read the ordinary way.
  times_read_as_text = 0;
  const std::string text = "v=1.22.333!";
  const auto found = scan::match<"v=([0-9]+\\.[0-9]+\\.[0-9]+)!">.into(
      scan::as<version>())(text);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.get<1>().minor, 22);
  EXPECT_EQ(times_read_as_text, 1);
}

TEST(ScannerFromGroups, AndTheCountIsWrittenOutWhereItCan) {
  // `[0-9]{1,}` is `[0-9]+`, so this is the same expression and the groups are
  // handed over.
  times_read_as_text = 0;
  const std::string text = "v=1.22.333!";
  const auto found =
      scan::match<"v=(([0-9]{1,})\\.([0-9]+)\\.([0-9]+))!">.into(
          scan::as<version>(), scan::skip(), scan::skip(), scan::skip())(text);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.get<1>().major, 1);
  EXPECT_EQ(times_read_as_text, 0);
}

}  // namespace
