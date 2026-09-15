// A place that is only a piece of the subject, standing beside one that folds.
//
// Such a place gathers nothing as the walk goes. Its scanner says how to read a
// piece handed to it whole and says nothing about being told one character at a
// time, so the marks say where its piece stood and the subject is still there
// to be pointed at: it is read where the value is put together, not as the walk
// passes.
//
// Beside a type that folds its own groups the shape is read by the machine that
// gathers, and that machine runs the commands that end a match. Those copy
// every tag out of whatever register the reading was holding it in and into the
// register numbered for the tag -- which is why a group still open where the
// match ends has a closing at all, written at the position the match ended. Read
// through the reading instead, which is where a tag stands only while the walk
// is still going, such a group is read from a register nothing filled and comes
// back empty. That, and the piece agreeing with the room it would otherwise have
// needed, is what this holds to.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

// Told its own groups as the walk passes them, so that the shapes below are
// read by the machine that gathers rather than out of the marks alone.
struct tally {
  unsigned long value = 0;
};

}  // namespace

template <>
struct scan::scanner<tally> {
  [[nodiscard]] static constexpr std::string_view pattern() {
    return R"(\(((_+)((A)|(B)))*\))";
  }

  struct state_type {
    unsigned long total = 0;
    unsigned place = 0;
    unsigned digit = 0;
  };

  [[nodiscard]] static constexpr state_type begin_groups() { return {}; }

  static constexpr void opened_group(state_type& one, scan::group_at<0>) {
    one.place = 0;
    one.digit = 0;
  }

  static constexpr void closed_group(state_type& one, scan::group_at<0>) {
    unsigned long weight = 1;
    for (unsigned step = 1; step < one.place; ++step) weight *= 10;
    one.total += weight * one.digit;
  }

  static constexpr void push_group(state_type& one, scan::group_at<1>, char) {
    ++one.place;
  }

  static constexpr void push_group(state_type& one, scan::group_at<3>, char) {
    one.digit = 1;
  }

  static constexpr void push_group(state_type& one, scan::group_at<4>, char) {
    one.digit = 2;
  }

  static constexpr void push_group(state_type&, std::size_t, char) {}

  [[nodiscard]] static constexpr tally finish_groups(state_type one) {
    return {one.total};
  }
};

namespace {

struct pointed_at {
  tally number;
  std::string_view tail;
};

struct room_for_it {
  tally number;
  scan::held<8> tail;
};

TEST(APieceBesideAFold, ThePieceRunsToTheEndOfTheInput) {
  const auto got = scan::scan<"value={}{[a-z]*}">("value=(_A__B)abcdefgh"sv)
                       .of<pointed_at>();
  EXPECT_EQ(got.number.value, 21u);
  EXPECT_EQ(got.tail, "abcdefgh"sv);
}

TEST(APieceBesideAFold, ThePieceClosesInsideTheInput) {
  const auto got = scan::scan<"value={}{[a-z]*}!">("value=(_A__B)abcdefgh!"sv)
                       .of<pointed_at>();
  EXPECT_EQ(got.number.value, 21u);
  EXPECT_EQ(got.tail, "abcdefgh"sv);
}

TEST(APieceBesideAFold, ThePieceTookNoPart) {
  const auto got =
      scan::scan<"value={}{[a-z]*}">("value=(_A__B)"sv).of<pointed_at>();
  EXPECT_EQ(got.number.value, 21u);
  EXPECT_EQ(got.tail.size(), 0u);
}

TEST(APieceBesideAFold, ThePieceAndTheRoomItWouldHaveNeededAgree) {
  const auto pointed = scan::scan<"value={}{[a-z]*}">("value=(_A__B)abcdefgh"sv)
                           .of<pointed_at>();
  const auto held = scan::scan<"value={}{[a-z]*}">("value=(_A__B)abcdefgh"sv)
                        .of<room_for_it>();
  EXPECT_EQ(pointed.number.value, held.number.value);
  EXPECT_EQ(pointed.tail, held.tail.view());
}

}  // namespace
