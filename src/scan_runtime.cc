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

template <auto& automaton, bool in_words, std::size_t state,
          std::size_t register_count>
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

// What keeps a state: a handful of ranges of characters, and nothing else.
//
// One range covers a field of letters. Three cover the body of a quoted string,
// which is everything but the quote and the backslash. Six cover the local part
// of an address. The test costs one subtraction and one comparison per range
// per vector, so a class of several ranges is read very nearly as fast as a
// class of one, and the cases that are not one range are the common ones.
struct staying_class {
  std::array<unsigned char, 8> first{};
  std::array<unsigned char, 8> last{};
  std::size_t count = 0;
  // Whether every range ends below a hundred and twenty-eight, which is what
  // the word step needs and the vectors do not.
  bool below_the_high_bit = false;
};

#if defined(__clang__) || defined(__GNUC__)
#define SCAN_HAS_LANES 1
template <class lane_type>
[[nodiscard]] constexpr lane_type spread_over(unsigned char value) {
  lane_type made{};
  for (std::size_t index = 0; index < sizeof(lane_type); ++index) {
    made[index] = value;
  }
  return made;
}

// Ones where a character belongs to none of the ranges. A byte is inside a
// range exactly when subtracting the low end of it, in the arithmetic that
// wraps, lands at or below the width of it -- which holds for every byte there
// is, high bit or not.
// Written so that no vector is ever returned from anything: a vector wider than
// the machine has registers for is passed back through memory, and saying so is
// a warning about the calling convention on every build. Everything here is
// inlined and the accumulator never leaves the frame.
template <staying_class klass, class lane_type>
SCAN_FORCE_INLINE void outside_of(lane_type letters,
                                  decltype(std::declval<lane_type>() <
                                           std::declval<lane_type>())& answer) {
  const auto belongs = [&]<std::size_t index>(auto& into, bool first) {
    constexpr lane_type low = spread_over<lane_type>(klass.first[index]);
    constexpr lane_type span = spread_over<lane_type>(
        static_cast<unsigned char>(klass.last[index] - klass.first[index]));
    const auto here = (letters - low) <= span;
    if (first) {
      into = here;
    } else {
      into = into | here;
    }
  };
  [&]<std::size_t... index>(std::index_sequence<index...>) {
    belongs.template operator()<0>(answer, true);
    (belongs.template operator()<index + 1>(answer, false), ...);
  }(std::make_index_sequence<klass.count - 1>{});
  answer = ~answer;
}

// Did any of them fall out of the class? Not which -- any. The comparison
// collapses to one bit, and where the compiler can fold a vector down to a
// scalar it does it in a couple of instructions.
template <class lane_type>
[[nodiscard]] SCAN_FORCE_INLINE bool any_of(lane_type mask) {
#if __has_builtin(__builtin_reduce_or)
  return __builtin_reduce_or(mask) != 0;
#else
  std::uint64_t words[sizeof(lane_type) / 8];
  __builtin_memcpy(words, &mask, sizeof(mask));
  std::uint64_t together = 0;
  for (std::uint64_t word : words) together |= word;
  return together != 0;
#endif
}
#endif

template <staying_class klass>
[[nodiscard]] SCAN_FORCE_INLINE constexpr const char* skip_class(
    const char* cursor, const char* limit) {
  // Everything below reads several characters as one number and then asks which
  // end of it they came from, so it holds only where the first character is the
  // least significant byte. Elsewhere the caller reads them one at a time,
  // which is what it would have done anyway.
  if (std::is_constant_evaluated()) return cursor;
  if constexpr (std::endian::native != std::endian::little) {
    return cursor;
  } else {
#if SCAN_HAS_LANES
    // Sixty-four characters to a step and one question at the end of it.
    // Written as a vector of bytes and not as anything named after an
    // instruction set: on one machine each comparison is an SSE2 operation, on
    // another a NEON one, and where the compiler has no such register none of
    // it is compiled. Asking after every thirty-two costs more than the
    // comparison does -- the question is a branch, and that is a branch too
    // often.
    {
      using lane [[gnu::vector_size(32)]] = unsigned char;
      while (limit - cursor >= 64) {
        lane head{}, tail{};
        __builtin_memcpy(&head, cursor, 32);
        __builtin_memcpy(&tail, cursor + 32, 32);
        decltype(head < head) head_out{}, tail_out{};
        outside_of<klass>(head, head_out);
        outside_of<klass>(tail, tail_out);
        if (any_of(head_out | tail_out)) break;
        cursor += 64;
      }
      while (limit - cursor >= 32) {
        lane letters{};
        __builtin_memcpy(&letters, cursor, 32);
        decltype(letters < letters) outside{};
        outside_of<klass>(letters, outside);
        if (any_of(outside)) break;
        cursor += 32;
      }
    }
    {
      using lane [[gnu::vector_size(16)]] = unsigned char;
      while (limit - cursor >= 16) {
        lane letters{};
        __builtin_memcpy(&letters, cursor, 16);
        decltype(letters < letters) outside{};
        outside_of<klass>(letters, outside);
        if (any_of(outside)) break;
        cursor += 16;
      }
    }
#endif
    // Eight in a word, which is what a machine without vectors has and what the
    // last few characters fall back to in any case. For bytes under a hundred
    // and twenty-eight a byte is below a bound exactly when subtracting the
    // bound borrows out of it, and the borrow shows in a high bit cleared
    // beforehand; two such tests are the two ends of a range, a character is
    // foreign when it is foreign to every range, and one with its high bit
    // already set is foreign outright -- which it is only because this step is
    // not used for a class that reaches above the high bit.
    if constexpr (klass.below_the_high_bit) {
      constexpr std::uint64_t ones = 0x0101010101010101ull;
      constexpr std::uint64_t highs = 0x8080808080808080ull;
      while (limit - cursor >= 8) {
        std::uint64_t word = 0;
        __builtin_memcpy(&word, cursor, 8);
        std::uint64_t foreign = ~std::uint64_t{0};
        for (std::size_t index = 0; index < klass.count; ++index) {
          const std::uint64_t below =
              (word - ones * klass.first[index]) & ~word & highs;
          const std::uint64_t above =
              (word + ones * (127 - klass.last[index])) & ~word & highs;
          foreign &= below | above;
        }
        foreign |= word & highs;
        if (foreign != 0) {
          return cursor +
                 (static_cast<std::size_t>(std::countr_zero(foreign)) >> 3);
        }
        cursor += 8;
      }
    }
    return cursor;
  }
}

