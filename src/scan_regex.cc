export module scan.regex;

import std;
import scan.tre;
export import scan.runtime;

export namespace scan {

// A piece of the subject a match stood on, however the subject is held.
//
// Contiguous input is pointed at -- a view of it costs nothing and outlives
// the match, because the subject does. Input that is walked by iterators is
// held as the pair of them. Input that can only be read once is held as text
// of its own: there is nothing left behind to point at.
template <class holder>
class basic_submatch {
 public:
  constexpr basic_submatch() = default;
  constexpr basic_submatch(holder held, bool matched = true)
      : held_(std::move(held)), matched_(matched) {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return matched_;
  }

  [[nodiscard]] constexpr const holder& held() const noexcept { return held_; }

  [[nodiscard]] constexpr auto begin() const { return std::ranges::begin(held_); }
  [[nodiscard]] constexpr auto end() const { return std::ranges::end(held_); }

  [[nodiscard]] constexpr std::size_t size() const {
    return static_cast<std::size_t>(std::ranges::size(held_));
  }

  // Only where the characters lie in a row: a view has to point at something
  // that is already there, and a subject read once is not.
  [[nodiscard]] constexpr auto data() const
    requires std::ranges::contiguous_range<const holder&>
  {
    return std::ranges::data(held_);
  }

  [[nodiscard]] constexpr std::string_view to_view() const
    requires std::ranges::contiguous_range<const holder&>
  {
    return std::string_view(std::ranges::data(held_),
                            static_cast<std::size_t>(std::ranges::size(held_)));
  }

  [[nodiscard]] constexpr operator std::string_view() const
    requires std::ranges::contiguous_range<const holder&>
  {
    return to_view();
  }

 private:
  holder held_{};
  bool matched_ = false;
};

using regex_submatch = basic_submatch<std::string_view>;

template <class holder, std::size_t capture_count>
class basic_result {
  struct nothing {};
  using captures_type =
      std::conditional_t<capture_count == 0, nothing,
                         std::array<basic_submatch<holder>, capture_count>>;

 public:
  constexpr basic_result() = default;

  constexpr basic_result(basic_submatch<holder> whole, captures_type captures)
      : whole_(std::move(whole)), captures_(std::move(captures)) {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return static_cast<bool>(whole_);
  }
  [[nodiscard]] constexpr const basic_submatch<holder>& whole() const noexcept {
    return whole_;
  }
  [[nodiscard]] constexpr auto begin() const { return whole_.begin(); }
  [[nodiscard]] constexpr auto end() const { return whole_.end(); }
  [[nodiscard]] constexpr std::size_t size() const { return whole_.size(); }

  [[nodiscard]] constexpr auto data() const
    requires std::ranges::contiguous_range<const holder&>
  {
    return whole_.data();
  }
  [[nodiscard]] constexpr std::string_view to_view() const
    requires std::ranges::contiguous_range<const holder&>
  {
    return whole_.to_view();
  }
  [[nodiscard]] constexpr operator std::string_view() const
    requires std::ranges::contiguous_range<const holder&>
  {
    return to_view();
  }

