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

#if defined(__clang__)
#define SCAN_FORCE_INLINE_CALL [[clang::always_inline]]
#else
#define SCAN_FORCE_INLINE_CALL
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


template <auto& automaton, std::size_t state, std::size_t range, class mark,
          std::size_t register_count>
SCAN_FORCE_INLINE constexpr void execute_static_transition_commands(
    std::array<mark, register_count>& registers, mark here) {
  constexpr const auto& transition =
      automaton.states[state].ranges[range];
  [&]<std::size_t... index> SCAN_FORCE_INLINE_LAMBDA(
      std::index_sequence<index...>) {
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
  [&]<std::size_t... index> SCAN_FORCE_INLINE_LAMBDA(
      std::index_sequence<index...>) {
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

// What keeps a state: runs of characters, and nothing else.
//
// One run covers a field of letters. Three cover the body of a quoted string,
// which is everything but the quote and the backslash. Twelve cover the local
// part of an address, and the room here is for that: the class used to hold
// eight and a state with more of them was left to be read a character at a
// time, which is the one class in the benchmarks that most wanted the vectors.
struct staying_class {
  std::array<unsigned char, 16> first{};
  std::array<unsigned char, 16> last{};
  std::size_t count = 0;
  // Whether every range ends below a hundred and twenty-eight, which is what
  // the word step and the nibble tables need and the range compares do not.
  bool below_the_high_bit = false;
};

// Above this many runs the nibble tables answer for the class, where the
// machine can shuffle bytes; below it the runs are compared, which needs no
// tables and no loads.
inline constexpr std::size_t runs_worth_comparing_in_lanes = 3;

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

// Two tables of sixteen bytes, which answer for a whole class at once however
// many runs it has.
//
// A symbol below a hundred and twenty-eight is one nibble and another. The low
// table says, for a low nibble, which high nibbles carry a symbol of the class;
// the high table says which high nibble this is. One byte of each, and if they
// share a bit the symbol belongs. Symbols at or above a hundred and twenty-eight
// index the empty half of the high table and belong to nothing, which is what
// the class asked for anyway.
//
// Measured against the run compares on the local part of an address, in
// nanoseconds a character: 1.00 read one at a time, 0.29 comparing twelve runs
// in a vector, 0.062 here.
struct nibble_tables {
  std::array<unsigned char, 16> low{};
  std::array<unsigned char, 16> high{};
};

template <staying_class klass>
[[nodiscard]] consteval nibble_tables nibbles_of() {
  nibble_tables made{};
  for (unsigned symbol = 0; symbol < 128; ++symbol) {
    bool inside = false;
    for (std::size_t index = 0; index < klass.count; ++index) {
      if (symbol >= klass.first[index] && symbol <= klass.last[index]) {
        inside = true;
      }
    }
    if (inside) {
      made.low[symbol & 15] |= static_cast<unsigned char>(1u << (symbol >> 4));
    }
  }
  for (unsigned half = 0; half < 8; ++half) {
    made.high[half] = static_cast<unsigned char>(1u << half);
  }
  return made;
}

// The same sixteen bytes repeated to the width of the lane: the shuffle picks
// within each half of a wide register, so each half carries the whole table.
template <class lane_type, std::array<unsigned char, 16> table>
[[nodiscard]] constexpr lane_type table_over() {
  lane_type made{};
  for (std::size_t index = 0; index < sizeof(lane_type); ++index) {
    made[index] = table[index & 15];
  }
  return made;
}

#if defined(__SSSE3__) || defined(__AVX2__)
#define SCAN_HAS_SHUFFLE 1
template <class lane_type>
[[nodiscard]] SCAN_FORCE_INLINE lane_type shuffled(lane_type table,
                                                   lane_type picks) {
  using signed_lane [[gnu::vector_size(sizeof(lane_type))]] = char;
  if constexpr (sizeof(lane_type) == 32) {
    return static_cast<lane_type>(__builtin_ia32_pshufb256(
        static_cast<signed_lane>(table), static_cast<signed_lane>(picks)));
  } else {
    return static_cast<lane_type>(__builtin_ia32_pshufb128(
        static_cast<signed_lane>(table), static_cast<signed_lane>(picks)));
  }
}
#endif

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
#if defined(SCAN_HAS_SHUFFLE)
  if constexpr (klass.below_the_high_bit &&
                klass.count > runs_worth_comparing_in_lanes &&
                (sizeof(lane_type) == 16 || sizeof(lane_type) == 32)) {
    constexpr nibble_tables tables = nibbles_of<klass>();
    const lane_type low = table_over<lane_type, tables.low>();
    const lane_type high = table_over<lane_type, tables.high>();
    const lane_type fifteen = spread_over<lane_type>(15);
    const lane_type by_low = shuffled(low, letters & fifteen);
    const lane_type by_high = shuffled(high, (letters >> 4) & fifteen);
    answer = (by_low & by_high) == spread_over<lane_type>(0);
    return;
  }
#endif
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
    // A word at a time, and only for a class of a few runs: every run costs
    // four operations on the word, so eight runs cost more per eight
    // characters than reading them one at a time would.
    if constexpr (klass.below_the_high_bit &&
                  klass.count <= runs_worth_comparing_in_lanes) {
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

// Whether two runs of a state go to the same place and write the same thing.
//
// The captureless walk groups a state's runs by where they lead, so that what
// follows the move is written once for the place instead of once for the run.
// Here a move is where it leads and what it writes on the way, so runs are
// grouped by both -- a run that writes something of its own is a move of its
// own.
template <auto& automaton, std::size_t state>
[[nodiscard]] consteval bool runs_agree(std::size_t left, std::size_t right) {
  const auto& packed = automaton.states[state];
  const auto& one = packed.ranges[left];
  const auto& other = packed.ranges[right];
  if (one.target != other.target) return false;
  if (one.command_count != other.command_count) return false;
  for (std::size_t index = 0; index < one.command_count; ++index) {
    if (one.commands[index].destination != other.commands[index].destination) {
      return false;
    }
    if (one.commands[index].source != other.commands[index].source) {
      return false;
    }
    if (one.commands[index].value != other.commands[index].value) return false;
  }
  return true;
}

template <std::size_t capacity>
struct distinct_moves_of {
  std::array<std::size_t, capacity> at{};
  std::size_t count = 0;
};

// The moves a state can make, each named once by the first run that makes it.
template <auto& automaton, std::size_t state>
[[nodiscard]] consteval auto distinct_moves() {
  constexpr const auto& packed = automaton.states[state];
  distinct_moves_of<packed.ranges.size()> made;
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    bool named = false;
    for (std::size_t other = 0; other < made.count; ++other) {
      if (runs_agree<automaton, state>(made.at[other], index)) named = true;
    }
    if (!named) made.at[made.count++] = index;
  }
  return made;
}

// How many runs make the same move.
template <auto& automaton, std::size_t state, std::size_t move>
[[nodiscard]] consteval std::size_t runs_making() {
  constexpr const auto& packed = automaton.states[state];
  std::size_t count = 0;
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    if (runs_agree<automaton, state>(move, index)) ++count;
  }
  return count;
}

// Which symbols make this move, for a move that a handful of runs make.
template <auto& automaton, std::size_t state, std::size_t move>
inline constexpr auto move_table = [] consteval {
  constexpr const auto& packed = automaton.states[state];
  std::array<unsigned char, 256> made{};
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    if (!runs_agree<automaton, state>(move, index)) continue;
    for (std::size_t symbol = packed.ranges[index].first;
         symbol <= packed.ranges[index].last; ++symbol) {
      made[symbol] = 1;
    }
  }
  return made;
}();

template <auto& automaton, std::size_t state, std::size_t move,
          std::size_t index = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool makes_move_by_runs(
    unsigned char symbol) {
  constexpr const auto& packed = automaton.states[state];
  if constexpr (index == packed.range_count) {
    return false;
  } else if constexpr (!runs_agree<automaton, state>(move, index)) {
    return makes_move_by_runs<automaton, state, move, index + 1>(symbol);
  } else {
    constexpr const auto& range = packed.ranges[index];
    if (symbol >= range.first && symbol <= range.last) return true;
    return makes_move_by_runs<automaton, state, move, index + 1>(symbol);
  }
}

// Whether the symbol makes this move. Asked of a table where several runs make
// it, and of the runs themselves where one or two do -- which is the same
// bargain the captureless walk strikes, measured there.
template <auto& automaton, std::size_t state, std::size_t move>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool makes_move(
    unsigned char symbol) {
  if constexpr (runs_making<automaton, state, move>() >
                runs_worth_comparing_in_lanes) {
    return move_table<automaton, state, move>[symbol] != 0;
  } else {
    return makes_move_by_runs<automaton, state, move>(symbol);
  }
}

template <auto& automaton, std::size_t state, class mark,
          std::size_t register_count, std::size_t which = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool
execute_tagged_self_transition(
    unsigned char symbol, std::array<mark, register_count>& registers,
    mark here) {
  constexpr auto moves = distinct_moves<automaton, state>();
  if constexpr (which == moves.count) {
    return false;
  } else if constexpr (automaton.states[state].ranges[moves.at[which]].target !=
                       state) {
    // A move that leads elsewhere is passed over without being compared
    // against. It used to be compared and then declined, which put the test for
    // the comma that ends a field inside the loop that reads the field -- one
    // comparison and one branch on every letter, to find something that happens
    // once. The runs of a state do not overlap, so a symbol skipped here
    // cannot make any of the other moves either, and the answer is the same.
    return execute_tagged_self_transition<automaton, state, mark,
                                          register_count, which + 1>(
        symbol, registers, here);
  } else {
    constexpr std::size_t move = moves.at[which];
    if (makes_move<automaton, state, move>(symbol)) {
      execute_static_transition_commands<automaton, state, move>(registers,
                                                                 here);
      return true;
    }
    return execute_tagged_self_transition<automaton, state, mark,
                                          register_count, which + 1>(
        symbol, registers, here);
  }
}




// Which move keeps the machine here, or none. The move is what the gatherer
// needs: its commands are what a field's gathering follows.
template <auto& automaton, std::size_t state, class gatherer, class mark,
          std::size_t register_count, std::size_t which = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::size_t taken_self_move(
    unsigned char symbol, std::array<mark, register_count>& registers,
    mark here, gatherer& into) {
  constexpr auto moves = distinct_moves<automaton, state>();
  if constexpr (which == moves.count) {
    return no_run;
  } else if constexpr (automaton.states[state].ranges[moves.at[which]].target !=
                       state) {
    return taken_self_move<automaton, state, gatherer, mark, register_count,
                           which + 1>(symbol, registers, here, into);
  } else {
    constexpr std::size_t move = moves.at[which];
    if (makes_move<automaton, state, move>(symbol)) {
      // What a move is about to write is asked before it writes it: a list
      // takes in the turn that is ending, and what says the turn ended is the
      // registers as they stand now.
      into.template moving<state, move>(registers, here);
      execute_static_transition_commands<automaton, state, move>(registers,
                                                                 here);
      return move;
    }
    return taken_self_move<automaton, state, gatherer, mark, register_count,
                           which + 1>(symbol, registers, here, into);
  }
}

// Whether staying in this state writes anything.
//
// Where it does not, nothing a group is held in can change while the machine
// stays here, so whether a group is being gathered is the same for every
// character of the run and is worth asking once.
template <auto& automaton, std::size_t state>
[[nodiscard]] consteval bool staying_writes() {
  const auto& packed = automaton.states[state];
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    if (packed.ranges[index].target != state) continue;
    if (packed.ranges[index].command_count != 0) return true;
  }
  return false;
}

// A walk that gathers as it goes, over any pair of iterators.
//
// This is the walk over pointers, with two differences and no third. It reads
// through an iterator rather than a pointer, so it cannot step over a run in
// vectors; and it hands every character to whoever is gathering, because a
// subject that arrives as it is read leaves nothing behind to point at
// afterwards.
//
// Everything else is the same, and that is the point of it: the state is where
// it stands in this code, so a state's runs, its commands and the registers
// its groups are held in are all constants. What the machine that can be
// stopped and started has to look up on every character -- which state it is
// in, which runs that state has, which registers hold this group -- is not
// looked up here at all.


