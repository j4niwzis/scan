// Generated from the module interface unit of the same name. Do not edit.
#pragma once

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include "scan_tre.hpp"
#include "scan_shape.hpp"

 namespace scan {

// A piece of the subject a match stood on, however the subject is held.
//
// Contiguous input is pointed at -- a view of it costs nothing and outlives
// the match, because the subject does. Input that is walked by iterators is
// held as the pair of them. Input that can only be read once is held as text
// of its own: there is nothing left behind to point at.
template <class Holder>
class basic_submatch {
 public:
  constexpr basic_submatch() = default;
  constexpr basic_submatch(Holder held, bool matched = true)
      : held_(std::move(held)), matched_(matched) {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return matched_;
  }

  [[nodiscard]] constexpr const Holder& held() const noexcept { return held_; }

  [[nodiscard]] constexpr auto begin() const { return std::ranges::begin(held_); }
  [[nodiscard]] constexpr auto end() const { return std::ranges::end(held_); }

  [[nodiscard]] constexpr std::size_t size() const {
    return static_cast<std::size_t>(std::ranges::size(held_));
  }

  // Only where the characters lie in a row: a view has to point at something
  // that is already there, and a subject read once is not.
  [[nodiscard]] constexpr auto data() const
    requires std::ranges::contiguous_range<const Holder&>
  {
    return std::ranges::data(held_);
  }

  [[nodiscard]] constexpr std::string_view to_view() const
    requires std::ranges::contiguous_range<const Holder&>
  {
    return std::string_view(std::ranges::data(held_),
                            static_cast<std::size_t>(std::ranges::size(held_)));
  }

  [[nodiscard]] constexpr operator std::string_view() const
    requires std::ranges::contiguous_range<const Holder&>
  {
    return to_view();
  }

 private:
  Holder held_{};
  bool matched_ = false;
};

using regex_submatch = basic_submatch<std::string_view>;

template <class Holder, std::size_t CaptureCount>
class basic_result {
  struct nothing {};
  using captures_type =
      std::conditional_t<CaptureCount == 0, nothing,
                         std::array<basic_submatch<Holder>, CaptureCount>>;

 public:
  constexpr basic_result() = default;

  constexpr basic_result(basic_submatch<Holder> whole, captures_type captures)
      : whole_(std::move(whole)), captures_(std::move(captures)) {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return static_cast<bool>(whole_);
  }
  [[nodiscard]] constexpr const basic_submatch<Holder>& whole() const noexcept {
    return whole_;
  }
  [[nodiscard]] constexpr auto begin() const { return whole_.begin(); }
  [[nodiscard]] constexpr auto end() const { return whole_.end(); }
  [[nodiscard]] constexpr std::size_t size() const { return whole_.size(); }

  [[nodiscard]] constexpr auto data() const
    requires std::ranges::contiguous_range<const Holder&>
  {
    return whole_.data();
  }
  [[nodiscard]] constexpr std::string_view to_view() const
    requires std::ranges::contiguous_range<const Holder&>
  {
    return whole_.to_view();
  }
  [[nodiscard]] constexpr operator std::string_view() const
    requires std::ranges::contiguous_range<const Holder&>
  {
    return to_view();
  }

  template <std::size_t Index>
  [[nodiscard]] constexpr basic_submatch<Holder> get() const {
    if constexpr (Index == 0) {
      return whole_;
    } else if constexpr (Index > CaptureCount) {
      return {};
    } else {
      return captures_[Index - 1];
    }
  }

 private:
  // Where a pattern has no captures there is nothing to hold, and holding
  // nothing costs nothing.
  //
  // An empty `std::array` is not empty: it keeps room for one element so that
  // `data()` has something to point at, which here is another twenty-four
  // bytes on a result of twenty-four -- zeroed on every match that never had a
  // capture to put there.
  basic_submatch<Holder> whole_;
  [[no_unique_address]] captures_type captures_{};
};

template <std::size_t CaptureCount>
using regex_result = basic_result<std::string_view, CaptureCount>;

namespace detail {

// And the opposite request, for the entry points whose bodies hold the
// scanning loop. Inlined into a caller, that loop shares its registers with
// whatever is alive there and is encoded with the extended ones and a
// byte-wide comparison -- seventeen bytes, across an instruction fetch
// boundary, where the same five instructions in a frame of their own take
// fourteen and sit in one. On a four kilobyte subject that is 1.8 gigabytes a
// second against 3.2.
#if defined(_MSC_VER)
#define SCAN_REGEX_FORCE_INLINE __forceinline
// A lambda takes the attribute but not the specifier: `inline` is not one of
// the things a lambda-declarator may carry, and naming it there is not a
// weaker request that compilers ignore -- it stops the parse.
#define SCAN_REGEX_FORCE_INLINE_LAMBDA __forceinline
#define SCAN_REGEX_NEVER_INLINE_CALL
#define SCAN_REGEX_FORCE_INLINE_CALL
#elif defined(__GNUC__) || defined(__clang__)
#define SCAN_REGEX_FORCE_INLINE [[gnu::always_inline]] inline
#define SCAN_REGEX_FORCE_INLINE_LAMBDA [[gnu::always_inline]]
#define SCAN_REGEX_NEVER_INLINE_CALL [[clang::noinline]]
#define SCAN_REGEX_FORCE_INLINE_CALL [[clang::always_inline]]
#else
#define SCAN_REGEX_FORCE_INLINE inline
#define SCAN_REGEX_FORCE_INLINE_LAMBDA
#define SCAN_REGEX_NEVER_INLINE_CALL
#define SCAN_REGEX_FORCE_INLINE_CALL
#endif

template <class StateType>
struct transition_range {
  unsigned char first = 0;
  unsigned char last = 0;
  StateType target{};
};

template <class StateType>
struct transition_ranges {
  std::array<transition_range<StateType>, 256> values{};
  std::size_t size = 0;
};

// Whether the machine standing here would have a match.
template <fixed_string Pattern, std::size_t State>
[[nodiscard]] consteval bool accepts_here() {
  return regex_automaton<Pattern>.states[State].accepting_slot !=
         packed_state<0, 0, 0>::not_accepting;
}

// The runs of a state, as the automaton holds them.
//
// These used to be recovered from a cell for every symbol, once for every
// instantiation that asked. The automaton is packed as runs now, so this hands
// them over.
template <fixed_string Pattern, std::size_t State>
[[nodiscard]] consteval auto make_transition_ranges() {
  constexpr const auto& automaton = regex_automaton<Pattern>;
  using state_type = std::size_t;
  transition_ranges<state_type> result;
  const auto& packed = automaton.states[State];
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    result.values[result.size++] = {
        .first = packed.ranges[index].first,
        .last = packed.ranges[index].last,
        .target = packed.ranges[index].target};
  }
  return result;
}

// Whether the reading that accepts holds its groups where the first one does.
//
// A state stands in several readings at once, ordered by which the
// disambiguation prefers, and they can disagree about which register holds a
// group. A machine that gathers as it goes has one gathering per group and
// fills it by the first reading, so what it hands back is right exactly where
// the reading that accepted holds that group in the same registers.
//
// It usually does: the readings of a state differ about the tags that are
// still undecided, not about the ones behind them. Where they differ, this
// says so instead of answering out of a gathering that did not win.
template <fixed_string Pattern>
[[nodiscard]] consteval bool accepts_where_the_first_reading_gathers() {
  constexpr const auto& automaton = regex_automaton<Pattern>;
  constexpr std::size_t group_count = automaton.tag_count / 2;
  for (const auto& state : automaton.states) {
    if (state.accepting_slot == packed_state<0, 0, 0>::not_accepting) continue;
    if (state.reading_count == 0) continue;
    for (std::size_t group = 0; group < group_count; ++group) {
      const auto& accepted = state.readings[state.accepting_slot];
      const auto& first = state.readings[0];
      if (accepted[group * 2] != first[group * 2]) return false;
      if (accepted[group * 2 + 1] != first[group * 2 + 1]) return false;
    }
  }
  return true;
}

template <fixed_string Pattern>
[[nodiscard]] consteval std::size_t minimum_match_length() {
  constexpr const auto& automaton = regex_automaton<Pattern>;
  constexpr std::size_t state_count =
      std::tuple_size_v<std::remove_cvref_t<decltype(automaton.states)>>;
  constexpr std::size_t unreachable =
      std::numeric_limits<std::size_t>::max();
  // Value-initialised, not left to the default constructor: an implicit
  // constructor of a class from the `std` module is defined only where it is
  // used, and clang's bytecode interpreter cannot produce that definition in
  // whoever imports it. Braces build the object without asking for one, and
  // this array is filled on the next line regardless.
  std::array<std::size_t, state_count> distance{};
  std::ranges::fill(distance, unreachable);
  distance[automaton.initial] = 0;
  // As many rounds as there are states: a shortest path visits each at most
  // once, so this is the distance to every state by then.
  for (std::size_t round = 0; round < state_count; ++round) {
    for (std::size_t source = 0; source < state_count; ++source) {
      if (distance[source] == unreachable) continue;
      const auto& packed = automaton.states[source];
      for (std::size_t index = 0; index < packed.range_count; ++index) {
        const std::size_t target = packed.ranges[index].target;
        distance[target] = std::min(distance[target], distance[source] + 1);
      }
    }
  }
  auto result = unreachable;
  for (std::size_t state = 0; state < state_count; ++state) {
    if (automaton.states[state].accepting_slot !=
        packed_state<0, 0, 0>::not_accepting) {
      result = std::min(result, distance[state]);
    }
  }
  return result;
}

// Above this many runs the question is asked of a table instead of asked of
// every run in turn. Measured on a long subject, in nanoseconds a character:
//
//     runs      1     2     3     4     6    12
//     compares  1.13  1.51  1.70  1.14  1.51  5.35
//     table     1.12  1.10  1.12  1.14  1.14  1.10
//
// The table is never behind, and by twelve runs it is five times ahead --
// where the comparisons are, the compiler spreads one character across a
// vector and compares it against four bounds at once, then folds the answer
// back through the mask registers, which is twenty instructions to decide one
// character. What keeps the comparisons here for one run and two is the short
// subject: the table is a load from memory that a couple of compares against
// constants do not need, and a match a few characters long never gets far
// enough for the loop to matter.
inline constexpr std::size_t runs_worth_comparing = 2;

// The states a state can move to, each named once, in the order the runs of
// symbols first mention them.
//
// A dozen runs that all lead to the same place are one entry here, and what
// follows the move is written once for the place instead of once for the run.
// The sentinel walk inlines what follows, so writing it per run made twelve
// copies of the same loop of the local part of an address.
template <class StateType>
struct transition_targets {
  std::array<StateType, 256> values{};
  std::size_t size = 0;
};

template <fixed_string Pattern, std::size_t State>
[[nodiscard]] consteval auto make_transition_targets() {
  constexpr const auto& automaton = regex_automaton<Pattern>;
  using state_type = std::size_t;
  constexpr auto ranges = make_transition_ranges<Pattern, State>();
  transition_targets<state_type> result;
  for (std::size_t index = 0; index < ranges.size; ++index) {
    const state_type target = ranges.values[index].target;
    bool named = false;
    for (std::size_t other = 0; other < result.size; ++other) {
      if (result.values[other] == target) named = true;
    }
    if (!named) result.values[result.size++] = target;
  }
  return result;
}

// How many runs of symbols lead from this state to that one.
template <fixed_string Pattern, std::size_t State, auto Target>
[[nodiscard]] consteval std::size_t runs_to_target() {
  constexpr auto ranges = make_transition_ranges<Pattern, State>();
  std::size_t count = 0;
  for (std::size_t index = 0; index < ranges.size; ++index) {
    if (ranges.values[index].target == Target) ++count;
  }
  return count;
}

// Where each symbol takes the automaton from this state, which is what the
// automaton was built with and is read back here rather than rebuilt.
template <fixed_string Pattern, std::size_t State>
inline constexpr auto transition_target_table = [] consteval {
  constexpr const auto& automaton = regex_automaton<Pattern>;
  using state_type = std::size_t;
  std::array<state_type, 256> result{};
  std::ranges::fill(result, static_cast<state_type>(
      std::numeric_limits<state_type>::max()));
  const auto& packed = automaton.states[State];
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    for (std::size_t symbol = packed.ranges[index].first;
         symbol <= packed.ranges[index].last; ++symbol) {
      result[symbol] = static_cast<state_type>(packed.ranges[index].target);
    }
  }
  return result;
}();

template <fixed_string Pattern, std::size_t State, auto Target,
          std::size_t Index = 0>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool moves_to_by_runs(
    unsigned char symbol) {
  constexpr auto ranges = make_transition_ranges<Pattern, State>();
  if constexpr (Index == ranges.size) {
    return false;
  } else {
    constexpr auto range = ranges.values[Index];
    if constexpr (range.target == Target) {
      if (symbol >= range.first && symbol <= range.last) return true;
    }
    return moves_to_by_runs<Pattern, State, Target, Index + 1>(symbol);
  }
}

// Whether the symbol moves the automaton from this state to that one.
template <fixed_string Pattern, std::size_t State, auto Target>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool moves_to(
    unsigned char symbol) {
  if constexpr (runs_to_target<Pattern, State, Target>() >
                runs_worth_comparing) {
    return transition_target_table<Pattern, State>[symbol] == Target;
  } else {
    return moves_to_by_runs<Pattern, State, Target>(symbol);
  }
}

// The class of symbols that keeps a state, in the shape the vector skip wants.
//
// The tagged walk has had this for a while: where a state stays put over a run
// of characters, the run is stepped over in vectors instead of being read one
// character at a time. Nothing about it needs the tags -- a state that keeps
// itself writes nothing while it does -- so the captureless walk asks the same
// question of the same code.
template <fixed_string Pattern, std::size_t State>
[[nodiscard]] consteval staying_class staying_class_of() {
  constexpr auto ranges = make_transition_ranges<Pattern, State>();
  staying_class answer;
  answer.below_the_high_bit = true;
  for (std::size_t index = 0; index < ranges.size; ++index) {
    const auto& range = ranges.values[index];
    if (range.target != State) continue;
    if (answer.count == answer.first.size()) return staying_class{};
    // Runs arrive in symbol order, so one that begins where the last ended is
    // the same run written twice and is joined here rather than tested twice.
    if (answer.count != 0 && answer.last[answer.count - 1] + 1 == range.first) {
      answer.last[answer.count - 1] = range.last;
    } else {
      answer.first[answer.count] = range.first;
      answer.last[answer.count] = range.last;
      ++answer.count;
    }
    if (range.last >= 128) answer.below_the_high_bit = false;
  }
  return answer;
}




