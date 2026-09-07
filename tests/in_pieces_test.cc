// Characters as they arrive, handed on in pieces that lie in a row.
//
// A subject read once is read a character at a time, which costs the walk its
// vectors. This gathers the characters into room said in advance and hands
// over the room, so the reading is by pieces -- and the answers are the same
// answers, which is what this holds down.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

// A subject that can only be read once, wrapped around characters that are
// really in a row -- so that the one-pass reading can be asked for without a
// stream to go with it.
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

struct command { scan::held<16> name; int value; };

const std::string commands = "set speed 42\nset gain 7\nset trim 3\n";


TEST(InPieces, TheSameRecordsAsOneCharacterAtATime) {
  const auto read = [](auto&& source) {
    std::vector<std::pair<std::string, int>> got;
    for (const command& one :
         scan::each<"set {[a-z]+} {[0-9]+}\n">(std::forward<decltype(source)>(
                                                   source))
             .of<command>()) {
      got.emplace_back(std::string(one.name.view()), one.value);
    }
    return got;
  };

  std::size_t at = 0;
  const auto one_at_a_time = read(read_once(commands, &at));

  std::size_t again = 0;
  const auto in_pieces = read(read_once(commands, &again) | scan::in_pieces<8>);

  const std::vector<std::pair<std::string, int>> wanted{
      {"speed", 42}, {"gain", 7}, {"trim", 3}};
  EXPECT_EQ(one_at_a_time, wanted);
  EXPECT_EQ(in_pieces, wanted);
}

TEST(InPieces, APieceSmallerThanARecord) {
  // Four characters at a time, against records of thirteen: every record
  // spans pieces, and the walk asks for the next one in the middle of a
  // field.
  std::size_t at = 0;
  std::vector<int> values;
  for (const command& one :
       scan::each<"set {[a-z]+} {[0-9]+}\n">(read_once(commands, &at) |
                                             scan::in_pieces<4>)
           .of<command>()) {
    values.push_back(one.value);
  }
  EXPECT_EQ(values, std::vector<int>({42, 7, 3}));
}

TEST(InPieces, APieceLargerThanTheWholeSubject) {
  std::size_t at = 0;
  std::vector<int> values;
  for (const command& one :
       scan::each<"set {[a-z]+} {[0-9]+}\n">(read_once(commands, &at) |
                                             scan::in_pieces<4096>)
           .of<command>()) {
    values.push_back(one.value);
  }
  EXPECT_EQ(values, std::vector<int>({42, 7, 3}));
}

TEST(InPieces, TheCharactersThemselves) {
  // What it hands over is what it was given, in pieces of the size asked for.
  std::size_t at = 0;
  std::string put_together;
  std::vector<std::size_t> sizes;
  for (std::string_view piece : read_once(commands, &at) | scan::in_pieces<8>) {
    put_together += piece;
    sizes.push_back(piece.size());
  }
  EXPECT_EQ(put_together, commands);
  for (std::size_t size : sizes | std::views::take(sizes.size() - 1)) {
    EXPECT_EQ(size, 8u);
  }
}

}  // namespace