// What keeps this state, if the answer is simple enough to be worth asking:
// every range that returns to it costs no operation, and there are few enough
// of them that testing all of them together is still cheaper than reading one
// character at a time. Ranges that lead elsewhere are not part of it -- they
// end the run, which is what the caller then handles.
template <auto& automaton, std::size_t state>
[[nodiscard]] consteval staying_class staying_of() {
  constexpr const auto& packed = automaton.states[state];
  staying_class answer{};
  answer.below_the_high_bit = true;
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    const auto& range = packed.ranges[index];
    if (range.target != state) continue;
    if (range.command_count != 0) return staying_class{};
    if (answer.count == answer.first.size()) return staying_class{};
    // Ranges arrive in symbol order, so one that begins where the last ended is
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

template <auto& automaton, std::size_t state>
[[nodiscard]] consteval bool runs_in_place() {
  return staying_of<automaton, state>().count != 0;
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
  } else if constexpr (packed.ranges[index].target != state) {
    // A range that leads elsewhere is passed over without being compared
    // against. It used to be compared and then declined, which put the test for
    // the comma that ends a field inside the loop that reads the field -- one
    // comparison and one branch on every letter, to find something that happens
    // once. The ranges of a state do not overlap, so a symbol skipped here
    // cannot match any of the others either, and the answer is the same.
    return execute_tagged_self_transition<automaton, state, mark,
                                          register_count, index + 1>(
        symbol, registers, here);
  } else {
    constexpr const auto& range = packed.ranges[index];
    if (symbol >= range.first && symbol <= range.last) {
      execute_static_transition_commands<automaton, state, index>(registers,
                                                                  here);
      return true;
    }
    return execute_tagged_self_transition<automaton, state, mark,
                                          register_count, index + 1>(
        symbol, registers, here);
  }
}

template <auto& automaton, bool in_words, std::size_t state,
          std::size_t register_count, std::size_t index = 0>
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
          automaton, in_words, range.target>(cursor, end, registers);
    }
    return dispatch_tagged_transition<automaton, in_words, state,
                                      register_count, index + 1>(
        symbol, cursor, end, registers);
  }
}

template <auto& automaton, bool in_words, std::size_t state,
          std::size_t register_count>
[[nodiscard]] constexpr bool run_tagged_state_continuation(
    const char* cursor, const char* end,
    std::array<const char*, register_count>& registers) {
  if constexpr (in_words && runs_in_place<automaton, state>()) {
    cursor = skip_class<staying_of<automaton, state>()>(cursor, end);
  }
  while (cursor != end) {
    const unsigned char symbol = static_cast<unsigned char>(*cursor++);
    // The operations of a transition are the tags the state before it was
    // holding back, so they are written with the place from before this
    // symbol, which is where the cursor stood one character ago.
    if (execute_tagged_self_transition<automaton, state>(symbol, registers,
                                                         cursor - 1)) {
      continue;
    }
    return dispatch_tagged_transition<automaton, in_words, state>(
        symbol, cursor, end, registers);
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

template <auto& automaton, unsigned char sentinel, bool in_words,
          std::size_t state, std::size_t register_count>
[[nodiscard]] constexpr bool run_tagged_sentinel_continuation(
    const char* cursor, const char* limit,
    std::array<const char*, register_count>& registers);

template <auto& automaton, unsigned char sentinel, bool in_words,
          std::size_t state, std::size_t register_count, std::size_t index = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool
dispatch_tagged_sentinel_transition(
    unsigned char symbol, const char* cursor, const char* limit,
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
          automaton, sentinel, in_words, range.target>(cursor, limit,
                                                       registers);
    }
    return dispatch_tagged_sentinel_transition<automaton, sentinel, in_words,
                                               state, register_count,
                                               index + 1>(symbol, cursor, limit,
                                                          registers);
  }
}

template <auto& automaton, unsigned char sentinel, bool in_words,
          std::size_t state, std::size_t register_count>
