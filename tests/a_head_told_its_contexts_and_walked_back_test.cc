// A head read once, told its contexts in braces, where the walk goes past a
// match and comes back.
//
// Four things at once, because each of them is where the others go wrong. The
// subject arrives as it is read, so nothing can be rewound and what was read
// past a match has to be given back. The pattern's last place is a fold, so
// the walk goes past a match, starts a turn it cannot finish, and has to come
// back to the state it was in at the match -- not to an empty one, and not to
// the one the failed turn left. The contexts are said in braces, so a place
// that is a shape of two parts gets one for each. And every context writes
// down what it was handed, so what reached which place is a thing the test can
// read rather than infer.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

// A subject that can only be read once, and that says how much of it was read.
class counted_reading {
 public:
  class cursor {
   public:
    using iterator_concept = std::input_iterator_tag;
    using value_type = char;
    using difference_type = std::ptrdiff_t;

    cursor() = default;
    cursor(std::string_view text, std::size_t* taken)
        : text_(text), taken_(taken) {}

    [[nodiscard]] char operator*() const { return text_[*taken_]; }
    cursor& operator++() {
      ++*taken_;
      return *this;
    }
    void operator++(int) { ++*this; }
    [[nodiscard]] bool operator==(std::default_sentinel_t) const {
      return *taken_ == text_.size();
    }

   private:
    std::string_view text_;
    std::size_t* taken_ = nullptr;
  };

  counted_reading(std::string_view text, std::size_t* taken)
      : text_(text), taken_(taken) {}

  [[nodiscard]] cursor begin() const { return cursor(text_, taken_); }
  [[nodiscard]] std::default_sentinel_t end() const { return {}; }

 private:
  std::string_view text_;
  std::size_t* taken_ = nullptr;
};

// A context of the caller's own: a mark to leave on what was read with it, and
// somewhere to write down that it was handed over at all.
struct room {
  int mark = 0;
  std::vector<std::string>* said = nullptr;

  void say(std::string what) const {
    if (said != nullptr) said->push_back(std::move(what));
  }
};

// The branches of the alternation. `foreach` is written first, so it is
// preferred: on "fore" the walk takes it as far as it goes, finds it is not
// there, and comes back to the match of `for` underneath it.
struct for_word { scan::held<8> text; };
struct each_word { scan::held<8> text; };
struct whole_word { scan::held<8> text; };

using longest_first = std::variant<whole_word, for_word, each_word>;
struct choice { longest_first value; };

// A leaf told a context.
struct tagged {
  std::string text;
  int mark = 0;
};

// A fold: its state is built turn by turn as the walk passes, which is the
// state that going back to a match has to restore.
struct numbers {
  std::vector<int> values;
  int mark = 0;
};

// The place that is a shape of two parts, so that braces have two to say.
struct head_of {
  choice which;
  tagged name;
};

struct row {
  head_of head;
  numbers tail;
};

}  // namespace

// Told its characters as they are read, because a subject that arrives as it
// is read has nothing in a row to hand over when the match is over.
template <>
struct scan::scanner<tagged> {
  struct state {
    std::string text;
    int mark = 0;
    const room* where = nullptr;
  };

  static constexpr std::string_view pattern() { return "([a-z]+)"; }

  static state begin_groups() { return state{}; }
  static state begin_groups(const room& where) {
    where.say("name begins");
    return state{{}, where.mark, &where};
  }

  static void opened_group(state& made, scan::group_at<0>) {
    made.text.clear();
  }
  static void push_group(state& made, scan::group_at<0>, char value) {
    made.text.push_back(value);
  }
  static void closed_group(state& made, scan::group_at<0>) {
    if (made.where != nullptr) made.where->say("name=" + made.text);
  }

  static tagged finish_groups(state made) {
    return tagged{std::move(made.text), made.mark};
  }
};

template <>
struct scan::scanner<numbers> {
  struct state {
    std::vector<int> values;
    int running = 0;
    int mark = 0;
    const room* where = nullptr;
  };

  // The first number, and every one after a comma. The second group is inside
  // a repetition, so it opens and closes once a turn -- and a turn that opens
  // on a comma the walk cannot get past is a turn that has to be undone.
  static constexpr std::string_view pattern() {
    return "([0-9]+)(?:,([0-9]+))*";
  }

  static state begin_groups() { return state{}; }
  static state begin_groups(const room& where) {
    where.say("fold begins");
    return state{{}, 0, where.mark, &where};
  }

