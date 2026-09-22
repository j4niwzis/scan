// A fold told its context in braces.
//
// A fold keeps a state for the whole walk, and a place said in braces forgets
// the type of what it was told at the door -- which is why this used to be a
// static assert telling the caller to drop the braces. The state does have
// somewhere to live: the carrier holds it behind the interface that knows the
// field, and the carrier is made in the caller's own full expression, so it
// stands for as long as the walk that tells it.
//
// What this says is that the two forms read the same thing, and that what
// reaches the scanner is the caller's own type either way.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

// The caller's own thing. Nothing in the scanner below names it except to say
// which types it will answer for.
struct arena {
  int mark = 0;
  std::vector<std::string>* said = nullptr;
  void say(std::string one) const {
    if (said != nullptr) said->push_back(std::move(one));
  }
};

struct numbers {
  std::vector<int> values;
  int mark = 0;
};

// Two of them, so the braced form has a part to say for each.
struct both_ways {
  numbers left;
  numbers right;
};

}  // namespace

template <>
struct scan::scanner<numbers> {
  static constexpr std::string_view pattern() {
    return "([0-9]+)(?:,([0-9]+))*";
  }

  // The state is a template on what it was told, so nothing of the caller's is
  // named here and the same scanner answers for a caller that says nothing.
  template <class Told>
  struct state {
    std::vector<int> values;
    int running = 0;
    int mark = 0;
  };

  static state<scan::default_context_t> begin_groups() { return {}; }

  template <class Told>
    requires std::same_as<Told, arena>
  static state<Told> begin_groups(const Told& told) {
    told.say("a fold begins");
    return {{}, 0, told.mark};
  }

  template <class Told, std::size_t Which>
  static void push_group(state<Told>& one, scan::group_at<Which>, char digit) {
    one.running = one.running * 10 + (digit - '0');
  }

  template <class Told, std::size_t Which>
  static void closed_group(state<Told>& one, scan::group_at<Which>) {
    one.values.push_back(one.running + one.mark);
    one.running = 0;
  }

  template <class Told>
  static numbers finish_groups(state<Told> one) {
    return {std::move(one.values), one.mark};
  }
};

namespace {

TEST(AFoldToldInBraces, ItReadsWhatTheSameContextReadsWithoutThem) {
  std::vector<std::string> said;
  const arena here{100, &said};
  const arena there{200, &said};
  const both_ways plain =
      scan::scan<"{} {}">("1,2,3 4,5,6"sv).of<both_ways>(here, there);
  const both_ways braced =
      scan::scan<"{} {}">("1,2,3 4,5,6"sv).of<both_ways>({here, there});
  EXPECT_EQ(plain.left.values, (std::vector<int>{101, 102, 103}));
  EXPECT_EQ(plain.right.values, (std::vector<int>{204, 205, 206}));
  EXPECT_EQ(braced.left.values, plain.left.values);
  EXPECT_EQ(braced.right.values, plain.right.values);
}

// Told in braces, the scanner is still handed the caller's own type: the hook
// that only answers for `arena` is the one that ran, and it wrote where the
// caller said to write.
TEST(AFoldToldInBraces, TheCallersOwnThingIsWhatReachesTheScanner) {
  std::vector<std::string> said;
  const arena here{0, &said};
  const arena there{0, &said};
  const both_ways braced =
      scan::scan<"{} {}">("4,5 6,7"sv).of<both_ways>({here, there});
  EXPECT_EQ(braced.left.values, (std::vector<int>{4, 5}));
  EXPECT_EQ(braced.right.values, (std::vector<int>{6, 7}));
  EXPECT_FALSE(said.empty());
}

TEST(AFoldToldInBraces, ToldNothingItIsReadAsItAlwaysWas) {
  const both_ways got = scan::scan<"{} {}">("7,8 9"sv).of<both_ways>();
  EXPECT_EQ(got.left.values, (std::vector<int>{7, 8}));
  EXPECT_EQ(got.right.values, (std::vector<int>{9}));
}

}  // namespace
