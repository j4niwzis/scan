// A fold kept in one place rather than one for every reading the walk stands
// in.
//
// The default is a state with every reading, which makes a turn cost a write
// and nothing else. Where the state holds something that allocates, that is a
// great many made and thrown away, so a scanner may say `gathers_in_one_place`
// and have its turns held back until the machine stands in one reading -- the
// same hooks, in the same order, told later.
//
// What this says is that the two answer the same.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

// A context of the caller's, so that the places are told in braces and the
// fold is reached through the interface that carries one.
struct arena {
  std::pmr::memory_resource* where = nullptr;
  std::pmr::memory_resource* resource() const { return where; }
};

struct as_they_go {
  std::vector<int> values;
};

struct in_one_place {
  std::vector<int> values;
};

struct both_ways {
  as_they_go left;
  in_one_place right;
};

// A context of the caller's, so that the places are told in braces: that is
// the road where a fold is kept behind an interface, and where holding the
// turns back has something to hold them in.
struct room {
  int mark = 0;
};

}  // namespace

template <>
struct scan::scanner<as_they_go> {
  static constexpr std::string_view pattern() {
    return "([0-9]+)(?:,([0-9]+))*";
  }
  struct state {
    std::vector<int> values;
    int running = 0;
  };
  static state begin_groups() { return {}; }
  static void push_group(state& one, std::size_t, char digit) {
    one.running = one.running * 10 + (digit - '0');
  }
  static void closed_group(state& one, std::size_t) {
    one.values.push_back(one.running);
    one.running = 0;
  }
  static as_they_go finish_groups(state one) { return {std::move(one.values)}; }
};

template <>
struct scan::scanner<in_one_place> {
  // The only difference between the two.
  static constexpr bool gathers_in_one_place = true;

  static constexpr std::string_view pattern() {
    return "([0-9]+)(?:,([0-9]+))*";
  }
  struct state {
    std::vector<int> values;
    int running = 0;
  };
  static state begin_groups() { return {}; }
  static void push_group(state& one, std::size_t, char digit) {
    one.running = one.running * 10 + (digit - '0');
  }
  static void closed_group(state& one, std::size_t) {
    one.values.push_back(one.running);
    one.running = 0;
  }
  static in_one_place finish_groups(state one) { return {std::move(one.values)}; }
};

namespace {

TEST(AFoldKeptInOnePlace, ItReadsWhatTheOtherReads) {
  const room here{1};
  const room there{2};
  const both_ways got =
      scan::scan<"{} {}">("1,2,3 4,5,6"sv).of<both_ways>({here, there});
  EXPECT_EQ(got.left.values, (std::vector<int>{1, 2, 3}));
  EXPECT_EQ(got.right.values, (std::vector<int>{4, 5, 6}));
}

TEST(AFoldKeptInOnePlace, OneTurnIsReadTheSame) {
  const room here{1};
  const room there{2};
  const both_ways got =
      scan::scan<"{} {}">("7 8"sv).of<both_ways>({here, there});
  EXPECT_EQ(got.left.values, (std::vector<int>{7}));
  EXPECT_EQ(got.right.values, (std::vector<int>{8}));
}

TEST(AFoldKeptInOnePlace, ToldInBracesItReadsTheSame) {
  std::pmr::monotonic_buffer_resource bytes;
  const both_ways got = scan::scan<"{} {}">("1,2,3 4,5,6"sv)
                            .of<both_ways>({arena{&bytes}, arena{&bytes}});
  EXPECT_EQ(got.left.values, (std::vector<int>{1, 2, 3}));
  EXPECT_EQ(got.right.values, (std::vector<int>{4, 5, 6}));
}

TEST(AFoldKeptInOnePlace, AndALongerOne) {
  const room here{1};
  const room there{2};
  const both_ways got = scan::scan<"{} {}">("10,20,30,40 50,60,70,80"sv)
                            .of<both_ways>({here, there});
  EXPECT_EQ(got.left.values, (std::vector<int>{10, 20, 30, 40}));
  EXPECT_EQ(got.right.values, (std::vector<int>{50, 60, 70, 80}));
}

}  // namespace
