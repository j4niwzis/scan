// One reading, every kind of subject, told and untold.
//
// The same format and the same output type go four ways inside: a subject in a
// row read by pointing at it, one in a row whose reading holds a list and so is
// gathered turn by turn, one that arrives a character at a time, and one handed
// over in pieces. They are meant to be the same reading, and what makes them
// the same is nobody writing any of them twice.
//
// This is the shape the rest of the suite is worth having in: a table, not a
// case. Both of the faults this file was written after were the same fault --
// one branch doing what the branch beside it forgot -- and neither could have
// survived a table.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

constexpr int number(std::string_view text) {
  int made = 0;
  for (char letter : text) made = made * 10 + (letter - '0');
  return made;
}

struct room {
  int mark = 0;
};

struct counted {
  int value = 0;
};

struct row {
  std::vector<counted> values;
};

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

}  // namespace

// Read whole where the subject can be pointed at, and told its groups as they
// arrive where it cannot -- so one element type suits every subject here.
template <>
struct scan::scanner<counted> {
  static constexpr std::string_view pattern() { return "[0-9]+"; }

  struct state {
    int running = 0;
    int mark = 0;
  };
  static constexpr state begin() { return {}; }
  static constexpr state begin(std::string_view) { return {}; }
  static constexpr state begin(std::string_view, const room& where) {
    return state{0, where.mark};
  }
  static constexpr void push(state& made, char value) {
    made.running = made.running * 10 + (value - '0');
  }
  static constexpr counted finish(state made) {
    return counted{made.running + made.mark};
  }

  static constexpr counted parse(std::string_view text) {
    return counted{number(text)};
  }
  static constexpr counted parse(std::string_view text, const room& where) {
    return counted{number(text) + where.mark};
  }
};

// A place told its groups as they arrive, rather than read whole: the other way
// a reading holds turns, and the other walk it takes.
namespace {
struct numbers {
  std::vector<int> values;
  int mark = 0;
};
struct folded {
  numbers list;
};
}  // namespace

template <>
struct scan::scanner<numbers> {
  struct state {
    std::vector<int> values;
    int running = 0;
    int mark = 0;
  };
  static constexpr std::string_view pattern() {
    return "([0-9]+)(?:,([0-9]+))*";
  }
  static constexpr state begin_groups() { return state{}; }
  static constexpr state begin_groups(const room& where) {
    return state{{}, 0, where.mark};
  }
  static constexpr void opened_group(state& made, scan::group_at<0>) {
    made.running = 0;
  }
  static constexpr void opened_group(state& made, scan::group_at<1>) {
    made.running = 0;
  }
  static constexpr void push_group(state& made, scan::group_at<0>, char value) {
    made.running = made.running * 10 + (value - '0');
  }
  static constexpr void push_group(state& made, scan::group_at<1>, char value) {
    made.running = made.running * 10 + (value - '0');
  }
  static constexpr void closed_group(state& made, scan::group_at<0>) {
    made.values.push_back(made.running);
  }
  static constexpr void closed_group(state& made, scan::group_at<1>) {
    made.values.push_back(made.running);
  }
  static constexpr numbers finish_groups(state made) {
    return numbers{std::move(made.values), made.mark};
  }
};

// One of several: exactly one branch runs, and which one is not known where the
// call is written -- so a context for each branch has to reach the one that did,
// whichever road the subject took.
namespace {
struct dashed {
  int letters = 0;
  int mark = 0;
};
struct picked {
  std::variant<counted, dashed> pick;
};
}  // namespace

template <>
struct scan::scanner<dashed> {
  static constexpr std::string_view pattern() { return "[a-z]+"; }

  struct state {
    int letters = 0;
    int mark = 0;
  };
  static constexpr state begin() { return {}; }
  static constexpr state begin(std::string_view) { return {}; }
  static constexpr state begin(std::string_view, const room& where) {
    return state{0, where.mark};
  }
  static constexpr void push(state& made, char) { ++made.letters; }
  static constexpr dashed finish(state made) {
    return dashed{made.letters, made.mark};
  }