// How many runs of symbols keep the automaton in the state it is in. A class
// like [a-z] is one of them; the local part of an address is twelve.
template <fixed_string Pattern, std::size_t State>
[[nodiscard]] consteval std::size_t self_range_count() {
  constexpr auto ranges = make_transition_ranges<Pattern, State>();
  std::size_t count = 0;
  for (std::size_t index = 0; index < ranges.size; ++index) {
    if (ranges.values[index].target == State) ++count;
  }
  return count;
}


template <fixed_string Pattern, std::size_t State>
inline constexpr auto self_transition_table = [] consteval {
  std::array<unsigned char, 256> result{};
  constexpr const auto& automaton = regex_automaton<Pattern>;
  const auto& packed = automaton.states[State];
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    if (packed.ranges[index].target != State) continue;
    for (std::size_t symbol = packed.ranges[index].first;
         symbol <= packed.ranges[index].last; ++symbol) {
      result[symbol] = 1;
    }
  }
  return result;
}();

template <fixed_string Pattern, std::size_t State, std::size_t Index = 0>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool
is_self_transition_by_runs(unsigned char symbol) {
  constexpr auto ranges = make_transition_ranges<Pattern, State>();
  if constexpr (Index == ranges.size) {
    return false;
  } else {
    constexpr auto range = ranges.values[Index];
    if constexpr (range.target == State) {
      if (symbol >= range.first && symbol <= range.last) return true;
    }
    return is_self_transition_by_runs<Pattern, State, Index + 1>(symbol);
  }
}

// Whether the symbol keeps the automaton where it is.
template <fixed_string Pattern, std::size_t State>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool is_self_transition(
    unsigned char symbol) {
  if constexpr (self_range_count<Pattern, State>() > runs_worth_comparing) {
    return self_transition_table<Pattern, State>[symbol] != 0;
  } else {
    return is_self_transition_by_runs<Pattern, State>(symbol);
  }
}


template <fixed_string Pattern, unsigned char Sentinel>
[[nodiscard]] consteval bool is_safe_sentinel() {
  constexpr const auto& automaton = regex_automaton<Pattern>;
  for (const auto& state : automaton.states) {
    for (std::size_t index = 0; index < state.range_count; ++index) {
      if (Sentinel >= state.ranges[index].first &&
          Sentinel <= state.ranges[index].last) {
        return false;
      }
    }
  }
  return true;
}




// The machine walked over any pair of iterators.
//
// The walks above take a pointer and give a pointer back, and everything they
// do -- the vectors, the terminator, the chain written out without calls --
// rests on that. This one rests on nothing: it asks the same questions of the
// same automaton, one character at a time, and works wherever a character can
// be read from. Contiguous input never comes here.
// The same walk, told what to do with the characters it consumes.
//
// Nothing where the answer is only whether it matched; where the answer holds

// The longest head, over any pair of iterators. Where nothing was accepted the
// answer is empty.


// Every reading of a pattern, said in terms of the one walk.
//
// What used to be five walks here -- bounded, terminated, terminated and
// inlined, the longest head, and the walk over iterators -- is five shapes of
// the same one.
template <fixed_string Pattern>
[[nodiscard]] consteval detail::walk_shape bounded_shape(bool in_words) {
  return {.in_words = in_words};
}

template <fixed_string Pattern, unsigned char Terminator>
[[nodiscard]] consteval detail::walk_shape terminated_shape(bool in_words) {
  return {.in_words = in_words,
          .by_terminator = true,
          .terminator = Terminator};
}

template <fixed_string Pattern>
[[nodiscard]] consteval detail::walk_shape head_shape() {
  return {.longest = true};
}

// Whether the subject matched, and where the longest head ended for the walks
// that look for one.
//
// The answer is filled in rather than handed back, because a reading of a
// subject that arrives as it is read does not copy, and passing it along by
// value would ask it to.
template <fixed_string Pattern, detail::walk_shape Shape, class CursorType,
          class SentinelType>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool walk_over(
    CursorType& cursor, SentinelType last,
    detail::walk_answer<CursorType>& best) {
  constexpr const auto& automaton = detail::regex_automaton<Pattern>;
  using mark_type =
      std::conditional_t<std::is_pointer_v<CursorType>, const char*,
                         std::ptrdiff_t>;
  detail::register_file<mark_type, automaton.register_count> registers{};
  if constexpr (!std::is_pointer_v<CursorType>) {
    registers.fill(scan::tre::negative_tag);
  }
  detail::gathers_nothing nothing;
  mark_type place{};
  if constexpr (std::is_pointer_v<CursorType>) place = cursor;
  // How many characters are known to be there: the subject was measured
  // against the shortest match before the first one was read. A walk after a
  // head was given no such promise -- the head may be shorter than the whole
  // of what it was handed.
  // Written where the caller is. A walk of a subject that was handed to us by
  // name is a few dozen instructions, and leaving it as a call costs the call,
  // the spills around it and an answer handed back through memory -- which for
  // a subject of twenty characters is a third of the work. Whoever must not
  // have it written out says so at their call: searching does, because it runs
  // a match for every place it tries.
  SCAN_REGEX_FORCE_INLINE_CALL
  return detail::run_continuation<automaton, Shape, automaton.initial,
                                  mark_type>(cursor, last, place, registers,
                                             nothing, best);
}

// The same, where nobody is asking where it stopped.
template <fixed_string Pattern, detail::walk_shape Shape, class CursorType,
          class SentinelType>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool matched_over(
    CursorType cursor, SentinelType last) {
  detail::walk_answer<CursorType> best;
  return walk_over<Pattern, Shape>(cursor, last, best);
}

template <fixed_string Pattern>
using regex_result_for =
    regex_result<regex_automaton<Pattern>.tag_count / 2>;

// The whole of the subject, from one end to the other.
//
// Built for the anchored rule, which is the same leftmost-first rule said at
// the other end: a match does not end the reading here, because the end of the
// subject does, so the walks below a match are kept and the answer is the
// first of them still accepting when the characters run out. `a|ab` reads
// "ab" as `ab`, where the same pattern searching for a head reads `a`.
template <fixed_string Pattern, how_to_walk Walk = how_to_walk::by_length>
[[nodiscard]] constexpr regex_result_for<Pattern.to_the_end()>
regex_match(std::string_view input) {
  constexpr auto whole = Pattern.to_the_end();
  constexpr const auto& automaton = regex_automaton<whole>;
  if constexpr (automaton.tag_count == 0) {
    if (input.size() < minimum_match_length<whole>()) return {};
    const char* cursor = input.data();
    const char* const end = cursor + input.size();
    // Which of the two walks runs is decided here, once, on the length of the
    // subject -- the way the tagged walk below decides it.
    //
    // A short subject is read a character at a time by code that carries no
    // trace of the other walk: the vector skip brings two dozen constants with
    // it and stops the states folding into one another, which costs a match of
    // twenty characters more than half of what it takes to run. Past sixty-four
    // the skip is ahead however the runs fall, and by a few hundred characters
    // it is ahead by ten times.
    const bool matched = [&] {
      if constexpr (Walk == how_to_walk::by_length) {
        constexpr std::size_t worth_a_vector = 64;
        return input.size() >= worth_a_vector
                   ? matched_over<whole, bounded_shape<whole>(true)>(cursor, end)
                   : matched_over<whole, bounded_shape<whole>(false)>(cursor,
                                                                     end);
      } else {
        return matched_over<whole, bounded_shape<whole>(
                                       Walk == how_to_walk::in_words)>(cursor,
                                                                       end);
      }
    }();
    if (!matched) return {};
    return {regex_submatch(input), {}};
  } else {
    // A register holds where in the subject something happened, and holds it as
    // the address itself, so that nothing has to be added to it or taken from
    // it. A slot that was never written holds nothing at all.
    detail::register_file<const char*, automaton.register_count> registers{};
    const char* cursor = input.data();
    const char* const end = cursor + input.size();
    execute_commands(automaton.initialize, automaton.initialize.size(),
                     registers, cursor);
    // The generated form, as the captureless branch above uses. Which of the
    // two walks runs is decided once, on the length of the subject: a short one
    // is read a character at a time, a long one in words and vectors.
    const bool matched = [&] {
      if constexpr (Walk == how_to_walk::by_length) {
        constexpr std::size_t worth_a_word = 32;
        return input.size() >= worth_a_word
                   ? run_from_here<automaton, true, automaton.initial>(
                         cursor, end, registers)
                   : run_from_here<automaton, false, automaton.initial>(
                         cursor, end, registers);
      } else {
        return run_from_here<automaton, Walk == how_to_walk::in_words,
                             automaton.initial>(cursor, end, registers);
      }
    }();
    if (!matched) return {};

    std::array<regex_submatch, automaton.tag_count / 2> captures{};
    for (std::size_t capture : std::views::iota(std::size_t{0}, automaton.tag_count / 2)) {
          const auto begin =
              slot_read(registers, capture * 2);
          const auto end =
              slot_read(registers, capture * 2 + 1);
          if (begin == nullptr || end == nullptr) continue;
          captures[capture] =
              regex_submatch(std::string_view(begin, static_cast<std::size_t>(end - begin)));
        }
    return {regex_submatch(input), captures};
  }
}

// A terminated subject, read either the way a measured one is read or the way
// an unmeasured one would be.
//
// `measured` is what the length is spent on: the early answer where the
// subject is shorter than the shortest match, and the choice between reading
// one character at a time and reading in words. Neither is available to a
// reading that arrives without a length, and neither is wanted where the
// subject is known to be short -- a date is nineteen characters, and asking
// twice whether it is long enough to be worth a vector costs more than it
// saves.
//
// Nothing else changes. A walk that stops at a terminator never compares the
// cursor with an end, so there is no end test to lose either way.
template <fixed_string Pattern, unsigned char Sentinel,
          how_to_walk Walk = how_to_walk::by_length>
[[nodiscard]] constexpr regex_result_for<Pattern.to_the_end()>
regex_match_sentinel(std::string_view input) {
  constexpr auto whole = Pattern.to_the_end();
  constexpr const auto& automaton = regex_automaton<whole>;
  static_assert(is_safe_sentinel<whole, Sentinel>(),
                "sentinel must be rejected in every automaton state");
  const char* const end = input.data() + input.size();
  if constexpr (automaton.tag_count == 0) {
    const bool matched = [&] {
      if constexpr (Walk == how_to_walk::by_length) {
        if (input.size() < minimum_match_length<whole>()) return false;
        constexpr std::size_t worth_a_vector = 64;
        return input.size() >= worth_a_vector
                   ? matched_over<whole,
                                  terminated_shape<whole, Sentinel>(true)>(
                         input.data(), end)
                   : matched_over<whole,
                                  terminated_shape<whole, Sentinel>(false)>(
                         input.data(), end);
      } else {
        return matched_over<
            whole, terminated_shape<whole, Sentinel>(Walk ==
                                                     how_to_walk::in_words)>(
            input.data(), end);
      }
    }();
    if (!matched) return {};
    return {regex_submatch(input), {}};
  } else {
    // The same walk with the registers carried, which is the walk the format
    // layer has always used for a terminated subject. What the terminator
    // saves is the end test, and a group is written by the same operations
    // whether the end is tested or not -- so there was nothing here to refuse.
    detail::register_file<const char*, automaton.register_count> registers{};
    const char* cursor = input.data();
    execute_commands(automaton.initialize, automaton.initialize.size(),
                     registers, cursor);
    const bool matched = [&] {
      if constexpr (Walk == how_to_walk::by_length) {
        if (input.size() < minimum_match_length<whole>()) return false;
        constexpr std::size_t worth_a_word = 32;
        return input.size() >= worth_a_word
                   ? run_to_terminator<automaton, Sentinel, true,
                                       automaton.initial>(cursor, end,
                                                          registers)
                   : run_to_terminator<automaton, Sentinel, false,
                                       automaton.initial>(cursor, end,
                                                          registers);
      } else {
        return run_to_terminator<automaton, Sentinel,
                                 Walk == how_to_walk::in_words,
                                 automaton.initial>(cursor, end, registers);
      }
    }();
    if (!matched) return {};
    std::array<regex_submatch, automaton.tag_count / 2> captures{};
    for (std::size_t capture :
         std::views::iota(std::size_t{0}, automaton.tag_count / 2)) {
      const char* const from = slot_read(registers, capture * 2);
      const char* const to = slot_read(registers, capture * 2 + 1);
      if (from == nullptr || to == nullptr) continue;
      captures[capture] = regex_submatch(
          std::string_view(from, static_cast<std::size_t>(to - from)));
    }
    return {regex_submatch(input), captures};
  }
}

// The longest head of the input the automaton accepts, or nothing.
//
// One pass, remembering the last place the machine stood in an accepting
// state -- which is how the format layer has always read a head, and how a
// scanner reads one. What was here instead tried every length in turn and ran
// a whole anchored match for each, so a head of a hundred characters was a
// hundred matches and a search over it was ten thousand.


// The same question of whichever machine the pattern was given. A pattern that
// captures is a tagged machine, and the runtime has read a head off one of
// those since the format layer needed it.
template <fixed_string Pattern>
[[nodiscard]] constexpr const char* longest_head(const char* cursor,
                                                 const char* end) {
  detail::walk_answer<const char*> best;
  const char* walking = cursor;
  const bool found =
      walk_over<Pattern, head_shape<Pattern>()>(walking, end, best);
  return found ? *best.at : nullptr;
}

template <fixed_string Pattern>
[[nodiscard]] constexpr regex_result_for<Pattern> regex_starts_with(
    std::string_view input) {
  const char* const begin = input.data();
  const char* const best = longest_head<Pattern>(begin, begin + input.size());
  if (best == nullptr) return {};
  // The head is known; the match over exactly that head is run again to fill
  // in whatever the pattern captures, which one pass cannot carry.
  //
  // Called rather than written out here. Whoever matches a subject of their
  // own wants it written where they are -- no call, no result handed back
  // through memory, and the parts of it they do not look at gone. A search
  // runs it once for every place it tries, and writing it out there would put
  // the whole of a match inside a loop.
  SCAN_REGEX_NEVER_INLINE_CALL
  return regex_match<Pattern>(
      input.substr(0, static_cast<std::size_t>(best - begin)));
}