[[nodiscard]] constexpr bool run_tagged_sentinel_continuation(
    const char* cursor, const char* limit,
    std::array<const char*, register_count>& registers) {
  // The limit is not what ends the match -- the terminator is -- and no
  // character is compared against it. It says only how far a word may be read
  // in one piece, which is a question about the subject and not about the
  // pattern.
  if constexpr (in_words && runs_in_place<automaton, state>()) {
    cursor = skip_class<staying_of<automaton, state>()>(cursor, limit);
  }
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
    return dispatch_tagged_sentinel_transition<automaton, sentinel, in_words,
                                               state>(symbol, cursor, limit,
                                                      registers);
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
// How much of the input the pattern takes, and nothing else about it.
//
// The walk that reads the fields is anchored: it answers whether the whole
// subject is the pattern. Asking how much of the subject the pattern takes is a
// different question and is asked here, by a walk that carries no registers,
// writes nothing and remembers only the furthest place the machine stood in a
// state that would have accepted. It is the same automaton and the same runs
// read in words and vectors; it is a good deal less code because there is
// nothing to record.
//
// Two passes over the head of the input, then -- one to find where it ends and
// one to read the fields out of it. That is the price of not disturbing the
// anchored walk, which is the one every other call goes through.
template <auto& automaton, std::size_t state>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool stays_here(unsigned char symbol) {
  constexpr const auto& packed = automaton.states[state];
  return [&]<std::size_t... index>(std::index_sequence<index...>) {
    return (false || ... ||
            (packed.ranges[index].target == state &&
             symbol >= packed.ranges[index].first &&
             symbol <= packed.ranges[index].last));
  }(std::make_index_sequence<packed.range_count>{});
}

template <auto& automaton, std::size_t state>
[[nodiscard]] constexpr const char* run_prefix_continuation(
    const char* cursor, const char* end, const char* best);

template <auto& automaton, std::size_t state, std::size_t index = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr const char* dispatch_prefix(
    unsigned char symbol, const char* cursor, const char* end,
    const char* best) {
  constexpr const auto& packed = automaton.states[state];
  if constexpr (index == packed.range_count) {
    return best;
  } else if constexpr (packed.ranges[index].target == state) {
    return dispatch_prefix<automaton, state, index + 1>(symbol, cursor, end,
                                                        best);
  } else {
    constexpr const auto& range = packed.ranges[index];
    if (symbol >= range.first && symbol <= range.last) {
      [[clang::always_inline]] return run_prefix_continuation<
          automaton, range.target>(cursor, end, best);
    }
    return dispatch_prefix<automaton, state, index + 1>(symbol, cursor, end,
                                                        best);
  }
}

template <auto& automaton, std::size_t state>
[[nodiscard]] constexpr const char* run_prefix_continuation(
    const char* cursor, const char* end, const char* best) {
  if constexpr (runs_in_place<automaton, state>()) {
    cursor = skip_class<staying_of<automaton, state>()>(cursor, end);
  }
  while (cursor != end &&
         stays_here<automaton, state>(static_cast<unsigned char>(*cursor))) {
    ++cursor;
  }
  // Standing in a state that accepts, the furthest place reached in it is the
  // furthest the pattern has taken so far. Inside a run nothing changes but the
  // cursor, so this is asked once when the run ends and not once a character.
  if constexpr (automaton.states[state].accepting_slot !=
                packed_state<0, 0, 0>::not_accepting) {
    best = cursor;
  }
  if (cursor == end) return best;
  return dispatch_prefix<automaton, state>(static_cast<unsigned char>(*cursor),
                                           cursor + 1, end, best);
}

// The head the pattern takes, or a view of nothing at all -- which is not the
// same as an empty head, and is told apart by pointing nowhere.
// The same walk, read out of the automaton instead of written into the code.
//
// Every state of the compiled form is an instantiation and every pattern is a
// constant evaluation: that is what makes a scan cost nothing when it runs,
// and a great deal when it is built. A test wants the answer and does not care
// what it cost to arrange, so this reads the automaton as data -- a state in a
// variable, a search through the transitions, a loop over the commands -- and
// asks the compiler for one function instead of one per state.
//
// It is the interpreter the compiled form was written to replace, kept because
// the two answer alike: the same determiniser, the same registers, the same
// order of operations. Only the schedule differs.
inline void execute_runtime_commands(
    const std::vector<scan::tre::register_command>& commands,
    std::vector<const char*>& registers, const char* here) {
  // Every source is read before any destination is written: commands on one
  // transition happen at once, and a copy must not see a register that another
  // command in the same breath has already changed.
  std::array<const char*, 64> room{};
  std::vector<const char*> spill;
  const bool roomy = commands.size() <= room.size();
  if (!roomy) spill.resize(commands.size());
  const auto source_at = [&](std::size_t which) -> const char*& {
    return roomy ? room[which] : spill[which];
  };
  for (std::size_t which = 0; which < commands.size(); ++which) {
    source_at(which) = commands[which].source
                           ? registers[*commands[which].source]
                           : nullptr;
  }
  for (std::size_t which = 0; which < commands.size(); ++which) {
    const scan::tre::register_command& command = commands[which];
    const char* value = command.source ? source_at(which) : nullptr;
    if (!command.values.empty()) value = command.values.back() ? here : nullptr;
    registers[command.destination] = value;
  }
}

[[nodiscard]] inline bool run_tagged_runtime(const scan::tre::tdfa& automaton,
                                             const char* cursor,
                                             const char* end,
                                             std::vector<const char*>& registers) {
  execute_runtime_commands(automaton.initialize, registers, cursor);
  std::size_t state = automaton.initial;
  while (cursor != end) {
    const unsigned char symbol = static_cast<unsigned char>(*cursor++);
    const scan::tre::tdfa_transition* taken = nullptr;
    for (const scan::tre::tdfa_transition& transition :
         automaton.states[state].transitions) {
      if (transition.symbols.test(symbol)) {
        taken = &transition;
        break;
      }
    }
    if (taken == nullptr) return false;
    // The operations of a transition are the tags the state before it was
    // holding back, so they are written with the place from before this
    // symbol.
    execute_runtime_commands(taken->commands, registers, cursor - 1);
    state = taken->target;
  }
  const scan::tre::tdfa_state& reached = automaton.states[state];
  if (!reached.accepting_slot.has_value()) return false;
  execute_runtime_commands(reached.final_commands, registers, cursor);
  return true;
}

// The longest head of the input the automaton accepts, or nothing.
[[nodiscard]] inline const char* run_prefix_runtime(
    const scan::tre::tdfa& automaton, const char* cursor, const char* end) {
  std::size_t state = automaton.initial;
  const char* best =
      automaton.states[state].accepting_slot.has_value() ? cursor : nullptr;
  while (cursor != end) {
    const unsigned char symbol = static_cast<unsigned char>(*cursor);
    const scan::tre::tdfa_transition* taken = nullptr;
    for (const scan::tre::tdfa_transition& transition :
         automaton.states[state].transitions) {
      if (transition.symbols.test(symbol)) {
        taken = &transition;
        break;
      }
    }
    if (taken == nullptr) break;
    state = taken->target;
    ++cursor;
    if (automaton.states[state].accepting_slot.has_value()) best = cursor;
  }
  return best;
}

template <class type, fixed_string format>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::string_view taken_prefix_or_none(
    std::string_view input) {
  const char* const begin = input.data();
  const char* best = nullptr;
  if constexpr (automata_at_runtime) {
    best = run_prefix_runtime(runtime_automaton<type, format>(), begin,
                              begin + input.size());
  } else {
    constexpr const auto& automaton = packed_automaton<type, format>;
    best = run_prefix_continuation<automaton, automaton.initial>(
        begin, begin + input.size(), nullptr);
  }
  if (best == nullptr) return {};
  return std::string_view(begin, static_cast<std::size_t>(best - begin));
}

template <class type, fixed_string format>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::string_view taken_prefix(
    std::string_view input) {
  const std::string_view head = taken_prefix_or_none<type, format>(input);
  if (head.data() == nullptr) {
    throw scan_error("input does not begin with the pattern");
  }
  return head;
}

// Whether the pattern is happy with nothing at all. Reading one match after
// another, such a pattern never moves and the reading never ends.
template <auto& automaton>
[[nodiscard]] consteval bool matches_nothing() {
  return automaton.states[automaton.initial].accepting_slot !=
         packed_state<0, 0, 0>::not_accepting;
}

template <class type, fixed_string format, int sentinel, bool terminated,
          bool absent_is_empty, std::size_t... index>
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
    // A compound statement, because that is what `if consteval` is written
    // with: the branch that is not the constant-evaluated one is a block, and
    // the choice of machine is made inside it.
    if constexpr (automata_at_runtime) {
      // Nothing here is a constant: the automaton is a value built on first use
      // and the walk is a loop over it. Not one instantiation per state, not one
      // determinisation per pattern while compiling.
      const scan::tre::tdfa& automaton = runtime_automaton<type, format>();
      std::vector<const char*> registers(automaton.register_count, nullptr);
      if (!run_tagged_runtime(automaton, input.data(),
                              input.data() + input.size(), registers)) {
        throw scan_error("input does not match scan expression");
      }
      const auto capture = [&]<std::size_t capture_index>() -> std::string_view {
        const char* const begin = registers[capture_index * 2];
        const char* const end = registers[capture_index * 2 + 1];
        if (begin == nullptr || end == nullptr) {
          // A group that took no part is an error where every group was meant to
          // take part, and the ordinary state of affairs where the format has
          // branches and only one of them ran.
          if constexpr (absent_is_empty) return std::string_view{};
          throw scan_error("capture group did not participate in the match");
        }
        return std::string_view(begin, static_cast<std::size_t>(end - begin));
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
      // Which terminator, if any.
      //
      // Asked for outright it is whatever the caller named, and it must be one
      // the pattern rejects in every state, which is required here. Not asked
      // for, it is still taken when the type of the input promises a null
      // character past its last -- a `std::string` always does -- and when the
      // pattern happens to reject that character. Where it does not, the loop
      // that tests the end of the input runs, and the caller never has to know
      // the question was asked.
      //
      // Not `sentinel != 0`, which is what this used to ask. Zero is the
      // terminator of every `std::string`, and so the one worth asking for; it
      // is also what a defaulted template parameter of a character type is,
      // which meant that asking for it politely was the same as not asking. The
      // absence is its own value now.
      constexpr unsigned char terminator =
          sentinel >= 0 ? static_cast<unsigned char>(sentinel) : 0;
      constexpr bool by_terminator =
          sentinel >= 0 ||
          (terminated && is_safe_tagged_sentinel<automaton, terminator>());
      if constexpr (by_terminator) {
        static_assert(is_safe_tagged_sentinel<automaton, terminator>(),
                      "the terminator must be rejected in every state");
        // Two machines, and the subject picks one. A field of five characters is
        // read faster one at a time than by a loop that first asks whether a
        // whole word will fit; a field of two hundred is read four times faster
        // in words. Asking once, here, costs one comparison for the match --
        // asking inside would cost one for every state it passes through.
        constexpr std::size_t worth_a_word = 32;
        if (input.size() >= worth_a_word) {
          [[clang::always_inline]] matched = run_tagged_sentinel_continuation<
              automaton, terminator, true, automaton.initial>(
              cursor, cursor + input.size(), registers);
    } else {
          [[clang::always_inline]] matched = run_tagged_sentinel_continuation<
              automaton, terminator, false, automaton.initial>(
              cursor, cursor + input.size(), registers);
        }
    } else {
        const char* const end = cursor + input.size();
        constexpr std::size_t worth_a_word = 32;
        if (input.size() >= worth_a_word) {
          [[clang::always_inline]] matched =
              run_tagged_state_continuation<automaton, true, automaton.initial>(
                  cursor, end, registers);
    } else {
          [[clang::always_inline]] matched =
              run_tagged_state_continuation<automaton, false, automaton.initial>(
                  cursor, end, registers);
        }
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
            // A group that took no part is an error where every group was meant
            // to take part, and is the ordinary state of affairs where the format
            // has branches and only one of them ran. There the empty view says
            // so: it points nowhere, which no group that did take part does.
            if constexpr (absent_is_empty) return std::string_view{};
            throw scan_error("capture group did not participate in the match");
          }
        }
        return std::string_view(begin, static_cast<std::size_t>(end - begin));
      };
      return std::array{capture.template operator()<index>()...};
    }
  }
}

