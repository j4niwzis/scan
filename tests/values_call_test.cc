// A type that says how it is read and also how it is made: the places of its
// format stand for the arguments of the call, so it need not be an aggregate.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

class angle {
 public:
  static constexpr angle from(int degrees, int minutes) {
    return angle{degrees * 60 + minutes};
  }
  [[nodiscard]] constexpr int total() const { return minutes_; }

 private:
  constexpr explicit angle(int minutes) : minutes_(minutes) {}
  int minutes_;
};

}  // namespace

template <> struct scan::scanner<angle> : scan::aggregate_scanner<"{}d{}m"> {
  static constexpr angle parse(int degrees, int minutes) {
    return angle::from(degrees, minutes);
  }
};

namespace {

struct one_angle { angle value; };
struct bearing { angle from; angle to; };

TEST(ValuesCallTest, MadeByTheCallItNamed) {
  const std::string text = "12d30m";
  const one_angle value = scan::scan<"{}">(text);
  EXPECT_EQ(value.value.total(), 12 * 60 + 30);
}

TEST(ValuesCallTest, TwoOfThemInAStructure) {
  const std::string text = "12d30m -> 40d15m";
  const bearing value = scan::scan<"{} -> {}">(text);
  EXPECT_EQ(value.from.total(), 750);
  EXPECT_EQ(value.to.total(), 2415);
}

}  // namespace
