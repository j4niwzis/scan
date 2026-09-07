// A scan that is asked rather than assigned, and a scan that hands back what
// went wrong instead of throwing it.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct pair {
  int left;
  int right;
};


TEST(ExpectedForm, AScanThatDidNot) {
  const std::string text = "12;34";
  const auto value = scan::scan<"{},{}">(text).try_of<pair>();
  EXPECT_FALSE(value.has_value());
  EXPECT_NE(std::string_view(scan::what(value.error())).size(), 0u);
  // Handed back, a failure still says which kind it was: the subject is not
  // what the pattern says, which is a different thing from a field that will
  // not read.
  EXPECT_TRUE(std::holds_alternative<scan::no_match>(value.error()));
}

TEST(ExpectedForm, AFieldThatWillNotConvert) {
  // A place written `{}` for an `int` matches digits and nothing else, so a
  // field of letters is not a field that will not convert -- it is a subject
  // the pattern does not describe, and that is what comes back.
  const std::string text = "12,abc";
  const auto value = scan::scan<"{},{}">(text).try_of<pair>();
  EXPECT_FALSE(value.has_value());
  EXPECT_TRUE(std::holds_alternative<scan::no_match>(value.error()));
}

TEST(ExpectedForm, AFieldThatMatchedAndStillWillNotConvert) {
  // Written out, the place takes what the type will not: eight digits are a
  // fine `[0-9]+` and not a fine `int`… which is the field below. Here the
  // reading is of something that matched and then did not read.
  struct wide {
    int left;
    std::uint8_t right;
  };
  const std::string text = "12,300";
  const auto value = scan::scan<"{},{[0-9]+}">(text).try_of<wide>();
  EXPECT_FALSE(value.has_value());
  EXPECT_TRUE(std::holds_alternative<scan::out_of_range>(value.error()));
}

TEST(ExpectedForm, AFieldThatDoesNotFit) {
  // The same field, read the same way, and a different question: this one is
  // a number and it is too big for the type it was asked to be.
  const std::string text = "12,99999999999999999999";
  const auto value = scan::scan<"{},{}">(text).try_of<pair>();
  EXPECT_FALSE(value.has_value());
  EXPECT_TRUE(std::holds_alternative<scan::out_of_range>(value.error()));
}

}  // namespace
