// A true input range: characters are taken one at a time and never buffered,
// so what stops the match is reported rather than pushed back.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct pair { int left; int right; };
struct one { int value; };
struct two_words { std::string left; std::string right; };


TEST(StreamTest, RecordAfterRecordOffOneStream) {
  std::istringstream source("1,2;3,4;5,6;");
  int sum = 0;
  int records = 0;
  while (source.peek() != std::char_traits<char>::eof()) {
    auto got = scan::scan_prefix<"{},{}">(std::views::istream<char>(source))
                   .take<pair>();
    sum += got.value.left + got.value.right;
    ++records;
    if (got.stopped != ';') break;
  }
  EXPECT_EQ(sum, 21);
  EXPECT_EQ(records, 3);
}

}  // namespace
