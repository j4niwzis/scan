// One reading, every kind of subject, told and untold.
//
// The same format and the same output type go four ways inside: a subject in a
// row read by pointing at it, one in a row whose reading holds a list and so is
// gathered turn by turn, one that arrives a character at a time, and one handed
// over in pieces. They are meant to be the same reading, and what makes them
// the same is nobody writing any of them twice.
//
// This is the shape the rest of the suite is worth having in: a table, not a
// case. Both of the faults this file was written after were the same fault --
// one branch doing what the branch beside it forgot -- and neither could have
// survived a table.
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
  std::vector<counted> values;
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

}  // namespace

// Read whole where the subject can be pointed at, and told its groups as they
// arrive where it cannot -- so one element type suits every subject here.
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

// A place told its groups as they arrive, rather than read whole: the other way
// a reading holds turns, and the other walk it takes.
namespace {
struct numbers {
  std::vector<int> values;
  int mark = 0;
};
struct folded {
  numbers list;
};
}  // namespace

template <>
struct scan::scanner<numbers> {
  struct state {
    std::vector<int> values;
    int running = 0;
    int mark = 0;
  };
  static constexpr std::string_view pattern() {
    return "([0-9]+)(?:,([0-9]+))*";
  }
  static constexpr state begin_groups() { return state{}; }
  static constexpr state begin_groups(const room& where) {
    return state{{}, 0, where.mark};
  }
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
    return numbers{std::move(made.values), made.mark};
  }
};

// One of several: exactly one branch runs, and which one is not known where the
// call is written -- so a context for each branch has to reach the one that did,
// whichever road the subject took.
namespace {
struct dashed {
  int letters = 0;
  int mark = 0;
};
struct picked {
  std::variant<counted, dashed> pick;
};
}  // namespace

template <>
struct scan::scanner<dashed> {
  static constexpr std::string_view pattern() { return "[a-z]+"; }

  struct state {
    int letters = 0;
    int mark = 0;
  };
  static constexpr state begin() { return {}; }
  static constexpr state begin(std::string_view) { return {}; }
  static constexpr state begin(std::string_view, const room& where) {
    return state{0, where.mark};
  }
  static constexpr void push(state& made, char) { ++made.letters; }
  static constexpr dashed finish(state made) {
    return dashed{made.letters, made.mark};
  }

  static constexpr dashed parse(std::string_view text) {
    return dashed{static_cast<int>(text.size()), 0};
  }
  static constexpr dashed parse(std::string_view text, const room& where) {
    return dashed{static_cast<int>(text.size()), where.mark};
  }
};

// A list whose element is a shape of its own, read by its own scanner. Two
// things are wrong here and both are written down rather than worked around:
// the first turn never reaches the list, and what the list's place was told
// does not reach the element's places. These tests fail, and are here because a
// fault nobody has written down is a fault that comes back.
namespace {
struct both {
  counted number;
  dashed word;
};
struct rows {
  std::vector<both> items;
};
}  // namespace

template <>
struct scan::scanner<both> : scan::aggregate_scanner<"{}:{}"> {};

