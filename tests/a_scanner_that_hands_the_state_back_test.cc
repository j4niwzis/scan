// A scanner that takes its characters by handing back a fresh state.
//
// A scanner says how it takes a character once, in one of two shapes: it hands
// back the state it made, or it changes the one it was given. The first is
// written as a function of what it was given, which is what lets the walk keep
// the state it had before the call.
//
// The two are told apart by what comes back and not by how the parameter is
// written: taking by value accepts an lvalue as happily as anything else, so a
// scanner that only changes what it is given would answer yes to "can I be
// called with a state" either way. Saying both is refused.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

struct tally {
  unsigned long value = 0;
};

// Hands back the state it made.
struct weight {
  unsigned long grams = 0;
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

template <>
struct scan::scanner<weight> {
  [[nodiscard]] static constexpr std::string_view pattern() {
    return "[0-9]+";
  }

  struct state_type {
    unsigned long so_far = 0;
  };

  [[nodiscard]] static constexpr state_type begin() { return {}; }

  [[nodiscard]] static constexpr state_type push(state_type one, char letter) {
    one.so_far = one.so_far * 10 + static_cast<unsigned long>(letter - '0');
    return one;
  }

  [[nodiscard]] static constexpr weight finish(state_type one) {
    return {one.so_far};
  }
};

namespace {

// Beside a fold, so that the shape is read by the machine that gathers and the
// scanner is told its characters one at a time rather than handed the place
// whole.
struct with_a_weight {
  tally number;
  weight how_heavy;
};

TEST(AScannerThatHandsTheStateBack, IsToldItsCharactersOneAtATime) {
  const auto got =
      scan::scan<"value={}w={}">("value=(_A__B)w=907"sv).of<with_a_weight>();
  EXPECT_EQ(got.number.value, 21u);
  EXPECT_EQ(got.how_heavy.grams, 907u);
}

TEST(AScannerThatHandsTheStateBack, ReadsTheSameOffASubjectHeldInMemory) {
  const auto got =
      scan::scan<"value={}w={}">("value=(_A)w=42"sv).of<with_a_weight>();
  EXPECT_EQ(got.number.value, 1u);
  EXPECT_EQ(got.how_heavy.grams, 42u);
}

}  // namespace
