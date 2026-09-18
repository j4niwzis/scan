// Where a resource said at a place stops on its way to a gathering.
//
// A field read whole keeps the resource its place was told about; the same
// field gathered a character at a time does not, and six places that begin a
// gathering have been made to build where they stand rather than assign over an
// empty one without mending it. So this asks the question directly: the scanner
// below writes down what it was told, at every hook it has, and the test says
// what arrived where. Nothing here can fail -- what it is for is the record it
// prints.
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

void say(std::string_view what, std::pmr::memory_resource* got,
         std::pmr::memory_resource* wanted) {
  std::println("  {:<28} told {}  (the pool is {}, the default is {})", what,
               static_cast<const void*>(got), static_cast<const void*>(wanted),
               static_cast<const void*>(std::pmr::get_default_resource()));
}

TEST(AResourceToldToAGathering, WhereItArrives) {
  std::pmr::monotonic_buffer_resource bytes;
  const std::pmr::polymorphic_allocator<> mine(&bytes);

  seen = {};
  const auto in_a_row = scan::scan<"{}">("abc"sv).of<one_word>(mine);
  std::println("in a row:  parse {}  begin {}", seen.parsed, seen.begun);
  say("parse was told", seen.at_parse, &bytes);
  say("the value came back on", in_a_row.word.text.get_allocator().resource(),
      &bytes);

  seen = {};
  std::size_t at = 0;
  const auto once =
      scan::scan<"{}">(read_once("abc"sv, &at)).of<one_word>(mine);
  std::println("read once: parse {}  begin {}", seen.parsed, seen.begun);
  say("begin was told", seen.at_begin, &bytes);
  say("the value came back on", once.word.text.get_allocator().resource(),
      &bytes);

  // The record is the point; the reading itself is the same either way.
  EXPECT_EQ(in_a_row.word.text, "abc");
  EXPECT_EQ(once.word.text, "abc");
}

}  // namespace
