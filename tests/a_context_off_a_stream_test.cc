// A context said where the subject does not lie in a row.
//
// A subject read once, or handed over in pieces, is gathered as it arrives: the
// places are told their contexts at the door and each one begins with what it
// was told. Said as a pack or said in braces, it reaches the same place -- a
// scanner that gathers its own groups is begun by the carrier, where the type
// of the context is still known.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

struct room {
  int mark = 0;
};

struct numbers {
  std::vector<int> values;
  int mark = 0;
};

struct row {
  numbers list;
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

namespace {

class a_context_off_a_stream : public ::testing::Test {
 protected:
  std::string text{"1,22,333"};
  room fast{7};
};

TEST_F(a_context_off_a_stream, SaidAsAPackItReachesThePlace) {
  std::size_t at = 0;
  const auto got = scan::scan<"{}">(read_once(text, &at)).of<row>(fast);
  EXPECT_EQ(got.list.mark, 7);
}

// The braced form carries the context behind an interface that knows the field,
// and the beginning of a fold is part of that interface: the carrier begins it
// where the type of the context is still known.
TEST_F(a_context_off_a_stream, SaidInBracesItReachesTheSamePlace) {
  std::size_t at = 0;
  const auto got = scan::scan<"{}">(read_once(text, &at)).of<row>({fast});
  EXPECT_EQ(got.list.mark, 7);
}

TEST_F(a_context_off_a_stream, ToldNothingItBeginsTheWayItAlwaysDid) {
  std::size_t at = 0;
  const auto got = scan::scan<"{}">(read_once(text, &at)).of<row>();
  EXPECT_EQ(got.list.mark, 0);
}

// The same, handed over in pieces small enough that the subject crosses a seam.
TEST_F(a_context_off_a_stream, InPiecesItIsToldTheSame) {
  std::size_t at = 0;
  const auto got = scan::scan<"{}">(read_once(text, &at) | scan::in_pieces<4>)
                       .of<row>(fast);
  EXPECT_EQ(got.list.mark, 7);
}

}  // namespace
