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

TEST(stream_test, a_whole_subject_off_a_stream) {
  std::istringstream source("4711");
  const one value = scan::scan<"{}">(std::views::istream<char>(source));
  EXPECT_EQ(value.value, 4711);
}

TEST(stream_test, two_fields_off_a_stream) {
  std::istringstream source("ab-cd");
  const two_words value =
      scan::scan<"{[a-z]+}-{[a-z]+}">(std::views::istream<char>(source));
  EXPECT_EQ(value.left, "ab");
  EXPECT_EQ(value.right, "cd");
}

TEST(stream_test, a_head_off_a_stream_and_what_stopped_it) {
  std::istringstream source("12,34rest");
  auto got =
      scan::scan_prefix<"{},{}">(std::views::istream<char>(source)).take<pair>();
  EXPECT_EQ(got.value.left, 12);
  EXPECT_EQ(got.value.right, 34);
  EXPECT_EQ(got.stopped, 'r');
}

TEST(stream_test, a_stream_that_runs_out_exactly) {
  std::istringstream source("77,88");
  auto got =
      scan::scan_prefix<"{},{}">(std::views::istream<char>(source)).take<pair>();
  EXPECT_EQ(got.value.left, 77);
  EXPECT_EQ(got.value.right, 88);
  EXPECT_FALSE(got.stopped);
}

TEST(stream_test, record_after_record_off_one_stream) {
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