  static constexpr dashed parse(std::string_view text) {
    return dashed{static_cast<int>(text.size()), 0};
  }
  static constexpr dashed parse(std::string_view text, const room& where) {
    return dashed{static_cast<int>(text.size()), where.mark};
  }
};

// A list whose element is a shape of its own, read by its own scanner. Two
// things are wrong here and both are written down rather than worked around:
// the first turn never reaches the list, and what the list's place was told
// does not reach the element's places. These tests fail, and are here because a
// fault nobody has written down is a fault that comes back.
namespace {
struct both {
  counted number;
  dashed word;
};
struct rows {
  std::vector<both> items;
};
}  // namespace

template <>
struct scan::scanner<both> : scan::aggregate_scanner<"{}:{}"> {};

namespace {

// A shape whose places are themselves shapes, read by their own scanner: what
// a place is told is told to its parts, and a braced list goes as deep as the
// shape does.
struct deep {
  both left;
  both right;
};

// Room said in advance -- a field that holds characters with no allocator
// anywhere, and says so where they did not fit.
struct named {
  scan::held<8> name;
  counted value;
};

// And the other way a field holds characters: a string built with the resource
// its place was told about.
struct kept {
  std::pmr::string name;
  counted value;
};

// A list built with that resource too, which is the same question asked of a
// container rather than of a leaf.
struct pooled {
  std::pmr::vector<int> values;
};

constexpr auto subject = "1,2,3"sv;
constexpr auto pairs = "1:ab 2:cd 3:ef"sv;
constexpr auto digits = "42"sv;
constexpr auto letters = "abc"sv;
constexpr auto nested = "1:ab 2:cd"sv;
constexpr auto worded = "abc 42"sv;
constexpr auto wider = "abcdefghij 42"sv;
constexpr auto marked = "42-abc"sv;

void expect_read(const row& got, int base) {
  ASSERT_EQ(got.values.size(), 3u);
  EXPECT_EQ(got.values[0].value, base + 1);
  EXPECT_EQ(got.values[1].value, base + 2);
  EXPECT_EQ(got.values[2].value, base + 3);
}

class every_subject : public ::testing::Test {
 protected:
  room fast{10};
  std::size_t at = 0;
};

TEST_F(every_subject, InARowAndTold) {
  expect_read(scan::scan<"{{}{*,?}}">(subject).of<row>(fast), 10);
}

TEST_F(every_subject, ReadOnceAndTold) {
  expect_read(scan::scan<"{{}{*,?}}">(read_once(subject, &at)).of<row>(fast), 10);
}

TEST_F(every_subject, InPiecesAndTold) {
  expect_read(scan::scan<"{{}{*,?}}">(read_once(subject, &at) | scan::in_pieces<2>)
                  .of<row>(fast),
              10);
}

TEST_F(every_subject, InARowAndToldNothing) {
  expect_read(scan::scan<"{{}{*,?}}">(subject).of<row>(), 0);
}

TEST_F(every_subject, ReadOnceAndToldNothing) {
  expect_read(scan::scan<"{{}{*,?}}">(read_once(subject, &at)).of<row>(), 0);
}

TEST_F(every_subject, InPiecesAndToldNothing) {
  expect_read(scan::scan<"{{}{*,?}}">(read_once(subject, &at) | scan::in_pieces<2>)
                  .of<row>(),
              0);
}

TEST_F(every_subject, AFoldInARowIsTold) {
  EXPECT_EQ(scan::scan<"{}">(subject).of<folded>(fast).list.mark, 10);
}

TEST_F(every_subject, AFoldReadOnceIsTold) {
  EXPECT_EQ(scan::scan<"{}">(read_once(subject, &at)).of<folded>(fast).list.mark,
            10);
}

TEST_F(every_subject, AFoldInPiecesIsTold) {
  EXPECT_EQ(scan::scan<"{}">(read_once(subject, &at) | scan::in_pieces<2>)
                .of<folded>(fast)
                .list.mark,
            10);
}

TEST_F(every_subject, EveryTurnOfAListOfShapesArrives) {
  const auto got = scan::scan<"{{}{* ?}}+">(pairs).of<rows>();
  ASSERT_EQ(got.items.size(), 3u);
  EXPECT_EQ(got.items[0].number.value, 1);
  EXPECT_EQ(got.items[1].number.value, 2);
  EXPECT_EQ(got.items[2].number.value, 3);
}

TEST_F(every_subject, AListOfShapesTellsItsElementsWhatItWasTold) {
  const auto got = scan::scan<"{{}{* ?}}+">(pairs).of<rows>(fast);
  ASSERT_EQ(got.items.size(), 3u);
  EXPECT_EQ(got.items[0].number.value, 11);
  EXPECT_EQ(got.items[0].word.mark, 10);
  EXPECT_EQ(got.items[2].word.mark, 10);
}

TEST_F(every_subject, TheBranchThatRanIsTold) {
  EXPECT_EQ(std::get<0>(scan::scan<"{}">(digits).of<picked>(fast).pick).value, 52);
  EXPECT_EQ(std::get<1>(scan::scan<"{}">(letters).of<picked>(fast).pick).mark, 10);
}

TEST_F(every_subject, TheBranchThatRanIsToldOffAStream) {
  EXPECT_EQ(
      std::get<0>(scan::scan<"{}">(read_once(digits, &at)).of<picked>(fast).pick)
          .value,
      52);
  std::size_t pieces = 0;
  EXPECT_EQ(std::get<1>(scan::scan<"{}">(read_once(letters, &pieces) |
                                         scan::in_pieces<2>)
                            .of<picked>(fast)
                            .pick)
                .mark,
            10);
}

// A context per branch, said in braces -- and the branch that runs is told its
// own, on a subject in a row and on one that arrives a character at a time.
TEST_F(every_subject, EachBranchIsToldItsOwnWhereverItIsRead) {
  room slow{2};
  EXPECT_EQ(
      std::get<0>(scan::scan<"{}">(digits).of<picked>({{fast, slow}}).pick).value,
      52);
  EXPECT_EQ(
      std::get<1>(scan::scan<"{}">(letters).of<picked>({{fast, slow}}).pick).mark,
      2);
  EXPECT_EQ(std::get<1>(scan::scan<"{}">(read_once(letters, &at))
                            .of<picked>({{fast, slow}})
                            .pick)
                .mark,
            2);
}

TEST_F(every_subject, AFoldToldNothingIsToldNothing) {
  EXPECT_EQ(scan::scan<"{}">(subject).of<folded>().list.mark, 0);
  std::size_t once = 0;
  EXPECT_EQ(scan::scan<"{}">(read_once(subject, &once)).of<folded>().list.mark, 0);
  std::size_t pieces = 0;
  EXPECT_EQ(scan::scan<"{}">(read_once(subject, &pieces) | scan::in_pieces<2>)
                .of<folded>()
                .list.mark,
            0);
}

// A shape of shapes, every way a subject can arrive.
TEST_F(every_subject, AShapeOfShapesIsReadEveryWay) {
  const auto in_a_row = scan::scan<"{} {}">(nested).of<deep>();
  EXPECT_EQ(in_a_row.left.number.value, 1);
  EXPECT_EQ(in_a_row.left.word.letters, 2);
  EXPECT_EQ(in_a_row.right.number.value, 2);
  EXPECT_EQ(in_a_row.right.word.letters, 2);

  const auto once = scan::scan<"{} {}">(read_once(nested, &at)).of<deep>();
  EXPECT_EQ(once.left.number.value, 1);
  EXPECT_EQ(once.right.number.value, 2);
  EXPECT_EQ(once.right.word.letters, 2);

  std::size_t pieces = 0;
  const auto in_pieces =
      scan::scan<"{} {}">(read_once(nested, &pieces) | scan::in_pieces<2>)
          .of<deep>();
  EXPECT_EQ(in_pieces.left.number.value, 1);
  EXPECT_EQ(in_pieces.right.word.letters, 2);
}

// One context is everybody's, and everybody here is two places down.
TEST_F(every_subject, AShapeOfShapesTellsItsPartsWhatItWasTold) {
  const auto in_a_row = scan::scan<"{} {}">(nested).of<deep>(fast);
  EXPECT_EQ(in_a_row.left.number.value, 11);
  EXPECT_EQ(in_a_row.left.word.mark, 10);
  EXPECT_EQ(in_a_row.right.number.value, 12);
  EXPECT_EQ(in_a_row.right.word.mark, 10);

  const auto once = scan::scan<"{} {}">(read_once(nested, &at)).of<deep>(fast);
  EXPECT_EQ(once.left.word.mark, 10);
  EXPECT_EQ(once.right.number.value, 12);

  std::size_t pieces = 0;
  const auto in_pieces =
      scan::scan<"{} {}">(read_once(nested, &pieces) | scan::in_pieces<2>)
          .of<deep>(fast);
  EXPECT_EQ(in_pieces.right.word.mark, 10);
}

// One context a place, where every place is a shape: what a place is told is
// told to all of it, however deep the shape goes.
TEST_F(every_subject, EachPlaceOfAShapeOfShapesHasItsOwn) {
  room slow{2};
  const auto got = scan::scan<"{} {}">(nested).of<deep>(fast, slow);
  EXPECT_EQ(got.left.number.value, 11);
  EXPECT_EQ(got.left.word.mark, 10);
  EXPECT_EQ(got.right.number.value, 4);
  EXPECT_EQ(got.right.word.mark, 2);
}

// Room said in advance is filled the same way off every subject.
TEST_F(every_subject, RoomSaidInAdvanceIsFilledEveryWay) {
  const auto in_a_row = scan::scan<"{[a-z]+} {}">(worded).of<named>();
  EXPECT_EQ(in_a_row.name.view(), "abc");
  EXPECT_FALSE(in_a_row.name.overflowed);
  EXPECT_EQ(in_a_row.value.value, 42);

  const auto once = scan::scan<"{[a-z]+} {}">(read_once(worded, &at)).of<named>();
  EXPECT_EQ(once.name.view(), "abc");
  EXPECT_EQ(once.value.value, 42);

  std::size_t pieces = 0;
  const auto in_pieces =
      scan::scan<"{[a-z]+} {}">(read_once(worded, &pieces) | scan::in_pieces<2>)
          .of<named>();
  EXPECT_EQ(in_pieces.name.view(), "abc");
  EXPECT_EQ(in_pieces.value.value, 42);

  // Told one context, and a scanner that takes none is read as it always was.
  const auto told = scan::scan<"{[a-z]+} {}">(worded).of<named>(fast);
  EXPECT_EQ(told.name.view(), "abc");
  EXPECT_EQ(told.value.value, 52);
}

// What did not fit is dropped and said, rather than allocated for.
TEST_F(every_subject, WhatDoesNotFitSaysSo) {
  const auto in_a_row = scan::scan<"{[a-z]+} {}">(wider).of<named>();
  EXPECT_TRUE(in_a_row.name.overflowed);
  EXPECT_EQ(in_a_row.name.view().size(), 8u);
  EXPECT_EQ(in_a_row.value.value, 42);

  const auto once = scan::scan<"{[a-z]+} {}">(read_once(wider, &at)).of<named>();
  EXPECT_TRUE(once.name.overflowed);
  EXPECT_EQ(once.name.view().size(), 8u);
  EXPECT_EQ(once.value.value, 42);
}

// A context that keeps memory is what the value is built with, wherever the
// characters came from -- and the place beside it, whose scanner knows nothing
// about allocators, is read as it always was.
TEST_F(every_subject, AStringKeepsTheResourceItsPlaceWasTold) {
  std::pmr::monotonic_buffer_resource bytes;
  const std::pmr::polymorphic_allocator<> mine(&bytes);

  const auto in_a_row = scan::scan<"{[a-z]+} {}">(worded).of<kept>(mine);
  EXPECT_EQ(in_a_row.name, "abc");
  EXPECT_EQ(in_a_row.name.get_allocator().resource(), &bytes);
  EXPECT_EQ(in_a_row.value.value, 42);

  const auto once = scan::scan<"{[a-z]+} {}">(read_once(worded, &at)).of<kept>(mine);
  EXPECT_EQ(once.name, "abc");
  EXPECT_EQ(once.name.get_allocator().resource(), &bytes);

  std::size_t pieces = 0;
  const auto in_pieces =
      scan::scan<"{[a-z]+} {}">(read_once(worded, &pieces) | scan::in_pieces<2>)
          .of<kept>(mine);
  EXPECT_EQ(in_pieces.name, "abc");
  EXPECT_EQ(in_pieces.name.get_allocator().resource(), &bytes);
}

TEST_F(every_subject, AListIsBuiltWithTheResourceItWasTold) {
  std::pmr::monotonic_buffer_resource bytes;
  const std::pmr::polymorphic_allocator<> mine(&bytes);

  const auto in_a_row = scan::scan<"{{}{*,?}}">(subject).of<pooled>(mine);
  ASSERT_EQ(in_a_row.values.size(), 3u);
  EXPECT_EQ(in_a_row.values[0], 1);
  EXPECT_EQ(in_a_row.values[2], 3);
  EXPECT_EQ(in_a_row.values.get_allocator().resource(), &bytes);

  const auto once = scan::scan<"{{}{*,?}}">(read_once(subject, &at)).of<pooled>(mine);
  ASSERT_EQ(once.values.size(), 3u);
  EXPECT_EQ(once.values[1], 2);
  EXPECT_EQ(once.values.get_allocator().resource(), &bytes);
}

// The other layer, on the same three subjects: a collector a group, and what
// they make differs only where the subject gives them no choice.
TEST_F(every_subject, CollectorsAgreeWhereverTheyAreRead) {
  constexpr auto reading = scan::match<"([0-9]+)-([a-z]+)">.into(
      scan::as<int>(), scan::text());

  const auto in_a_row = reading(marked);
  ASSERT_TRUE(in_a_row);
  EXPECT_EQ(in_a_row.get<1>(), 42);
  EXPECT_EQ(in_a_row.get<2>(), "abc");

  const auto once = reading(read_once(marked, &at));
  ASSERT_TRUE(once);
  EXPECT_EQ(once.get<1>(), 42);
  EXPECT_EQ(once.get<2>(), "abc");

  std::size_t pieces = 0;
  const auto in_pieces = reading(read_once(marked, &pieces) | scan::in_pieces<2>);
  ASSERT_TRUE(in_pieces);
  EXPECT_EQ(in_pieces.get<1>(), 42);
  EXPECT_EQ(in_pieces.get<2>(), "abc");
}

TEST_F(every_subject, AGroupNobodyWantedAndOneFoldedByHand) {
  constexpr auto reading = scan::match<"([0-9]+)-([a-z]+)">.into(
      scan::skip(), scan::collecting<std::size_t>(
                        [](std::size_t& sum, char letter) {
                          sum += static_cast<unsigned char>(letter);
                        }));
  constexpr std::size_t letters_of_abc = 'a' + 'b' + 'c';

  const auto in_a_row = reading(marked);
  ASSERT_TRUE(in_a_row);
  static_assert(std::same_as<std::remove_cvref_t<decltype(in_a_row.get<1>())>,
                             scan::skipped>);
  EXPECT_EQ(in_a_row.get<2>(), letters_of_abc);

  const auto once = reading(read_once(marked, &at));
  ASSERT_TRUE(once);
  EXPECT_EQ(once.get<2>(), letters_of_abc);

  std::size_t pieces = 0;
  const auto in_pieces = reading(read_once(marked, &pieces) | scan::in_pieces<2>);
  ASSERT_TRUE(in_pieces);
  EXPECT_EQ(in_pieces.get<2>(), letters_of_abc);
}

}  // namespace
