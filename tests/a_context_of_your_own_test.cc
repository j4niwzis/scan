// A context of the caller's own, handed to the scanner that makes a value.
//
// An allocator, a pool, a piece of the caller's world -- whatever a scanner
// wants and the library has never heard of. It is said at the call, it reaches
// the one place where the value of a place is made, and it goes no further.
//
// The places are the fields of the output, in the order they are read. A value
// said at a place is that place's and every part of it; a braced list said at a
// place says its parts one by one; `scan::default_context` stands where a place
// wants none. One context and nothing else is everybody's.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

// A word, and the memory it was told to use -- kept so that a test can say
// which context reached which place.
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

  // The reading that was always there, for a place that was told nothing.
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
  std::pmr::monotonic_buffer_resource one{bytes.data(), bytes.size() / 2};
  std::pmr::monotonic_buffer_resource two{bytes.data() + bytes.size() / 2,
                                          bytes.size() / 2};
  std::pmr::polymorphic_allocator<> fast{&one};
  std::pmr::polymorphic_allocator<> slow{&two};
  const void* first() const { return static_cast<const void*>(&one); }
  const void* second() const { return static_cast<const void*>(&two); }
};

TEST_F(a_context_of_your_own, OneContextIsEverybodys) {
  const auto got = scan::scan<"{} {}">("abc def"sv).of<pair>(fast);
  EXPECT_EQ(got.left.text, "abc");
  EXPECT_EQ(got.right.text, "def");
  EXPECT_EQ(got.left.room, first());
  EXPECT_EQ(got.right.room, first());
}

TEST_F(a_context_of_your_own, OnePerPlace) {
  const auto got = scan::scan<"{} {}">("abc def"sv).of<pair>(fast, slow);
  EXPECT_EQ(got.left.room, first());
  EXPECT_EQ(got.right.room, second());
}

TEST_F(a_context_of_your_own, APlaceThatWantsNoneSaysSo) {
  const auto got =
      scan::scan<"{} {}">("abc def"sv).of<pair>(fast, scan::default_context);
  EXPECT_EQ(got.left.room, first());
  EXPECT_EQ(got.right.room, nullptr);
}

TEST_F(a_context_of_your_own, BracesSayThePartsOfAPlace) {
  const auto got = scan::scan<"{} {} {}">("abc def ghi"sv)
                       .of<nest>({{fast, scan::default_context}, slow});
  EXPECT_EQ(got.both.left.room, first());
  EXPECT_EQ(got.both.right.room, nullptr);
  EXPECT_EQ(got.last.text, "ghi");
  EXPECT_EQ(got.last.room, second());
}

TEST_F(a_context_of_your_own, AValueAtAPlaceIsAllOfThatPlaces) {
  const auto got = scan::scan<"{} {} {}">("abc def ghi"sv).of<nest>(fast, slow);
  EXPECT_EQ(got.both.left.room, first());
  EXPECT_EQ(got.both.right.room, first());
  EXPECT_EQ(got.last.room, second());
}

TEST_F(a_context_of_your_own, ToldBeforeTheOutputTypeIsNamed) {
  // A reading assigned to a variable names its type in the conversion, which
  // takes no arguments -- so the contexts are told to the reading instead.
  const pair got = scan::scan<"{} {}">("abc def"sv).with(fast);
  EXPECT_EQ(got.left.text, "abc");
  EXPECT_EQ(got.left.room, first());
  EXPECT_EQ(got.right.room, first());
}

TEST_F(a_context_of_your_own, SaidNothingReadsAsItAlwaysDid) {
  const auto got = scan::scan<"{} {}">("abc def"sv).of<pair>();
  EXPECT_EQ(got.left.text, "abc");
  EXPECT_EQ(got.left.room, nullptr);
  EXPECT_EQ(got.right.room, nullptr);
}

TEST_F(a_context_of_your_own, TriedForRatherThanAskedFor) {
  const auto got = scan::scan<"{} {}">("abc def"sv).try_of<pair>(fast, slow);
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->left.room, first());
  EXPECT_EQ(got->right.room, second());
  const auto missed = scan::scan<"{} {}">("abc"sv).try_of<pair>(fast);
  EXPECT_FALSE(missed.has_value());
}

}  // namespace
