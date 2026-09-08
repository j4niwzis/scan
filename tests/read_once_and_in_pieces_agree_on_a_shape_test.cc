// A shape read three ways gives one answer.
//
// A field that is itself a shape is several places, and a field still open when
// the input ends is read from where it was gathered rather than from a copy.
// Neither of those is the same work on a subject that can be pointed at and on
// one that cannot, and both have to come out the same.
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

struct counted {
  std::string word;
  int number = 0;
};

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

TEST(ReadOnceAndInPiecesAgreeOnAShape, AShapeInsideAShape) {
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

// The field nothing closes: the input ends while it is still being read, so it
// is read out of the register it was gathered in and not out of a copy taken
// when it closed.
TEST(ReadOnceAndInPiecesAgreeOnAShape, ALastFieldStillOpenAtTheEnd) {
  const auto [direct, once, pieces] =
      read_three_ways<"{[a-z]+} {}", counted>("abc 42");
  ASSERT_TRUE(direct.has_value());
  ASSERT_TRUE(once.has_value());
  ASSERT_TRUE(pieces.has_value());
  EXPECT_EQ(once->number, 42);
  EXPECT_EQ(pieces->number, 42);
}

}  // namespace