// How many states a state can move to, not counting itself. One is a chain:
// the machine goes there and nowhere else, and what follows can be written
// where the move is. More than one is a fork, and writing what follows at the
// fork would write it once per branch.
template <auto& automaton, std::size_t state>
[[nodiscard]] consteval std::size_t forks_of() {
  constexpr const auto& packed = automaton.states[state];
  std::array<std::size_t, packed.ranges.size()> named{};
  std::size_t count = 0;
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    const std::size_t target = packed.ranges[index].target;
    if (target == state) continue;
    bool already = false;
    for (std::size_t at = 0; at < count; ++at) {
      if (named[at] == target) already = true;
    }
    if (!already) named[count++] = target;
  }
  return count;
}

// Input that arrives in pieces, each of them characters in a row.
//
// A subject read one character at a time gives up two things: the vectors,
// which want the characters in a row, and every question the walk can answer
// by looking ahead. Almost no subject is really like that -- a file arrives a
// block at a time, a socket a datagram at a time -- and inside a piece the
// characters do lie in a row.
//
// So the walk reads a piece the way it reads a string, and where it runs out
// it asks for the next one and goes on in the state it is standing in. What it
// cannot do is point at what it has read: a piece is gone when the next one
// arrives, so the marks count characters and the fields gather as they go,
// exactly as they do for a subject read one character at a time.
// The walk's source of pieces, and what it has to remember about them.
//
// A piece is alive until the next one is asked for -- the room it lies in is
// filled again after that -- so a walk that goes past a match and then goes
// back to it cannot expect the characters it read to still be where it read
// them. The stream reading has the same trouble and answers it the same way:
// what was read past the place is carried, and handed out again before
// anything new.
//
// How much can be carried is a number the automaton gives: the longest walk
// out of a match that finds no other match, which is what the papers call the
// fallback and what this library refuses to read a stream without. What ends
// up here is at most that, because a walk that read further would have died
// before it got here.
template <class gatherer_type, class pieces_type, std::size_t hold = 0>
struct gathers_from_pieces : gatherer_type {
  pieces_type pieces;
  std::optional<std::ranges::iterator_t<pieces_type>> at;

  constexpr explicit gathers_from_pieces(gatherer_type inner,
                                         pieces_type given)
      : gatherer_type(std::move(inner)), pieces(std::move(given)) {}

  // Where the walk writes the place it kept, so that carrying the characters
  // it stands in can move it along with them.
  constexpr void watch(std::optional<const char*>& place,
                       std::optional<const char*>& upto) {
    place_ = &place;
    upto_ = &upto;
  }

  // The next characters, which are one of three things: what was carried from
  // a piece already given up, what is left of the piece the last reading
  // stopped in, or a piece nobody has read yet.
  [[nodiscard]] constexpr bool refill(const char*& from, const char*& to) {
    if (rest_from_ != rest_to_) {
      from = rest_from_;
      to = rest_to_;
      rest_from_ = rest_to_;
      piece_from_ = from;
      piece_to_ = to;
      return true;
    }
    carry_what_was_read_past();
    if (!at) at.emplace(std::ranges::begin(pieces));
    while (*at != std::ranges::end(pieces)) {
      auto piece = **at;
      ++*at;
      const char* const first = std::ranges::data(piece);
      const auto size = std::ranges::size(piece);
      if (size == 0) continue;
      from = first;
      to = first + size;
      piece_from_ = from;
      piece_to_ = to;
      return true;
    }
    return false;
  }

  // Back to the place the walk kept, and to the characters that were there.
  //
  // Where the place is still in the piece the walk is standing in, that is all
  // there is to it. Where it is not, it was carried when that piece was given
  // up -- and what the walk read from this piece since is carried now, so that
  // the next reading sees the same characters in the same order.
  constexpr void go_back_to(const char*& cursor, const char*& last) {
    if (place_ == nullptr || !place_->has_value()) return;
    const char* const kept = **place_;
    if constexpr (hold != 0) {
      if (kept >= held_.data() && kept <= held_.data() + held_count_) {
        std::array<char, hold> made{};
        std::size_t count = 0;
        for (const char* one = kept; one != held_.data() + held_count_ &&
                                     count != made.size();
             ++one) {
          made[count++] = *one;
        }
        for (const char* one = piece_from_;
             one != cursor && count != made.size(); ++one) {
          made[count++] = *one;
        }
        held_ = made;
        held_count_ = count;
        rest_from_ = cursor;
        rest_to_ = piece_to_;
        cursor = held_.data();
        last = held_.data() + held_count_;
        return;
      }
    }
    cursor = kept;
    if (upto_ != nullptr && upto_->has_value()) last = **upto_;
  }

 private:
  // A piece is about to be given up. Whatever of it the walk read after the
  // place it kept goes into the carry, and the place goes with it.
  constexpr void carry_what_was_read_past() {
    if constexpr (hold != 0) {
      if (place_ == nullptr || !place_->has_value()) return;
      const char* const kept = **place_;
      if (kept >= held_.data() && kept <= held_.data() + held_count_) {
        // Already carried, and everything in the piece being given up came
        // after it.
        carry(piece_from_, piece_to_);
      } else if (kept >= piece_from_ && kept <= piece_to_) {
        const std::size_t was = held_count_;
        carry(kept, piece_to_);
        *place_ = held_.data() + was;
        if (upto_ != nullptr) *upto_ = held_.data() + held_count_;
      }
    }
  }

  constexpr void carry(const char* from, const char* to) {
    if constexpr (hold != 0) {
      for (const char* one = from; one != to && held_count_ != held_.size();
           ++one) {
        held_[held_count_++] = *one;
      }
      if (upto_ != nullptr && upto_->has_value()) {
        *upto_ = held_.data() + held_count_;
      }
    }
  }

  // How much can be here is what the automaton says: a walk that read further
  // past a match than that would have died before it got here.
  std::array<char, hold == 0 ? 1 : hold> held_{};
  std::size_t held_count_ = 0;
  const char* piece_from_ = nullptr;
  const char* piece_to_ = nullptr;
  const char* rest_from_ = nullptr;
  const char* rest_to_ = nullptr;
  std::optional<const char*>* place_ = nullptr;
  std::optional<const char*>* upto_ = nullptr;
};

// Nothing gathered: what the walk hands over goes nowhere and costs nothing.
struct gathers_nothing {
  template <std::size_t state, std::size_t move, class registers_type,
            class mark>
  constexpr void moving(const registers_type&, mark) const {}
  template <std::size_t state, std::size_t landed, class registers_type,
            class mark>
  constexpr void moved(std::size_t, char, const registers_type&, mark) const {}
  template <std::size_t state, class registers_type>
  constexpr void ended(const registers_type&) const {}
};

// A gatherer that keeps every character it is handed, which is what a match
// over a subject read once hands back.
template <class held_type>
struct keeps_into {
  held_type& held;

  template <std::size_t state, std::size_t move, class registers_type,
            class mark>
  constexpr void moving(const registers_type&, mark) const {}
  template <std::size_t state, std::size_t landed, class registers_type,
            class mark>
  constexpr void moved(std::size_t, char letter, const registers_type&,
                       mark) const {
    held.push_back(letter);
  }
  template <std::size_t state, class registers_type>
  constexpr void ended(const registers_type&) const {}
};

// How a walk reads, and what it answers.
//
// Every walk in this library is this walk. What used to be seven bodies is
// seven settings: characters in a row or characters as they arrive, an end to
// stop at or a terminator to stop on, an answer of yes or no or of where the
// longest match ended, tags or none, a gatherer or nobody, vectors or not, and
// how far to write the chain of states out without calling.
struct walk_shape {
  // Step over a run in vectors. Wants characters in a row and nobody
  // gathering: what is stepped over is not read.
  bool in_words = false;
  // The reading ends on a symbol no state takes rather than at a limit, which
  // is one comparison a character instead of two.
  bool by_terminator = false;
  unsigned char terminator = 0;
  // Answer where the machine last stood in a state that accepts, rather than
  // whether the whole of the subject matched.
  //
  // This is the head, and it is not the same as where the machine stopped. A
  // walk can go past a match and die: `(?:ab)+` on "ababa" takes four
  // characters, steps onto the fifth and stops with nothing, and
  // `foreach|for|each` on "fore" goes past `for` because the order prefers the
  // longer branch and then finds it is not there. The place is kept, and the
  // registers with it where the automaton has anywhere to go from a match that
  // is not another match -- which is the fallback of the TDFA papers, asked
  // once while this is compiled.
  bool longest = false;
  // How far the chain of states is written out before the next one is reached
  // by a call.
  std::size_t budget = 0;
};

// Where the walk stopped, for the walks that answer that.
//
// Held as a maybe rather than as a place, because a reading of a subject that
// arrives as it is read is an iterator that cannot be made out of nothing and
// cannot be copied -- and such a subject is never asked where the longest head
// ended, because keeping the place would mean keeping the characters.
// Where a walk that is not looking for a head would have kept the place.
struct nothing_kept {};

// What the walk keeps of the place it liked best.
//
// The place itself, and -- where the machine can read past a match and die
// away from one -- the registers as they stood there, with that state's final
// operations already applied. That is the backup the TDFA papers put on the
// transitions out of a fallback state, and it is kept only by the walks whose
// automaton has one: where every step out of a match lands in another match,
// the registers at the end are the registers at the note and nothing is
// copied.
template <class cursor_type, class kept_type = nothing_kept>
struct walk_answer {
  bool matched = false;
  std::optional<cursor_type> at{};
  // How far the characters the place was kept in went.
  //
  // A walk that goes past a match reads on, and where the input arrives in
  // pieces it asks for the next piece while it does -- so by the time it dies
  // the end it is reading towards belongs to a different piece than the place
  // it kept. Going back to that place means going back to its end as well, or
  // the reading that follows runs from one piece to the end of another.
  std::optional<cursor_type> upto{};
  [[no_unique_address]] kept_type kept{};
};

// The end that goes with the place, where the two are the same kind of thing.
//
// A walk over characters in a row reads towards a pointer; a walk over
// anything else reads towards a sentinel, which says nothing about where a
// piece ends and is not kept.
template <class answer_type, class sentinel_type>
SCAN_FORCE_INLINE constexpr void keep_the_end(answer_type& best,
                                              const sentinel_type& last) {
  if constexpr (requires { best.upto = last; }) best.upto = last;
}

// The note taken where the machine stands in a match: the registers as they
// are, with this state's final operations applied to the copy rather than to
// them. Where the answer keeps no registers this is nothing at all.
template <auto& automaton, std::size_t state, class answer_type,
          class cursor_type, class mark, std::size_t register_count,
          class gatherer>
SCAN_FORCE_INLINE constexpr void keep_the_place(
    answer_type& best, const std::array<mark, register_count>& registers,
    const cursor_type& cursor, mark place, gatherer& into) {
  if constexpr (requires { best.kept = registers; }) {
    best.kept = registers;
    // Where the machine stands, said the way this walk says it: an address
    // where the characters lie in a row, and how far along otherwise.
    if constexpr (std::is_pointer_v<mark>) {
      execute_static_final_commands<automaton, state>(
          best.kept, static_cast<mark>(cursor));
    } else {
      execute_static_final_commands<automaton, state>(best.kept, place);
    }
    // And where somebody is gathering, the value is put together here, out of
    // the gatherings as they stand now. Nothing has to be copied and nothing
    // has to be undone: what the walk pushes into the gatherings after this
    // cannot reach a value already made, and a later match makes it again.
    into.template ended<state>(best.kept);
  }
}

template <auto& automaton, walk_shape shape, std::size_t state,
          std::size_t budget, std::size_t certain, class mark,
          class cursor_type, class sentinel_type, std::size_t register_count,
          class gatherer, class answer_type>
[[nodiscard]] constexpr bool run_continuation(
    cursor_type& cursor, sentinel_type last, mark& place,
    std::array<mark, register_count>& registers, gatherer& into,
    answer_type& best);

