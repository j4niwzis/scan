// A subject that can only be read once, and that says how much of it was read.
//
// The reading is lazy, so the body of the loop runs on `for` while `each` is
// still unread. This subject counts every character taken out of it, and the
// count is looked at from inside the body: at that moment exactly the
// characters of the record just handed over have been taken, and not one more.
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

using either = std::variant<for_word, each_word>;
struct row { either value; };


TEST(FirstAccept, ReadsNoFurtherThanTheRecordHandedOver) {
  std::size_t taken = 0;
  std::vector<std::size_t> taken_when_the_body_ran;
  std::vector<std::size_t> which;
  for (const row& one : scan::each<"{{for}|{each}}">(
                            counted_reading("foreach", &taken))
                            .of<row>()) {
    taken_when_the_body_ran.push_back(taken);
    which.push_back(one.value.index());
  }
  ASSERT_EQ(which, std::vector<std::size_t>({0u, 1u}));
  // `for` is handed over having read `f`, `o`, `r` and nothing else: the four
  // characters of `each` are still in the subject while the body runs.
  ASSERT_EQ(taken_when_the_body_ran.size(), 2u);
  EXPECT_EQ(taken_when_the_body_ran[0], 3u);
  EXPECT_EQ(taken_when_the_body_ran[1], 7u);
}

}  // namespace
