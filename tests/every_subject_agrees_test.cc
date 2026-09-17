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

namespace {

constexpr auto subject = "1,2,3"sv;

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

}  // namespace
