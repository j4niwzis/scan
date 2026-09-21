export module scan.runtime;

import std;
import scan.tre;
export import scan.compiler;

namespace scan::detail {

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
export template <class Mark>
inline constexpr Mark absent_mark = [] {
  if constexpr (std::is_pointer_v<Mark>) {
    return nullptr;
  } else {
    return scan::tre::negative_tag;
  }
}();

template <class Mark, class Sequence>
struct marks_tuple;
template <class Mark, std::size_t... Which>
struct marks_tuple<Mark, std::index_sequence<Which...>> {
  using type = std::tuple<decltype(Which, Mark())...>;
};

// The marks a walk writes: a variable each, not a row of cells.
//
// A tuple rather than an array, and that is the whole point of it. Every index
// into this file is known while the program is compiled -- the commands of a
// move are unrolled with their destinations as constants -- and a tuple is the
// type that says so, because an index that came out of the data does not
// compile against it. An array allowed one, and one is all it takes: a single
// access with a computed index puts the whole file in memory, and every
// command after it becomes a store with a dependent load behind it.
//
// re2c has no such array. It writes `yyt1 = YYCURSOR` against a named local
// and copies marks between locals, which is why what it emits keeps them in
// machine registers. This is that, said in a type.
export template <class Mark, std::size_t Count>
struct register_file {
  using storage_type =
      typename marks_tuple<Mark, std::make_index_sequence<Count>>::type;
  storage_type kept{};