// Reached by a call, with the chain ahead of it written out again from there.
template <auto& automaton, walk_shape shape, std::size_t state, class mark,
          class cursor_type, class sentinel_type, std::size_t register_count,
          class gatherer, class answer_type>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool run_from_state(
    cursor_type& cursor, sentinel_type last, mark& place,
    std::array<mark, register_count>& registers, gatherer& into,
    answer_type& best) {
  return run_continuation<automaton, shape, state, shape.budget, 0, mark>(
      cursor, last, place, registers, into, best);
}

template <auto& automaton, walk_shape shape, std::size_t state,
          std::size_t budget, std::size_t certain, class mark,
          class cursor_type, class sentinel_type, std::size_t register_count,
          class gatherer, class answer_type, std::size_t which = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool dispatch_continuation(
    unsigned char symbol, cursor_type& cursor, sentinel_type last, mark& place,
    std::array<mark, register_count>& registers, gatherer& into,
    answer_type& best) {
  constexpr auto moves = distinct_moves<automaton, state>();
  if constexpr (which == moves.count) {
    // Nowhere to go. For a walk that wants the whole of the subject that is a
    // refusal; for one looking for a head it is the head, where this state
    // accepts -- and the operations that end a match are run here, for the
    // walks that keep no registers in the note and read them from where they
    // are.
    if constexpr (shape.longest) {
      constexpr bool accepts_here =
          automaton.states[state].accepting_slot !=
          packed_state<0, 0, 0>::not_accepting;
      if constexpr (accepts_here) {
        if constexpr (std::is_pointer_v<mark>) {
          execute_static_final_commands<automaton, state>(registers, place);
        } else {
          execute_static_final_commands<automaton, state>(registers, place - 1);
        }
        into.template ended<state>(registers);
        best.matched = true;
        return true;
      }
    }
    return best.matched;
  } else {
    constexpr std::size_t move = moves.at[which];
    constexpr const auto& range = automaton.states[state].ranges[move];
    if constexpr (range.target == state) {
      // Whether the symbol keeps the machine here is asked before this.
      return dispatch_continuation<automaton, shape, state, budget, certain,
                                   mark, cursor_type, sentinel_type,
                                   register_count, gatherer, answer_type,
                                   which + 1>(
          symbol, cursor, last, place, registers, into, best);
    } else {
      if (makes_move<automaton, state, move>(symbol)) {
        into.template moving<state, move>(registers, place);
        execute_static_transition_commands<automaton, state, move>(registers,
                                                                   place);
        into.template moved<state, range.target>(
            move, static_cast<char>(symbol), registers, place);
        if constexpr (budget != 0 && forks_of<automaton, state>() == 1) {
          // Written out here rather than called, and said so rather than left
          // to be guessed.
          //
          // The budget is what decides how much of the chain is worth writing
          // out, and it was only ever a hope: the optimiser stopped after a
          // handful of steps and left a call in the middle of a date, with the
          // spills around it costing more than the characters it went on to
          // read. Nineteen characters were four bodies and two calls; they are
          // one body and no calls now.
          //
          // Only this call. The one below ends the chain, and forcing that one
          // would ask an automaton with a cycle to write itself out for ever.
          SCAN_FORCE_INLINE_CALL
          return run_continuation<automaton, shape, range.target, budget - 1,
                                  certain>(cursor, last, place, registers, into,
                                           best);
        } else {
          return run_from_state<automaton, shape, range.target, mark>(
              cursor, last, place, registers, into, best);
        }
      }
      return dispatch_continuation<automaton, shape, state, budget, certain,
                                   mark, cursor_type, sentinel_type,
                                   register_count, gatherer, answer_type,
                                   which + 1>(
          symbol, cursor, last, place, registers, into, best);
    }
  }
}

template <auto& automaton, walk_shape shape, std::size_t state,
          std::size_t budget, std::size_t certain, class mark,
          class cursor_type, class sentinel_type, std::size_t register_count,
          class gatherer, class answer_type>
[[nodiscard]] constexpr bool run_continuation(
    cursor_type& cursor, sentinel_type last, mark& place,
    std::array<mark, register_count>& registers, gatherer& into,
    answer_type& best) {
  constexpr bool by_place = std::is_pointer_v<mark>;
  constexpr bool gathers = !std::same_as<gatherer, gathers_nothing>;
  constexpr bool accepts_here =
      automaton.states[state].accepting_slot !=
      packed_state<0, 0, 0>::not_accepting;

  if constexpr (shape.longest && accepts_here) {
    best.matched = true;
    best.at = cursor;
    keep_the_end(best, last);
    keep_the_place<automaton, state>(best, registers, cursor, place, into);
  }
  // Over the run this state keeps, in vectors -- only where the characters lie
  // in a row and nobody is gathering them, because what is stepped over is not
  // read. A head may be read this way too: what is stepped over is a run that
  // keeps the machine here, and where it stops is where the run ends.
  // Over the run this state keeps, in vectors.
  //
  // Wants the characters to lie in a row, which is a question about the
  // reading and not about the marks: input that arrives in pieces is in a row
  // inside a piece. Where somebody is gathering, what is stepped over is
  // handed to them as a piece -- one append instead of one a character -- and
  // where they cannot take a piece, the run is read a character at a time as
  // before.
  constexpr bool by_pointer = std::is_pointer_v<cursor_type>;
  constexpr bool takes_a_piece = requires(gatherer& one, const char* from) {
    one.template took_run<state>(from, from, registers, place);
  };
  if constexpr (shape.in_words && by_pointer && (!gathers || takes_a_piece) &&
                runs_in_place<automaton, state>()) {
    const cursor_type from = cursor;
    cursor = skip_class<staying_of<automaton, state>()>(cursor, last);
    if constexpr (gathers && takes_a_piece) {
      into.template took_run<state>(from, cursor, registers, place);
      if constexpr (!by_place) place += cursor - from;
    }
    if constexpr (shape.longest && accepts_here) {
      best.at = cursor;
      keep_the_end(best, last);
      keep_the_place<automaton, state>(best, registers, cursor, place, into);
    }
  }
  while (true) {
    // A character that is certainly there is read without asking whether it
    // is: the subject was measured against the shortest match before the first
    // one, so along a chain of states that each take one character the next is
    // known to exist. A state that can keep itself takes as many characters as
    // it likes, and then the count no longer says anything -- so it is only
    // spent where the state takes exactly one. A terminator answers the
    // question by itself.
    constexpr bool counts_here =
        certain != 0 && !runs_in_place<automaton, state>();
    if constexpr (!shape.by_terminator && !counts_here) {
      if (cursor == last) {
        // The reading ran out. Whoever is gathering may have more of it --
        // input that arrives in pieces is contiguous inside a piece, and the
        // walk goes on in the state it is standing in, because the state is
        // where it stands in this code and not a number to be put back.
        if constexpr (requires { into.refill(cursor, last); }) {
          if (!into.refill(cursor, last)) break;
        } else {
          break;
        }
      }
    }
    const unsigned char symbol = static_cast<unsigned char>(*cursor);
    ++cursor;
    // The operations of a transition are the tags the state before it was
    // holding back, so they are written with the mark of this symbol.
    if constexpr (by_place) {
      place = cursor - 1;
    } else {
      ++place;
    }
    const std::size_t stayed =
        taken_self_move<automaton, state, gatherer>(symbol, registers, place,
                                                    into);
    if (stayed != no_run) {
      if constexpr (gathers) {
        into.template moved<state, state>(stayed, static_cast<char>(symbol),
                                          registers, place);
      }
      if constexpr (shape.longest && accepts_here) {
        best.at = cursor;
        keep_the_end(best, last);
        keep_the_place<automaton, state>(best, registers, cursor, place, into);
      }
      continue;
    }
    // Tested after the class, not before: a terminator no state takes cannot
    // keep the machine where it is, so asking about it first would only add a
    // branch to every character.
    if constexpr (shape.by_terminator) {
      if (symbol == shape.terminator) {
        if constexpr (accepts_here) {
          execute_static_final_commands<automaton, state>(registers, place);
          into.template ended<state>(registers);
          return true;
        } else {
          return best.matched;
        }
      }
    }
    return dispatch_continuation<automaton, shape, state, budget,
                                 counts_here ? certain - 1 : 0, mark>(
        symbol, cursor, last, place, registers, into, best);
  }
  if constexpr (!accepts_here) {
    return best.matched;
  } else {
    if constexpr (shape.longest) {
      best.matched = true;
      best.at = cursor;
      keep_the_end(best, last);
    }
    if constexpr (by_place) {
      execute_static_final_commands<automaton, state>(registers, cursor);
    } else {
      execute_static_final_commands<automaton, state>(registers, place);
    }
    into.template ended<state>(registers);
    return true;
  }
}

// Walking characters in a row to a terminator, gathering nothing.
template <auto& automaton, unsigned char terminator, bool in_words,
          std::size_t state, std::size_t register_count>
[[nodiscard]] constexpr bool run_to_terminator(
    const char* cursor, const char* end,
    std::array<const char*, register_count>& registers) {
  gathers_nothing nothing;
  const char* place = cursor;
  walk_answer<const char*> best;
  constexpr walk_shape shape{
      .in_words = in_words, .by_terminator = true, .terminator = terminator};
  return run_continuation<automaton, shape, state, shape.budget, 0,
                          const char*>(cursor, end, place, registers, nothing,
                                       best);
}

// The head of characters in a row that the pattern takes, or nothing.
// The longest walk out of each state that never lands in a final state.
//
// This one relaxation answers both of the questions a reading has to ask
// about going past a match, and they are the questions the TDFA papers ask
// about fallback: not whether the automaton has a cycle -- that was too blunt
// by half -- but how long a walk can be that finds nothing.
//
// Out of a final state it is how far the machine can read past a match before
// it dies, and out of the start it is how much an attempt that comes to
// nothing can swallow. Where a walk can go round a cycle with no match along
// it, there is no number -- and that is said of the states it can happen from
// and not of the whole automaton, because the two are far apart in practice.
// The format `{},{}` spends the whole of its first field in such a cycle and
// still cannot read one character past a match: from the end there is no way
// back into it.
template <auto& automaton>
[[nodiscard]] consteval auto barren_walks() {
  constexpr std::size_t state_count =
      std::tuple_size_v<std::remove_cvref_t<decltype(automaton.states)>>;
  struct answer_type {
    std::array<std::size_t, state_count> longest{};
    std::array<bool, state_count> forever{};
  };
  answer_type answer;
  const auto accepts = [&](std::size_t state) {
    return automaton.states[state].accepting_slot !=
           packed_state<0, 0, 0>::not_accepting;
  };
  const auto relax = [&](const std::array<std::size_t, state_count>& from) {
    std::array<std::size_t, state_count> next{};
    for (std::size_t state = 0; state < state_count; ++state) {
      const auto& packed = automaton.states[state];
      std::size_t best = 0;
      for (std::size_t index = 0; index < packed.range_count; ++index) {
        const std::size_t target = packed.ranges[index].target;
        if (accepts(target)) continue;
        best = std::max(best, from[target] + 1);
      }
      next[state] = best;
    }
    return next;
  };
  // As many rounds as there are states settles every walk that ends. What is
  // still growing after that is going round.
  for (std::size_t round = 0; round < state_count; ++round) {
    const auto next = relax(answer.longest);
    if (next == answer.longest) return answer;
    answer.longest = next;
  }
  const auto once_more = relax(answer.longest);
  for (std::size_t state = 0; state < state_count; ++state) {
    answer.forever[state] = once_more[state] != answer.longest[state];
  }
  // And a state that can step into one of those has no number either.
  for (std::size_t round = 0; round < state_count; ++round) {
    bool changed = false;
    for (std::size_t state = 0; state < state_count; ++state) {
      if (answer.forever[state]) continue;
      const auto& packed = automaton.states[state];
      for (std::size_t index = 0; index < packed.range_count; ++index) {
        const std::size_t target = packed.ranges[index].target;
        if (accepts(target) || !answer.forever[target]) continue;
        answer.forever[state] = true;
        changed = true;
        break;
      }
    }
    if (!changed) break;
  }
  return answer;
}

