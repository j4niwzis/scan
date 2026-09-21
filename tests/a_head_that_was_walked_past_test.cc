// What a context sees when the walk goes past a match and comes back.
//
// A head is where the fallback lives: the machine walks past a match on the
// chance of a longer one the order prefers, and where it dies the answer is the
// place it passed. A context is the caller's own and is not copied where a
// reading divides, so what a scanner writes into one stays written -- which
// raises the question this file answers: is a context told by the readings that
// died as well?
//
// Where the characters can be pointed at, no: a place is read whole once the
// match is over, and only the reading that won is read from. The walk here does
// go past a match -- the value proves it, `a` where the subject begins `abx` --
// and the context hears one telling per place.
//
// The four things at once: hooks of the caller's own, a context written into by
// those hooks, the walk going past a match, and the contexts said in braces --
// one per place.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

// What the scanner writes into. Nothing here is the library's: it is a thing of
// the caller's that the reading has never heard of.
struct ledger {
  int begun = 0;
  int finished = 0;
  std::string seen;
};

// A leaf read a character at a time, by hooks of our own.
//
// Its pattern prefers the long branch, so reading "ab…" the walk takes `a` --
// a whole match -- and goes on for `abcd`, which is the branch the order
// prefers. Where the next character is not `b`'s successor the walk dies and
// the answer is the match it passed.
struct picky {
  std::string text;
  int mark = 0;
};

struct tail {
  int letters = 0;
  int mark = 0;
};

struct head_of {
  picky first;
  tail rest;
};

}  // namespace

template <>
struct scan::scanner<picky> {
  static constexpr std::string_view pattern() { return "abcd|a"; }

  struct state {
    std::string text;
    ledger* book = nullptr;
  };

  static state begin(std::string_view) { return {}; }
  static state begin(std::string_view, ledger& book) {
    ++book.begun;
    return state{{}, &book};
  }
  static void push(state& made, char value) {
    made.text.push_back(value);
    // Written where the caller can see it, and written for every reading the
    // walk stands in -- including the one it is about to abandon.
    if (made.book != nullptr) made.book->seen.push_back(value);
  }
  static picky finish(state made) {
    if (made.book != nullptr) ++made.book->finished;
    return picky{std::move(made.text),
                 made.book == nullptr ? 0 : made.book->begun};
  }

  static picky parse(std::string_view text) { return picky{std::string(text)}; }
  static picky parse(std::string_view text, ledger& book) {
    ++book.begun;
    book.seen.append(text);
    ++book.finished;
    return picky{std::string(text), book.begun};
  }
};

template <>
struct scan::scanner<tail> {
  static constexpr std::string_view pattern() { return "[b-z]+"; }

  struct state {
    int letters = 0;
    ledger* book = nullptr;
  };

  static state begin(std::string_view) { return {}; }
  static state begin(std::string_view, ledger& book) {
    ++book.begun;
    return state{0, &book};
  }
  static void push(state& made, char value) {
    ++made.letters;
    if (made.book != nullptr) made.book->seen.push_back(value);
  }
  static tail finish(state made) {
    if (made.book != nullptr) ++made.book->finished;
    return tail{made.letters, made.book == nullptr ? 0 : made.book->begun};
  }

  static tail parse(std::string_view text) {
    return tail{static_cast<int>(text.size()), 0};
  }
  static tail parse(std::string_view text, ledger& book) {
    ++book.begun;
    book.seen.append(text);
    ++book.finished;
    return tail{static_cast<int>(text.size()), book.begun};
  }
};

namespace {

// The head itself, before anything is said to it: the walk takes `a`, goes on
// for `abcd`, dies on `x`, and the answer is the match it passed.
TEST(AHeadThatWasWalkedPast, TheAnswerIsThePlaceThePassedMatchWasAt) {
  const auto got = scan::scan_prefix<"{}{}">("abx!"sv).take<head_of>();
  EXPECT_EQ(got.value.first.text, "a");
  EXPECT_EQ(got.value.rest.letters, 2);
  EXPECT_EQ(got.rest, "!");
}

// The same head, with a context at each place, said in braces. What comes back
// is the same value; what the ledgers hold is what the walk did to get there.
TEST(AHeadThatWasWalkedPast, EachPlaceIsToldItsOwnInBraces) {
  ledger first;
  ledger second;
  const auto got =
      scan::scan_prefix<"{}{}">("abx!"sv).take<head_of>({first, second});

  // The value is the reading that won.
  EXPECT_EQ(got.value.first.text, "a");
  EXPECT_EQ(got.value.rest.letters, 2);
  EXPECT_EQ(got.rest, "!");

  // Each place was told its own and nobody else's: the first ledger saw the
  // characters of the first place, the second the characters of the second.
  EXPECT_NE(first.begun, 0);
  EXPECT_NE(second.begun, 0);
  EXPECT_EQ(second.seen, "bx");

  // And it was told once, by the reading that won -- not by the one the walk
  // abandoned. Where the characters can be pointed at, a place is read whole
  // once the match is over, so the walk going past `a` and dying on `x` costs
  // the context nothing: it hears `a`, which is what the value holds.
  EXPECT_EQ(first.seen, "a");
  EXPECT_EQ(first.begun, 1);
  EXPECT_EQ(first.finished, 1);
  EXPECT_EQ(second.begun, 1);
  EXPECT_EQ(second.finished, 1);
}

// Told one context for everybody, the same walk writes into the one ledger --
// so what it holds is every character every place was handed, in the order the
// walk handed them over.
TEST(AHeadThatWasWalkedPast, OneContextHearsEveryPlace) {
  ledger book;
  const auto got = scan::scan_prefix<"{}{}">("abx!"sv).take<head_of>(book);
  EXPECT_EQ(got.value.first.text, "a");
  // Both places, in the order they were read, and each of them once.
  EXPECT_EQ(book.seen, "abx");
  EXPECT_EQ(book.begun, 2);
  EXPECT_EQ(book.finished, 2);
}

// And where nothing is said, nothing is written: the hooks that take a context
// are not the hooks that are called.
TEST(AHeadThatWasWalkedPast, ToldNothingItWritesNothing) {
  ledger book;
  const auto got = scan::scan_prefix<"{}{}">("abx!"sv).take<head_of>();
  EXPECT_EQ(got.value.first.mark, 0);
  EXPECT_EQ(book.begun, 0);
  EXPECT_EQ(book.seen, "");
}

}  // namespace
