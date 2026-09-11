// A fold whose every turn has groups of its own.
//
// The turns of a list are told apart by the moves, and the fold above shows
// that. This shows the other half: within one turn there are groups again --
// a run of one kind of character and then a choice between two others -- and
// each of them is what a hook is written against. Nothing here asks where any
// of them stood; the moves say what each character lies inside, and the hooks
// count.
//
// Worth its own file because nothing else asks for a number that every turn
// contributes to. A reading that loses a turn still matches, still fills the
// fields beside it, and still says a number -- a smaller one. Tests that ask
// whether the shape read at all cannot tell the difference.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

// A heap of stones, written as a run of marks and then the kind: `___A` is
// three of kind A. What is read out is the heaps put together, each weighed by
// how many marks it stood on, so that a turn that goes missing is a digit that
// goes missing.
struct weighed {
  unsigned long value = 0;
};

}  // namespace

template <>
struct scan::scanner<weighed> {
  static constexpr std::string_view pattern() {
    return R"(\(((_+)((A)|(B)))*\))";
  }

  struct state_type {
    unsigned long total = 0;
    unsigned marks = 0;
    unsigned kind = 0;
  };

  static constexpr state_type begin_groups() { return {}; }

  // The turn opens: a fresh heap.
  static constexpr void opened_group(state_type& state, scan::group_at<0>) {
    state.marks = 0;
    state.kind = 0;
  }

  // And closes: weighed by its marks, ten to the power of them less one, so
  // that every turn lands in a digit of its own and a missing turn is visible.
  static constexpr void closed_group(state_type& state, scan::group_at<0>) {
    unsigned long weight = 1;
    for (unsigned step = 1; step < state.marks; ++step) weight *= 10;
    state.total += weight * state.kind;
  }

  static constexpr void push_group(state_type& state, scan::group_at<1>, char) {
    ++state.marks;
  }
  static constexpr void push_group(state_type& state, scan::group_at<3>, char) {
    state.kind = 1;
  }
  static constexpr void push_group(state_type& state, scan::group_at<4>, char) {
    state.kind = 2;
  }
  static constexpr void push_group(state_type&, std::size_t, char) {}

  static constexpr weighed finish_groups(state_type state) {
    return {state.total};
  }
};

namespace {

struct labelled {
  weighed heaps;
  scan::held<8> name;
};

unsigned long value_of(std::string_view text) {
  return scan::scan<"value={}{[a-z]*}">(text).of<labelled>().heaps.value;
}

TEST(AFoldWhoseTurnHasGroups, OneTurn) {
  EXPECT_EQ(value_of("value=(_A)x"), 1u);
  EXPECT_EQ(value_of("value=(_B)x"), 2u);
}

TEST(AFoldWhoseTurnHasGroups, EveryTurnIsCounted) {
  // Four turns of one mark each: four ones, and they add.
  EXPECT_EQ(value_of("value=(_A_A_A_A)x"), 4u);
  // And of both kinds: three of A and one of B.
  EXPECT_EQ(value_of("value=(_A_A_A_B)x"), 5u);
}

TEST(AFoldWhoseTurnHasGroups, EachTurnLandsInItsOwnDigit) {
  // One mark, two marks, three marks: a one, a two and a three, each a digit
  // further up. A turn that is not counted takes its digit with it.
  EXPECT_EQ(value_of("value=(_A__A___A)x"), 111u);
  EXPECT_EQ(value_of("value=(_B__B___B)x"), 222u);
  EXPECT_EQ(value_of("value=(___A__B_A)x"), 121u);
}

TEST(AFoldWhoseTurnHasGroups, TheSameTurnOverAndOverAgain) {
  // Eight turns, alternating, the marks going round one to four: one, twenty,
  // a hundred, two thousand, and the same again. The shape the walk is written
  // for, and the one a reading that drops turns gets wrong -- it still reads,
  // it still says a number, and the number is smaller.
  EXPECT_EQ(value_of("value=(_A__B___A____B_A__B___A____B)x"), 4242u);
}

TEST(AFoldWhoseTurnHasGroups, TheFieldBesideItIsStillRead) {
  const labelled one = scan::scan<"value={}{[a-z]*}">("value=(_A__B)abc").of<labelled>();
  EXPECT_EQ(one.heaps.value, 21u);
  EXPECT_EQ(one.name.view(), "abc");
}

}  // namespace
