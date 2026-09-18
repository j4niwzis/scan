// Where a resource said at a place stops on its way to a gathering.
//
// A field read whole keeps the resource its place was told about; the same
// field gathered a character at a time does not. The scanner below writes down
// what it was told at every hook it has, so what fails here says which half of
// the road is broken: the context on its way to `begin`, or the gathering on
// its way from `begin` to the value.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

// What the scanner below was told, and where.
struct what_arrived {
  std::pmr::memory_resource* at_parse = nullptr;
  std::pmr::memory_resource* at_begin = nullptr;
  std::pmr::memory_resource* untold_parse = 0;
  std::pmr::memory_resource* untold_begin = 0;
  bool parsed = false;
  bool begun = false;
};

what_arrived seen;

struct kept_word {
  std::pmr::string text;
};

struct one_word {
  kept_word word;
};

}  // namespace

template <>
struct scan::scanner<kept_word> {
  [[nodiscard]] static constexpr std::string_view pattern() { return "[a-z]+"; }

  using state_type = std::pmr::string;

  [[nodiscard]] static state_type begin(std::string_view) {
    seen.begun = true;
    seen.untold_begin = std::pmr::get_default_resource();
    return {};
  }
  [[nodiscard]] static state_type begin(std::string_view,
                                        std::pmr::polymorphic_allocator<> where) {
    seen.begun = true;
    seen.at_begin = where.resource();
    return state_type(where);
  }
  static void push(state_type& state, char value) { state.push_back(value); }
  [[nodiscard]] static kept_word finish(state_type state) {
    return kept_word{std::move(state)};
  }

  [[nodiscard]] static kept_word parse(std::string_view text) {
    seen.parsed = true;
    seen.untold_parse = std::pmr::get_default_resource();
    return kept_word{std::pmr::string(text)};
  }
  [[nodiscard]] static kept_word parse(std::string_view text,
                                       std::pmr::polymorphic_allocator<> where) {
    seen.parsed = true;
    seen.at_parse = where.resource();
    return kept_word{std::pmr::string(text, where)};
  }
};

namespace {

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

TEST(AResourceToldToAGathering, WhereItArrives) {
  std::pmr::monotonic_buffer_resource bytes;
  const std::pmr::polymorphic_allocator<> mine(&bytes);

  seen = {};
  const auto in_a_row = scan::scan<"{}">("abc"sv).of<one_word>(mine);
  EXPECT_EQ(in_a_row.word.text, "abc");
  EXPECT_TRUE(seen.parsed) << "in a row: the whole piece is what a place is "
                              "read from, so parse is what should be called";
  EXPECT_EQ(seen.at_parse, &bytes) << "in a row: parse was told no resource";
  EXPECT_EQ(in_a_row.word.text.get_allocator().resource(), &bytes)
      << "in a row: the value came back on another resource than it was made "
         "with";

  seen = {};
  std::size_t at = 0;
  const auto once =
      scan::scan<"{}">(read_once("abc"sv, &at)).of<one_word>(mine);
  EXPECT_EQ(once.word.text, "abc");
  EXPECT_TRUE(seen.begun) << "read once: nothing can be pointed at, so the "
                             "gathering hooks are what should be called";
  EXPECT_FALSE(seen.parsed) << "read once: parse cannot be called, there is "
                               "nothing to hand it";
  // The two halves of the question: was the place told, and did what it was
  // told reach the value.
  EXPECT_EQ(seen.at_begin, &bytes)
      << "read once: begin was told no resource -- the context stops before "
         "the gathering is begun";
  EXPECT_EQ(once.word.text.get_allocator().resource(), &bytes)
      << "read once: begin was told the pool and the value still came back "
         "elsewhere -- the gathering is dropped between begin and finish";
}

}  // namespace