  static void opened_group(state& made, scan::group_at<0>) { made.running = 0; }
  static void opened_group(state& made, scan::group_at<1>) {
    made.running = 0;
    if (made.where != nullptr) made.where->say("turn opens");
  }
  static void push_group(state& made, scan::group_at<0>, char value) {
    made.running = made.running * 10 + (value - '0');
  }
  static void push_group(state& made, scan::group_at<1>, char value) {
    made.running = made.running * 10 + (value - '0');
  }
  static void closed_group(state& made, scan::group_at<0>) {
    made.values.push_back(made.running);
  }
  static void closed_group(state& made, scan::group_at<1>) {
    made.values.push_back(made.running);
    if (made.where != nullptr) {
      made.where->say("turn closes " + std::to_string(made.running));
    }
  }

  // What is worth keeping of this gathering where the walk stands in a match.
  // Said so that the test can see that it was asked at all.
  // And going back to it, where the walk went past the match and died. Said
  // so that the test can see that the scanner was asked rather than walked
  // over.
  static void groups_go_back_to(state& live, const state& kept) {
    if (kept.where != nullptr) kept.where->say("went back");
    live = kept;
  }

  static state keep_groups(const state& made) {
    if (made.where != nullptr) made.where->say("kept");
    return made;
  }

  static numbers finish_groups(state made) {
    return numbers{std::move(made.values), made.mark};
  }
};

namespace {

class a_head_told_its_contexts : public ::testing::Test {
 protected:
  std::vector<std::string> said;
  room slow{1, &said};
  room fast{2, &said};
};

// The whole of it: a head off a reading that cannot be rewound, an alternation
// that prefers the longer branch and does not get it, a fold whose last turn
// opens on a comma and dies, and a context for each part of the first place.
TEST_F(a_head_told_its_contexts, AHeadToldInBracesThatWalkedBackOffAStream) {
  std::size_t taken = 0;
  counted_reading source("for abc 1,2,3,x rest", &taken);
  auto got = scan::scan_prefix<"{{foreach}|{for}|{each}} {} {}">(std::move(source))
                 .try_of<row>({{slow, fast}, fast});
  ASSERT_TRUE(got.has_value()) << "the head did not match";

  // The branch underneath the one the order prefers: `foreach` is first and is
  // not there, so the walk went past the match of `for` and came back to it.
  EXPECT_EQ(got->head.which.value.index(), 1u);
  EXPECT_EQ(std::get<for_word>(got->head.which.value).text.view(), "for"sv);

  // The braces said which context each part of the first place gets.
  EXPECT_EQ(got->head.name.text, "abc");
  EXPECT_EQ(got->head.name.mark, 2);
  EXPECT_EQ(got->tail.mark, 2);

  // And the state the fold was in at the match, not the one the turn that died
  // left behind: the walk opened a turn on the comma before `x`, could not
  // finish it, and came back.
  EXPECT_EQ(got->tail.values, std::vector<int>({1, 2, 3}));

  // The contexts were handed over, and said so.
  EXPECT_NE(std::ranges::find(said, "fold begins"), said.end());
  EXPECT_NE(std::ranges::find(said, "name begins"), said.end());

  // And what a hook sees, which is not what a gathering sees.
  //
  // The value is the one the match left; the calls are not. The walk stands in
  // more than one reading of the subject at once and begins a gathering for
  // each of them, so `begin_groups` runs many times over for one record. And
  // the turn the match ends on is closed a second time when the answer is made
  // from the place that was kept.
  //
  // So a hook that writes into its own state and nowhere else cannot tell the
  // difference -- the values below are the ones the match left. A hook that
  // writes anywhere else is called more than once for the same turn, and this
  // is where that is written down, so that changing it is a thing somebody
  // sees rather than a thing somebody's program finds out.
  EXPECT_GE(std::ranges::count(said, "fold begins"), 1);
  // And the scanner was asked what is worth keeping, rather than having its
  // state copied behind its back.
  EXPECT_GE(std::ranges::count(said, "kept"), 1);
  EXPECT_GE(std::ranges::count(said, "went back"), 1);
  EXPECT_EQ(std::ranges::count(said, "turn closes 3"), 2);
}

// The same head, read for what stopped it as well as for what it is: the
// character the walk would not take is the caller's to see, and the reading
// stands on it.
TEST_F(a_head_told_its_contexts, WhatStoppedTheHeadIsSaidWithIt) {
  std::size_t taken = 0;
  counted_reading source("for abc 1,2,3,x rest", &taken);
  auto got = scan::scan_prefix<"{{foreach}|{for}|{each}} {} {}">(std::move(source))
                 .take<row>(fast);
  EXPECT_EQ(got.value.tail.values, std::vector<int>({1, 2, 3}));
  EXPECT_EQ(got.value.head.name.mark, 2);
}

}  // namespace
