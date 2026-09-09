// A group that is taken over and over, told turn by turn, and a list made of
// those turns.
//
// The end of one turn and the start of the next arrive together: a tag is
// written when the walk takes the step after the character that wrote it, so
// the walk learns a turn ended only from the character that began the next. A
// fold that announced the opening first lost the closing of the turn before it
// and every character after the first, which is what this holds to.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

// Nothing but a count of what it was told.
struct turns {
  int opened = 0;
  int closed = 0;
  int letters = 0;
};

// A number written as weighed heaps: a heap of underscores says which decimal
// place it is, and the marks after it say how many -- X one, Y two. So 12 is
// `__X_XX`, and `__X_Y` as well.
struct tally {
  unsigned long value = 0;
};

// One heap on its own, read by the groups of its own pattern -- so a list of
// them is a place taken over and over, gathering as it goes.
struct heap {
  unsigned place = 0;
  unsigned marks = 0;
};

// And a list of those that never allocates: room for as many heaps as a number
// can have, and a count of how many were pushed. A list is whatever can be
// grown, which is what makes this one a list.
struct heaps {
  std::array<heap, 20> items{};
  std::size_t count = 0;

  constexpr void push_back(heap one) {
    if (count < items.size()) items[count++] = one;
  }
  [[nodiscard]] constexpr auto begin() const { return items.begin(); }
  [[nodiscard]] constexpr auto end() const { return items.begin() + count; }
};

}  // namespace

template <>
struct scan::scanner<turns> {
  static constexpr std::string_view pattern() { return R"(\((X)*\))"; }

  struct state_type {
    turns made;
  };

  static constexpr state_type begin_groups() { return {}; }
  static constexpr void opened_group(state_type& state, std::size_t) {
    ++state.made.opened;
  }
  static constexpr void push_group(state_type& state, std::size_t, char) {
    ++state.made.letters;
  }
  static constexpr void closed_group(state_type& state, std::size_t) {
    ++state.made.closed;
  }
  static constexpr turns finish_groups(state_type state) {
    return state.made;
  }
};

template <>
struct scan::scanner<tally> {
  static constexpr std::string_view pattern() { return R"(\(((_+)(X|Y)*)*\))"; }

  struct state_type {
    unsigned long total = 0;
    unsigned place = 0;
    unsigned marks = 0;
  };

  static constexpr state_type begin_groups() { return {}; }

  // The turn is the whole of one heap and its marks; the two groups inside it
  // are counted as their characters arrive, because what a group holds after
  // the match is only its last turn.
  static constexpr void opened_group(state_type& state, std::size_t which) {
    if (which != 0) return;
    state.place = 0;
    state.marks = 0;
  }

  static constexpr void push_group(state_type& state, std::size_t which,
                                   char letter) {
    if (which == 1) {
      ++state.place;
    } else if (which == 2) {
      state.marks += letter == 'Y' ? 2u : 1u;
    }
  }

  static constexpr void closed_group(state_type& state, std::size_t which) {
    if (which != 0) return;
    unsigned long weight = 1;
    for (unsigned step = 1; step < state.place; ++step) weight *= 10;
    state.total += weight * state.marks;
  }

  static constexpr tally finish_groups(state_type state) {
    return {state.total};
  }
};

template <>
struct scan::scanner<heap> {
  static constexpr std::string_view pattern() { return R"((_+)(X|Y)*)"; }

  struct state_type {
    heap made;
  };

  static constexpr state_type begin_groups() { return {}; }
  static constexpr void opened_group(state_type& state, std::size_t which) {
    if (which == 0) state.made = {};
  }
  static constexpr void push_group(state_type& state, std::size_t which,
                                   char letter) {
    if (which == 0) {
      ++state.made.place;
    } else {
      state.made.marks += letter == 'Y' ? 2u : 1u;
    }
  }
  static constexpr heap finish_groups(state_type state) { return state.made; }
};

namespace {

struct counted {
  turns it;
};

struct written {
  tally number;
  scan::held<8> name;
};

struct heaped {
  heaps parts;
  scan::held<8> name;
};

[[nodiscard]] unsigned long value_of(const heaps& parts) {
  unsigned long total = 0;
  for (const heap& one : parts) {
    unsigned long weight = 1;
    for (unsigned step = 1; step < one.place; ++step) weight *= 10;
    total += weight * one.marks;
  }
  return total;
}

TEST(AGroupTakenOverAndOver, EveryTurnIsOpenedAndClosed) {
  for (int many = 1; many <= 6; ++many) {
    const std::string text = "(" + std::string(static_cast<std::size_t>(many), 'X') + ")";
    const counted got = scan::scan<"{}">(std::string_view(text));
    EXPECT_EQ(got.it.opened, many);
    EXPECT_EQ(got.it.closed, many);
    EXPECT_EQ(got.it.letters, many);
  }
}

TEST(AGroupTakenOverAndOver, NoTurnAtAll) {
  const counted got = scan::scan<"{}">(std::string_view("()"));
  EXPECT_EQ(got.it.opened, 0);
  EXPECT_EQ(got.it.closed, 0);
  EXPECT_EQ(got.it.letters, 0);
}

TEST(AGroupTakenOverAndOver, HeapsAreWeighedAsTheyClose) {
  const auto value = [](std::string_view text) {
    const written got = scan::scan<"value={}{[a-z]*}">(text);
    return got.number.value;
  };
  EXPECT_EQ(value("value=(_X)a"), 1u);
  EXPECT_EQ(value("value=(_Y)a"), 2u);
  EXPECT_EQ(value("value=(__X)a"), 10u);
  EXPECT_EQ(value("value=(__X_X)a"), 11u);
  EXPECT_EQ(value("value=(__X_XX)a"), 12u);
  EXPECT_EQ(value("value=(__X_Y)a"), 12u);
  EXPECT_EQ(value("value=(___XX__Y_XXX)a"), 223u);
  EXPECT_EQ(value("value=(_XXXXXXXXX)a"), 9u);
}

TEST(AGroupTakenOverAndOver, EveryTurnIsAnElementOfTheList) {
  const auto heaps_of = [](std::string_view text) {
    const heaped got = scan::scan<R"(value=\({}*\){[a-z]*})">(text);
    return got.parts;
  };
  {
    const heaps one = heaps_of("value=(_X)a");
    EXPECT_EQ(one.count, 1u);
    EXPECT_EQ(value_of(one), 1u);
  }
  {
    const heaps three = heaps_of("value=(_X_X_X)a");
    EXPECT_EQ(three.count, 3u);
    EXPECT_EQ(value_of(three), 3u);
  }
  {
    const heaps mixed = heaps_of("value=(___XX__Y_XXX)a");
    EXPECT_EQ(mixed.count, 3u);
    EXPECT_EQ(value_of(mixed), 223u);
  }
  {
    const heaps rising = heaps_of("value=(_XX__XXX___X)a");
    EXPECT_EQ(rising.count, 3u);
    EXPECT_EQ(value_of(rising), 132u);
  }
}

}  // namespace
