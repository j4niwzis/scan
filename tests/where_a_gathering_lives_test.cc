// What the reading decides before it reads anything.
//
// Everything else in this suite asks a question of a subject and looks at the
// answer. This asks the machinery underneath: which slot a place gathers in,
// which place a context said at the door ends up at, and how many turns a place
// may take. Every one of these is settled while the program is compiled, so
// every one of them is a `static_assert` -- and every fault this file would
// have caught was found the long way instead, by reading a value that came back
// wrong and working backwards.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

struct room {
  int mark = 0;
};

struct counted {
  int value = 0;
};

struct dashed {
  int letters = 0;
};

// A record of two places gathered the same way.
struct twice {
  counted first;
  counted second;
};

// A place whose type says a format of its own, and a record of two of them.
struct both {
  counted number;
  dashed word;
};

struct deep {
  both left;
  both right;
};

// A type that folds its own groups: no places inside for anybody to say a
// context at, and its groups are its own business.
struct numbers {
  std::vector<int> values;
  int mark = 0;
};

struct folded {
  numbers list;
};

struct listed {
  std::vector<counted> values;
};

}  // namespace

template <>
struct scan::scanner<counted> {
  static constexpr std::string_view pattern() { return "[0-9]+"; }
  struct state {
    int running = 0;
  };
  static constexpr state begin(std::string_view) { return {}; }
  static constexpr void push(state& made, char value) {
    made.running = made.running * 10 + (value - '0');
  }
  static constexpr counted finish(state made) { return counted{made.running}; }
  static constexpr counted parse(std::string_view text) {
    int made = 0;
    for (char letter : text) made = made * 10 + (letter - '0');
    return counted{made};
  }
};

template <>
struct scan::scanner<dashed> {
  static constexpr std::string_view pattern() { return "[a-z]+"; }
  struct state {
    int letters = 0;
  };
  static constexpr state begin(std::string_view) { return {}; }
  static constexpr void push(state& made, char) { ++made.letters; }
  static constexpr dashed finish(state made) { return dashed{made.letters}; }
  static constexpr dashed parse(std::string_view text) {
    return dashed{static_cast<int>(text.size())};
  }
};

template <>
struct scan::scanner<both> : scan::aggregate_scanner<"{}:{}"> {};

template <>
struct scan::scanner<numbers> {
  struct state {
    std::vector<int> values;
    int running = 0;
    int mark = 0;
  };
  static constexpr std::string_view pattern() {
    return "([0-9]+)(?:,([0-9]+))*";
  }
  static constexpr state begin_groups() { return state{}; }
  static constexpr state begin_groups(const room& where) {
    return state{{}, 0, where.mark};
  }
  static constexpr void opened_group(state& made, std::size_t) {
    made.running = 0;
  }
  static constexpr void push_group(state& made, std::size_t, char value) {
    made.running = made.running * 10 + (value - '0');
  }
  static constexpr void closed_group(state& made, std::size_t) {
    made.values.push_back(made.running);
  }
  static constexpr numbers finish_groups(state made) {
    return numbers{std::move(made.values), made.mark};
  }
};

