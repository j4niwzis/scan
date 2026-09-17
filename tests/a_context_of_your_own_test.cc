// A context of the caller's own, handed to the scanner that makes a value.
//
// An allocator, a pool, a piece of the caller's world -- whatever a scanner
// wants and the library has never heard of. It is told at the call, it reaches
// the one place where the value of a place is made, and it goes no further:
// what the scanner keeps of it is the scanner's business.
//
// One context is everybody's. More than one is one per place, in the order the
// places are read, with `scan::by_default` standing for a place that wants
// none.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

// A word, and the memory it was told to use. What is kept here is the resource
// itself, so that a test can say which context reached which place.
struct tagged {
  std::string text;
  const void* room = nullptr;
};

struct pair {
  tagged left;
  tagged right;
};

struct nest {
  pair both;
  tagged last;
};

}  // namespace

template <>
struct scan::scanner<tagged> {
  static constexpr std::string_view pattern() { return "[a-z]+"; }

  // The reading that was always there, for a place that was given no context.
  static tagged parse(std::string_view text) {
    return tagged{std::string(text), nullptr};
  }

  // The same reading, told where to get its room from.
  static tagged parse(std::string_view text,
                      std::pmr::polymorphic_allocator<> room) {
    return tagged{std::string(text), room.resource()};
  }
};

namespace {

class a_context_of_your_own : public ::testing::Test {
 protected:
  std::array<std::byte, 4096> bytes{};
  std::pmr::monotonic_buffer_resource room{bytes.data(), bytes.size()};
  std::pmr::polymorphic_allocator<> memory{&room};
  const void* which() const { return static_cast<const void*>(&room); }
};

TEST_F(a_context_of_your_own, OneContextIsEverybodys) {
  const auto got = scan::scan<"{} {}">("abc def"sv).of<pair>(memory);
  EXPECT_EQ(got.left.text, "abc");
  EXPECT_EQ(got.right.text, "def");
  EXPECT_EQ(got.left.room, which());
  EXPECT_EQ(got.right.room, which());
}

TEST_F(a_context_of_your_own, ToldApartOnePerPlace) {
  const auto got =
      scan::scan<"{} {}">("abc def"sv).of<pair>(memory, scan::by_default{});
  EXPECT_EQ(got.left.room, which());
  EXPECT_EQ(got.right.room, nullptr);
}

TEST_F(a_context_of_your_own, ToldBeforeTheOutputTypeIsNamed) {
  // A reading assigned to a variable names its type in the conversion, which
  // takes no arguments -- so the contexts are told to the reading instead.
  const pair got = scan::scan<"{} {}">("abc def"sv).with(memory);
  EXPECT_EQ(got.left.text, "abc");
  EXPECT_EQ(got.left.room, which());
  EXPECT_EQ(got.right.room, which());
}

TEST_F(a_context_of_your_own, SaidNothingReadsAsItAlwaysDid) {
  const auto got = scan::scan<"{} {}">("abc def"sv).of<pair>();
  EXPECT_EQ(got.left.text, "abc");
  EXPECT_EQ(got.left.room, nullptr);
  EXPECT_EQ(got.right.room, nullptr);
}

TEST_F(a_context_of_your_own, AShapeInsideAShapeTakesThemInOrder) {
  const auto got = scan::scan<"{} {} {}">("abc def ghi"sv)
                       .of<nest>(memory, scan::by_default{}, memory);
  EXPECT_EQ(got.both.left.room, which());
  EXPECT_EQ(got.both.right.room, nullptr);
  EXPECT_EQ(got.last.text, "ghi");
  EXPECT_EQ(got.last.room, which());
}

TEST_F(a_context_of_your_own, TriedForRatherThanAskedFor) {
  const auto got = scan::scan<"{} {}">("abc def"sv).try_of<pair>(memory);
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->left.room, which());
  const auto missed = scan::scan<"{} {}">("abc"sv).try_of<pair>(memory);
  EXPECT_FALSE(missed.has_value());
}

}  // namespace