// The leftmost match, and the longest one there.
//
// A head is read from every place in turn, and reading one stops at the first
// character the machine will not take -- so a place that cannot begin a match
// costs what it costs to find that out, and not a match of every length from
// there.
template <fixed_string Pattern>
[[nodiscard]] constexpr regex_result_for<Pattern> regex_search(
    std::string_view input) {
  const char* const begin = input.data();
  const char* const end = begin + input.size();
  for (const char* from = begin; from <= end; ++from) {
    const char* const best = longest_head<Pattern>(from, end);
    if (best == nullptr) continue;
    SCAN_REGEX_NEVER_INLINE_CALL
    return regex_match<Pattern>(
        std::string_view(from, static_cast<std::size_t>(best - from)));
  }
  return {};
}

}  // namespace detail

namespace detail {

// The three questions asked of a pattern rather than of an automaton. The
// arithmetic itself is in the runtime, where the format layer asks it too.
template <fixed_string Pattern>
[[nodiscard]] consteval std::size_t fallback_window() {
  return detail::walk_past_a_match<regex_automaton<Pattern>>();
}

template <fixed_string Pattern>
[[nodiscard]] consteval std::size_t dead_end_window() {
  return detail::walk_from_the_start<regex_automaton<Pattern>>();
}

template <fixed_string Pattern>
[[nodiscard]] consteval bool falls_back_a_bounded_way() {
  return fallback_window<Pattern>() !=
         std::numeric_limits<std::size_t>::max();
}

// Whether a subject that can only be read once can be searched at all: both
// ends of the reading have to name a number -- what a failed attempt swallows
// and what is read past a match.
template <fixed_string Pattern>
[[nodiscard]] consteval bool holds_a_bounded_way() {
  constexpr std::size_t unbounded = std::numeric_limits<std::size_t>::max();
  return dead_end_window<Pattern>() != unbounded &&
         fallback_window<Pattern>() != unbounded;
}

// Whether the group is being read where the machine stands now.
template <fixed_string Pattern, std::size_t Group, class RegistersType>
[[nodiscard]] constexpr bool group_is_open(std::size_t here,
                                           const RegistersType& registers) {
  constexpr const auto& automaton = regex_automaton<Pattern>;
  const auto& packed = automaton.states[here];
  if (packed.reading_count == 0) return false;
  const std::uint32_t opening = packed.readings[0][Group * 2];
  const std::uint32_t closing = packed.readings[0][Group * 2 + 1];
  return slot_read(registers, opening) >= 0 && slot_read(registers, closing) < slot_read(registers, opening);
}


template <class RangeType>
concept forward_char_range =
    std::ranges::forward_range<RangeType> &&
    std::same_as<std::ranges::range_value_t<RangeType>, char> &&
    !contiguous_char_range<RangeType>;

template <class RangeType>
concept read_once_char_range =
    std::ranges::input_range<RangeType> &&
    std::same_as<std::ranges::range_value_t<RangeType>, char> &&
    !std::ranges::forward_range<RangeType>;

// A subject that can only be read once is read into text of its own, and
// everything after that is the ordinary reading of contiguous characters --
// with the answers owning what they stood on, because there is nothing else
// left to point at.
template <class HeldType, class RangeType>
[[nodiscard]] constexpr HeldType read_once(RangeType&& input) {
  HeldType held;
  auto cursor = std::ranges::begin(input);
  const auto last = std::ranges::end(input);
  for (; cursor != last; ++cursor) held.push_back(*cursor);
  return held;
}

template <class RangeType>
using walked_holder =
    std::ranges::subrange<std::ranges::iterator_t<RangeType>,
                          std::ranges::iterator_t<RangeType>>;

}  // namespace detail

namespace detail {

// The text a capturing group was written with, counting groups from one.
//
// Read the way the pattern reader reads it: a backslash takes the character
// after it whatever that is, a class runs to its closing bracket, and a
// parenthesis that opens with a question mark captures nothing and is not
// counted.
template <fixed_string Pattern>
[[nodiscard]] consteval std::string_view group_text(std::size_t wanted) {
  const std::string_view text = Pattern.view();
  const auto skip_class = [&](std::size_t at) {
    ++at;
    while (at < text.size() && text[at] != ']') {
      if (text[at] == '\\') ++at;
      ++at;
    }
    return at;
  };
  std::size_t seen = 0;
  for (std::size_t at = 0; at < text.size(); ++at) {
    if (text[at] == '\\') {
      ++at;
      continue;
    }
    if (text[at] == '[') {
      at = skip_class(at);
      continue;
    }
    if (text[at] != '(') continue;
    if (at + 1 < text.size() && text[at + 1] == '?') continue;
    ++seen;
    if (seen != wanted) continue;
    std::size_t depth = 0;
    for (std::size_t end = at; end < text.size(); ++end) {
      if (text[end] == '\\') {
        ++end;
        continue;
      }
      if (text[end] == '[') {
        end = skip_class(end);
        continue;
      }
      if (text[end] == '(') {
        ++depth;
      } else if (text[end] == ')') {
        --depth;
        if (depth == 0) return text.substr(at + 1, end - at - 1);
      }
    }
    return {};
  }
  return {};
}

// Whether this group is written with the very pattern the type declares for
// itself.
//
// Where it is, the groups inside it are the type's own values -- the big
// machine has already found them -- and the type is built from them. Where it
// is not, there is nothing to reuse: the characters are handed to the type to
// read as it sees fit.
// Whether this move writes the opening of that group.
//
// An opening written is an opening that happened: only the closings are
// written on the chance of a match ending here and taken back when it does
// not. So this is the one edge of a group that can be known while the pattern
// is compiled, and the other edge follows from it -- a turn ends where the
// next one begins, or where the match does.
template <auto& Automaton, std::size_t State, std::size_t Move,
          std::size_t Group>
[[nodiscard]] consteval bool opens_the_group() {
  constexpr const auto& taken = Automaton.states[State].ranges[Move];
  for (std::size_t at = 0; at < taken.command_count; ++at) {
    const std::size_t destination = taken.commands[at].destination;
    if (destination >= Automaton.register_tag.size()) continue;
    if (Automaton.register_tag[destination] == Group * 2) return true;
  }
  return false;
}

// A type told where its groups begin and end, and not only what is in them.
//
// This is what a list is made of -- every turn of a repeated group is an
// element, and the type is told when one ended -- and what a choice is made of
// -- the branch whose group opened is the branch that ran. Both were things
// only a format could say.
template <class Type>
concept knows_its_edges =
    requires(group_state_of<Type>& state) {
      scan::scanner<std::remove_cv_t<Type>>{}.opened_group(state,
                                                          std::size_t{0});
    } || requires(group_state_of<Type>& state) {
      scan::scanner<std::remove_cv_t<Type>>{}.opened_group(
          state, scan::group_at<std::size_t{0}>{});
    } || (names_its_groups<Type> && requires(group_state_of<Type>& state) {
      scan::scanner<std::remove_cv_t<Type>>{}.opened_group(
          state,
          std::variant_alternative_t<
              0, typename scan::scanner<std::remove_cv_t<Type>>::group>{});
    });

// A type gathered by its own groups, a character at a time.
//
// The other way round from `from_groups`: there the groups are handed over
// when the match is done, which wants a subject that can still be pointed at.
// This one is told, as each character arrives, which of the type's own groups
// it belongs to -- so a type with parts can be read off a subject that will
// never be seen again, and no text is put together anywhere.
template <class Type>
concept gathers_by_group = requires {
  scan::scanner<std::remove_cv_t<Type>>{}.begin_groups();
  scan::scanner<std::remove_cv_t<Type>>{}.finish_groups(
      scan::scanner<std::remove_cv_t<Type>>{}.begin_groups());
};

// A type that would rather be handed the groups than the text.
//
// Where a type's own pattern has groups in it, the machine that matched the
// big pattern has already found them -- they are groups of that match like any
// others. This is how a type says it can be built from them, and it is handed
// exactly its own, in the order it wrote them.
template <class Type>
concept scanned_from_groups = requires(std::span<const std::string_view> given) {
  scan::scanner<std::remove_cv_t<Type>>{}.from_groups(given);
};

// Whether this group is written with the very expression the type declares.
template <class Type, fixed_string Pattern, std::size_t Group>
[[nodiscard]] consteval bool group_is_written_as_the_types_pattern() {
  if constexpr (!scanned_as_leaf<std::remove_cv_t<Type>>) {
    return false;
  } else {
    constexpr auto written = group_text<Pattern>(Group);
    if constexpr (written.empty()) {
      return false;
    } else {
      const auto declared = declared_pattern<Type>();
      std::size_t here = 0;
      tre_parser reading_the_group(written, {}, here, true);
      const auto theirs = reading_the_group.parse_regex();
      std::size_t there = 0;
      tre_parser reading_the_type(pattern_view(declared), {}, there, true);
      const auto ours = reading_the_type.parse_regex();
      return scan::tre::same_expression(theirs, ours);
    }
  }
}

// The two kinds of reuse, each asking that question and its own.
template <class Type, fixed_string Pattern, std::size_t Group>
[[nodiscard]] consteval bool group_is_the_types_pattern() {
  return scanned_from_groups<Type> &&
         group_is_written_as_the_types_pattern<Type, Pattern, Group>();
}

template <class Type, fixed_string Pattern, std::size_t Group>
[[nodiscard]] consteval bool group_gathers_by_group() {
  return gathers_by_group<Type> &&
         group_is_written_as_the_types_pattern<Type, Pattern, Group>();
}

template <class Type, fixed_string Pattern, std::size_t Group>
[[nodiscard]] consteval bool group_spells_out() {
  if constexpr (!says_it_reads_its_groups<std::remove_cv_t<Type>>) {
    return false;
  } else if constexpr (groups_of_output<std::remove_cv_t<Type>>() <= 1) {
    return false;
  } else {
    // Both read, and the readings compared -- not the characters. `a+` and
    // `a{1,}` are the same repetition and `(?:ab)` and `ab` the same
    // sequence; what tells them apart is the writing, and the writing is not
    // what decides this.
    //
    // Only what the reading itself makes identical, and nothing beyond. A
    // count is compared as a count: `a{2}` and `aa` are the same characters
    // and not the same expression, and putting a group around them shows why
    // -- `(a){2}` is one group that took two turns and `(a)(a)` is two
    // groups. Anything that moved the groups would be worse than useless
    // here, because what is being decided is whether the groups already found
    // are that type's values.
    //
    // Where two expressions really are the same and this says they are not,
    // the text is read the ordinary way and the cost is one reading. Where it
    // said yes wrongly, the answer would be wrong. So it says no unless the
    // trees are the same tree.
    //
    // Each is read on its own, with its own count of the groups it opens, so
    // the tags in the two trees are numbered from the same place and can be
    // compared as they stand.
    // The pattern the type declares, which for a shape is its places written
    // as groups -- the same groups this asks whether the big pattern already
    // has.
    constexpr auto declared = declared_pattern<std::remove_cv_t<Type>>();
    constexpr auto written = group_text<Pattern>(Group);
    if constexpr (written.empty()) {
      return false;
    } else {
      std::size_t here = 0;
      tre_parser reading_the_group(written, {}, here, true);
      const auto theirs = reading_the_group.parse_regex();
      std::size_t there = 0;
      tre_parser reading_the_type(pattern_view(declared), {}, there, true);
      const auto ours = reading_the_type.parse_regex();
      return scan::tre::same_expression(theirs, ours);
    }
  }
}

}  // namespace detail

// What a group is turned into.
//
// A collector says how to make a value out of the characters a group stood on.
// There are three ways and the choice is made where the type is known, not
// where the characters arrive:
//
//   * the type's own incremental scanner, where it has one -- the characters
//     go straight in and no text is ever built;
//   * the type's `parse`, where it has that instead -- the characters are
//     handed over once, as a piece;
//   * the type built from the characters, for a type that is a string of some
//     kind and wants no scanner at all.
//
// Arguments given here are the arguments the value is made with, so a group
// can be collected into a string with one allocator and the next group into a
// string with another.
template <class Type, class... Arguments>
class as_collector {
 public:
  using value_type = Type;

  constexpr explicit as_collector(Arguments... given)
      : arguments_(std::move(given)...) {}

  [[nodiscard]] constexpr value_type parse(
      std::string_view text, std::string_view parameters) const {
    // Asked of the scanner itself, not of the helper: the helper is a template
    // whose body is what fails for a type that has no scanner, and a body
    // failing is not a question anyone can ask.
    // Arguments given are the arguments the value is made with, and that is
    // the whole point of giving them: a type that also says a scanner of its
    // own would have that scanner asked instead, and the arguments would go
    // nowhere. A pool said here is said because the value has to be built with
    // it, and no scanner of that type knows about it.
    if constexpr (sizeof...(Arguments) != 0 &&
                  requires(const Arguments&... given) {
                    value_type(text.begin(), text.end(), given...);
                  }) {
      return std::apply(
          [&](const Arguments&... given) {
            return value_type(text.begin(), text.end(), given...);
          },
          arguments_);
    } else if constexpr (scan::says_what_went_wrong<Type>) {
      // A collector has to give a value, so this is the throwing way of
      // reading and the scanner is told so. One written against it throws
      // where it stands and never builds the expected that would only be
      // unwrapped and thrown again here; one that hands a failure back anyway
      // is answered the same as before.
      return scan::as_thrown<Type>(
          scan::scanner_told_parse<Type, scan::throws_a_failure>(text,
                                                                 parameters));
    } else if constexpr (requires {
                    std::declval<scan::scanner<Type>&>().parse(text,
                                                               parameters);
                  } || requires {
                    std::declval<scan::scanner<Type>&>().parse(text);
                  }) {
      return scanner_parse<Type>(text, parameters);
    } else {
      return std::apply(
          [&](const Arguments&... given) {
            return value_type(text.begin(), text.end(), given...);
          },
          arguments_);
    }
  }

