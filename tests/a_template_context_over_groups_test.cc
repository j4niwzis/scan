// The group hooks, where the state is a template on what the place was told.
//
// A place told in braces carries its context behind an interface, so what
// reaches the hooks there is the carrier and not the caller's own type. A state
// that is a template takes whichever of the two it was handed, and the reading
// is the same either way -- which is what this test is for: the same scanner,
// said both ways, answering the same.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

// A context of the caller's: where this reading is to put what it keeps.
struct arena {
  std::pmr::memory_resource* where = nullptr;
  std::pmr::memory_resource* resource() const { return where; }
};

struct numbers {
  std::pmr::vector<int> values;
};

struct both {
  numbers left;
  numbers right;
};

}  // namespace

template <>
struct scan::scanner<numbers> {
  static constexpr std::string_view pattern() {
    return "([0-9]+)(?:,([0-9]+))*";
  }

  // Told nothing, told the caller's own thing, told the carrier that carries
  // it: one template, and the state is of a piece with whichever it was.
  template <class Told>
  struct state {
    std::pmr::vector<int> values;
    int running = 0;
  };

  static state<scan::default_context_t> begin_groups() { return {}; }

  // The caller's own type, and nothing of the library's in the signature.
  template <class Told>
    requires requires(const Told& one) { one.resource(); }
  static state<Told> begin_groups(const Told& told) {
    return {std::pmr::vector<int>(told.resource()), 0};
  }

  // The group's number as a plain index: one hook for both of them.
  template <class Told>
  static void push_group(state<Told>& one, std::size_t, char digit) {
    one.running = one.running * 10 + (digit - '0');
  }

  template <class Told>
  static void closed_group(state<Told>& one, std::size_t) {
    one.values.push_back(one.running);
    one.running = 0;
  }

  template <class Told>
  static numbers finish_groups(state<Told> one) {
    return {std::move(one.values)};
  }
};

namespace {

TEST(ATemplateContextOverGroups, ToldWithoutBraces) {
  std::pmr::monotonic_buffer_resource bytes;
  const both got = scan::scan<"{} {}">("1,2,3 4,5"sv).of<both>(arena{&bytes});
  EXPECT_EQ(got.left.values, (std::pmr::vector<int>{1, 2, 3}));
  EXPECT_EQ(got.right.values, (std::pmr::vector<int>{4, 5}));
}

TEST(ATemplateContextOverGroups, AndInBraces) {
  std::pmr::monotonic_buffer_resource bytes;
  std::pmr::monotonic_buffer_resource other;
  const both got = scan::scan<"{} {}">("1,2,3 4,5"sv)
                       .of<both>({arena{&bytes}, arena{&other}});
  EXPECT_EQ(got.left.values, (std::pmr::vector<int>{1, 2, 3}));
  EXPECT_EQ(got.right.values, (std::pmr::vector<int>{4, 5}));
}

TEST(ATemplateContextOverGroups, AndWithNoneAtAll) {
  const both got = scan::scan<"{} {}">("7,8 9"sv).of<both>();
  EXPECT_EQ(got.left.values, (std::pmr::vector<int>{7, 8}));
  EXPECT_EQ(got.right.values, (std::pmr::vector<int>{9}));
}

}  // namespace