template <class type, fixed_string format, int sentinel = -1,
          bool terminated = false>
[[nodiscard]] SCAN_FORCE_INLINE constexpr auto scan_fields(
    std::string_view input) {
  return scan_fields<type, format, sentinel, terminated, false>(
      input, std::make_index_sequence<groups_of<type>()>{});
}

// Every group of every branch, with the ones that took no part left empty.
//
// The count comes from the automaton and not from the output type: a format
// with branches has a group for each branch on top of the ones written down,
// and that is how the scan says which branch the input took.
template <class type, fixed_string format, int sentinel = -1,
          bool terminated = false>
[[nodiscard]] SCAN_FORCE_INLINE constexpr auto scan_branch_fields(
    std::string_view input) {
  return scan_fields<type, format, sentinel, terminated, true>(
      input, std::make_index_sequence<groups_of<type>()>{});
}

template <class type, std::size_t index>
using field_type = std::remove_cvref_t<decltype(
    boost::pfr::get<index>(std::declval<type&>()))>;

// One gathering per value the pattern reads, not one per field of the output.
//
// They are the same thing only where every field is one place. A field that is
// itself a shape is several places, and its own reader would then have to be
// handed the text and made to find the same boundaries a second time -- with a
// pattern that is a copy of the one already running. The boundaries are known
// here: the machine has a group for each of them. So each value is gathered by
// the reader of the type that value is, and the output is put together from
// those afterwards, the same way it is put together from pieces of a subject
// that can be pointed at.
template <class type, fixed_string format, std::size_t... group>
[[nodiscard]] constexpr auto make_scanner_state(
    std::index_sequence<group...>) {
  static constexpr auto spread = spread_of<type, format>();
  // A group that stands for a list gathers the list itself, which needs no
  // reader: what goes into it are whole elements, put there as each one ends.
  const auto one = []<std::size_t which>() {
    using held_type = leaf_kind<type, which>;
    if constexpr (scanned_as_range<held_type>) {
      return held_type{};
    } else {
      static_assert(requires {
        scanner_begin<held_type>(spread.parameters[which].view());
      }, "single-pass input requires incremental scan::scanner<T>");
      return scanner_begin<held_type>(spread.parameters[which].view());
    }
  };
  return std::tuple{one.template operator()<group>()...};
}