  // Characters as they come, for a subject that cannot be looked at twice.
  [[nodiscard]] constexpr auto begin(
      std::string_view parameters) const {
    if constexpr (sizeof...(Arguments) != 0 &&
                  requires(const Arguments&... given) {
                    value_type(given...);
                  }) {
      // The same rule where the characters arrive one at a time: what the
      // value is made with is what was said here.
      return std::apply(
          [&](const Arguments&... given) { return value_type(given...); },
          arguments_);
    } else if constexpr (requires {
                    std::declval<scan::scanner<Type>&>().begin(parameters);
                  } || requires {
                    std::declval<scan::scanner<Type>&>().begin();
                  }) {
      return scanner_begin<Type>(parameters);
    } else {
      return std::apply(
          [&](const Arguments&... given) { return value_type(given...); },
          arguments_);
    }
  }


  constexpr void push(auto& state, char letter) const {
    if constexpr (requires { scanner_push<Type>(state, letter); }) {
      scanner_push<Type>(state, letter);
    } else {
      state.push_back(letter);
    }
  }

  [[nodiscard]] constexpr value_type finish(auto state) const {
    if constexpr (requires {
                    scanner_finish<Type, decltype(state)>(state);
                  }) {
      return scanner_finish<Type>(std::move(state));
    } else {
      return std::move(state);
    }
  }
 private:
  std::tuple<Arguments...> arguments_;
};

template <class Type, class... Arguments>
[[nodiscard]] constexpr auto as(Arguments&&... given) {
  return as_collector<Type, std::remove_cvref_t<Arguments>...>(
      std::forward<Arguments>(given)...);
}

// What stands in the answer where a group was wanted by nobody. Nothing, and
// it takes no room.
struct skipped {};

// The characters themselves, held however the subject affords: pointed at,
// walked between, or owned. This is what every group is collected into when
// nothing else is said.
struct text_collector {
  // What it makes is what the subject affords: a view where the characters
  // can be pointed at, something owning where they cannot. Nothing here is
  // privileged -- a collector of your own says the same three things and is
  // treated the same way.
  template <class Holder>
  using value_for = Holder;

  template <class Holder>
  [[nodiscard]] constexpr Holder parse(std::string_view text,
                                           std::string_view) const {
    return Holder(text.begin(), text.end());
  }

  template <class Holder>
  [[nodiscard]] constexpr Holder begin(std::string_view) const {
    return Holder{};
  }

  constexpr void push(auto& into, char letter) const {
    into.push_back(letter);
  }

  // A run the walk stepped over in vectors, handed over as a run rather than
  // one character at a time. Optional: a collector without it is handed the
  // characters one by one, as everything was before.
  constexpr void push(auto& into, std::string_view run) const {
    into.append(run.begin(), run.end());
  }

  [[nodiscard]] constexpr auto finish(auto state) const {
    return state;
  }
};

[[nodiscard]] constexpr text_collector text() { return {}; }

// Nothing from this group.
//
// The group is still there -- it may be there because the type of another
// group is read out of it -- but no value is made from it and it takes no room
// in what comes back.
//
// Written the way a collector of your own would be: `takes_nothing` is what
// says the characters need not be handed over at all, and `scan::skipped` is
// what stands in the answer where a value would have been.
struct skip_collector {
  static constexpr bool takes_nothing = true;

  template <class Holder>
  using value_for = skipped;
};

[[nodiscard]] constexpr skip_collector skip() { return {}; }

// A value of any type at all, filled by a call of your own.
//
// The value is made from the arguments given here, and every character of the
// group is handed to the call along with it. What that does is nobody else's
// business: it can push into a string, count, hash, or throw the characters
// away.
template <class Type, class Pusher, class... Arguments>
class collecting_collector {
 public:
  using value_type = Type;

  constexpr collecting_collector(Pusher push, Arguments... given)
      : push_(std::move(push)), arguments_(std::move(given)...) {}

  [[nodiscard]] constexpr value_type parse(std::string_view text,
                                               std::string_view) const {
    value_type made = std::apply(
        [&](const Arguments&... given) { return value_type(given...); },
        arguments_);
    for (const char letter : text) push_(made, letter);
    return made;
  }

  [[nodiscard]] constexpr auto begin(std::string_view) const {
    return std::apply(
        [&](const Arguments&... given) { return value_type(given...); },
        arguments_);
  }

  constexpr void push(value_type& into, char letter) const {
    push_(into, letter);
  }

  // The value is filled as the characters arrive, so what is begun is already
  // what comes back: this is here because a subject read once asks for it, and
  // a collector that cannot answer it is one that only reads what it can point
  // at.
  [[nodiscard]] constexpr value_type finish(value_type state) const {
    return state;
  }

 private:
  Pusher push_;
  std::tuple<Arguments...> arguments_;
};

template <class Type, class Pusher, class... Arguments>
[[nodiscard]] constexpr auto collecting(Pusher&& push, Arguments&&... given) {
  return collecting_collector<Type, std::remove_cvref_t<Pusher>,
                              std::remove_cvref_t<Arguments>...>(
      std::forward<Pusher>(push), std::forward<Arguments>(given)...);
}

// A match whose groups were turned into values.
//
// The whole of it is still a piece of the subject, held the way the subject
// affords; each group is whatever its collector made of it, and they keep the
// order they were written in. A group nobody wanted is `skipped`, which is
// nothing and takes no room.
template <class WholeHolder, class... Values>
class typed_result {
 public:
  constexpr typed_result() = default;
  constexpr typed_result(basic_submatch<WholeHolder> whole,
                         std::tuple<Values...> made)
      : whole_(std::move(whole)), values_(std::move(made)) {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return static_cast<bool>(whole_);
  }
  [[nodiscard]] constexpr const basic_submatch<WholeHolder>& whole()
      const noexcept {
    return whole_;
  }

  // Nought is the whole match, as it is everywhere else here; the rest are the
  // groups, in the order they were written.
  template <std::size_t Index>
  [[nodiscard]] constexpr decltype(auto) get() const {
    if constexpr (Index == 0) {
      return (whole_);
    } else {
      static_assert(Index <= sizeof...(Values), "no such group");
      return std::get<Index - 1>(values_);
    }
  }

  [[nodiscard]] constexpr const std::tuple<Values...>& all() const noexcept {
    return values_;
  }

 private:
  basic_submatch<WholeHolder> whole_;
  [[no_unique_address]] std::tuple<Values...> values_{};
};

namespace detail {

// What one collector makes.
//
// A collector says it with `value_type`; a scanner says it by being the scanner
// of a type, and `scan::scanner<T>` handed over where a collector is wanted is
// a collector of `T`. The two speak the same hooks -- `parse`, `begin`, `push`,
// `finish` -- so the only thing a scanner is missing here is the name of what
// it makes, and it is written on the scanner itself.
template <class Collector, class Holder>
struct collected {
  using type = scan::scanner_target_t<Collector>;
};

template <class Collector, class Holder>
  requires requires { typename Collector::value_type; }
struct collected<Collector, Holder> {
  using type = typename Collector::value_type;
};

// What a collector makes, which may depend on what the subject affords.
//
// A collector that keeps the characters themselves makes a view where they
// can be pointed at and something owning where they cannot, so what it makes
// is not one type but a type per holder. Saying `value_for` is how a collector
// says that; saying `value_type` is how it says the one type it always makes.
template <class Collector, class Holder>
concept makes_by_holder = requires {
  typename Collector::template value_for<Holder>;
};

template <class Collector, class Holder>
  requires makes_by_holder<Collector, Holder>
struct collected<Collector, Holder> {
  using type = typename Collector::template value_for<Holder>;
};

template <class Collector, class Holder>
using collected_type = typename collected<Collector, Holder>::type;

// The same question asked without a holder, for the places that want to know
// what a collector is for rather than what it will make this time: a collector
// says it with `value_type`, a scanner by being the scanner of a type.
template <class Collector>
struct collector_value;

template <class Collector>
  requires requires { typename Collector::value_type; }
struct collector_value<Collector> {
  using type = typename Collector::value_type;
};

template <class Collector>
  requires(!requires { typename Collector::value_type; } &&
           requires { typename scan::scanner_target<Collector>::type_t; })
struct collector_value<Collector> {
  using type = typename scan::scanner_target<Collector>::type_t;
};

template <class Collector>
using collector_value_t = typename collector_value<Collector>::type;

// Every group of a match, as text, for the types that are built out of them.
template <class FoundType, std::size_t... Group>
[[nodiscard]] constexpr auto all_groups(const FoundType& found,
                                        std::index_sequence<Group...>) {
  return std::array<std::string_view, sizeof...(Group)>{
      found.template get<Group + 1>().to_view()...};
}

template <fixed_string Pattern, std::size_t Group, class Collector,
          class Holder, class FoundType>
[[nodiscard]] constexpr collected_type<Collector, Holder> collect_one(
    const Collector& one, const FoundType& found) {
  if constexpr (requires { Collector::takes_nothing; }) {
    return {};
  } else if constexpr (requires {
                         one.template parse<Holder>(std::string_view{},
                                                        std::string_view{});
                       }) {
    // A collector that makes what the subject affords is handed the holder to
    // make it as. Where the characters can be pointed at, that is a view of
    // them and nothing is copied.
    if constexpr (std::same_as<Holder, std::string_view>) {
      return found.template get<Group>().held();
    } else {
      return one.template parse<Holder>(
          found.template get<Group>().to_view(), std::string_view{});
    }
  } else if constexpr (group_is_the_types_pattern<
                           collector_value_t<Collector>, Pattern, Group>()) {
    // The type says a pattern of its own with groups in it, and this group is
    // written with that pattern -- so the groups inside it are the type's own,
    // already found, and the type asked to be handed them rather than the
    // text.
    using held_type = std::remove_cv_t<collector_value_t<Collector>>;
    constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
    std::array<std::string_view, inside> theirs{};
    [&]<std::size_t... at>(std::index_sequence<at...>) {
      ((theirs[at] = found.template get<Group + 1 + at>().to_view()), ...);
    }(std::make_index_sequence<inside>{});
    // A collector has to give a value: a type that hands its failure back is
    // asked the same way as one that throws, and what it handed back is thrown
    // here.
    return scan::as_thrown<held_type>(
        scan::scanner_told_from_groups<held_type, scan::throws_a_failure>(
            std::span<const std::string_view>(theirs)));
  } else if constexpr (group_gathers_by_group<collector_value_t<Collector>,
                                             Pattern, Group>()) {
    // The type is gathered by its own groups, and here they are already
    // found: the characters of each are handed to it the same way they would
    // be handed over one at a time on a subject that cannot be looked at
    // twice, so the type is read the same way wherever it is used.
    using held_type_here = std::remove_cv_t<collector_value_t<Collector>>;
    auto state = scan::scanner<held_type_here>{}.begin_groups();
    [&]<std::size_t... inside>(std::index_sequence<inside...>) {
      ((void)[&] {
        for (char letter : found.template get<Group + 1 + inside>().to_view()) {
          push_one_group<held_type_here, inside>(state, letter);
        }
      }(), ...);
    }(std::make_index_sequence<groups_a_leaf_opens<held_type_here>()>{});
    return scan::as_thrown<held_type_here>(
        scan::scanner_told_finish_groups<held_type_here,
                                         scan::throws_a_failure>(
            std::move(state)));
  } else if constexpr (group_spells_out<collector_value_t<Collector>,
                                        Pattern, Group>()) {
    // The group is the type's own pattern, so the groups inside it are the
    // type's own values and the machine has already found them. Nothing is
    // read twice and no second automaton was ever built.
    constexpr std::size_t count = regex_automaton<Pattern>.tag_count / 2;
    const auto groups = all_groups(found, std::make_index_sequence<count>{});
    // A collector gives a value and has nowhere to put a failure, so a reading
    // that went wrong is thrown here -- at the asking, which is the only place
    // in this library anything is thrown.
    // As what is being built, and not as a value standing in a place: the
    // group is the whole of what the type matched, and the type's own places
    // are the groups inside it -- which in this array, which begins at the
    // first group, are the entries from this one on. Read as a value, a shape
    // of two numbers was handed "(3,-4)" where it wanted "3".
    using held = collector_value_t<Collector>;
    return scan::or_thrown(
        build_value<failure_for<held>, no_parameters, held, Group, true>(
            groups));
  } else {
    return one.parse(found.template get<Group>().to_view(),
                         std::string_view{});
  }
}

}  // namespace detail