// How far past a match the machine can read before it dies -- the fallback
// window of the TDFA papers. For `a+` it is zero: every state the machine
// stands in after a step is a final state, so wherever it stops it has a
// match and nothing was ever read past one. For `abc|abd` it is two. Where a
// final state can walk into a cycle with no match along it, there is no
// number.
template <auto& automaton>
[[nodiscard]] consteval std::size_t walk_past_a_match() {
  constexpr auto walks = barren_walks<automaton>();
  std::size_t window = 0;
  for (std::size_t state = 0; state < walks.longest.size(); ++state) {
    if (automaton.states[state].accepting_slot ==
        packed_state<0, 0, 0>::not_accepting) {
      continue;
    }
    if (walks.forever[state]) return std::numeric_limits<std::size_t>::max();
    window = std::max(window, walks.longest[state]);
  }
  return window;
}

// How many characters an attempt that comes to nothing can swallow -- the
// longest walk out of the start that never reaches a final state. These are
// the characters that have to be given back, because the attempt that starts
// one character later needs them. For `\s+` it is zero: a space is already a
// whole match, and anything else dies before it is taken. For `ab` it is one.
// For `a+b` there is no such number, and that is the pattern this refuses.
template <auto& automaton>
[[nodiscard]] consteval std::size_t walk_from_the_start() {
  constexpr auto walks = barren_walks<automaton>();
  if (walks.forever[automaton.initial]) {
    return std::numeric_limits<std::size_t>::max();
  }
  return walks.longest[automaton.initial];
}




template <auto& automaton, std::size_t state, std::size_t register_count>
[[nodiscard]] constexpr const char* run_head(
    const char* cursor, const char* end,
    std::array<const char*, register_count>& registers) {
  gathers_nothing nothing;
  const char* place = cursor;
  walk_answer<const char*> best;
  constexpr walk_shape shape{.longest = true};
  const bool found =
      run_continuation<automaton, shape, state, shape.budget, 0, const char*>(
          cursor, end, place, registers, nothing, best);
  return found ? *best.at : nullptr;
}

// Walking characters that lie in a row, gathering nothing: the shape almost
// every caller wants, said once.
template <auto& automaton, bool in_words, std::size_t state,
          std::size_t register_count>
[[nodiscard]] constexpr bool run_from_here(
    const char* cursor, const char* end,
    std::array<const char*, register_count>& registers) {
  gathers_nothing nothing;
  const char* place = cursor;
  walk_answer<const char*> best;
  constexpr walk_shape shape{.in_words = in_words};
  return run_continuation<automaton, shape, state, shape.budget, 0,
                          const char*>(cursor, end, place, registers, nothing,
                                       best);
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
// subject is the pattern. Asking how much of the subject the pattern takes is
// a different question, and it is the same walk told to answer it -- the
// longest place it stood in a state that would have accepted.
//
// Two passes over the head, still: one to find where it ends and one to read
// the fields out of it. The walk that finds it now carries the registers, so
// the second pass is a thing that could go rather than a thing that must stay.

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

// The head of the input and the fields out of it, in one walk.
//
// Finding where a head ends and reading what is in it were two walks over the
// same characters: the first carried no registers and threw away everything
// but the length, the second began again. The walk carries the registers, so
// what the second walk went to fetch is already in hand.
//
// The head ends at the last place the machine stood in a match, which is not
// always where it stopped: a walk can go on past a match, on the chance of a
// longer one that the order of the alternatives prefers, and die without
// finding it. `foreach|for|each` reading "fore" is that -- it goes past `for`
// after the `e`, dies at the end, and the answer is the place it kept.
template <class type, fixed_string format, bool absent_is_empty = false>
[[nodiscard]] constexpr auto taken_prefix_fields(std::string_view input) {
  constexpr const auto& automaton = packed_automaton<type, format>;
  constexpr std::size_t group_count = automaton.tag_count / 2;
  struct answer {
    std::string_view head;
    std::array<std::string_view, group_count> groups{};
    bool matched = false;
  };
  answer said;
  const char* const begin = input.data();
  std::array<const char*, automaton.register_count> registers{};
  constexpr auto written_everywhere = tags_always_written<automaton>();
  [&]<std::size_t... tag>(std::index_sequence<tag...>) {
    ((written_everywhere[tag] ? void() : void(registers[tag] = nullptr)), ...);
  }(std::make_index_sequence<automaton.tag_count>{});
  execute_commands(automaton.initialize, automaton.initialize.size(), registers,
                   begin);

  gathers_nothing nothing;
  // Where the machine can read past a match and die away from one, the
  // registers of the head are not the registers it died holding, so the note
  // keeps them. Where it cannot, the note is a pointer and nothing else.
  constexpr bool walks_past = walk_past_a_match<automaton>() != 0;
  using kept_type =
      std::conditional_t<walks_past,
                         std::array<const char*, automaton.register_count>,
                         nothing_kept>;
  walk_answer<const char*, kept_type> best;
  const char* cursor = begin;
  const char* place = begin;
  constexpr walk_shape shape{.longest = true};
  if (!run_continuation<automaton, shape, automaton.initial, shape.budget, 0,
                        const char*>(cursor, begin + input.size(), place,
                                     registers, nothing, best)) {
    return said;
  }
  said.matched = true;
  said.head =
      std::string_view(begin, static_cast<std::size_t>(*best.at - begin));
  // The registers of the head: the ones kept where it ended, or the ones in
  // hand where the machine could not have gone past it.
  const auto& said_by = [&]() -> const auto& {
    if constexpr (walks_past) {
      return best.kept;
    } else {
      return registers;
    }
  }();
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    ((said.groups[group] = [&]() -> std::string_view {
        const char* const from = said_by[group * 2];
        const char* const to = said_by[group * 2 + 1];
        // Pointing nowhere is how a group that took no part is said, here as
        // everywhere: whether that is a failure is decided by whoever asked,
        // and there is nothing to throw it at from inside a walk.
        if (from == nullptr || to == nullptr) return std::string_view{};
        return std::string_view(from, static_cast<std::size_t>(to - from));
      }()),
     ...);
  }(std::make_index_sequence<group_count>{});
  return said;
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
    std::array<const char*, automaton.register_count> registers{};
    best = run_head<automaton, automaton.initial>(begin, begin + input.size(),
                                                  registers);
  }
  if (best == nullptr) return {};
  return std::string_view(begin, static_cast<std::size_t>(best - begin));
}

// Whether the pattern is happy with nothing at all. Reading one match after
// another, such a pattern never moves and the reading never ends.
template <auto& automaton>
[[nodiscard]] consteval bool matches_nothing() {
  return automaton.states[automaton.initial].accepting_slot !=
         packed_state<0, 0, 0>::not_accepting;
}

template <class type, fixed_string format, int sentinel, bool terminated,
          bool absent_is_empty, how_to_walk walk, std::size_t... index>
[[nodiscard]] [[gnu::flatten]] SCAN_FORCE_INLINE constexpr auto scan_fields(
    std::string_view input, std::index_sequence<index...>)
    -> std::expected<std::array<std::string_view, sizeof...(index)>,
                     scan::failure> {
  // A group that took no part points nowhere, and that is how it is said all
  // the way through here: the three walks below each write it, and one place
  // at the end decides whether it is a failure or the ordinary state of
  // affairs. Nothing throws, because nothing here would be caught.
  const auto answer = [](std::array<std::string_view, sizeof...(index)> made)
      -> std::expected<std::array<std::string_view, sizeof...(index)>,
                       scan::failure> {
    if constexpr (!absent_is_empty) {
      for (const std::string_view one : made) {
        if (one.data() == nullptr) {
          return std::unexpected(scan::failure(
              no_group("capture group did not participate in the match")));
        }
      }
    }
    return made;
  };
  if consteval {
    const auto matched = scan::tre::simulate(build_tnfa<type, format>(), input);
    if (!matched.matched) {
      return std::unexpected(
          scan::failure(no_match("input does not match scan expression")));
    }
    const auto capture = [&]<std::size_t capture_index>() -> std::string_view {
      const auto& begins = matched.tags[capture_index * 2];
      const auto& ends = matched.tags[capture_index * 2 + 1];
      if (begins.empty() || ends.empty()) return std::string_view{};
      const auto begin = begins.back();
      const auto end = ends.back();
      if (begin < 0 || end < begin) return std::string_view{};
      return input.substr(static_cast<std::size_t>(begin),
                          static_cast<std::size_t>(end - begin));
    };
    return answer(std::array{capture.template operator()<index>()...});
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
        return std::unexpected(
            scan::failure(no_match("input does not match scan expression")));
      }
      const auto capture = [&]<std::size_t capture_index>() -> std::string_view {
        const char* const begin = registers[capture_index * 2];
        const char* const end = registers[capture_index * 2 + 1];
        // A group that took no part is an error where every group was meant to
        // take part, and the ordinary state of affairs where the format has
        // branches and only one of them ran. Which it is, is decided once, at
        // the end.
        if (begin == nullptr || end == nullptr) return std::string_view{};
        return std::string_view(begin, static_cast<std::size_t>(end - begin));
      };
      return answer(std::array{capture.template operator()<index>()...});
    } else {
      // Anchored to both ends of the subject, so the walks below a match are
      // kept: one of them may be the only walk that reaches the end, and the
      // answer is the first still accepting when it does.
      constexpr const auto& automaton = packed_automaton<type, format, false>;
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
        // Two machines, and the subject picks one. A field of five characters
        // is read faster one at a time than by a loop that first asks whether
        // a whole word will fit; a field of two hundred is read four times
        // faster in words. Asking once, here, costs one comparison for the
        // match -- asking inside would cost one for every state it passes
        // through.
        //
        // Where the caller said which walk they want, nothing is asked: the
        // length is not looked at, and only the walk they named is written.
        constexpr std::size_t worth_a_word = 32;
        constexpr bool asks = walk == how_to_walk::by_length;
        if (asks ? input.size() < worth_a_word
                 : walk == how_to_walk::one_at_a_time) {
          [[clang::always_inline]] matched =
              run_to_terminator<automaton, terminator, false,
                                automaton.initial>(
                  cursor, cursor + input.size(), registers);
    } else {
          [[clang::always_inline]] matched =
              run_to_terminator<automaton, terminator, true,
                                automaton.initial>(
                  cursor, cursor + input.size(), registers);
        }
    } else {
        const char* const end = cursor + input.size();
        constexpr std::size_t worth_a_word = 32;
        constexpr bool asks = walk == how_to_walk::by_length;
        if (asks ? input.size() < worth_a_word
                 : walk == how_to_walk::one_at_a_time) {
          [[clang::always_inline]] matched =
              run_from_here<automaton, false, automaton.initial>(
                  cursor, end, registers);
    } else {
          [[clang::always_inline]] matched =
              run_from_here<automaton, true, automaton.initial>(
                  cursor, end, registers);
        }
      }
      if (!matched) {
        return std::unexpected(
            scan::failure(no_match("input does not match scan expression")));
      }
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
          // A group that took no part points nowhere, which no group that did
          // take part does. Whether that is a failure is decided once, at the
          // end, and not here.
          if (begin == nullptr) return std::string_view{};
        }
        return std::string_view(begin, static_cast<std::size_t>(end - begin));
      };
      return answer(std::array{capture.template operator()<index>()...});
    }
  }
}

template <class type, fixed_string format, int sentinel = -1,
          bool terminated = false,
          how_to_walk walk = how_to_walk::by_length>
[[nodiscard]] SCAN_FORCE_INLINE constexpr auto scan_fields(
    std::string_view input) {
  return scan_fields<type, format, sentinel, terminated, false, walk>(
      input, std::make_index_sequence<groups_of<type>()>{});
}

// Every group of every branch, with the ones that took no part left empty.
//
// The count comes from the automaton and not from the output type: a format
// with branches has a group for each branch on top of the ones written down,
// and that is how the scan says which branch the input took.
template <class type, fixed_string format, int sentinel = -1,
          bool terminated = false,
          how_to_walk walk = how_to_walk::by_length>
[[nodiscard]] SCAN_FORCE_INLINE constexpr auto scan_branch_fields(
    std::string_view input) {
  return scan_fields<type, format, sentinel, terminated, true, walk>(
      input, std::make_index_sequence<groups_of<type>()>{});
}

