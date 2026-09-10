export module scan.runtime;

import std;
import scan.tre;
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


// Which registers anything will ever read.
//
// A register is read where it holds a tag of a group somebody asks about, and
// where a command copies out of it into a register that is read. Everything
// else a walk writes is written and thrown away: the tags of a group whose
// type only takes the characters are the marks of a place nobody will ever
// ask about.
//
// That matters because writing them is not free. A move that begins a turn
// writes half a dozen positions into the register file, and a subject made of
// short turns is a subject made of those stores -- which is the whole of the
// difference between this walk and the loop somebody would have written by
// hand for the same expression, where a position nobody reads is a position
// nobody writes either.
//
// The reading is closed under copying: a register that feeds a live one is
// live too, however many moves the copy takes to arrive.
template <auto& automaton, std::uint64_t tags_read>
inline constexpr auto registers_worth_writing = [] consteval {
  constexpr std::size_t count = automaton.register_count;
  std::array<bool, count> live{};
  for (std::size_t at = 0; at < count; ++at) {
    const std::size_t group = automaton.register_tag[at] / 2;
    live[at] = group >= 64 || (tags_read & (std::uint64_t{1} << group)) != 0;
  }
  bool again = true;
  while (again) {
    again = false;
    for (std::size_t state = 0; state < automaton.states.size(); ++state) {
      const auto& packed = automaton.states[state];
      const auto feeds = [&](const auto& one) {
        if (one.destination >= count || !live[one.destination]) return;
        if (one.source == packed_command::no_source) return;
        if (one.source >= count || live[one.source]) return;
        live[one.source] = true;
        again = true;
      };
      for (std::size_t at = 0; at < packed.range_count; ++at) {
        const auto& range = packed.ranges[at];
        for (std::size_t index = 0; index < range.command_count; ++index) {
          feeds(range.commands[index]);
        }
      }
      for (std::size_t index = 0; index < packed.final_command_count; ++index) {
        feeds(packed.final_commands[index]);
      }
    }
  }
  return live;
}();

template <auto& automaton, std::size_t state, std::size_t range,
          std::uint64_t tags_read, class mark, std::size_t register_count>
