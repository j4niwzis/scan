// A list, and two places that divide a reading, read three ways.
//
// A list goes on for as long as the subject affords, and where it is read once
// each turn is gathered and handed over before the next begins. Two places that
// can each take the same character divide the reading between them, and the
// machine reads both ways at once -- what each of them gathered has to stay
// apart.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

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

struct row {
  std::vector<int> values;
};

struct two_words {
  std::string head, tail;
};


}  // namespace

namespace {

// One subject read three ways: pointed at, read once, and read in pieces small
// enough that these subjects cross a seam.
template <scan::fixed_string format, class type>
[[nodiscard]] auto read_three_ways(std::string_view text) {
  constexpr auto read = scan::scan<format>.template try_of<type>();
  std::size_t once_at = 0;
  std::size_t pieces_at = 0;
  auto direct = read(std::string_view(text));
  auto once = read(read_once(text, &once_at));
  auto pieces = read(read_once(text, &pieces_at) | scan::in_pieces<8>);
  return std::tuple(std::move(direct), std::move(once), std::move(pieces));
}

TEST(ReadOnceAndInPiecesAgreeOnAList, AsManyAsThereAre) {
  for (std::string_view text : {"1,2,3", "7", "1,2,3,4,5,6,7,8,9,10"}) {
    const auto [direct, once, pieces] = read_three_ways<"{{}{*,?}}", row>(text);
    ASSERT_TRUE(direct.has_value());
    ASSERT_TRUE(once.has_value());
    ASSERT_TRUE(pieces.has_value());
    EXPECT_EQ(once->values, direct->values);
    EXPECT_EQ(pieces->values, direct->values);
  }
}

// Where one place ends and the next begins is not known until the end, so the
// machine reads both ways at once and the gatherings must not run together.
TEST(ReadOnceAndInPiecesAgreeOnAList, AReadingThatDividesBetweenTwoPlaces) {
  for (std::string_view text : {"abcdef", "a", "abcdefghijklmnopqrstuvwxyz"}) {
    const auto [direct, once, pieces] =
        read_three_ways<"{[a-z]*}{[a-z]*}", two_words>(text);
    ASSERT_TRUE(direct.has_value());
    ASSERT_TRUE(once.has_value());
    ASSERT_TRUE(pieces.has_value());
    EXPECT_EQ(once->head, direct->head);
    EXPECT_EQ(once->tail, direct->tail);
    EXPECT_EQ(pieces->head, direct->head);
    EXPECT_EQ(pieces->tail, direct->tail);
  }
}

}  // namespace