template <class type, std::size_t index>
using field_type = std::remove_cvref_t<decltype(
    boost::pfr::get<index>(std::declval<type&>()))>;

// Nothing is gathered at the place a leaf that reads its own groups stands on:
// what it is built from are its groups, and they are gathered each at its own.
struct no_gathering {};

// A fold, and what it has been told.
//
// The state is the type's own -- it says how it is made and what it holds. The
// two arrays beside it are the walk's bookkeeping: which turn of each group the
// type has been told about, and whether that turn is still open. They travel
// with the state, because a reading that divides carries its fold with it, and
// what one reading has been told the other has not.
//
// A turn is known by the position its group opened at. Positions only ever move
// forward, so a group whose opening has moved is a new turn and is announced;
// one whose opening stands still is the same turn going on.
template <class held>
struct fold_of {
  using held_type = std::remove_cv_t<held>;
  static constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
  using state_type = decltype(scan::scanner<held_type>::begin_groups());

  state_type state = scan::scanner<held_type>::begin_groups();
  std::array<std::ptrdiff_t, inside> told_at{};
  std::array<bool, inside> open{};
  // Whether it asked for something this subject cannot give: its groups whole,
  // off a reading with nothing to point at.
  bool wanted_a_subject = false;
  // The first character of the subject this walk started on, where there is
  // one to point at. Then a group that closes is handed whole, and the
  // characters are never handed over one at a time. Off a stream this stays
  // nothing, and the fold is told the characters instead.
  const char* text = nullptr;

  constexpr fold_of() { told_at.fill(-1); }
};

// One step of a fold: what happened to the groups inside a place, said to the
// type in the order it can make sense of.
//
// Opened first and in the order they are written, because a group that opens
// inside another opens after it. Then the character, to every group it is
// inside of. Then the closings, innermost first, because a group that opens
// inside another closes before it.
//
// Nothing here asks what step the walk is on. It asks the positions, which say
// everything: a group whose opening has moved has begun a turn, one whose
// closing has caught up with its opening has ended one. So the same step run
// twice tells nothing twice, and the same step run at the end of the input --
// where there is no character to hand over -- finishes what the characters
// left open.
template <std::size_t place, class held, class reading_type, class fold_type,
          std::size_t register_count>
constexpr void fold_one_step(
    fold_type& fold, const reading_type& reading,
    const std::array<std::ptrdiff_t, register_count>& registers, char symbol,
    bool hands_the_character) {
  using held_type = std::remove_cv_t<held>;
  constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
  const auto opening_of = [&](std::size_t which) {
    return registers[reading[(place + 1 + which) * 2]];
  };
  const auto closing_of = [&](std::size_t which) {
    return registers[reading[(place + 1 + which) * 2 + 1]];
  };
  [&]<std::size_t... which>(std::index_sequence<which...>) {
    ((void)[&] {
      const std::ptrdiff_t began = opening_of(which);
      if (began < 0 || fold.told_at[which] == began) return;
      open_one_group<held_type, which>(fold.state);
      fold.told_at[which] = began;
      fold.open[which] = true;
    }(), ...);
  }(std::make_index_sequence<inside>{});
  if (hands_the_character) {
    [&]<std::size_t... which>(std::index_sequence<which...>) {
      ((void)[&] {
        if constexpr (takes_group_characters<held_type, which,
                                             typename fold_type::state_type> &&
                      !takes_the_group_whole<
                          held_type, which,
                          typename fold_type::state_type>) {
          if (!fold.open[which]) return;
          if (closing_of(which) >= opening_of(which)) return;
          push_one_group<held_type, which>(fold.state, symbol);
        } else if constexpr (takes_group_characters<
                                 held_type, which,
                                 typename fold_type::state_type>) {
          // It would take the group whole, and will where there is something
          // to point at. Where there is not, the characters are all there is.
          if (fold.text != nullptr) return;
          if (!fold.open[which]) return;
          if (closing_of(which) >= opening_of(which)) return;
          push_one_group<held_type, which>(fold.state, symbol);
        }
      }(), ...);
    }(std::make_index_sequence<inside>{});
  }
  [&]<std::size_t... step>(std::index_sequence<step...>) {
    ((void)[&] {
      constexpr std::size_t which = inside - 1 - step;
      if (!fold.open[which]) return;
      const std::ptrdiff_t began = opening_of(which);
      const std::ptrdiff_t ended = closing_of(which);
      if (ended < began) return;
      if constexpr (takes_the_group_whole<held_type, which,
                                          typename fold_type::state_type>) {
        // The whole of what the group stood on, pointed at rather than copied.
        //
        // A position here is how many characters have been read and not the
        // index of one: the walk writes a tag with the count after the
        // character that wrote it. So what a group stood on begins one before
        // where its opening says.
        if (fold.text != nullptr) {
          close_one_group<held_type, which>(
              fold.state,
              std::string_view(fold.text + (began > 0 ? began - 1 : 0),
                               static_cast<std::size_t>(ended - began)));
          fold.open[which] = false;
          return;
        }
        if constexpr (!takes_group_characters<held_type, which,
                                              typename fold_type::state_type>) {
          // It takes its groups whole and nothing else, and there is nothing
          // here to point at. Holding the characters to hand them over at the
          // end would be a hold with no bound, so this reading cannot be had --
          // said here and handed back where the value would have been, because
          // a walk has nobody to say it to.
          fold.wanted_a_subject = true;
          fold.open[which] = false;
          return;
        }
      }
      close_one_group<held_type, which>(fold.state);
      fold.open[which] = false;
    }(), ...);
  }(std::make_index_sequence<inside>{});
}

// Whether every group of a fold would take its group whole. Then a run of
// characters the walk stepped over costs one step and not one a character:
// nothing is handed over, and what opened and what closed is the same at both
// ends of a run, because a run is where nothing is written.
template <class held, class state_type>
[[nodiscard]] consteval bool every_group_whole() {
  using held_type = std::remove_cv_t<held>;
  return []<std::size_t... which>(std::index_sequence<which...>) {
    return (true && ... &&
            takes_the_group_whole<held_type, which, state_type>);
  }(std::make_index_sequence<groups_a_leaf_opens<held_type>()>{});
}

// The fold of every reading that stands at this state, told the same step once.
//
// A fold lives at the register holding the place's opening, which is the
// discipline a list is gathered by: readings that share that register share
// what is gathered there, and where two readings would have to disagree the
// machine has already given them registers of their own.
template <std::size_t place, class held, auto& automaton, class states_type,
          std::size_t register_count>
constexpr void fold_the_readings(
    std::size_t state,
    const std::array<std::ptrdiff_t, register_count>& registers,
    states_type& states, char symbol, bool hands_the_character,
    const char* text) {
  const auto& entered = automaton.states[state];
  std::array<bool, register_count> told{};
  for (std::size_t reading = 0; reading < entered.reading_count; ++reading) {
    const std::uint32_t at = entered.readings[reading][place * 2];
    if (told[at] || registers[at] < 0) continue;
    told[at] = true;
    auto& folding = std::get<place>(states[at]);
    // Said every step rather than once, because a fold is made where its place
    // opens and carried where a reading divides, and neither of those knows
    // what the walk is reading.
    folding.text = text;
    fold_one_step<place, held>(folding, entered.readings[reading], registers,
                               symbol, hands_the_character);
  }
}

// How one group is gathered, made once and asked at every place that gathers.
//
// A leaf that is built from the groups its own pattern opens is not handed the
// text it stands on, so its place gathers nothing and each of its groups
// gathers characters. Every other group is gathered by the reader of the type
// it holds, which is what it was before any of this.
template <class type, fixed_string format, std::size_t group>
struct gathering_of {
  using held_type = leaf_kind<type, group>;
  static constexpr bool by_groups = gathers_by_its_groups<held_type>;
  static constexpr bool folds = folds_by_turns<std::remove_cv_t<held_type>>;
  static constexpr bool the_place = by_groups && leaf_offset_of<type, group> == 0;
  static constexpr bool inside = by_groups && leaf_offset_of<type, group> != 0;

  [[nodiscard]] static constexpr auto begin(std::string_view parameters) {
    if constexpr (the_place && folds) {
      // The type's own state, and the walk's note of what it has been told.
      static_cast<void>(parameters);
      return fold_of<std::remove_cv_t<held_type>>{};
    } else if constexpr (inside && folds) {
      // Nothing: the characters and the edges of this group go to the fold,
      // which is kept at the place the group is inside of.
      static_cast<void>(parameters);
      return no_gathering{};
    } else if constexpr (the_place || inside) {
      // A leaf read from its groups after the match gathers nothing at all:
      // the positions say where each of its groups stood, and the subject is
      // still there to be pointed at -- which is why such a leaf is refused
      // where the subject is not.
      static_cast<void>(parameters);
      return no_gathering{};
    } else {
      static_assert(requires { scanner_begin<held_type>(parameters); },
                    "single-pass input requires incremental scan::scanner<T>");
      return scanner_begin<held_type>(parameters);
    }
  }

  template <class state_type>
  static constexpr void push(state_type& state, char letter) {
    if constexpr (the_place || inside) {
      // A fold is handed its characters by the group they fell in, which the
      // place does for all of its groups at once and in order; a leaf read
      // after the match is handed nothing at all.
      static_cast<void>(state);
      static_cast<void>(letter);
    } else {
      scanner_push<held_type>(state, letter);
    }
  }
};

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
      return gathering_of<type, format, which>::begin(
          spread.parameters[which].view());
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
// The gatherings a transition's copies will read, and only those.
//
// A transition that copies a register needs that register's gathering as it
// was before the transition, so what was here copied the whole set on every
// character -- every register's gathering of every field, to be ready for a
// copy of one or two of them. On a subject read a character at a time that was
// most of what reading it cost.
template <class states_type, std::size_t command_capacity>
struct kept_gatherings {
  using held_type = typename states_type::value_type;
  std::array<std::size_t, command_capacity> which{};
  std::array<held_type, command_capacity> held{};
  std::size_t count = 0;

  [[nodiscard]] constexpr const held_type& operator[](
      std::size_t source) const {
    for (std::size_t at = 0; at < count; ++at) {
      if (which[at] == source) return held[at];
    }
    return held[0];
  }
};

template <class states_type, std::size_t command_count>
[[nodiscard]] constexpr auto keep_gatherings(
    const states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count) {
  kept_gatherings<states_type, command_count> kept;
  for (std::size_t index = 0; index < count; ++index) {
    if (commands[index].source == packed_command::no_source) continue;
    if (commands[index].value != -2) continue;
    bool already = false;
    for (std::size_t at = 0; at < kept.count; ++at) {
      if (kept.which[at] == commands[index].source) already = true;
    }
    if (already) continue;
    kept.which[kept.count] = commands[index].source;
    kept.held[kept.count] = states[commands[index].source];
    ++kept.count;
  }
  return kept;
}

template <std::size_t group, class type, fixed_string format, auto& automaton,
          bool hands_the_character = true, class states_type, class kept_type,
          std::size_t register_count, std::size_t command_count>