SCAN_FORCE_INLINE constexpr void execute_static_transition_commands(
    std::array<mark, register_count>& registers, mark here) {
  constexpr const auto& transition =
      automaton.states[state].ranges[range];
  static constexpr auto live = registers_worth_writing<automaton, tags_read>;
  [&]<std::size_t... index> SCAN_FORCE_INLINE_LAMBDA(
      std::index_sequence<index...>) {
        const std::array<mark, sizeof...(index)> source_values{
            (!live[transition.commands[index].destination] ||
                     transition.commands[index].source ==
                         packed_command::no_source
                 ? absent_mark<mark>
                 : registers[transition.commands[index].source])...};
        ([&] SCAN_FORCE_INLINE_LAMBDA {
          if constexpr (live[transition.commands[index].destination]) {
            execute_command(transition.commands[index], source_values[index],
                            registers, here);
          }
        }(),
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

// Whether one character belongs to the run a state keeps itself by.
//
// The same question the vectors ask of sixty-four at once, asked of one -- for
// the head of a run, where there are not sixty-four to ask about yet.
template <staying_class klass>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool inside_of(unsigned char letter) {
  bool belongs = false;
  for (std::size_t at = 0; at < klass.count; ++at) {
    belongs = belongs || (letter >= klass.first[at] && letter <= klass.last[at]);
  }
  return belongs;
}

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
    // A few characters first, one at a time.
    //
    // Most runs are short: a field of three letters, a heap of two
    // underscores. Everything below is written to swallow sixty-four
    // characters at a time and pays for it before it reads any -- a constant
    // loaded, a length measured, a question asked about whether that many are
    // even there -- and for a run of three the answer to that question is no,
    // every time, after all of the paying. So the run is walked as a hand
    // would walk it until there is enough of it left to be worth the vectors.
    for (int step = 0; step < 8 && cursor != limit; ++step) {
      if (!inside_of<klass>(static_cast<unsigned char>(*cursor))) return cursor;
      ++cursor;
    }
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
// Whether a move writes a tag somebody will read.
//
// A move that writes only tags nobody reads writes nothing that matters, and a
// run of such moves is a run the vectors can step over: what is stepped over
// was going to be thrown away.
template <auto& automaton, class range_type>
[[nodiscard]] consteval bool writes_for_a_reader(const range_type& range,
                                                 std::uint64_t tags_read) {
  for (std::size_t at = 0; at < range.command_count; ++at) {
    const std::size_t group =
        automaton.register_tag[range.commands[at].destination] / 2;
    if (group >= 64) return true;
    if ((tags_read & (std::uint64_t{1} << group)) != 0) return true;
  }
  return false;
}

template <auto& automaton, std::size_t state,
          std::uint64_t tags_read = ~std::uint64_t{0}>
[[nodiscard]] consteval staying_class staying_of() {
  constexpr const auto& packed = automaton.states[state];
  staying_class answer{};
  answer.below_the_high_bit = true;
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    const auto& range = packed.ranges[index];
    if (range.target != state) continue;
    if (writes_for_a_reader<automaton>(range, tags_read)) return staying_class{};
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

template <auto& automaton, std::size_t state,
          std::uint64_t tags_read = ~std::uint64_t{0}>
[[nodiscard]] consteval bool runs_in_place() {
  return staying_of<automaton, state, tags_read>().count != 0;
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

template <auto& automaton, std::size_t state, std::uint64_t tags_read,
          class mark, std::size_t register_count, std::size_t which = 0>
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
    return execute_tagged_self_transition<automaton, state, tags_read, mark,
                                          register_count, which + 1>(
        symbol, registers, here);
  } else {
    constexpr std::size_t move = moves.at[which];
    if (makes_move<automaton, state, move>(symbol)) {
      execute_static_transition_commands<automaton, state, move, tags_read>(registers,
                                                                 here);
      return true;
    }
    return execute_tagged_self_transition<automaton, state, tags_read, mark,
                                          register_count, which + 1>(
        symbol, registers, here);
  }
}




// Which move keeps the machine here, or none. The move is what the gatherer
// needs: its commands are what a field's gathering follows.
template <auto& automaton, std::size_t state, std::uint64_t tags_read,
          class gatherer, class mark, std::size_t register_count,
          std::size_t which = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::size_t taken_self_move(
    unsigned char symbol, std::array<mark, register_count>& registers,
    mark here, gatherer& into) {
  constexpr auto moves = distinct_moves<automaton, state>();
  if constexpr (which == moves.count) {
    return no_run;
  } else if constexpr (automaton.states[state].ranges[moves.at[which]].target !=
                       state) {
    return taken_self_move<automaton, state, tags_read, gatherer, mark,
                           register_count, which + 1>(symbol, registers, here,
                                                      into);
  } else {
    constexpr std::size_t move = moves.at[which];
    if (makes_move<automaton, state, move>(symbol)) {
      // What a move is about to write is asked before it writes it: a list
      // takes in the turn that is ending, and what says the turn ended is the
      // registers as they stand now.
      into.template moving<state, move>(registers, here);
      execute_static_transition_commands<automaton, state, move, tags_read>(registers,
                                                                 here);
      // Handed over here rather than by whoever called: which move this is, is
      // a constant only while this frame is written out, and what a character
      // belongs to is a fact about the move.
      if constexpr (requires {
                      into.template moved<state, state, move>(
                          static_cast<char>(symbol), registers, here);
                    }) {
        into.template moved<state, state, move>(static_cast<char>(symbol),
                                                registers, here);
      }
      return move;
    }
    return taken_self_move<automaton, state, tags_read, gatherer, mark,
                           register_count, which + 1>(symbol, registers, here,
                                                      into);
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
// How many places in the pattern a walk can step over several characters at
// once: the states a run of characters keeps the machine in. Everything the
// vectors buy is bought there, and nowhere else.
template <auto& automaton>
[[nodiscard]] consteval std::size_t runs_stepped_over() {
  constexpr std::size_t state_count =
      std::tuple_size_v<std::remove_cvref_t<decltype(automaton.states)>>;
  std::size_t count = 0;
  [&]<std::size_t... state>(std::index_sequence<state...>) {
    ((count += staying_of<automaton, state>().count != 0 ? 1 : 0), ...);
  }(std::make_index_sequence<state_count>{});
  return count;
}

// The length of subject at which reading in words is worth asking for.
//
// A step in vectors reads sixteen characters and asks one question of them, so
// it is ahead of reading one at a time only where a run really is that long.
// A subject is shared out between the runs the pattern has -- five fields of a
// row take a fifth of it each -- so the length that pays is sixteen characters
// for every run, and not a fixed number of them: one field of sixteen letters
// is read in words, five fields of sixteen letters need eighty.
//
// It used to be eight, which is a run of one and a half characters a field: a
// row of thirty characters took the walk that reads sixty-four at a time,
// asked three questions to find that it could not, and read them one at a
// time in the end. Twice the time of the walk it should have taken.
template <auto& automaton>
[[nodiscard]] consteval std::size_t worth_reading_in_words() {
  constexpr std::size_t runs = runs_stepped_over<automaton>();
  return 16 * (runs != 0 ? runs : 1);
}

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
  template <std::size_t state, std::size_t landed, std::size_t move,
            class registers_type, class mark>
  constexpr void moved(char, const registers_type&, mark) const {}
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
  template <std::size_t state, std::size_t landed, std::size_t move,
            class registers_type, class mark>
  constexpr void moved(char letter, const registers_type&, mark) const {
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
  // Which groups' positions anybody will read, a bit each.
  //
  // A run of characters is one the walk can step over in vectors only where
  // nothing is written across it -- and a tag written on every character of a
  // run is written for a reader that, more often than not, is not there: a
  // fold hears what opened and what closed from the moves themselves. Told
  // which tags are read, the walk can see such a run for what it is.
  std::uint64_t tags_read = ~std::uint64_t{0};
  // Which marks are worth writing at all, a bit each.
  //
  // Two questions, not one. What is read decides whether a run can be stepped
  // over: a run is one only where nothing is written across it. What is
  // written decides what a single step costs, and there a group that hears
  // every edge from the moves needs no mark at all -- so the walk stops
  // storing one, which on a subject of short turns is most of what it was
  // doing besides reading.
  std::uint64_t tags_written = ~std::uint64_t{0};
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
[[nodiscard]] constexpr bool run_body(
    cursor_type& __restrict cursor, sentinel_type last, mark& __restrict place,
    std::array<mark, register_count>& __restrict registers,
    gatherer& __restrict into, answer_type& __restrict best);

// Reached by a call, with the chain ahead of it written out again from there.
template <auto& automaton, walk_shape shape, std::size_t state, class mark,
          class cursor_type, class sentinel_type, std::size_t register_count,
          class gatherer, class answer_type>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool run_from_state(
    cursor_type& __restrict cursor, sentinel_type last, mark& __restrict place,
    std::array<mark, register_count>& __restrict registers,
    gatherer& __restrict into, answer_type& __restrict best) {
  return run_body<automaton, shape, state, shape.budget, 0, mark, cursor_type,
                  sentinel_type, register_count, gatherer, answer_type>(
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
      // Told which way to guess.
      //
      // A machine written out as code is a chain of these, and the processor
      // guesses at every one of them. What it should guess is known here and
      // not there: the moves of a state are tried in the order they are
      // written, and the one written first is the one this state is most
      // likely to take -- a state that reads a field takes the move that keeps
      // it there for every character but the last.
      if (makes_move<automaton, state, move>(symbol)) [[likely]] {
        into.template moving<state, move>(registers, place);
        execute_static_transition_commands<automaton, state, move,
                                           shape.tags_written>(registers,
                                                               place);
        into.template moved<state, range.target, move>(
            static_cast<char>(symbol), registers, place);
        // What is left of the budget past this move. A step along a chain
        // costs one; a fork shares what is left between the branches it can
        // take, so everything written out from here is bounded by the budget
        // however the automaton is shaped, and a fork no longer ends the
        // writing outright.
        //
        // It used to: only a state with one way out wrote its continuation
        // here, and every fork was a call. A row of comma-separated fields
        // forks at every field -- the letters keep the machine where it is,
        // the comma takes it on -- so every field was a body of its own,
        // reached by a jump, with the registers in memory across it because a
        // call cannot keep them anywhere else. That is what a generated
        // scanner never does, and it cost half again the time of one.
        if constexpr (budget != 0) {
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
          return run_body<automaton, shape, range.target, budget - 1, certain,
                          mark, cursor_type, sentinel_type, register_count,
                          gatherer, answer_type>(
              cursor, last, place, registers, into, best);
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
[[nodiscard]] constexpr bool run_body(
    cursor_type& __restrict cursor, sentinel_type last, mark& __restrict place,
    std::array<mark, register_count>& __restrict registers,
    gatherer& __restrict into, answer_type& __restrict best) {
  constexpr bool by_place = std::is_pointer_v<mark>;
  constexpr bool gathers = !std::same_as<gatherer, gathers_nothing>;
  constexpr bool accepts_here =
      automaton.states[state].accepting_slot !=
      packed_state<0, 0, 0>::not_accepting;

  // Where the walk is, held here rather than through the references it was
  // handed.
  //
  // A run of characters that keeps the machine where it is, is most of what a
  // reading does, and through a reference every one of them is a store: the
  // caller's cursor and the caller's mark have to be right at every moment,
  // because anything at all might look at them. Nothing does until the walk
  // leaves this state, so they are written back there and nowhere else -- which
  // takes two stores a character out of the loop that runs for most of the
  // subject.
  //
  // Where the cursor can be copied at all. An iterator over a stream is
  // move-only -- there is one of it, and reading through it is the reading --
  // so there the caller's own is used and every step writes it, which is what
  // such a subject costs anyway.
  static constexpr bool keeps_its_own = std::copyable<cursor_type>;
  std::conditional_t<keeps_its_own, cursor_type, cursor_type&> here = cursor;
  std::conditional_t<keeps_its_own, mark, mark&> spot = place;
  const auto put_back = [&] {
    if constexpr (keeps_its_own) {
      cursor = here;
      place = spot;
    }
  };
  // Whether the reading can be handed more of the subject part way through.
  constexpr bool asks_for_more = requires(cursor_type& one, sentinel_type end) {
    into.refill(one, end);
  };
  static_cast<void>(asks_for_more);
  if constexpr (shape.longest && accepts_here) {
    best.matched = true;
    best.at = here;
    keep_the_end(best, last);
    keep_the_place<automaton, state>(best, registers, here, spot, into);
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
    one.template took_run<state>(from, from, registers, spot);
  };
  // Whether anything gathering would rather have the run whole. Where nothing
  // would, the run is walked here and each character handed over as it is
  // read: one pass, which is the loop a hand would have written.
  constexpr bool whole_run = !gathers || requires(gatherer& one) {
    requires one.template wants_a_run_whole<state>();
  };
  // And whether it would rather read the run itself, which it can where one
  // place does all the work of it.
  constexpr auto class_of_the_run =
      staying_of<automaton, state, shape.tags_read>();
  constexpr bool reads_the_run_itself =
      gathers && requires(gatherer& one, const char* from) {
        {
          one.template took_class<state, class_of_the_run>(from, from)
        } -> std::same_as<const char*>;
      };
  if constexpr (by_pointer && (!gathers || takes_a_piece) &&
                (shape.in_words || !whole_run) &&
                runs_in_place<automaton, state, shape.tags_read>()) {
    constexpr auto run_class = staying_of<automaton, state, shape.tags_read>();
    const cursor_type from = here;
    if constexpr (gathers && !whole_run && reads_the_run_itself) {
      // The loop belongs to whoever is gathering: see `took_class`.
      here = into.template took_class<state, run_class>(here, last);
      if constexpr (!by_place) spot += here - from;
    } else if constexpr (gathers && !whole_run) {
      while (here != last &&
             inside_of<run_class>(static_cast<unsigned char>(*here))) {
        into.template took_run<state>(here, here + 1, registers, spot);
        ++here;
      }
      if constexpr (!by_place) spot += here - from;
    } else {
      here = skip_class<run_class>(here, last);
      if constexpr (gathers && takes_a_piece) {
        into.template took_run<state>(from, here, registers, spot);
        if constexpr (!by_place) spot += here - from;
      }
    }
    if constexpr (shape.longest && accepts_here) {
      best.at = here;
      keep_the_end(best, last);
      keep_the_place<automaton, state>(best, registers, here, spot, into);
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
      if (here == last) {
        // The reading ran out. Whoever is gathering may have more of it --
        // input that arrives in pieces is contiguous inside a piece, and the
        // walk goes on in the state it is standing in, because the state is
        // where it stands in this code and not a number to be put back.
        if constexpr (requires { into.refill(cursor, last); }) {
          put_back();
          if (!into.refill(cursor, last)) break;
          here = cursor;
        } else {
          put_back();
          break;
        }
      }
    }
    const unsigned char symbol = static_cast<unsigned char>(*here);
    ++here;
    // The operations of a transition are the tags the state before it was
    // holding back, so they are written with the mark of this symbol.
    if constexpr (by_place) {
      spot = here - 1;
    } else {
      ++spot;
    }
    const std::size_t stayed =
        taken_self_move<automaton, state, shape.tags_written, gatherer>(
            symbol, registers, spot, into);
    if (stayed != no_run) {
      if constexpr (shape.longest && accepts_here) {
        best.at = here;
        keep_the_end(best, last);
        keep_the_place<automaton, state>(best, registers, here, spot, into);
      }
      continue;
    }
    // Tested after the class, not before: a terminator no state takes cannot
    // keep the machine where it is, so asking about it first would only add a
    // branch to every character.
    if constexpr (shape.by_terminator) {
      if (symbol == shape.terminator) {
        put_back();
        if constexpr (accepts_here) {
          execute_static_final_commands<automaton, state>(registers, place);
          into.template ended<state>(registers);
          return true;
        } else {
          return best.matched;
        }
      }
    }
    // Handed on where the walk is, not where the caller last heard it was.
    //
    // The state after this one is written out here, and the one after that,
    // and every one of them would begin by reading a cursor out of memory and
    // end by writing it back -- two stores and two loads a character, along a
    // chain that is most of what a reading does. What the chain is handed is
    // this walk's own cursor and mark, and the caller's are written once, when
    // the chain is done with them.
    const bool said =
        dispatch_continuation<automaton, shape, state, budget,
                              counts_here ? certain - 1 : 0, mark, cursor_type,
                              sentinel_type, register_count, gatherer,
                              answer_type>(symbol, here, last, spot, registers,
                                           into, best);
    put_back();
    return said;
  }
  if constexpr (!accepts_here) {
    put_back();
    return best.matched;
  } else {
    if constexpr (shape.longest) {
      best.matched = true;
      best.at = here;
      keep_the_end(best, last);
    }
    if constexpr (by_place) {
      execute_static_final_commands<automaton, state>(registers, here);
    } else {
      execute_static_final_commands<automaton, state>(registers, spot);
    }
    put_back();
    into.template ended<state>(registers);
    return true;
  }
}

// The walk, for whoever is not one of its own frames.
//
// Everything above passes a chain along; a caller outside has none, and what
// it wants back is whether the reading was a match.
template <auto& automaton, walk_shape shape, std::size_t state,
          std::size_t budget, std::size_t certain, class mark,
          class cursor_type, class sentinel_type, std::size_t register_count,
          class gatherer, class answer_type>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool run_continuation(
    cursor_type& __restrict cursor, sentinel_type last, mark& __restrict place,
    std::array<mark, register_count>& __restrict registers,
    gatherer& __restrict into, answer_type& __restrict best) {
  // One function a machine while the program runs, and the states written out
  // where they are reached while it is compiled.
  //
  // A label is not a thing a constant evaluation has, so the walk that reads a
  // pattern at compile time is the one above; everything else takes the other
  // one, which is the whole machine in one body.
  if consteval {
    return run_body<automaton, shape, state, budget, certain, mark, cursor_type,
                    sentinel_type, register_count, gatherer, answer_type>(
        cursor, last, place, registers, into, best);
  } else {
    return run_threaded<automaton, shape, state, mark, cursor_type,
                        sentinel_type, register_count, gatherer, answer_type>(
        cursor, last, place, registers, into, best);
  }
}

// How far a chain of states is written out before the next one is reached by a
// call.
//
// A row of comma-separated fields is a dozen states in a row, each taking one
// character. Reaching each of them by a call is a call for every character of
// the subject, which is what a generated scanner never does -- so the chain is
// followed, and the cap is only against a pattern long enough to make one
// function of the whole of it. The pattern layer has said this for a long time;
// the format layer walked with a budget of nothing, and paid a call a state.
template <auto& automaton>
[[nodiscard]] consteval std::size_t bodies_worth_writing() {
  // A number, and no longer a measurement.
  //
  // This used to walk the machine from every state it could be entered at,
  // counting how many bodies each depth of writing would produce, to find the
  // depth whose total stayed under a ceiling. That was the whole of what kept
  // the generated code from growing past what a compiler would hold -- and
  // nothing is generated this way any more. The states are labels in one
  // function now, and the walk this sizes is the one constant evaluation
  // takes, where a body costs nothing because none is emitted.
  //
  // What it still decides is how deep the instantiation goes before a call,
  // which is a question about how long the compiler takes and not about the
  // program. Eight is enough to keep a chain of literal characters in one
  // instantiation and small enough that no machine makes many.
  return 8;
}

// One function for the whole machine, and a label for every state.
//
// Everything above writes a state out where it is reached, so a state reached
// from two places is written twice, and what the compiler is handed is a set
// of functions that call one another. It cannot see the machine that way: the
// middle end loses the thread at the first boundary, and what should be a
// small loop becomes thousands of instructions.
//
// This is the other way. The states are labels in one function, a move is a
// jump to a label known while compiling, and the whole walk is one body --
// what a generator prints, and what a hand would write. Nothing is spilled
// across a call, and the reading that arrives in pieces stops being a special
// case: there is one end of input, held once.
//
// A label cannot be made by instantiating a template, so the ladder is laid
// out by the preprocessor and every rung past the end of the machine is
// thrown away by `if constexpr`. A rung costs about a millisecond to compile
// and nothing at all to run.
#define SCAN_CAT_(a, b) a##b
#define SCAN_CAT(a, b) SCAN_CAT_(a, b)
// The same row three times over: a macro is not replaced inside its own
// replacement, so a ladder of three digits needs three of them.
#define SCAN_ROW_A(M, p, N)                                                   \
  N(M, SCAN_CAT(p, 0)) N(M, SCAN_CAT(p, 1)) N(M, SCAN_CAT(p, 2))              \
  N(M, SCAN_CAT(p, 3)) N(M, SCAN_CAT(p, 4)) N(M, SCAN_CAT(p, 5))              \
  N(M, SCAN_CAT(p, 6)) N(M, SCAN_CAT(p, 7)) N(M, SCAN_CAT(p, 8))              \
  N(M, SCAN_CAT(p, 9)) N(M, SCAN_CAT(p, a)) N(M, SCAN_CAT(p, b))              \
  N(M, SCAN_CAT(p, c)) N(M, SCAN_CAT(p, d)) N(M, SCAN_CAT(p, e))              \
  N(M, SCAN_CAT(p, f))
#define SCAN_ROW_B(M, p, N)                                                   \
  N(M, SCAN_CAT(p, 0)) N(M, SCAN_CAT(p, 1)) N(M, SCAN_CAT(p, 2))              \
  N(M, SCAN_CAT(p, 3)) N(M, SCAN_CAT(p, 4)) N(M, SCAN_CAT(p, 5))              \
  N(M, SCAN_CAT(p, 6)) N(M, SCAN_CAT(p, 7)) N(M, SCAN_CAT(p, 8))              \
  N(M, SCAN_CAT(p, 9)) N(M, SCAN_CAT(p, a)) N(M, SCAN_CAT(p, b))              \
  N(M, SCAN_CAT(p, c)) N(M, SCAN_CAT(p, d)) N(M, SCAN_CAT(p, e))              \
  N(M, SCAN_CAT(p, f))
#define SCAN_ROW_C(M, p, N)                                                   \
  N(M, SCAN_CAT(p, 0)) N(M, SCAN_CAT(p, 1)) N(M, SCAN_CAT(p, 2))              \
  N(M, SCAN_CAT(p, 3)) N(M, SCAN_CAT(p, 4)) N(M, SCAN_CAT(p, 5))              \
  N(M, SCAN_CAT(p, 6)) N(M, SCAN_CAT(p, 7)) N(M, SCAN_CAT(p, 8))              \
  N(M, SCAN_CAT(p, 9)) N(M, SCAN_CAT(p, a)) N(M, SCAN_CAT(p, b))              \
  N(M, SCAN_CAT(p, c)) N(M, SCAN_CAT(p, d)) N(M, SCAN_CAT(p, e))              \
  N(M, SCAN_CAT(p, f))
#define SCAN_RUNG(M, t) M(t)
#define SCAN_ROW_OF_16(M, p) SCAN_ROW_A(M, p, SCAN_RUNG)
#define SCAN_ROW_OF_256(M, p) SCAN_ROW_B(M, p, SCAN_ROW_OF_16)
#ifndef SCAN_LADDER
#define SCAN_LADDER 256
#endif
#if SCAN_LADDER == 256
#define SCAN_EVERY_RUNG(M) SCAN_ROW_C(M, 0x, SCAN_ROW_OF_16)
#elif SCAN_LADDER == 4096
#define SCAN_EVERY_RUNG(M) SCAN_ROW_C(M, 0x, SCAN_ROW_OF_256)
#else
#error "SCAN_LADDER is 256 or 4096"
#endif

// How many states the machine has, asked of the machine and not of a caller.
template <auto& automaton>
inline constexpr std::size_t states_in =
    std::tuple_size_v<std::remove_cvref_t<decltype(automaton.states)>>;

// The ways out of a state, in the order they are to be tried, and where each
// of them goes.
template <auto& automaton, std::size_t state>
[[nodiscard]] consteval std::size_t ways_out() {
  if constexpr (state < states_in<automaton>) {
    return distinct_moves<automaton, state>().count;
  } else {
    return 0;
  }
}
template <auto& automaton, std::size_t state, std::size_t which>
[[nodiscard]] consteval std::size_t move_at() {
  return distinct_moves<automaton, state>().at[which];
}
template <auto& automaton, std::size_t state, std::size_t which>
[[nodiscard]] consteval std::size_t target_at() {
  return automaton.states[state].ranges[move_at<automaton, state, which>()]
      .target;
}

// What reading one character in a state came to.
enum class step_said { stopped, took };

// Everything a state does before it knows where it is going: the place it
// keeps if it accepts, the run of characters that keeps it where it is, and
// then one character -- which may keep it here again, and then the question is
// asked over. Where the reading ends here, it is ended here: what a match owes
// is owed in the state it stopped in.
template <auto& automaton, walk_shape shape, std::size_t state, class mark,
          class cursor_type, class sentinel_type, std::size_t register_count,
          class gatherer, class answer_type>
[[nodiscard]] SCAN_FORCE_INLINE constexpr step_said step_in_state(
    cursor_type& __restrict here, sentinel_type& __restrict last,
    mark& __restrict spot,
    std::array<mark, register_count>& __restrict registers,
    gatherer& __restrict into, answer_type& __restrict best,
    unsigned char& symbol) {
  constexpr bool by_place = std::is_pointer_v<mark>;
  constexpr bool gathers = !std::same_as<gatherer, gathers_nothing>;
  constexpr bool accepts_here =
      automaton.states[state].accepting_slot !=
      packed_state<0, 0, 0>::not_accepting;
  const auto keep_here = [&] {
    if constexpr (shape.longest && accepts_here) {
      best.matched = true;
      best.at = here;
      keep_the_end(best, last);
      keep_the_place<automaton, state>(best, registers, here, spot, into);
    }
  };
  keep_here();
  constexpr bool by_pointer = std::is_pointer_v<cursor_type>;
  constexpr bool takes_a_piece = requires(gatherer& one, const char* from) {
    one.template took_run<state>(from, from, registers, spot);
  };
  constexpr bool whole_run = !gathers || requires(gatherer& one) {
    requires one.template wants_a_run_whole<state>();
  };
  constexpr auto class_of_the_run =
      staying_of<automaton, state, shape.tags_read>();
  constexpr bool reads_the_run_itself =
      gathers && requires(gatherer& one, const char* from) {
        {
          one.template took_class<state, class_of_the_run>(from, from)
        } -> std::same_as<const char*>;
      };
  if constexpr (by_pointer && (!gathers || takes_a_piece) &&
                (shape.in_words || !whole_run) &&
                runs_in_place<automaton, state, shape.tags_read>()) {
    const cursor_type from = here;
    if constexpr (gathers && !whole_run && reads_the_run_itself) {
      here = into.template took_class<state, class_of_the_run>(here, last);
      if constexpr (!by_place) spot += here - from;
    } else if constexpr (gathers && !whole_run) {
      while (here != last &&
             inside_of<class_of_the_run>(static_cast<unsigned char>(*here))) {
        into.template took_run<state>(here, here + 1, registers, spot);
        ++here;
      }
      if constexpr (!by_place) spot += here - from;
    } else {
      here = skip_class<class_of_the_run>(here, last);
      if constexpr (gathers && takes_a_piece) {
        into.template took_run<state>(from, here, registers, spot);
        if constexpr (!by_place) spot += here - from;
      }
    }
    keep_here();
  }
  for (;;) {
    if constexpr (!shape.by_terminator) {
      if (here == last) {
        if constexpr (requires { into.refill(here, last); }) {
          if (!into.refill(here, last)) {
            // The reading ran out where it stands.
            if constexpr (accepts_here) {
              best.matched = true;
              if constexpr (shape.longest) {
                best.at = here;
                keep_the_end(best, last);
              }
              if constexpr (by_place) {
                execute_static_final_commands<automaton, state>(registers, here);
              } else {
                execute_static_final_commands<automaton, state>(registers, spot);
              }
              into.template ended<state>(registers);
            }
            return step_said::stopped;
          }
        } else {
          if constexpr (accepts_here) {
            best.matched = true;
            if constexpr (shape.longest) {
              best.at = here;
              keep_the_end(best, last);
            }
            if constexpr (by_place) {
              execute_static_final_commands<automaton, state>(registers, here);
            } else {
              execute_static_final_commands<automaton, state>(registers, spot);
            }
            into.template ended<state>(registers);
          }
          return step_said::stopped;
        }
      }
    }
    symbol = static_cast<unsigned char>(*here);
    ++here;
    if constexpr (by_place) {
      spot = here - 1;
    } else {
      ++spot;
    }
    if (taken_self_move<automaton, state, shape.tags_written, gatherer>(
            symbol, registers, spot, into) != no_run) {
      if constexpr (shape.longest && accepts_here) {
        best.at = here;
        keep_the_end(best, last);
        keep_the_place<automaton, state>(best, registers, here, spot, into);
      }
      continue;
    }
    if constexpr (shape.by_terminator) {
      if (symbol == shape.terminator) {
        if constexpr (accepts_here) {
          execute_static_final_commands<automaton, state>(registers, spot);
          into.template ended<state>(registers);
          best.matched = true;
        }
        return step_said::stopped;
      }
    }
    return step_said::took;
  }
}

// A character was read and no move takes it: the walk is over, and where this
// state accepts, the match ends one character back from where the walk got to.
template <auto& automaton, walk_shape shape, std::size_t state, class mark,
          std::size_t register_count, class gatherer, class answer_type>
SCAN_FORCE_INLINE constexpr void end_with_no_move(
    mark& spot, std::array<mark, register_count>& registers, gatherer& into,
    answer_type& best) {
  if constexpr (shape.longest) {
    constexpr bool accepts_here =
        automaton.states[state].accepting_slot !=
        packed_state<0, 0, 0>::not_accepting;
    if constexpr (accepts_here) {
      if constexpr (std::is_pointer_v<mark>) {
        execute_static_final_commands<automaton, state>(registers, spot);
      } else {
        execute_static_final_commands<automaton, state>(registers, spot - 1);
      }
      into.template ended<state>(registers);
      best.matched = true;
    }
  }
}

// The operations of one move, run where the move is taken.
template <auto& automaton, walk_shape shape, std::size_t state,
          std::size_t which, class mark, std::size_t register_count,
          class gatherer>
SCAN_FORCE_INLINE constexpr void take_move(
    unsigned char symbol, std::array<mark, register_count>& registers,
    mark& spot, gatherer& into) {
  constexpr std::size_t move = move_at<automaton, state, which>();
  into.template moving<state, move>(registers, spot);
  execute_static_transition_commands<automaton, state, move,
                                     shape.tags_written>(registers, spot);
  into.template moved<state, target_at<automaton, state, which>(), move>(
      static_cast<char>(symbol), registers, spot);
}

// One way out of one state. A move that keeps the machine where it is has
// already been taken above, so it is not asked about again here.
#define SCAN_WAY_OUT(t, w)                                                    \
  if constexpr (w < ways_out<automaton, t>() &&                               \
                target_at<automaton, t, w>() != t) {                          \
    if (makes_move<automaton, t, move_at<automaton, t, w>()>(symbol))         \
        [[likely]] {                                                          \
      take_move<automaton, shape, t, w>(symbol, registers, spot, into);       \
      goto* rungs[target_at<automaton, t, w>()];                              \
    }                                                                         \
  }

#define SCAN_RUNG_NAME(t) &&SCAN_CAT(scan_at_, t),
// Where the walk starts is known while compiling, so it is reached by a jump
// to a label and not through the table. A search runs a walk from every
// position in the subject; going in through the table would be an indirect
// branch nobody can predict, once for every character of it.
#define SCAN_RUNG_ENTRY(t)                                                    \
  if constexpr (entry == t) goto SCAN_CAT(scan_at_, t);
#define SCAN_RUNG_BODY(t)                                                     \
  SCAN_CAT(scan_at_, t)                                                       \
      : if constexpr (t < states_in<automaton>) {                             \
    if (step_in_state<automaton, shape, t>(here, last_here, spot, registers,   \
                                          into, best, symbol) ==            \
        step_said::took) {                                                    \
      SCAN_WAY_OUT(t, 0)                                                      \
      SCAN_WAY_OUT(t, 1)                                                      \
      SCAN_WAY_OUT(t, 2)                                                      \
      SCAN_WAY_OUT(t, 3)                                                      \
      SCAN_WAY_OUT(t, 4)                                                      \
      SCAN_WAY_OUT(t, 5)                                                      \
      SCAN_WAY_OUT(t, 6)                                                      \
      SCAN_WAY_OUT(t, 7)                                                      \
      end_with_no_move<automaton, shape, t>(spot, registers, into, best);     \
    }                                                                         \
  }                                                                           \
  goto scan_over;

// The walk itself: one function, one body a state, and every move a jump.
// Reached by a call, and there is no asking otherwise.
//
// A function whose labels have had their addresses taken is one no inliner
// will write into its caller: the addresses would have to be duplicated, and
// there is no meaning for that. So this walk is a call however it is marked --
// which is the price of the labels, and it is paid where everything the walk
// touches is handed to it by reference and therefore lives in memory.
template <auto& automaton, walk_shape shape, std::size_t entry, class mark,
          class cursor_type, class sentinel_type, std::size_t register_count,
          class gatherer, class answer_type>
[[nodiscard]] bool run_threaded(
    cursor_type& __restrict cursor, sentinel_type last, mark& __restrict place,
    std::array<mark, register_count>& __restrict registers,
    gatherer& __restrict into, answer_type& __restrict best) {
  static_assert(states_in<automaton> <= SCAN_LADDER,
                "this machine has more states than the ladder has rungs: "
                "build with -DSCAN_LADDER=4096");
  static void* const rungs[] = {SCAN_EVERY_RUNG(SCAN_RUNG_NAME)};
  // Where the walk is, held here rather than through the references it was
  // handed -- but only where it can be held. An iterator over a stream is
  // move-only: there is one of it, and reading through it is the reading, so
  // there the caller's own is used and every step writes it, which is what
  // such a subject costs anyway.
  static constexpr bool keeps_its_own = std::copyable<cursor_type>;
  std::conditional_t<keeps_its_own, cursor_type, cursor_type&> here = cursor;
  std::conditional_t<keeps_its_own, mark, mark&> spot = place;
  sentinel_type last_here = last;
  unsigned char symbol = 0;
  SCAN_EVERY_RUNG(SCAN_RUNG_ENTRY)
  goto scan_over;
  SCAN_EVERY_RUNG(SCAN_RUNG_BODY)
scan_over:
  if constexpr (keeps_its_own) {
    cursor = here;
    place = spot;
  }
  return best.matched;
}

template <auto& automaton>
[[nodiscard]] consteval auto tags_always_written();

// The same walk, owning everything it works on.
//
// A function whose labels have had their addresses taken is one no inliner
// will write into its caller, so whatever it is handed by reference stays in
// memory for as long as the walk runs: the compiler has to assume a call can
// look at anything it was given the address of. The gatherings a reading fills
// in are the whole of what it does, and they were on the stack, written back
// at every character.
//
// Handed nothing and told to make its own, none of it escapes: the gatherer,
// the registers and the place the walk liked best are values of this function
// and go wherever values go. What comes back is the reading, already made.
template <auto& automaton, walk_shape shape, std::size_t entry, class mark,
          class cursor_type, class sentinel_type, std::size_t register_count,
          class gatherer, class answer_type>
[[nodiscard]] auto run_threaded_owning(cursor_type cursor, sentinel_type last,
                                       const char* text) {
  static_assert(states_in<automaton> <= SCAN_LADDER,
                "this machine has more states than the ladder has rungs: "
                "build with -DSCAN_LADDER=4096");
  static void* const rungs[] = {SCAN_EVERY_RUNG(SCAN_RUNG_NAME)};
  std::array<mark, register_count> registers;
  constexpr auto written_everywhere = tags_always_written<automaton>();
  constexpr mark nowhere =
      std::is_pointer_v<mark> ? mark{} : mark(scan::tre::negative_tag);
  // A constant evaluation may not read what was never written, and it does not
  // care what the clearing costs: there, everything is set.
  if consteval {
    for (mark& one : registers) one = nowhere;
  } else {
    [&]<std::size_t... tag>(std::index_sequence<tag...>) {
      ((written_everywhere[tag] ? void() : void(registers[tag] = nowhere)),
       ...);
    }(std::make_index_sequence<automaton.tag_count>{});
  }
  if constexpr (std::is_pointer_v<mark>) {
    execute_commands(automaton.initialize, automaton.initialize.size(),
                     registers, static_cast<const char*>(nullptr));
  } else {
    execute_commands(automaton.initialize, automaton.initialize.size(),
                     registers, mark{});
  }
  gatherer into;
  if constexpr (requires { into.points_at(text); }) {
    if (text != nullptr) into.points_at(text);
  }
  answer_type best;
  cursor_type here = cursor;
  mark spot = std::is_pointer_v<mark> ? mark(cursor) : mark{};
  sentinel_type last_here = last;
  unsigned char symbol = 0;
  SCAN_EVERY_RUNG(SCAN_RUNG_ENTRY)
  goto scan_over;
  SCAN_EVERY_RUNG(SCAN_RUNG_BODY)
scan_over:
  return into.taken();
}

// Walking characters in a row to a terminator, gathering nothing.
template <auto& automaton, unsigned char terminator, bool in_words,
          std::size_t state, std::size_t register_count>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool run_to_terminator(
    const char* cursor, const char* end,
    std::array<const char*, register_count>& registers) {
  gathers_nothing nothing;
  const char* place = cursor;
  walk_answer<const char*> best;
  constexpr walk_shape shape{.in_words = in_words,
                             .by_terminator = true,
                             .terminator = terminator,
                             .budget = bodies_worth_writing<automaton>()};
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
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool run_from_here(
    const char* cursor, const char* end,
    std::array<const char*, register_count>& registers) {
  gathers_nothing nothing;
  const char* place = cursor;
  walk_answer<const char*> best;
  constexpr walk_shape shape{.in_words = in_words,
                             .budget = bodies_worth_writing<automaton>()};
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


#undef SCAN_FORCE_INLINE

}  // namespace scan::detail
