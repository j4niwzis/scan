// A context said at a place that is one of several.
//
// Exactly one branch runs, and which one is not known where the call is
// written -- so both of these have to be sayable: a context for each branch, in
// the order the branches stand, and one context for the place, which reaches
// whichever branch runs. Neither names std::variant: what opens up into
// branches is whatever scan::branches was written for.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

constexpr int number(std::string_view text) {
  int made = 0;
  for (char letter : text) made = made * 10 + (letter - '0');
  return made;
}

struct room {
  int mark = 0;
};

struct tagged {
  std::string text;
  int mark = 0;
};

struct counted {
  int value = 0;
};

// One of several of our own, and nothing of the standard's in it.
template <class... parts>
struct either {
  std::size_t which = 0;
  std::tuple<parts...> all{};
};

struct picked {
  either<tagged, counted> pick;
  tagged name;
};

}  // namespace

template <class... parts>
struct scan::branches<either<parts...>> {
  static constexpr std::size_t count = sizeof...(parts);
  template <std::size_t which>
  using at = std::tuple_element_t<which, std::tuple<parts...>>;
  template <std::size_t which, class value>
  [[nodiscard]] static constexpr either<parts...> make(value&& one) {
    either<parts...> made;
    made.which = which;
    std::get<which>(made.all) = std::forward<value>(one);
    return made;
  }
};

template <>
struct scan::scanner<tagged> {
  static constexpr std::string_view pattern() { return "[a-z]+"; }
  static tagged parse(std::string_view text) {
    return tagged{std::string(text), 0};
  }
  static tagged parse(std::string_view text, const room& where) {
    return tagged{std::string(text), where.mark};
  }
};

template <>
struct scan::scanner<counted> {
  static constexpr std::string_view pattern() { return "[0-9]+"; }
  static constexpr counted parse(std::string_view text) {
    return counted{number(text)};
  }
  static constexpr counted parse(std::string_view text, const room& where) {
    return counted{number(text) + where.mark};
  }
};

namespace {

class a_context_for_each_branch : public ::testing::Test {
 protected:
  room fast{1};
  room slow{2};
};

TEST_F(a_context_for_each_branch, TheBranchThatRunsIsToldItsOwn) {
  const auto got = scan::scan<"{} {}">("abc def"sv).of<picked>(
      {{fast, slow}, scan::default_context});
  EXPECT_EQ(std::get<0>(got.pick.all).mark, 1);
  EXPECT_EQ(got.name.mark, 0);
}

TEST_F(a_context_for_each_branch, AndSoIsTheOtherOne) {
  const auto got = scan::scan<"{} {}">("42 def"sv).of<picked>(
      {{fast, slow}, scan::default_context});
  EXPECT_EQ(std::get<1>(got.pick.all).value, 42 + 2);
}

TEST_F(a_context_for_each_branch, OneForThePlaceReachesWhicheverRuns) {
  const auto got =
      scan::scan<"{} {}">("42 def"sv).of<picked>({fast, scan::default_context});
  EXPECT_EQ(std::get<1>(got.pick.all).value, 42 + 1);
}

}  // namespace
