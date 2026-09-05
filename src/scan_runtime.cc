export module scan.runtime;

import std;
import tre;
import boost.pfr;
export import scan.compiler;

export namespace scan::detail {

#if defined(_MSC_VER)
#define SCAN_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define SCAN_FORCE_INLINE [[gnu::always_inline]] inline
#else
#define SCAN_FORCE_INLINE inline
#endif

template <std::size_t register_count>
SCAN_FORCE_INLINE constexpr void execute_command(
    const packed_command& command, std::ptrdiff_t source_value,
    std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position) {
  std::ptrdiff_t value = tre::negative_tag;
  if (command.source != packed_command::no_source) {
    value = source_value;
  }
  if (command.value == -1) value = tre::negative_tag;
  if (command.value == 0) value = position;
  registers[command.destination] = value;
}

template <std::size_t register_count, std::size_t command_count>
SCAN_FORCE_INLINE constexpr void execute_commands(
    const std::array<packed_command, command_count>& commands,
    std::size_t count, std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position) {
  std::array<std::ptrdiff_t, command_count> source_values{};
  std::size_t index = 0;
  for (const packed_command& command : commands | std::views::take(count)) {
    source_values[index++] = command.source == packed_command::no_source
                                 ? tre::negative_tag
                                 : registers[command.source];
  }
  index = 0;
  for (const packed_command& command : commands | std::views::take(count)) {
    execute_command(command, source_values[index++], registers, position);
  }
}


// The opposite request, for the entry that carries the scanning loop: inlined
// into a caller it shares registers with everything alive there, and the loop
// is encoded with the extended registers -- which is measurable, and which
// changes when an unrelated function appears beside it. The recognition path
// draws the same line at its entry.
#if defined(_MSC_VER) && !defined(__clang__)
#define SCAN_NEVER_INLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define SCAN_NEVER_INLINE [[gnu::noinline]]
#else
#define SCAN_NEVER_INLINE
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define SCAN_FORCE_INLINE_LAMBDA
#elif defined(__GNUC__) || defined(__clang__)
#define SCAN_FORCE_INLINE_LAMBDA [[gnu::always_inline]]
#else
#define SCAN_FORCE_INLINE_LAMBDA
#endif

// The generated form of a tagged automaton: a chain of comparisons per state,
// unrolled by the template recursion, with the register operations of each
// transition written out.
//
// It is parameterised by the automaton rather than by the pattern that made
// it, so that both users can reach it: `scan::match` over a regular
// expression, and the format path, which until now walked the same automaton
// with an interpreter -- a search through ranges and a loop over commands, per
// character, through pointers.

template <auto& automaton, std::size_t state, std::size_t register_count>
[[nodiscard]] constexpr bool run_tagged_state_continuation(
    const char* cursor, const char* end,
    std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position);

template <auto& automaton, std::size_t state, std::size_t range,
          std::size_t register_count>
SCAN_FORCE_INLINE constexpr void execute_static_transition_commands(
    std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position) {
  constexpr const auto& transition =
      automaton.states[state].ranges[range];
  [&]<std::size_t... index>(std::index_sequence<index...>)
      SCAN_FORCE_INLINE_LAMBDA {
        const std::array<std::ptrdiff_t, sizeof...(index)> source_values{
            (transition.commands[index].source == packed_command::no_source
                 ? tre::negative_tag
                 : registers[transition.commands[index].source])...};
        (execute_command(transition.commands[index], source_values[index],
                         registers, position),
         ...);
      }(std::make_index_sequence<transition.command_count>{});
}

template <auto& automaton, std::size_t state, std::size_t register_count>
SCAN_FORCE_INLINE constexpr void execute_static_final_commands(
    std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position) {
  constexpr const auto& packed_state = automaton.states[state];
  [&]<std::size_t... index>(std::index_sequence<index...>)
      SCAN_FORCE_INLINE_LAMBDA {
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

template <auto& automaton, std::size_t state, std::size_t register_count,
          std::size_t index = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool
execute_tagged_self_transition(
    unsigned char symbol,
    std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position) {
  constexpr const auto& packed = automaton.states[state];
  if constexpr (index == packed.range_count) {
    return false;
  } else {
    constexpr const auto& range = packed.ranges[index];
    if (symbol >= range.first && symbol <= range.last) {
      if constexpr (range.target != state) return false;
      execute_static_transition_commands<automaton, state, index>(registers,
                                                                position);
      return true;
    }
    return execute_tagged_self_transition<automaton, state, register_count,
                                          index + 1>(symbol, registers,
                                                     position);
  }
}

template <auto& automaton, std::size_t state, std::size_t register_count,
          std::size_t index = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool
dispatch_tagged_transition(
    unsigned char symbol, const char* cursor, const char* end,
    std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position) {
  constexpr const auto& packed = automaton.states[state];
  if constexpr (index == packed.range_count) {
    return false;
  } else {
    constexpr const auto& range = packed.ranges[index];
    if (symbol >= range.first && symbol <= range.last) {
      if constexpr (range.target == state) return false;
      execute_static_transition_commands<automaton, state, index>(registers,
                                                                position);
      return run_tagged_state_continuation<automaton, range.target>(
          cursor, end, registers, position + 1);
    }
    return dispatch_tagged_transition<automaton, state, register_count,
                                      index + 1>(symbol, cursor, end,
                                                 registers, position);
  }
}

template <auto& automaton, std::size_t state, std::size_t register_count>
[[nodiscard]] constexpr bool run_tagged_state_continuation(
    const char* cursor, const char* end,
    std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position) {
  while (cursor != end) {
    const unsigned char symbol = static_cast<unsigned char>(*cursor++);
    // The operations of a transition are the tags the state before it was
    // holding back, so they are written with the position from before this
    // symbol -- which is why the position advances after them, not before.
    if (execute_tagged_self_transition<automaton, state>(symbol, registers,
                                                        position)) {
      ++position;
      continue;
    }
    return dispatch_tagged_transition<automaton, state>(
        symbol, cursor, end, registers, position);
  }
  if constexpr (automaton.states[state].accepting_slot ==
                packed_state<0, 0, 0>::not_accepting) {
    return false;
  } else {
    execute_static_final_commands<automaton, state>(registers, position);
    return true;
  }
}


// The same automaton walked without an end pointer.
//
// A terminator the automaton rejects in every state ends the match by failing
// the class test, exactly as any other symbol that no transition takes would,
// so the loop carries one comparison per character instead of two. The class
// is tested first and the terminator afterwards -- it can never keep the
// automaton where it is, so asking about it first would only add a branch.
template <auto& automaton, unsigned char sentinel>
[[nodiscard]] consteval bool is_safe_tagged_sentinel() {
  for (const auto& state : automaton.states) {
    for (std::size_t index = 0; index < state.range_count; ++index) {
      const auto& range = state.ranges[index];
      if (sentinel >= range.first && sentinel <= range.last) return false;
    }
  }
  return true;
}

template <auto& automaton, unsigned char sentinel, std::size_t state,
          std::size_t register_count>
[[nodiscard]] constexpr bool run_tagged_sentinel_continuation(
    const char* cursor, std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position);

template <auto& automaton, unsigned char sentinel, std::size_t state,
          std::size_t register_count, std::size_t index = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool
dispatch_tagged_sentinel_transition(
    unsigned char symbol, const char* cursor,
    std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position) {
  constexpr const auto& packed = automaton.states[state];
  if constexpr (index == packed.range_count) {
    return false;
  } else {
    constexpr const auto& range = packed.ranges[index];
    if (symbol >= range.first && symbol <= range.last) {
      if constexpr (range.target == state) return false;
      execute_static_transition_commands<automaton, state, index>(registers,
                                                                  position);
      return run_tagged_sentinel_continuation<automaton, sentinel,
                                              range.target>(cursor, registers,
                                                            position + 1);
    }
    return dispatch_tagged_sentinel_transition<automaton, sentinel, state,
                                               register_count, index + 1>(
        symbol, cursor, registers, position);
  }
}

template <auto& automaton, unsigned char sentinel, std::size_t state,
          std::size_t register_count>
[[nodiscard]] constexpr bool run_tagged_sentinel_continuation(
    const char* cursor, std::array<std::ptrdiff_t, register_count>& registers,
    std::ptrdiff_t position) {
  while (true) {
    const unsigned char symbol = static_cast<unsigned char>(*cursor++);
    if (execute_tagged_self_transition<automaton, state>(symbol, registers,
                                                         position)) {
      ++position;
      continue;
    }
    if (symbol == sentinel) {
      if constexpr (automaton.states[state].accepting_slot ==
                    packed_state<0, 0, 0>::not_accepting) {
        return false;
      } else {
        execute_static_final_commands<automaton, state>(registers, position);
        return true;
      }
    }
    return dispatch_tagged_sentinel_transition<automaton, sentinel, state>(
        symbol, cursor, registers, position);
  }
}

template <class type, fixed_string format, unsigned char sentinel,
          std::size_t... index>
[[nodiscard]] SCAN_NEVER_INLINE constexpr auto scan_fields(
    std::string_view input, std::index_sequence<index...>) {
  if consteval {
    const auto matched = tre::simulate(build_tnfa<type, format>(), input);
    if (!matched.matched) throw scan_error("input does not match scan expression");
    const auto capture = [&]<std::size_t capture_index>() -> std::string_view {
      const auto& begins = matched.tags[capture_index * 2];
      const auto& ends = matched.tags[capture_index * 2 + 1];
      if (begins.empty() || ends.empty()) {
        throw scan_error("capture group did not participate in the match");
      }
      const auto begin = begins.back();
      const auto end = ends.back();
      if (begin < 0 || end < begin) throw scan_error("invalid capture group");
      return input.substr(static_cast<std::size_t>(begin),
                          static_cast<std::size_t>(end - begin));
    };
    return std::array{capture.template operator()<index>()...};
  } else {
    constexpr const auto& automaton = packed_automaton<type, format>;
    std::array<std::ptrdiff_t, automaton.register_count> registers{};
    std::ranges::fill(registers, tre::negative_tag);
    execute_commands(automaton.initialize, automaton.initialize.size(), registers,
                     0);
    // The generated form, not an interpreter.
    //
    // This walked the automaton one character at a time: a search through the
    // ranges of the current state, then a loop over that transition's
    // commands, both through pointers, and a state index carried in a
    // variable. The same automaton unrolled into comparisons against
    // constants is what the captureless path has always used, and it is the
    // difference between reading a table and running code.
    const char* cursor = input.data();
    bool matched = false;
    if constexpr (sentinel != 0) {
      static_assert(is_safe_tagged_sentinel<automaton, sentinel>(),
                    "the terminator must be rejected in every state");
      matched = run_tagged_sentinel_continuation<automaton, sentinel,
                                                 automaton.initial>(
          cursor, registers, 0);
    } else {
      const char* const end = cursor + input.size();
      matched = run_tagged_state_continuation<automaton, automaton.initial>(
          cursor, end, registers, 0);
    }
    if (!matched) throw scan_error("input does not match scan expression");
    const auto capture = [&]<std::size_t capture_index>() -> std::string_view {
      const auto begin = registers[capture_index * 2];
      const auto end = registers[capture_index * 2 + 1];
      if (begin < 0 || end < begin ||
          static_cast<std::size_t>(end) > input.size()) {
        throw scan_error("capture group did not participate in the match");
      }
      return input.substr(static_cast<std::size_t>(begin),
                          static_cast<std::size_t>(end - begin));
    };
    return std::array{capture.template operator()<index>()...};
  }
}

template <class type, fixed_string format, unsigned char sentinel = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr auto scan_fields(
    std::string_view input) {
  return scan_fields<type, format, sentinel>(
      input, std::make_index_sequence<boost::pfr::tuple_size_v<type>>{});
}

template <class type, std::size_t index>
using field_type = std::remove_cvref_t<decltype(
    boost::pfr::get<index>(std::declval<type&>()))>;

template <class type, fixed_string format, std::size_t... index>
[[nodiscard]] constexpr auto make_scanner_state(
    std::index_sequence<index...>) {
  constexpr auto parameters =
      field_parameters<format, boost::pfr::tuple_size_v<type>>();
  static_assert(
      (requires {
        scanner_begin<field_type<type, index>>(parameters[index]);
      } && ...),
      "single-pass input requires incremental scan::scanner<T>");
  return std::tuple{
      scanner_begin<field_type<type, index>>(parameters[index])...};
}

template <class type, fixed_string format>
[[nodiscard]] constexpr auto make_scanner_state() {
  return make_scanner_state<type, format>(
      std::make_index_sequence<boost::pfr::tuple_size_v<type>>{});
}

template <std::size_t field, class type, fixed_string format, class states_type,
          std::size_t register_count, std::size_t command_count>
constexpr void advance_scanner(
    char symbol, std::size_t tag_count,
    const std::array<std::ptrdiff_t, register_count>& registers,
    const states_type& old_states, states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count) {
  std::size_t command_index = 0;
  std::apply(
      [&](const auto&... command) {
        ([&] {
          if (command_index++ >= count ||
              command.destination % tag_count != field * 2) {
            return;
          }
          const std::size_t destination = command.destination / tag_count;
          constexpr auto parameters =
              field_parameters<format, boost::pfr::tuple_size_v<type>>();
          auto value = scanner_begin<field_type<type, field>>(parameters[field]);
          if (command.source != packed_command::no_source) {
            const std::size_t source = command.source / tag_count;
            value = std::get<field>(old_states[source]);
            const auto begin = registers[source * tag_count + field * 2];
            const auto end = registers[source * tag_count + field * 2 + 1];
            if (begin >= 0 && begin > end) {
              scanner_push<field_type<type, field>>(value, symbol);
            }
          }
          if (command.value != -2) {
            value = scanner_begin<field_type<type, field>>(parameters[field]);
          }
          std::get<field>(states[destination]) = std::move(value);
        }(),
         ...);
      },
      commands);
}

template <class type, fixed_string format, std::size_t register_count, class states_type,
          std::size_t command_count, std::size_t... field>
constexpr void advance_scanners(
    char symbol, std::size_t tag_count,
    const std::array<std::ptrdiff_t, register_count>& registers,
    states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count, std::index_sequence<field...>) {
  const states_type old_states = states;
  (advance_scanner<field, type, format>(symbol, tag_count, registers, old_states,
                                states, commands, count),
   ...);
}

template <class type, class state_type, std::size_t... index>
[[nodiscard]] constexpr type finish_scanners(
    state_type state, std::index_sequence<index...>) {
  type result{};
  ((boost::pfr::get<index>(result) =
        scanner_finish<field_type<type, index>>(
            std::move(std::get<index>(state)))),
   ...);
  return result;
}

template <class type, fixed_string format>
class stream_state {
 private:
  inline static constexpr const auto& automaton =
      packed_automaton<type, format>;
  inline static constexpr std::size_t field_count =
      boost::pfr::tuple_size_v<type>;
  inline static constexpr std::size_t slot_count =
      automaton.register_count / automaton.tag_count;
  using field_states = decltype(make_scanner_state<type, format>());

 public:
  constexpr stream_state() {
    std::ranges::fill(scanner_states_, make_scanner_state<type, format>());
    std::ranges::fill(registers_, tre::negative_tag);
    execute_commands(automaton.initialize, automaton.initialize.size(),
                     registers_, 0);
  }

  constexpr void push(char symbol) {
    if (state_ == packed_range<0>::reject) return;
    const auto* transition = find_range(automaton.states[state_], static_cast<unsigned char>(symbol));
    if (transition == nullptr) {
      state_ = packed_range<0>::reject;
      return;
    }
    advance_scanners<type, format>(
        symbol, automaton.tag_count, registers_, scanner_states_,
        transition->commands, transition->command_count,
        std::make_index_sequence<field_count>{});
    execute_commands(transition->commands, transition->command_count, registers_,
                     ++position_);
    state_ = transition->target;
  }

  [[nodiscard]] constexpr type finish() && {
    if (state_ == packed_range<0>::reject) {
      throw scan_error("input does not match scan expression");
    }
    const auto slot = automaton.states[state_].accepting_slot;
    if (slot == packed_state<0, 0, 0>::not_accepting) {
      throw scan_error("input does not match scan expression");
    }
    const std::size_t scanner_slot =
        automaton.states[state_].final_commands.front().source /
        automaton.tag_count;
    return finish_scanners<type>(
        std::move(scanner_states_[scanner_slot]),
        std::make_index_sequence<field_count>{});
  }

 private:
  std::array<field_states, slot_count> scanner_states_{};
  std::array<std::ptrdiff_t, automaton.register_count> registers_{};
  std::size_t state_ = automaton.initial;
  std::ptrdiff_t position_ = 0;
};

template <class type, fixed_string format, std::ranges::input_range range_type>
[[nodiscard]] constexpr type scan_stream(range_type&& input) {
  stream_state<type, format> state;
  for (char symbol : input) { state.push(symbol); }
  return std::move(state).finish();
}

#undef SCAN_FORCE_INLINE


}  // namespace scan::detail
