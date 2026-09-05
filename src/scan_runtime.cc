export module scan.runtime;

import std;
import scan.tre;
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

// What a register holds: where in the subject something happened.
//
// Over a range that can be read once and not pointed into afterwards that has
// to be a count of characters seen. Over a contiguous subject it can be the
// address itself, and then nothing has to be added to it or taken from it --
// neither when it is written nor when a field is cut out of it at the end. Both
// are marks, and everything below is written for either.
template <class mark>
inline constexpr mark absent_mark = [] {
  if constexpr (std::is_pointer_v<mark>) {
    return nullptr;
  } else {
    return scan::tre::negative_tag;
  }
}();

template <class mark, std::size_t register_count>
SCAN_FORCE_INLINE constexpr void execute_command(
    const packed_command& command, mark source_value,
    std::array<mark, register_count>& registers, mark here) {
  mark value = absent_mark<mark>;
  if (command.source != packed_command::no_source) {
    value = source_value;
  }
  if (command.value == -1) value = absent_mark<mark>;
  if (command.value == 0) value = here;
  registers[command.destination] = value;
}

template <class mark, std::size_t register_count, std::size_t command_count>
SCAN_FORCE_INLINE constexpr void execute_commands(
    const std::array<packed_command, command_count>& commands,
    std::size_t count, std::array<mark, register_count>& registers, mark here) {
  std::array<mark, command_count> source_values{};
  std::size_t index = 0;
  for (const packed_command& command : commands | std::views::take(count)) {
    source_values[index++] = command.source == packed_command::no_source
                                 ? absent_mark<mark>
                                 : registers[command.source];
  }
  index = 0;
  for (const packed_command& command : commands | std::views::take(count)) {
    execute_command(command, source_values[index++], registers, here);
  }
}


// Not inlining the entry that carries the loop is what the recognition path
// does, and it was tried here: it made this slower, by about a tenth. The two
// are not the same shape -- this one hands back a structure of views that the
// caller takes apart field by field, and inlining that is worth more than the
// registers the loop gives up. Measured, not assumed, and left as it was.

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
    std::array<const char*, register_count>& registers);

template <auto& automaton, std::size_t state, std::size_t range, class mark,
          std::size_t register_count>
SCAN_FORCE_INLINE constexpr void execute_static_transition_commands(
    std::array<mark, register_count>& registers, mark here) {
  constexpr const auto& transition =
      automaton.states[state].ranges[range];
  [&]<std::size_t... index>(std::index_sequence<index...>)
      SCAN_FORCE_INLINE_LAMBDA {
        const std::array<mark, sizeof...(index)> source_values{
            (transition.commands[index].source == packed_command::no_source
                 ? absent_mark<mark>
                 : registers[transition.commands[index].source])...};
        (execute_command(transition.commands[index], source_values[index],
                         registers, here),
         ...);
      }(std::make_index_sequence<transition.command_count>{});
}

template <auto& automaton, std::size_t state, class mark,
          std::size_t register_count>
SCAN_FORCE_INLINE constexpr void execute_static_final_commands(
    std::array<mark, register_count>& registers, mark here) {
  constexpr const auto& packed_state = automaton.states[state];
  [&]<std::size_t... index>(std::index_sequence<index...>)
      SCAN_FORCE_INLINE_LAMBDA {
        const std::array<mark, sizeof...(index)> source_values{
            (packed_state.final_commands[index].source ==
                     packed_command::no_source
                 ? absent_mark<mark>
                 : registers[packed_state.final_commands[index].source])...};
        (execute_command(packed_state.final_commands[index],
                         source_values[index], registers, here),
         ...);
      }(std::make_index_sequence<packed_state.final_command_count>{});
}

template <auto& automaton, std::size_t state, class mark,
          std::size_t register_count, std::size_t index = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool
execute_tagged_self_transition(
    unsigned char symbol, std::array<mark, register_count>& registers,
    mark here) {
  constexpr const auto& packed = automaton.states[state];
  if constexpr (index == packed.range_count) {
    return false;
  } else {
    constexpr const auto& range = packed.ranges[index];
    if (symbol >= range.first && symbol <= range.last) {
      if constexpr (range.target != state) return false;
      execute_static_transition_commands<automaton, state, index>(registers,
                                                                  here);
      return true;
    }
    return execute_tagged_self_transition<automaton, state, mark,
                                          register_count, index + 1>(
        symbol, registers, here);
  }
}

template <auto& automaton, std::size_t state, std::size_t register_count,
          std::size_t index = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool
dispatch_tagged_transition(
    unsigned char symbol, const char* cursor, const char* end,
    std::array<const char*, register_count>& registers) {
  constexpr const auto& packed = automaton.states[state];
  if constexpr (index == packed.range_count) {
    return false;
  } else {
    constexpr const auto& range = packed.ranges[index];
    if (symbol >= range.first && symbol <= range.last) {
      if constexpr (range.target == state) return false;
      execute_static_transition_commands<automaton, state, index>(registers,
                                                                  cursor - 1);
      [[clang::always_inline]] return run_tagged_state_continuation<
          automaton, range.target>(cursor, end, registers);
    }
    return dispatch_tagged_transition<automaton, state, register_count,
                                      index + 1>(symbol, cursor, end,
                                                 registers);
  }
}

template <auto& automaton, std::size_t state, std::size_t register_count>
[[nodiscard]] constexpr bool run_tagged_state_continuation(
    const char* cursor, const char* end,
    std::array<const char*, register_count>& registers) {
  while (cursor != end) {
    const unsigned char symbol = static_cast<unsigned char>(*cursor++);
    // The operations of a transition are the tags the state before it was
    // holding back, so they are written with the place from before this
    // symbol, which is where the cursor stood one character ago.
    if (execute_tagged_self_transition<automaton, state>(symbol, registers,
                                                         cursor - 1)) {
      continue;
    }
    return dispatch_tagged_transition<automaton, state>(symbol, cursor, end,
                                                        registers);
  }
  if constexpr (automaton.states[state].accepting_slot ==
                packed_state<0, 0, 0>::not_accepting) {
    return false;
  } else {
    execute_static_final_commands<automaton, state>(registers, cursor);
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

// Which tags the machine cannot reach an accepting state without having
// written.
//
// A field is read out of two slots, and a slot that was never written holds the
// value that says the group took no part in the match -- which has to be
// checked before the field is handed back. For most patterns there is nothing
// to check: a group inside no alternation and under no star is written on
// every path there is, and the test is a branch that is never taken and never
// needed.
//
// So ask the automaton. Entering a state, a tag is certainly written if it is
// certainly written entering every state that leads here, or written by the
// step that led here; unreachable states start out claiming everything, and
// the answer falls to a fixed point. What every accepting state agrees on --
// its own closing operations included -- is what needs no test.
template <auto& automaton>
[[nodiscard]] consteval auto tags_always_written() {
  constexpr std::size_t tags = automaton.tag_count;
  constexpr std::size_t count = automaton.states.size();
  using row_type = std::array<bool, tags>;
  const auto note = [](row_type& row, const auto& commands, std::size_t total) {
    for (std::size_t index = 0; index < total; ++index) {
      const std::size_t destination = commands[index].destination;
      if (destination < tags) row[destination] = true;
    }
  };
  row_type start{};
  note(start, automaton.initialize, automaton.initialize.size());
  std::array<row_type, count> entry{};
  for (row_type& row : entry) row.fill(true);
  entry[automaton.initial] = start;
  for (bool changed = true; changed;) {
    changed = false;
    std::array<row_type, count> next{};
    for (row_type& row : next) row.fill(true);
    next[automaton.initial] = start;
    for (std::size_t state = 0; state < count; ++state) {
      const auto& packed = automaton.states[state];
      for (std::size_t index = 0; index < packed.range_count; ++index) {
        const auto& range = packed.ranges[index];
        if (range.target == range.reject) continue;
        row_type carried = entry[state];
        note(carried, range.commands, range.command_count);
        for (std::size_t tag = 0; tag < tags; ++tag) {
          next[range.target][tag] = next[range.target][tag] && carried[tag];
        }
      }
    }
    if (next != entry) {
      entry = next;
      changed = true;
    }
  }
  row_type answer{};
  answer.fill(true);
  for (std::size_t state = 0; state < count; ++state) {
    const auto& packed = automaton.states[state];
    if (packed.accepting_slot == packed.not_accepting) continue;
    row_type closing = entry[state];
    note(closing, packed.final_commands, packed.final_command_count);
    for (std::size_t tag = 0; tag < tags; ++tag) {
      answer[tag] = answer[tag] && closing[tag];
    }
  }
  return answer;
}

template <auto& automaton, unsigned char sentinel, std::size_t state,
          std::size_t register_count>
[[nodiscard]] constexpr bool run_tagged_sentinel_continuation(
    const char* cursor, std::array<const char*, register_count>& registers);

template <auto& automaton, unsigned char sentinel, std::size_t state,
          std::size_t register_count, std::size_t index = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool
dispatch_tagged_sentinel_transition(
    unsigned char symbol, const char* cursor,
    std::array<const char*, register_count>& registers) {
  constexpr const auto& packed = automaton.states[state];
  if constexpr (index == packed.range_count) {
    return false;
  } else {
    constexpr const auto& range = packed.ranges[index];
    if (symbol >= range.first && symbol <= range.last) {
      if constexpr (range.target == state) return false;
      execute_static_transition_commands<automaton, state, index>(registers,
                                                                  cursor - 1);
      [[clang::always_inline]] return run_tagged_sentinel_continuation<
          automaton, sentinel, range.target>(cursor, registers);
    }
    return dispatch_tagged_sentinel_transition<automaton, sentinel, state,
                                               register_count, index + 1>(
        symbol, cursor, registers);
  }
}

template <auto& automaton, unsigned char sentinel, std::size_t state,
          std::size_t register_count>
[[nodiscard]] constexpr bool run_tagged_sentinel_continuation(
    const char* cursor, std::array<const char*, register_count>& registers) {
  while (true) {
    const unsigned char symbol = static_cast<unsigned char>(*cursor++);
    if (execute_tagged_self_transition<automaton, state>(symbol, registers,
                                                         cursor - 1)) {
      continue;
    }
    if (symbol == sentinel) {
      if constexpr (automaton.states[state].accepting_slot ==
                    packed_state<0, 0, 0>::not_accepting) {
        return false;
      } else {
        execute_static_final_commands<automaton, state>(registers,
                                                        cursor - 1);
        return true;
      }
    }
    return dispatch_tagged_sentinel_transition<automaton, sentinel, state>(
        symbol, cursor, registers);
  }
}

// The machine belongs in this frame, not behind a call.
//
// The register file is a local array, and an array whose address is handed to a
// function the compiler keeps at arm's length has to live in memory: nine
// stores and ten loads for a five field subject, on a match that is over in
// thirty. Inlined, the address escapes nowhere, the array becomes values in
// registers, and the stack frame disappears entirely. It is asked for at the
// step from one state to the next as well as at the entry, so the whole walk
// arrives here and not just its first state.
//
// Where the states lead back into one another the request cannot be granted,
// and is not: the compiler says so and carries on with a call, which is what a
// pattern that loops has to pay anyway.
template <class type, fixed_string format, int sentinel,
          std::size_t... index>
[[nodiscard]] [[gnu::flatten]] SCAN_FORCE_INLINE constexpr auto scan_fields(
    std::string_view input, std::index_sequence<index...>) {
  if consteval {
    const auto matched = scan::tre::simulate(build_tnfa<type, format>(), input);
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
    std::array<const char*, automaton.register_count> registers{};
    // Only the slots that can still be unwritten when the machine accepts are
    // given the value that says a field took no part. Everything past the tags
    // is a working register, never read before it is written -- there is no
    // initialisation left at all -- and a tag written on every path does not
    // need telling either. For a pattern whose fields all take part, which is
    // most of them, there is nothing here to do.
    constexpr auto written_everywhere = tags_always_written<automaton>();
    [&]<std::size_t... tag>(std::index_sequence<tag...>) {
      ((written_everywhere[tag]
            ? void()
            : void(registers[tag] = nullptr)),
       ...);
    }(std::make_index_sequence<automaton.tag_count>{});
    execute_commands(automaton.initialize, automaton.initialize.size(),
                     registers, input.data());
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
    // Not `sentinel != 0`, which is what this used to ask. Zero is the
    // terminator of every `std::string`, and so the one worth asking for; it
    // is also what a defaulted template parameter of a character type is,
    // which meant that asking for it politely was the same as not asking. The
    // absence is its own value now.
    if constexpr (sentinel >= 0) {
      static_assert(is_safe_tagged_sentinel<automaton,
                                            static_cast<unsigned char>(sentinel)>(),
                    "the terminator must be rejected in every state");
      [[clang::always_inline]] matched = run_tagged_sentinel_continuation<
          automaton, static_cast<unsigned char>(sentinel), automaton.initial>(
          cursor, registers);
    } else {
      const char* const end = cursor + input.size();
      [[clang::always_inline]] matched =
          run_tagged_state_continuation<automaton, automaton.initial>(
              cursor, end, registers);
    }
    if (!matched) throw scan_error("input does not match scan expression");
    // Two of the three tests this used to make were asking whether the machine
    // had done something it cannot do. A position is written as the cursor
    // stands somewhere inside the subject, so it is never past the end; the
    // opening slot of a group is written before its closing one, so the length
    // is never negative. Only the third question is real, and only for a group
    // that some path can reach the end without entering -- which the automaton
    // is asked about while it is being compiled.
    constexpr auto always_written = tags_always_written<automaton>();
    const auto capture = [&]<std::size_t capture_index>() -> std::string_view {
      const auto begin = registers[capture_index * 2];
      const auto end = registers[capture_index * 2 + 1];
      if constexpr (!(always_written[capture_index * 2] &&
                      always_written[capture_index * 2 + 1])) {
        if (begin == nullptr) {
          throw scan_error("capture group did not participate in the match");
        }
      }
      return std::string_view(begin, static_cast<std::size_t>(end - begin));
    };
    return std::array{capture.template operator()<index>()...};
  }
}

template <class type, fixed_string format, int sentinel = -1>
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
    std::ranges::fill(registers_, scan::tre::negative_tag);
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
