// A place in a format standing for a type that reads its own groups.
//
// A type may say a pattern with groups in it and be built from those groups
// rather than from the text it stands on. That was true of the pattern layer
// already; this is the same thing one layer up, where the place is written in
// a format. The groups the type wrote are groups of this match like any others
// -- they take the numbers straight after the place itself -- so the machine
// finds them on its way past and the type is handed exactly its own.
//
// It holds both ways round: where the subject can be pointed at the groups are
// pieces of it, and where the subject is read once each group is gathered as
// it arrives and handed over at the end. Neither reads anything twice.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

struct version {
  int major = 0;
  int minor = 0;
  int patch = 0;
};

// A type that is handed its groups all at once, and says so.
struct stamp {
  int hour = 0;
  int minute = 0;
};

// What the plain reading would do, so a test can say it was not done.
inline int times_read_as_text = 0;

[[nodiscard]] constexpr int number(std::string_view text) {
  int made = 0;
  for (char letter : text) made = made * 10 + (letter - '0');
  return made;
}

}  // namespace

template <>
struct scan::scanner<version> {
  static constexpr std::string_view pattern() {
    return "([0-9]+)\\.([0-9]+)\\.([0-9]+)";
  }

  static constexpr version from_groups(
      std::span<const std::string_view> groups) {
    return version{number(groups[0]), number(groups[1]), number(groups[2])};
  }

  static version parse(std::string_view text) {
    ++times_read_as_text;
    return version{};
  }
};

// The same thing said the other way: a state, the characters of each group as
// they arrive, and the answer at the end. This is the way a subject that is
// read once is read, and it is written out here to show it is a way of writing
// a type and not a thing the library does behind one.
template <>
struct scan::scanner<stamp> {
  struct state {
    int at[2]{};
    std::size_t which = 0;
  };

  static constexpr std::string_view pattern() { return "([0-9]+):([0-9]+)"; }

  static constexpr state begin_groups() { return state{}; }

  static constexpr void push_group(state& made, std::size_t group, char value) {
    made.at[group] = made.at[group] * 10 + (value - '0');
  }

  static constexpr stamp finish_groups(state made) {
    return stamp{made.at[0], made.at[1]};
  }
};

namespace {

struct release {
  version number;
  scan::held<16> name;
};

struct entry {
  stamp when;
  scan::held<16> what;
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

TEST(ALeafThatReadsItsGroupsInAFormat, HandedThemOffASubjectPointedAt) {
  times_read_as_text = 0;
  const std::string text = "1.22.333 stable";
  const release one = scan::scan<"{} {[a-z]+}">(text).of<release>();
  EXPECT_EQ(one.number.major, 1);
  EXPECT_EQ(one.number.minor, 22);
  EXPECT_EQ(one.number.patch, 333);
  EXPECT_EQ(one.name.view(), "stable");
  EXPECT_EQ(times_read_as_text, 0);
}

TEST(ALeafThatReadsItsGroupsInAFormat, AndTheFieldAfterItReadsItsOwn) {
  // The groups the type opened take numbers of their own, and what stands
  // after the place has to go on counting from there.
  times_read_as_text = 0;
  const std::string text = "1.2.3 4.5.6";
  struct pair {
    version left;
    version right;
  };
  const pair both = scan::scan<"{} {}">(text).of<pair>();
  EXPECT_EQ(both.left.patch, 3);
  EXPECT_EQ(both.right.major, 4);
  EXPECT_EQ(both.right.patch, 6);
  EXPECT_EQ(times_read_as_text, 0);
}

TEST(ALeafThatReadsItsGroupsInAFormat, GatheredOffASubjectReadOnce) {
  const std::string text = "1.22.333 stable";
  std::size_t at = 0;
  const release one =
      scan::scan<"{} {[a-z]+}">(read_once(text, &at)).of<release>();
  EXPECT_EQ(one.number.major, 1);
  EXPECT_EQ(one.number.minor, 22);
  EXPECT_EQ(one.number.patch, 333);
  EXPECT_EQ(one.name.view(), "stable");
}

TEST(ALeafThatReadsItsGroupsInAFormat, ACharacterAtATimeIntoItsGroups) {
  const std::string text = "09:30 standup";
  const entry one = scan::scan<"{} {[a-z]+}">(text).of<entry>();
  EXPECT_EQ(one.when.hour, 9);
  EXPECT_EQ(one.when.minute, 30);
  EXPECT_EQ(one.what.view(), "standup");
}

TEST(ALeafThatReadsItsGroupsInAFormat, TheSameOffAStream) {
  const std::string text = "09:30 standup";
  std::size_t at = 0;
  const entry one = scan::scan<"{} {[a-z]+}">(read_once(text, &at)).of<entry>();
  EXPECT_EQ(one.when.hour, 9);
  EXPECT_EQ(one.when.minute, 30);
  EXPECT_EQ(one.what.view(), "standup");
}

TEST(ALeafThatReadsItsGroupsInAFormat, OneRecordAfterAnother) {
  const std::string text = "1.2.3 alpha\n4.5.6 beta\n";
  std::size_t at = 0;
  std::vector<int> majors;
  for (const release& one :
       scan::each<"{} {[a-z]+}\n">(read_once(text, &at)).of<release>()) {
    majors.push_back(one.number.major);
  }
  EXPECT_EQ(majors, std::vector<int>({1, 4}));
}

}  // namespace
