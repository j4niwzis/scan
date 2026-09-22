// A scanner with nothing of the caller's named in it: the state is a template
// on what it was told, and so is every hook.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

// Two contexts of the caller's, with nothing in common but what they answer.
struct room {
  int mark = 0;
  std::vector<std::string>* said = nullptr;
  void say(std::string one) const { said->push_back(std::move(one)); }
};

struct depot {
  int mark = 0;
  std::vector<std::string>* said = nullptr;
  void say(std::string one) const { said->push_back("depot: " + std::move(one)); }
};

struct tagged {
  std::string text;
  int mark = 0;
};

struct pair {
  tagged left;
  tagged right;
};

}  // namespace

template <>
struct scan::scanner<tagged> {
  static constexpr std::string_view pattern() { return "([a-z]+)"; }

  // Of a piece with whatever it was told, and holding it for the reading.
  // Of a piece with what it was told, and keeping what it copied out of it.
  // Not its address: what a hook is handed lives as long as the call, and a
  // state outlives every call that touches it.
  template <class Told>
  struct state {
    std::string text;
    int mark = 0;
    const Told* where = nullptr;
  };

  static state<scan::default_context_t> begin_groups() { return {}; }

  template <class Told>
    requires requires(const Told& one) {
      one.say(std::string{});
      { one.mark } -> std::convertible_to<int>;
    }
  static state<Told> begin_groups(const Told& told) {
    told.say("a name begins");
    return {{}, told.mark, &told};
  }

  template <class Told>
  static void opened_group(state<Told>& one, scan::group_at<0>) {
    one.text.clear();
  }

  template <class Told>
  static void push_group(state<Told>& one, scan::group_at<0>, char letter) {
    one.text.push_back(letter);
  }

  template <class Told>
  static void closed_group(state<Told>& one, scan::group_at<0>) {
    if constexpr (requires(const Told& told) { told.say(std::string{}); }) {
      if (one.where != nullptr) one.where->say("a name: " + one.text);
    }
  }

  template <class Told>
  static tagged finish_groups(state<Told> one) {
    return {std::move(one.text), one.mark};
  }
};

namespace {

class a_templated_state : public ::testing::Test {
 protected:
  std::vector<std::string> said;
  room here{7, &said};
  depot there{3, &said};
};

TEST_F(a_templated_state, ToldWithoutBraces) {
  const pair got = scan::scan<"{} {}">("abc def"sv).of<pair>(here, there);
  EXPECT_EQ(got.left.text, "abc");
  EXPECT_EQ(got.left.mark, 7);
  EXPECT_EQ(got.right.text, "def");
  EXPECT_EQ(got.right.mark, 3);
  EXPECT_NE(std::ranges::find(said, "a name: abc"), said.end());
  EXPECT_NE(std::ranges::find(said, "depot: a name: def"), said.end());
}

TEST_F(a_templated_state, AndInBraces) {
  const pair got = scan::scan<"{} {}">("abc def"sv).of<pair>({here, there});
  EXPECT_EQ(got.left.mark, 7);
  EXPECT_EQ(got.right.mark, 3);
  EXPECT_NE(std::ranges::find(said, "depot: a name: def"), said.end());
}

TEST_F(a_templated_state, ToldNothingItReadsTheWayItAlwaysDid) {
  const pair got = scan::scan<"{} {}">("abc def"sv).of<pair>();
  EXPECT_EQ(got.left.text, "abc");
  EXPECT_EQ(got.left.mark, 0);
  EXPECT_TRUE(said.empty());
}

}  // namespace
