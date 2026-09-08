// A row of fields read three ways gives one answer.
//
// Characters that lie in a row are pointed at; characters that arrive once and
// never again are gathered as they go by; characters that arrive in pieces are
// pointed at inside a piece and gathered across the seams. Three machines,
// three ways of keeping what was read, and one answer -- which is what this
// asks about.
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

TEST(ReadOnceAndInPiecesAgreeOnARow, EveryField) {
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

TEST(ReadOnceAndInPiecesAgreeOnARow, AndOnWhatIsNotOne) {
  for (std::string_view text : {"a,b,c,d", "a,b,c,d,e,f", ""}) {
    const auto [direct, once, pieces] =
        read_three_ways<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}", five>(
            text);
    EXPECT_FALSE(direct.has_value());
    EXPECT_EQ(once.has_value(), direct.has_value());
    EXPECT_EQ(pieces.has_value(), direct.has_value());
  }
}

}  // namespace
