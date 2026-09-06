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

// Past a match: `a` has matched, and then two characters are read on the
// chance of `abcd` and lead nowhere.
static_assert(scan::detail::fallback_window<"a|abcd">() == 2);
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


TEST(ReadOnceHolding, TheNumbersAreTheWalksThatFindNothing) { SUCCEED(); }

}  // namespace
