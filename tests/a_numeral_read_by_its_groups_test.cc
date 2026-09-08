// A numeral read from the groups its own pattern opened.
//
// The character-at-a-time reading of a Roman numeral asks the same question of
// every character -- which letter is this, is it bigger than the last one --
// and the machine that matched it has already answered a better one: where the
// thousands end and the hundreds begin. Written with a group around each part,
// the value is four small sums over four pieces whose lengths are known, and
// nothing is looked at twice.
//
// The same scanner says both things, because a subject that can only be read
// once has no pieces to point at: `from_groups` where there are, and
// `begin_groups` / `push_group` where the groups arrive as they are read.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct numeral {
  std::uint32_t value = 0;
};

// Characters one at a time and never again.
class read_once {
 public:
  class cursor {
   public:
    using iterator_concept = std::input_iterator_tag;
    using value_type = char;
    using difference_type = std::ptrdiff_t;

    cursor() = default;
    cursor(std::string_view text, std::size_t* at) : text_(text), at_(at) {}

    [[nodiscard]] char operator*() const { return text_[*at_]; }
    cursor& operator++() {
      ++*at_;
      return *this;
    }
    void operator++(int) { ++*this; }
    [[nodiscard]] bool operator==(std::default_sentinel_t) const {
      return *at_ == text_.size();
    }

   private:
    std::string_view text_;
    std::size_t* at_ = nullptr;
  };

  read_once(std::string_view text, std::size_t* at) : text_(text), at_(at) {}
  [[nodiscard]] cursor begin() const { return cursor(text_, at_); }
  [[nodiscard]] std::default_sentinel_t end() const { return {}; }

 private:
  std::string_view text_;
  std::size_t* at_ = nullptr;
};

// The numeral written the ordinary way, to have something to agree with.
std::string written(int number) {
  static constexpr std::array<std::pair<int, std::string_view>, 13> parts{
      {{1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"},
       {90, "XC"}, {50, "L"}, {40, "XL"}, {10, "X"}, {9, "IX"}, {5, "V"},
       {4, "IV"}, {1, "I"}}};
  std::string made;
  for (const auto& [worth, letters] : parts) {
    while (number >= worth) {
      made += letters;
      number -= worth;
    }
  }
  return made;
}

}  // namespace

template <>
struct scan::scanner<numeral> {
  // The numeral said in the four parts it is made of, each one a group. The
  // machine says where they are while it recognises the whole, and saying so
  // costs it nothing.
  static constexpr std::string_view pattern =
      "(M{0,3})(CM|CD|D?C{0,3})(XC|XL|L?X{0,3})(IX|IV|V?I{0,3})";

  // What one part is worth, from its first letter, its last, and how long it
  // is: nothing, a subtraction of two letters, five and some more, or as many
  // as there are.
  [[nodiscard]] static constexpr std::uint32_t worth(std::size_t size,
                                                     char first, char last,
                                                     std::uint32_t one,
                                                     char ten, char five) {
    if (size == 0) return 0;
    if (size == 2 && (last == ten || last == five)) {
      return last == ten ? one * 9 : one * 4;
    }
    if (first == five) {
      return one * 5 + one * static_cast<std::uint32_t>(size - 1);
    }
    return one * static_cast<std::uint32_t>(size);
  }

  [[nodiscard]] static constexpr std::uint32_t worth(std::string_view part,
                                                     std::uint32_t one,
                                                     char ten, char five) {
    if (part.empty()) return 0;
    return worth(part.size(), part.front(), part.back(), one, ten, five);
  }

  // Where the subject can be pointed at, the groups are handed over whole.
  [[nodiscard]] static constexpr numeral from_groups(
      std::span<const std::string_view> groups) {
    return {static_cast<std::uint32_t>(groups[0].size()) * 1000 +
            worth(groups[1], 100, 'M', 'D') + worth(groups[2], 10, 'C', 'L') +
            worth(groups[3], 1, 'X', 'V')};
  }

  // Where it cannot, the same four questions are answered from what was told:
  // how long each part was, and the letters at its ends.
  struct state_type {
    std::array<std::uint16_t, 4> size{};
    std::array<char, 4> first{};
    std::array<char, 4> last{};
  };

  [[nodiscard]] static constexpr state_type begin_groups() { return {}; }

  // Which group this character fell in is said by the library, either as a
  // tag it can be told apart by or as a plain number. A numeral wants the
  // number, and takes it as one.
  static constexpr void push_group(state_type& state, std::size_t group,
                                   char letter) {
    if (state.size[group] == 0) state.first[group] = letter;
    state.last[group] = letter;
    ++state.size[group];
  }

  [[nodiscard]] static constexpr numeral finish_groups(state_type state) {
    const auto told = [&](std::size_t at, std::uint32_t one, char ten,
                          char five) {
      return worth(state.size[at], state.first[at], state.last[at], one, ten,
                   five);
    };
    return {static_cast<std::uint32_t>(state.size[0]) * 1000 +
            told(1, 100, 'M', 'D') + told(2, 10, 'C', 'L') +
            told(3, 1, 'X', 'V')};
  }
};

namespace {

struct one_numeral {
  numeral number;
};

TEST(ANumeralReadByItsGroups, EveryNumeralThereIs) {
  for (int number = 1; number <= 3999; ++number) {
    const std::string text = written(number);
    const one_numeral got = scan::scan<"{}">(std::string_view(text));
    ASSERT_EQ(got.number.value, static_cast<std::uint32_t>(number));
  }
}

TEST(ANumeralReadByItsGroups, AndTheSameOffASubjectReadOnce) {
  for (std::string_view text :
       {"MCMXCIV", "MMXXV", "XLII", "III", "MMMDCCCLXXXVIII", "IX", "XIV"}) {
    std::size_t at = 0;
    const one_numeral once = scan::scan<"{}">(read_once(text, &at));
    const one_numeral pointed = scan::scan<"{}">(text);
    EXPECT_EQ(once.number.value, pointed.number.value);
  }
}

TEST(ANumeralReadByItsGroups, WhatIsNotANumeralIsNotRead) {
  const auto got = scan::scan<"{}">.try_of<one_numeral>()(std::string_view("MMMM"));
  EXPECT_FALSE(got.has_value());
}

}  // namespace
