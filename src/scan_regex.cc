export module scan.regex;

import std;
import scan.tre;
export import scan.runtime;

export namespace scan {

class regex_submatch {
 public:
  constexpr regex_submatch() = default;
  constexpr regex_submatch(std::string_view view, bool matched = true)
      : view_(view), matched_(matched) {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return matched_;
  }
  [[nodiscard]] constexpr std::string_view to_view() const noexcept {
    return view_;
  }
  [[nodiscard]] constexpr const char* data() const noexcept {
    return view_.data();
  }
  [[nodiscard]] constexpr std::size_t size() const noexcept {
    return view_.size();
  }
  [[nodiscard]] constexpr operator std::string_view() const noexcept {
    return view_;
  }

 private:
  std::string_view view_;
  bool matched_ = false;
};

template <std::size_t capture_count>
class regex_result {
 public:
  constexpr regex_result() = default;

  constexpr regex_result(regex_submatch whole,
                         std::array<regex_submatch, capture_count> captures)
      : whole_(whole), captures_(captures) {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return static_cast<bool>(whole_);
  }
  [[nodiscard]] constexpr std::string_view to_view() const noexcept {
    return whole_.to_view();
  }
  [[nodiscard]] constexpr const char* data() const noexcept {
    return whole_.data();
  }
  [[nodiscard]] constexpr std::size_t size() const noexcept {
    return whole_.size();
  }
  [[nodiscard]] constexpr operator std::string_view() const noexcept {
    return to_view();
  }

  template <std::size_t index>
  [[nodiscard]] constexpr regex_submatch get() const {
    if constexpr (index == 0) {
      return whole_;
    } else if constexpr (index > capture_count) {
      return {};
    } else {
      return captures_[index - 1];
    }
  }