  template <std::size_t Which>
  [[nodiscard]] constexpr Mark& at() noexcept {
    return std::get<Which>(kept);
  }
  template <std::size_t Which>
  [[nodiscard]] constexpr const Mark& at() const noexcept {
    return std::get<Which>(kept);
  }
  // Where the answer is read out of the file, which happens once a reading and
  // never on a character.
  [[nodiscard]] constexpr std::array<Mark, Count> row() const noexcept {
    return [&]<std::size_t... which>(std::index_sequence<which...>) {
      return std::array<Mark, Count>{std::get<which>(kept)...};
    }(std::make_index_sequence<Count>{});
  }
  constexpr void fill(Mark value) noexcept {
    [&]<std::size_t... which>(std::index_sequence<which...>) {
      ((std::get<which>(kept) = value), ...);
    }(std::make_index_sequence<Count>{});
  }
  // The one place a number is not known while compiling: the commands a
  // machine runs before it reads anything. Written out as a comparison per
  // slot, which costs what it costs once and keeps the file out of memory.
  constexpr void write(std::size_t which, Mark value) noexcept {
    [&]<std::size_t... slot>(std::index_sequence<slot...>) {
      (((slot == which) ? void(std::get<slot>(kept) = value) : void()), ...);
    }(std::make_index_sequence<Count>{});
  }
  [[nodiscard]] constexpr Mark read(std::size_t which) const noexcept {
    Mark found = absent_mark<Mark>;
    [&]<std::size_t... slot>(std::index_sequence<slot...>) {
      (((slot == which) ? void(found = std::get<slot>(kept)) : void()), ...);
    }(std::make_index_sequence<Count>{});
    return found;
  }
};

// A mark by a number nobody knows while compiling, from whichever file holds
// it.
//
// The walk that runs keeps its marks in a tuple; the walk that reads a range as
// it comes, and the one that reads an automaton built at run time, keep theirs
// in an array. Both are asked the same way here, so that a number out of the
// data stays possible where it is unavoidable and impossible everywhere else.
export template <class FileType>
[[nodiscard]] SCAN_FORCE_INLINE constexpr auto slot_read(
    const FileType& registers, std::size_t which) {
  if constexpr (requires { registers.read(which); }) {
    return registers.read(which);
  } else {
    return registers[which];
  }
}

export template <class FileType, class Mark>
SCAN_FORCE_INLINE constexpr void slot_write(FileType& registers,
                                            std::size_t which, Mark value) {
  if constexpr (requires { registers.write(which, value); }) {
    registers.write(which, value);
  } else {
    registers[which] = value;
  }
}

// A mark by a number known while compiling, from whichever file holds it.
//
// The walk that runs keeps its marks in a tuple, where the number has to be a
// constant. The walk that reads an automaton built at run time keeps them in a
// vector, where it cannot be. Both are asked the same way here.
export template <std::size_t Which, class FileType>
[[nodiscard]] SCAN_FORCE_INLINE constexpr auto mark_at(const FileType& registers) {
  if constexpr (requires { registers.template at<Which>(); }) {
    return registers.template at<Which>();
  } else {
    return registers[Which];
  }
}

template <class Mark>
SCAN_FORCE_INLINE constexpr void execute_command(const packed_command& command,
                                                 Mark source_value, Mark& slot,
                                                 Mark here) {
  Mark value = absent_mark<Mark>;
  if (command.source != packed_command::no_source) {
    value = source_value;
  }
  if (command.value == -1) value = absent_mark<Mark>;
  if (command.value == 0) value = here;
  slot = value;
}

// A slot of whichever file holds the marks, by a number known while compiling.
template <std::size_t Which, class FileType>
[[nodiscard]] SCAN_FORCE_INLINE constexpr auto& slot_ref(FileType& registers) {
  if constexpr (requires { registers.template at<Which>(); }) {
    return registers.template at<Which>();
  } else {
    return registers[Which];
  }
}

// The commands a machine runs before it reads anything, with their numbers
// where they belong -- in the code.
//
// The list is a constant of the machine: every destination and every source in
// it is known while compiling. Asked by number instead, against a file that is
// a tuple, each one costs a comparison with every slot it is not -- a hundred
// of them before the first character, paid by every reading. On a long subject
// that hides; on a short one it is most of the time.
export template <auto& Automaton, class Mark, class FileType>
SCAN_FORCE_INLINE constexpr void execute_initial(FileType& registers,
                                                 Mark here) {
  [&]<std::size_t... index>(std::index_sequence<index...>) {
    // Every source read before any destination is written: the commands of one
    // step happen at once.
    const std::array<Mark, sizeof...(index)> source_values{
        [&]() -> Mark {
          constexpr auto one = Automaton.initialize[index];
          if constexpr (one.source == packed_command::no_source) {
            return absent_mark<Mark>;
          } else {
            return slot_ref<one.source>(registers);
          }
        }()...};
    ([&] {
      constexpr auto one = Automaton.initialize[index];
      Mark value = absent_mark<Mark>;
      if constexpr (one.source != packed_command::no_source) {
        value = source_values[index];
      }
      if constexpr (one.value == -1) value = absent_mark<Mark>;
      if constexpr (one.value == 0) value = here;
      slot_ref<one.destination>(registers) = value;
    }(), ...);
  }(std::make_index_sequence<Automaton.initialize.size()>{});
}

export template <class Mark, class FileType, std::size_t CommandCount>
SCAN_FORCE_INLINE constexpr void execute_commands(
    const std::array<packed_command, CommandCount>& commands,
    std::size_t count, FileType& registers, Mark here) {
  std::array<Mark, CommandCount> source_values{};
  std::size_t index = 0;
  for (const packed_command& command : commands | std::views::take(count)) {
    source_values[index++] = command.source == packed_command::no_source
                                 ? absent_mark<Mark>
                                 : slot_read(registers, command.source);
  }
  index = 0;
  for (const packed_command& command : commands | std::views::take(count)) {
    Mark value = absent_mark<Mark>;
    const Mark source_value = source_values[index++];
    if (command.source != packed_command::no_source) value = source_value;
    if (command.value == -1) value = absent_mark<Mark>;
    if (command.value == 0) value = here;
    slot_write(registers, command.destination, value);
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
export template <auto& Automaton, std::uint64_t TagsRead>
inline constexpr auto registers_worth_writing = [] consteval {
  constexpr std::size_t count = Automaton.register_count;
  std::array<bool, count> live{};
  for (std::size_t at = 0; at < count; ++at) {
    const std::size_t group = Automaton.register_tag[at] / 2;
    live[at] = group >= 64 || (TagsRead & (std::uint64_t{1} << group)) != 0;
  }
  bool again = true;
  while (again) {
    again = false;
    for (std::size_t state = 0; state < Automaton.states.size(); ++state) {
      const auto& packed = Automaton.states[state];
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

template <auto& Automaton, std::size_t State, std::size_t Range,
          std::uint64_t TagsRead, class Mark, std::size_t RegisterCount>
SCAN_FORCE_INLINE constexpr void execute_static_transition_commands(
    register_file<Mark, RegisterCount>& registers, Mark here) {
  constexpr const auto& transition =
      Automaton.states[State].ranges[Range];
  static constexpr auto live = registers_worth_writing<Automaton, TagsRead>;
  [&]<std::size_t... index> SCAN_FORCE_INLINE_LAMBDA(
      std::index_sequence<index...>) {
        const std::array<Mark, sizeof...(index)> source_values{
            [&]() -> Mark {
              constexpr auto command = transition.commands[index];
              if constexpr (!live[command.destination] ||
                            command.source == packed_command::no_source) {
                return absent_mark<Mark>;
              } else {
                return registers.template at<command.source>();
              }
            }()...};
        ([&] SCAN_FORCE_INLINE_LAMBDA {
          if constexpr (live[transition.commands[index].destination]) {
            execute_command(
                transition.commands[index], source_values[index],
                registers.template at<transition.commands[index].destination>(),
                here);
          }
        }(),
         ...);
      }(std::make_index_sequence<transition.command_count>{});
}

template <auto& Automaton, std::size_t State, class Mark,
          std::size_t RegisterCount>
SCAN_FORCE_INLINE constexpr void execute_static_final_commands(
    register_file<Mark, RegisterCount>& registers, Mark here) {
  constexpr const auto& packed_state = Automaton.states[State];
  [&]<std::size_t... index> SCAN_FORCE_INLINE_LAMBDA(
      std::index_sequence<index...>) {
        const std::array<Mark, sizeof...(index)> source_values{
            [&]() -> Mark {
              constexpr auto command = packed_state.final_commands[index];
              if constexpr (command.source == packed_command::no_source) {
                return absent_mark<Mark>;
              } else {
                return registers.template at<command.source>();
              }
            }()...};
        (execute_command(
             packed_state.final_commands[index], source_values[index],
             registers.template at<packed_state.final_commands[index]
                                       .destination>(),
             here),
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
export struct staying_class {
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
// How many runs a move is worth comparing before it is worth a table.
//
// Nought, which is to say always the table -- and the number is here rather
// than in the sentence above because it was measured and the measurement went
// the other way from the guess. Instructions for one reading, the process
// start subtracted: a fold over a thousand heaps is 58,541 comparing and
// 55,541 by table, and a match of an address is 376 comparing and 298 by
// table. The table is a byte read out of a line the walk is already standing
// on; the comparisons are a chain whose length is the number of runs, and a
// state's runs are answered one after another for every way out of it.
//
// Left as a number somebody can put back: a machine whose states have one run
// each pays a load where a compare would have done, and the line it reads is
// one the cache would rather have kept for the subject.
#ifndef SCAN_TABLE_ABOVE
#define SCAN_TABLE_ABOVE 0
#endif

#if defined(__clang__) || defined(__GNUC__)
#define SCAN_HAS_LANES 1
// Written as an initialisation and not as a loop that fills one in: a lane is a
// vector type, and writing an element of one is a thing GCC will not do while
// it is compiling. Said as a list of every element at once, both compilers make
// the constant.
template <class LaneType, std::size_t... Index>
[[nodiscard]] constexpr LaneType spread_over(unsigned char value,
                                             std::index_sequence<Index...>) {
  return LaneType{(static_cast<void>(Index), value)...};
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpsabi"
#endif

template <class LaneType>
[[nodiscard]] constexpr LaneType spread_over(unsigned char value) {
  return spread_over<LaneType>(value,
                               std::make_index_sequence<sizeof(LaneType)>{});
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

template <staying_class Klass>
[[nodiscard]] consteval nibble_tables nibbles_of() {
  nibble_tables made{};
  for (unsigned symbol = 0; symbol < 128; ++symbol) {
    bool inside = false;
    for (std::size_t index = 0; index < Klass.count; ++index) {
      if (symbol >= Klass.first[index] && symbol <= Klass.last[index]) {
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
template <class LaneType, std::array<unsigned char, 16> Table,
          std::size_t... Index>
[[nodiscard]] constexpr LaneType table_over(std::index_sequence<Index...>) {
  return LaneType{Table[Index & 15]...};
}

template <class LaneType, std::array<unsigned char, 16> Table>
[[nodiscard]] constexpr LaneType table_over() {
  return table_over<LaneType, Table>(
      std::make_index_sequence<sizeof(LaneType)>{});
}

#if defined(__SSSE3__) || defined(__AVX2__)
#define SCAN_HAS_SHUFFLE 1
template <class LaneType>
[[nodiscard]] SCAN_FORCE_INLINE LaneType shuffled(LaneType table,
                                                   LaneType picks) {
  using signed_lane [[gnu::vector_size(sizeof(LaneType))]] = char;
  if constexpr (sizeof(LaneType) == 32) {
    return static_cast<LaneType>(__builtin_ia32_pshufb256(
        static_cast<signed_lane>(table), static_cast<signed_lane>(picks)));
  } else {
    return static_cast<LaneType>(__builtin_ia32_pshufb128(
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

template <staying_class Klass, class LaneType>
SCAN_FORCE_INLINE void outside_of(LaneType letters,
                                  decltype(std::declval<LaneType>() <
                                           std::declval<LaneType>())& answer) {
#if defined(SCAN_HAS_SHUFFLE)
  if constexpr (Klass.below_the_high_bit &&
                Klass.count > runs_worth_comparing_in_lanes &&
                (sizeof(LaneType) == 16 || sizeof(LaneType) == 32)) {
    constexpr nibble_tables tables = nibbles_of<Klass>();
    const LaneType low = table_over<LaneType, tables.low>();
    const LaneType high = table_over<LaneType, tables.high>();
    const LaneType fifteen = spread_over<LaneType>(15);
    const LaneType by_low = shuffled(low, letters & fifteen);
    const LaneType by_high = shuffled(high, (letters >> 4) & fifteen);
    answer = (by_low & by_high) == spread_over<LaneType>(0);
    return;
  }
#endif
  const auto belongs = [&]<std::size_t index>(auto& into, bool first) {
    constexpr LaneType low = spread_over<LaneType>(Klass.first[index]);
    constexpr LaneType span = spread_over<LaneType>(
        static_cast<unsigned char>(Klass.last[index] - Klass.first[index]));
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
  }(std::make_index_sequence<Klass.count - 1>{});
  answer = ~answer;
}

// Did any of them fall out of the class? Not which -- any. The comparison
// collapses to one bit, and where the compiler can fold a vector down to a
// scalar it does it in a couple of instructions.
export template <class LaneType>
[[nodiscard]] SCAN_FORCE_INLINE bool any_of(LaneType mask) {
#if __has_builtin(__builtin_reduce_or)
  return __builtin_reduce_or(mask) != 0;
#else
  std::uint64_t words[sizeof(LaneType) / 8];
  __builtin_memcpy(words, &mask, sizeof(mask));
  std::uint64_t together = 0;
  for (std::uint64_t word : words) together |= word;
  return together != 0;
#endif
}

// Which of them fell out of the class, and not only whether one did.
//
// The eight-character step below already answers this from the word it has --
// the first byte set is the first character outside, and counting zeros finds
// it. The wider steps threw that answer away: a `break` sent them down through
// every narrower step to read the same characters again, and then one at a
// time, to learn what the mask already said. Asked on the way out, where it is
// asked once per run and not once per character, it costs what counting zeros
// costs.
template <class LaneType>
[[nodiscard]] SCAN_FORCE_INLINE std::size_t first_of(LaneType mask) {
  std::uint64_t words[sizeof(LaneType) / 8];
  __builtin_memcpy(words, &mask, sizeof(mask));
  for (std::size_t at = 0; at < sizeof(LaneType) / 8; ++at) {
    if (words[at] != 0) {
      return at * 8 +
             (static_cast<std::size_t>(std::countr_zero(words[at])) >> 3);
    }
  }
  return sizeof(LaneType);
}
#endif

// Whether one character belongs to the run a state keeps itself by.
//
// The same question the vectors ask of sixty-four at once, asked of one -- for
// the head of a run, where there are not sixty-four to ask about yet.
export template <staying_class Klass>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool inside_of(unsigned char letter) {
  bool belongs = false;
  for (std::size_t at = 0; at < Klass.count; ++at) {
    belongs = belongs || (letter >= Klass.first[at] && letter <= Klass.last[at]);
  }
  return belongs;
}

export template <staying_class Klass, bool InWords = true>
[[nodiscard]] SCAN_FORCE_INLINE constexpr const char* skip_class(
    const char* cursor, const char* limit) {
  // Everything below reads several characters as one number and then asks which
  // end of it they came from, so it holds only where the first character is the
  // least significant byte. Elsewhere the caller reads them one at a time,
  // which is what it would have done anyway.
  if (std::is_constant_evaluated()) return cursor;
  // The same run, walked as a hand would walk it.
  //
  // Stepping over a run in words and handing it over whole are two separate
  // things, and a reading asked for one character at a time gives up only the
  // first of them: the characters still lie in a row, so whoever gathers them
  // still takes the run as a piece. Where the run is short -- and it is what
  // a reading asks to be read this way for -- the words were never worth their
  // setting up anyway.
  if constexpr (!InWords) {
    while (cursor != limit &&
           inside_of<Klass>(static_cast<unsigned char>(*cursor)))
      ++cursor;
    return cursor;
  }
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
    // Eight read one at a time before any wider step is set up, because a run
    // of five characters is read faster than a vector is filled for it. A run
    // with sixty-four characters still in front of it is not that run: the
    // peel is what a short run needs and what a long one pays for, eight
    // characters at every boundary it crosses. So it is asked for by length,
    // the same question the walk itself is chosen by.
    if (limit - cursor < 64) {
      for (int step = 0; step < 8 && cursor != limit; ++step) {
        if (!inside_of<Klass>(static_cast<unsigned char>(*cursor))) {
          return cursor;
        }
        ++cursor;
      }
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
        decltype(std::declval<lane>() < std::declval<lane>()) head_out{},
            tail_out{};
        outside_of<Klass>(head, head_out);
        outside_of<Klass>(tail, tail_out);
        if (any_of(head_out | tail_out)) {
          const std::size_t at = first_of(head_out);
          return cursor + (at < 32 ? at : 32 + first_of(tail_out));
        }
        cursor += 64;
      }
      while (limit - cursor >= 32) {
        lane letters{};
        __builtin_memcpy(&letters, cursor, 32);
        decltype(letters < letters) outside{};
        outside_of<Klass>(letters, outside);
        if (any_of(outside)) return cursor + first_of(outside);
        cursor += 32;
      }
    }
    {
      using lane [[gnu::vector_size(16)]] = unsigned char;
      while (limit - cursor >= 16) {
        lane letters{};
        __builtin_memcpy(&letters, cursor, 16);
        decltype(letters < letters) outside{};
        outside_of<Klass>(letters, outside);
        if (any_of(outside)) return cursor + first_of(outside);
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
    if constexpr (Klass.below_the_high_bit &&
                  Klass.count <= runs_worth_comparing_in_lanes) {
      constexpr std::uint64_t ones = 0x0101010101010101ull;
      constexpr std::uint64_t highs = 0x8080808080808080ull;
      while (limit - cursor >= 8) {
        std::uint64_t word = 0;
        __builtin_memcpy(&word, cursor, 8);
        std::uint64_t foreign = ~std::uint64_t{0};
        for (std::size_t index = 0; index < Klass.count; ++index) {
          const std::uint64_t below =
              (word - ones * Klass.first[index]) & ~word & highs;
          const std::uint64_t above =
              (word + ones * (127 - Klass.last[index])) & ~word & highs;
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
template <auto& Automaton, class RangeType>
[[nodiscard]] consteval bool writes_for_a_reader(const RangeType& range,
                                                 std::uint64_t tags_read) {
  for (std::size_t at = 0; at < range.command_count; ++at) {
    const std::size_t group =
        Automaton.register_tag[range.commands[at].destination] / 2;
    if (group >= 64) return true;
    if ((tags_read & (std::uint64_t{1} << group)) != 0) return true;
  }
  return false;
}

template <auto& Automaton, std::size_t State,
          std::uint64_t TagsRead = ~std::uint64_t{0}>
[[nodiscard]] consteval staying_class staying_of() {
  constexpr const auto& packed = Automaton.states[State];
  staying_class answer{};
  answer.below_the_high_bit = true;
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    const auto& range = packed.ranges[index];
    if (range.target != State) continue;
    if (writes_for_a_reader<Automaton>(range, TagsRead)) return staying_class{};
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

template <auto& Automaton, std::size_t State,
          std::uint64_t TagsRead = ~std::uint64_t{0}>
[[nodiscard]] consteval bool runs_in_place() {
  return staying_of<Automaton, State, TagsRead>().count != 0;
}

// Whether two runs of a state go to the same place and write the same thing.
//
// The captureless walk groups a state's runs by where they lead, so that what
// follows the move is written once for the place instead of once for the run.
// Here a move is where it leads and what it writes on the way, so runs are
// grouped by both -- a run that writes something of its own is a move of its
// own.
template <auto& Automaton, std::size_t State>
[[nodiscard]] consteval bool runs_agree(std::size_t left, std::size_t right) {
  const auto& packed = Automaton.states[State];
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

template <std::size_t Capacity>
struct distinct_moves_of {
  std::array<std::size_t, Capacity> at{};
  std::size_t count = 0;
};

// The moves a state can make, each named once by the first run that makes it.
template <auto& Automaton, std::size_t State>
[[nodiscard]] consteval auto distinct_moves() {
  constexpr const auto& packed = Automaton.states[State];
  distinct_moves_of<packed.ranges.size()> made;
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    bool named = false;
    for (std::size_t other = 0; other < made.count; ++other) {
      if (runs_agree<Automaton, State>(made.at[other], index)) named = true;
    }
    if (!named) made.at[made.count++] = index;
  }
  // The move that finishes the reading is asked about last.
  //
  // A move into a state that accepts is made once, whatever the subject's
  // length; every other move is made as many times as the subject is long.
  // Asked about first, that one move costs a comparison on every character of
  // the subject to say no. Asked about last it costs one at the end.
  //
  // The order is free to choose because the runs that make different moves do
  // not overlap: no character makes two of them, so no character can be
  // claimed by whichever is asked about first.
  distinct_moves_of<packed.ranges.size()> sorted;
  for (std::size_t pass = 0; pass < 2; ++pass) {
    for (std::size_t at = 0; at < made.count; ++at) {
      const std::size_t target = packed.ranges[made.at[at]].target;
      const bool ends = Automaton.states[target].accepting_slot !=
                        packed_state<0, 0, 0>::not_accepting;
      if (ends == (pass == 1)) sorted.at[sorted.count++] = made.at[at];
    }
  }
  return sorted;
}

// How many runs make the same move.
template <auto& Automaton, std::size_t State, std::size_t Move>
[[nodiscard]] consteval std::size_t runs_making() {
  constexpr const auto& packed = Automaton.states[State];
  std::size_t count = 0;
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    if (runs_agree<Automaton, State>(Move, index)) ++count;
  }
  return count;
}

// Which symbols make this move, for a move that a handful of runs make.
export template <auto& Automaton, std::size_t State, std::size_t Move>
inline constexpr auto move_table = [] consteval {
  constexpr const auto& packed = Automaton.states[State];
  std::array<unsigned char, 256> made{};
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    if (!runs_agree<Automaton, State>(Move, index)) continue;
    for (std::size_t symbol = packed.ranges[index].first;
         symbol <= packed.ranges[index].last; ++symbol) {
      made[symbol] = 1;
    }
  }
  return made;
}();

template <auto& Automaton, std::size_t State, std::size_t Move,
          std::size_t Index = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool makes_move_by_runs(
    unsigned char symbol) {
  constexpr const auto& packed = Automaton.states[State];
  if constexpr (Index == packed.range_count) {
    return false;
  } else if constexpr (!runs_agree<Automaton, State>(Move, Index)) {
    return makes_move_by_runs<Automaton, State, Move, Index + 1>(symbol);
  } else {
    constexpr const auto& range = packed.ranges[Index];
    if (symbol >= range.first && symbol <= range.last) return true;
    return makes_move_by_runs<Automaton, State, Move, Index + 1>(symbol);
  }
}

// Whether the symbol makes this move. Asked of a table where several runs make
// it, and of the runs themselves where one or two do -- which is the same
// bargain the captureless walk strikes, measured there.
template <auto& Automaton, std::size_t State, std::size_t Move>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool makes_move(
    unsigned char symbol) {
  if constexpr (runs_making<Automaton, State, Move>() >
                SCAN_TABLE_ABOVE) {
    return move_table<Automaton, State, Move>[symbol] != 0;
  } else {
    return makes_move_by_runs<Automaton, State, Move>(symbol);
  }
}

template <auto& Automaton, std::size_t State, std::uint64_t TagsRead,
          class Mark, std::size_t RegisterCount, std::size_t Which = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool
execute_tagged_self_transition(
    unsigned char symbol, register_file<Mark, RegisterCount>& registers,
    Mark here) {
  constexpr auto moves = distinct_moves<Automaton, State>();
  if constexpr (Which == moves.count) {
    return false;
  } else if constexpr (Automaton.states[State].ranges[moves.at[Which]].target !=
                       State) {
    // A move that leads elsewhere is passed over without being compared
    // against. It used to be compared and then declined, which put the test for
    // the comma that ends a field inside the loop that reads the field -- one
    // comparison and one branch on every letter, to find something that happens
    // once. The runs of a state do not overlap, so a symbol skipped here
    // cannot make any of the other moves either, and the answer is the same.
    return execute_tagged_self_transition<Automaton, State, TagsRead, Mark,
                                          RegisterCount, Which + 1>(
        symbol, registers, here);
  } else {
    constexpr std::size_t move = moves.at[Which];
    if (makes_move<Automaton, State, move>(symbol)) {
      execute_static_transition_commands<Automaton, State, move, TagsRead>(registers,
                                                                 here);
      return true;
    }
    return execute_tagged_self_transition<Automaton, State, TagsRead, Mark,
                                          RegisterCount, Which + 1>(
        symbol, registers, here);
  }
}




// Which move keeps the machine here, or none. The move is what the gatherer
// needs: its commands are what a field's gathering follows.
template <auto& Automaton, std::size_t State, std::uint64_t TagsRead,
          class Gatherer, class Mark, std::size_t RegisterCount,
          std::size_t Which = 0>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::size_t taken_self_move(
    unsigned char symbol, register_file<Mark, RegisterCount>& registers,
    Mark here, Gatherer& into) {
  constexpr auto moves = distinct_moves<Automaton, State>();
  if constexpr (Which == moves.count) {
    return no_run;
  } else if constexpr (Automaton.states[State].ranges[moves.at[Which]].target !=
                       State) {
    return taken_self_move<Automaton, State, TagsRead, Gatherer, Mark,
                           RegisterCount, Which + 1>(symbol, registers, here,
                                                      into);
  } else {
    constexpr std::size_t move = moves.at[Which];
    if (makes_move<Automaton, State, move>(symbol)) {
      // What a move is about to write is asked before it writes it: a list
      // takes in the turn that is ending, and what says the turn ended is the
      // registers as they stand now.
      into.template moving<State, move>(registers, here);
      execute_static_transition_commands<Automaton, State, move, TagsRead>(registers,
                                                                 here);
      // Handed over here rather than by whoever called: which move this is, is
      // a constant only while this frame is written out, and what a character
      // belongs to is a fact about the move.
      if constexpr (requires {
                      into.template moved<State, State, move>(
                          static_cast<char>(symbol), registers, here);
                    }) {
        into.template moved<State, State, move>(static_cast<char>(symbol),
                                                registers, here);
      }
      return move;
    }
    return taken_self_move<Automaton, State, TagsRead, Gatherer, Mark,
                           RegisterCount, Which + 1>(symbol, registers, here,
                                                      into);
  }
}

// Whether staying in this state writes anything.
//
// Where it does not, nothing a group is held in can change while the machine
// stays here, so whether a group is being gathered is the same for every
// character of the run and is worth asking once.
export template <auto& Automaton, std::size_t State>
[[nodiscard]] consteval bool staying_writes() {
  const auto& packed = Automaton.states[State];
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    if (packed.ranges[index].target != State) continue;
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
template <auto& Automaton>
[[nodiscard]] consteval std::size_t runs_stepped_over() {
  constexpr std::size_t state_count =
      std::tuple_size_v<std::remove_cvref_t<decltype(Automaton.states)>>;
  std::size_t count = 0;
  [&]<std::size_t... state>(std::index_sequence<state...>) {
    ((count += staying_of<Automaton, state>().count != 0 ? 1 : 0), ...);
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
export template <auto& Automaton>
[[nodiscard]] consteval std::size_t worth_reading_in_words() {
  constexpr std::size_t runs = runs_stepped_over<Automaton>();
  return 16 * (runs != 0 ? runs : 1);
}

template <auto& Automaton, std::size_t State>
[[nodiscard]] consteval std::size_t forks_of() {
  constexpr const auto& packed = Automaton.states[State];
  std::array<std::size_t, packed.ranges.size()> named{};
  std::size_t count = 0;
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    const std::size_t target = packed.ranges[index].target;
    if (target == State) continue;
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
export template <class GathererType, class PiecesType, std::size_t Hold = 0>
struct gathers_from_pieces : GathererType {
  PiecesType pieces;
  std::optional<std::ranges::iterator_t<PiecesType>> at;

  constexpr explicit gathers_from_pieces(GathererType inner,
                                         PiecesType given)
      : GathererType(std::move(inner)),
        pieces(std::forward<PiecesType>(given)) {}

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
    if constexpr (Hold != 0) {
      if (kept >= held_.data() && kept <= held_.data() + held_count_) {
        std::array<char, Hold> made{};
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
    if constexpr (Hold != 0) {
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
    if constexpr (Hold != 0) {
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
  std::array<char, Hold == 0 ? 1 : Hold> held_{};
  std::size_t held_count_ = 0;
  const char* piece_from_ = nullptr;
  const char* piece_to_ = nullptr;
  const char* rest_from_ = nullptr;
  const char* rest_to_ = nullptr;
  std::optional<const char*>* place_ = nullptr;
  std::optional<const char*>* upto_ = nullptr;
};

// Nothing gathered: what the walk hands over goes nowhere and costs nothing.
export struct gathers_nothing {
  template <std::size_t State, std::size_t Move, class RegistersType,
            class Mark>
  constexpr void moving(const RegistersType&, Mark) const {}
  template <std::size_t State, std::size_t Landed, std::size_t Move,
            class RegistersType, class Mark>
  constexpr void moved(char, const RegistersType&, Mark) const {}
  template <std::size_t State, class RegistersType>
  constexpr void ended(const RegistersType&) const {}
  // Nothing kept anywhere, said in the shape the owning walk asks for: it
  // makes the gatherer itself, and one that keeps nothing still has to be
  // makeable.
  struct cold_type {};
  constexpr gathers_nothing() = default;
  constexpr explicit gathers_nothing(cold_type&) {}
};

// A gatherer that keeps every character it is handed, which is what a match
// over a subject read once hands back.
export template <class HeldType>
struct keeps_into {
  HeldType& held;

  template <std::size_t State, std::size_t Move, class RegistersType,
            class Mark>
  constexpr void moving(const RegistersType&, Mark) const {}
  template <std::size_t State, std::size_t Landed, std::size_t Move,
            class RegistersType, class Mark>
  constexpr void moved(char letter, const RegistersType&, Mark) const {
    held.push_back(letter);
  }
  template <std::size_t State, class RegistersType>
  constexpr void ended(const RegistersType&) const {}
};

// How a walk reads, and what it answers.
//
// Every walk in this library is this walk. What used to be seven bodies is
// seven settings: characters in a row or characters as they arrive, an end to
// stop at or a terminator to stop on, an answer of yes or no or of where the
// longest match ended, tags or none, a gatherer or nobody, vectors or not, and
// how far to write the chain of states out without calling.
export struct walk_shape {
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
};

// Where the walk stopped, for the walks that answer that.
//
// Held as a maybe rather than as a place, because a reading of a subject that
// arrives as it is read is an iterator that cannot be made out of nothing and
// cannot be copied -- and such a subject is never asked where the longest head
// ended, because keeping the place would mean keeping the characters.
// Where a walk that is not looking for a head would have kept the place.
export struct nothing_kept {};

// What the walk keeps of the place it liked best.
//
// The place itself, and -- where the machine can read past a match and die
// away from one -- the registers as they stood there, with that state's final
// operations already applied. That is the backup the TDFA papers put on the
// transitions out of a fallback state, and it is kept only by the walks whose
// automaton has one: where every step out of a match lands in another match,
// the registers at the end are the registers at the note and nothing is
// copied.
export template <class CursorType, class KeptType = nothing_kept>
struct walk_answer {
  bool matched = false;
  std::optional<CursorType> at{};
  // How far the characters the place was kept in went.
  //
  // A walk that goes past a match reads on, and where the input arrives in
  // pieces it asks for the next piece while it does -- so by the time it dies
  // the end it is reading towards belongs to a different piece than the place
  // it kept. Going back to that place means going back to its end as well, or
  // the reading that follows runs from one piece to the end of another.
  std::optional<CursorType> upto{};
  [[no_unique_address]] KeptType kept{};
};

// The end that goes with the place, where the two are the same kind of thing.
//
// A walk over characters in a row reads towards a pointer; a walk over
// anything else reads towards a sentinel, which says nothing about where a
// piece ends and is not kept.
template <class AnswerType, class SentinelType>
SCAN_FORCE_INLINE constexpr void keep_the_end(AnswerType& best,
                                              const SentinelType& last) {
  if constexpr (requires { best.upto = last; }) best.upto = last;
}

// The note taken where the machine stands in a match: the registers as they
// are, with this state's final operations applied to the copy rather than to
// them. Where the answer keeps no registers this is nothing at all.
template <auto& Automaton, std::size_t State, class AnswerType,
          class CursorType, class Mark, std::size_t RegisterCount,
          class Gatherer>
SCAN_FORCE_INLINE constexpr void keep_the_place(
    AnswerType& best, const register_file<Mark, RegisterCount>& registers,
    const CursorType& cursor, Mark place, Gatherer& into) {
  // Asked of the answer and not of the assignment.
  //
  // This was `requires { best.kept = registers; }`, which is true where the
  // answer keeps the marks and false where it keeps nothing -- and false, too,
  // where it keeps them in a type the marks can no longer be assigned to. That
  // last one is not a question anybody meant to ask: a change of type turned
  // it into a walk that quietly stopped keeping the place, and a reading whose
  // fields all came back empty. Asked this way, a mismatch is a compilation
  // error, which is what it is.
  if constexpr (!std::same_as<std::remove_cvref_t<decltype(best.kept)>,
                              nothing_kept>) {
    best.kept = registers;
    // Where the machine stands, said the way this walk says it: an address
    // where the characters lie in a row, and how far along otherwise.
    if constexpr (std::is_pointer_v<Mark>) {
      execute_static_final_commands<Automaton, State>(
          best.kept, static_cast<Mark>(cursor));
    } else {
      execute_static_final_commands<Automaton, State>(best.kept, place);
    }
    // And where somebody is gathering, the value is put together here, out of
    // the gatherings as they stand now. Nothing has to be copied and nothing
    // has to be undone: what the walk pushes into the gatherings after this
    // cannot reach a value already made, and a later match makes it again.
    into.template ended<State>(best.kept);
  }
}

// The walk a constant evaluation takes, said before it is written: the
// wrapper below is what chooses between it and the threaded one.
// Said here because the walk below names it before it is written out, and a
// name with template arguments of its own is not a dependent one: it has to
// have been seen.
template <auto& Automaton, walk_shape Shape, std::size_t Entry, class Mark,
          class CursorType, class SentinelType, std::size_t RegisterCount,
          class Gatherer, class AnswerType>
[[nodiscard]] bool run_threaded(
    CursorType& __restrict cursor, SentinelType last, Mark& __restrict place,
    register_file<Mark, RegisterCount>& __restrict registers,
    Gatherer& __restrict into, AnswerType& __restrict best);

template <auto& Automaton, walk_shape Shape, std::size_t Entry, class Mark,
          class CursorType, class SentinelType, std::size_t RegisterCount,
          class Gatherer, class AnswerType>
[[nodiscard]] constexpr bool run_continuation_switch(
    CursorType& __restrict cursor, SentinelType last, Mark& __restrict place,
    register_file<Mark, RegisterCount>& __restrict registers,
    Gatherer& __restrict into, AnswerType& __restrict best);

// The walk, for whoever is not one of its own frames.
//
// Everything above passes a chain along; a caller outside has none, and what
// it wants back is whether the reading was a match.
export template <auto& Automaton, walk_shape Shape, std::size_t State,
          class Mark,
          class CursorType, class SentinelType, std::size_t RegisterCount,
          class Gatherer, class AnswerType>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool run_continuation(
    CursorType& __restrict cursor, SentinelType last, Mark& __restrict place,
    register_file<Mark, RegisterCount>& __restrict registers,
    Gatherer& __restrict into, AnswerType& __restrict best) {
  // One function a machine while the program runs, and the states written out
  // where they are reached while it is compiled.
  //
  // A label is not a thing a constant evaluation has, so the walk that reads a
  // pattern at compile time is the one above; everything else takes the other
  // one, which is the whole machine in one body.
  if consteval {
    return run_continuation_switch<Automaton, Shape, State, Mark, CursorType,
                                   SentinelType, RegisterCount, Gatherer,
                                   AnswerType>(cursor, last, place, registers,
                                               into, best);
  } else {
    return run_threaded<Automaton, Shape, State, Mark, CursorType,
                        SentinelType, RegisterCount, Gatherer, AnswerType>(
        cursor, last, place, registers, into, best);
  }
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
// What follows is the walk written as labels and jumps: a label's address
// taken, and a goto through it. The standard has no way to say that -- both
// compilers have had it for decades and neither spells it -- so the dialect is
// asked for where the library is built (`gnu++`), and the warning is put down
// here, where the extension is, rather than turned off for whoever reads these
// headers. GCC has no finer word for it than `-Wpedantic`.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-label-as-value"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

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
export template <auto& Automaton>
inline constexpr std::size_t states_in =
    std::tuple_size_v<std::remove_cvref_t<decltype(Automaton.states)>>;

// The ways out of a state, in the order they are to be tried, and where each
// of them goes.
template <auto& Automaton, std::size_t State>
[[nodiscard]] consteval std::size_t ways_out() {
  if constexpr (State < states_in<Automaton>) {
    return distinct_moves<Automaton, State>().count;
  } else {
    return 0;
  }
}
template <auto& Automaton, std::size_t State, std::size_t Which>
[[nodiscard]] consteval std::size_t move_at() {
  return distinct_moves<Automaton, State>().at[Which];
}
template <auto& Automaton, std::size_t State, std::size_t Which>
[[nodiscard]] consteval std::size_t target_at() {
  return Automaton.states[State].ranges[move_at<Automaton, State, Which>()]
      .target;
}

// What reading one character in a state came to.
enum class step_said { stopped, took };

// Everything a state does before it knows where it is going: the place it
// keeps if it accepts, the run of characters that keeps it where it is, and
// then one character -- which may keep it here again, and then the question is
// asked over. Where the reading ends here, it is ended here: what a match owes
// is owed in the state it stopped in.
template <auto& Automaton, walk_shape Shape, std::size_t State, class Mark,
          class CursorType, class SentinelType, std::size_t RegisterCount,
          class Gatherer, class AnswerType>
[[nodiscard]] SCAN_FORCE_INLINE constexpr step_said step_in_state(
    CursorType& __restrict here, SentinelType& __restrict last,
    Mark& __restrict spot,
    register_file<Mark, RegisterCount>& __restrict registers,
    Gatherer& __restrict into, AnswerType& __restrict best,
    unsigned char& symbol) {
  constexpr bool by_place = std::is_pointer_v<Mark>;
  constexpr bool gathers = !std::same_as<Gatherer, gathers_nothing>;
  constexpr bool accepts_here =
      Automaton.states[State].accepting_slot !=
      packed_state<0, 0, 0>::not_accepting;
  const auto keep_here = [&] {
    if constexpr (Shape.longest && accepts_here) {
      best.matched = true;
      best.at = here;
      keep_the_end(best, last);
      keep_the_place<Automaton, State>(best, registers, here, spot, into);
    }
  };
  keep_here();
  constexpr bool by_pointer = std::is_pointer_v<CursorType>;
  constexpr bool takes_a_piece = requires(Gatherer& one, const char* from) {
    one.template took_run<State>(from, from, registers, spot);
  };
  constexpr bool whole_run = !gathers || requires(Gatherer& one) {
    requires one.template wants_a_run_whole<State>();
  };
  // And whether anything is open to be handed it at all.
  constexpr bool anything_takes = gathers && requires(Gatherer& one) {
    requires one.template anything_takes_the_run<State>();
  };
  constexpr auto class_of_the_run =
      staying_of<Automaton, State, Shape.tags_read>();
  constexpr bool reads_the_run_itself =
      gathers && requires(Gatherer& one, const char* from) {
        {
          one.template took_class<State, class_of_the_run>(from, from)
        } -> std::same_as<const char*>;
      };
  if constexpr (by_pointer && (!gathers || takes_a_piece) &&
                (Shape.in_words || !whole_run || anything_takes) &&
                runs_in_place<Automaton, State, Shape.tags_read>()) {
    const CursorType from = here;
    if constexpr (gathers && !whole_run && reads_the_run_itself) {
      here = into.template took_class<State, class_of_the_run>(here, last);
      if constexpr (!by_place) spot += here - from;
    } else if constexpr (gathers && !whole_run) {
      while (here != last &&
             inside_of<class_of_the_run>(static_cast<unsigned char>(*here))) {
        into.template took_run<State>(here, here + 1, registers, spot);
        ++here;
      }
      if constexpr (!by_place) spot += here - from;
    } else {
      here = skip_class<class_of_the_run, Shape.in_words>(here, last);
      if constexpr (gathers && takes_a_piece) {
        into.template took_run<State>(from, here, registers, spot);
        if constexpr (!by_place) spot += here - from;
      }
    }
    keep_here();
  }
  // As in the rung above: a match with nowhere to go ends the reading
  // without reading anything to find it out.
  if constexpr (Shape.longest && accepts_here &&
                Automaton.states[State].range_count == 0) {
    best.matched = true;
    best.at = here;
    keep_the_end(best, last);
    if constexpr (by_place) {
      execute_static_final_commands<Automaton, State>(registers, here);
    } else {
      execute_static_final_commands<Automaton, State>(registers, spot);
    }
    into.template ended<State>(registers);
    return step_said::stopped;
  }
  for (;;) {
    if constexpr (!Shape.by_terminator) {
      if (here == last) {
        if constexpr (requires { into.refill(here, last); }) {
          if (!into.refill(here, last)) {
            // The reading ran out where it stands.
            if constexpr (accepts_here) {
              best.matched = true;
              if constexpr (Shape.longest) {
                best.at = here;
                keep_the_end(best, last);
              }
              if constexpr (by_place) {
                execute_static_final_commands<Automaton, State>(registers, here);
              } else {
                execute_static_final_commands<Automaton, State>(registers, spot);
              }
              into.template ended<State>(registers);
            }
            return step_said::stopped;
          }
        } else {
          if constexpr (accepts_here) {
            best.matched = true;
            if constexpr (Shape.longest) {
              best.at = here;
              keep_the_end(best, last);
            }
            if constexpr (by_place) {
              execute_static_final_commands<Automaton, State>(registers, here);
            } else {
              execute_static_final_commands<Automaton, State>(registers, spot);
            }
            into.template ended<State>(registers);
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
    if (taken_self_move<Automaton, State, Shape.tags_written, Gatherer>(
            symbol, registers, spot, into) != no_run) {
      if constexpr (Shape.longest && accepts_here) {
        best.at = here;
        keep_the_end(best, last);
        keep_the_place<Automaton, State>(best, registers, here, spot, into);
      }
      continue;
    }
    if constexpr (Shape.by_terminator) {
      if (symbol == Shape.terminator) {
        if constexpr (accepts_here) {
          execute_static_final_commands<Automaton, State>(registers, spot);
          into.template ended<State>(registers);
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
template <auto& Automaton, walk_shape Shape, std::size_t State, class Mark,
          std::size_t RegisterCount, class Gatherer, class AnswerType>
SCAN_FORCE_INLINE constexpr void end_with_no_move(
    Mark& spot, register_file<Mark, RegisterCount>& registers, Gatherer& into,
    AnswerType& best) {
  if constexpr (Shape.longest) {
    constexpr bool accepts_here =
        Automaton.states[State].accepting_slot !=
        packed_state<0, 0, 0>::not_accepting;
    if constexpr (accepts_here) {
      if constexpr (std::is_pointer_v<Mark>) {
        execute_static_final_commands<Automaton, State>(registers, spot);
      } else {
        execute_static_final_commands<Automaton, State>(registers, spot - 1);
      }
      into.template ended<State>(registers);
      best.matched = true;
    }
  }
}

// The operations of one move, run where the move is taken.
template <auto& Automaton, walk_shape Shape, std::size_t State,
          std::size_t Which, class Mark, std::size_t RegisterCount,
          class Gatherer>
SCAN_FORCE_INLINE constexpr void take_move(
    unsigned char symbol, register_file<Mark, RegisterCount>& registers,
    Mark& spot, Gatherer& into) {
  constexpr std::size_t move = move_at<Automaton, State, Which>();
  into.template moving<State, move>(registers, spot);
  execute_static_transition_commands<Automaton, State, move,
                                     Shape.tags_written>(registers, spot);
  into.template moved<State, target_at<Automaton, State, Which>(), move>(
      static_cast<char>(symbol), registers, spot);
}

// Where one state goes, or nowhere at all.
inline constexpr std::size_t no_rung = static_cast<std::size_t>(-1);

// One rung of the walk, said as a value rather than as a jump.
//
// Everything a state does is `step_in_state`, and everything a move does is
// `take_move`. What is left is choosing the move, and that is the whole of
// the difference between the two walks: the threaded one writes the choice as
// a jump to a label, because it can, and this one writes it as the number of
// the state the move lands in.
//
// They differ there and nowhere else, which is the only arrangement under
// which they cannot answer differently -- and they did answer differently for
// as long as each had a body of its own to choose a move in.
template <auto& Automaton, walk_shape Shape, std::size_t State, class Mark,
          class CursorType, class SentinelType, std::size_t RegisterCount,
          class Gatherer, class AnswerType>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::size_t rung_of(
    CursorType& __restrict here, SentinelType& __restrict last,
    Mark& __restrict spot,
    register_file<Mark, RegisterCount>& __restrict registers,
    Gatherer& __restrict into, AnswerType& __restrict best,
    unsigned char& symbol) {
  // The same question the ladder asks, and for the same reason: where the
  // walk was told a terminator, the step has already finished the match and
  // asking whether it took a character is a branch on every state.
  if (step_in_state<Automaton, Shape, State>(here, last, spot, registers, into,
                                             best, symbol) ==
          step_said::took ||
      (Shape.by_terminator && !Shape.longest)) {
    std::size_t next = no_rung;
    [&]<std::size_t... Which>(std::index_sequence<Which...>) {
      const auto way = [&]<std::size_t One>() {
        // A move that keeps the machine where it is was taken inside the
        // step, so it is not asked about again here.
        if constexpr (target_at<Automaton, State, One>() != State) {
          if (next == no_rung &&
              makes_move<Automaton, State, move_at<Automaton, State, One>()>(
                  symbol)) {
            take_move<Automaton, Shape, State, One>(symbol, registers, spot,
                                                    into);
            next = target_at<Automaton, State, One>();
          }
        }
      };
      (way.template operator()<Which>(), ...);
    }(std::make_index_sequence<ways_out<Automaton, State>()>{});
    if (next != no_rung) return next;
    end_with_no_move<Automaton, Shape, State>(spot, registers, into, best);
  }
  return no_rung;
}

// The walk, driven by the number of the state instead of by a jump to it.
//
// A label is not a thing a constant evaluation has, so this is the walk a
// pattern read while compiling takes. It is the same walk: the rung above is
// the whole of what a state does, and this says which rung runs next.
//
// Written out instead as a chain of bodies, one calling the next, it was a
// second machine: a second way of choosing a move, a budget deciding how much
// of the chain was worth writing out, and a stack that grew with the subject.
// A machine written twice is a machine that can disagree with itself, and this
// one did.
template <auto& Automaton, walk_shape Shape, std::size_t Entry, class Mark,
          class CursorType, class SentinelType, std::size_t RegisterCount,
          class Gatherer, class AnswerType>
[[nodiscard]] constexpr bool run_continuation_switch(
    CursorType& __restrict cursor, SentinelType last, Mark& __restrict place,
    register_file<Mark, RegisterCount>& __restrict registers,
    Gatherer& __restrict into, AnswerType& __restrict best) {
  // Held here where it can be held, as in the threaded walk: a reading of a
  // subject that arrives as it is read is move-only, and there the caller's
  // own cursor is the reading and every step writes it.
  constexpr bool keeps_its_own = std::copyable<CursorType>;
  std::conditional_t<keeps_its_own, CursorType, CursorType&> here = cursor;
  std::conditional_t<keeps_its_own, Mark, Mark&> spot = place;
  SentinelType last_here = last;
  unsigned char symbol = 0;
  std::size_t at = Entry;
  while (at != no_rung) {
    const std::size_t standing = at;
    at = no_rung;
    // The cases, written by a fold because how many there are is a template
    // parameter rather than a number somebody wrote down. One of them runs,
    // and the rest are a comparison the compiler folds into a table.
    [&]<std::size_t... State>(std::index_sequence<State...>) {
      const auto rung = [&]<std::size_t One>() {
        if (standing == One) {
          at = rung_of<Automaton, Shape, One>(here, last_here, spot, registers,
                                              into, best, symbol);
        }
      };
      (rung.template operator()<State>(), ...);
    }(std::make_index_sequence<states_in<Automaton>>{});
  }
  if constexpr (keeps_its_own) {
    cursor = here;
    place = spot;
  }
  return best.matched;
}

// The rung before and the rung after, as tokens.
//
// A move's target is a constant while this is compiled, and a jump to a
// constant should be a jump and not a load: an indirect branch through the
// table of rungs is one the predictor has to learn, and on a record of five
// short fields there is nothing to learn it from -- measured, thirteen of them
// a record and five missed, against none at all for the same machine written
// out by a generator.
//
// What stops it being written directly is that the label is named by a token
// and the target is a number. So the tokens are listed here once. Only the
// neighbours are: a machine spread into a row of fields moves to the next
// state, to the one after it, or back to the one before -- which is every move
// this had to make indirect, and the rest stay on the table.
#if SCAN_LADDER == 256
#define SCAN_AFTER_0x00 0x01
#define SCAN_AFTER_0x01 0x02
#define SCAN_AFTER_0x02 0x03
#define SCAN_AFTER_0x03 0x04
#define SCAN_AFTER_0x04 0x05
#define SCAN_AFTER_0x05 0x06
#define SCAN_AFTER_0x06 0x07
#define SCAN_AFTER_0x07 0x08
#define SCAN_AFTER_0x08 0x09
#define SCAN_AFTER_0x09 0x0a
#define SCAN_AFTER_0x0a 0x0b
#define SCAN_AFTER_0x0b 0x0c
#define SCAN_AFTER_0x0c 0x0d
#define SCAN_AFTER_0x0d 0x0e
#define SCAN_AFTER_0x0e 0x0f
#define SCAN_AFTER_0x0f 0x10
#define SCAN_AFTER_0x10 0x11
#define SCAN_AFTER_0x11 0x12
#define SCAN_AFTER_0x12 0x13
#define SCAN_AFTER_0x13 0x14
#define SCAN_AFTER_0x14 0x15
#define SCAN_AFTER_0x15 0x16
#define SCAN_AFTER_0x16 0x17
#define SCAN_AFTER_0x17 0x18
#define SCAN_AFTER_0x18 0x19
#define SCAN_AFTER_0x19 0x1a
#define SCAN_AFTER_0x1a 0x1b
#define SCAN_AFTER_0x1b 0x1c
#define SCAN_AFTER_0x1c 0x1d
#define SCAN_AFTER_0x1d 0x1e
#define SCAN_AFTER_0x1e 0x1f
#define SCAN_AFTER_0x1f 0x20
#define SCAN_AFTER_0x20 0x21
#define SCAN_AFTER_0x21 0x22
#define SCAN_AFTER_0x22 0x23
#define SCAN_AFTER_0x23 0x24
#define SCAN_AFTER_0x24 0x25
#define SCAN_AFTER_0x25 0x26
#define SCAN_AFTER_0x26 0x27
#define SCAN_AFTER_0x27 0x28
#define SCAN_AFTER_0x28 0x29
#define SCAN_AFTER_0x29 0x2a
#define SCAN_AFTER_0x2a 0x2b
#define SCAN_AFTER_0x2b 0x2c
#define SCAN_AFTER_0x2c 0x2d
#define SCAN_AFTER_0x2d 0x2e
#define SCAN_AFTER_0x2e 0x2f
#define SCAN_AFTER_0x2f 0x30
#define SCAN_AFTER_0x30 0x31
#define SCAN_AFTER_0x31 0x32
#define SCAN_AFTER_0x32 0x33
#define SCAN_AFTER_0x33 0x34
#define SCAN_AFTER_0x34 0x35
#define SCAN_AFTER_0x35 0x36
#define SCAN_AFTER_0x36 0x37
#define SCAN_AFTER_0x37 0x38
#define SCAN_AFTER_0x38 0x39
#define SCAN_AFTER_0x39 0x3a
#define SCAN_AFTER_0x3a 0x3b
#define SCAN_AFTER_0x3b 0x3c
#define SCAN_AFTER_0x3c 0x3d
#define SCAN_AFTER_0x3d 0x3e
#define SCAN_AFTER_0x3e 0x3f
#define SCAN_AFTER_0x3f 0x40
#define SCAN_AFTER_0x40 0x41
#define SCAN_AFTER_0x41 0x42
#define SCAN_AFTER_0x42 0x43
#define SCAN_AFTER_0x43 0x44
#define SCAN_AFTER_0x44 0x45
#define SCAN_AFTER_0x45 0x46
#define SCAN_AFTER_0x46 0x47
#define SCAN_AFTER_0x47 0x48
#define SCAN_AFTER_0x48 0x49
#define SCAN_AFTER_0x49 0x4a
#define SCAN_AFTER_0x4a 0x4b
#define SCAN_AFTER_0x4b 0x4c
#define SCAN_AFTER_0x4c 0x4d
#define SCAN_AFTER_0x4d 0x4e
#define SCAN_AFTER_0x4e 0x4f
#define SCAN_AFTER_0x4f 0x50
#define SCAN_AFTER_0x50 0x51
#define SCAN_AFTER_0x51 0x52
#define SCAN_AFTER_0x52 0x53
#define SCAN_AFTER_0x53 0x54
#define SCAN_AFTER_0x54 0x55
#define SCAN_AFTER_0x55 0x56
#define SCAN_AFTER_0x56 0x57
#define SCAN_AFTER_0x57 0x58
#define SCAN_AFTER_0x58 0x59
#define SCAN_AFTER_0x59 0x5a
#define SCAN_AFTER_0x5a 0x5b
#define SCAN_AFTER_0x5b 0x5c
#define SCAN_AFTER_0x5c 0x5d
#define SCAN_AFTER_0x5d 0x5e
#define SCAN_AFTER_0x5e 0x5f
#define SCAN_AFTER_0x5f 0x60
#define SCAN_AFTER_0x60 0x61
#define SCAN_AFTER_0x61 0x62
#define SCAN_AFTER_0x62 0x63
#define SCAN_AFTER_0x63 0x64
#define SCAN_AFTER_0x64 0x65
#define SCAN_AFTER_0x65 0x66
#define SCAN_AFTER_0x66 0x67
#define SCAN_AFTER_0x67 0x68
#define SCAN_AFTER_0x68 0x69
#define SCAN_AFTER_0x69 0x6a
#define SCAN_AFTER_0x6a 0x6b
#define SCAN_AFTER_0x6b 0x6c
#define SCAN_AFTER_0x6c 0x6d
#define SCAN_AFTER_0x6d 0x6e
#define SCAN_AFTER_0x6e 0x6f
#define SCAN_AFTER_0x6f 0x70
#define SCAN_AFTER_0x70 0x71
#define SCAN_AFTER_0x71 0x72
#define SCAN_AFTER_0x72 0x73
#define SCAN_AFTER_0x73 0x74
#define SCAN_AFTER_0x74 0x75
#define SCAN_AFTER_0x75 0x76
#define SCAN_AFTER_0x76 0x77
#define SCAN_AFTER_0x77 0x78
#define SCAN_AFTER_0x78 0x79
#define SCAN_AFTER_0x79 0x7a
#define SCAN_AFTER_0x7a 0x7b
#define SCAN_AFTER_0x7b 0x7c
#define SCAN_AFTER_0x7c 0x7d
#define SCAN_AFTER_0x7d 0x7e
#define SCAN_AFTER_0x7e 0x7f
#define SCAN_AFTER_0x7f 0x80
#define SCAN_AFTER_0x80 0x81
#define SCAN_AFTER_0x81 0x82
#define SCAN_AFTER_0x82 0x83
#define SCAN_AFTER_0x83 0x84
#define SCAN_AFTER_0x84 0x85
#define SCAN_AFTER_0x85 0x86
#define SCAN_AFTER_0x86 0x87
#define SCAN_AFTER_0x87 0x88
#define SCAN_AFTER_0x88 0x89
#define SCAN_AFTER_0x89 0x8a
#define SCAN_AFTER_0x8a 0x8b
#define SCAN_AFTER_0x8b 0x8c
#define SCAN_AFTER_0x8c 0x8d
#define SCAN_AFTER_0x8d 0x8e
#define SCAN_AFTER_0x8e 0x8f
#define SCAN_AFTER_0x8f 0x90
#define SCAN_AFTER_0x90 0x91
#define SCAN_AFTER_0x91 0x92
#define SCAN_AFTER_0x92 0x93
#define SCAN_AFTER_0x93 0x94
#define SCAN_AFTER_0x94 0x95
#define SCAN_AFTER_0x95 0x96
#define SCAN_AFTER_0x96 0x97
#define SCAN_AFTER_0x97 0x98
#define SCAN_AFTER_0x98 0x99
#define SCAN_AFTER_0x99 0x9a
#define SCAN_AFTER_0x9a 0x9b
#define SCAN_AFTER_0x9b 0x9c
#define SCAN_AFTER_0x9c 0x9d
#define SCAN_AFTER_0x9d 0x9e
#define SCAN_AFTER_0x9e 0x9f
#define SCAN_AFTER_0x9f 0xa0
#define SCAN_AFTER_0xa0 0xa1
#define SCAN_AFTER_0xa1 0xa2
#define SCAN_AFTER_0xa2 0xa3
#define SCAN_AFTER_0xa3 0xa4
#define SCAN_AFTER_0xa4 0xa5
#define SCAN_AFTER_0xa5 0xa6
#define SCAN_AFTER_0xa6 0xa7
#define SCAN_AFTER_0xa7 0xa8
#define SCAN_AFTER_0xa8 0xa9
#define SCAN_AFTER_0xa9 0xaa
#define SCAN_AFTER_0xaa 0xab
#define SCAN_AFTER_0xab 0xac
#define SCAN_AFTER_0xac 0xad
#define SCAN_AFTER_0xad 0xae
#define SCAN_AFTER_0xae 0xaf
#define SCAN_AFTER_0xaf 0xb0
#define SCAN_AFTER_0xb0 0xb1
#define SCAN_AFTER_0xb1 0xb2
#define SCAN_AFTER_0xb2 0xb3
#define SCAN_AFTER_0xb3 0xb4
#define SCAN_AFTER_0xb4 0xb5
#define SCAN_AFTER_0xb5 0xb6
#define SCAN_AFTER_0xb6 0xb7
#define SCAN_AFTER_0xb7 0xb8
#define SCAN_AFTER_0xb8 0xb9
#define SCAN_AFTER_0xb9 0xba
#define SCAN_AFTER_0xba 0xbb
#define SCAN_AFTER_0xbb 0xbc
#define SCAN_AFTER_0xbc 0xbd
#define SCAN_AFTER_0xbd 0xbe
#define SCAN_AFTER_0xbe 0xbf
#define SCAN_AFTER_0xbf 0xc0
#define SCAN_AFTER_0xc0 0xc1
#define SCAN_AFTER_0xc1 0xc2
#define SCAN_AFTER_0xc2 0xc3
#define SCAN_AFTER_0xc3 0xc4
#define SCAN_AFTER_0xc4 0xc5
#define SCAN_AFTER_0xc5 0xc6
#define SCAN_AFTER_0xc6 0xc7
#define SCAN_AFTER_0xc7 0xc8
#define SCAN_AFTER_0xc8 0xc9
#define SCAN_AFTER_0xc9 0xca
#define SCAN_AFTER_0xca 0xcb
#define SCAN_AFTER_0xcb 0xcc
#define SCAN_AFTER_0xcc 0xcd
#define SCAN_AFTER_0xcd 0xce
#define SCAN_AFTER_0xce 0xcf
#define SCAN_AFTER_0xcf 0xd0
#define SCAN_AFTER_0xd0 0xd1
#define SCAN_AFTER_0xd1 0xd2
#define SCAN_AFTER_0xd2 0xd3
#define SCAN_AFTER_0xd3 0xd4
#define SCAN_AFTER_0xd4 0xd5
#define SCAN_AFTER_0xd5 0xd6
#define SCAN_AFTER_0xd6 0xd7
#define SCAN_AFTER_0xd7 0xd8
#define SCAN_AFTER_0xd8 0xd9
#define SCAN_AFTER_0xd9 0xda
#define SCAN_AFTER_0xda 0xdb
#define SCAN_AFTER_0xdb 0xdc
#define SCAN_AFTER_0xdc 0xdd
#define SCAN_AFTER_0xdd 0xde
#define SCAN_AFTER_0xde 0xdf
#define SCAN_AFTER_0xdf 0xe0
#define SCAN_AFTER_0xe0 0xe1
#define SCAN_AFTER_0xe1 0xe2
#define SCAN_AFTER_0xe2 0xe3
#define SCAN_AFTER_0xe3 0xe4
#define SCAN_AFTER_0xe4 0xe5
#define SCAN_AFTER_0xe5 0xe6
#define SCAN_AFTER_0xe6 0xe7
#define SCAN_AFTER_0xe7 0xe8
#define SCAN_AFTER_0xe8 0xe9
#define SCAN_AFTER_0xe9 0xea
#define SCAN_AFTER_0xea 0xeb
#define SCAN_AFTER_0xeb 0xec
#define SCAN_AFTER_0xec 0xed
#define SCAN_AFTER_0xed 0xee
#define SCAN_AFTER_0xee 0xef
#define SCAN_AFTER_0xef 0xf0
#define SCAN_AFTER_0xf0 0xf1
#define SCAN_AFTER_0xf1 0xf2
#define SCAN_AFTER_0xf2 0xf3
#define SCAN_AFTER_0xf3 0xf4
#define SCAN_AFTER_0xf4 0xf5
#define SCAN_AFTER_0xf5 0xf6
#define SCAN_AFTER_0xf6 0xf7
#define SCAN_AFTER_0xf7 0xf8
#define SCAN_AFTER_0xf8 0xf9
#define SCAN_AFTER_0xf9 0xfa
#define SCAN_AFTER_0xfa 0xfb
#define SCAN_AFTER_0xfb 0xfc
#define SCAN_AFTER_0xfc 0xfd
#define SCAN_AFTER_0xfd 0xfe
#define SCAN_AFTER_0xfe 0xff
#define SCAN_BEFORE_0x01 0x00
#define SCAN_BEFORE_0x02 0x01
#define SCAN_BEFORE_0x03 0x02
#define SCAN_BEFORE_0x04 0x03
#define SCAN_BEFORE_0x05 0x04
#define SCAN_BEFORE_0x06 0x05
#define SCAN_BEFORE_0x07 0x06
#define SCAN_BEFORE_0x08 0x07
#define SCAN_BEFORE_0x09 0x08
#define SCAN_BEFORE_0x0a 0x09
#define SCAN_BEFORE_0x0b 0x0a
#define SCAN_BEFORE_0x0c 0x0b
#define SCAN_BEFORE_0x0d 0x0c
#define SCAN_BEFORE_0x0e 0x0d
#define SCAN_BEFORE_0x0f 0x0e
#define SCAN_BEFORE_0x10 0x0f
#define SCAN_BEFORE_0x11 0x10
#define SCAN_BEFORE_0x12 0x11
#define SCAN_BEFORE_0x13 0x12
#define SCAN_BEFORE_0x14 0x13
#define SCAN_BEFORE_0x15 0x14
#define SCAN_BEFORE_0x16 0x15
#define SCAN_BEFORE_0x17 0x16
#define SCAN_BEFORE_0x18 0x17
#define SCAN_BEFORE_0x19 0x18
#define SCAN_BEFORE_0x1a 0x19
#define SCAN_BEFORE_0x1b 0x1a
#define SCAN_BEFORE_0x1c 0x1b
#define SCAN_BEFORE_0x1d 0x1c
#define SCAN_BEFORE_0x1e 0x1d
#define SCAN_BEFORE_0x1f 0x1e
#define SCAN_BEFORE_0x20 0x1f
#define SCAN_BEFORE_0x21 0x20
#define SCAN_BEFORE_0x22 0x21
#define SCAN_BEFORE_0x23 0x22
#define SCAN_BEFORE_0x24 0x23
#define SCAN_BEFORE_0x25 0x24
#define SCAN_BEFORE_0x26 0x25
#define SCAN_BEFORE_0x27 0x26
#define SCAN_BEFORE_0x28 0x27
#define SCAN_BEFORE_0x29 0x28
#define SCAN_BEFORE_0x2a 0x29
#define SCAN_BEFORE_0x2b 0x2a
#define SCAN_BEFORE_0x2c 0x2b
#define SCAN_BEFORE_0x2d 0x2c
#define SCAN_BEFORE_0x2e 0x2d
#define SCAN_BEFORE_0x2f 0x2e
#define SCAN_BEFORE_0x30 0x2f
#define SCAN_BEFORE_0x31 0x30
#define SCAN_BEFORE_0x32 0x31
#define SCAN_BEFORE_0x33 0x32
#define SCAN_BEFORE_0x34 0x33
#define SCAN_BEFORE_0x35 0x34
#define SCAN_BEFORE_0x36 0x35
#define SCAN_BEFORE_0x37 0x36
#define SCAN_BEFORE_0x38 0x37
#define SCAN_BEFORE_0x39 0x38
#define SCAN_BEFORE_0x3a 0x39
#define SCAN_BEFORE_0x3b 0x3a
#define SCAN_BEFORE_0x3c 0x3b
#define SCAN_BEFORE_0x3d 0x3c
#define SCAN_BEFORE_0x3e 0x3d
#define SCAN_BEFORE_0x3f 0x3e
#define SCAN_BEFORE_0x40 0x3f
#define SCAN_BEFORE_0x41 0x40
#define SCAN_BEFORE_0x42 0x41
#define SCAN_BEFORE_0x43 0x42
#define SCAN_BEFORE_0x44 0x43
#define SCAN_BEFORE_0x45 0x44
#define SCAN_BEFORE_0x46 0x45
#define SCAN_BEFORE_0x47 0x46
#define SCAN_BEFORE_0x48 0x47
#define SCAN_BEFORE_0x49 0x48
#define SCAN_BEFORE_0x4a 0x49
#define SCAN_BEFORE_0x4b 0x4a
#define SCAN_BEFORE_0x4c 0x4b
#define SCAN_BEFORE_0x4d 0x4c
#define SCAN_BEFORE_0x4e 0x4d
#define SCAN_BEFORE_0x4f 0x4e
#define SCAN_BEFORE_0x50 0x4f
#define SCAN_BEFORE_0x51 0x50
#define SCAN_BEFORE_0x52 0x51
#define SCAN_BEFORE_0x53 0x52
#define SCAN_BEFORE_0x54 0x53
#define SCAN_BEFORE_0x55 0x54
#define SCAN_BEFORE_0x56 0x55
#define SCAN_BEFORE_0x57 0x56
#define SCAN_BEFORE_0x58 0x57
#define SCAN_BEFORE_0x59 0x58
#define SCAN_BEFORE_0x5a 0x59
#define SCAN_BEFORE_0x5b 0x5a
#define SCAN_BEFORE_0x5c 0x5b
#define SCAN_BEFORE_0x5d 0x5c
#define SCAN_BEFORE_0x5e 0x5d
#define SCAN_BEFORE_0x5f 0x5e
#define SCAN_BEFORE_0x60 0x5f
#define SCAN_BEFORE_0x61 0x60
#define SCAN_BEFORE_0x62 0x61
#define SCAN_BEFORE_0x63 0x62
#define SCAN_BEFORE_0x64 0x63
#define SCAN_BEFORE_0x65 0x64
#define SCAN_BEFORE_0x66 0x65
#define SCAN_BEFORE_0x67 0x66
#define SCAN_BEFORE_0x68 0x67
#define SCAN_BEFORE_0x69 0x68
#define SCAN_BEFORE_0x6a 0x69
#define SCAN_BEFORE_0x6b 0x6a
#define SCAN_BEFORE_0x6c 0x6b
#define SCAN_BEFORE_0x6d 0x6c
#define SCAN_BEFORE_0x6e 0x6d
#define SCAN_BEFORE_0x6f 0x6e
#define SCAN_BEFORE_0x70 0x6f
#define SCAN_BEFORE_0x71 0x70
#define SCAN_BEFORE_0x72 0x71
#define SCAN_BEFORE_0x73 0x72
#define SCAN_BEFORE_0x74 0x73
#define SCAN_BEFORE_0x75 0x74
#define SCAN_BEFORE_0x76 0x75
#define SCAN_BEFORE_0x77 0x76
#define SCAN_BEFORE_0x78 0x77
#define SCAN_BEFORE_0x79 0x78
#define SCAN_BEFORE_0x7a 0x79
#define SCAN_BEFORE_0x7b 0x7a
#define SCAN_BEFORE_0x7c 0x7b
#define SCAN_BEFORE_0x7d 0x7c
#define SCAN_BEFORE_0x7e 0x7d
#define SCAN_BEFORE_0x7f 0x7e
#define SCAN_BEFORE_0x80 0x7f
#define SCAN_BEFORE_0x81 0x80
#define SCAN_BEFORE_0x82 0x81
#define SCAN_BEFORE_0x83 0x82
#define SCAN_BEFORE_0x84 0x83
#define SCAN_BEFORE_0x85 0x84
#define SCAN_BEFORE_0x86 0x85
#define SCAN_BEFORE_0x87 0x86
#define SCAN_BEFORE_0x88 0x87
#define SCAN_BEFORE_0x89 0x88
#define SCAN_BEFORE_0x8a 0x89
#define SCAN_BEFORE_0x8b 0x8a
#define SCAN_BEFORE_0x8c 0x8b
#define SCAN_BEFORE_0x8d 0x8c
#define SCAN_BEFORE_0x8e 0x8d
#define SCAN_BEFORE_0x8f 0x8e
#define SCAN_BEFORE_0x90 0x8f
#define SCAN_BEFORE_0x91 0x90
#define SCAN_BEFORE_0x92 0x91
#define SCAN_BEFORE_0x93 0x92
#define SCAN_BEFORE_0x94 0x93
#define SCAN_BEFORE_0x95 0x94
#define SCAN_BEFORE_0x96 0x95
#define SCAN_BEFORE_0x97 0x96
#define SCAN_BEFORE_0x98 0x97
#define SCAN_BEFORE_0x99 0x98
#define SCAN_BEFORE_0x9a 0x99
#define SCAN_BEFORE_0x9b 0x9a
#define SCAN_BEFORE_0x9c 0x9b
#define SCAN_BEFORE_0x9d 0x9c
#define SCAN_BEFORE_0x9e 0x9d
#define SCAN_BEFORE_0x9f 0x9e
#define SCAN_BEFORE_0xa0 0x9f
#define SCAN_BEFORE_0xa1 0xa0
#define SCAN_BEFORE_0xa2 0xa1
#define SCAN_BEFORE_0xa3 0xa2
#define SCAN_BEFORE_0xa4 0xa3
#define SCAN_BEFORE_0xa5 0xa4
#define SCAN_BEFORE_0xa6 0xa5
#define SCAN_BEFORE_0xa7 0xa6
#define SCAN_BEFORE_0xa8 0xa7
#define SCAN_BEFORE_0xa9 0xa8
#define SCAN_BEFORE_0xaa 0xa9
#define SCAN_BEFORE_0xab 0xaa
#define SCAN_BEFORE_0xac 0xab
#define SCAN_BEFORE_0xad 0xac
#define SCAN_BEFORE_0xae 0xad
#define SCAN_BEFORE_0xaf 0xae
#define SCAN_BEFORE_0xb0 0xaf
#define SCAN_BEFORE_0xb1 0xb0
#define SCAN_BEFORE_0xb2 0xb1
#define SCAN_BEFORE_0xb3 0xb2
#define SCAN_BEFORE_0xb4 0xb3
#define SCAN_BEFORE_0xb5 0xb4
#define SCAN_BEFORE_0xb6 0xb5
#define SCAN_BEFORE_0xb7 0xb6
#define SCAN_BEFORE_0xb8 0xb7
#define SCAN_BEFORE_0xb9 0xb8
#define SCAN_BEFORE_0xba 0xb9
#define SCAN_BEFORE_0xbb 0xba
#define SCAN_BEFORE_0xbc 0xbb
#define SCAN_BEFORE_0xbd 0xbc
#define SCAN_BEFORE_0xbe 0xbd
#define SCAN_BEFORE_0xbf 0xbe
#define SCAN_BEFORE_0xc0 0xbf
#define SCAN_BEFORE_0xc1 0xc0
#define SCAN_BEFORE_0xc2 0xc1
#define SCAN_BEFORE_0xc3 0xc2
#define SCAN_BEFORE_0xc4 0xc3
#define SCAN_BEFORE_0xc5 0xc4
#define SCAN_BEFORE_0xc6 0xc5
#define SCAN_BEFORE_0xc7 0xc6
#define SCAN_BEFORE_0xc8 0xc7
#define SCAN_BEFORE_0xc9 0xc8
#define SCAN_BEFORE_0xca 0xc9
#define SCAN_BEFORE_0xcb 0xca
#define SCAN_BEFORE_0xcc 0xcb
#define SCAN_BEFORE_0xcd 0xcc
#define SCAN_BEFORE_0xce 0xcd
#define SCAN_BEFORE_0xcf 0xce
#define SCAN_BEFORE_0xd0 0xcf
#define SCAN_BEFORE_0xd1 0xd0
#define SCAN_BEFORE_0xd2 0xd1
#define SCAN_BEFORE_0xd3 0xd2
#define SCAN_BEFORE_0xd4 0xd3
#define SCAN_BEFORE_0xd5 0xd4
#define SCAN_BEFORE_0xd6 0xd5
#define SCAN_BEFORE_0xd7 0xd6
#define SCAN_BEFORE_0xd8 0xd7
#define SCAN_BEFORE_0xd9 0xd8
#define SCAN_BEFORE_0xda 0xd9
#define SCAN_BEFORE_0xdb 0xda
#define SCAN_BEFORE_0xdc 0xdb
#define SCAN_BEFORE_0xdd 0xdc
#define SCAN_BEFORE_0xde 0xdd
#define SCAN_BEFORE_0xdf 0xde
#define SCAN_BEFORE_0xe0 0xdf
#define SCAN_BEFORE_0xe1 0xe0
#define SCAN_BEFORE_0xe2 0xe1
#define SCAN_BEFORE_0xe3 0xe2
#define SCAN_BEFORE_0xe4 0xe3
#define SCAN_BEFORE_0xe5 0xe4
#define SCAN_BEFORE_0xe6 0xe5
#define SCAN_BEFORE_0xe7 0xe6
#define SCAN_BEFORE_0xe8 0xe7
#define SCAN_BEFORE_0xe9 0xe8
#define SCAN_BEFORE_0xea 0xe9
#define SCAN_BEFORE_0xeb 0xea
#define SCAN_BEFORE_0xec 0xeb
#define SCAN_BEFORE_0xed 0xec
#define SCAN_BEFORE_0xee 0xed
#define SCAN_BEFORE_0xef 0xee
#define SCAN_BEFORE_0xf0 0xef
#define SCAN_BEFORE_0xf1 0xf0
#define SCAN_BEFORE_0xf2 0xf1
#define SCAN_BEFORE_0xf3 0xf2
#define SCAN_BEFORE_0xf4 0xf3
#define SCAN_BEFORE_0xf5 0xf4
#define SCAN_BEFORE_0xf6 0xf5
#define SCAN_BEFORE_0xf7 0xf6
#define SCAN_BEFORE_0xf8 0xf7
#define SCAN_BEFORE_0xf9 0xf8
#define SCAN_BEFORE_0xfa 0xf9
#define SCAN_BEFORE_0xfb 0xfa
#define SCAN_BEFORE_0xfc 0xfb
#define SCAN_BEFORE_0xfd 0xfc
#define SCAN_BEFORE_0xfe 0xfd
#define SCAN_BEFORE_0xff 0xfe
// The ends point at themselves: the guard above never lets the jump happen,
// and a label named by a token that did not expand is a label that does not
// exist -- which a discarded branch is still parsed for.
#define SCAN_AFTER_0xff 0xff
#define SCAN_BEFORE_0x00 0x00
#define SCAN_AFTER(t) SCAN_CAT(SCAN_AFTER_, t)
#define SCAN_BEFORE(t) SCAN_CAT(SCAN_BEFORE_, t)
#define SCAN_HAS_NEIGHBOURS 1
#else
#define SCAN_HAS_NEIGHBOURS 0
#endif
// One way out of one state. A move that keeps the machine where it is has
// already been taken above, so it is not asked about again here.
#define SCAN_WAY_OUT(t, w)                                                    \
  if constexpr (w < ways_out<Automaton, t>() &&                               \
                target_at<Automaton, t, w>() != t) {                          \
    if (makes_move<Automaton, t, move_at<Automaton, t, w>()>(symbol))         \
        [[likely]] {                                                          \
      take_move<Automaton, Shape, t, w>(symbol, registers, spot, into);       \
      if constexpr (SCAN_HAS_NEIGHBOURS &&                                   \
                    target_at<Automaton, t, w>() == t + 1) {                  \
        goto SCAN_CAT(scan_at_, SCAN_AFTER(t));                               \
      } else if constexpr (SCAN_HAS_NEIGHBOURS && t + 2 < SCAN_LADDER &&      \
                           target_at<Automaton, t, w>() == t + 2) {           \
        goto SCAN_CAT(scan_at_, SCAN_AFTER(SCAN_AFTER(t)));                   \
      } else if constexpr (SCAN_HAS_NEIGHBOURS && t > 0 &&                    \
                           target_at<Automaton, t, w>() + 1 == t) {           \
        goto SCAN_CAT(scan_at_, SCAN_BEFORE(t));                              \
      } else {                                                                \
        goto* rungs[target_at<Automaton, t, w>()];                            \
      }                                                                       \
    }                                                                         \
  }

#define SCAN_RUNG_NAME(t) &&SCAN_CAT(scan_at_, t),
// Where the walk starts is known while compiling, so it is reached by a jump
// to a label and not through the table. A search runs a walk from every
// position in the subject; going in through the table would be an indirect
// branch nobody can predict, once for every character of it.
#define SCAN_RUNG_ENTRY(t)                                                    \
  if constexpr (Entry == t) goto SCAN_CAT(scan_at_, t);
#define SCAN_RUNG_BODY(t)                                                     \
  SCAN_CAT(scan_at_, t)                                                       \
      : if constexpr (t < states_in<Automaton>) {                             \
    /* Whether the step took a character is a question the comparisons below \
       already answer, where the walk was told a terminator.                 \
                                                                            \
       That terminator is a character every state rejects -- the caller says \
       so and an assert checks it -- so it makes no move out of any state and \
       arrives where a state with nothing to take arrives anyway. The step    \
       has already finished the match by then; what is skipped is only the    \
       asking, which is a branch on every state a record passes through. Ten  \
       of them on a row of five fields, and the generated scanner pays none.  \
                                                                            \
       Not where the walk is looking for the longest head: there the arrival  \
       finishes the match a second time, and once is what it is written for. */\
    if (step_in_state<Automaton, Shape, t>(here, last_here, spot, registers,   \
                                          into, best, symbol) ==            \
        step_said::took || (Shape.by_terminator && !Shape.longest)) {                        \
      SCAN_WAY_OUT(t, 0)                                                      \
      SCAN_WAY_OUT(t, 1)                                                      \
      SCAN_WAY_OUT(t, 2)                                                      \
      SCAN_WAY_OUT(t, 3)                                                      \
      SCAN_WAY_OUT(t, 4)                                                      \
      SCAN_WAY_OUT(t, 5)                                                      \
      SCAN_WAY_OUT(t, 6)                                                      \
      SCAN_WAY_OUT(t, 7)                                                      \
      end_with_no_move<Automaton, Shape, t>(spot, registers, into, best);     \
    }                                                                         \
  }                                                                           \
  goto scan_over;

// The walk itself: one function, one body a state, and every move a jump.
template <auto& Automaton, walk_shape Shape, std::size_t Entry, class Mark,
          class CursorType, class SentinelType, std::size_t RegisterCount,
          class Gatherer, class AnswerType>
[[nodiscard]] bool run_threaded(
    CursorType& __restrict cursor, SentinelType last, Mark& __restrict place,
    register_file<Mark, RegisterCount>& __restrict registers,
    Gatherer& __restrict into, AnswerType& __restrict best) {
  static_assert(states_in<Automaton> <= SCAN_LADDER,
                "this machine has more states than the ladder has rungs: "
                "build with -DSCAN_LADDER=4096");
  static void* const rungs[] = {SCAN_EVERY_RUNG(SCAN_RUNG_NAME)};
  // Where the walk is, held here rather than through the references it was
  // handed -- but only where it can be held. An iterator over a stream is
  // move-only: there is one of it, and reading through it is the reading, so
  // there the caller's own is used and every step writes it, which is what
  // such a subject costs anyway.
  static constexpr bool keeps_its_own = std::copyable<CursorType>;
  std::conditional_t<keeps_its_own, CursorType, CursorType&> here = cursor;
  std::conditional_t<keeps_its_own, Mark, Mark&> spot = place;
  SentinelType last_here = last;
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

// The same walk, owning everything it works on.
//
// A function whose labels have had their addresses taken is one no inliner
// will write into its caller, so whatever it is handed by reference stays in
// memory for as long as the walk runs: a call may look at anything it was
// given the address of. The gatherings a reading fills in are the whole of
// what it does, and they were on the stack, written back at every character.
//
// Handed nothing and told to make its own, none of it escapes: the gatherer,
// the registers and the place the walk liked best are values of this function
// and go wherever values go. What comes back is the reading, already made --
// and a reading that never accepted is a reading that was never made, which
// the gatherer says for itself.
// What the owning walk hands back, where the gatherer is not the one to ask.
//
// A reading that gathers says what it made for itself. A reading that gathers
// nothing has the marks and nothing else, and what is made of them is the
// caller's business -- so the caller says it, and it is said inside this
// frame. That is the whole point: the marks are values of this function, and a
// caller that read them from outside is a caller whose address they had to
// have.
struct taken_from_gatherer {};

template <auto& Automaton, walk_shape Shape, std::size_t Entry, class Mark,
          bool PointsAtSubject, class CursorType, class SentinelType,
          std::size_t RegisterCount, class Gatherer, class AnswerType,
          class MakeType = taken_from_gatherer,
          class CarrierType = scan::no_contexts>
[[nodiscard]] auto run_threaded_owning(CursorType cursor, SentinelType last,
                                       const char* text, Mark start,
                                       MakeType make = {},
                                       const CarrierType& told = CarrierType{}) {
  static_assert(states_in<Automaton> <= SCAN_LADDER,
                "this machine has more states than the ladder has rungs: "
                "build with -DSCAN_LADDER=4096");
  static void* const rungs[] = {SCAN_EVERY_RUNG(SCAN_RUNG_NAME)};
  register_file<Mark, RegisterCount> registers{};
  if constexpr (std::is_pointer_v<Mark>) {
    registers.fill(nullptr);
    execute_initial<Automaton>(registers, static_cast<const char*>(nullptr));
  } else {
    registers.fill(scan::tre::negative_tag);
    execute_initial<Automaton>(registers, Mark{});
  }
  typename Gatherer::cold_type collected = [&] {
    if constexpr (requires { Gatherer::cold_for(told); }) {
      return Gatherer::cold_for(told);
    } else {
      return typename Gatherer::cold_type{};
    }
  }();
  Gatherer into = [&] {
    if constexpr (requires { Gatherer{collected, told}; }) {
      return Gatherer{collected, told};
    } else {
      return Gatherer{collected};
    }
  }();
  // Only where there is something to point at, and said while compiling: a
  // reading that holds a list is handed nothing, and asking at every reading
  // whether it was is a branch on the way in for a question the type answers.
  if constexpr (PointsAtSubject) into.points_at(text);
  AnswerType best;
  CursorType here = cursor;
  Mark spot = start;
  SentinelType last_here = last;
  unsigned char symbol = 0;
  SCAN_EVERY_RUNG(SCAN_RUNG_ENTRY)
  goto scan_over;
  SCAN_EVERY_RUNG(SCAN_RUNG_BODY)
scan_over:
  if constexpr (std::same_as<MakeType, taken_from_gatherer>) {
    return into.taken();
  } else {
    return make(registers, best.matched);
  }
}

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// The owning walk, for whoever is not one of its own frames.
//
// A label is not a thing a constant evaluation has, so a pattern read while
// compiling takes the written-out walk and is handed the state it works on in
// the ordinary way -- there is no stack to keep it off. Only the walk that
// runs owns what it reads into.
export template <auto& Automaton, walk_shape Shape, std::size_t Entry,
          class Mark,
          bool PointsAtSubject, class CursorType, class SentinelType,
          std::size_t RegisterCount, class Gatherer, class AnswerType,
          class MakeType = taken_from_gatherer,
          class CarrierType = scan::no_contexts>
[[nodiscard]] constexpr auto run_owning(CursorType cursor, SentinelType last,
                                        const char* text, Mark start,
                                        MakeType make = {},
                                        const CarrierType& told =
                                            CarrierType{}) {
  if consteval {
    register_file<Mark, RegisterCount> registers{};
    if constexpr (std::is_pointer_v<Mark>) {
      registers.fill(nullptr);
      execute_initial<Automaton>(registers, static_cast<const char*>(nullptr));
    } else {
      registers.fill(scan::tre::negative_tag);
      execute_initial<Automaton>(registers, Mark{});
    }
    typename Gatherer::cold_type collected = [&] {
    if constexpr (requires { Gatherer::cold_for(told); }) {
      return Gatherer::cold_for(told);
    } else {
      return typename Gatherer::cold_type{};
    }
  }();
  Gatherer into = [&] {
    if constexpr (requires { Gatherer{collected, told}; }) {
      return Gatherer{collected, told};
    } else {
      return Gatherer{collected};
    }
  }();
    if constexpr (PointsAtSubject) into.points_at(text);
    AnswerType best;
    CursorType here = cursor;
    Mark spot = start;
    (void)run_continuation_switch<Automaton, Shape, Entry, Mark, CursorType,
                                  SentinelType, RegisterCount, Gatherer,
                                  AnswerType>(here, last, spot, registers,
                                              into, best);
    if constexpr (std::same_as<MakeType, taken_from_gatherer>) {
      return into.taken();
    } else {
      return make(registers, best.matched);
    }
  } else {
    return run_threaded_owning<Automaton, Shape, Entry, Mark,
                               PointsAtSubject, CursorType, SentinelType,
                               RegisterCount, Gatherer, AnswerType, MakeType,
                               CarrierType>(cursor, last, text, start, make,
                                             told);
  }
}

// Walking characters in a row to a terminator, gathering nothing.
export template <auto& Automaton, unsigned char Terminator, bool InWords,
          std::size_t State, std::size_t RegisterCount>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool run_to_terminator(
    const char* cursor, const char* end,
    register_file<const char*, RegisterCount>& registers) {
  gathers_nothing nothing;
  const char* place = cursor;
  walk_answer<const char*> best;
  constexpr walk_shape shape{.in_words = InWords,
                             .by_terminator = true,
                             .terminator = Terminator};
  return run_continuation<Automaton, shape, State,
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
export template <auto& Automaton>
[[nodiscard]] consteval auto barren_walks() {
  constexpr std::size_t state_count =
      std::tuple_size_v<std::remove_cvref_t<decltype(Automaton.states)>>;
  struct answer_type {
    std::array<std::size_t, state_count> longest{};
    std::array<bool, state_count> forever{};
  };
  answer_type answer;
  const auto accepts = [&](std::size_t state) {
    return Automaton.states[state].accepting_slot !=
           packed_state<0, 0, 0>::not_accepting;
  };
  const auto relax = [&](const std::array<std::size_t, state_count>& from) {
    std::array<std::size_t, state_count> next{};
    for (std::size_t state = 0; state < state_count; ++state) {
      const auto& packed = Automaton.states[state];
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
      const auto& packed = Automaton.states[state];
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
export template <auto& Automaton>
[[nodiscard]] consteval std::size_t walk_past_a_match() {
  constexpr auto walks = barren_walks<Automaton>();
  std::size_t window = 0;
  for (std::size_t state = 0; state < walks.longest.size(); ++state) {
    if (Automaton.states[state].accepting_slot ==
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
export template <auto& Automaton>
[[nodiscard]] consteval std::size_t walk_from_the_start() {
  constexpr auto walks = barren_walks<Automaton>();
  if (walks.forever[Automaton.initial]) {
    return std::numeric_limits<std::size_t>::max();
  }
  return walks.longest[Automaton.initial];
}




export template <auto& Automaton, std::size_t State, std::size_t RegisterCount>
[[nodiscard]] constexpr const char* run_head(
    const char* cursor, const char* end,
    register_file<const char*, RegisterCount>& registers) {
  gathers_nothing nothing;
  const char* place = cursor;
  walk_answer<const char*> best;
  constexpr walk_shape shape{.longest = true};
  const bool found =
      run_continuation<Automaton, shape, State, const char*>(
          cursor, end, place, registers, nothing, best);
  return found ? *best.at : nullptr;
}

// Walking characters that lie in a row, gathering nothing: the shape almost
// every caller wants, said once.
export template <auto& Automaton, bool InWords, std::size_t State,
          std::size_t RegisterCount>
[[nodiscard]] SCAN_FORCE_INLINE constexpr bool run_from_here(
    const char* cursor, const char* end,
    register_file<const char*, RegisterCount>& registers) {
  gathers_nothing nothing;
  const char* place = cursor;
  walk_answer<const char*> best;
  constexpr walk_shape shape{.in_words = InWords};
  return run_continuation<Automaton, shape, State,
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
export template <auto& Automaton, unsigned char Sentinel>
[[nodiscard]] consteval bool is_safe_tagged_sentinel() {
  for (const auto& state : Automaton.states) {
    for (std::size_t index = 0; index < state.range_count; ++index) {
      const auto& range = state.ranges[index];
      if (Sentinel >= range.first && Sentinel <= range.last) return false;
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
export template <auto& Automaton>
[[nodiscard]] consteval auto tags_always_written() {
  constexpr std::size_t tags = Automaton.tag_count;
  constexpr std::size_t count = Automaton.states.size();
  using row_type = std::array<bool, tags>;
  const auto note = [](row_type& row, const auto& commands, std::size_t total) {
    for (std::size_t index = 0; index < total; ++index) {
      const std::size_t destination = commands[index].destination;
      if (destination < tags) row[destination] = true;
    }
  };
  row_type start{};
  note(start, Automaton.initialize, Automaton.initialize.size());
  std::array<row_type, count> entry{};
  for (row_type& row : entry) row.fill(true);
  entry[Automaton.initial] = start;
  for (bool changed = true; changed;) {
    changed = false;
    std::array<row_type, count> next{};
    for (row_type& row : next) row.fill(true);
    next[Automaton.initial] = start;
    for (std::size_t state = 0; state < count; ++state) {
      const auto& packed = Automaton.states[state];
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
    const auto& packed = Automaton.states[state];
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

export [[nodiscard]] inline bool run_tagged_runtime(const scan::tre::tdfa& automaton,
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
export [[nodiscard]] inline const char* run_prefix_runtime(
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

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

}  // namespace scan::detail
