// A scanner that says what went wrong instead of throwing it.
//
// The kind of failure is read off the return type of `try_parse`, so there is
// no list to write down and no list to keep in step with the code: whatever a
// reading of this output can go wrong with is what it hands back.
//
// A scanner that throws instead can still say its kinds, with `throws`, and
// that is the only case where anything has to be declared.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

struct too_heavy : scan::scan_error {
  using scan_error::scan_error;
};

struct not_a_weight : scan::scan_error {
  using scan_error::scan_error;
};

struct weight {
  int grams = 0;
};

struct thrown_at_you : scan::scan_error {
  using scan_error::scan_error;
};

struct label {
  std::string text;
};

}  // namespace

template <>
struct scan::scanner<weight> {
  static constexpr std::string_view pattern() { return "[0-9]+"; }

  // The kinds are these two, said where they cannot fall out of step with the
  // code: in the type this hands back.
  static std::expected<weight, std::variant<too_heavy, not_a_weight>> try_parse(
      std::string_view text) {
    int made = 0;
    for (char letter : text) {
      made = made * 10 + (letter - '0');
      if (made > 10000) return std::unexpected(too_heavy("over ten kilos"));
    }
    if (text.empty()) return std::unexpected(not_a_weight("nothing there"));
    return weight{made};
  }
};

template <>
struct scan::scanner<label> {
  static constexpr std::string_view pattern() { return "[a-z]+"; }

  // This one throws instead. Nothing in the library catches, so it goes past
  // the reading to whoever called -- even a reading that was asked to try.
  static label parse(std::string_view text) {
    if (text == "no") throw thrown_at_you("that label is not allowed");
    return label{std::string(text)};
  }
};

namespace {

struct package {
  weight how_heavy;
  label name;
};

TEST(AFailureOfYourOwn, TheValueWhereNothingWentWrong) {
  const package one = scan::scan<"{} {}">("450 flour"sv).of<package>();
  EXPECT_EQ(one.how_heavy.grams, 450);
  EXPECT_EQ(one.name.text, "flour");
}

TEST(AFailureOfYourOwn, HandedBackAsTheKindItIs) {
  const auto got = scan::scan<"{} {}">("99999 flour"sv).try_of<package>();
  ASSERT_FALSE(got.has_value());
  EXPECT_TRUE(std::holds_alternative<too_heavy>(got.error()));
  EXPECT_EQ(std::string_view(scan::what(got.error())), "over ten kilos");
}

TEST(AFailureOfYourOwn, AndOneThatThrowsGoesPastTheReading) {
  // Nothing here catches. A scanner that throws is throwing at whoever called,
  // and trying rather than asking does not change that -- what it changes is
  // what a scanner that hands its failure back comes out as.
  EXPECT_THROW(
      static_cast<void>(scan::scan<"{} {}">("450 no"sv).try_of<package>()),
      thrown_at_you);
}

TEST(AFailureOfYourOwn, TheLibrarysOwnKindsAreStillThere) {
  const auto got = scan::scan<"{} {}">("450;flour"sv).try_of<package>();
  ASSERT_FALSE(got.has_value());
  EXPECT_TRUE(std::holds_alternative<scan::no_match>(got.error()));
}

TEST(AFailureOfYourOwn, AskedForRatherThanTriedForItThrows) {
  EXPECT_THROW(
      static_cast<void>(scan::scan<"{} {}">("99999 flour"sv).of<package>()),
      too_heavy);
}

}  // namespace