  template <std::size_t index>
  [[nodiscard]] constexpr basic_submatch<holder> get() const {
    if constexpr (index == 0) {
      return whole_;
    } else if constexpr (index > capture_count) {
      return {};
    } else {
      return captures_[index - 1];
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
  basic_submatch<holder> whole_;
  [[no_unique_address]] captures_type captures_{};
};

template <std::size_t capture_count>
using regex_result = basic_result<std::string_view, capture_count>;

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
#define SCAN_REGEX_NEVER_INLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define SCAN_REGEX_FORCE_INLINE [[gnu::always_inline]] inline
#define SCAN_REGEX_FORCE_INLINE_LAMBDA [[gnu::always_inline]]
#define SCAN_REGEX_NEVER_INLINE [[gnu::noinline]]
#else
#define SCAN_REGEX_FORCE_INLINE inline
#define SCAN_REGEX_FORCE_INLINE_LAMBDA
#define SCAN_REGEX_NEVER_INLINE
#endif

template <class state_type>
struct transition_range {
  unsigned char first = 0;
  unsigned char last = 0;
  state_type target{};
};

template <class state_type>
struct transition_ranges {
  std::array<transition_range<state_type>, 256> values{};
  std::size_t size = 0;
};

// Whether the machine standing here would have a match.
template <fixed_string pattern, std::size_t state>
[[nodiscard]] consteval bool accepts_here() {
  return regex_automaton<pattern>.states[state].accepting_slot !=
         packed_state<0, 0, 0>::not_accepting;
}

// The runs of a state, as the automaton holds them.
//
// These used to be recovered from a cell for every symbol, once for every
// instantiation that asked. The automaton is packed as runs now, so this hands
// them over.
template <fixed_string pattern, std::size_t state>
[[nodiscard]] consteval auto make_transition_ranges() {
  constexpr const auto& automaton = regex_automaton<pattern>;
  using state_type = std::size_t;
  transition_ranges<state_type> result;
  const auto& packed = automaton.states[state];
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
template <fixed_string pattern>
[[nodiscard]] consteval bool accepts_where_the_first_reading_gathers() {
  constexpr const auto& automaton = regex_automaton<pattern>;
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

// The longest a match can be, or nothing at all where it can be any length.
//
// A pattern with no cycle in its automaton matches a bounded number of
// characters: plain text, a count in braces, anything written out. One with a
// cycle -- a star, a plus, an open-ended count -- matches as much as there is.
//
// This is what says whether a subject that can only be read once can be
// searched. Finding the leftmost match means trying a place, failing, and
// trying the next, and the characters of the failed attempt have already gone
// by: they have to be held. Bounded, they are held in a window of a size known
// here; unbounded, they would need a buffer that grows, and there is none.
template <fixed_string pattern>
[[nodiscard]] consteval std::size_t longest_match_length() {
  constexpr const auto& automaton = regex_automaton<pattern>;
  constexpr std::size_t state_count =
      std::tuple_size_v<std::remove_cvref_t<decltype(automaton.states)>>;
  constexpr std::size_t unbounded = std::numeric_limits<std::size_t>::max();
  // The longest walk from each state, found by relaxing as many times as there
  // are states. A walk that is still growing after that many rounds is going
  // round a cycle.
  std::array<std::size_t, state_count> longest{};
  for (std::size_t round = 0; round <= state_count; ++round) {
    std::array<std::size_t, state_count> next{};
    for (std::size_t state = 0; state < state_count; ++state) {
      const auto& packed = automaton.states[state];
      std::size_t best = 0;
      for (std::size_t index = 0; index < packed.range_count; ++index) {
        const std::size_t target = packed.ranges[index].target;
        if (longest[target] == unbounded) return unbounded;
        best = std::max(best, longest[target] + 1);
      }
      next[state] = best;
    }
    if (next == longest) return longest[automaton.initial];
    longest = next;
  }
  return unbounded;
}

template <fixed_string pattern>
[[nodiscard]] consteval bool matches_a_bounded_length() {
  return longest_match_length<pattern>() !=
         std::numeric_limits<std::size_t>::max();
}

template <fixed_string pattern>
[[nodiscard]] consteval std::size_t minimum_match_length() {
  constexpr const auto& automaton = regex_automaton<pattern>;
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
template <class state_type>
struct transition_targets {
  std::array<state_type, 256> values{};
  std::size_t size = 0;
};

template <fixed_string pattern, std::size_t state>
[[nodiscard]] consteval auto make_transition_targets() {
  constexpr const auto& automaton = regex_automaton<pattern>;
  using state_type = std::size_t;
  constexpr auto ranges = make_transition_ranges<pattern, state>();
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
template <fixed_string pattern, std::size_t state, auto target>
[[nodiscard]] consteval std::size_t runs_to_target() {
  constexpr auto ranges = make_transition_ranges<pattern, state>();
  std::size_t count = 0;
  for (std::size_t index = 0; index < ranges.size; ++index) {
    if (ranges.values[index].target == target) ++count;
  }
  return count;
}

// Where each symbol takes the automaton from this state, which is what the
// automaton was built with and is read back here rather than rebuilt.
template <fixed_string pattern, std::size_t state>
inline constexpr auto transition_target_table = [] consteval {
  constexpr const auto& automaton = regex_automaton<pattern>;
  using state_type = std::size_t;
  std::array<state_type, 256> result{};
  std::ranges::fill(result, static_cast<state_type>(
      std::numeric_limits<state_type>::max()));
  const auto& packed = automaton.states[state];
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    for (std::size_t symbol = packed.ranges[index].first;
         symbol <= packed.ranges[index].last; ++symbol) {
      result[symbol] = static_cast<state_type>(packed.ranges[index].target);
    }
  }
  return result;
}();

template <fixed_string pattern, std::size_t state, auto target,
          std::size_t index = 0>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool moves_to_by_runs(
    unsigned char symbol) {
  constexpr auto ranges = make_transition_ranges<pattern, state>();
  if constexpr (index == ranges.size) {
    return false;
  } else {
    constexpr auto range = ranges.values[index];
    if constexpr (range.target == target) {
      if (symbol >= range.first && symbol <= range.last) return true;
    }
    return moves_to_by_runs<pattern, state, target, index + 1>(symbol);
  }
}

// Whether the symbol moves the automaton from this state to that one.
template <fixed_string pattern, std::size_t state, auto target>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool moves_to(
    unsigned char symbol) {
  if constexpr (runs_to_target<pattern, state, target>() >
                runs_worth_comparing) {
    return transition_target_table<pattern, state>[symbol] == target;
  } else {
    return moves_to_by_runs<pattern, state, target>(symbol);
  }
}

// The class of symbols that keeps a state, in the shape the vector skip wants.
//
// The tagged walk has had this for a while: where a state stays put over a run
// of characters, the run is stepped over in vectors instead of being read one
// character at a time. Nothing about it needs the tags -- a state that keeps
// itself writes nothing while it does -- so the captureless walk asks the same
// question of the same code.
template <fixed_string pattern, std::size_t state>
[[nodiscard]] consteval staying_class staying_class_of() {
  constexpr auto ranges = make_transition_ranges<pattern, state>();
  staying_class answer;
  answer.below_the_high_bit = true;
  for (std::size_t index = 0; index < ranges.size; ++index) {
    const auto& range = ranges.values[index];
    if (range.target != state) continue;
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

// How many states a state can move to, not counting itself. One is a chain:
// the machine goes there and nowhere else, and what follows can be written
// where the move is. More than one is a fork, and writing what follows at the
// fork would write it once per branch.
// How far a chain is followed before the next state is reached by a call.
//
// A timestamp is nineteen states in a row, each taking one character; a row of
// comma-separated fields is a dozen. Reaching each of them by a call is a call
// for every character of the subject, which is what a generated scanner never
// does -- so the chain is followed here, and the cap is only against a pattern
// long enough to make one function of the whole of it.
template <fixed_string pattern>
[[nodiscard]] consteval std::size_t chain_budget() {
  constexpr const auto& automaton = regex_automaton<pattern>;
  constexpr std::size_t state_count =
      std::tuple_size_v<std::remove_cvref_t<decltype(automaton.states)>>;
  return state_count < 32 ? state_count : 32;
}



// How many runs of symbols keep the automaton in the state it is in. A class
// like [a-z] is one of them; the local part of an address is twelve.
template <fixed_string pattern, std::size_t state>
[[nodiscard]] consteval std::size_t self_range_count() {
  constexpr auto ranges = make_transition_ranges<pattern, state>();
  std::size_t count = 0;
  for (std::size_t index = 0; index < ranges.size; ++index) {
    if (ranges.values[index].target == state) ++count;
  }
  return count;
}


template <fixed_string pattern, std::size_t state>
inline constexpr auto self_transition_table = [] consteval {
  std::array<unsigned char, 256> result{};
  constexpr const auto& automaton = regex_automaton<pattern>;
  const auto& packed = automaton.states[state];
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    if (packed.ranges[index].target != state) continue;
    for (std::size_t symbol = packed.ranges[index].first;
         symbol <= packed.ranges[index].last; ++symbol) {
      result[symbol] = 1;
    }
  }
  return result;
}();

template <fixed_string pattern, std::size_t state, std::size_t index = 0>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool
is_self_transition_by_runs(unsigned char symbol) {
  constexpr auto ranges = make_transition_ranges<pattern, state>();
  if constexpr (index == ranges.size) {
    return false;
  } else {
    constexpr auto range = ranges.values[index];
    if constexpr (range.target == state) {
      if (symbol >= range.first && symbol <= range.last) return true;
    }
    return is_self_transition_by_runs<pattern, state, index + 1>(symbol);
  }
}

// Whether the symbol keeps the automaton where it is.
template <fixed_string pattern, std::size_t state>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool is_self_transition(
    unsigned char symbol) {
  if constexpr (self_range_count<pattern, state>() > runs_worth_comparing) {
    return self_transition_table<pattern, state>[symbol] != 0;
  } else {
    return is_self_transition_by_runs<pattern, state>(symbol);
  }
}


template <fixed_string pattern, unsigned char sentinel>
[[nodiscard]] consteval bool is_safe_sentinel() {
  constexpr const auto& automaton = regex_automaton<pattern>;
  for (const auto& state : automaton.states) {
    for (std::size_t index = 0; index < state.range_count; ++index) {
      if (sentinel >= state.ranges[index].first &&
          sentinel <= state.ranges[index].last) {
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
template <fixed_string pattern>
[[nodiscard]] consteval detail::walk_shape bounded_shape(bool in_words) {
  return {.in_words = in_words, .budget = chain_budget<pattern>()};
}

template <fixed_string pattern, unsigned char terminator>
[[nodiscard]] consteval detail::walk_shape terminated_shape(bool in_words) {
  return {.in_words = in_words,
          .by_terminator = true,
          .terminator = terminator,
          .budget = chain_budget<pattern>()};
}

template <fixed_string pattern>
[[nodiscard]] consteval detail::walk_shape head_shape() {
  return {.longest = true, .budget = chain_budget<pattern>()};
}

// Whether the subject matched, and where the longest head ended for the walks
// that look for one.
//
// The answer is filled in rather than handed back, because a reading of a
// subject that arrives as it is read does not copy, and passing it along by
// value would ask it to.
template <fixed_string pattern, detail::walk_shape shape, class cursor_type,
          class sentinel_type>
[[nodiscard]] constexpr bool walk_over(cursor_type& cursor, sentinel_type last,
                                       detail::walk_answer<cursor_type>& best) {
  constexpr const auto& automaton = detail::regex_automaton<pattern>;
  using mark_type =
      std::conditional_t<std::is_pointer_v<cursor_type>, const char*,
                         std::ptrdiff_t>;
  std::array<mark_type, automaton.register_count> registers{};
  if constexpr (!std::is_pointer_v<cursor_type>) {
    std::ranges::fill(registers, scan::tre::negative_tag);
  }
  detail::gathers_nothing nothing;
  mark_type place{};
  if constexpr (std::is_pointer_v<cursor_type>) place = cursor;
  // How many characters are known to be there: the subject was measured
  // against the shortest match before the first one was read. A walk after a
  // head was given no such promise -- the head may be shorter than the whole
  // of what it was handed.
  return detail::run_continuation<automaton, shape, automaton.initial,
                                  shape.budget,
                                  shape.head || shape.longest
                                      ? 0
                                      : minimum_match_length<pattern>(),
                                  mark_type>(cursor, last, place, registers,
                                             nothing, best);
}

// The same, where nobody is asking where it stopped.
template <fixed_string pattern, detail::walk_shape shape, class cursor_type,
          class sentinel_type>
[[nodiscard]] constexpr bool matched_over(cursor_type cursor,
                                          sentinel_type last) {
  detail::walk_answer<cursor_type> best;
  return walk_over<pattern, shape>(cursor, last, best);
}

template <fixed_string pattern>
using regex_result_for =
    regex_result<regex_automaton<pattern>.tag_count / 2>;

template <fixed_string pattern>
[[nodiscard]] SCAN_REGEX_NEVER_INLINE constexpr regex_result_for<pattern>
regex_match(
    std::string_view input) {
  constexpr const auto& automaton = regex_automaton<pattern>;
  if constexpr (automaton.tag_count == 0) {
    if (input.size() < minimum_match_length<pattern>()) return {};
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
    constexpr std::size_t worth_a_vector = 64;
    const bool matched =
        input.size() >= worth_a_vector
            ? matched_over<pattern, bounded_shape<pattern>(true)>(cursor, end)
            : matched_over<pattern, bounded_shape<pattern>(false)>(cursor,
                                                                   end);
    if (!matched) return {};
    return {regex_submatch(input), {}};
  } else {
    // A register holds where in the subject something happened, and holds it as
    // the address itself, so that nothing has to be added to it or taken from
    // it. A slot that was never written holds nothing at all.
    std::array<const char*, automaton.register_count> registers{};
    const char* cursor = input.data();
    const char* const end = cursor + input.size();
    execute_commands(automaton.initialize, automaton.initialize.size(),
                     registers, cursor);
    // The generated form, as the captureless branch above uses. Which of the
    // two walks runs is decided once, on the length of the subject: a short one
    // is read a character at a time, a long one in words and vectors.
    constexpr std::size_t worth_a_word = 32;
    const bool matched =
        input.size() >= worth_a_word
            ? run_from_here<automaton, true, automaton.initial>(cursor, end,
                                                                registers)
            : run_from_here<automaton, false, automaton.initial>(cursor, end,
                                                                 registers);
    if (!matched) return {};

    std::array<regex_submatch, automaton.tag_count / 2> captures{};
    for (std::size_t capture : std::views::iota(std::size_t{0}, automaton.tag_count / 2)) {
          const auto begin =
              registers[capture * 2];
          const auto end =
              registers[capture * 2 + 1];
          if (begin == nullptr || end == nullptr) continue;
          captures[capture] =
              regex_submatch(std::string_view(begin, static_cast<std::size_t>(end - begin)));
        }
    return {regex_submatch(input), captures};
  }
}

template <fixed_string pattern, unsigned char sentinel>
[[nodiscard]] SCAN_REGEX_NEVER_INLINE constexpr regex_result_for<pattern>
regex_match_sentinel(std::string_view input) {
  constexpr const auto& automaton = regex_automaton<pattern>;
  static_assert(automaton.tag_count == 0,
                "sentinel matching currently supports captureless patterns");
  static_assert(is_safe_sentinel<pattern, sentinel>(),
                "sentinel must be rejected in every automaton state");
  if (input.size() < minimum_match_length<pattern>()) return {};
  const char* const end = input.data() + input.size();
  constexpr std::size_t worth_a_vector = 64;
  const bool matched =
      input.size() >= worth_a_vector
          ? matched_over<pattern, terminated_shape<pattern, sentinel>(true)>(
                input.data(), end)
          : matched_over<pattern, terminated_shape<pattern, sentinel>(false)>(
                input.data(), end);
  if (!matched) return {};
  return {regex_submatch(input), {}};
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
template <fixed_string pattern>
[[nodiscard]] constexpr const char* longest_head(const char* cursor,
                                                 const char* end) {
  detail::walk_answer<const char*> best;
  const char* walking = cursor;
  const bool found =
      walk_over<pattern, head_shape<pattern>()>(walking, end, best);
  return found ? *best.at : nullptr;
}

template <fixed_string pattern>
[[nodiscard]] constexpr regex_result_for<pattern> regex_starts_with(
    std::string_view input) {
  const char* const begin = input.data();
  const char* const best = longest_head<pattern>(begin, begin + input.size());
  if (best == nullptr) return {};
  // The head is known; the match over exactly that head is run again to fill
  // in whatever the pattern captures, which one pass cannot carry.
  return regex_match<pattern>(
      input.substr(0, static_cast<std::size_t>(best - begin)));
}

// The leftmost match, and the longest one there.
//
// A head is read from every place in turn, and reading one stops at the first
// character the machine will not take -- so a place that cannot begin a match
// costs what it costs to find that out, and not a match of every length from
// there.
template <fixed_string pattern>
[[nodiscard]] constexpr regex_result_for<pattern> regex_search(
    std::string_view input) {
  const char* const begin = input.data();
  const char* const end = begin + input.size();
  for (const char* from = begin; from <= end; ++from) {
    const char* const best = longest_head<pattern>(from, end);
    if (best == nullptr) continue;
    return regex_match<pattern>(
        std::string_view(from, static_cast<std::size_t>(best - from)));
  }
  return {};
}

}  // namespace detail

namespace detail {

// The machine as an object, fed a character at a time.
//
// A subject that can only be read once cannot be walked twice, so the walk
// cannot be a chain of calls -- where it stands has to be a value that
// survives between characters. What is generated here is the step: which run a
// symbol takes is asked of the state's own runs, compared against constants,
// rather than searched for in a table at every character.
//
// Nothing is kept but what the answer is made of. The characters go into the
// collectors of whatever groups are open as they arrive, and a collector that
// holds no text holds nothing at all.
// How far the machine can go past a match without finding another one.
//
// This is the fallback of the TDFA papers, and the measure that matters for a
// subject that can only be read once. A final state is a fallback state where
// there are paths out of it that do not go through another final state; the
// characters read along such a path are the characters that would have to be
// given back when it dies, and there is nowhere to give them back to unless
// they were held.
//
// So the question is not whether the automaton has a cycle -- that was too
// blunt by half -- but how long the longest non-accepting walk out of a final
// state is. For `[a-z]+` it is nothing at all: every letter out of the final
// state lands in a final state, so wherever the machine stops it has a match
// and nothing was ever read past one. For `abc|abd` it is two. For a pattern
// with a cycle that never accepts, it is unbounded, and that is the only case
// this refuses.
template <fixed_string pattern>
[[nodiscard]] consteval std::size_t fallback_window() {
  constexpr const auto& automaton = regex_automaton<pattern>;
  constexpr std::size_t state_count =
      std::tuple_size_v<std::remove_cvref_t<decltype(automaton.states)>>;
  constexpr std::size_t unbounded = std::numeric_limits<std::size_t>::max();
  const auto accepts = [&](std::size_t state) {
    return automaton.states[state].accepting_slot !=
           packed_state<0, 0, 0>::not_accepting;
  };
  // The longest walk from each state that never lands in a final state, found
  // by relaxing as many times as there are states. Still growing after that
  // many rounds means it is going round a cycle that never accepts.
  std::array<std::size_t, state_count> longest{};
  for (std::size_t round = 0; round <= state_count; ++round) {
    std::array<std::size_t, state_count> next{};
    for (std::size_t state = 0; state < state_count; ++state) {
      const auto& packed = automaton.states[state];
      std::size_t best = 0;
      for (std::size_t index = 0; index < packed.range_count; ++index) {
        const std::size_t target = packed.ranges[index].target;
        if (accepts(target)) continue;
        if (longest[target] == unbounded) return unbounded;
        best = std::max(best, longest[target] + 1);
      }
      next[state] = best;
    }
    if (next == longest) break;
    longest = next;
    if (round == state_count) return unbounded;
  }
  // Only what can be read past a match counts, so only final states are asked.
  std::size_t window = 0;
  for (std::size_t state = 0; state < state_count; ++state) {
    if (!accepts(state)) continue;
    if (longest[state] == unbounded) return unbounded;
    window = std::max(window, longest[state]);
  }
  return window;
}

template <fixed_string pattern>
[[nodiscard]] consteval bool falls_back_a_bounded_way() {
  return fallback_window<pattern>() !=
         std::numeric_limits<std::size_t>::max();
}

// A match found in a window of a size known while compiling.
//
// A subject that can only be read once cannot be gone back over, so finding
// the leftmost match means holding what has been read: the characters of an
// attempt that fails belong to the attempt that starts one character later.
// How many that can be is what the pattern says -- a match is at most so many
// characters long -- and where the pattern says nothing, because it can match
// any length at all, this is not offered.
//
// Within the window the machine walks forward and never back. Where it stands
// in an accepting state it takes a note: how far it got, and what the
// registers held, which is the backup the TDFA papers put on the transitions
// out of a fallback state. Where it then dies, the note is the answer and the
// registers are what they were -- no second walk over the same characters.
template <fixed_string pattern, std::size_t window>
struct window_match {
  std::size_t length = 0;
  bool matched = false;
  std::array<std::ptrdiff_t, regex_automaton<pattern>.register_count>
      registers{};
};

template <fixed_string pattern, std::size_t window>
[[nodiscard]] constexpr window_match<pattern, window> match_in_window(
    const std::array<char, window>& held, std::size_t count) {
  constexpr const auto& automaton = regex_automaton<pattern>;
  window_match<pattern, window> answer;
  std::array<std::ptrdiff_t, automaton.register_count> registers{};
  std::ranges::fill(registers, scan::tre::negative_tag);
  execute_commands(automaton.initialize, automaton.initialize.size(),
                   registers, std::ptrdiff_t{0});
  std::size_t here = automaton.initial;
  if (automaton.states[here].accepting_slot !=
      packed_state<0, 0, 0>::not_accepting) {
    answer.matched = true;
    answer.registers = registers;
  }
  for (std::size_t at = 0; at < count; ++at) {
    const auto symbol = static_cast<unsigned char>(held[at]);
    const std::size_t run = run_taken<automaton>(here, symbol);
    if (run == no_run) break;
    const auto& taken = automaton.states[here].ranges[run];
    execute_commands(taken.commands, taken.command_count, registers,
                     static_cast<std::ptrdiff_t>(at + 1));
    here = taken.target;
    if (automaton.states[here].accepting_slot ==
        packed_state<0, 0, 0>::not_accepting) {
      continue;
    }
    // The note taken at every accepting place, which is what makes the death
    // that follows cost nothing.
    answer.length = at + 1;
    answer.matched = true;
    answer.registers = registers;
    execute_commands(automaton.states[here].final_commands,
                     automaton.states[here].final_command_count,
                     answer.registers, static_cast<std::ptrdiff_t>(at + 1));
  }
  return answer;
}

// Whether the group is being read where the machine stands now.
template <fixed_string pattern, std::size_t group, class registers_type>
[[nodiscard]] constexpr bool group_is_open(std::size_t here,
                                           const registers_type& registers) {
  constexpr const auto& automaton = regex_automaton<pattern>;
  const auto& packed = automaton.states[here];
  if (packed.reading_count == 0) return false;
  const std::uint32_t opening = packed.readings[0][group * 2];
  const std::uint32_t closing = packed.readings[0][group * 2 + 1];
  return registers[opening] >= 0 && registers[closing] < registers[opening];
}


template <class range_type>
concept forward_char_range =
    std::ranges::forward_range<range_type> &&
    std::same_as<std::ranges::range_value_t<range_type>, char> &&
    !contiguous_char_range<range_type>;

template <class range_type>
concept read_once_char_range =
    std::ranges::input_range<range_type> &&
    std::same_as<std::ranges::range_value_t<range_type>, char> &&
    !std::ranges::forward_range<range_type>;

// A subject that can only be read once is read into text of its own, and
// everything after that is the ordinary reading of contiguous characters --
// with the answers owning what they stood on, because there is nothing else
// left to point at.
template <class held_type, class range_type>
[[nodiscard]] constexpr held_type read_once(range_type&& input) {
  held_type held;
  auto cursor = std::ranges::begin(input);
  const auto last = std::ranges::end(input);
  for (; cursor != last; ++cursor) held.push_back(*cursor);
  return held;
}

template <class range_type>
using walked_holder =
    std::ranges::subrange<std::ranges::iterator_t<range_type>,
                          std::ranges::iterator_t<range_type>>;

}  // namespace detail

namespace detail {

// The text a capturing group was written with, counting groups from one.
//
// Read the way the pattern reader reads it: a backslash takes the character
// after it whatever that is, a class runs to its closing bracket, and a
// parenthesis that opens with a question mark captures nothing and is not
// counted.
template <fixed_string pattern>
[[nodiscard]] consteval std::string_view group_text(std::size_t wanted) {
  const std::string_view text = pattern.view();
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
template <class type, fixed_string pattern, std::size_t group>
[[nodiscard]] consteval bool group_spells_out() {
  if constexpr (groups_of<type>() <= 1) {
    return false;
  } else if constexpr (!scanned_by_format<std::remove_cv_t<type>>) {
    return false;
  } else {
    constexpr auto declared = capturing_pattern<std::remove_cv_t<type>>();
    return group_text<pattern>(group) == declared.view();
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
template <class type, class... arguments>
class as_collector {
 public:
  using value_type = type;

  constexpr explicit as_collector(arguments... given)
      : arguments_(std::move(given)...) {}

  [[nodiscard]] constexpr value_type from_text(
      std::string_view text, std::string_view parameters) const {
    // Asked of the scanner itself, not of the helper: the helper is a template
    // whose body is what fails for a type that has no scanner, and a body
    // failing is not a question anyone can ask.
    if constexpr (requires {
                    std::declval<scan::scanner<type>&>().parse(text,
                                                               parameters);
                  } || requires {
                    std::declval<scan::scanner<type>&>().parse(text);
                  }) {
      return scanner_parse<type>(text, parameters);
    } else {
      return std::apply(
          [&](const arguments&... given) {
            return value_type(text.begin(), text.end(), given...);
          },
          arguments_);
    }
  }

  // Characters as they come, for a subject that cannot be looked at twice.
  [[nodiscard]] constexpr auto begin_pushing(
      std::string_view parameters) const {
    if constexpr (requires {
                    std::declval<scan::scanner<type>&>().begin(parameters);
                  } || requires {
                    std::declval<scan::scanner<type>&>().begin();
                  }) {
      return scanner_begin<type>(parameters);
    } else {
      return std::apply(
          [&](const arguments&... given) { return value_type(given...); },
          arguments_);
    }
  }


  // The characters of the group as they arrive, for a subject read once.
  [[nodiscard]] constexpr auto begin_pushing_state(
      std::string_view parameters) const {
    return begin_pushing(parameters);
  }

  constexpr void push_one(auto& state, char letter) const {
    if constexpr (requires { scanner_push<type>(state, letter); }) {
      scanner_push<type>(state, letter);
    } else {
      state.push_back(letter);
    }
  }

  [[nodiscard]] constexpr value_type finish_pushed(auto state) const {
    if constexpr (requires {
                    scanner_finish<type, decltype(state)>(state);
                  }) {
      return scanner_finish<type>(std::move(state));
    } else {
      return std::move(state);
    }
  }
 private:
  std::tuple<arguments...> arguments_;
};

template <class type, class... arguments>
[[nodiscard]] constexpr auto as(arguments&&... given) {
  return as_collector<type, std::remove_cvref_t<arguments>...>(
      std::forward<arguments>(given)...);
}

// The characters themselves, held however the subject affords: pointed at,
// walked between, or owned. This is what every group is collected into when
// nothing else is said.
struct text_collector {};

[[nodiscard]] constexpr text_collector text() { return {}; }

// Nothing from this group.
//
// The group is still there -- it may be there because the type of another
// group is read out of it -- but no value is made from it and it takes no room
// in what comes back.
struct skip_collector {};

[[nodiscard]] constexpr skip_collector skip() { return {}; }

// A value of any type at all, filled by a call of your own.
//
// The value is made from the arguments given here, and every character of the
// group is handed to the call along with it. What that does is nobody else's
// business: it can push into a string, count, hash, or throw the characters
// away.
template <class type, class pusher, class... arguments>
class collecting_collector {
 public:
  using value_type = type;

  constexpr collecting_collector(pusher push, arguments... given)
      : push_(std::move(push)), arguments_(std::move(given)...) {}

  [[nodiscard]] constexpr value_type from_text(std::string_view text,
                                               std::string_view) const {
    value_type made = std::apply(
        [&](const arguments&... given) { return value_type(given...); },
        arguments_);
    for (const char letter : text) push_(made, letter);
    return made;
  }

  [[nodiscard]] constexpr auto begin_pushing(std::string_view) const {
    return std::apply(
        [&](const arguments&... given) { return value_type(given...); },
        arguments_);
  }

  constexpr void push_one(value_type& into, char letter) const {
    push_(into, letter);
  }

 private:
  pusher push_;
  std::tuple<arguments...> arguments_;
};

template <class type, class pusher, class... arguments>
[[nodiscard]] constexpr auto collecting(pusher&& push, arguments&&... given) {
  return collecting_collector<type, std::remove_cvref_t<pusher>,
                              std::remove_cvref_t<arguments>...>(
      std::forward<pusher>(push), std::forward<arguments>(given)...);
}

// A match whose groups were turned into values.
//
// The whole of it is still a piece of the subject, held the way the subject
// affords; each group is whatever its collector made of it, and they keep the
// order they were written in. A group nobody wanted is `skipped`, which is
// nothing and takes no room.
struct skipped {};

template <class whole_holder, class... values>
class typed_result {
 public:
  constexpr typed_result() = default;
  constexpr typed_result(basic_submatch<whole_holder> whole,
                         std::tuple<values...> made)
      : whole_(std::move(whole)), values_(std::move(made)) {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return static_cast<bool>(whole_);
  }
  [[nodiscard]] constexpr const basic_submatch<whole_holder>& whole()
      const noexcept {
    return whole_;
  }

  // Nought is the whole match, as it is everywhere else here; the rest are the
  // groups, in the order they were written.
  template <std::size_t index>
  [[nodiscard]] constexpr decltype(auto) get() const {
    if constexpr (index == 0) {
      return (whole_);
    } else {
      static_assert(index <= sizeof...(values), "no such group");
      return std::get<index - 1>(values_);
    }
  }

  [[nodiscard]] constexpr const std::tuple<values...>& all() const noexcept {
    return values_;
  }

 private:
  basic_submatch<whole_holder> whole_;
  [[no_unique_address]] std::tuple<values...> values_{};
};

namespace detail {

// What one collector makes.
template <class collector, class holder>
struct collected {
  using type = typename collector::value_type;
};

template <class holder>
struct collected<skip_collector, holder> {
  using type = skipped;
};

template <class holder>
struct collected<text_collector, holder> {
  using type = holder;
};

template <class collector, class holder>
using collected_type = typename collected<collector, holder>::type;

// Every group of a match, as text, for the types that are built out of them.
template <class found_type, std::size_t... group>
[[nodiscard]] constexpr auto all_groups(const found_type& found,
                                        std::index_sequence<group...>) {
  return std::array<std::string_view, sizeof...(group)>{
      found.template get<group + 1>().to_view()...};
}

template <fixed_string pattern, std::size_t group, class collector,
          class holder, class found_type>
[[nodiscard]] constexpr collected_type<collector, holder> collect_one(
    const collector& one, const found_type& found) {
  if constexpr (std::same_as<collector, skip_collector>) {
    return {};
  } else if constexpr (std::same_as<collector, text_collector>) {
    return found.template get<group>().held();
  } else if constexpr (group_spells_out<typename collector::value_type,
                                        pattern, group>()) {
    // The group is the type's own pattern, so the groups inside it are the
    // type's own values and the machine has already found them. Nothing is
    // read twice and no second automaton was ever built.
    constexpr std::size_t count = regex_automaton<pattern>.tag_count / 2;
    const auto groups = all_groups(found, std::make_index_sequence<count>{});
    return build_value<no_parameters, typename collector::value_type, group>(
        groups);
  } else {
    return one.from_text(found.template get<group>().to_view(),
                         std::string_view{});
  }
}

}  // namespace detail

// A match whose groups are collected, each by its own collector.
template <fixed_string pattern, class held_type, class... collectors>
struct collected_match_closure
    : std::ranges::range_adaptor_closure<
          collected_match_closure<pattern, held_type, collectors...>> {
  constexpr explicit collected_match_closure(collectors... given)
      : collectors_(std::move(given)...) {}

  template <detail::contiguous_char_range range_type>
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    using holder = std::string_view;
    using result_type =
        typed_result<holder, detail::collected_type<collectors, holder>...>;
    const auto found = detail::regex_match<pattern>(std::string_view(
        std::ranges::data(input), std::ranges::size(input)));
    if (!found) return result_type{};
    return build<holder, result_type>(
        found, std::make_index_sequence<sizeof...(collectors)>{});
  }

  // Off input that arrives in pieces: each piece read in words and vectors,
  // and the collectors gathering as the walk goes through them.
  template <detail::piecewise_char_range pieces_type>
    requires(!detail::contiguous_char_range<pieces_type> &&
             !std::same_as<std::ranges::range_value_t<pieces_type>, char>)
  [[nodiscard]] constexpr auto operator()(pieces_type&& input) const {
    constexpr const auto& automaton = detail::regex_automaton<pattern>;
    using holder = skipped;
    using result_type =
        typed_result<holder, detail::collected_type<collectors, held_type>...>;

    std::array<std::ptrdiff_t, automaton.register_count> registers{};
    std::ranges::fill(registers, scan::tre::negative_tag);
    detail::execute_commands(automaton.initialize,
                             automaton.initialize.size(), registers,
                             std::ptrdiff_t{0});
    auto states = beginning(std::make_index_sequence<sizeof...(collectors)>{});
    auto view = std::views::all(std::forward<pieces_type>(input));
    detail::gathers_from_pieces<gathering_into<decltype(states)>,
                                decltype(view)>
        into(gathering_into<decltype(states)>(*this, states), std::move(view));

    const char* cursor = nullptr;
    const char* last = nullptr;
    std::ptrdiff_t place = 0;
    detail::walk_answer<const char*> best;
    constexpr detail::walk_shape shape{.in_words = true};
    if (!detail::run_continuation<automaton, shape, automaton.initial,
                                  shape.budget, 0, std::ptrdiff_t>(
            cursor, last, place, registers, into, best)) {
      return result_type{};
    }
    return result_type{
        basic_submatch<holder>(skipped{}),
        finishing(std::move(states),
                  std::make_index_sequence<sizeof...(collectors)>{})};
  }

  // Walked by the same code that walks characters in a row, with the
  // collectors gathering as it goes.
  //
  // Nothing is kept but what is asked for: a group collected into a number is
  // a number being read, and the subject is never held anywhere.
  template <detail::read_once_char_range range_type>
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    static_assert(
        detail::accepts_where_the_first_reading_gathers<pattern>(),
        "this pattern accepts by a reading that holds a group in other "
        "registers than the first reading does, and a subject that arrives as "
        "it is read is gathered into one place per group: what came back "
        "would be the gathering of a reading that did not win");
    constexpr const auto& automaton = detail::regex_automaton<pattern>;
    // The whole of the match is not kept: there is nothing left behind to
    // point at, and nobody asked for it -- what was asked for is what the
    // collectors say. So the type says so too, and asking for the text of the
    // whole match here does not compile rather than coming back empty.
    using holder = skipped;
    using result_type =
        typed_result<holder, detail::collected_type<collectors, held_type>...>;

    std::array<std::ptrdiff_t, automaton.register_count> registers{};
    std::ranges::fill(registers, scan::tre::negative_tag);
    detail::execute_commands(automaton.initialize,
                             automaton.initialize.size(), registers,
                             std::ptrdiff_t{0});
    auto states = beginning(std::make_index_sequence<sizeof...(collectors)>{});
    gathering_into<decltype(states)> into(*this, states);

    auto cursor = std::ranges::begin(input);
    std::ptrdiff_t position = 0;
    detail::walk_answer<decltype(cursor)> best;
    if (!detail::run_continuation<automaton, detail::walk_shape{},
                                  automaton.initial, 0, 0, std::ptrdiff_t>(
            cursor, std::ranges::end(input), position, registers, into,
            best)) {
      return result_type{};
    }
    return result_type{
        basic_submatch<holder>(skipped{}),
        finishing(std::move(states),
                  std::make_index_sequence<sizeof...(collectors)>{})};
  }


  // What the walk hands characters to: the collectors of the groups that are
  // open where it stands.
  //
  // The state is where the walk stands in its own code, so the registers a
  // group is held in are constants here, and asking whether it is open is two
  // loads and a comparison -- not a lookup of the state, then of its readings,
  // then of the registers.
  template <class states_type>
  class gathering_into {
   public:
    constexpr gathering_into(const collected_match_closure& owner,
                             states_type& states)
        : owner_(owner), states_(states) {}

    template <std::size_t state, std::size_t move, class registers_type,
              class mark>
    constexpr void moving(const registers_type&, mark) const {}

    // A run stepped over in vectors: whatever is open takes all of it.
    template <std::size_t state, class registers_type, class mark>
    constexpr void took_run(const char* from, const char* to,
                            const registers_type& registers, mark) {
      hand_run<state>(from, to, registers,
                      std::make_index_sequence<sizeof...(collectors)>{});
    }

    // A move was made: whatever groups are open where it lands take the
    // character.
    //
    // Which registers a group is held in are constants at the state the walk
    // stands in, so this is two loads and a comparison rather than a lookup of
    // the state, then of its readings, then of the registers.
    template <std::size_t state, std::size_t landed, class registers_type>
    constexpr void moved(std::size_t, char letter,
                         const registers_type& registers, std::ptrdiff_t) {
      hand_all<landed>(letter, registers,
                       std::make_index_sequence<sizeof...(collectors)>{});
    }

    template <std::size_t state, class registers_type>
    constexpr void ended(const registers_type&) {}

   private:
    template <std::size_t landed, class registers_type, std::size_t... group>
    constexpr void hand_run(const char* from, const char* to,
                            const registers_type& registers,
                            std::index_sequence<group...>) {
      (hand_run_group<landed, group>(from, to, registers), ...);
    }

    template <std::size_t landed, std::size_t group, class registers_type>
    constexpr void hand_run_group(const char* from, const char* to,
                                  const registers_type& registers) {
      using collector =
          std::tuple_element_t<group, std::tuple<collectors...>>;
      if constexpr (std::same_as<collector, skip_collector>) {
        return;
      } else {
        constexpr const auto& entered =
            detail::regex_automaton<pattern>.states[landed];
        if constexpr (entered.reading_count != 0) {
          constexpr std::uint32_t opening = entered.readings[0][group * 2];
          constexpr std::uint32_t closing = entered.readings[0][group * 2 + 1];
          if (registers[opening] < 0) return;
          if (registers[closing] >= registers[opening]) return;
          for (const char* letter = from; letter != to; ++letter) {
            if constexpr (std::same_as<collector, text_collector>) {
              std::get<group>(states_).push_back(*letter);
            } else {
              std::get<group>(owner_.collectors_)
                  .push_one(std::get<group>(states_), *letter);
            }
          }
        }
      }
    }

    template <std::size_t landed, class registers_type, std::size_t... group>
    constexpr void hand_all(char letter, const registers_type& registers,
                            std::index_sequence<group...>) {
      (hand_group<landed, group>(letter, registers), ...);
    }

    template <std::size_t landed, std::size_t group, class registers_type>
    constexpr void hand_group(char letter,
                              const registers_type& registers) {
      using collector =
          std::tuple_element_t<group, std::tuple<collectors...>>;
      if constexpr (std::same_as<collector, skip_collector>) {
        return;
      } else {
        constexpr const auto& entered =
            detail::regex_automaton<pattern>.states[landed];
        if constexpr (entered.reading_count == 0) {
          return;
        } else {
          constexpr std::uint32_t opening = entered.readings[0][group * 2];
          constexpr std::uint32_t closing = entered.readings[0][group * 2 + 1];
          if (registers[opening] < 0) return;
          if (registers[closing] >= registers[opening]) return;
          if constexpr (std::same_as<collector, text_collector>) {
            std::get<group>(states_).push_back(letter);
          } else {
            std::get<group>(owner_.collectors_)
                .push_one(std::get<group>(states_), letter);
          }
        }
      }
    }

    const collected_match_closure& owner_;
    states_type& states_;
  };

  template <std::size_t... group>
  [[nodiscard]] constexpr auto beginning(std::index_sequence<group...>) const {
    return std::tuple{begin_one<group>()...};
  }

  template <std::size_t group>
  [[nodiscard]] constexpr auto begin_one() const {
    using collector =
        std::tuple_element_t<group, std::tuple<collectors...>>;
    if constexpr (std::same_as<collector, skip_collector> ||
                  std::same_as<collector, text_collector>) {
      return held_type{};
    } else {
      return std::get<group>(collectors_).begin_pushing(std::string_view{});
    }
  }

  template <class states_type, class registers_type, std::size_t... group>
  constexpr void offer(states_type& states, std::size_t here,
                       const registers_type& registers, char letter,
                       std::index_sequence<group...>) const {
    (offer_one<group>(states, here, registers, letter), ...);
  }

  template <std::size_t group, class states_type, class registers_type>
  constexpr void offer_one(states_type& states, std::size_t here,
                           const registers_type& registers,
                           char letter) const {
    using collector =
        std::tuple_element_t<group, std::tuple<collectors...>>;
    if constexpr (std::same_as<collector, skip_collector>) {
      return;
    } else {
      if (!detail::group_is_open<pattern, group>(here, registers)) return;
      if constexpr (std::same_as<collector, text_collector>) {
        std::get<group>(states).push_back(letter);
      } else {
        std::get<group>(collectors_).push_one(std::get<group>(states), letter);
      }
    }
  }

  template <class states_type, std::size_t... group>
  [[nodiscard]] constexpr auto finishing(states_type states,
                                         std::index_sequence<group...>) const {
    return std::tuple<detail::collected_type<collectors, held_type>...>{
        finish_one<group>(std::move(std::get<group>(states)))...};
  }

  template <std::size_t group, class state_type>
  [[nodiscard]] constexpr auto finish_one(state_type state) const {
    using collector =
        std::tuple_element_t<group, std::tuple<collectors...>>;
    if constexpr (std::same_as<collector, skip_collector>) {
      return skipped{};
    } else if constexpr (std::same_as<collector, text_collector>) {
      return state;
    } else {
      return std::get<group>(collectors_).finish_pushed(std::move(state));
    }
  }

 private:
  template <class holder, class result_type, class found_type,
            std::size_t... group>
  [[nodiscard]] constexpr result_type build(
      const found_type& found, std::index_sequence<group...>) const {
    return result_type{
        basic_submatch<holder>(found.whole()),
        std::tuple<detail::collected_type<collectors, holder>...>{
            detail::collect_one<
                pattern, group + 1,
                std::tuple_element_t<group, std::tuple<collectors...>>,
                holder>(std::get<group>(collectors_), found)...}};
  }

  template <class found_type, std::size_t... group>
  [[nodiscard]] constexpr auto build_values(
      const found_type& found, std::index_sequence<group...>) const {
    return std::tuple<
        detail::collected_type<collectors, held_type>...>{
        detail::collect_one<
            pattern, group + 1,
            std::tuple_element_t<group, std::tuple<collectors...>>, held_type>(
            std::get<group>(collectors_), found)...};
  }

  std::tuple<collectors...> collectors_;
};

// The whole subject, matched.
//
// Three subjects and three answers. Characters that lie in a row are pointed
// at, and the match is the fast walk over them. Characters reached by walking
// are held as the pair of iterators they lie between, and the match is the
// plain walk of the same automaton. Characters that can only be read once are
// read into text of their own, and the answer owns it -- there is nothing left
// behind to point at.
template <fixed_string pattern, class held_type = std::string>
struct match_closure
    : std::ranges::range_adaptor_closure<match_closure<pattern, held_type>> {
  // Where the answers are put, said rather than taken as it comes. What is
  // named here is what a subject read once is read into, and what its pieces
  // are handed back as.
  template <class other>
  [[nodiscard]] constexpr match_closure<pattern, other> into() const {
    return {};
  }

  // A collector for each group, in the order the groups were written. What
  // each of them is made with is its own business, so one group can be a
  // string with one allocator and the next a string with another.
  template <class... collectors>
  [[nodiscard]] constexpr auto into(collectors... given) const {
    return collected_match_closure<pattern, held_type, collectors...>(
        std::move(given)...);
  }

  template <detail::contiguous_char_range range_type>
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    return detail::regex_match<pattern>(std::string_view(
        std::ranges::data(input), std::ranges::size(input)));
  }

  template <detail::forward_char_range range_type>
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    static_assert(detail::regex_automaton<pattern>.tag_count == 0,
                  "a pattern that captures wants the subject in one piece: "
                  "read it into a string first");
    using holder = detail::walked_holder<range_type>;
    const auto first = std::ranges::begin(input);
    const auto last = std::ranges::end(input);
    auto walking = first;
    if (!detail::matched_over<pattern, detail::walk_shape{}>(walking, last)) {
      return basic_result<holder, 0>{};
    }
    return basic_result<holder, 0>{
        basic_submatch<holder>(holder(first, std::ranges::next(first, last))),
        {}};
  }

  // Off input that arrives in pieces: read in words and vectors inside a
  // piece, and the answer owns what it kept, because a piece is gone once the
  // walk has left it.
  template <detail::piecewise_char_range pieces_type>
    requires(!detail::contiguous_char_range<pieces_type> &&
             !std::same_as<std::ranges::range_value_t<pieces_type>, char>)
  [[nodiscard]] constexpr auto operator()(pieces_type&& input) const {
    constexpr const auto& automaton = detail::regex_automaton<pattern>;
    held_type held;
    auto view = std::views::all(std::forward<pieces_type>(input));
    detail::gathers_from_pieces<detail::keeps_into<held_type>, decltype(view)>
        into(detail::keeps_into<held_type>{held}, std::move(view));
    std::array<std::ptrdiff_t, automaton.register_count> registers{};
    std::ranges::fill(registers, scan::tre::negative_tag);
    const char* cursor = nullptr;
    const char* last = nullptr;
    std::ptrdiff_t place = 0;
    detail::walk_answer<const char*> best;
    constexpr detail::walk_shape shape{.in_words = true};
    if (!detail::run_continuation<automaton, shape, automaton.initial,
                                  shape.budget, 0, std::ptrdiff_t>(
            cursor, last, place, registers, into, best)) {
      return basic_result<held_type, 0>{};
    }
    return basic_result<held_type, 0>{
        basic_submatch<held_type>(std::move(held)), {}};
  }

  // Walked by the same code that walks a list of characters, which is the
  // same code that walks characters in a row -- only the reading differs.
  //
  // A subject that arrives as it is read does not need a machine that can be
  // stopped and started: this call owns the loop, so the walk is written out
  // by the compiler as it is everywhere else, and the state is where it stands
  // in that code rather than a number to look up. What it cannot have is the
  // vectors, which want characters in a row.
  template <detail::read_once_char_range range_type>
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    constexpr const auto& automaton = detail::regex_automaton<pattern>;
    held_type held;
    detail::keeps_into<held_type> keep{held};
    std::array<std::ptrdiff_t, automaton.register_count> registers{};
    std::ranges::fill(registers, scan::tre::negative_tag);
    std::ptrdiff_t place = 0;
    auto cursor = std::ranges::begin(input);
    detail::walk_answer<decltype(cursor)> best;
    if (!detail::run_continuation<automaton, detail::walk_shape{},
                                  automaton.initial, 0, 0, std::ptrdiff_t>(
            cursor, std::ranges::end(input), place, registers, keep, best)) {
      return basic_result<held_type, 0>{};
    }
    return basic_result<held_type, 0>{
        basic_submatch<held_type>(std::move(held)), {}};
  }
};

template <fixed_string pattern>
inline constexpr match_closure<pattern> match{};

template <fixed_string pattern, unsigned char sentinel = 0,
          class held_type = std::string>
struct match_sentinel_closure
    : std::ranges::range_adaptor_closure<
          match_sentinel_closure<pattern, sentinel, held_type>> {
  template <class other>
  [[nodiscard]] constexpr match_sentinel_closure<pattern, sentinel, other>
  into() const {
    return {};
  }

  template <detail::contiguous_char_range range_type>
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    return detail::regex_match_sentinel<pattern, sentinel>(std::string_view(
        std::ranges::data(input), std::ranges::size(input)));
  }

  // A terminator is what saves the walk a comparison, and a subject walked by
  // iterators has none to promise -- so these read it as any other subject.
  template <detail::forward_char_range range_type>
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    return match_closure<pattern, held_type>{}(std::forward<range_type>(input));
  }

  template <detail::read_once_char_range range_type>
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    return match_closure<pattern, held_type>{}(std::forward<range_type>(input));
  }
};

template <fixed_string pattern, unsigned char sentinel = 0>
inline constexpr match_sentinel_closure<pattern, sentinel> match_sentinel{};

// The head of the subject the pattern takes.
template <fixed_string pattern, class held_type = std::string>
struct starts_with_closure
    : std::ranges::range_adaptor_closure<
          starts_with_closure<pattern, held_type>> {
  template <class other>
  [[nodiscard]] constexpr starts_with_closure<pattern, other> into() const {
    return {};
  }

  template <detail::contiguous_char_range range_type>
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    return detail::regex_starts_with<pattern>(std::string_view(
        std::ranges::data(input), std::ranges::size(input)));
  }

  template <detail::forward_char_range range_type>
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    static_assert(detail::regex_automaton<pattern>.tag_count == 0,
                  "a pattern that captures wants the subject in one piece: "
                  "read it into a string first");
    using holder = detail::walked_holder<range_type>;
    auto walking = std::ranges::begin(input);
    const auto first = walking;
    detail::walk_answer<decltype(walking)> best;
    const bool found = detail::walk_over<pattern, detail::head_shape<pattern>()>(
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
  template <detail::read_once_char_range range_type>
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    static_assert(
        detail::fallback_window<pattern>() == 0,
        "a subject that can only be read once has nowhere to give back the "
        "characters read past the head, and this pattern can walk away from a "
        "match without finding another one: it would have to hold them");
    constexpr const auto& automaton = detail::regex_automaton<pattern>;
    held_type held;
    std::size_t here = automaton.initial;
    auto cursor = std::ranges::begin(input);
    const auto last = std::ranges::end(input);
    for (; cursor != last; ++cursor) {
      const unsigned char symbol = static_cast<unsigned char>(*cursor);
      const std::size_t run = detail::run_taken<detail::regex_automaton<pattern>>(here, symbol);
      if (run == detail::no_run) break;
      here = automaton.states[here].ranges[run].target;
      held.push_back(static_cast<char>(symbol));
    }
    if (automaton.states[here].accepting_slot ==
        detail::packed_state<0, 0, 0>::not_accepting) {
      return basic_result<held_type, 0>{};
    }
    return basic_result<held_type, 0>{
        basic_submatch<held_type>(std::move(held)), {}};
  }
};

template <fixed_string pattern>
inline constexpr starts_with_closure<pattern> starts_with{};

// The leftmost match. Only over characters that are already all there: finding
// it asks the subject about places it has been past, which a subject that can
// only be read once cannot answer.
template <fixed_string pattern>
struct search_closure
    : std::ranges::range_adaptor_closure<search_closure<pattern>> {
  template <detail::contiguous_char_range range_type>
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    return detail::regex_search<pattern>(std::string_view(
        std::ranges::data(input), std::ranges::size(input)));
  }
};

template <fixed_string pattern>
inline constexpr search_closure<pattern> search{};

// One match after another, found as they are asked for.
//
// Nothing is collected: the view holds where to look next and finds the next
// match when the loop asks for it. What was here built a vector of every match
// in the input before the caller had looked at the first one.
template <fixed_string pattern>
class search_view {
 public:
  using result_type = detail::regex_result_for<pattern>;

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
    constexpr void seek() { found_ = detail::regex_search<pattern>(rest_); }

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
// The window holds what has been read and not yet answered for: at most as
// many characters as a match can be, which the pattern says while it is
// compiled. Where a match is found at the head of the window it is handed over
// and those characters are dropped; where none is, one character is dropped
// and the window slides on.
template <fixed_string pattern, class held_type, class range_type>
class read_once_search_view {
 public:
  static constexpr std::size_t window = detail::longest_match_length<pattern>();

  constexpr explicit read_once_search_view(range_type input)
      : input_(std::move(input)) {}

  read_once_search_view(read_once_search_view&&) = default;
  read_once_search_view& operator=(read_once_search_view&&) = default;
  read_once_search_view(const read_once_search_view&) = delete;
  read_once_search_view& operator=(const read_once_search_view&) = delete;

  class iterator {
   public:
    using value_type = basic_submatch<held_type>;
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
      found_ = value_type{};
      while (true) {
        owner_->fill();
        if (owner_->count_ == 0) return;
        const auto taken =
            detail::match_in_window<pattern, window>(owner_->held_,
                                                     owner_->count_);
        if (taken.matched && taken.length != 0) {
          held_type made;
          for (std::size_t at = 0; at < taken.length; ++at) {
            made.push_back(owner_->held_[at]);
          }
          owner_->drop(taken.length);
          found_ = value_type(std::move(made));
          return;
        }
        owner_->drop(1);
      }
    }

    read_once_search_view* owner_ = nullptr;
    value_type found_{};
  };

  [[nodiscard]] constexpr iterator begin() { return iterator(*this); }
  [[nodiscard]] constexpr std::default_sentinel_t end() const { return {}; }

 private:
  friend class iterator;
  friend class read_once_split_view_access;

  // An iterator of such a range need not be default-constructible, so it is
  // made where the reading starts rather than kept empty until then.
  constexpr void fill() {
    if (!cursor_) cursor_.emplace(std::ranges::begin(input_));
    while (count_ < window && *cursor_ != std::ranges::end(input_)) {
      held_[count_++] = **cursor_;
      ++*cursor_;
    }
  }

  constexpr void drop(std::size_t many) {
    for (std::size_t at = many; at < count_; ++at) held_[at - many] = held_[at];
    count_ -= many;
  }

  range_type input_;
  std::optional<std::ranges::iterator_t<range_type>> cursor_;
  std::array<char, window> held_{};
  std::size_t count_ = 0;
};

// The pieces between those matches, off the same kind of subject. A piece is
// as long as it is, and it is the answer, so it is the only thing here that
// grows.
template <fixed_string pattern, class held_type, class range_type>
class read_once_split_view {
 public:
  static constexpr std::size_t window = detail::longest_match_length<pattern>();

  constexpr explicit read_once_split_view(range_type input)
      : input_(std::move(input)) {}

  read_once_split_view(read_once_split_view&&) = default;
  read_once_split_view& operator=(read_once_split_view&&) = default;
  read_once_split_view(const read_once_split_view&) = delete;
  read_once_split_view& operator=(const read_once_split_view&) = delete;

  class iterator {
   public:
    using value_type = held_type;
    using difference_type = std::ptrdiff_t;

    constexpr iterator() = default;
    constexpr explicit iterator(read_once_split_view& owner)
        : owner_(&owner), done_(false) {
      seek();
    }

    [[nodiscard]] constexpr const held_type& operator*() const {
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
      piece_ = held_type{};
      while (true) {
        owner_->fill();
        if (owner_->count_ == 0) {
          last_ = true;
          return;
        }
        const auto taken =
            detail::match_in_window<pattern, window>(owner_->held_,
                                                     owner_->count_);
        if (taken.matched && taken.length != 0) {
          owner_->drop(taken.length);
          return;
        }
        piece_.push_back(owner_->held_[0]);
        owner_->drop(1);
      }
    }

    read_once_split_view* owner_ = nullptr;
    held_type piece_{};
    bool last_ = false;
    bool done_ = true;
  };

  [[nodiscard]] constexpr iterator begin() { return iterator(*this); }
  [[nodiscard]] constexpr std::default_sentinel_t end() const { return {}; }

 private:
  friend class iterator;

  // An iterator of such a range need not be default-constructible, so it is
  // made where the reading starts rather than kept empty until then.
  constexpr void fill() {
    if (!cursor_) cursor_.emplace(std::ranges::begin(input_));
    while (count_ < window && *cursor_ != std::ranges::end(input_)) {
      held_[count_++] = **cursor_;
      ++*cursor_;
    }
  }

  constexpr void drop(std::size_t many) {
    for (std::size_t at = many; at < count_; ++at) held_[at - many] = held_[at];
    count_ -= many;
  }

  range_type input_;
  std::optional<std::ranges::iterator_t<range_type>> cursor_;
  std::array<char, window> held_{};
  std::size_t count_ = 0;
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
template <fixed_string pattern, class held_type, class pieces_type>
class pieces_search_view {
 public:
  constexpr explicit pieces_search_view(pieces_type input)
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
      const auto taken = detail::regex_search<pattern>(rest_);
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
      const auto ahead = detail::regex_search<pattern>(sofar);
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
    const auto taken = detail::regex_search<pattern>(over);
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

  pieces_type pieces_;
  std::optional<std::ranges::iterator_t<pieces_type>> at_;
  std::string_view rest_;
  held_type held_;
  held_type kept_;
  value_type found_{};
};

template <fixed_string pattern, class held_type = std::string>
struct search_all_closure
    : std::ranges::range_adaptor_closure<
          search_all_closure<pattern, held_type>> {
  template <class other>
  [[nodiscard]] constexpr search_all_closure<pattern, other> into() const {
    return {};
  }

  template <detail::contiguous_char_range range_type>
  [[nodiscard]] constexpr search_view<pattern> operator()(
      range_type&& input) const {
    return search_view<pattern>(std::string_view(std::ranges::data(input),
                                                 std::ranges::size(input)));
  }

  template <detail::read_once_char_range range_type>
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    static_assert(
        detail::matches_a_bounded_length<pattern>(),
        "a subject that can only be read once cannot be gone back over, and "
        "the characters of a failed attempt belong to the attempt that starts "
        "one character later: only a pattern that says how long a match can "
        "be says how many of them to hold");
    auto view = std::views::all(std::forward<range_type>(input));
    return read_once_search_view<pattern, held_type, decltype(view)>(
        std::move(view));
  }

  // Off input that arrives in pieces: what fits in a piece is found in it and
  // costs no copy at all, and only what crosses a boundary is put together.
  template <detail::piecewise_char_range pieces_type>
    requires(!detail::contiguous_char_range<pieces_type> &&
             !std::same_as<std::ranges::range_value_t<pieces_type>, char>)
  [[nodiscard]] constexpr auto operator()(pieces_type&& input) const {
    auto view = std::views::all(std::forward<pieces_type>(input));
    return pieces_search_view<pattern, held_type, decltype(view)>(
        std::move(view));
  }
};

template <fixed_string pattern>
inline constexpr search_all_closure<pattern> search_all{};

template <fixed_string pattern>
inline constexpr search_all_closure<pattern> iterator{};

template <fixed_string pattern>
inline constexpr search_all_closure<pattern> tokenize{};

// The pieces between the matches, found as they are asked for.
template <fixed_string pattern>
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
      const auto delimiter = detail::regex_search<pattern>(rest_);
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
template <fixed_string pattern, class held_type, class pieces_type>
class pieces_split_view {
 public:
  constexpr explicit pieces_split_view(pieces_type input)
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
        const auto taken = detail::regex_search<pattern>(rest_);
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
      const auto taken = detail::regex_search<pattern>(sofar);
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

  pieces_type pieces_;
  std::optional<std::ranges::iterator_t<pieces_type>> at_;
  std::string_view rest_;
  std::string_view piece_;
  std::string_view leftovers_;
  held_type held_;
  held_type crossing_;
  held_type left_;
  bool last_ = false;
  bool done_ = false;
  bool gave_one_ = false;
};

template <fixed_string pattern, class held_type = std::string>
struct split_closure
    : std::ranges::range_adaptor_closure<split_closure<pattern, held_type>> {
  template <class other>
  [[nodiscard]] constexpr split_closure<pattern, other> into() const {
    return {};
  }

  template <detail::contiguous_char_range range_type>
  [[nodiscard]] constexpr split_view<pattern> operator()(
      range_type&& input) const {
    return split_view<pattern>(std::string_view(std::ranges::data(input),
                                                std::ranges::size(input)));
  }

  template <detail::read_once_char_range range_type>
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    static_assert(
        detail::matches_a_bounded_length<pattern>(),
        "a subject that can only be read once cannot be gone back over: only "
        "a delimiter that says how long it can be says how much to hold while "
        "looking for it");
    auto view = std::views::all(std::forward<range_type>(input));
    return read_once_split_view<pattern, held_type, decltype(view)>(
        std::move(view));
  }

  // Off input that arrives in pieces: a piece that lies inside one of the
  // input's own costs no copy, and only one that runs across a boundary is put
  // together.
  template <detail::piecewise_char_range pieces_type>
    requires(!detail::contiguous_char_range<pieces_type> &&
             !std::same_as<std::ranges::range_value_t<pieces_type>, char>)
  [[nodiscard]] constexpr auto operator()(pieces_type&& input) const {
    auto view = std::views::all(std::forward<pieces_type>(input));
    return pieces_split_view<pattern, held_type, decltype(view)>(
        std::move(view));
  }
};

template <fixed_string pattern>
inline constexpr split_closure<pattern> split{};

template <fixed_string pattern>
[[deprecated("use search_all")]]
inline constexpr search_all_closure<pattern> range{};

#undef SCAN_REGEX_FORCE_INLINE

}  // namespace scan