constexpr void advance_scanner(
    char symbol, std::size_t state, std::ptrdiff_t position,
    const std::array<std::ptrdiff_t, register_count>& registers,
    const kept_type& old_states, states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count, const char* text) {
  static constexpr auto spread = spread_of<type, format>();
  constexpr std::size_t opening = group * 2;
  constexpr std::size_t closing = group * 2 + 1;
  using held_type = leaf_kind<type, group>;
  constexpr bool gathers_a_list = scanned_as_range<held_type>;
  using how = gathering_of<type, format, group>;
  // A group inside a folding place is not gathered at all: its place tells the
  // fold what happened to it, and does that for all of its groups in one go,
  // where the order can be got right.
  if constexpr (how::folds && how::inside) return;
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
  // Two passes, because a transition can rename a group's opening and write
  // its closing at once: `r86(t3) <- position` beside `r77(t2) <- r80` is a
  // field ending and its opening moving house in the same breath. The copy has
  // to happen first, or the copy taken for the closing is of a register that
  // has not been given what it holds yet.
  std::size_t command_index = 0;
  std::apply(
      [&](const auto&... command) {
        ([&] {
          if (command_index++ >= count) return;
          const std::uint32_t tag = automaton.register_tag[command.destination];
          if (tag == opening) {
            if (command.source != packed_command::no_source &&
                command.value == -2) {
              // A reading that divides carries its gathering with it.
              std::get<group>(states[command.destination]) =
                  std::get<group>(old_states[command.source]);
            } else if constexpr (gathers_a_list) {
              std::get<group>(states[command.destination]) = held_type{};
            } else {
              std::get<group>(states[command.destination]) =
                  gathering_of<type, format, group>::begin(
                      spread.parameters[group].view());
            }
          } else if (tag == closing && registers[command.destination] != position &&
                     command.source != packed_command::no_source &&
                     command.value == -2) {
            // A closing already written, only being carried along, keeps what
            // it holds.
            std::get<group>(states[command.destination]) =
                std::get<group>(old_states[command.source]);
          }
        }(),
         ...);
      },
      commands);
  if constexpr (how::folds && how::the_place) {
    // Everything that happened inside this place on this character, told in
    // order -- and told now, before the copy below, or a fold that ends where
    // its place ends would be copied one closing short.
    fold_the_readings<group, std::remove_cv_t<held_type>, automaton>(
        state, registers, states, symbol, hands_the_character, text);
  }
  if constexpr (!gathers_a_list) {
    // The groups that close on this step. What the field gathered is in the
    // opening it was being added to, whichever register that has become.
    command_index = 0;
    std::apply(
        [&](const auto&... command) {
          ([&] {
            if (command_index++ >= count) return;
            if (automaton.register_tag[command.destination] != closing) return;
            if (registers[command.destination] != position) return;
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
          }(),
           ...);
        },
        commands);
  }
  // A list gathers elements, not characters. Written as an early return this
  // would discard nothing: what follows an `if constexpr` is not the branch it
  // did not take.
  //
  // Handing the character over is skipped where the caller knows the state and
  // does it by the numbers: this walks every reading of the state and keeps a
  // flag per register to do it once, which on a subject read a character at a
  // time is most of what reading it costs.
  if constexpr (!gathers_a_list && hands_the_character) {
    // Once each, however many readings share it: a register is one gathering.
    std::array<bool, register_count> filled{};
    const auto& packed = automaton.states[state];
    for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
      const std::uint32_t open = packed.readings[reading][opening];
      const std::uint32_t close = packed.readings[reading][closing];
      if (filled[open]) continue;
      if (registers[open] < 0 || registers[close] >= registers[open]) continue;
      filled[open] = true;
      gathering_of<type, format, group>::push(std::get<group>(states[open]),
                                              symbol);
    }
  }
}

template <class root, class type, std::size_t offset, class reading_type,
          class states_type, std::size_t register_count>
[[nodiscard]] constexpr std::expected<type, failure_for<root>> finish_value(
    const reading_type& reading, const states_type& states,
    const std::array<std::ptrdiff_t, register_count>& registers,
    const char* text);

// The parts of a product, and the arguments of a call, as named functions
// rather than as lambdas called where they stand. A lambda holding references
// and called inside the argument of something that itself holds references is
// more than the constant evaluator will follow.
template <class root, class type, std::size_t offset, class reading_type,
          class states_type, std::size_t register_count, std::size_t... part>
[[nodiscard]] constexpr std::expected<type, failure_for<root>> finish_parts(
    const reading_type& reading, const states_type& states,
    const std::array<std::ptrdiff_t, register_count>& registers,
    const char* text, std::index_sequence<part...>) {
  auto parts =
      std::tuple{finish_value<root, typename parts_of<type>::template at<part>,
                              offset + groups_before_field<type, part>()>(
          reading, states, registers, text)...};
  if (auto went_wrong = what_went_wrong<failure_for<root>>(parts)) {
    return std::unexpected(std::move(*went_wrong));
  }
  return type{std::move(*std::get<part>(parts))...};
}

template <class root, class type, std::size_t offset, class reading_type,
          class states_type, std::size_t register_count, std::size_t... part>
[[nodiscard]] constexpr std::expected<type, failure_for<root>> finish_by_call(
    const reading_type& reading, const states_type& states,
    const std::array<std::ptrdiff_t, register_count>& registers,
    const char* text, std::index_sequence<part...>) {
  auto parts =
      std::tuple{finish_value<root, typename parts_of<type>::template at<part>,
                              offset + groups_before_field<type, part>()>(
          reading, states, registers, text)...};
  if (auto went_wrong = what_went_wrong<failure_for<root>>(parts)) {
    return std::unexpected(std::move(*went_wrong));
  }
  return scan::scanner<std::remove_cv_t<type>>::parse(
      std::move(*std::get<part>(parts))...);
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
    std::size_t count, const char* text,
    std::optional<failure_for<type>>& failed) {
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
      auto one = finish_value<type, element, group>(packed.readings[reading],
                                                    states, registers, text);
      if (!one) {
        if (!failed) failed = std::move(one).error();
        continue;
      }
      append_to(std::get<list_group>(states[into]), std::move(*one));
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
    std::size_t count, std::index_sequence<group...>, const char* text,
    std::optional<failure_for<type>>& failed) {
  (collect_element<group, type, format, automaton>(state, registers, states,
                                                   commands, count, text,
                                                   failed),
   ...);
}

template <class type, fixed_string format, auto& automaton,
          bool hands_the_character = true, std::size_t register_count,
          class states_type, std::size_t command_count, std::size_t... group>
constexpr void advance_scanners(
    char symbol, std::size_t state, std::ptrdiff_t position,
    const std::array<std::ptrdiff_t, register_count>& registers,
    states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count, std::index_sequence<group...>,
    const char* text = nullptr) {
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
    const auto old_states = keep_gatherings(states, commands, count);
    (advance_scanner<group, type, format, automaton, hands_the_character>(
         symbol, state, position, registers, old_states, states, commands,
         count, text),
     ...);
  } else {
    (advance_scanner<group, type, format, automaton, hands_the_character>(
         symbol, state, position, registers, states, states, commands,
         count, text),
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
[[nodiscard]] constexpr std::expected<type, failure_for<root>> finish_value(
    const reading_type& reading, const states_type& states,
    const std::array<std::ptrdiff_t, register_count>& registers,
    const char* text) {
  using failure_type = failure_for<root>;
  if constexpr (scanned_as_leaf<type> && folds_by_turns<std::remove_cv_t<type>>) {
    // A leaf that was told its groups as the walk passed them. What is left is
    // the end of the input, which is not a character and so was never handed
    // over: a group that opened where nothing followed it, and every group
    // still open when the reading stopped. The same step the walk runs says
    // both, asked once more with nothing to hand over.
    using held = std::remove_cv_t<type>;
    const std::uint32_t open = reading[offset * 2];
    const std::uint32_t close = reading[offset * 2 + 1];
    const bool still_reading = registers[close] < registers[open];
    auto fold = std::get<offset>(states[still_reading ? open : close]);
    fold_one_step<offset, held>(fold, reading, registers, '\0', false);
    if (fold.wanted_a_subject) {
      return std::unexpected(scan::as_a_failure<failure_type>(wrong_subject(
          "a fold that only takes its groups whole needs a subject that can be "
          "pointed at: give it push_group to read a stream")));
    }
    if constexpr (scan::says_what_went_wrong_folding<held>) {
      auto got = scan::scanner<held>::try_finish_groups(std::move(fold.state));
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<failure_type>(std::move(got).error()));
    } else {
      return scan::scanner<held>::finish_groups(std::move(fold.state));
    }
  } else if constexpr (scanned_as_leaf<type> && gathers_by_its_groups<type>) {
    // A leaf built from its own groups once the match is over. They are groups
    // of this match like any others and the positions say where each one
    // stood, so what it is handed are views of the subject: nothing was
    // gathered for it and nothing was copied. That it has a subject to point
    // at is settled where the walk is made.
    using held = std::remove_cv_t<type>;
    constexpr std::size_t inside = groups_a_leaf_opens<held>();
    std::array<std::string_view, inside> theirs{};
    std::array<bool, inside> took{};
    [&]<std::size_t... at>(std::index_sequence<at...>) {
      ((void)[&] {
        constexpr std::size_t which = offset + 1 + at;
        const std::ptrdiff_t began = registers[reading[which * 2]];
        const std::ptrdiff_t ended = registers[reading[which * 2 + 1]];
        if (began < 0 || ended < began) return;
        took[at] = true;
        // The same count-not-index the fold reads its spans by.
        theirs[at] = std::string_view(text + (began > 0 ? began - 1 : 0),
                                      static_cast<std::size_t>(ended - began));
      }(), ...);
    }(std::make_index_sequence<inside>{});
    const auto given = std::span<const std::string_view>(theirs);
    if constexpr (scan::says_what_went_wrong_from_groups<held>) {
      auto got = scan::scanner<held>::try_from_groups(given);
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<failure_type>(std::move(got).error()));
    } else if constexpr (requires {
                           scan::scanner<held>::from_groups(given);
                         }) {
      return scan::scanner<held>::from_groups(given);
    } else {
      auto state = scan::scanner<held>::begin_groups();
      [&]<std::size_t... at>(std::index_sequence<at...>) {
        ((void)[&] {
          if (!took[at]) return;
          open_one_group<held, at>(state);
          close_one_group<held, at>(state, theirs[at]);
        }(), ...);
      }(std::make_index_sequence<inside>{});
      if constexpr (scan::says_what_went_wrong_folding<held>) {
        auto got = scan::scanner<held>::try_finish_groups(std::move(state));
        if (got) return std::move(*got);
        return std::unexpected(
            scan::as_a_failure<failure_type>(std::move(got).error()));
      } else {
        return scan::scanner<held>::finish_groups(std::move(state));
      }
    }
  } else if constexpr (scanned_as_leaf<type>) {
    // A field still being read when the input ended is where it was being
    // gathered; one that ended earlier is the copy taken when it closed, which
    // the readings that went on adding to the opening cannot have changed.
    // The same question the gathering asks of every character: has this group
    // closed since it opened.
    const std::uint32_t open = reading[offset * 2];
    const std::uint32_t close = reading[offset * 2 + 1];
    const bool still_reading = registers[close] < registers[open];
    auto& gathered = std::get<offset>(states[still_reading ? open : close]);
    if constexpr (scan::says_what_went_wrong_finishing<type>) {
      auto got = scan::scanner<std::remove_cv_t<type>>::try_finish(gathered);
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<failure_type>(std::move(got).error()));
    } else {
      return scanner_finish<type>(gathered);
    }
  } else if constexpr (scanned_as_range<type>) {
    // What has been put in as each element ended, and then the one that was
    // still being read when the whole thing ended.
    using element = std::remove_cvref_t<std::ranges::range_value_t<type>>;
    type made = std::get<offset>(states[reading[offset * 2]]);
    // The turn that was still going when the whole thing ended. Where the list
    // is written to be allowed none at all, there may not have been one.
    if (registers[reading[(offset + 1) * 2]] >= 0) {
      auto last = finish_value<root, element, offset + 1>(reading, states,
                                                          registers, text);
      if (!last) return std::unexpected(std::move(last).error());
      append_to(made, std::move(*last));
    }
    return made;
  } else if constexpr (scanned_as_variant<type>) {
    // Exactly one branch ran, and its mark says so: the mark of the branch
    // that took part was opened, and the others never were. The same question
    // the subject that can be pointed at answers by whether the group points
    // anywhere.
    return [&]<std::size_t... branch>(std::index_sequence<branch...>)
               -> std::expected<type, failure_type> {
      std::optional<std::expected<type, failure_type>> made;
      const auto take = [&]<std::size_t which>() {
        constexpr std::size_t mark =
            offset + groups_before_branch<type, which>();
        if (made || registers[reading[mark * 2]] < 0) return;
        using alternative = branch_at<type, which>;
        auto part = finish_value<root, alternative, mark + 1>(reading, states,
                                                              registers, text);
        if (!part) {
          made = std::unexpected(std::move(part).error());
          return;
        }
        made = scan::branches<std::remove_cv_t<type>>::template make<which>(
            std::move(*part));
      };
      (take.template operator()<branch>(), ...);
      if (!made) {
        return std::unexpected(scan::as_a_failure<failure_type>(
            no_match("no branch of the format took the input")));
      }
      return std::move(*made);
    }(std::make_index_sequence<branch_count<type>()>{});
  } else if constexpr (scanned_from_values<type>) {
    return finish_by_call<root, type, offset>(
        reading, states, registers, text,
        std::make_index_sequence<parts_of<type>::count>{});
  } else {
    return finish_parts<root, type, offset>(
        reading, states, registers, text,
        std::make_index_sequence<parts_of<type>::count>{});
  }
}