// A match whose groups are collected, each by its own collector.
template <fixed_string Pattern, class HeldType, class... Collectors>
struct collected_match_closure
    : std::ranges::range_adaptor_closure<
          collected_match_closure<Pattern, HeldType, Collectors...>> {
  constexpr explicit collected_match_closure(Collectors... given)
      : collectors_(std::move(given)...) {}

  template <detail::contiguous_char_range RangeType>
  [[nodiscard]] constexpr auto operator()(RangeType&& input) const {
    using holder = std::string_view;
    using result_type =
        typed_result<holder, detail::collected_type<Collectors, holder>...>;
    const auto found = detail::regex_match<Pattern>(detail::characters_of(input));
    if (!found) return result_type{};
    return build<holder, result_type>(
        found, std::make_index_sequence<sizeof...(Collectors)>{});
  }

  // Off input that arrives in pieces: each piece read in words and vectors,
  // and the collectors gathering as the walk goes through them.
  template <detail::piecewise_char_range PiecesType>
    requires(!detail::contiguous_char_range<PiecesType> &&
             !std::same_as<std::ranges::range_value_t<PiecesType>, char>)
  [[nodiscard]] constexpr auto operator()(PiecesType&& input) const {
    constexpr const auto& automaton = detail::regex_automaton<Pattern>;
    using holder = skipped;
    using result_type =
        typed_result<holder, detail::collected_type<Collectors, HeldType>...>;

    detail::register_file<std::ptrdiff_t, automaton.register_count> registers{};
    registers.fill(scan::tre::negative_tag);
    detail::execute_commands(automaton.initialize,
                             automaton.initialize.size(), registers,
                             std::ptrdiff_t{0});
    auto states = beginning(std::make_index_sequence<sizeof...(Collectors)>{});
    auto view = std::views::all(std::forward<PiecesType>(input));
    detail::gathers_from_pieces<gathering_into<decltype(states)>,
                                decltype(view)>
        into(gathering_into<decltype(states)>(*this, states), std::move(view));

    const char* cursor = nullptr;
    const char* last = nullptr;
    std::ptrdiff_t place = 0;
    detail::walk_answer<const char*> best;
    constexpr detail::walk_shape shape{.in_words = true};
    if (!detail::run_continuation<automaton, shape, automaton.initial, std::ptrdiff_t>(
            cursor, last, place, registers, into, best)) {
      return result_type{};
    }
    return result_type{
        basic_submatch<holder>(skipped{}),
        finishing(std::move(states),
                  std::make_index_sequence<sizeof...(Collectors)>{})};
  }

  // Walked by the same code that walks characters in a row, with the
  // collectors gathering as it goes.
  //
  // Nothing is kept but what is asked for: a group collected into a number is
  // a number being read, and the subject is never held anywhere.
  template <detail::read_once_char_range RangeType>
  [[nodiscard]] constexpr auto operator()(RangeType&& input) const {
    static_assert(
        detail::accepts_where_the_first_reading_gathers<Pattern>(),
        "this pattern accepts by a reading that holds a group in other "
        "registers than the first reading does, and a subject that arrives as "
        "it is read is gathered into one place per group: what came back "
        "would be the gathering of a reading that did not win");
    constexpr const auto& automaton = detail::regex_automaton<Pattern>;
    // The whole of the match is not kept: there is nothing left behind to
    // point at, and nobody asked for it -- what was asked for is what the
    // collectors say. So the type says so too, and asking for the text of the
    // whole match here does not compile rather than coming back empty.
    using holder = skipped;
    using result_type =
        typed_result<holder, detail::collected_type<Collectors, HeldType>...>;

    detail::register_file<std::ptrdiff_t, automaton.register_count> registers{};
    registers.fill(scan::tre::negative_tag);
    detail::execute_commands(automaton.initialize,
                             automaton.initialize.size(), registers,
                             std::ptrdiff_t{0});
    auto states = beginning(std::make_index_sequence<sizeof...(Collectors)>{});
    gathering_into<decltype(states)> into(*this, states);

    auto cursor = std::ranges::begin(input);
    std::ptrdiff_t position = 0;
    detail::walk_answer<decltype(cursor)> best;
    // The same walk as every other, and the plain shape of it: a subject
    // handed over a character at a time cannot have the vectors, because
    // there is nothing in a row to read.
    constexpr detail::walk_shape once{};
    if (!detail::run_continuation<automaton, once, automaton.initial, std::ptrdiff_t>(
            cursor, std::ranges::end(input), position, registers, into,
            best)) {
      return result_type{};
    }
    return result_type{
        basic_submatch<holder>(skipped{}),
        finishing(std::move(states),
                  std::make_index_sequence<sizeof...(Collectors)>{})};
  }


  // What the walk hands characters to: the collectors of the groups that are
  // open where it stands.
  //
  // The state is where the walk stands in its own code, so the registers a
  // group is held in are constants here, and asking whether it is open is two
  // loads and a comparison -- not a lookup of the state, then of its readings,
  // then of the registers.
  template <class StatesType>
  class gathering_into {
   public:
    using owner_type = collected_match_closure;

    constexpr gathering_into(const collected_match_closure& owner,
                             StatesType& states)
        : owner_(owner), states_(states) {}

    // Nothing to do before a move writes: what happened to a group is read off
    // the positions afterwards, which is the only way that answers the same in
    // both layers.
    template <std::size_t State, std::size_t Move, class RegistersType,
              class Mark>
    constexpr void moving(const RegistersType&, Mark) {}

    // A run stepped over in vectors: whatever is open takes all of it.
    template <std::size_t State, class RegistersType, class Mark>
    constexpr void took_run(const char* from, const char* to,
                            const RegistersType& registers, Mark) {
      // Nothing is written inside a run, so what is open at one end of it is
      // open at the other: the edges are told once and the characters go in as
      // they come.
      for (const char* letter = from; letter != to; ++letter) {
        fold_all<State>(*letter, true, registers,
                        std::make_index_sequence<sizeof...(Collectors)>{});
      }
      hand_run<State>(from, to, registers,
                      std::make_index_sequence<sizeof...(Collectors)>{});
    }

    // A move was made: whatever groups are open where it lands take the
    // character.
    //
    // Which registers a group is held in are constants at the state the walk
    // stands in, so this is two loads and a comparison rather than a lookup of
    // the state, then of its readings, then of the registers.
    template <std::size_t State, std::size_t Landed, std::size_t Move,
              class RegistersType>
    constexpr void moved(char letter, const RegistersType& registers,
                         std::ptrdiff_t) {
      fold_all<Landed>(letter, true, registers,
                       std::make_index_sequence<sizeof...(Collectors)>{});
      hand_all<Landed>(letter, registers,
                       std::make_index_sequence<sizeof...(Collectors)>{});
    }

    // And the match ending ends the turn that was going. Asked with the
    // positions as they stand and no character to hand over, which is the same
    // step the characters run through.
    template <std::size_t State, class RegistersType>
    constexpr void ended(const RegistersType& registers) {
      fold_all<State>(char{}, false, registers,
                      std::make_index_sequence<sizeof...(Collectors)>{});
    }

   private:
    // Which of the type's own groups is open, and the character to it.
    //
    // They are groups of this match like any others, sitting straight after
    // the one written with the type's pattern, so the same two registers say
    // whether each is open.
    template <std::size_t Landed, std::size_t Group, class RegistersType,
              std::size_t... Inside>
    constexpr void hand_inner(char letter, bool hands_the_character,
                              const RegistersType& registers,
                              std::index_sequence<Inside...>) {
      // Opened first and in the order they are written, then the character to
      // whatever is inside, then the closings innermost first -- the same step,
      // in the same order, as a fold read by the format layer.
      (open_inner<Landed, Group, Inside>(registers), ...);
      if (hands_the_character) {
        (push_inner<Landed, Group, Inside>(letter, registers), ...);
      }
      [&]<std::size_t... step>(std::index_sequence<step...>) {
        (close_inner<Landed, Group, sizeof...(Inside) - 1 - step>(registers),
         ...);
      }(std::make_index_sequence<sizeof...(Inside)>{});
    }

    // Where a group of the type stands in the groups of this match, and
    // whether the pattern has it at all.
    template <std::size_t Group, std::size_t Inside>
    static constexpr std::size_t theirs_at =
        owner_type::template group_of<Group>() + 1 + Inside;

    template <std::size_t Group, std::size_t Inside>
    static constexpr bool inside_the_pattern =
        theirs_at<Group, Inside> * 2 + 1 <
        detail::regex_automaton<Pattern>.tag_count;

    // A turn is known by the position its group opened at: positions only move
    // forward, so an opening that has moved is a turn that has begun. Told once
    // per turn, however many characters the walk hands over inside it.
    template <std::size_t Landed, std::size_t Group, std::size_t Inside,
              class RegistersType>
    constexpr void open_inner(const RegistersType& registers) {
      if constexpr (inside_the_pattern<Group, Inside>) {
        using collector = std::tuple_element_t<Group, std::tuple<Collectors...>>;
        using held = std::remove_cv_t<detail::collector_value_t<collector>>;
        constexpr std::size_t theirs = theirs_at<Group, Inside>;
        constexpr const auto& entered =
            detail::regex_automaton<Pattern>.states[Landed];
        constexpr std::uint32_t opening = entered.readings[0][theirs * 2];
        const auto began = slot_read(registers, opening);
        if (began < 0 || told_at_[theirs] == began) return;
        detail::open_one_group<held, Inside>(std::get<Group>(states_));
        told_at_[theirs] = began;
        open_[theirs] = true;
      }
    }

    template <std::size_t Landed, std::size_t Group, std::size_t Inside,
              class RegistersType>
    constexpr void push_inner(char letter, const RegistersType& registers) {
      if constexpr (inside_the_pattern<Group, Inside>) {
        using collector = std::tuple_element_t<Group, std::tuple<Collectors...>>;
        using held = std::remove_cv_t<detail::collector_value_t<collector>>;
        constexpr std::size_t theirs = theirs_at<Group, Inside>;
        constexpr const auto& entered =
            detail::regex_automaton<Pattern>.states[Landed];
        constexpr std::uint32_t opening = entered.readings[0][theirs * 2];
        constexpr std::uint32_t closing = entered.readings[0][theirs * 2 + 1];
        if (!open_[theirs]) return;
        if (slot_read(registers, closing) >= slot_read(registers, opening)) return;
        detail::push_one_group<held, Inside>(std::get<Group>(states_), letter);
      }
    }

    template <std::size_t Landed, std::size_t Group, std::size_t Inside,
              class RegistersType>
    constexpr void close_inner(const RegistersType& registers) {
      if constexpr (inside_the_pattern<Group, Inside>) {
        using collector = std::tuple_element_t<Group, std::tuple<Collectors...>>;
        using held = std::remove_cv_t<detail::collector_value_t<collector>>;
        constexpr std::size_t theirs = theirs_at<Group, Inside>;
        constexpr const auto& entered =
            detail::regex_automaton<Pattern>.states[Landed];
        constexpr std::uint32_t opening = entered.readings[0][theirs * 2];
        constexpr std::uint32_t closing = entered.readings[0][theirs * 2 + 1];
        if (!open_[theirs]) return;
        if (slot_read(registers, closing) < slot_read(registers, opening)) return;
        detail::close_one_group<held, Inside>(std::get<Group>(states_));
        open_[theirs] = false;
      }
    }

    // Every collector, told what this step did to the groups it reads.
    template <std::size_t Landed, class RegistersType, std::size_t... Group>
    constexpr void fold_all(char letter, bool hands_the_character,
                            const RegistersType& registers,
                            std::index_sequence<Group...>) {
      (fold_one<Landed, Group>(letter, hands_the_character, registers), ...);
    }

    template <std::size_t Landed, std::size_t Group, class RegistersType>
    constexpr void fold_one(char letter, bool hands_the_character,
                            const RegistersType& registers) {
      using collector = std::tuple_element_t<Group, std::tuple<Collectors...>>;
      if constexpr (requires { typename collector::value_type; }) {
        using held = std::remove_cv_t<detail::collector_value_t<collector>>;
        if constexpr (owner_type::template gathers_its_own_groups<Group>()) {
          hand_inner<Landed, Group>(
              letter, hands_the_character, registers,
              std::make_index_sequence<detail::groups_a_leaf_opens<held>()>{});
        }
      }
    }

    template <std::size_t Landed, class RegistersType, std::size_t... Group>
    constexpr void hand_run(const char* from, const char* to,
                            const RegistersType& registers,
                            std::index_sequence<Group...>) {
      (hand_run_group<Landed, Group>(from, to, registers), ...);
    }

    template <std::size_t Landed, std::size_t Group, class RegistersType>
    constexpr void hand_run_group(const char* from, const char* to,
                                  const RegistersType& registers) {
      using collector =
          std::tuple_element_t<Group, std::tuple<Collectors...>>;
      if constexpr (requires { collector::takes_nothing; }) {
        return;
      } else {
        constexpr const auto& entered =
            detail::regex_automaton<Pattern>.states[Landed];
        if constexpr (entered.reading_count != 0) {
          constexpr std::size_t where = owner_type::template group_of<Group>();
          constexpr std::uint32_t opening = entered.readings[0][where * 2];
          constexpr std::uint32_t closing = entered.readings[0][where * 2 + 1];
          if (slot_read(registers, opening) < 0) return;
          if (slot_read(registers, closing) >= slot_read(registers, opening)) return;
          if constexpr (owner_type::template gathers_its_own_groups<Group>()) {
            // Told by the step above, which asks the positions rather than
            // whether the group around it happens to be open here.
            return;
          } else if constexpr (requires {
                                 std::get<Group>(owner_.collectors_)
                                     .push(std::get<Group>(states_),
                                               std::string_view{});
                               }) {
            // The walk stepped over this run in vectors, and a collector that
            // takes a run takes it in one go rather than in as many calls as
            // there are characters.
            std::get<Group>(owner_.collectors_)
                .push(std::get<Group>(states_),
                          std::string_view(from,
                                           static_cast<std::size_t>(to - from)));
          } else {
            for (const char* letter = from; letter != to; ++letter) {
              std::get<Group>(owner_.collectors_)
                  .push(std::get<Group>(states_), *letter);
            }
          }
        }
      }
    }

    template <std::size_t Landed, class RegistersType, std::size_t... Group>
    constexpr void hand_all(char letter, const RegistersType& registers,
                            std::index_sequence<Group...>) {
      (hand_group<Landed, Group>(letter, registers), ...);
    }

    template <std::size_t Landed, std::size_t Group, class RegistersType>
    constexpr void hand_group(char letter,
                              const RegistersType& registers) {
      using collector =
          std::tuple_element_t<Group, std::tuple<Collectors...>>;
      if constexpr (requires { collector::takes_nothing; }) {
        return;
      } else {
        constexpr const auto& entered =
            detail::regex_automaton<Pattern>.states[Landed];
        if constexpr (entered.reading_count == 0) {
          return;
        } else {
          constexpr std::size_t where = owner_type::template group_of<Group>();
          constexpr std::uint32_t opening = entered.readings[0][where * 2];
          constexpr std::uint32_t closing = entered.readings[0][where * 2 + 1];
          if (slot_read(registers, opening) < 0) return;
          if (slot_read(registers, closing) >= slot_read(registers, opening)) return;
          if constexpr (owner_type::template gathers_its_own_groups<
                            Group>()) {
            // Told by the step above.
            return;
          } else {
            std::get<Group>(owner_.collectors_)
                .push(std::get<Group>(states_), letter);
          }
        }
      }
    }

    const collected_match_closure& owner_;
    StatesType& states_;
    // Where each group of this match last opened, so a turn is told once, and
    // whether it is open now.
    std::array<std::ptrdiff_t,
               detail::regex_automaton<Pattern>.tag_count / 2 + 1>
        told_at_ = [] {
          std::array<std::ptrdiff_t,
                     detail::regex_automaton<Pattern>.tag_count / 2 + 1>
              made{};
          for (auto& one : made) one = -1;
          return made;
        }();
    // Which of the groups of this match are open, by the number of the group.
    // A turn is ended by the opening of the next one, and the walk is what
    // sees that, so what has been opened has to be remembered here.
    std::array<bool, detail::regex_automaton<Pattern>.tag_count / 2 + 1> open_{};
  };

  template <std::size_t... Group>
  [[nodiscard]] constexpr auto beginning(std::index_sequence<Group...>) const {
    return std::tuple{begin_one<Group>()...};
  }

  // Which group of the match each collector reads.
  //
  // One each, in the order they were written -- until one of them reads a type
  // out of the groups inside its own. Those groups belong to that type, so the
  // collector after it starts past them, and nobody has to write `skip()` for
  // groups that were never theirs to skip:
  //
  //   into(as<version>())   over   v=(([0-9]+)\.([0-9]+)\.([0-9]+))!
  //
  // is one collector over four groups.
  template <std::size_t Which, std::size_t Where>
  [[nodiscard]] static consteval std::size_t swallowed() {
    using collector = std::tuple_element_t<Which, std::tuple<Collectors...>>;
    if constexpr (requires { typename collector::value_type; }) {
      using held = std::remove_cv_t<detail::collector_value_t<collector>>;
      if constexpr (detail::group_gathers_by_group<held, Pattern, Where + 1>() ||
                    detail::group_is_the_types_pattern<held, Pattern,
                                                       Where + 1>()) {
        return detail::groups_a_leaf_opens<held>();
      } else if constexpr (detail::group_spells_out<held, Pattern,
                                                    Where + 1>()) {
        // A type that declares a format spells its places out as groups, and
        // those are its too -- its places, and not the group they were
        // written inside, which the caller counts for itself. Counted with it,
        // the next collector was handed the group after the one it wanted.
        return detail::groups_a_leaf_opens<held>();
      } else {
        return 0;
      }
    } else {
      return 0;
    }
  }

  template <std::size_t Which>
  [[nodiscard]] static consteval std::size_t group_of() {
    if constexpr (Which == 0) {
      return 0;
    } else {
      constexpr std::size_t before = group_of<Which - 1>();
      return before + 1 + swallowed<Which - 1, before>();
    }
  }

  // Whether the collector at this place is a type that gathers by its own
  // groups, and whether its group is written as that type's own pattern. Both
  // have to hold: the type asks for it, and the pattern gives it something to
  // ask about.
  template <std::size_t Group>
  [[nodiscard]] static consteval bool gathers_its_own_groups() {
    using collector = std::tuple_element_t<Group, std::tuple<Collectors...>>;
    if constexpr (requires { typename collector::value_type; }) {
      return detail::group_gathers_by_group<typename collector::value_type,
                                            Pattern, group_of<Group>() + 1>();
    } else {
      return false;
    }
  }

  template <std::size_t Group>
  [[nodiscard]] constexpr auto begin_one() const {
    using collector =
        std::tuple_element_t<Group, std::tuple<Collectors...>>;
    if constexpr (requires { collector::takes_nothing; }) {
      return HeldType{};
    } else if constexpr (requires {
                           std::declval<const collector&>()
                               .template begin<HeldType>(
                                   std::string_view{});
                         }) {
      return std::get<Group>(collectors_)
          .template begin<HeldType>(std::string_view{});
    } else if constexpr (gathers_its_own_groups<Group>()) {
      return scan::scanner<std::remove_cv_t<
          typename collector::value_type>>{}.begin_groups();
    } else {
      return std::get<Group>(collectors_).begin(std::string_view{});
    }
  }

  template <class StatesType, class RegistersType, std::size_t... Group>
  constexpr void offer(StatesType& states, std::size_t here,
                       const RegistersType& registers, char letter,
                       std::index_sequence<Group...>) const {
    (offer_one<Group>(states, here, registers, letter), ...);
  }

  template <std::size_t Group, class StatesType, class RegistersType>
  constexpr void offer_one(StatesType& states, std::size_t here,
                           const RegistersType& registers,
                           char letter) const {
    using collector =
        std::tuple_element_t<Group, std::tuple<Collectors...>>;
    if constexpr (requires { collector::takes_nothing; }) {
      return;
    } else {
      if (!detail::group_is_open<Pattern, group_of<Group>()>(here, registers)) {
        return;
      }
      std::get<Group>(collectors_).push(std::get<Group>(states), letter);
    }
  }

  template <class StatesType, std::size_t... Group>
  [[nodiscard]] constexpr auto finishing(StatesType states,
                                         std::index_sequence<Group...>) const {
    return std::tuple<detail::collected_type<Collectors, HeldType>...>{
        finish_one<Group>(std::move(std::get<Group>(states)))...};
  }

  template <std::size_t Group, class StateType>
  [[nodiscard]] constexpr auto finish_one(StateType state) const {
    using collector =
        std::tuple_element_t<Group, std::tuple<Collectors...>>;
    if constexpr (requires { collector::takes_nothing; }) {
      return skipped{};
    } else if constexpr (gathers_its_own_groups<Group>()) {
      return scan::scanner<std::remove_cv_t<
          typename collector::value_type>>{}.finish_groups(std::move(state));
    } else {
      return std::get<Group>(collectors_).finish(std::move(state));
    }
  }

 private:
  template <class Holder, class ResultType, class FoundType,
            std::size_t... Group>
  [[nodiscard]] constexpr ResultType build(
      const FoundType& found, std::index_sequence<Group...>) const {
    return ResultType{
        basic_submatch<Holder>(found.whole()),
        std::tuple<detail::collected_type<Collectors, Holder>...>{
            detail::collect_one<
                Pattern, group_of<Group>() + 1,
                std::tuple_element_t<Group, std::tuple<Collectors...>>,
                Holder>(std::get<Group>(collectors_), found)...}};
  }

  template <class FoundType, std::size_t... Group>
  [[nodiscard]] constexpr auto build_values(
      const FoundType& found, std::index_sequence<Group...>) const {
    return std::tuple<
        detail::collected_type<Collectors, HeldType>...>{
        detail::collect_one<
            Pattern, Group + 1,
            std::tuple_element_t<Group, std::tuple<Collectors...>>, HeldType>(
            std::get<Group>(collectors_), found)...};
  }

  std::tuple<Collectors...> collectors_;
};

