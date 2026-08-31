export module scan.regex;

import std;
import tre;
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

#if defined(_MSC_VER)
#define SCAN_REGEX_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define SCAN_REGEX_FORCE_INLINE [[gnu::always_inline]] inline
#else
#define SCAN_REGEX_FORCE_INLINE inline
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

struct tagged_transition_range {
  unsigned char first = 0;
  unsigned char last = 0;
  unsigned char representative = 0;
  std::size_t target = 0;
};

struct tagged_transition_ranges {
  std::array<tagged_transition_range, 256> values{};
  std::size_t size = 0;
};

template <class transition_type>
[[nodiscard]] consteval bool same_tagged_transition(
    const transition_type& lhs, const transition_type& rhs) {
  if (lhs.target != rhs.target || lhs.command_count != rhs.command_count)
    return false;
  return std::ranges::equal(
      lhs.commands | std::views::take(lhs.command_count),
      rhs.commands | std::views::take(rhs.command_count), {},
      [](const packed_command& command) {
        return std::tuple(command.destination, command.source, command.value);
      },
      [](const packed_command& command) {
        return std::tuple(command.destination, command.source, command.value);
      });
}

template <fixed_string pattern, std::size_t state>
[[nodiscard]] consteval auto make_tagged_transition_ranges() {
  constexpr const auto& automaton = regex_automaton<pattern>;
  tagged_transition_ranges result;
  for (std::size_t symbol :
       std::views::iota(std::size_t{0}, std::size_t{256})) {
    const auto& transition = automaton.states[state].transitions[symbol];
    if (transition.target == transition.reject) continue;
    if (result.size != 0 &&
        result.values[result.size - 1].last + 1 == symbol) {
      const auto representative =
          result.values[result.size - 1].representative;
      if (same_tagged_transition(
              automaton.states[state].transitions[representative],
              transition)) {
        result.values[result.size - 1].last =
            static_cast<unsigned char>(symbol);
        continue;
      }
    }
    result.values[result.size++] = {
        .first = static_cast<unsigned char>(symbol),
        .last = static_cast<unsigned char>(symbol),
        .representative = static_cast<unsigned char>(symbol),
        .target = transition.target};
  }
  return result;
}

template <fixed_string pattern, std::size_t state>
[[nodiscard]] consteval auto make_transition_ranges() {
  constexpr const auto& automaton = regex_automaton<pattern>;
  using state_type = typename std::remove_cvref_t<decltype(automaton)>::state_type;
  transition_ranges<state_type> result;
  std::ranges::for_each(
      std::views::iota(std::size_t{0}, std::size_t{256}),
      [&](std::size_t symbol) {
        const state_type target = automaton.transitions[state][symbol];
        if (target == std::remove_cvref_t<decltype(automaton)>::reject) return;
        if (result.size != 0 &&
            result.values[result.size - 1].target == target &&
            result.values[result.size - 1].last + 1 == symbol) {
          result.values[result.size - 1].last =
              static_cast<unsigned char>(symbol);
          return;
        }
        result.values[result.size++] = {
            .first = static_cast<unsigned char>(symbol),
            .last = static_cast<unsigned char>(symbol),
            .target = target};
      });
  return result;
}

template <fixed_string pattern>
[[nodiscard]] consteval std::size_t minimum_match_length() {
  constexpr const auto& automaton = regex_automaton<pattern>;
  constexpr std::size_t state_count =
      std::tuple_size_v<std::remove_cvref_t<decltype(automaton.accepting)>>;
  constexpr std::size_t unreachable =
      std::numeric_limits<std::size_t>::max();
  std::array<std::size_t, state_count> distance;
  std::ranges::fill(distance, unreachable);
  distance[automaton.initial] = 0;
  std::ranges::for_each(
      std::views::iota(std::size_t{0}, state_count), [&](std::size_t) {
        std::ranges::for_each(
            std::views::iota(std::size_t{0}, state_count),
            [&](std::size_t source) {
              if (distance[source] == unreachable) return;
              std::ranges::for_each(
                  automaton.transitions[source], [&](auto target) {
                    if (target == std::remove_cvref_t<
                                      decltype(automaton)>::reject)
                      return;
                    distance[target] = std::min(distance[target],
                                                distance[source] + 1);
                  });
            });
      });
  auto result = unreachable;
  std::ranges::for_each(
      std::views::iota(std::size_t{0}, state_count), [&](std::size_t state) {
        if (automaton.accepting[state])
          result = std::min(result, distance[state]);
      });
  return result;
}

