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
  std::ranges::for_each(commands | std::views::take(count),
                        [&](const packed_command& command) {
    source_values[index++] = command.source == packed_command::no_source
                                 ? tre::negative_tag
                                 : registers[command.source];
  });
  index = 0;
  std::ranges::for_each(commands | std::views::take(count),
                        [&](const packed_command& command) {
    execute_command(command, source_values[index++], registers, position);
  });
}

template <class type, fixed_string format, std::size_t... index>
[[nodiscard]] SCAN_FORCE_INLINE constexpr auto scan_fields(
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
    if (state == packed_transition<0>::reject) {
      throw scan_error("input does not match scan expression");
    }
    const auto slot = automaton.states[state].accepting_slot;
    if (slot == packed_state<0, 0>::not_accepting) {
      throw scan_error("input does not match scan expression");
    }
    execute_commands(automaton.states[state].final_commands,
                     automaton.states[state].final_command_count, registers,
                     static_cast<std::ptrdiff_t>(input.size()));
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

template <class type, fixed_string format>
[[nodiscard]] SCAN_FORCE_INLINE constexpr auto scan_fields(
    std::string_view input) {
  return scan_fields<type, format>(
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
    if (state_ == packed_transition<0>::reject) return;
    const auto& transition = automaton.states[state_].transitions[
        static_cast<unsigned char>(symbol)];
    if (transition.target == transition.reject) {
      state_ = transition.reject;
      return;
    }
    advance_scanners<type, format>(
        symbol, automaton.tag_count, registers_, scanner_states_,
        transition.commands, transition.command_count,
        std::make_index_sequence<field_count>{});
    execute_commands(transition.commands, transition.command_count, registers_,
                     ++position_);
    state_ = transition.target;
  }

  [[nodiscard]] constexpr type finish() && {
    if (state_ == packed_transition<0>::reject) {
      throw scan_error("input does not match scan expression");
    }
    const auto slot = automaton.states[state_].accepting_slot;
    if (slot == packed_state<0, 0>::not_accepting) {
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
  std::ranges::for_each(input,
                        [&](char symbol) { state.push(symbol); });
  return std::move(state).finish();
}

#undef SCAN_FORCE_INLINE


}  // namespace scan::detail