// Fed a character at a time. Whoever holds it says where the input ends, so
// the reading is anchored by default -- the walks below a match are kept, and
// the answer is the first still accepting when the feeding stops. A prefix
// read off a stream asks for the other policy, and stops where the match ends.
template <class type, fixed_string format, bool cut = true>
class stream_state {
 private:
  static_assert(
      !holds_a_flat_reader<type>(),
      "a type built from its groups after the match cannot read a stream: "
      "there is nothing left to point at by the time it would be handed them "
      "-- give it begin_groups and push_group to be told its groups as they "
      "arrive");
  inline static constexpr const auto& automaton =
      streaming_automaton<type, format, cut>;
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
    const std::size_t run =
        run_taken<automaton>(state_, static_cast<unsigned char>(symbol));
    if (run == no_run) return false;
    const auto* transition = &automaton.states[state_].ranges[run];
    collect_elements<type, format, automaton>(
        state_, registers_, scanner_states_, transition->commands,
        transition->command_count, std::make_index_sequence<field_count>{},
        nullptr, failed_);
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
    // The same question the reading asks: a field still being typed is where
    // it is being gathered, and one that has closed is the copy taken then.
    const auto& reading = automaton.states[here].readings[0];
    const std::uint32_t open = reading[field * 2];
    const std::uint32_t close = reading[field * 2 + 1];
    const bool still_reading = registers_[close] < registers_[open];
    return std::get<field>(scanner_states_[still_reading ? open : close]);
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

  [[nodiscard]] constexpr std::expected<type, failure_for<type>> finish()
      const& {
    stream_state copy = *this;
    return std::move(copy).finish();
  }

  [[nodiscard]] constexpr std::expected<type, failure_for<type>> finish() && {
    if (failed_) return std::unexpected(std::move(*failed_));
    if (state_ == packed_range<0>::reject) {
      return std::unexpected(scan::as_a_failure<failure_for<type>>(
          no_match("input does not match scan expression")));
    }
    const auto slot = automaton.states[state_].accepting_slot;
    if (slot == packed_state<0, 0, 0>::not_accepting) {
      return std::unexpected(scan::as_a_failure<failure_for<type>>(
          no_match("input does not match scan expression")));
    }
    // The reading that accepted says which register holds each value. Nothing
    // is written here: the commands that end a match are not run by this
    // machine, so a group that never closed is read from where it was being
    // gathered, which is what the registers say.
    const auto& reached = automaton.states[state_];
    // Nothing to point at: this machine is fed and never holds the subject.
    return finish_value<type, type, 0>(reached.readings[slot], scanner_states_,
                                       registers_, nullptr);
  }

 private:
  std::array<field_states, slot_count> scanner_states_{};
  std::array<std::ptrdiff_t, automaton.register_count> registers_{};
  std::size_t state_ = automaton.initial;
  std::ptrdiff_t position_ = 0;
  // An element of a list that did not read: met in the middle of the walk,
  // where there is nothing to hand it back to yet, so it waits here.
  std::optional<failure_for<type>> failed_;
};

// Which gatherings a group is added to where the machine stands.
//
// A state stands in several readings at once and they can share a register, so
// what a character is added to is the set of the registers holding the group's
// opening across those readings, each of them once. Walking the readings to
// find that out on every character is what a machine that does not know where
// it stands has to do; where the state is known, the set is known, and this is
// it.
template <auto& automaton, std::size_t state, std::size_t group>
inline constexpr auto gathered_at = [] consteval {
  constexpr const auto& packed = automaton.states[state];
  struct answer {
    std::array<std::uint32_t, automaton.register_count> at{};
    std::size_t count = 0;
  };
  answer said;
  for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
    const std::uint32_t opening = packed.readings[reading][group * 2];
    bool already = false;
    for (std::size_t at = 0; at < said.count; ++at) {
      if (said.at[at] == opening) already = true;
    }
    if (!already) said.at[said.count++] = opening;
  }
  return said;
}();

// The gatherer the format reader hands to the walk: the fields' own scanners,
// following the moves.
//
// What the machine that can be stopped and started looks up on every character
// -- the state, its runs, the commands of the run it took, the readings that
// say which register holds which group -- is a constant here, because the walk
// knows where it stands. The work each character does is what it was: apply
// what the move writes to the gatherings, and give the character to the fields
// that are open.
template <class type, fixed_string format, auto& automaton,
          bool pointable = false>
class field_gatherer {
 public:
  // A type built from its groups once the match is over is handed views of the
  // subject. Where the subject is gone as it is read there is nothing to give
  // it, and holding the characters until the end to give it something would be
  // a hold with no bound.
  static_assert(
      pointable || !holds_a_flat_reader<type>(),
      "a type built from its groups after the match needs a subject that can "
      "be pointed at: give it begin_groups and push_group to be told its "
      "groups as they are read, or scan it from something contiguous");
  static constexpr std::size_t field_count = groups_of<type>();
  using states_type =
      std::array<decltype(make_scanner_state<type, format>()),
                 automaton.register_count>;

  constexpr field_gatherer() {
    std::ranges::fill(states_, make_scanner_state<type, format>());
  }

  // A list takes in the turn that has just ended, and what says it ended is
  // the registers as they stood before this move wrote anything.
  template <std::size_t state, std::size_t move, class registers_type>
  constexpr void moving(const registers_type& registers, std::ptrdiff_t) {
    constexpr const auto& taken = automaton.states[state].ranges[move];
    collect_elements<type, format, automaton>(
        state, registers, states_, taken.commands, taken.command_count,
        std::make_index_sequence<field_count>{}, text_, failed_);
  }

  // A run the walk stepped over in vectors: the fields that are open take all
  // of it, which is one pass over the piece rather than one call a character.
  template <std::size_t state, class registers_type>
  constexpr void took_run(const char* from, const char* to,
                          const registers_type& registers, std::ptrdiff_t) {
    hand_run<state>(from, to, registers,
                    std::make_index_sequence<field_count>{});
  }

  template <std::size_t state, std::size_t landed, class registers_type>
  constexpr void moved(std::size_t move, char letter,
                       const registers_type& registers,
                       std::ptrdiff_t position) {
    // A move that writes nothing leaves the gatherings where they are, and
    // most of the characters of a subject are read by one: inside a field
    // nothing is written, which is what holding the tags back bought. So the
    // whole of what a move does to the gatherings is skipped for it, and what
    // is left is handing the character to the fields that are open.
    if constexpr (state != landed || staying_writes<automaton, state>()) {
      const auto& taken = automaton.states[state].ranges[move];
      advance_scanners<type, format, automaton, false>(
          letter, landed, position, registers, states_, taken.commands,
          taken.command_count, std::make_index_sequence<field_count>{}, text_);
    }
    hand_over<landed>(letter, registers,
                      std::make_index_sequence<field_count>{});
  }

  // Where the walk ended is a constant, so the reading that accepted is one
  // too, and the value is put together here rather than looked for afterwards.
  // A walk keeps every place it passes, and the last one it keeps is the
  // answer -- so this runs more than once and the last of them wins, which is
  // what it has always done with the value. It has to do the same with a
  // failure: a place passed early where a field was not read yet is not this
  // reading's answer, and holding on to that would lose every match after it.
  template <std::size_t state, class registers_type>
  constexpr void ended(const registers_type& registers) {
    constexpr const auto& packed = automaton.states[state];
    auto got = finish_value<type, type, 0>(
        packed.readings[packed.accepting_slot], states_, registers, text_);
    if (!got) {
      failed_ = std::move(got).error();
      made_.reset();
      return;
    }
    failed_.reset();
    made_.emplace(std::move(*got));
  }

  // What was read, or what went wrong instead. An element of a list that did
  // not read is kept here too: the walk that met it is not over, and there is
  // nowhere to say so until it is.
  [[nodiscard]] constexpr std::expected<type, failure_for<type>> taken() {
    if (failed_) return std::unexpected(std::move(*failed_));
    if (!made_) {
      return std::unexpected(scan::as_a_failure<failure_for<type>>(
          no_match("input does not match scan expression")));
    }
    return std::move(*made_);
  }

  [[nodiscard]] constexpr std::optional<failure_for<type>>& went_wrong() {
    return failed_;
  }

  [[nodiscard]] constexpr std::optional<type>& made() { return made_; }

  // Where the subject begins, for a walk that has all of it in front of it. A
  // fold reading such a subject is handed each of its groups whole.
  constexpr void points_at(const char* text) { text_ = text; }

 private:
  template <std::size_t state, class registers_type, std::size_t... group>
  constexpr void hand_run(const char* from, const char* to,
                          const registers_type& registers,
                          std::index_sequence<group...>) {
    (hand_run_group<state, group>(from, to, registers), ...);
  }

  template <std::size_t state, std::size_t group, class registers_type>
  constexpr void hand_run_group(const char* from, const char* to,
                                const registers_type& registers) {
    using held_type = leaf_kind<type, group>;
    using how = gathering_of<type, format, group>;
    if constexpr (scanned_as_range<held_type>) {
      return;
    } else if constexpr (how::folds && how::inside) {
      return;
    } else if constexpr (how::folds && how::the_place) {
      // The characters of a run, each to the group it fell in. The positions
      // stand still across a run, so what opened and what closed is said once,
      // and where the subject can be pointed at nothing is handed over at all.
      using folded = fold_of<std::remove_cv_t<held_type>>;
      if constexpr (every_group_whole<held_type, typename folded::state_type>()) {
        if (from != to) {
          fold_the_readings<group, std::remove_cv_t<held_type>, automaton>(
              state, registers, states_, *from, true, text_);
        }
      } else {
        for (const char* letter = from; letter != to; ++letter) {
          fold_the_readings<group, std::remove_cv_t<held_type>, automaton>(
              state, registers, states_, *letter, true, text_);
        }
      }
    } else {
      constexpr auto at = gathered_at<automaton, state, group>;
      constexpr std::uint32_t closing =
          automaton.states[state].reading_count == 0
              ? 0
              : automaton.states[state].readings[0][group * 2 + 1];
      for (std::size_t which = 0; which < at.count; ++which) {
        const std::uint32_t opening = at.at[which];
        if (registers[opening] < 0) continue;
        if (registers[closing] >= registers[opening]) continue;
        auto& gathering = std::get<group>(states_[opening]);
        for (const char* letter = from; letter != to; ++letter) {
          gathering_of<type, format, group>::push(gathering, *letter);
        }
      }
    }
  }

  template <std::size_t landed, class registers_type, std::size_t... group>
  constexpr void hand_over(char letter, const registers_type& registers,
                           std::index_sequence<group...>) {
    (hand_group<landed, group>(letter, registers), ...);
  }

  template <std::size_t landed, std::size_t group, class registers_type>
  constexpr void hand_group(char letter, const registers_type& registers) {
    using held_type = leaf_kind<type, group>;
    using how = gathering_of<type, format, group>;
    if constexpr (scanned_as_range<held_type>) {
      return;
    } else if constexpr (how::folds && how::inside) {
      return;
    } else if constexpr (how::folds && how::the_place) {
      fold_the_readings<group, std::remove_cv_t<held_type>, automaton>(
          landed, registers, states_, letter, true, text_);
    } else {
      static constexpr auto spread = spread_of<type, format>();
      constexpr auto at = gathered_at<automaton, landed, group>;
      constexpr std::uint32_t closing =
          automaton.states[landed].reading_count == 0
              ? 0
              : automaton.states[landed].readings[0][group * 2 + 1];
      for (std::size_t which = 0; which < at.count; ++which) {
        const std::uint32_t opening = at.at[which];
        if (registers[opening] < 0) continue;
        if (registers[closing] >= registers[opening]) continue;
        gathering_of<type, format, group>::push(
            std::get<group>(states_[opening]), letter);
      }
    }
  }


