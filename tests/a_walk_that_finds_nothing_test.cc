// What a subject read once has to hold, and the only pattern that will not say
// how much.
//
// Not how long a match can be -- that is the answer, and the answer goes to
// the caller's collector. How long a walk can be that finds nothing, which is
// the fallback of the TDFA papers, asked of two places in the automaton.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

// A space is already a whole match, so the machine never reads past one; and
// the first character that is not a space dies before it is taken, so an
// attempt that comes to nothing swallows nothing. Both numbers are zero:
// searching and splitting on it holds not one character.
static_assert(scan::detail::fallback_window<"\\s+">() == 0);
static_assert(scan::detail::dead_end_window<"\\s+">() == 0);
static_assert(scan::detail::holds_a_bounded_way<"\\s+">());

static_assert(scan::detail::fallback_window<"[a-z]+">() == 0);
static_assert(scan::detail::dead_end_window<"[a-z]+">() == 0);
static_assert(scan::detail::holds_a_bounded_way<"[a-z]+">());

// `ab` takes the `a` before it knows, and gives it back where what follows is
// not a `b`.
static_assert(scan::detail::dead_end_window<"ab">() == 1);
static_assert(scan::detail::dead_end_window<"abc|abd">() == 2);

// Past a match, and this is where the order of the alternatives is felt.
//
// Written `abcd|a`, the walk of `abcd` sits above the match of `a` and is
// still alive, so the machine reads on: `b` and `c` lead nowhere, and those
// are the two characters a reading would have to be able to give back.
static_assert(scan::detail::fallback_window<"abcd|a">() == 2);

// Written the other way round, the match of `a` is the first walk in that
// state, everything under it was cut where the automaton was built, and there
// is nowhere to go at all. Nothing is ever read past the match, so nothing has
// to be held -- the same characters, the same rule, and the cost decided by
// which branch was written first.
static_assert(scan::detail::fallback_window<"a|abcd">() == 0);
static_assert(scan::detail::fallback_window<"a">() == 0);

// A cycle that never accepts: any number of characters swallowed and still no
// match, so there is no number, and this is the one thing a subject read once
// refuses -- `search_all<"a+b">` and `split<"a+b">` over such a subject say so
// where they are compiled.
static_assert(!scan::detail::holds_a_bounded_way<"a+b">());
static_assert(!scan::detail::holds_a_bounded_way<"(?:ab)+c">());

// Cycles as such are not the question: these have one and hold nothing, since
// every letter of the cycle is itself a whole match.
static_assert(scan::detail::holds_a_bounded_way<"a+">());
static_assert(scan::detail::holds_a_bounded_way<"[0-9]+">());

// This one has the same cycle, but nothing along it accepts until a digit
// arrives, so the letters pile up with nowhere to go.
static_assert(!scan::detail::holds_a_bounded_way<"[a-z]+[0-9]">());

// And the two ends are asked separately. A cycle with no match along it in
// front of the pattern says nothing about what can be read past a match: here
// the letters loop with nothing accepting, and yet from the match at the end
// there is no way back into them.
static_assert(scan::detail::fallback_window<"[a-z]+[0-9]">() == 0);
static_assert(scan::detail::dead_end_window<"[a-z]+[0-9]">() ==
              std::numeric_limits<std::size_t>::max());


TEST(ReadOnceHolding, TheNumbersAreTheWalksThatFindNothing) { SUCCEED(); }

}  // namespace
