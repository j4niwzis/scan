// A subject that can only be read once, and that says how much of it was read.
//
// Two things are being pinned down here. The reading is lazy: the body of the
// loop runs on `for` while `each` is still unread, and the count says so from
// inside the body. And where the walk does read past a match -- because a
// branch above it is still alive -- what it read is given back, so the next
// record sees those characters again although the subject cannot be rewound.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

// Input and no more than input: one pass, no going back, and every character
// taken is counted where the test can see it.
class counted_reading {
 public:
  class cursor {
   public:
    using iterator_concept = std::input_iterator_tag;
    using value_type = char;
    using difference_type = std::ptrdiff_t;

    cursor() = default;
    cursor(std::string_view text, std::size_t* taken)
        : text_(text), taken_(taken) {}

    [[nodiscard]] char operator*() const { return text_[*taken_]; }
    cursor& operator++() {
      ++*taken_;
      return *this;
    }
    void operator++(int) { ++*this; }
    [[nodiscard]] bool operator==(std::default_sentinel_t) const {
      return *taken_ == text_.size();
    }

   private:
    std::string_view text_;
    std::size_t* taken_ = nullptr;
  };

  counted_reading(std::string_view text, std::size_t* taken)
      : text_(text), taken_(taken) {}

  [[nodiscard]] cursor begin() const { return cursor(text_, taken_); }
  [[nodiscard]] std::default_sentinel_t end() const { return {}; }

 private:
  std::string_view text_;
  std::size_t* taken_ = nullptr;
};

static_assert(std::ranges::input_range<counted_reading>);
static_assert(!std::ranges::forward_range<counted_reading>);
static_assert(scan::detail::read_once_char_range<counted_reading>);

struct for_word { scan::held<8> text; };
struct each_word { scan::held<8> text; };
struct whole_word { scan::held<8> text; };

// `foreach` is written last, so it is the last resort: `for` matches first and
// ends the record there.
using shortest_first = std::variant<for_word, each_word, whole_word>;
struct row { shortest_first value; };

using longest_first = std::variant<whole_word, for_word, each_word>;
struct wide_row { longest_first value; };


TEST(WhereARecordEnds, ReadsNoFurtherThanTheRecordHandedOver) {
  std::size_t taken = 0;
  std::vector<std::size_t> taken_when_the_body_ran;
  std::vector<std::size_t> which;
  for (const row& one : scan::each<"{{for}|{each}|{foreach}}">(
                            counted_reading("foreach", &taken))
                            .of<row>()) {
    taken_when_the_body_ran.push_back(taken);
    which.push_back(one.value.index());
  }
  ASSERT_EQ(which, std::vector<std::size_t>({0u, 1u}));
  // `for` is handed over having read `f`, `o`, `r` and nothing else: the four
  // characters of `each` are still unread while the body of the loop runs, and
  // `foreach` is never tried, because the walk under the match of `for` was
  // cut where the automaton was built.
  ASSERT_EQ(taken_when_the_body_ran.size(), 2u);
  EXPECT_EQ(taken_when_the_body_ran[0], 3u);
  EXPECT_EQ(taken_when_the_body_ran[1], 7u);
}

TEST(WhereARecordEnds, GivesBackWhatItReadPastTheMatch) {
  // `foreach` is preferred here, so the walk takes all four characters, finds
  // nothing, and answers with `for`. The `e` it read past the match cannot be
  // put back into the subject -- it is held instead, and the next record is
  // offered it before anything is read.
  std::size_t taken = 0;
  std::vector<std::string> seen;
  std::size_t taken_when_the_body_ran = 0;
  for (const wide_row& one : scan::each<"{{foreach}|{for}|{each}}">(
                                 counted_reading("fore", &taken))
                                 .of<wide_row>()) {
    taken_when_the_body_ran = taken;
    std::visit([&](const auto& what) {
      seen.push_back(std::string(what.text.view()));
    }, one.value);
  }
  EXPECT_EQ(seen, std::vector<std::string>({"for"}));
  // All four were read on the way to a `foreach` that was not there.
  EXPECT_EQ(taken_when_the_body_ran, 4u);
}

}  // namespace
