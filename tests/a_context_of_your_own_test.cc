// A context of the caller's own, handed to the scanner that makes a value.
//
// An allocator, a pool, a piece of the caller's world -- whatever a scanner
// wants and the library has never heard of. It is said at the call, it reaches
// the one place where the value of a place is made, and it goes no further.
//
// The places are the fields of the output, in the order they are read. A value
// said at a place is that place's and every part of it; a braced list says the
// parts of a place one by one; scan::default_context stands where a place wants
// none; one context and nothing else is everybody's.
//
// Said without braces, a context may be of any type at all -- there its type is
// never forgotten. Said in braces, it inherits scan::context, which is what
// lets the place carry it without naming it, in a constant expression too.
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

// A context of our own, and the mark it leaves on whatever was read with it.
struct room : scan::context {
  int mark = 0;
};

struct tagged {
  std::string text;
  int mark = 0;
};

struct pair {
  tagged left;
  tagged right;
};

struct nest {
  pair both;
  tagged last;
};

// A leaf read from its own groups rather than from a piece.
struct span_of {
  int lo = 0;
  int hi = 0;
  int mark = 0;
};

struct holder {
  tagged head;
  span_of range;
};

// Read while the program is compiled, where nothing may allocate.
struct counted {
  int value = 0;
};

struct two_counts {
  counted first;
  counted second;
};

}  // namespace

template <>
struct scan::scanner<tagged> {
  static constexpr std::string_view pattern() { return "[a-z]+"; }
  static tagged parse(std::string_view text) {
    return tagged{std::string(text), 0};
  }
  static tagged parse(std::string_view text, const room& where) {
    return tagged{std::string(text), where.mark};
  }
  // A context of a type that inherits nothing, which only the form without
  // braces can hand over.
  static tagged parse(std::string_view text, std::pmr::polymorphic_allocator<>) {
    return tagged{std::string(text), -1};
  }
};

template <>
struct scan::scanner<span_of> {
  static constexpr std::string_view pattern() { return "([0-9]+)-([0-9]+)"; }
  static constexpr bool reads_its_groups() { return true; }
  static span_of from_groups(std::span<const std::string_view> groups) {
    return span_of{number(groups[0]), number(groups[1]), 0};
  }
  static span_of from_groups(std::span<const std::string_view> groups,
                             const room& where) {
    return span_of{number(groups[0]), number(groups[1]), where.mark};
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

// The whole of it while the program is compiled: the context is carried as a
// scan::context and cast back where its type is known again, which a constant
// expression allows and a cast through void does not.
constexpr int at_compile_time() {
  room where;
  where.mark = 7;
  const auto got =
      scan::scan<"{} {}">("10 20"sv).of<two_counts>({where, scan::default_context});
  return got.first.value * 100 + got.second.value;
}

static_assert(at_compile_time() == 1720);

class a_context_of_your_own : public ::testing::Test {
 protected:
  room fast{{}, 1};
  room slow{{}, 2};
  std::pmr::monotonic_buffer_resource bytes;
  std::pmr::polymorphic_allocator<> anything{&bytes};
};

TEST_F(a_context_of_your_own, OneContextIsEverybodys) {
  const auto got = scan::scan<"{} {}">("abc def"sv).of<pair>(fast);
  EXPECT_EQ(got.left.text, "abc");
  EXPECT_EQ(got.left.mark, 1);
  EXPECT_EQ(got.right.mark, 1);
}

TEST_F(a_context_of_your_own, WithoutBracesAContextInheritsNothing) {
  const auto got = scan::scan<"{} {}">("abc def"sv).of<pair>(anything);
  EXPECT_EQ(got.left.mark, -1);
  EXPECT_EQ(got.right.mark, -1);
}

TEST_F(a_context_of_your_own, OnePerPlaceAndOneThatWantsNone) {
  const auto got =
      scan::scan<"{} {}">("abc def"sv).of<pair>(fast, scan::default_context);
  EXPECT_EQ(got.left.mark, 1);
  EXPECT_EQ(got.right.mark, 0);
}

TEST_F(a_context_of_your_own, BracesSayThePartsOfAPlace) {
  const auto got = scan::scan<"{} {} {}">("abc def ghi"sv)
                       .of<nest>({{fast, scan::default_context}, slow});
  EXPECT_EQ(got.both.left.mark, 1);
  EXPECT_EQ(got.both.right.mark, 0);
  EXPECT_EQ(got.last.text, "ghi");
  EXPECT_EQ(got.last.mark, 2);
}

TEST_F(a_context_of_your_own, AValueAtAPlaceIsAllOfThatPlaces) {
  const auto got = scan::scan<"{} {} {}">("abc def ghi"sv).of<nest>(fast, slow);
  EXPECT_EQ(got.both.left.mark, 1);
  EXPECT_EQ(got.both.right.mark, 1);
  EXPECT_EQ(got.last.mark, 2);
}

TEST_F(a_context_of_your_own, ALeafReadFromItsGroupsIsToldToo) {
  const auto got = scan::scan<"{} {}">("abc 3-9"sv).of<holder>({fast, slow});
  EXPECT_EQ(got.head.mark, 1);
  EXPECT_EQ(got.range.lo, 3);
  EXPECT_EQ(got.range.hi, 9);
  EXPECT_EQ(got.range.mark, 2);
}

TEST_F(a_context_of_your_own, ToldBeforeTheOutputTypeIsNamed) {
  const pair got = scan::scan<"{} {}">("abc def"sv).with(fast);
  EXPECT_EQ(got.left.mark, 1);
  EXPECT_EQ(got.right.mark, 1);
}

TEST_F(a_context_of_your_own, SaidNothingReadsAsItAlwaysDid) {
  const auto got = scan::scan<"{} {}">("abc def"sv).of<pair>();
  EXPECT_EQ(got.left.mark, 0);
  EXPECT_EQ(got.right.mark, 0);
}

TEST_F(a_context_of_your_own, TriedForRatherThanAskedFor) {
  const auto got = scan::scan<"{} {}">("abc def"sv).try_of<pair>(fast, slow);
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->left.mark, 1);
  EXPECT_EQ(got->right.mark, 2);
  const auto missed = scan::scan<"{} {}">("abc"sv).try_of<pair>(fast);
  EXPECT_FALSE(missed.has_value());
}

TEST_F(a_context_of_your_own, ReadWhileTheProgramIsCompiled) {
  EXPECT_EQ(at_compile_time(), 1720);
}

}  // namespace