 private:
  regex_submatch whole_;
  std::array<regex_submatch, capture_count> captures_{};
};

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

template <fixed_string pattern, std::size_t state>
[[nodiscard]] consteval auto make_transition_ranges() {
  constexpr const auto& automaton = regex_automaton<pattern>;
  using state_type = typename std::remove_cvref_t<decltype(automaton)>::state_type;
  transition_ranges<state_type> result;
  for (std::size_t symbol : std::views::iota(std::size_t{0}, std::size_t{256})) {
        const state_type target = automaton.transitions[state][symbol];
        if (target == std::remove_cvref_t<decltype(automaton)>::reject) continue;
        if (result.size != 0 &&
            result.values[result.size - 1].target == target &&
            result.values[result.size - 1].last + 1 == symbol) {
          result.values[result.size - 1].last =
              static_cast<unsigned char>(symbol);
          continue;
        }
        result.values[result.size++] = {
            .first = static_cast<unsigned char>(symbol),
            .last = static_cast<unsigned char>(symbol),
            .target = target};
      }
  return result;
}

template <fixed_string pattern>
[[nodiscard]] consteval std::size_t minimum_match_length() {
  constexpr const auto& automaton = regex_automaton<pattern>;
  constexpr std::size_t state_count =
      std::tuple_size_v<std::remove_cvref_t<decltype(automaton.accepting)>>;
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
      for (const auto target : automaton.transitions[source]) {
        if (target == std::remove_cvref_t<decltype(automaton)>::reject) continue;
        distance[target] = std::min(distance[target], distance[source] + 1);
      }
    }
  }
  auto result = unreachable;
  for (std::size_t state = 0; state < state_count; ++state) {
    if (automaton.accepting[state]) {
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
  using state_type =
      typename std::remove_cvref_t<decltype(automaton)>::state_type;
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
  using state_type =
      typename std::remove_cvref_t<decltype(automaton)>::state_type;
  std::array<state_type, 256> result{};
  std::ranges::copy(automaton.transitions[state], result.begin());
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

template <fixed_string pattern, bool in_vectors, std::size_t state>
[[nodiscard]] constexpr bool run_state_continuation(const char* cursor,
                                                    const char* end);

template <fixed_string pattern, unsigned char sentinel, std::size_t state>
[[nodiscard]] constexpr bool run_sentinel_continuation(const char* cursor);

template <fixed_string pattern, bool in_vectors, std::size_t state,
          std::size_t which = 0>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool
dispatch_transition(unsigned char symbol, const char* cursor,
                    const char* end) {
  constexpr auto targets = make_transition_targets<pattern, state>();
  if constexpr (which == targets.size) {
    return false;
  } else if constexpr (targets.values[which] == state) {
    // Where the symbol keeps the automaton is asked before this, and runs do
    // not overlap, so a symbol that stays here belongs to no other target and
    // falls out of every test below.
    return dispatch_transition<pattern, in_vectors, state, which + 1>(
        symbol, cursor, end);
  } else {
    constexpr auto target = targets.values[which];
    if (moves_to<pattern, state, target>(symbol)) {
      return run_state_continuation<pattern, in_vectors, target>(cursor, end);
    }
    return dispatch_transition<pattern, in_vectors, state, which + 1>(
        symbol, cursor, end);
  }
}

template <fixed_string pattern, unsigned char sentinel, std::size_t state,
          std::size_t which = 0>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool
dispatch_sentinel_transition(unsigned char symbol, const char* cursor) {
  constexpr auto targets = make_transition_targets<pattern, state>();
  if constexpr (which == targets.size) {
    return false;
  } else if constexpr (targets.values[which] == state) {
    return dispatch_sentinel_transition<pattern, sentinel, state, which + 1>(
        symbol, cursor);
  } else {
    constexpr auto target = targets.values[which];
    if (moves_to<pattern, state, target>(symbol)) {
      return run_sentinel_continuation<pattern, sentinel, target>(cursor);
    }
    return dispatch_sentinel_transition<pattern, sentinel, state, which + 1>(
        symbol, cursor);
  }
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
  std::ranges::transform(
      automaton.transitions[state], result.begin(), [](auto target) {
        return static_cast<unsigned char>(target == state);
      });
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

template <fixed_string pattern, bool in_vectors, std::size_t state>
[[nodiscard]] constexpr bool run_state_continuation(const char* cursor,
                                                    const char* end) {
  // Over the run this state keeps, in vectors, once on the way in. What is
  // left after it is shorter than a vector and is read a character at a time,
  // which is what the loop below does anyway.
  if constexpr (in_vectors && staying_class_of<pattern, state>().count != 0) {
    cursor = skip_class<staying_class_of<pattern, state>()>(cursor, end);
  }
  while (cursor != end) {
    const unsigned char symbol = static_cast<unsigned char>(*cursor++);
    if (is_self_transition<pattern, state>(symbol)) continue;
    return dispatch_transition<pattern, in_vectors, state>(symbol, cursor,
                                                           end);
  }
  return regex_automaton<pattern>.accepting[state];
}

template <fixed_string pattern, unsigned char sentinel>
[[nodiscard]] consteval bool is_safe_sentinel() {
  constexpr const auto& automaton = regex_automaton<pattern>;
  return std::ranges::all_of(automaton.transitions, [](const auto& row) {
    return row[sentinel] ==
           std::remove_cvref_t<decltype(automaton)>::reject;
  });
}

template <fixed_string pattern, unsigned char sentinel, std::size_t state>
[[nodiscard]] constexpr bool run_sentinel_continuation(const char* cursor) {
  // The class first, the sentinel afterwards.
  //
  // A sentinel is only accepted here when no state takes it, which
  // `is_safe_sentinel` has already required -- so it cannot be a symbol that
  // keeps the automaton where it is, and testing for it before the class only
  // adds a comparison and a branch to every character of the subject. Tested
  // after, it costs nothing until the loop ends anyway, and what is left in
  // the loop is a load, a subtraction, an increment, a comparison and a jump:
  // what a generated scanner emits.
  while (true) {
    const unsigned char symbol = static_cast<unsigned char>(*cursor++);
    if (is_self_transition<pattern, state>(symbol)) continue;
    if (symbol == sentinel) return regex_automaton<pattern>.accepting[state];
    return dispatch_sentinel_transition<pattern, sentinel, state>(symbol,
                                                                  cursor);
  }
}

template <fixed_string pattern, unsigned char sentinel, std::size_t state,
          std::size_t budget>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool
run_inlined_sentinel_continuation(const char* cursor);

template <fixed_string pattern, unsigned char sentinel, std::size_t state,
          std::size_t budget, std::size_t which = 0>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool
dispatch_inlined_sentinel_transition(unsigned char symbol,
                                     const char* cursor) {
  constexpr auto targets = make_transition_targets<pattern, state>();
  if constexpr (which == targets.size) {
    return false;
  } else if constexpr (targets.values[which] == state) {
    return dispatch_inlined_sentinel_transition<pattern, sentinel, state,
                                                budget, which + 1>(symbol,
                                                                   cursor);
  } else {
    constexpr auto target = targets.values[which];
    if (moves_to<pattern, state, target>(symbol)) {
      if constexpr (budget == 0) {
        return run_sentinel_continuation<pattern, sentinel, target>(cursor);
      } else {
        return run_inlined_sentinel_continuation<pattern, sentinel, target,
                                                 budget - 1>(cursor);
      }
    }
    return dispatch_inlined_sentinel_transition<pattern, sentinel, state,
                                                budget, which + 1>(symbol,
                                                                   cursor);
  }
}

template <fixed_string pattern, unsigned char sentinel, std::size_t state,
          std::size_t budget>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool
run_inlined_sentinel_continuation(const char* cursor) {
  while (true) {
    const unsigned char symbol = static_cast<unsigned char>(*cursor++);
    if (is_self_transition<pattern, state>(symbol)) continue;
    if (symbol == sentinel) return regex_automaton<pattern>.accepting[state];
    return dispatch_inlined_sentinel_transition<pattern, sentinel, state,
                                                budget>(symbol, cursor);
  }
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
            ? run_state_continuation<pattern, true, automaton.initial>(cursor,
                                                                      end)
            : run_state_continuation<pattern, false, automaton.initial>(cursor,
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
            ? run_tagged_state_continuation<automaton, true,
                                            automaton.initial>(cursor, end,
                                                               registers)
            : run_tagged_state_continuation<automaton, false,
                                            automaton.initial>(cursor, end,
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
  if (!run_inlined_sentinel_continuation<pattern, sentinel,
                                         automaton.initial, 1>(input.data()))
    return {};
  return {regex_submatch(input), {}};
}

template <fixed_string pattern>
[[nodiscard]] constexpr regex_result_for<pattern> regex_starts_with(
    std::string_view input) {
  regex_result_for<pattern> result;
  for (std::size_t size : std::views::iota(std::size_t{0}, input.size() + 1) |
          std::views::reverse) {
        if (!result) result = regex_match<pattern>(input.substr(0, size));
      }
  return result;
}

template <fixed_string pattern>
[[nodiscard]] constexpr regex_result_for<pattern> regex_search(std::string_view input) {
  regex_result_for<pattern> result;
  for (std::size_t begin : std::views::iota(std::size_t{0}, input.size() + 1)) {
        if (result) continue;
        for (std::size_t end : std::views::iota(begin, input.size() + 1) |
                std::views::reverse) {
              if (!result) {
                result = regex_match<pattern>(
                    input.substr(begin, end - begin));
              }
            }
      }
  return result;
}

}  // namespace detail

template <fixed_string pattern>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr auto match(
    std::string_view input) {
  return detail::regex_match<pattern>(input);
}

template <fixed_string pattern, unsigned char sentinel = 0>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr auto match_sentinel(
    std::string_view input) {
  return detail::regex_match_sentinel<pattern, sentinel>(input);
}

template <fixed_string pattern>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr auto starts_with(
    std::string_view input) {
  return detail::regex_starts_with<pattern>(input);
}

template <fixed_string pattern>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr auto search(
    std::string_view input) {
  return detail::regex_search<pattern>(input);
}

template <fixed_string pattern>
[[nodiscard]] constexpr auto search_all(
    std::string_view input) {
  using result_type = detail::regex_result_for<pattern>;
  std::vector<result_type> results;
  std::size_t offset = 0;
  while (offset <= input.size()) {
    result_type result = search<pattern>(input.substr(offset));
    if (!result) break;
    const auto relative = static_cast<std::size_t>(
        result.data() - input.data() - static_cast<std::ptrdiff_t>(offset));
    offset += relative;
    const std::size_t length = result.size();
    results.push_back(std::move(result));
    offset += std::max(length, std::size_t{1});
  }
  return results;
}

template <fixed_string pattern>
[[nodiscard]] constexpr auto iterator(
    std::string_view input) {
  return search_all<pattern>(input);
}

template <fixed_string pattern>
[[nodiscard]] constexpr auto tokenize(
    std::string_view input) {
  return search_all<pattern>(input);
}

template <fixed_string pattern>
[[nodiscard]] constexpr std::vector<std::string_view> split(
    std::string_view input) {
  std::vector<std::string_view> pieces;
  std::size_t offset = 0;
  for (const auto& delimiter : search_all<pattern>(input)) {
    const auto begin = static_cast<std::size_t>(delimiter.data() - input.data());
    pieces.push_back(input.substr(offset, begin - offset));
    offset = begin + delimiter.size();
  }
  pieces.push_back(input.substr(offset));
  return pieces;
}

template <fixed_string pattern>
[[deprecated("use search_all")]]
[[nodiscard]] constexpr auto range(
    std::string_view input) {
  return search_all<pattern>(input);
}

#undef SCAN_REGEX_FORCE_INLINE

}  // namespace scan
