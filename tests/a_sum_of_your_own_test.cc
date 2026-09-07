// A type that is one of several, written by somebody else.
//
// `std::variant` is the one everybody has, and nothing about a sum type is
// peculiar to it: three questions are asked of one, and a type that answers
// them is read as a sum here. This is a tagged union of the kind people write
// when a variant will not do, answering them.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct word { std::string_view text; };
struct number { std::string_view digits; };

// One of two, with the tag its own business.
class either_word_or_number {
 public:
  constexpr either_word_or_number() = default;
  constexpr either_word_or_number(word one) : which_(0), word_(one) {}
  constexpr either_word_or_number(number one) : which_(1), number_(one) {}

  [[nodiscard]] constexpr std::size_t which() const { return which_; }
  [[nodiscard]] constexpr const word& as_word() const { return word_; }
  [[nodiscard]] constexpr const number& as_number() const { return number_; }

 private:
  std::size_t which_ = 0;
  word word_{};
  number number_{};
};

struct row { either_word_or_number value; };

}  // namespace

template <>
struct scan::branches<either_word_or_number> {
  static constexpr std::size_t count = 2;

  template <std::size_t which>
  using at = std::conditional_t<which == 0, word, number>;

  template <std::size_t which, class value>
  [[nodiscard]] static constexpr either_word_or_number make(value&& one) {
    return either_word_or_number(std::forward<value>(one));
  }
};

namespace {

TEST(ASumOfYourOwn, TheFirstBranch) {
  const std::string text = "hello";
  const row one = scan::scan<"{{[a-z]+}|{[0-9]+}}">(text);
  ASSERT_EQ(one.value.which(), 0u);
  EXPECT_EQ(one.value.as_word().text, "hello");
}

TEST(ASumOfYourOwn, TheSecond) {
  const std::string text = "4210";
  const row one = scan::scan<"{{[a-z]+}|{[0-9]+}}">(text);
  ASSERT_EQ(one.value.which(), 1u);
  EXPECT_EQ(one.value.as_number().digits, "4210");
}

TEST(ASumOfYourOwn, AndOneAfterAnother) {
  const std::string text = "abc 42 def";
  std::vector<std::size_t> which;
  for (const row& one : scan::each<"{{[a-z]+}|{[0-9]+}}{* ?}">(text).of<row>()) {
    which.push_back(one.value.which());
  }
  EXPECT_EQ(which, std::vector<std::size_t>({0u, 1u, 0u}));
}

}  // namespace