// The whole subject, matched.
//
// Three subjects and three answers. Characters that lie in a row are pointed
// at, and the match is the fast walk over them. Characters reached by walking
// are held as the pair of iterators they lie between, and the match is the
// plain walk of the same automaton. Characters that can only be read once are
// read into text of their own, and the answer owns it -- there is nothing left
// behind to point at.
// Everything a match is asked to be, said in one place.
//
// A reading has three things about it that are nobody's business but the
// caller's: what the answers are kept in, whether the subject carries a
// terminator the pattern can never match, and which walk reads it. Written as
// names they multiply -- `match`, `match_sentinel`, `match_scalar`,
// `match_sentinel_scalar`, and so on for every combination that will ever be
// wanted. Written as parameters with a method each, they compose:
//
//   scan::match<p>(text)
//   scan::match<p>.sentinel()(text)
//   scan::match<p>.sentinel().scalar()(text)
//   text | scan::match<p>.scalar().into<std::pmr::string>()
//
// Each method hands back the same reading with one thing said differently, so
// the order they are written in does not matter and nothing has to be named
// twice.
template <fixed_string Pattern, class HeldType = std::string,
          int Terminator = -1, how_to_walk Walk = how_to_walk::by_length>
struct match_closure
    : std::ranges::range_adaptor_closure<
          match_closure<Pattern, HeldType, Terminator, Walk>> {
  // The subject ends where it ends, and the walk tests that as well as the
  // character. This is the reading for a `string_view` into the middle of
  // something.
  [[nodiscard]] constexpr match_closure<Pattern, HeldType, -1, Walk> sized()
      const {
    return {};
  }

  // The subject carries a character the pattern can never match, so the walk
  // tests only the character. Whether the terminator really is there is the
  // caller's promise -- a `std::string` always has one; whether the pattern
  // can match it is asked while it is compiled.
  template <unsigned char Byte = 0>
  [[nodiscard]] constexpr match_closure<Pattern, HeldType, Byte, Walk>
  sentinel() const {
    return {};
  }

  // Which walk reads it: asked of the length, or said outright. Said outright,
  // the length is not looked at and the walk that was not named is not written.
  [[nodiscard]] constexpr match_closure<Pattern, HeldType, Terminator,
                                        how_to_walk::by_length>
  by_length() const {
    return {};
  }

  [[nodiscard]] constexpr match_closure<Pattern, HeldType, Terminator,
                                        how_to_walk::one_at_a_time>
  scalar() const {
    return {};
  }

  [[nodiscard]] constexpr match_closure<Pattern, HeldType, Terminator,
                                        how_to_walk::in_words>
  vec() const {
    return {};
  }

  // Where the answers are put, said rather than taken as it comes. What is
  // named here is what a subject read once is read into, and what its pieces
  // are handed back as.
  template <class Other>
  [[nodiscard]] constexpr match_closure<Pattern, Other, Terminator, Walk>
  into() const {
    return {};
  }

  // A collector for each group, in the order the groups were written. What
  // each of them is made with is its own business, so one group can be a
  // string with one allocator and the next a string with another.
  template <class... Collectors>
  [[nodiscard]] constexpr auto into(Collectors... given) const {
    return collected_match_closure<Pattern, HeldType, Collectors...>(
        std::move(given)...);
  }

  template <detail::contiguous_char_range RangeType>
  [[nodiscard]] constexpr auto operator()(RangeType&& input) const {
    const std::string_view text = detail::characters_of(input);
    if constexpr (Terminator < 0) {
      return detail::regex_match<Pattern, Walk>(text);
    } else {
      return detail::regex_match_sentinel<
          Pattern, static_cast<unsigned char>(Terminator), Walk>(text);
    }
  }

  template <detail::forward_char_range RangeType>
  [[nodiscard]] constexpr auto operator()(RangeType&& input) const {
    static_assert(detail::regex_automaton<Pattern>.tag_count == 0,
                  "a pattern that captures wants the subject in one piece: "
                  "read it into a string first");
    using holder = detail::walked_holder<RangeType>;
    const auto first = std::ranges::begin(input);
    const auto last = std::ranges::end(input);
    auto walking = first;
    if (!detail::matched_over<Pattern, detail::walk_shape{}>(walking, last)) {
      return basic_result<holder, 0>{};
    }
    return basic_result<holder, 0>{
        basic_submatch<holder>(holder(first, std::ranges::next(first, last))),
        {}};
  }

  // Off input that arrives in pieces: read in words and vectors inside a
  // piece, and the answer owns what it kept, because a piece is gone once the
  // walk has left it.
  template <detail::piecewise_char_range PiecesType>
    requires(!detail::contiguous_char_range<PiecesType> &&
             !std::same_as<std::ranges::range_value_t<PiecesType>, char>)
  [[nodiscard]] constexpr auto operator()(PiecesType&& input) const {
    constexpr const auto& automaton = detail::regex_automaton<Pattern>;
    HeldType held;
    auto view = std::views::all(std::forward<PiecesType>(input));
    detail::gathers_from_pieces<detail::keeps_into<HeldType>, decltype(view)>
        into(detail::keeps_into<HeldType>{held}, std::move(view));
    detail::register_file<std::ptrdiff_t, automaton.register_count> registers{};
    registers.fill(scan::tre::negative_tag);
    const char* cursor = nullptr;
    const char* last = nullptr;
    std::ptrdiff_t place = 0;
    detail::walk_answer<const char*> best;
    constexpr detail::walk_shape shape{.in_words = true};
    if (!detail::run_continuation<automaton, shape, automaton.initial, std::ptrdiff_t>(
            cursor, last, place, registers, into, best)) {
      return basic_result<HeldType, 0>{};
    }
    return basic_result<HeldType, 0>{
        basic_submatch<HeldType>(std::move(held)), {}};
  }

  // Walked by the same code that walks a list of characters, which is the
  // same code that walks characters in a row -- only the reading differs.
  //
  // A subject that arrives as it is read does not need a machine that can be
  // stopped and started: this call owns the loop, so the walk is written out
  // by the compiler as it is everywhere else, and the state is where it stands
  // in that code rather than a number to look up. What it cannot have is the
  // vectors, which want characters in a row.
  template <detail::read_once_char_range RangeType>
  [[nodiscard]] constexpr auto operator()(RangeType&& input) const {
    constexpr const auto& automaton = detail::regex_automaton<Pattern>;
    HeldType held;
    detail::keeps_into<HeldType> keep{held};
    detail::register_file<std::ptrdiff_t, automaton.register_count> registers{};
    registers.fill(scan::tre::negative_tag);
    std::ptrdiff_t place = 0;
    auto cursor = std::ranges::begin(input);
    detail::walk_answer<decltype(cursor)> best;
    // The same walk as every other, and the plain shape of it: a subject
    // handed over a character at a time cannot have the vectors, because
    // there is nothing in a row to read.
    constexpr detail::walk_shape once{};
    if (!detail::run_continuation<automaton, once, automaton.initial, std::ptrdiff_t>(
            cursor, std::ranges::end(input), place, registers, keep, best)) {
      return basic_result<HeldType, 0>{};
    }
    return basic_result<HeldType, 0>{
        basic_submatch<HeldType>(std::move(held)), {}};
  }
};

template <fixed_string Pattern>
inline constexpr match_closure<Pattern> match{};

// The head of the subject the pattern takes.
template <fixed_string Pattern, class HeldType = std::string>
struct starts_with_closure
    : std::ranges::range_adaptor_closure<
          starts_with_closure<Pattern, HeldType>> {
  template <class Other>
  [[nodiscard]] constexpr starts_with_closure<Pattern, Other> into() const {
    return {};
  }

  template <detail::contiguous_char_range RangeType>
  [[nodiscard]] constexpr auto operator()(RangeType&& input) const {
    return detail::regex_starts_with<Pattern>(detail::characters_of(input));
  }

  template <detail::forward_char_range RangeType>
  [[nodiscard]] constexpr auto operator()(RangeType&& input) const {
    static_assert(detail::regex_automaton<Pattern>.tag_count == 0,
                  "a pattern that captures wants the subject in one piece: "
                  "read it into a string first");
    using holder = detail::walked_holder<RangeType>;
    auto walking = std::ranges::begin(input);
    const auto first = walking;
    detail::walk_answer<decltype(walking)> best;
    const bool found = detail::walk_over<Pattern, detail::head_shape<Pattern>()>(
        walking, std::ranges::end(input), best);
    if (!found) return basic_result<holder, 0>{};
    return basic_result<holder, 0>{
        basic_submatch<holder>(holder(first, *best.at)), {}};
  }

  // The head of a subject read once.
  //
  // The longest head is the last place the machine stood in an accepting
  // state, and finding it means reading past that place -- and where the
  // machine then dies, giving those characters back. There is nowhere to give
  // them back to here, so this is only for a pattern that cannot go on once it
  // has a match: then where it stops is where the head ends, and nothing is
  // held.
  template <detail::read_once_char_range RangeType>
  [[nodiscard]] constexpr auto operator()(RangeType&& input) const {
    static_assert(
        detail::fallback_window<Pattern>() == 0,
        "a subject that can only be read once has nowhere to give back the "
        "characters read past the head, and this pattern can walk away from a "
        "match without finding another one: it would have to hold them");
    constexpr const auto& automaton = detail::regex_automaton<Pattern>;
    HeldType held;
    std::size_t here = automaton.initial;
    auto cursor = std::ranges::begin(input);
    const auto last = std::ranges::end(input);
    for (; cursor != last; ++cursor) {
      const unsigned char symbol = static_cast<unsigned char>(*cursor);
      const std::size_t run = detail::run_taken<detail::regex_automaton<Pattern>>(here, symbol);
      if (run == detail::no_run) break;
      here = automaton.states[here].ranges[run].target;
      held.push_back(static_cast<char>(symbol));
    }
    if (automaton.states[here].accepting_slot ==
        detail::packed_state<0, 0, 0>::not_accepting) {
      return basic_result<HeldType, 0>{};
    }
    return basic_result<HeldType, 0>{
        basic_submatch<HeldType>(std::move(held)), {}};
  }
};

