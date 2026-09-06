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


TEST(ExpectedForm, TheThrowingFormStillThrows) {
  const std::string text = "12;34";
  EXPECT_THROW(
      {
        const pair value = scan::scan<"{},{}">(text).of<pair>();
        (void)value;
      },
      scan::scan_error);
}

}  // namespace
