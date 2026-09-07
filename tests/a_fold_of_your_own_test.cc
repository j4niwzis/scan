// A type told its own groups as the walk passes them.
//
// A machine with tags keeps one position per tag, so what is left when the
// match is over is the last turn round a loop and nothing before it. A type
// whose pattern repeats therefore cannot be built from what is left: it has to
// be told the turns as they happen and fold them itself. That is what a list
// has always done here, and this is the same thing said in the user's own
// terms -- `begin_groups`, an opening, the characters, a closing, and the
// answer at the end.
//
// The state is the only thing the fold may touch. The walk stands in several
// readings at once and carries a fold with each of them, so the state is
// copied and sometimes dropped; anything written outside it would be written
// for a reading that never happened.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

// A list of numbers, held by a type that is not a list.
struct numbers {
  std::vector<int> values;
};

// A version, whose groups happen once each. The same hooks, with nothing
// repeating, to show the fold is not a thing only loops can use.
struct version {
  int major = 0;
  int minor = 0;
};

}  // namespace

template <>
struct scan::scanner<numbers> {
  struct state {
    std::vector<int> values;
    int running = 0;
  };

  // The first number, and every one after a comma. The second group is inside
  // a repetition, so it opens and closes once a turn.
  static constexpr std::string_view pattern() {
    return "([0-9]+)(?:,([0-9]+))*";
  }

  static constexpr state begin_groups() { return state{}; }

  // One overload a group, picked by the group's own number said as a type.
  static constexpr void opened_group(state& made, scan::group_at<0>) {
    made.running = 0;
  }
  static constexpr void opened_group(state& made, scan::group_at<1>) {
    made.running = 0;
  }
  static constexpr void push_group(state& made, scan::group_at<0>, char value) {
    made.running = made.running * 10 + (value - '0');
  }
  static constexpr void push_group(state& made, scan::group_at<1>, char value) {
    made.running = made.running * 10 + (value - '0');
  }
  static constexpr void closed_group(state& made, scan::group_at<0>) {
    made.values.push_back(made.running);
  }
  static constexpr void closed_group(state& made, scan::group_at<1>) {
    made.values.push_back(made.running);
  }

  static constexpr numbers finish_groups(state made) {
    return numbers{std::move(made.values)};
  }
};

template <>
struct scan::scanner<version> {
  struct state {
    int at[2]{};
    std::size_t which = 0;
  };

  static constexpr std::string_view pattern() { return "([0-9]+)\\.([0-9]+)"; }

  static constexpr state begin_groups() { return state{}; }
  static constexpr void push_group(state& made, std::size_t group, char value) {
    made.at[group] = made.at[group] * 10 + (value - '0');
  }
  static constexpr version finish_groups(state made) {
    return version{made.at[0], made.at[1]};
  }
};

namespace {

struct row {
  numbers list;
  scan::held<16> name;
};

struct release {
  version number;
  scan::held<16> name;
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

TEST(AFoldOfYourOwn, ATurnAtATime) {
  const std::string text = "1,22,333 stable";
  const row one = scan::scan<"{} {[a-z]+}">(text).of<row>();
  EXPECT_EQ(one.list.values, std::vector<int>({1, 22, 333}));
  EXPECT_EQ(one.name.view(), "stable");
}

TEST(AFoldOfYourOwn, OneTurnIsATurnToo) {
  const std::string text = "7 alpha";
  const row one = scan::scan<"{} {[a-z]+}">(text).of<row>();
  EXPECT_EQ(one.list.values, std::vector<int>({7}));
}

TEST(AFoldOfYourOwn, TheSameOffASubjectReadOnce) {
  const std::string text = "1,22,333 stable";
  std::size_t at = 0;
  const row one = scan::scan<"{} {[a-z]+}">(read_once(text, &at)).of<row>();
  EXPECT_EQ(one.list.values, std::vector<int>({1, 22, 333}));
  EXPECT_EQ(one.name.view(), "stable");
}

TEST(AFoldOfYourOwn, RecordAfterRecordOffAStream) {
  const std::string text = "1,2 alpha\n3,4,5 beta\n";
  std::size_t at = 0;
  std::vector<std::size_t> counts;
  for (const row& one :
       scan::each<"{} {[a-z]+}\n">(read_once(text, &at)).of<row>()) {
    counts.push_back(one.list.values.size());
  }
  EXPECT_EQ(counts, std::vector<std::size_t>({2, 3}));
}

TEST(AFoldOfYourOwn, AFoldThatDoesNotRepeat) {
  const std::string text = "1.22 stable";
  const release one = scan::scan<"{} {[a-z]+}">(text).of<release>();
  EXPECT_EQ(one.number.major, 1);
  EXPECT_EQ(one.number.minor, 22);
  EXPECT_EQ(one.name.view(), "stable");
}

}  // namespace
