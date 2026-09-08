// The same subject read three ways gives the same answer.
//
// Characters that lie in a row are pointed at; characters that arrive once and
// never again are gathered as they go by; characters that arrive in pieces are
// pointed at inside a piece and gathered across the seams. Three machines,
// three ways of keeping what was read -- and one answer, which is what this
// asks about. It is the question the gathering machine is easiest to get wrong
// on: a field that is still open when the input ends, a reading that divides
// between two places, a list that goes on after a piece has been given up.
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

struct five {
  std::string first, second, third, fourth, fifth;
};

struct version {
  int major = 0;
  int minor = 0;
  int patch = 0;
};

}  // namespace

template <>
struct scan::scanner<version> : scan::aggregate_scanner<"{}.{}.{}"> {};

namespace {

struct release {
  version number;
  std::string name;
};

struct row {
  std::vector<int> values;
};

struct two_words {
  std::string head, tail;
};

struct counted {
  std::string word;
  int number = 0;
};

// One subject read three ways: pointed at, read once, and read in pieces small
// enough that every one of these subjects crosses a seam.
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

TEST(ReadOnceAndInPiecesAgree, EveryFieldOfARow) {
  for (std::string_view text :
       {"alpha,bravo,charlie,delta,echo", "a,b,c,d,e",
        "aaaaaaaaaaaaaaaaaaaaaaaa,b,c,d,eeeeeeeeeeeeeeee"}) {
    const auto [direct, once, pieces] =
        read_three_ways<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}", five>(
            text);
    ASSERT_TRUE(direct.has_value());
    ASSERT_TRUE(once.has_value());
    ASSERT_TRUE(pieces.has_value());
    EXPECT_EQ(once->first, direct->first);
    EXPECT_EQ(once->fifth, direct->fifth);
    EXPECT_EQ(pieces->first, direct->first);
    EXPECT_EQ(pieces->fifth, direct->fifth);
  }
}

TEST(ReadOnceAndInPiecesAgree, ARowThatDoesNotMatchIsRefusedByAllThree) {
  for (std::string_view text : {"a,b,c,d", "a,b,c,d,e,f", ""}) {
    const auto [direct, once, pieces] =
        read_three_ways<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}", five>(
            text);
    EXPECT_EQ(once.has_value(), direct.has_value());
    EXPECT_EQ(pieces.has_value(), direct.has_value());
  }
}

TEST(ReadOnceAndInPiecesAgree, AShapeInsideAShape) {
  const auto [direct, once, pieces] =
      read_three_ways<"{} {[a-z]+}", release>("1.22.333 stable");
  ASSERT_TRUE(direct.has_value());
  ASSERT_TRUE(once.has_value());
  ASSERT_TRUE(pieces.has_value());
  EXPECT_EQ(once->number.patch, 333);
  EXPECT_EQ(pieces->number.patch, 333);
  EXPECT_EQ(once->name, "stable");
  EXPECT_EQ(pieces->name, "stable");
}

TEST(ReadOnceAndInPiecesAgree, ALastFieldStillOpenAtTheEnd) {
  const auto [direct, once, pieces] =
      read_three_ways<"{[a-z]+} {}", counted>("abc 42");
  ASSERT_TRUE(direct.has_value());
  ASSERT_TRUE(once.has_value());
  ASSERT_TRUE(pieces.has_value());
  EXPECT_EQ(once->number, 42);
  EXPECT_EQ(pieces->number, 42);
}

TEST(ReadOnceAndInPiecesAgree, AListOfAsManyAsThereAre) {
  for (std::string_view text : {"1,2,3", "7", "1,2,3,4,5,6,7,8,9,10"}) {
    const auto [direct, once, pieces] = read_three_ways<"{{}{*,?}}", row>(text);
    ASSERT_TRUE(direct.has_value());
    ASSERT_TRUE(once.has_value());
    ASSERT_TRUE(pieces.has_value());
    EXPECT_EQ(once->values, direct->values);
    EXPECT_EQ(pieces->values, direct->values);
  }
}

TEST(ReadOnceAndInPiecesAgree, AReadingThatDividesBetweenTwoPlaces) {
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