template <class type, fixed_string format>
[[nodiscard]] constexpr auto make_scanner_state() {
  return make_scanner_state<type, format>(
      std::make_index_sequence<groups_of<type>()>{});
}

// Following a reading instead of counting on the numbers.
//
// The machine stands in one state and in several readings of the input at once,
// and the registers are how the readings are kept apart. A field's text cannot
// be a piece of the subject here -- the subject is gone as it is read -- so it
// is gathered as it arrives, one gathering per register that holds the opening
// tag of that field. The gathering then goes where the register goes: a command
// that copies a register copies it, a command that writes a fresh position
// starts it again.
//
// Which register holds which tag, and which registers make up one reading, are
// both said by the automaton. They used to be worked out by dividing a register
// number by the number of tags, which was true of one way of handing registers
// out and of nothing else.
// Following a reading instead of counting on the numbers.
//
// The machine stands in one state and in several readings of the input at once,
// and the registers are how the readings are kept apart. A field's text cannot
// be a piece of the subject here -- the subject is gone as it is read -- so it
// is gathered as it arrives, one gathering per register that holds the opening
// tag of that field. The gathering goes where the register goes: a command that
// copies a register copies it, a command that writes a fresh position starts it
// again.
//
// The order is the whole of the correctness. The positions are written first,
// then the gatherings are moved the way the positions moved, and only then does
// the character go in -- so the character that opens a field is inside it and
// the character that closes one is not, which is what the positions say once
// they have been written and cannot be asked before.
//
// Which register holds which tag, and which registers make up one reading, are
// both said by the automaton. They used to be worked out by dividing a register
// number by the number of tags, which was true of one way of handing registers
// out and of nothing else.
template <std::size_t group, class type, fixed_string format, auto& automaton,
          class states_type, std::size_t register_count,
          std::size_t command_count>
constexpr void advance_scanner(
    char symbol, std::size_t state, std::ptrdiff_t position,
    const std::array<std::ptrdiff_t, register_count>& registers,
    const states_type& old_states, states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count) {
  static constexpr auto spread = spread_of<type, format>();
  constexpr std::size_t opening = group * 2;
  constexpr std::size_t closing = group * 2 + 1;
  using held_type = leaf_kind<type, group>;
  constexpr bool gathers_a_list = scanned_as_range<held_type>;
  // A field is gathered at the register holding its opening: nothing is written
  // inside a field, so that register stands still while the characters arrive.
  //
  // It is read at the register holding its closing, and the two are joined by a
  // copy made at the moment the group closes. Readings divide: two can hold the
  // same opening and disagree about whether the field is still being read, one
  // having met what follows it and one not, and one gathering cannot be both
  // "bob" and "bob id=7". The one that goes on goes on adding to the opening;
  // the one that has closed keeps the copy taken when it closed, and that is
  // what it is read from.
  //
  // A list is gathered and read at its opening throughout. Its elements go on
  // being added to the same list however the readings divide, and where it
  // began is what says which list that is.
  std::size_t command_index = 0;
  std::apply(
      [&](const auto&... command) {
        ([&] {
          if (command_index++ >= count) return;
          const std::uint32_t tag = automaton.register_tag[command.destination];
          if (tag != opening && tag != closing) return;
          if (tag == closing && registers[command.destination] == position) {
            // The group has closed on this step, however the closing came to
            // be written -- a fresh position, or a copy of a register that is
            // being written the same instant. Commands take their sources as
            // they stood before any of them ran, which is right for positions
            // and wrong for what was gathered: the copy would carry the empty
            // slot the closing had a moment ago. What the field gathered is in
            // the opening it was being added to.
            const auto& entered = automaton.states[state];
            for (std::size_t reading = 0; reading < entered.reading_count;
                 ++reading) {
              if (entered.readings[reading][closing] != command.destination) {
                continue;
              }
              std::get<group>(states[command.destination]) =
                  std::get<group>(states[entered.readings[reading][opening]]);
              break;
            }
          } else if (command.source != packed_command::no_source &&
                     command.value == -2) {
            // A reading that divides carries its gathering with it, whichever
            // of the two registers is being copied.
            std::get<group>(states[command.destination]) =
                std::get<group>(old_states[command.source]);
          } else if (tag == opening) {
            if constexpr (gathers_a_list) {
              std::get<group>(states[command.destination]) = held_type{};
            } else {
              std::get<group>(states[command.destination]) =
                  scanner_begin<held_type>(spread.parameters[group].view());
            }
          }
        }(),
         ...);
      },
      commands);
  // A list gathers elements, not characters. Written as an early return this
  // would discard nothing: what follows an `if constexpr` is not the branch it
  // did not take.
  if constexpr (!gathers_a_list) {
    // Once each, however many readings share it: a register is one gathering.
    std::array<bool, register_count> filled{};
    const auto& packed = automaton.states[state];
    for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
      const std::uint32_t open = packed.readings[reading][opening];
      const std::uint32_t close = packed.readings[reading][closing];
      if (filled[open]) continue;
      if (registers[open] < 0 || registers[close] >= registers[open]) continue;
      filled[open] = true;
      scanner_push<held_type>(std::get<group>(states[open]), symbol);
    }
  }
}