  states_type states_;
  std::optional<type> made_;
  std::optional<failure_for<type>> failed_;
  const char* text_ = nullptr;
};

// One match off input that arrives in pieces, and where the next one starts.
//
// The head of the reading, in the sense the format reader means: the machine
// takes what it takes and stops, and where it stopped is where the reading
// goes on from -- which for pieces is a place in the piece it is holding, so
// nothing has to be put back.
template <class type, fixed_string format, class source_type>
struct taken_from_pieces {
  std::optional<type> value;
  bool matched = false;
};

// How much a reading of this format has to carry between one match and the
// next, where the subject arrives in pieces. The same number the stream
// reading asks for: the longest walk out of a match that finds no other one.
template <class type, fixed_string format>
inline constexpr std::size_t pieces_hold = [] consteval {
  constexpr std::size_t window =
      walk_past_a_match<streaming_automaton<type, format>>();
  if constexpr (window == std::numeric_limits<std::size_t>::max()) {
    return std::size_t{0};
  } else {
    return window * 2 + 1;
  }
}();

template <class type, fixed_string format, class source_type>
[[nodiscard]] constexpr auto take_from_pieces(source_type& into,
                                              const char*& cursor,
                                              const char*& last,
                                              std::ptrdiff_t& place) {
  constexpr const auto& automaton = streaming_automaton<type, format>;
  taken_from_pieces<type, format, source_type> said;
  std::array<std::ptrdiff_t, automaton.register_count> registers{};
  std::ranges::fill(registers, scan::tre::negative_tag);
  execute_commands(automaton.initialize, automaton.initialize.size(), registers,
                   place);
  // The same note as everywhere else, and here it costs nothing to go back to:
  // the place is an address inside a piece the reading is still holding.
  constexpr bool walks_past = walk_past_a_match<automaton>() != 0;
  using kept_type =
      std::conditional_t<walks_past,
                         std::array<std::ptrdiff_t, automaton.register_count>,
                         nothing_kept>;
  walk_answer<const char*, kept_type> best;
  constexpr walk_shape shape{.in_words = true, .longest = true};
  into.watch(best.at, best.upto);
  if (!run_continuation<automaton, shape, automaton.initial, shape.budget, 0,
                        std::ptrdiff_t>(cursor, last, place, registers, into,
                                        best)) {
    return said;
  }
  // Where the match ended is where the next reading begins. Which characters
  // those are is the source's business: the piece they were in may have been
  // given up while the walk read past them, and then they were carried.
  into.go_back_to(cursor, last);
  auto got = into.taken();
  if (!got) return said;
  said.value = std::move(*got);
  said.matched = true;
  return said;
}

// A scan of input that arrives in pieces.
//
// The walk reads a piece the way it reads a string -- in words and vectors --
// and asks for the next one where it runs out. Nothing is buffered: what a
// field gathers, it gathers as the characters go by, and a piece is not looked
// at again once the walk has left it.
template <class type, fixed_string format, piecewise_char_range pieces_type>
[[nodiscard]] constexpr std::expected<type, failure_for<type>> scan_pieces(
    pieces_type&& pieces) {
  constexpr const auto& automaton = streaming_automaton<type, format>;
  std::array<std::ptrdiff_t, automaton.register_count> registers{};
  std::ranges::fill(registers, scan::tre::negative_tag);
  execute_commands(automaton.initialize, automaton.initialize.size(), registers,
                   std::ptrdiff_t{0});
  auto view = std::views::all(std::forward<pieces_type>(pieces));
  gathers_from_pieces<field_gatherer<type, format, automaton>, decltype(view),
                      pieces_hold<type, format>>
      into(field_gatherer<type, format, automaton>{}, std::move(view));
  // Nothing in hand to begin with, so the first thing the walk does is ask.
  const char* cursor = nullptr;
  const char* last = nullptr;
  std::ptrdiff_t place = 0;
  walk_answer<const char*> best;
  constexpr walk_shape shape{.in_words = true};
  if (!run_continuation<automaton, shape, automaton.initial, shape.budget, 0,
                        std::ptrdiff_t>(cursor, last, place, registers, into,
                                        best)) {
    return std::unexpected(scan::as_a_failure<failure_for<type>>(
        no_match("input does not match scan expression")));
  }
  return into.taken();
}

template <class type, fixed_string format, std::ranges::input_range range_type>
[[nodiscard]] constexpr std::expected<type, failure_for<type>> scan_stream(
    range_type&& input) {
  // The whole of the reading, so the walks below a match are kept.
  constexpr const auto& automaton = streaming_automaton<type, format, false>;
  std::array<std::ptrdiff_t, automaton.register_count> registers{};
  std::ranges::fill(registers, scan::tre::negative_tag);
  execute_commands(automaton.initialize, automaton.initialize.size(), registers,
                   std::ptrdiff_t{0});
  field_gatherer<type, format, automaton,
                 std::ranges::contiguous_range<range_type>>
      into;
  if constexpr (std::ranges::contiguous_range<range_type>) {
    into.points_at(std::ranges::data(input));
  }
  auto cursor = std::ranges::begin(input);
  std::ptrdiff_t position = 0;
  constexpr walk_shape shape{};
  walk_answer<decltype(cursor)> best;
  if (!run_continuation<automaton, shape, automaton.initial, 0, 0,
                        std::ptrdiff_t>(cursor, std::ranges::end(input),
                                        position, registers, into, best)) {
    return std::unexpected(scan::as_a_failure<failure_for<type>>(
        no_match("input does not match scan expression")));
  }
  return into.taken();
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

// What a reading holds between one match and the next.
//
// A walk can go past a match and die away from one, and then the answer is the
// place it passed -- which means the characters it read after that place have
// to be read again by whoever reads next. There is nowhere to put them back
// on a subject that is gone once it is read, so they are kept here, in front
// of the reading, and taken before anything else.
//
// How many there can be is what the automaton says: the longest walk out of a
// match that finds no other match. Where that is nothing, this is nothing.
template <std::size_t hold>
struct stream_carry {
  std::array<char, hold == 0 ? 1 : hold> held{};
  std::size_t count = 0;
  std::size_t at = 0;

  [[nodiscard]] constexpr bool empty() const { return at == count; }
  [[nodiscard]] constexpr char front() const { return held[at]; }
  constexpr void pop() { ++at; }

  // In front of whatever is still unread here, because they were read first.
  constexpr void put_in_front(const char* from, std::size_t many) {
    if (many == 0) return;
    const std::size_t left = count - at;
    std::array<char, hold == 0 ? 1 : hold> made{};
    for (std::size_t index = 0; index < many; ++index) made[index] = from[index];
    for (std::size_t index = 0; index < left; ++index) {
      made[many + index] = held[at + index];
    }
    held = made;
    count = many + left;
    at = 0;
  }
};

// The head of a subject read as it arrives, and where it ended.
//
// The machine is offered the character before it is taken out of the reading,
// so the one that ends a match is never lost -- it is looked at, reported, and
// left where it is. What has to be given back is the other thing: the
// characters read after the last match on the chance of a longer one, where
// the walk went past a match and then died. Those were taken, and they go into
// the carry for the next reading.
//
// A reading that can be gone back over needs neither: the place is an iterator
// and going back is assigning it. That is why a forward range holds nothing at
// all here, and why the only reading that has to name a number is the one that
// cannot be gone back over.
template <class type, fixed_string format, class iterator_type,
          class sentinel_type, std::size_t hold>
[[nodiscard]] constexpr std::expected<taken_ahead<type>, failure_for<type>>
scan_stream_prefix(iterator_type& first, sentinel_type last,
                   stream_carry<hold>& carry) {
  constexpr const auto& automaton = streaming_automaton<type, format>;
  constexpr std::size_t window = walk_past_a_match<automaton>();
  constexpr bool can_go_back = std::forward_iterator<iterator_type>;
  static_assert(
      can_go_back || window != std::numeric_limits<std::size_t>::max(),
      "this pattern can read any number of characters past a match without "
      "finding another one, so the reading that has to give them back would "
      "have to hold any number of them: read it from something that can be "
      "gone back over -- a forward range, characters in a row, or input in "
      "pieces");
  stream_state<type, format> state;
  std::optional<char> stopped;
  std::optional<stream_state<type, format>> note;
  // The place the note was taken at, kept the way this reading can keep it: an
  // iterator where the reading can be gone back over, and nothing at all where
  // it cannot -- an iterator of such a range cannot even be copied.
  using place_type =
      std::conditional_t<can_go_back, iterator_type, nothing_kept>;
  place_type note_at{};
  constexpr std::size_t held_here =
      can_go_back || window == 0 ||
              window == std::numeric_limits<std::size_t>::max()
          ? 1
          : window;
  std::array<char, held_here> since{};
  std::size_t since_count = 0;
  if (state.accepting()) note = state;
  while (true) {
    char symbol = 0;
    if (!carry.empty()) {
      symbol = carry.front();
    } else if (first != last) {
      symbol = static_cast<char>(*first);
    } else {
      break;
    }
    if (!state.offer(symbol)) {
      // Looked at and not taken: it stays where it is, and is said here so
      // that whoever asked knows what ended the match.
      stopped = symbol;
      break;
    }
    if (!carry.empty()) {
      carry.pop();
    } else {
      ++first;
    }
    if constexpr (!can_go_back && window != 0) since[since_count++] = symbol;
    if (state.accepting()) {
      note = state;
      since_count = 0;
      if constexpr (can_go_back) note_at = first;
      // Where the machine can go nowhere from where it stands, it is over, and
      // nothing needs to be read to find that out.
      if (state.settled()) break;
    }
  }
  const auto handed_back =
      [](std::expected<type, failure_for<type>> got,
         std::optional<char> ended_it)
      -> std::expected<taken_ahead<type>, failure_for<type>> {
    if (!got) return std::unexpected(std::move(got).error());
    return taken_ahead<type>{std::move(*got), ended_it};
  };
  if (state.accepting()) return handed_back(std::move(state).finish(), stopped);
  if (note) {
    // Past the match and dead. The answer is the place that was kept, and what
    // was read after it goes back in front of the reading.
    if constexpr (can_go_back) {
      first = note_at;
    } else {
      carry.put_in_front(since.data(), since_count);
    }
    return handed_back(std::move(*note).finish(), std::optional<char>{});
  }
  // Nothing matched; `finish` says so in the way the caller expects.
  return handed_back(std::move(state).finish(), stopped);
}

// How much a reading of this format has to be able to hold: nothing where it
// can be gone back over, and nothing where no walk out of a match ever fails
// to find another. Otherwise the characters read past a match, and the ones
// already held when that happened.
template <class type, fixed_string format, class iterator_type>
inline constexpr std::size_t stream_hold = [] consteval {
  if constexpr (std::forward_iterator<iterator_type>) {
    return std::size_t{0};
  } else {
    constexpr std::size_t window =
        walk_past_a_match<streaming_automaton<type, format>>();
    if constexpr (window == std::numeric_limits<std::size_t>::max()) {
      // Refused where it is used; sized so that saying so is what the caller
      // sees, rather than an array of every address there is.
      return std::size_t{0};
    } else {
      return window * 2;
    }
  }
}();

template <class type, fixed_string format, class iterator_type>
using stream_carry_for = stream_carry<stream_hold<type, format, iterator_type>>;

template <class type, fixed_string format, std::ranges::input_range range_type>
[[nodiscard]] constexpr std::expected<taken_ahead<type>, failure_for<type>>
scan_stream_prefix(range_type&& input) {
  auto first = std::ranges::begin(input);
  stream_carry<stream_hold<type, format, decltype(first)>> carry;
  return scan_stream_prefix<type, format>(first, std::ranges::end(input),
                                          carry);
}

#undef SCAN_FORCE_INLINE


}  // namespace scan::detail
