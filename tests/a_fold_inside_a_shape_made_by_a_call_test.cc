// A fold standing in a shape that is made by a call.
//
// Such a shape used to be handed its groups once the match was over, and a
// fold cannot be told its turns that way: a group that repeats keeps only its
// last turn, so a turn in the middle was lost, and a fold that took no turns
// left groups that took no part, which the shape took for a failure. A leaf
// that folds now counts as a list, and the shape is told its groups as they
// happen.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

// Every element's text, as the fold is told it.
struct pieces {
  std::vector<std::string> texts;
  int closed = 0;
};

// Numbers in brackets: none or some.
struct numbers {
  std::vector<int> values;
};

// A fold as a place of a shape that is built by parse.
struct member {
  numbers list;
  int n = 0;
};

struct grid {
  pieces rows;
  int n = 0;
};

template <class Type>
struct holder {
  Type value;
};

}  // namespace

template <>
struct scan::scanner<numbers> {
  struct state {
    std::vector<int> values;
    int running = 0;
  };
  static constexpr std::string_view pattern() {
    return R"(\[(?:([0-9]+)(?:,([0-9]+))*)?\])";
  }
  static constexpr state begin_groups() { return state{}; }
  static constexpr void opened_group(state& made, std::size_t) {
    made.running = 0;
  }
  static constexpr void push_group(state& made, std::size_t, char letter) {
    made.running = made.running * 10 + (letter - '0');
  }
  static constexpr void closed_group(state& made, std::size_t) {
    made.values.push_back(made.running);
  }
  static constexpr numbers finish_groups(state made) {
    return numbers{std::move(made.values)};
  }
};

template <>
struct scan::scanner<pieces> {
  struct state {
    std::vector<std::string> texts;
    int closed = 0;
  };
  static constexpr std::string_view pattern() {
    return R"(\[(?:(\[(?:[0-9]+(?:,[0-9]+)*)?\]))"
           R"((?:,(\[(?:[0-9]+(?:,[0-9]+)*)?\]))*)?\])";
  }
  static constexpr state begin_groups() { return state{}; }
  static constexpr void opened_group(state& made, std::size_t) {
    made.texts.emplace_back();
  }
  static constexpr void push_group(state& made, std::size_t, char letter) {
    made.texts.back() += letter;
  }
  static constexpr void closed_group(state& made, std::size_t) {
    ++made.closed;
  }
  static constexpr pieces finish_groups(state made) {
    return pieces{std::move(made.texts), made.closed};
  }
};

template <>
struct scan::scanner<member>
    : scan::aggregate_scanner<R"(\{"list":{},"n":{}\})"> {
  static constexpr member parse(numbers list, int n) {
    return member{std::move(list), n};
  }
};

template <>
struct scan::scanner<grid>
    : scan::aggregate_scanner<R"(\{"n":{},"rows":{}\})"> {
  static constexpr grid parse(int n, pieces rows) {
    return grid{std::move(rows), n};
  }
};

namespace {

template <class Type>
auto read(std::string_view text) {
  return scan::scan<"{}">(text).try_of<holder<Type>>();
}

TEST(ScanEmptyList, NoTurnsAtTheTop) {
  const auto got = read<numbers>("[]");
  ASSERT_TRUE(got) << scan::what(got.error());
  EXPECT_TRUE(got->value.values.empty());
}

TEST(ScanEmptyList, NoTurnsAsAPlaceOfAShapeBuiltByParse) {
  const auto got = read<member>(R"({"list":[],"n":4})");
  ASSERT_TRUE(got) << scan::what(got.error());
  EXPECT_TRUE(got->value.list.values.empty());
  EXPECT_EQ(got->value.n, 4);

  const auto some = read<member>(R"({"list":[1,2],"n":4})");
  ASSERT_TRUE(some) << scan::what(some.error());
  EXPECT_EQ(some->value.list.values, (std::vector<int>{1, 2}));
}

TEST(ScanEmptyList, TurnsThatAreBracketsOneOfThemEmpty) {
  const auto got = read<pieces>("[[1,2],[],[3]]");
  ASSERT_TRUE(got) << scan::what(got.error());
  EXPECT_EQ(got->value.texts,
            (std::vector<std::string>{"[1,2]", "[]", "[3]"}));
  EXPECT_EQ(got->value.closed, 3);
}

TEST(ScanEmptyList, AnEmptyTurnFirstAndLast) {
  const auto got = read<pieces>("[[],[]]");
  ASSERT_TRUE(got) << scan::what(got.error());
  EXPECT_EQ(got->value.texts, (std::vector<std::string>{"[]", "[]"}));
  EXPECT_EQ(got->value.closed, 2);
}

TEST(ScanEmptyList, BracketedTurnsAsAPlaceOfAShapeBuiltByParse) {
  const auto got = read<grid>(R"({"n":4,"rows":[[1,2],[],[3]]})");
  ASSERT_TRUE(got) << scan::what(got.error());
  EXPECT_EQ(got->value.rows.texts,
            (std::vector<std::string>{"[1,2]", "[]", "[3]"}));
  EXPECT_EQ(got->value.rows.closed, 3);
}

}  // namespace