template <class root, class type, std::size_t offset, class reading_type,
          class states_type, std::size_t register_count>
[[nodiscard]] constexpr type finish_value(
    const reading_type& reading, const states_type& states,
    const std::array<std::ptrdiff_t, register_count>& registers);

// The parts of a product, and the arguments of a call, as named functions
// rather than as lambdas called where they stand. A lambda holding references
// and called inside the argument of something that itself holds references is
// more than the constant evaluator will follow.
template <class root, class type, std::size_t offset, class reading_type,
          class states_type, std::size_t register_count, std::size_t... part>
[[nodiscard]] constexpr type finish_parts(
    const reading_type& reading, const states_type& states,
    const std::array<std::ptrdiff_t, register_count>& registers,
    std::index_sequence<part...>) {
  return type{finish_value<root, typename parts_of<type>::template at<part>,
                           offset + groups_before_field<type, part>()>(
      reading, states, registers)...};
}

template <class root, class type, std::size_t offset, class reading_type,
          class states_type, std::size_t register_count, std::size_t... part>
[[nodiscard]] constexpr type finish_by_call(
    const reading_type& reading, const states_type& states,
    const std::array<std::ptrdiff_t, register_count>& registers,
    std::index_sequence<part...>) {
  return scan::scanner<std::remove_cv_t<type>>::parse(
      finish_value<root, typename parts_of<type>::template at<part>,
                   offset + groups_before_field<type, part>()>(reading, states,
                                                                registers)...);
}

// An element ends where the next one begins, and where that is, is said by a
// command writing a fresh position into the group the element starts at. The
// positions still hold the turn that is ending when this runs, which is why it
// runs before they move.
// A list is written to with push_back, which is what a range is asked for. It
// is reached here through insert where the type has it, because libc++ writes
// vector::emplace_back through a helper taking two capturing lambdas, and
// clang's constant evaluator refuses those ("captures not currently allowed"):
// a list of anything but a leaf could not be read while compiling. Appending
// at the end is the same thing either way.
template <class list_type, class element_type>
constexpr void append_to(list_type& list, element_type&& value) {
  if constexpr (requires { list.insert(list.end(), std::move(value)); }) {
    list.insert(list.end(), std::move(value));
  } else {
    list.push_back(std::move(value));
  }
}

template <std::size_t group, class type, fixed_string format, auto& automaton,
          class states_type, std::size_t register_count,
          std::size_t command_count>
constexpr void collect_element(
    std::size_t state,
    const std::array<std::ptrdiff_t, register_count>& registers,
    states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count) {
  if constexpr (group == 0) {
    return;
  } else if constexpr (!scanned_as_range<leaf_kind<type, group - 1>>) {
    return;
  } else {
    using list_type = leaf_kind<type, group - 1>;
    using element = std::remove_cvref_t<std::ranges::range_value_t<list_type>>;
    constexpr std::size_t list_group = group - 1;
    // Does this step begin another turn? It does if it writes a fresh position
    // into a register that holds the place an element starts at.
    bool going_round = false;
    for (std::size_t index = 0; index < count; ++index) {
      const auto& command = commands[index];
      if (command.value == -2) continue;
      if (automaton.register_tag[command.destination] == group * 2) {
        going_round = true;
        break;
      }
    }
    if (!going_round) return;
    // The turn that is ending belongs to the state being left, and so do the
    // positions and the gatherings. Where the list goes next is the business of
    // the commands, which carry the gathering with the register.
    const auto& packed = automaton.states[state];
    std::array<bool, register_count> done{};
    for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
      const std::uint32_t open = packed.readings[reading][group * 2];
      const std::uint32_t into = packed.readings[reading][list_group * 2];
      if (done[into] || registers[open] < 0) continue;
      done[into] = true;
      append_to(std::get<list_group>(states[into]),
                finish_value<type, element, group>(packed.readings[reading],
                                                   states, registers));
    }
  }
}

template <class type, fixed_string format, auto& automaton,
          std::size_t register_count, class states_type,
          std::size_t command_count, std::size_t... group>
constexpr void collect_elements(
    std::size_t state,
    const std::array<std::ptrdiff_t, register_count>& registers,
    states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count, std::index_sequence<group...>) {
  (collect_element<group, type, format, automaton>(state, registers, states,
                                                   commands, count),
   ...);
}

template <class type, fixed_string format, auto& automaton,
          std::size_t register_count, class states_type,
          std::size_t command_count, std::size_t... group>