template <fixed_string Pattern>
inline constexpr starts_with_closure<Pattern> starts_with{};

// The leftmost match. Only over characters that are already all there: finding
// it asks the subject about places it has been past, which a subject that can
// only be read once cannot answer.
template <fixed_string Pattern>
struct search_closure
    : std::ranges::range_adaptor_closure<search_closure<Pattern>> {
  template <detail::contiguous_char_range RangeType>
  [[nodiscard]] constexpr auto operator()(RangeType&& input) const {
    return detail::regex_search<Pattern>(detail::characters_of(input));
  }
};

template <fixed_string Pattern>
inline constexpr search_closure<Pattern> search{};

// One match after another, found as they are asked for.
//
// Nothing is collected: the view holds where to look next and finds the next
// match when the loop asks for it. What was here built a vector of every match
// in the input before the caller had looked at the first one.
template <fixed_string Pattern>
class search_view {
 public:
  using result_type = detail::regex_result_for<Pattern>;

  constexpr explicit search_view(std::string_view input) : input_(input) {}

  class iterator {
   public:
    using value_type = result_type;
    using difference_type = std::ptrdiff_t;

    constexpr iterator() = default;
    constexpr explicit iterator(std::string_view input) : rest_(input) {
      seek();
    }

    [[nodiscard]] constexpr const result_type& operator*() const {
      return found_;
    }
    [[nodiscard]] constexpr const result_type* operator->() const {
      return &found_;
    }
    constexpr iterator& operator++() {
      // A match of nothing would be found again in the same place, so the
      // search moves on by a character where it took none.
      const std::size_t step = std::max(found_.size(), std::size_t{1});
      const auto begin =
          static_cast<std::size_t>(found_.data() - rest_.data());
      rest_ = rest_.substr(std::min(begin + step, rest_.size()));
      seek();
      return *this;
    }
    constexpr iterator operator++(int) {
      iterator held = *this;
      ++*this;
      return held;
    }
    [[nodiscard]] constexpr bool operator==(std::default_sentinel_t) const {
      return !static_cast<bool>(found_);
    }

   private:
    constexpr void seek() { found_ = detail::regex_search<Pattern>(rest_); }

    std::string_view rest_;
    result_type found_{};
  };

  [[nodiscard]] constexpr iterator begin() const { return iterator(input_); }
  [[nodiscard]] constexpr std::default_sentinel_t end() const { return {}; }

 private:
  std::string_view input_;
};

// Said as a name rather than called, so that it can be either.
//
// Each of these is an object with a call in it, and one that the pipe of the
// ranges library knows how to hand a subject to. `search_all<p>(text)` is the
// call; `text | search_all<p>` is the same thing said the other way round, and
// both come from writing it once.
// One match after another off a subject that can only be read once.
//
// Nothing here holds the answer. The characters of a match go into the
// caller's collector as the machine becomes sure of them, one at a time, and
// the characters before it go into whatever the caller wanted done with those
// -- thrown away by a search, kept as the piece by a split.
//
// What is held is only what the machine is not yet sure of, and there are
// exactly two such things, both of them numbers the pattern names while it is
// compiled:
//
//   * the characters of an attempt that has not accepted anything yet, which
//     have to be given back when it dies, because the attempt that starts one
//     character later needs them -- at most `dead_end_window` of them;
//   * the characters read past the last accepting place, which have to be
//     given back when the machine dies past a match -- at most
//     `fallback_window` of them.
//
// So the holding is a handful of characters no matter how long the subject or
// the answer is. For `\s+` both numbers are zero and nothing is ever held at
// all: every space is already a whole match, and the first character that is
// not a space was never taken.
template <fixed_string Pattern, class RangeType>
class read_once_finder {
 public:
  static constexpr std::size_t dead_end = detail::dead_end_window<Pattern>();
  static constexpr std::size_t past_match = detail::fallback_window<Pattern>();
  // Where the pattern names no number the caller is told so; the size here is
  // only kept sane so that the telling is what they see.
  static constexpr std::size_t hold =
      detail::holds_a_bounded_way<Pattern>()
          ? (dead_end > past_match ? dead_end : past_match) + 2
          : 2;

  constexpr explicit read_once_finder(RangeType input)
      : input_(std::move(input)) {}

  read_once_finder(read_once_finder&&) = default;
  read_once_finder& operator=(read_once_finder&&) = default;
  read_once_finder(const read_once_finder&) = delete;
  read_once_finder& operator=(const read_once_finder&) = delete;

  // The next match, leftmost and as long as it goes. What comes before it is
  // pushed into `skipped`, what it is made of into `made`. False where there
  // is no next match: whatever was left of the subject has gone into
  // `skipped` by then.
  template <class SkippedType, class MadeType>
  constexpr bool next(SkippedType& skipped, MadeType& made) {
    constexpr const auto& automaton = detail::regex_automaton<Pattern>;
    while (true) {
      std::size_t here = automaton.initial;
      std::size_t taken = 0;   // how much of the match is already in `made`
      std::array<char, hold> ahead{};  // read since the last accepting place
      std::size_t ahead_count = 0;
      char symbol = 0;
      while (take(symbol)) {
        const std::size_t run =
            detail::run_taken<automaton>(here,
                                         static_cast<unsigned char>(symbol));
        if (run == detail::no_run) {
          // Dead. What was read since the last accepting place was not part of
          // anything, and neither was this character.
          give_back(ahead, ahead_count, symbol);
          ahead_count = 0;
          break;
        }
        here = automaton.states[here].ranges[run].target;
        ahead[ahead_count++] = symbol;
        if (automaton.states[here].accepting_slot !=
            detail::packed_state<0, 0, 0>::not_accepting) {
          // Sure of these now, so they go where the answer goes and are gone
          // from here.
          for (std::size_t at = 0; at < ahead_count; ++at) {
            made.push_back(ahead[at]);
          }
          taken += ahead_count;
          ahead_count = 0;
        }
      }
      // The subject may have ended in the middle of what was being read; that
      // is a death like any other.
      if (ahead_count != 0) {
        give_back(ahead, ahead_count);
        ahead_count = 0;
      }
      if (taken != 0) return true;
      // Nothing began here, so this character is not part of any match and the
      // next attempt starts one later.
      char first = 0;
      if (!take(first)) return false;
      skipped.push_back(first);
    }
  }

 private:
  constexpr bool take(char& symbol) {
    if (count_ != 0) {
      symbol = queue_[0];
      for (std::size_t at = 1; at < count_; ++at) queue_[at - 1] = queue_[at];
      --count_;
      return true;
    }
    if (!cursor_) cursor_.emplace(std::ranges::begin(input_));
    if (*cursor_ == std::ranges::end(input_)) return false;
    symbol = **cursor_;
    ++*cursor_;
    return true;
  }

  // Put in front of the reading, in the order they were read. What is given
  // back is exactly what this attempt took, so the queue never holds more than
  // it held when the attempt began.
  constexpr void give_back(const std::array<char, hold>& many,
                           std::size_t how_many) {
    for (std::size_t at = count_; at != 0; --at) {
      queue_[at - 1 + how_many] = queue_[at - 1];
    }
    for (std::size_t at = 0; at < how_many; ++at) queue_[at] = many[at];
    count_ += how_many;
  }

  constexpr void give_back(const std::array<char, hold>& many,
                           std::size_t how_many, char last) {
    const std::size_t all = how_many + 1;
    for (std::size_t at = count_; at != 0; --at) {
      queue_[at - 1 + all] = queue_[at - 1];
    }
    for (std::size_t at = 0; at < how_many; ++at) queue_[at] = many[at];
    queue_[how_many] = last;
    count_ += all;
  }

  RangeType input_;
  std::optional<std::ranges::iterator_t<RangeType>> cursor_;
  std::array<char, hold + 2> queue_{};
  std::size_t count_ = 0;
};

// A place to push characters that are not wanted.
struct nowhere {
  constexpr void push_back(char) const noexcept {}
};

template <fixed_string Pattern, class HeldType, class RangeType>
class read_once_search_view {
 public:
  constexpr explicit read_once_search_view(RangeType input)
      : finder_(std::move(input)) {}

  read_once_search_view(read_once_search_view&&) = default;
  read_once_search_view& operator=(read_once_search_view&&) = default;
  read_once_search_view(const read_once_search_view&) = delete;
  read_once_search_view& operator=(const read_once_search_view&) = delete;

  class iterator {
   public:
    using value_type = basic_submatch<HeldType>;
    using difference_type = std::ptrdiff_t;

    constexpr iterator() = default;
    constexpr explicit iterator(read_once_search_view& owner) : owner_(&owner) {
      seek();
    }

    [[nodiscard]] constexpr const value_type& operator*() const {
      return found_;
    }
    constexpr iterator& operator++() {
      seek();
      return *this;
    }
    constexpr void operator++(int) { ++*this; }
    [[nodiscard]] constexpr bool operator==(std::default_sentinel_t) const {
      return owner_ == nullptr || !static_cast<bool>(found_);
    }

   private:
    constexpr void seek() {
      HeldType made;
      nowhere dropped;
      if (owner_->finder_.next(dropped, made)) {
        found_ = value_type(std::move(made));
      } else {
        found_ = value_type{};
      }
    }

    read_once_search_view* owner_ = nullptr;
    value_type found_{};
  };

  [[nodiscard]] constexpr iterator begin() { return iterator(*this); }
  [[nodiscard]] constexpr std::default_sentinel_t end() const { return {}; }

 private:
  friend class iterator;

  read_once_finder<Pattern, RangeType> finder_;
};

// The pieces between those matches, off the same kind of subject. The piece is
// the answer and it is the caller's collector; the search that finds its end
// holds a handful of characters and nothing more.
template <fixed_string Pattern, class HeldType, class RangeType>
class read_once_split_view {
 public:
  constexpr explicit read_once_split_view(RangeType input)
      : finder_(std::move(input)) {}

  read_once_split_view(read_once_split_view&&) = default;
  read_once_split_view& operator=(read_once_split_view&&) = default;
  read_once_split_view(const read_once_split_view&) = delete;
  read_once_split_view& operator=(const read_once_split_view&) = delete;

  class iterator {
   public:
    using value_type = HeldType;
    using difference_type = std::ptrdiff_t;

    constexpr iterator() = default;
    constexpr explicit iterator(read_once_split_view& owner)
        : owner_(&owner), done_(false) {
      seek();
    }

    [[nodiscard]] constexpr const HeldType& operator*() const {
      return piece_;
    }
    constexpr iterator& operator++() {
      if (last_) {
        done_ = true;
        return *this;
      }
      seek();
      return *this;
    }
    constexpr void operator++(int) { ++*this; }
    [[nodiscard]] constexpr bool operator==(std::default_sentinel_t) const {
      return done_;
    }

   private:
    constexpr void seek() {
      piece_ = HeldType{};
      HeldType gap;
      // The piece is what comes before the delimiter, so it is written
      // straight into the answer while the delimiter is being looked for.
      if (!owner_->finder_.next(piece_, gap)) last_ = true;
    }

    read_once_split_view* owner_ = nullptr;
    HeldType piece_{};
    bool last_ = false;
    bool done_ = true;
  };

  [[nodiscard]] constexpr iterator begin() { return iterator(*this); }
  [[nodiscard]] constexpr std::default_sentinel_t end() const { return {}; }

 private:
  friend class iterator;

  read_once_finder<Pattern, RangeType> finder_;
};

// One match after another off input that arrives in pieces.
//
// Inside a piece the characters lie in a row, so a match that fits in one is
// found the way it is found in a string -- in words and vectors -- and handed
// back as a view into that piece: nothing is copied and nothing is allocated.
//
// A match that runs to the end of a piece may not have ended, because what
// follows in the next piece could be part of it. That one is put together in a
// window the view keeps and reuses -- one buffer for the whole reading, not
// one a match -- and handed back as a view into that.
//
// So what comes back points at the piece it was found in, or at the window; it
// is good until the next match is asked for, which is what a reading of
// something that arrives as it is read can promise.
template <fixed_string Pattern, class HeldType, class PiecesType>
class pieces_search_view {
 public:
  constexpr explicit pieces_search_view(PiecesType input)
      : pieces_(std::move(input)) {}

  pieces_search_view(pieces_search_view&&) = default;
  pieces_search_view& operator=(pieces_search_view&&) = default;
  pieces_search_view(const pieces_search_view&) = delete;
  pieces_search_view& operator=(const pieces_search_view&) = delete;

  class iterator {
   public:
    using value_type = basic_submatch<std::string_view>;
    using difference_type = std::ptrdiff_t;

    constexpr iterator() = default;
    constexpr explicit iterator(pieces_search_view& owner) : owner_(&owner) {
      owner_->seek();
    }

    [[nodiscard]] constexpr const value_type& operator*() const {
      return owner_->found_;
    }
    constexpr iterator& operator++() {
      owner_->seek();
      return *this;
    }
    constexpr void operator++(int) { ++*this; }
    [[nodiscard]] constexpr bool operator==(std::default_sentinel_t) const {
      return owner_ == nullptr || !static_cast<bool>(owner_->found_);
    }