template <fixed_string pattern, std::size_t state>
[[nodiscard]] constexpr bool run_state_continuation(const char* cursor,
                                                    const char* end);

template <fixed_string pattern, unsigned char sentinel, std::size_t state>
[[nodiscard]] constexpr bool run_sentinel_continuation(const char* cursor);

template <fixed_string pattern, std::size_t state, std::size_t index = 0>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool
dispatch_transition(unsigned char symbol, const char* cursor,
                    const char* end) {
  constexpr auto ranges = make_transition_ranges<pattern, state>();
  if constexpr (index == ranges.size) {
    return false;
  } else {
    constexpr auto range = ranges.values[index];
    if (symbol >= range.first && symbol <= range.last) {
      if constexpr (range.target == state) {
        return false;
      } else {
        return run_state_continuation<pattern, range.target>(cursor, end);
      }
    }
    return dispatch_transition<pattern, state, index + 1>(symbol, cursor, end);
  }
}

template <fixed_string pattern, unsigned char sentinel, std::size_t state,
          std::size_t index = 0>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool
dispatch_sentinel_transition(unsigned char symbol, const char* cursor) {
  constexpr auto ranges = make_transition_ranges<pattern, state>();
  if constexpr (index == ranges.size) {
    return false;
  } else {
    constexpr auto range = ranges.values[index];
    if (symbol >= range.first && symbol <= range.last) {
      if constexpr (range.target == state) {
        return false;
      } else {
        return run_sentinel_continuation<pattern, sentinel, range.target>(
            cursor);
      }
    }
    return dispatch_sentinel_transition<pattern, sentinel, state, index + 1>(
        symbol, cursor);
  }
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

template <fixed_string pattern, std::size_t state>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool is_self_transition(
    unsigned char symbol) {
  return self_transition_table<pattern, state>[symbol] != 0;
}

template <fixed_string pattern, std::size_t state>
[[nodiscard]] constexpr bool run_state_continuation(const char* cursor,
                                                    const char* end) {
  while (cursor != end) {
    const unsigned char symbol = static_cast<unsigned char>(*cursor++);
    if (is_self_transition<pattern, state>(symbol)) continue;
    return dispatch_transition<pattern, state>(symbol, cursor, end);
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
  while (true) {
    const unsigned char symbol = static_cast<unsigned char>(*cursor);
    if (symbol == sentinel) return regex_automaton<pattern>.accepting[state];
    ++cursor;
    if (is_self_transition<pattern, state>(symbol)) continue;
    return dispatch_sentinel_transition<pattern, sentinel, state>(symbol,
                                                                  cursor);
  }
}

template <fixed_string pattern, unsigned char sentinel, std::size_t state,
          std::size_t budget>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool
run_inlined_sentinel_continuation(const char* cursor);

template <fixed_string pattern, unsigned char sentinel, std::size_t state,
          std::size_t budget, std::size_t index = 0>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool
dispatch_inlined_sentinel_transition(unsigned char symbol,
                                     const char* cursor) {
  constexpr auto ranges = make_transition_ranges<pattern, state>();
  if constexpr (index == ranges.size) {
    return false;
  } else {
    constexpr auto range = ranges.values[index];
    if (symbol >= range.first && symbol <= range.last) {
      if constexpr (range.target == state) {
        return false;
      } else if constexpr (budget == 0) {
        return run_sentinel_continuation<pattern, sentinel, range.target>(
            cursor);
      } else {
        return run_inlined_sentinel_continuation<
            pattern, sentinel, range.target, budget - 1>(cursor);
      }
    }
    return dispatch_inlined_sentinel_transition<pattern, sentinel, state,
                                                budget, index + 1>(symbol,
                                                                   cursor);
  }
}

template <fixed_string pattern, unsigned char sentinel, std::size_t state,
          std::size_t budget>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool
run_inlined_sentinel_continuation(const char* cursor) {
  while (true) {
    const unsigned char symbol = static_cast<unsigned char>(*cursor);
    if (symbol == sentinel) return regex_automaton<pattern>.accepting[state];
    ++cursor;
    if (is_self_transition<pattern, state>(symbol)) continue;
    return dispatch_inlined_sentinel_transition<pattern, sentinel, state,
                                                budget>(symbol, cursor);
  }
}

template <fixed_string pattern, std::size_t state, std::size_t register_count>
[[nodiscard]] constexpr bool run_tagged_state_continuation(
    const char* cursor, const char* end,
    std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position);

template <fixed_string pattern, std::size_t state, unsigned char symbol,
          std::size_t register_count>
SCAN_REGEX_FORCE_INLINE constexpr void execute_static_transition_commands(
    std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position) {
  constexpr const auto& transition =
      regex_automaton<pattern>.states[state].transitions[symbol];
  [&]<std::size_t... index>(std::index_sequence<index...>)
      SCAN_REGEX_FORCE_INLINE {
        const std::array<std::ptrdiff_t, sizeof...(index)> source_values{
            (transition.commands[index].source == packed_command::no_source
                 ? tre::negative_tag
                 : registers[transition.commands[index].source])...};
        (execute_command(transition.commands[index], source_values[index],
                         registers, position),
         ...);
      }(std::make_index_sequence<transition.command_count>{});
}

template <fixed_string pattern, std::size_t state, std::size_t register_count>
SCAN_REGEX_FORCE_INLINE constexpr void execute_static_final_commands(
    std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position) {
  constexpr const auto& packed_state = regex_automaton<pattern>.states[state];
  [&]<std::size_t... index>(std::index_sequence<index...>)
      SCAN_REGEX_FORCE_INLINE {
        const std::array<std::ptrdiff_t, sizeof...(index)> source_values{
            (packed_state.final_commands[index].source ==
                     packed_command::no_source
                 ? tre::negative_tag
                 : registers[packed_state.final_commands[index].source])...};
        (execute_command(packed_state.final_commands[index],
                         source_values[index], registers, position),
         ...);
      }(std::make_index_sequence<packed_state.final_command_count>{});
}

template <fixed_string pattern, std::size_t state, std::size_t register_count,
          std::size_t index = 0>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool
execute_tagged_self_transition(
    unsigned char symbol,
    std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position) {
  constexpr auto ranges = make_tagged_transition_ranges<pattern, state>();
  if constexpr (index == ranges.size) {
    return false;
  } else {
    constexpr auto range = ranges.values[index];
    if (symbol >= range.first && symbol <= range.last) {
      if constexpr (range.target != state) return false;
      execute_static_transition_commands<pattern, state,
                                         range.representative>(registers,
                                                               position);
      return true;
    }
    return execute_tagged_self_transition<pattern, state, register_count,
                                          index + 1>(symbol, registers,
                                                     position);
  }
}

template <fixed_string pattern, std::size_t state, std::size_t register_count,
          std::size_t index = 0>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr bool
dispatch_tagged_transition(
    unsigned char symbol, const char* cursor, const char* end,
    std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position) {
  constexpr auto ranges = make_tagged_transition_ranges<pattern, state>();
  if constexpr (index == ranges.size) {
    return false;
  } else {
    constexpr auto range = ranges.values[index];
    if (symbol >= range.first && symbol <= range.last) {
      if constexpr (range.target == state) return false;
      execute_static_transition_commands<pattern, state,
                                         range.representative>(registers,
                                                               position);
      return run_tagged_state_continuation<pattern, range.target>(
          cursor, end, registers, position);
    }
    return dispatch_tagged_transition<pattern, state, register_count,
                                      index + 1>(symbol, cursor, end,
                                                 registers, position);
  }
}

template <fixed_string pattern, std::size_t state, std::size_t register_count>
[[nodiscard]] constexpr bool run_tagged_state_continuation(
    const char* cursor, const char* end,
    std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position) {
  while (cursor != end) {
    const unsigned char symbol = static_cast<unsigned char>(*cursor++);
    ++position;
    if (execute_tagged_self_transition<pattern, state>(symbol, registers,
                                                        position)) {
      continue;
    }
    return dispatch_tagged_transition<pattern, state>(
        symbol, cursor, end, registers, position);
  }
  if constexpr (regex_automaton<pattern>.states[state].accepting_slot ==
                packed_state<0, 0>::not_accepting) {
    return false;
  } else {
    execute_static_final_commands<pattern, state>(registers, position);
    return true;
  }
}

template <fixed_string pattern>
using regex_result_for =
    regex_result<regex_automaton<pattern>.tag_count / 2>;

template <fixed_string pattern>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr regex_result_for<pattern>
regex_match(
    std::string_view input) {
  constexpr const auto& automaton = regex_automaton<pattern>;
  if constexpr (automaton.tag_count == 0) {
    if (input.size() < minimum_match_length<pattern>()) return {};
    const char* cursor = input.data();
    const char* const end = cursor + input.size();
    if (!run_state_continuation<pattern, automaton.initial>(cursor, end))
      return {};
    return {regex_submatch(input), {}};
  } else {
    std::array<std::ptrdiff_t, automaton.register_count> registers{};
    std::ranges::fill(registers, tre::negative_tag);
    execute_commands(automaton.initialize, automaton.initialize.size(),
                     registers, 0);
    std::size_t state = automaton.initial;
    std::ranges::for_each(
        std::views::iota(std::size_t{0}, input.size()),
        [&](std::size_t position) {
          if (state == packed_transition<0>::reject) return;
          const auto& transition = automaton.states[state].transitions[
              static_cast<unsigned char>(input[position])];
          if (transition.target == transition.reject) {
            state = transition.reject;
            return;
          }
          execute_commands(transition.commands, transition.command_count,
                           registers,
                           static_cast<std::ptrdiff_t>(position + 1));
          state = transition.target;
        });
    if (state == packed_transition<0>::reject) return {};
    const std::size_t slot = automaton.states[state].accepting_slot;
    if (slot == packed_state<0, 0>::not_accepting) return {};
    execute_commands(automaton.states[state].final_commands,
                     automaton.states[state].final_command_count, registers,
                     static_cast<std::ptrdiff_t>(input.size()));

    std::array<regex_submatch, automaton.tag_count / 2> captures{};
    std::ranges::for_each(
        std::views::iota(std::size_t{0}, automaton.tag_count / 2),
        [&](std::size_t capture) {
          const auto begin =
              registers[capture * 2];
          const auto end =
              registers[capture * 2 + 1];
          if (begin < 0 || end < begin) return;
          captures[capture] = regex_submatch(input.substr(
              static_cast<std::size_t>(begin),
              static_cast<std::size_t>(end - begin)));
        });
    return {regex_submatch(input), captures};
  }
}

template <fixed_string pattern, unsigned char sentinel>
[[nodiscard]] SCAN_REGEX_FORCE_INLINE constexpr regex_result_for<pattern>
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
  std::ranges::for_each(
      std::views::iota(std::size_t{0}, input.size() + 1) |
          std::views::reverse,
      [&](std::size_t size) {
        if (!result) result = regex_match<pattern>(input.substr(0, size));
      });
  return result;
}

template <fixed_string pattern>
[[nodiscard]] constexpr regex_result_for<pattern> regex_search(std::string_view input) {
  regex_result_for<pattern> result;
  std::ranges::for_each(
      std::views::iota(std::size_t{0}, input.size() + 1),
      [&](std::size_t begin) {
        if (result) return;
        std::ranges::for_each(
            std::views::iota(begin, input.size() + 1) |
                std::views::reverse,
            [&](std::size_t end) {
              if (!result) {
                result = regex_match<pattern>(
                    input.substr(begin, end - begin));
              }
            });
      });
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