constexpr void advance_scanners(
    char symbol, std::size_t state, std::ptrdiff_t position,
    const std::array<std::ptrdiff_t, register_count>& registers,
    states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count, std::index_sequence<group...>) {
  // The old gatherings are only needed where a command copies one, and inside a
  // field nothing is copied and nothing is written -- that is what holding the
  // tags back bought. Copying the whole set on every character to be ready for
  // a copy that is not coming is the difference between reading a long command
  // and reading it twice for nothing.
  bool copies = false;
  for (std::size_t index = 0; index < count; ++index) {
    if (commands[index].source != packed_command::no_source &&
        commands[index].value == -2) {
      copies = true;
      break;
    }
  }
  if (copies) {
    const states_type old_states = states;
    (advance_scanner<group, type, format, automaton>(
         symbol, state, position, registers, old_states, states, commands,
         count),
     ...);
  } else {
    (advance_scanner<group, type, format, automaton>(
         symbol, state, position, registers, states, states, commands,
         count),
     ...);
  }
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

// The output put together from the gatherings, walked the same way it is walked
// when the values are pieces of a subject that can be pointed at: a value asks
// its own reader to finish, a product asks its parts, a type made by a call
// makes it. Each value is taken from the gathering of the register that holds
// its opening tag in the reading that accepted.
template <class root, class type, std::size_t offset, class reading_type,
          class states_type, std::size_t register_count>
[[nodiscard]] constexpr type finish_value(
    const reading_type& reading, const states_type& states,
    const std::array<std::ptrdiff_t, register_count>& registers) {
  if constexpr (scanned_as_leaf<type>) {
    // A field still being read when the input ended is where it was being
    // gathered; one that ended earlier is the copy taken when it closed, which
    // the readings that went on adding to the opening cannot have changed.
    // The same question the gathering asks of every character: has this group
    // closed since it opened.
    const std::uint32_t open = reading[offset * 2];
    const std::uint32_t close = reading[offset * 2 + 1];
    const bool still_reading = registers[close] < registers[open];
    return scanner_finish<type>(
        std::get<offset>(states[still_reading ? open : close]));
  } else if constexpr (scanned_as_range<type>) {
    // What has been put in as each element ended, and then the one that was
    // still being read when the whole thing ended.
    using element = std::remove_cvref_t<std::ranges::range_value_t<type>>;
    type made = std::get<offset>(states[reading[offset * 2]]);
    // The turn that was still going when the whole thing ended. Where the list
    // is written to be allowed none at all, there may not have been one.
    if (registers[reading[(offset + 1) * 2]] >= 0) {
      append_to(made, finish_value<root, element, offset + 1>(reading, states,
                                                              registers));
    }
    return made;
  } else if constexpr (scanned_from_values<type>) {
    return finish_by_call<root, type, offset>(
        reading, states, registers,
        std::make_index_sequence<parts_of<type>::count>{});
  } else {
    return finish_parts<root, type, offset>(
        reading, states, registers,
        std::make_index_sequence<parts_of<type>::count>{});
  }
}

template <class type, fixed_string format>
class stream_state {
 private:
  inline static constexpr const auto& automaton =
      streaming_automaton<type, format>;
  inline static constexpr std::size_t field_count = groups_of<type>();
  // One gathering per register, because a gathering follows the register it
  // belongs to and there is no arithmetic that says which registers go
  // together.
  inline static constexpr std::size_t slot_count = automaton.register_count;
  using field_states = decltype(make_scanner_state<type, format>());

 public:
  constexpr stream_state() {
    std::ranges::fill(scanner_states_, make_scanner_state<type, format>());
    std::ranges::fill(registers_, scan::tre::negative_tag);
    execute_commands(automaton.initialize, automaton.initialize.size(),
                     registers_, std::ptrdiff_t{0});
  }

  constexpr void push(char symbol) {
    if (!offer(symbol)) state_ = packed_range<0>::reject;
  }

  // The same step, told rather than assumed: false means the machine could not
  // take this character and has not moved. Whoever is feeding it then knows
  // that what came before is as far as the pattern goes, and holds a character
  // that belongs to whatever comes next.
  [[nodiscard]] constexpr bool offer(char symbol) {
    if (state_ == packed_range<0>::reject) return false;
    const auto* transition = find_range(automaton.states[state_], static_cast<unsigned char>(symbol));
    if (transition == nullptr) return false;
    collect_elements<type, format, automaton>(
        state_, registers_, scanner_states_, transition->commands,
        transition->command_count, std::make_index_sequence<field_count>{});
    execute_commands(transition->commands, transition->command_count, registers_,
                     ++position_);
    advance_scanners<type, format, automaton>(
        symbol, transition->target, position_, registers_, scanner_states_,
        transition->commands, transition->command_count,
        std::make_index_sequence<field_count>{});
    state_ = transition->target;
    return true;
  }

  // Where the machine stands now: would what it has read so far be a whole
  // match? Asked between characters, this is how something that reads a command
  // as it is typed knows the command has arrived.
  [[nodiscard]] constexpr bool accepting() const {
    return state_ != packed_range<0>::reject &&
           automaton.states[state_].accepting_slot !=
               packed_state<0, 0, 0>::not_accepting;
  }

  // What has been gathered for a field so far, in the reading the machine would
  // prefer.
  //
  // Standing in several readings at once, the machine has several answers for a
  // field until the input decides between them; the readings are held in the
  // order the pattern gives them, so the first is the one that would win if the
  // match ended here. That is the one worth showing while a command is still
  // being typed.
  //
  // What comes back is the state of that field's own scanner, as far as it has
  // been fed -- for a field of fixed room, the characters themselves.
  template <std::size_t field>
  [[nodiscard]] constexpr const auto& gathering() const {
    const std::size_t here =
        state_ == packed_range<0>::reject ? automaton.initial : state_;
    // Where the value ends is where it is gathered.
    return std::get<field>(
        scanner_states_[automaton.states[here].readings[0][field * 2 + 1]]);
  }

  // Whether that field is being read right now: begun and not yet ended.
  template <std::size_t field>
  [[nodiscard]] constexpr bool reading() const {
    if (state_ == packed_range<0>::reject) return false;
    const auto& packed = automaton.states[state_];
    if (packed.reading_count == 0) return false;
    const std::uint32_t open = packed.readings[0][field * 2];
    const std::uint32_t close = packed.readings[0][field * 2 + 1];
    return registers_[open] >= 0 && registers_[close] < registers_[open];
  }

  [[nodiscard]] constexpr bool rejected() const {
    return state_ == packed_range<0>::reject;
  }

  // Accepting, with nowhere to go from here: the match is over and saying so
  // costs nothing, where finding out by offering the next character costs that
  // character. A pattern that ends in the thing that ends it -- a newline at
  // the end of a command -- settles like this, and then reading one after
  // another loses nothing at all.
  [[nodiscard]] constexpr bool settled() const {
    if (!accepting()) return false;
    const auto& packed = automaton.states[state_];
    for (std::size_t index = 0; index < packed.range_count; ++index) {
      if (packed.ranges[index].target != packed.ranges[index].reject) {
        return false;
      }
    }
    return true;
  }

  constexpr void restart() { *this = stream_state{}; }

  [[nodiscard]] constexpr type finish() const& {
    stream_state copy = *this;
    return std::move(copy).finish();
  }

  [[nodiscard]] constexpr type finish() && {
    if (state_ == packed_range<0>::reject) {
      throw scan_error("input does not match scan expression");
    }
    const auto slot = automaton.states[state_].accepting_slot;
    if (slot == packed_state<0, 0, 0>::not_accepting) {
      throw scan_error("input does not match scan expression");
    }
    // The reading that accepted says which register holds each value. Nothing
    // is written here: the commands that end a match are not run by this
    // machine, so a group that never closed is read from where it was being
    // gathered, which is what the registers say.
    const auto& reached = automaton.states[state_];
    return finish_value<type, type, 0>(reached.readings[slot], scanner_states_,
                                       registers_);
  }

 private:
  std::array<field_states, slot_count> scanner_states_{};
  std::array<std::ptrdiff_t, automaton.register_count> registers_{};
  std::size_t state_ = automaton.initial;
  std::ptrdiff_t position_ = 0;
};

// Whether stopping where the machine stops can be the wrong place.
//
// Reading an input that is gone once it is read, there is no going back: what
// the machine has taken, it has taken. That is right whenever leaving a state
// that accepts cannot land anywhere that does not -- then being unable to go on
// means the pattern ended here, and here is where it ended. Where a state that
// accepts leads to one that does not, a pattern like a number with an optional
// fraction can walk past its own ending and find nothing, and the answer would
// be a place already thrown away. Those are read from something that can be
// looked at twice.
template <auto& automaton>
[[nodiscard]] consteval bool can_walk_past_the_end() {
  constexpr std::size_t count = automaton.states.size();
  const auto accepts = [](std::size_t state) {
    return automaton.states[state].accepting_slot !=
           packed_state<0, 0, 0>::not_accepting;
  };
  std::array<bool, count> beyond{};
  for (std::size_t state = 0; state < count; ++state) {
    if (!accepts(state)) continue;
    const auto& packed = automaton.states[state];
    for (std::size_t index = 0; index < packed.range_count; ++index) {
      if (packed.ranges[index].target != packed.ranges[index].reject) {
        beyond[packed.ranges[index].target] = true;
      }
    }
  }
  for (bool changed = true; changed;) {
    changed = false;
    for (std::size_t state = 0; state < count; ++state) {
      if (!beyond[state]) continue;
      const auto& packed = automaton.states[state];
      for (std::size_t index = 0; index < packed.range_count; ++index) {
        const std::size_t target = packed.ranges[index].target;
        if (target == packed.ranges[index].reject || beyond[target]) continue;
        beyond[target] = true;
        changed = true;
      }
    }
  }
  for (std::size_t state = 0; state < count; ++state) {
    if (beyond[state] && !accepts(state)) return true;
  }
  return false;
}

template <class type, fixed_string format, std::ranges::input_range range_type>
[[nodiscard]] constexpr type scan_stream(range_type&& input) {
  stream_state<type, format> state;
  for (char symbol : input) { state.push(symbol); }
  return std::move(state).finish();
}

// The head of a range that is read once, and the character that ended it.
//
// Nothing is buffered: the characters go through the machine as they come, the
// values are gathered by the scanners of the fields themselves, and the one
// character the machine could not take is handed back with them, because it has
// been read and cannot be put back where it came from.
template <class type>
struct taken_ahead {
  type value;
  std::optional<char> stopped;
};

// By iterators rather than by a range, so that whoever holds them can go on
// from where this stopped -- which is what reading one match after another off
// a stream is.
template <class type, fixed_string format, class iterator_type,
          class sentinel_type>
[[nodiscard]] constexpr taken_ahead<type> scan_stream_prefix(
    iterator_type& first, sentinel_type last) {
  static_assert(
      !can_walk_past_the_end<streaming_automaton<type, format>>(),
      "this pattern can walk past its own ending, and an input read once "
      "cannot be walked back: scan the head of something that can be looked at "
      "twice");
  stream_state<type, format> state;
  std::optional<char> stopped;
  while (first != last) {
    const char symbol = static_cast<char>(*first);
    if (!state.offer(symbol)) {
      // Stop before stepping. Stepping is what reads the next character out of
      // the stream, and one taken and not used is one lost -- the character
      // that ended the match is already read and is handed back, and there is
      // no reason to take another with it.
      stopped = symbol;
      break;
    }
    ++first;
    // And where the machine can go nowhere from where it stands, it is over,
    // and nothing needs to be read to find that out.
    if (state.settled()) break;
  }
  return {std::move(state).finish(), stopped};
}

template <class type, fixed_string format, std::ranges::input_range range_type>
[[nodiscard]] constexpr taken_ahead<type> scan_stream_prefix(
    range_type&& input) {
  auto first = std::ranges::begin(input);
  return scan_stream_prefix<type, format>(first, std::ranges::end(input));
}

#undef SCAN_FORCE_INLINE


}  // namespace scan::detail
