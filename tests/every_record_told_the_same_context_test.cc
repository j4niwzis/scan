// One record after another off a reading that arrives as it is read, every one
// of them told the same contexts.
//
// Told as the contexts stand and not in braces. What a reading is told is held
// as the address of it, and this reading is a view: it reads a record when it
// is asked for one, which is after the call that named its output has ended.
// A braced list makes its contexts at that call and nothing else holds them,
// so the form this reading can take is the one whose contexts the caller is
// still holding -- `contexts_at_places` binds `Contexts&`, and a temporary is
// refused where it would be read after it died.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {


using namespace std::string_view_literals;

// A subject that can only be read once, and that says how much of it was read.
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

// A context of the caller's own: a mark to leave on what was read with it, and
// somewhere to write down that it was handed over at all.
struct room {
  int mark = 0;
  std::vector<std::string>* said = nullptr;

  void say(std::string what) const {
    if (said != nullptr) said->push_back(std::move(what));
  }
};

// The branches of the alternation. `foreach` is written first, so it is
// preferred: on "fore" the walk takes it as far as it goes, finds it is not
// there, and comes back to the match of `for` underneath it.
struct for_word { scan::held<8> text; };
struct each_word { scan::held<8> text; };
struct whole_word { scan::held<8> text; };

using longest_first = std::variant<whole_word, for_word, each_word>;
struct choice { longest_first value; };

// A leaf told a context.
struct tagged {
  std::string text;
  int mark = 0;
};

// A fold: its state is built turn by turn as the walk passes, which is the
// state that going back to a match has to restore.
struct numbers {
  std::vector<int> values;
  int mark = 0;
};

// The place that is a shape of two parts, so that braces have two to say.
struct head_of {
  choice which;
  tagged name;
};

struct row {
  head_of head;
  numbers tail;
};

}  // namespace

// Told its characters as they are read, because a subject that arrives as it
// is read has nothing in a row to hand over when the match is over.
template <>
struct scan::scanner<tagged> {
  struct state {
    std::string text;
    int mark = 0;
    const room* where = nullptr;
  };

  static constexpr std::string_view pattern() { return "([a-z]+)"; }

  static state begin_groups() { return state{}; }
  static state begin_groups(const room& where) {
    where.say("name begins");
    return state{{}, where.mark, &where};
  }

  static void opened_group(state& made, scan::group_at<0>) {
    made.text.clear();
  }
  static void push_group(state& made, scan::group_at<0>, char value) {
    made.text.push_back(value);
  }
  static void closed_group(state& made, scan::group_at<0>) {
    if (made.where != nullptr) made.where->say("name=" + made.text);
  }

  static tagged finish_groups(state made) {
    return tagged{std::move(made.text), made.mark};
  }
};

template <>
struct scan::scanner<numbers> {
  struct state {
    std::vector<int> values;
    int running = 0;
    int mark = 0;
    const room* where = nullptr;
  };

  // The first number, and every one after a comma. The second group is inside
  // a repetition, so it opens and closes once a turn -- and a turn that opens
  // on a comma the walk cannot get past is a turn that has to be undone.
  static constexpr std::string_view pattern() {
    return "([0-9]+)(?:,([0-9]+))*";
  }

  static state begin_groups() { return state{}; }
  static state begin_groups(const room& where) {
    where.say("fold begins");
    return state{{}, 0, where.mark, &where};
  }

  static void opened_group(state& made, scan::group_at<0>) { made.running = 0; }
  static void opened_group(state& made, scan::group_at<1>) {
    made.running = 0;
    if (made.where != nullptr) made.where->say("turn opens");
  }
  static void push_group(state& made, scan::group_at<0>, char value) {
    made.running = made.running * 10 + (value - '0');
  }
  static void push_group(state& made, scan::group_at<1>, char value) {
    made.running = made.running * 10 + (value - '0');
  }
  static void closed_group(state& made, scan::group_at<0>) {
    made.values.push_back(made.running);
  }
  static void closed_group(state& made, scan::group_at<1>) {
    made.values.push_back(made.running);
    if (made.where != nullptr) {
      made.where->say("turn closes " + std::to_string(made.running));
    }
  }

  static numbers finish_groups(state made) {
    return numbers{std::move(made.values), made.mark};
  }
};

namespace {

class every_record_told : public ::testing::Test {
 protected:
  std::vector<std::string> said;
  room fast{2, &said};
};

// One record after another off the same reading, every one of them told the
// same thing.
//
// Told as the contexts stand and not in braces: this reading is a view, and it
// reads a record after the call that named its output has ended. A context is
// held as the address of it, so the only contexts it can be given are ones the
// caller is still holding -- which is what `contexts_at_places` asks for, and
// a temporary is refused where it would have been read after it died.
TEST_F(every_record_told, EveryRecordIsToldTheSameThing) {
  std::size_t taken = 0;
  counted_reading source("for abc 1,2 for def 3,4 ", &taken);
  int records = 0;
  std::vector<std::string> names;
  std::vector<int> marks;
  for (const row& one :
       scan::each<"{{foreach}|{for}|{each}} {} {} ">(std::move(source))
           .of<row>(fast)) {
    names.push_back(one.head.name.text);
    marks.push_back(one.tail.mark);
    ++records;
    if (records > 4) break;
  }
  EXPECT_EQ(records, 2);
  EXPECT_EQ(names, std::vector<std::string>({"abc", "def"}));
  EXPECT_EQ(marks, std::vector<int>({2, 2}));
}

// And the parts of a place, said to a reading that is lazy.
//
// Braces cannot reach here: the readings a braced list makes are default
// arguments of the call that named the output, and this reading reads a record
// after that call has ended. `scan::parts` is deduced and held by value, so
// there is nothing waiting to die.
TEST_F(every_record_told, ThePartsOfAPlaceToldToEveryRecord) {
  std::size_t taken = 0;
  counted_reading source("for abc 1,2 for def 3,4 ", &taken);
  int records = 0;
  std::vector<int> marks;
  for (const row& one :
       scan::each<"{{foreach}|{for}|{each}} {} {} ">(std::move(source))
           .of<row>(scan::parts{fast, fast}, fast)) {
    marks.push_back(one.tail.mark);
    ++records;
    if (records > 4) break;
  }
  EXPECT_EQ(records, 2);
  EXPECT_EQ(marks, std::vector<int>({2, 2}));
}

// A context made at the call, told to a reading that reads a record long
// after that call has ended.
//
// Held by the carrier rather than pointed at: a context of the caller's own is
// the caller's own and is reached through its address, and one made here is
// nobody else's, so it is moved in and kept. There is nothing left for it to
// outlive.
TEST_F(every_record_told, AContextMadeAtTheCallIsKeptByTheReading) {
  std::size_t taken = 0;
  counted_reading source("for abc 1,2 for def 3,4 ", &taken);
  int records = 0;
  std::vector<int> marks;
  for (const row& one :
       scan::each<"{{foreach}|{for}|{each}} {} {} ">(std::move(source))
           .of<row>(room{7, &said})) {
    marks.push_back(one.tail.mark);
    ++records;
    if (records > 4) break;
  }
  EXPECT_EQ(records, 2);
  EXPECT_EQ(marks, std::vector<int>({7, 7}));
}

}  // namespace