namespace {

namespace detail = scan::detail;

// --- where a gathering lives ------------------------------------------------

// Slots are handed out by kind and not by group: two places read the same way
// share one, because at a register they are still two and the register is what
// tells them apart.
static_assert(detail::gathering_slot<twice, "{} {}", 0> ==
              detail::gathering_slot<twice, "{} {}", 1>);

// A fold is not gathered at a register -- the walk keeps it itself -- so two
// places of one type are two values and must be two slots. This is the fault
// that made `1:ab 2:cd` come back as two halves of one thing.
static_assert(detail::gathering_slot<deep, "{} {}", 0> !=
              detail::gathering_slot<deep, "{} {}", 3>);

// And a place the walk keeps is a place alone in its slot, which is the very
// question the walk asks before it decides to keep one.
static_assert(detail::alone_in_its_slot<deep, "{} {}", 0>());
static_assert(detail::alone_in_its_slot<deep, "{} {}", 3>());
static_assert(!detail::alone_in_its_slot<twice, "{} {}", 0>());

// --- which place is told what -----------------------------------------------

// A type whose places are a format of its own opens up into them: a context can
// be said at each. A type that folds its own groups does not -- what is inside
// is its own, and it is told the context whole.
static_assert(detail::carrier_places<both>() == 2);
static_assert(detail::carrier_places<deep>() == 2);
static_assert(detail::carrier_places<numbers>() == 0);
static_assert(detail::carrier_places<counted>() == 0);

// So the carrier for a record of shapes is a shape of shapes, and the carrier
// for a fold is a leaf.
static_assert(std::same_as<
              detail::carrier_for<deep>,
              detail::context_shape<detail::carrier_for<both>,
                                    detail::carrier_for<both>>>);
static_assert(std::same_as<detail::carrier_for<numbers>,
                           detail::context_leaf<numbers>>);

// The walk down to a group stops at a place read as one value by its own
// scanner: that place is told the carrier for itself, and what reaches the
// places inside is that scanner's business. Going one level further is what
// stopped a braced context from compiling off a stream.
static_assert(
    std::same_as<decltype(detail::context_at_group<deep, 0>(
                     std::declval<const detail::carrier_for<deep>&>())),
                 detail::carrier_for<both>>);
static_assert(
    std::same_as<decltype(detail::context_at_group<both, 0>(
                     std::declval<const detail::carrier_for<both>&>())),
                 detail::context_leaf<counted>>);

// --- how many turns a place may take ----------------------------------------

// A place written with nothing after it stands once and goes on as long as the
// subject affords: one turn at least, and no number at all at the other end.
constexpr auto plain_list = detail::spread_of<listed, "{{}{*,?}}">();
static_assert(plain_list.turns_least[0] == 1);
static_assert(plain_list.turns_most[0] == detail::turns_unbounded);

constexpr auto counted_list = detail::spread_of<listed, "{{}{*,?}}{2,3}">();
static_assert(counted_list.turns_least[0] == 2);
static_assert(counted_list.turns_most[0] == 3);

constexpr auto at_most_one = detail::spread_of<listed, "{{}{*,?}}?">();
static_assert(at_most_one.turns_least[0] == 0);
static_assert(at_most_one.turns_most[0] == 1);

// --- and that the readings above all of this still agree with it ------------

TEST(WhereAGatheringLives, TheDecisionsHoldWhenSomethingIsRead) {
  const auto pair = scan::scan<"{} {}">("1:ab 2:cd"sv).of<deep>();
  EXPECT_EQ(pair.left.number.value, 1);
  EXPECT_EQ(pair.right.number.value, 2);
  EXPECT_EQ(pair.right.word.letters, 2);

  const auto two = scan::scan<"{} {}">("12 34"sv).of<twice>();
  EXPECT_EQ(two.first.value, 12);
  EXPECT_EQ(two.second.value, 34);

  const auto untold = scan::scan<"{}">("1,2,3"sv).of<folded>();

  room fast{10};
  const auto told = scan::scan<"{}">("1,2,3"sv).of<folded>(fast);
  EXPECT_EQ(told.list.mark, 10);
  ASSERT_EQ(told.list.values.size(), 3u);
  EXPECT_EQ(told.list.values[2], 3);
}

TEST(WhereAGatheringLives, TheTurnsAPlaceMayTakeAreKept) {
  const auto three = scan::scan<"{{}{*,?}}{2,3}">("1,2,3"sv).try_of<listed>();
  ASSERT_TRUE(three.has_value());
  EXPECT_EQ(three->values.size(), 3u);

  const auto too_few = scan::scan<"{{}{*,?}}{2,3}">("1"sv).try_of<listed>();
  EXPECT_FALSE(too_few.has_value());
}

}  // namespace
