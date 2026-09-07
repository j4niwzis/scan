// A type told where its groups begin and end, which is what a list is made of.
//
// A repeated group takes a turn per element, and the type is told when one
// ended; a choice is a group per branch, and the one whose group opened is the
// one that ran. Both were things only a format could say, and both are now
// things a type can say for itself -- so `aggregate_scanner` is a convenience
// rather than a privilege.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

// As many numbers as the subject turns out to have.
struct numbers {
  std::vector<int> values;
};

}  // namespace

template <>
struct scan::scanner<numbers> {
  // One group, taken over and over.
  static constexpr std::string_view pattern() { return "([0-9]+)(?:,([0-9]+))*"; }

  struct state {
    numbers made;
    int gathering = 0;
    bool going = false;
  };

  static constexpr state begin_groups() { return {}; }

  static constexpr void opened_group(state& into, std::size_t) {
    into.gathering = 0;
    into.going = true;
  }

  static constexpr void push_group(state& into, std::size_t, char letter) {
    into.gathering = into.gathering * 10 + (letter - '0');
  }

  static constexpr void closed_group(state& into, std::size_t) {
    if (!into.going) return;
    into.made.values.push_back(into.gathering);
    into.going = false;
  }

  static constexpr numbers finish_groups(state from) {
    return std::move(from.made);
  }
};

namespace {

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


TEST(AListOfYourOwn, EveryTurnIsAnElement) {
  const std::string text = "[1,22,333]";
  std::size_t at = 0;
  const auto found =
      scan::match<"\\[(([0-9]+)(?:,([0-9]+))*)\\]">.into(
          scan::as<numbers>())(read_once(text, &at));
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.get<1>().values, std::vector<int>({1, 22, 333}));
}

TEST(AListOfYourOwn, AndOneTurnIsOne) {
  const std::string text = "[7]";
  std::size_t at = 0;
  const auto found =
      scan::match<"\\[(([0-9]+)(?:,([0-9]+))*)\\]">.into(
          scan::as<numbers>())(read_once(text, &at));
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.get<1>().values, std::vector<int>({7}));
}

}  // namespace