namespace {

constexpr auto subject = "1,2,3"sv;
constexpr auto pairs = "1:ab 2:cd 3:ef"sv;
constexpr auto digits = "42"sv;
constexpr auto letters = "abc"sv;

void expect_read(const row& got, int base) {
  ASSERT_EQ(got.values.size(), 3u);
  EXPECT_EQ(got.values[0].value, base + 1);
  EXPECT_EQ(got.values[1].value, base + 2);
  EXPECT_EQ(got.values[2].value, base + 3);
}

class every_subject : public ::testing::Test {
 protected:
  room fast{10};
  std::size_t at = 0;
};

TEST_F(every_subject, InARowAndTold) {
  expect_read(scan::scan<"{{}{*,?}}">(subject).of<row>(fast), 10);
}

TEST_F(every_subject, ReadOnceAndTold) {
  expect_read(scan::scan<"{{}{*,?}}">(read_once(subject, &at)).of<row>(fast), 10);
}

TEST_F(every_subject, InPiecesAndTold) {
  expect_read(scan::scan<"{{}{*,?}}">(read_once(subject, &at) | scan::in_pieces<2>)
                  .of<row>(fast),
              10);
}

TEST_F(every_subject, InARowAndToldNothing) {
  expect_read(scan::scan<"{{}{*,?}}">(subject).of<row>(), 0);
}

TEST_F(every_subject, ReadOnceAndToldNothing) {
  expect_read(scan::scan<"{{}{*,?}}">(read_once(subject, &at)).of<row>(), 0);
}

TEST_F(every_subject, InPiecesAndToldNothing) {
  expect_read(scan::scan<"{{}{*,?}}">(read_once(subject, &at) | scan::in_pieces<2>)
                  .of<row>(),
              0);
}

TEST_F(every_subject, AFoldInARowIsTold) {
  EXPECT_EQ(scan::scan<"{}">(subject).of<folded>(fast).list.mark, 10);
}

TEST_F(every_subject, AFoldReadOnceIsTold) {
  EXPECT_EQ(scan::scan<"{}">(read_once(subject, &at)).of<folded>(fast).list.mark,
            10);
}

TEST_F(every_subject, AFoldInPiecesIsTold) {
  EXPECT_EQ(scan::scan<"{}">(read_once(subject, &at) | scan::in_pieces<2>)
                .of<folded>(fast)
                .list.mark,
            10);
}

TEST_F(every_subject, EveryTurnOfAListOfShapesArrives) {
  const auto got = scan::scan<"{{}{* ?}}+">(pairs).of<rows>();
  ASSERT_EQ(got.items.size(), 3u);
  EXPECT_EQ(got.items[0].number.value, 1);
  EXPECT_EQ(got.items[1].number.value, 2);
  EXPECT_EQ(got.items[2].number.value, 3);
}

TEST_F(every_subject, AListOfShapesTellsItsElementsWhatItWasTold) {
  const auto got = scan::scan<"{{}{* ?}}+">(pairs).of<rows>(fast);
  ASSERT_EQ(got.items.size(), 3u);
  EXPECT_EQ(got.items[0].number.value, 11);
  EXPECT_EQ(got.items[0].word.mark, 10);
  EXPECT_EQ(got.items[2].word.mark, 10);
}

TEST_F(every_subject, TheBranchThatRanIsTold) {
  EXPECT_EQ(std::get<0>(scan::scan<"{}">(digits).of<picked>(fast).pick).value, 52);
  EXPECT_EQ(std::get<1>(scan::scan<"{}">(letters).of<picked>(fast).pick).mark, 10);
}

TEST_F(every_subject, TheBranchThatRanIsToldOffAStream) {
  EXPECT_EQ(
      std::get<0>(scan::scan<"{}">(read_once(digits, &at)).of<picked>(fast).pick)
          .value,
      52);
  std::size_t pieces = 0;
  EXPECT_EQ(std::get<1>(scan::scan<"{}">(read_once(letters, &pieces) |
                                         scan::in_pieces<2>)
                            .of<picked>(fast)
                            .pick)
                .mark,
            10);
}

// A context per branch, said in braces -- and the branch that runs is told its
// own, on a subject in a row and on one that arrives a character at a time.
TEST_F(every_subject, EachBranchIsToldItsOwnWhereverItIsRead) {
  room slow{2};
  EXPECT_EQ(
      std::get<0>(scan::scan<"{}">(digits).of<picked>({{fast, slow}}).pick).value,
      52);
  EXPECT_EQ(
      std::get<1>(scan::scan<"{}">(letters).of<picked>({{fast, slow}}).pick).mark,
      2);
  EXPECT_EQ(std::get<1>(scan::scan<"{}">(read_once(letters, &at))
                            .of<picked>({{fast, slow}})
                            .pick)
                .mark,
            2);
}

TEST_F(every_subject, AFoldToldNothingIsToldNothing) {
  EXPECT_EQ(scan::scan<"{}">(subject).of<folded>().list.mark, 0);
  std::size_t once = 0;
  EXPECT_EQ(scan::scan<"{}">(read_once(subject, &once)).of<folded>().list.mark, 0);
  std::size_t pieces = 0;
  EXPECT_EQ(scan::scan<"{}">(read_once(subject, &pieces) | scan::in_pieces<2>)
                .of<folded>()
                .list.mark,
            0);
}

}  // namespace
