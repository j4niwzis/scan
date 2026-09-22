// A list told its context in braces.
//
// A list is read by the machine that gathers as it goes, even where the subject
// lies in a row -- and that walk used to be started without what the caller
// said. Nothing was dropped loudly: the places were simply told nothing, and a
// scanner that takes a context was called as though none had been given.
//
// So this says the plain thing first -- every element is told what the list's
// place was told -- and then the thing that brought it to light: a place whose
// type keeps its own memory resource is built on the resource the caller said,
// with nobody writing a scanner for it.
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

struct counted {
  int value = 0;
};

struct row {
  std::pmr::vector<counted> values;
};

}  // namespace

// Read whole where the subject can be pointed at, and told its characters as
// they arrive where it cannot -- which is what an element of a list has to be,
// because the walk gathers a turn as it goes.
template <>
struct scan::scanner<counted> {
  static constexpr std::string_view pattern() { return "[0-9]+"; }

  struct state {
    int running = 0;
    int mark = 0;
  };
  static constexpr state begin() { return {}; }
  static constexpr state begin(std::string_view) { return {}; }
  static constexpr state begin(std::string_view, const room& where) {
    return state{0, where.mark};
  }
  static constexpr void push(state& made, char value) {
    made.running = made.running * 10 + (value - '0');
  }
  static constexpr counted finish(state made) {
    return counted{made.running + made.mark};
  }

  static constexpr counted parse(std::string_view text) {
    return counted{number(text)};
  }
  static constexpr counted parse(std::string_view text, const room& where) {
    return counted{number(text) + where.mark};
  }
};

namespace {

class a_list_told_in_braces : public ::testing::Test {
 protected:
  room fast{10};
  std::pmr::monotonic_buffer_resource bytes;
  std::pmr::polymorphic_allocator<> mine{&bytes};
};


// What a braced context reaches in a list is the list itself: the carrier
// keeps one reading per leaf and a leaf is typed by its field, so the list is
// built with what the caller said. Its elements are told through a reading of
// their own, which this carrier does not keep yet -- said without braces they
// are told, and that is what `a_context_reaches_a_list` says.
TEST_F(a_list_told_in_braces, TheListIsBuiltOnTheResourceItWasTold) {
  const auto got = scan::scan<"{{}{*,?}}">("1,2,3"sv).of<row>({mine});
  ASSERT_EQ(got.values.size(), 3u);
  EXPECT_EQ(got.values.get_allocator().resource(), &bytes);
}

}  // namespace
