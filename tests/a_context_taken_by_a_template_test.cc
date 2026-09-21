// A scanner whose hook is a template on the context type.
//
// A context is a type of the caller's, so a scanner that answers more than one
// of them would otherwise write an overload each. The hook may be a template
// instead, constrained by what it uses of the context -- and constrained is
// what keeps the library's probes honest, because a hook that claimed to take
// anything would answer every question the library asks about whether a
// context reaches this place at all.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

// Two contexts with nothing in common and nothing inherited.
struct room {
  int mark = 0;
};

struct depot {
  int weight = 0;
};

// A context the caller keeps hold of, and whose name says whether it was moved
// out of.
struct ledger {
  std::string name;
  int mark = 0;
};

struct tagged {
  std::string_view text;
  int mark = 0;
};

struct pair {
  tagged left;
  tagged right;
};

}  // namespace

template <>
struct scan::scanner<tagged> {
  static constexpr std::string_view pattern() { return "[a-z]+"; }

  static constexpr tagged parse(std::string_view text) {
    return tagged{text, 0};
  }

  // One hook for every context that has a mark, and another for every context
  // that has a weight. Neither says anything about a type that has neither.
  template <class Told>
    requires requires(const Told& one) {
      { one.mark } -> std::convertible_to<int>;
    }
  static constexpr tagged parse(std::string_view text, const Told& told) {
    return tagged{text, told.mark};
  }

  template <class Told>
    requires requires(const Told& one) {
      { one.weight } -> std::convertible_to<int>;
    }
  static constexpr tagged parse(std::string_view text, const Told& told) {
    return tagged{text, told.weight * 100};
  }
};

namespace {

TEST(AContextTakenByATemplate, OneHookAnswersTwoUnrelatedContexts) {
  const room here{7};
  const depot there{3};
  const pair got = scan::scan<"{} {}">("abc def"sv).of<pair>(here, there);
  EXPECT_EQ(got.left.mark, 7);
  EXPECT_EQ(got.right.mark, 300);
}

TEST(AContextTakenByATemplate, OneIsEverybodys) {
  const room here{7};
  const pair got = scan::scan<"{} {}">("abc def"sv).of<pair>(here);
  EXPECT_EQ(got.left.mark, 7);
  EXPECT_EQ(got.right.mark, 7);
}

TEST(AContextTakenByATemplate, WithNoneItIsReadAsItAlwaysWas) {
  const pair got = scan::scan<"{} {}">("abc def"sv).of<pair>();
  EXPECT_EQ(got.left.mark, 0);
  EXPECT_EQ(got.right.mark, 0);
}

TEST(AContextTakenByATemplate, AndWhereOnePlaceWantsNone) {
  const depot there{3};
  const pair got =
      scan::scan<"{} {}">("abc def"sv).of<pair>(there, scan::default_context);
  EXPECT_EQ(got.left.mark, 300);
  EXPECT_EQ(got.right.mark, 0);
}

TEST(AContextTakenByATemplate, InBracesToo) {
  const room here{7};
  const depot there{3};
  const pair got = scan::scan<"{} {}">("abc def"sv).of<pair>({here, there});
  EXPECT_EQ(got.left.mark, 7);
  EXPECT_EQ(got.right.mark, 300);
}

TEST(AContextTakenByATemplate, AndBeforeTheTypeIsNamed) {
  const depot there{3};
  const pair got = scan::scan<"{} {}">("abc def"sv).with(there);
  EXPECT_EQ(got.left.mark, 300);
  EXPECT_EQ(got.right.mark, 300);
}

// `with` used to strip the reference off every context, so what the caller
// still owned was moved out of -- and a const one did not compile at all.
TEST(AContextTakenByATemplate, TheCallerKeepsWhatItOwns) {
  ledger mine{"kept", 5};
  const pair got = scan::scan<"{} {}">("abc def"sv).with(mine);
  EXPECT_EQ(got.left.mark, 5);
  EXPECT_EQ(mine.name, "kept");
}

TEST(AContextTakenByATemplate, AndOneMadeAtTheCallIsHeldRatherThanPointedAt) {
  const pair got = scan::scan<"{} {}">("abc def"sv).with(depot{3});
  EXPECT_EQ(got.left.mark, 300);
  EXPECT_EQ(got.right.mark, 300);
}

TEST(AContextTakenByATemplate, WhileTheProgramIsCompiled) {
  constexpr room here{7};
  constexpr depot there{3};
  constexpr pair got = scan::scan<"{} {}">("abc def"sv).of<pair>(here, there);
  static_assert(got.left.mark == 7);
  static_assert(got.right.mark == 300);
  EXPECT_EQ(got.left.mark, 7);
}

}  // namespace