   private:
    pieces_search_view* owner_ = nullptr;
  };

  [[nodiscard]] constexpr iterator begin() { return iterator(*this); }
  [[nodiscard]] constexpr std::default_sentinel_t end() const { return {}; }

 private:
  friend class iterator;
  using value_type = basic_submatch<std::string_view>;

  // The next piece with anything in it.
  constexpr bool refill() {
    if (!at_) at_.emplace(std::ranges::begin(pieces_));
    while (*at_ != std::ranges::end(pieces_)) {
      auto piece = **at_;
      ++*at_;
      const char* const first = std::ranges::data(piece);
      const auto size = std::ranges::size(piece);
      if (size == 0) continue;
      rest_ = std::string_view(first, size);
      return true;
    }
    rest_ = std::string_view{};
    return false;
  }

  constexpr void seek() {
    found_ = value_type{};
    while (true) {
      if (!held_.empty()) {
        if (seek_across()) return;
        continue;
      }
      if (rest_.empty() && !refill()) return;
      const auto taken = detail::regex_search<Pattern>(rest_);
      if (!taken) {
        // Nothing here, but what is at the end of this piece may still begin
        // something the next one finishes -- so the rest of it is held back.
        //
        // The whole of it, not some fixed number of characters: which of them
        // could still begin a match is what the automaton would have to be
        // asked, and holding the rest is the answer that needs no asking. It
        // grows to the distance between two matches, which is the scale of the
        // answers themselves.
        held_.append(rest_);
        rest_ = std::string_view{};
        continue;
      }
      const auto begins =
          static_cast<std::size_t>(taken.data() - rest_.data());
      const std::size_t ends = begins + taken.size();
      if (ends == rest_.size()) {
        // It runs to the end of the piece, so it may not have ended.
        held_.assign(rest_.substr(begins));
        rest_ = std::string_view{};
        continue;
      }
      // Found whole inside the piece: a view into it, and nothing else.
      found_ = value_type(rest_.substr(begins, taken.size()));
      rest_ = rest_.substr(ends);
      return;
    }
  }

  // A match that crosses a boundary, put together in the window this view
  // keeps and reuses.
  //
  // Filled a piece at a time until what is in it is settled: a match that ends
  // before the end of what is held has nothing more to gain from what follows.
  // Where the reading ends first, what is held is all there is.
  constexpr bool seek_across() {
    while (true) {
      const std::string_view sofar(held_.data(), held_.size());
      const auto ahead = detail::regex_search<Pattern>(sofar);
      const bool settled =
          ahead && static_cast<std::size_t>(ahead.data() - sofar.data()) +
                           ahead.size() <
                       sofar.size();
      if (settled) break;
      if (rest_.empty() && !refill()) break;
      held_.append(rest_);
      rest_ = std::string_view{};
    }
    const std::string_view over(held_.data(), held_.size());
    const auto taken = detail::regex_search<Pattern>(over);
    if (!taken) {
      held_.clear();
      return false;
    }
    const auto begins = static_cast<std::size_t>(taken.data() - over.data());
    const std::size_t ends = begins + taken.size();
    found_ = value_type(over.substr(begins, taken.size()));
    // What the window still holds stays in it, in front of what comes next.
    // The match that was just handed back points into the window, so it is
    // moved rather than dropped: it is good until the next one is asked for.
    kept_.assign(over.substr(ends));
    held_.swap(kept_);
    return true;
  }

  PiecesType pieces_;
  std::optional<std::ranges::iterator_t<PiecesType>> at_;
  std::string_view rest_;
  HeldType held_;
  HeldType kept_;
  value_type found_{};
};

template <fixed_string Pattern, class HeldType = std::string>
struct search_all_closure
    : std::ranges::range_adaptor_closure<
          search_all_closure<Pattern, HeldType>> {
  template <class Other>
  [[nodiscard]] constexpr search_all_closure<Pattern, Other> into() const {
    return {};
  }

  template <detail::contiguous_char_range RangeType>
  [[nodiscard]] constexpr search_view<Pattern> operator()(
      RangeType&& input) const {
    return search_view<Pattern>(detail::characters_of(input));
  }

  template <detail::read_once_char_range RangeType>
  [[nodiscard]] constexpr auto operator()(RangeType&& input) const {
    static_assert(
        detail::holds_a_bounded_way<Pattern>(),
        "a subject that can only be read once cannot be gone back over, so "
        "the characters of an attempt that comes to nothing have to be held "
        "until it does: a pattern that can swallow any number of them without "
        "matching, like `a+b`, would have to hold the whole subject");
    auto view = std::views::all(std::forward<RangeType>(input));
    return read_once_search_view<Pattern, HeldType, decltype(view)>(
        std::move(view));
  }

  // Off input that arrives in pieces: what fits in a piece is found in it and
  // costs no copy at all, and only what crosses a boundary is put together.
  template <detail::piecewise_char_range PiecesType>
    requires(!detail::contiguous_char_range<PiecesType> &&
             !std::same_as<std::ranges::range_value_t<PiecesType>, char>)
  [[nodiscard]] constexpr auto operator()(PiecesType&& input) const {
    auto view = std::views::all(std::forward<PiecesType>(input));
    return pieces_search_view<Pattern, HeldType, decltype(view)>(
        std::move(view));
  }
};

template <fixed_string Pattern>
inline constexpr search_all_closure<Pattern> search_all{};

template <fixed_string Pattern>
inline constexpr search_all_closure<Pattern> iterator{};

template <fixed_string Pattern>
inline constexpr search_all_closure<Pattern> tokenize{};

// The pieces between the matches, found as they are asked for.
template <fixed_string Pattern>
class split_view {
 public:
  constexpr explicit split_view(std::string_view input) : input_(input) {}

  class iterator {
   public:
    using value_type = std::string_view;
    using difference_type = std::ptrdiff_t;

    constexpr iterator() = default;
    constexpr explicit iterator(std::string_view input)
        : rest_(input), done_(false) {
      seek();
    }

    [[nodiscard]] constexpr const std::string_view& operator*() const {
      return piece_;
    }
    constexpr iterator& operator++() {
      if (last_) {
        done_ = true;
        return *this;
      }
      seek();
      return *this;
    }
    constexpr void operator++(int) { ++*this; }
    [[nodiscard]] constexpr bool operator==(std::default_sentinel_t) const {
      return done_;
    }

   private:
    constexpr void seek() {
      const auto delimiter = detail::regex_search<Pattern>(rest_);
      if (!delimiter) {
        piece_ = rest_;
        last_ = true;
        return;
      }
      const auto begin =
          static_cast<std::size_t>(delimiter.data() - rest_.data());
      piece_ = rest_.substr(0, begin);
      const std::size_t step = std::max(delimiter.size(), std::size_t{1});
      rest_ = rest_.substr(std::min(begin + step, rest_.size()));
    }

    std::string_view rest_;
    std::string_view piece_;
    bool last_ = false;
    bool done_ = true;
  };

  [[nodiscard]] constexpr iterator begin() const { return iterator(input_); }
  [[nodiscard]] constexpr std::default_sentinel_t end() const { return {}; }

 private:
  std::string_view input_;
};

// The pieces between the matches, off input that arrives in pieces.
//
// A piece that lies inside one of the input's own is handed back as a view
// into it, with nothing copied. One that runs across a boundary is put
// together in a buffer the view keeps and reuses -- and that one has to be
// put together, because it is the answer and the input's piece is gone.
template <fixed_string Pattern, class HeldType, class PiecesType>
class pieces_split_view {
 public:
  constexpr explicit pieces_split_view(PiecesType input)
      : pieces_(std::move(input)) {}

  pieces_split_view(pieces_split_view&&) = default;
  pieces_split_view& operator=(pieces_split_view&&) = default;
  pieces_split_view(const pieces_split_view&) = delete;
  pieces_split_view& operator=(const pieces_split_view&) = delete;

  class iterator {
   public:
    using value_type = std::string_view;
    using difference_type = std::ptrdiff_t;

    constexpr iterator() = default;
    constexpr explicit iterator(pieces_split_view& owner)
        : owner_(&owner), done_(false) {
      owner_->seek();
      done_ = owner_->done_;
    }

    [[nodiscard]] constexpr const std::string_view& operator*() const {
      return owner_->piece_;
    }
    constexpr iterator& operator++() {
      if (owner_->last_) {
        done_ = true;
        return *this;
      }
      owner_->seek();
      return *this;
    }
    constexpr void operator++(int) { ++*this; }
    [[nodiscard]] constexpr bool operator==(std::default_sentinel_t) const {
      return done_;
    }

   private:
    pieces_split_view* owner_ = nullptr;
    bool done_ = true;
  };

  [[nodiscard]] constexpr iterator begin() { return iterator(*this); }
  [[nodiscard]] constexpr std::default_sentinel_t end() const { return {}; }

 private:
  friend class iterator;

  constexpr bool refill() {
    if (!at_) at_.emplace(std::ranges::begin(pieces_));
    while (*at_ != std::ranges::end(pieces_)) {
      auto one = **at_;
      ++*at_;
      const char* const first = std::ranges::data(one);
      const auto size = std::ranges::size(one);
      if (size == 0) continue;
      rest_ = std::string_view(first, size);
      return true;
    }
    rest_ = std::string_view{};
    return false;
  }

  constexpr void seek() {
    piece_ = std::string_view{};
    held_.clear();
    bool gathering = false;
    while (true) {
      if (rest_.empty() && !refill()) {
        // The reading has ended: what has been gathered is the last piece.
        if (gathering) piece_ = std::string_view(held_.data(), held_.size());
        last_ = true;
        done_ = gave_one_ && !gathering && piece_.empty();
        gave_one_ = true;
        return;
      }
      if (!gathering) {
        const auto taken = detail::regex_search<Pattern>(rest_);
        const auto begins =
            taken ? static_cast<std::size_t>(taken.data() - rest_.data()) : 0;
        if (taken && begins + taken.size() < rest_.size()) {
          // A delimiter whole inside this piece: what comes before it is a
          // view into the piece, and nothing is copied.
          piece_ = rest_.substr(0, begins);
          rest_ = rest_.substr(begins + taken.size());
          gave_one_ = true;
          return;
        }
        // Either nothing here, or something that runs to the end and may not
        // have ended. Either way what is left of this piece is held, because
        // the piece itself will be gone.
        held_.append(rest_);
        rest_ = std::string_view{};
        gathering = true;
        continue;
      }
      // Gathering: the delimiter is looked for in what is held, which grows a
      // piece at a time until it settles.
      const std::string_view sofar(held_.data(), held_.size());
      const auto taken = detail::regex_search<Pattern>(sofar);
      if (taken) {
        const auto begins =
            static_cast<std::size_t>(taken.data() - sofar.data());
        const std::size_t ends = begins + taken.size();
        if (ends < sofar.size()) {
          piece_ = sofar.substr(0, begins);
          // What follows the delimiter goes back in front of the reading, in a
          // buffer of its own so that what was just handed back stays where it
          // is.
          left_.assign(sofar.substr(ends));
          leftovers_ = std::string_view(left_.data(), left_.size());
          rest_ = leftovers_;
          gave_one_ = true;
          return;
        }
      }
      if (rest_.empty() && !refill()) {
        piece_ = std::string_view(held_.data(), held_.size());
        if (taken) {
          // It ran to the very end of the reading, so it was a delimiter after
          // all, and what came before it is the piece.
          const auto begins =
              static_cast<std::size_t>(taken.data() - sofar.data());
          piece_ = sofar.substr(0, begins);
        }
        last_ = true;
        gave_one_ = true;
        return;
      }
      held_.append(rest_);
      rest_ = std::string_view{};
    }
  }

  PiecesType pieces_;
  std::optional<std::ranges::iterator_t<PiecesType>> at_;
  std::string_view rest_;
  std::string_view piece_;
  std::string_view leftovers_;
  HeldType held_;
  HeldType crossing_;
  HeldType left_;
  bool last_ = false;
  bool done_ = false;
  bool gave_one_ = false;
};

template <fixed_string Pattern, class HeldType = std::string>
struct split_closure
    : std::ranges::range_adaptor_closure<split_closure<Pattern, HeldType>> {
  template <class Other>
  [[nodiscard]] constexpr split_closure<Pattern, Other> into() const {
    return {};
  }

  template <detail::contiguous_char_range RangeType>
  [[nodiscard]] constexpr split_view<Pattern> operator()(
      RangeType&& input) const {
    return split_view<Pattern>(detail::characters_of(input));
  }

  template <detail::read_once_char_range RangeType>
  [[nodiscard]] constexpr auto operator()(RangeType&& input) const {
    static_assert(
        detail::holds_a_bounded_way<Pattern>(),
        "a subject that can only be read once cannot be gone back over, so "
        "the characters of an attempt at the delimiter that comes to nothing "
        "have to be held until it does: a delimiter that can swallow any "
        "number of them without matching, like `a+b`, would have to hold the "
        "whole subject");
    auto view = std::views::all(std::forward<RangeType>(input));
    return read_once_split_view<Pattern, HeldType, decltype(view)>(
        std::move(view));
  }

  // Off input that arrives in pieces: a piece that lies inside one of the
  // input's own costs no copy, and only one that runs across a boundary is put
  // together.
  template <detail::piecewise_char_range PiecesType>
    requires(!detail::contiguous_char_range<PiecesType> &&
             !std::same_as<std::ranges::range_value_t<PiecesType>, char>)
  [[nodiscard]] constexpr auto operator()(PiecesType&& input) const {
    auto view = std::views::all(std::forward<PiecesType>(input));
    return pieces_split_view<Pattern, HeldType, decltype(view)>(
        std::move(view));
  }
};

template <fixed_string Pattern>
inline constexpr split_closure<Pattern> split{};

template <fixed_string Pattern>
[[deprecated("use search_all")]]
inline constexpr search_all_closure<Pattern> range{};

#undef SCAN_REGEX_FORCE_INLINE

}  // namespace scan

// A module keeps its macros to itself and a header does not, so they are
// taken back here rather than handed to whoever includes this.
#undef SCAN_REGEX_FORCE_INLINE
#undef SCAN_REGEX_FORCE_INLINE_LAMBDA
#undef SCAN_REGEX_NEVER_INLINE_CALL
#undef SCAN_REGEX_FORCE_INLINE_CALL
