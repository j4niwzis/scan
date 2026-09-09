export module scan.compiler;

import std;
import scan.tre;
export import scan.core;
export import scan.views;

export namespace scan::detail {



// The characters a backslash names. Without these a format can only say a
// newline by holding one, which means a pattern cannot be written on one line,
// and a class can only say one as a number.
[[nodiscard]] constexpr char named_character(char letter) {
  switch (letter) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'f': return '\f';
    case 'v': return '\v';
    case '0': return '\0';
    case 'a': return '\a';
    case 'b': return '\b';
    case 'e': return '\x1b';
    default: return letter;
  }
}

class tre_parser {
 public:
  constexpr tre_parser(std::string_view source,
                      std::span<const std::string_view> defaults,
                      std::size_t& capture_count,
                      bool capture_parentheses = false)
      : source_(source),
        defaults_(defaults),
        capture_count_(capture_count),
        capture_parentheses_(capture_parentheses) {}

  [[nodiscard]] constexpr scan::tre::node parse_regex() {
    scan::tre::node result = parse_alternative('\0');
    if (position_ != source_.size()) throw "invalid regular expression";
    return result;
  }

 private:
  [[nodiscard]] constexpr bool at_end() const {
    return position_ == source_.size();
  }

  [[nodiscard]] constexpr char peek(std::size_t offset = 0) const {
    return position_ + offset < source_.size() ? source_[position_ + offset]
                                               : '\0';
  }

  [[nodiscard]] constexpr scan::tre::node parse_capture(
      bool parentheses_are_groups = false) {
    ++position_;
    const std::size_t capture = capture_count_++;
    scan::tre::node body;
    if (peek() == '}' || peek() == ':') {
      if (capture >= defaults_.size()) throw "too many capture groups";
      if (peek() == ':') skip_parameters();
      std::size_t nested_count = capture_count_;
      tre_parser parser(defaults_[capture], defaults_, nested_count,
                        capture_parentheses_ || parentheses_are_groups);
      body = parser.parse_regex();
      capture_count_ = nested_count;
    } else {
      const bool before = capture_parentheses_;
      capture_parentheses_ = before || parentheses_are_groups;
      body = parse_alternative('}');
      capture_parentheses_ = before;
    }
    if (peek() != '}') throw "unterminated capture group";
    ++position_;
    return wrap_capture(capture, std::move(body));
  }

  constexpr void skip_parameters() {
    ++position_;
    while (!at_end() && peek() != '}') {
      if (peek() == '\\' && peek(1) != '\0') ++position_;
      ++position_;
    }
  }

 private:
  [[nodiscard]] static constexpr scan::tre::node wrap_capture(
      std::size_t capture, scan::tre::node body) {
    return scan::tre::cat({scan::tre::tag(static_cast<scan::tre::tag_id>(capture * 2)),
                     std::move(body),
                     scan::tre::tag(static_cast<scan::tre::tag_id>(capture * 2 + 1))});
  }

  [[nodiscard]] constexpr scan::tre::node parse_alternative(char stop) {
    scan::tre::node left = parse_sequence(stop);
    if (peek() != '|') return left;
    ++position_;
    return scan::tre::alt(
        {std::move(left), parse_alternative(stop)});
  }

  [[nodiscard]] constexpr scan::tre::node parse_sequence(char stop) {
    if (at_end() || peek() == stop || peek() == '|' || peek() == ')') {
      return scan::tre::epsilon();
    }
    scan::tre::node head = parse_quantified();
    return scan::tre::cat({std::move(head), parse_sequence(stop)});
  }

  // Whether the repetition just read prefers another turn or prefers to stop.
  //
  // A `?` after a quantifier is what says the second one. It was being left
  // where it stood and read afterwards as a question mark to match, so `a*?`
  // meant `a*` followed by the character `?` -- a pattern that means something
  // else and says nothing about it. A `+` after one is the possessive form,
  // which this does not have; it is swallowed rather than misread.
  [[nodiscard]] constexpr bool parse_greed() {
    if (peek() == '?') {
      ++position_;
      return false;
    }
    if (peek() == '+') ++position_;
    return true;
  }

  [[nodiscard]] constexpr scan::tre::node parse_quantified() {
    scan::tre::node atom = parse_atom();
    if (peek() == '*') {
      ++position_;
      return scan::tre::star(std::move(atom), parse_greed());
    }
    if (peek() == '+') {
      ++position_;
      return scan::tre::plus(std::move(atom), parse_greed());
    }
    if (peek() == '?') {
      ++position_;
      return scan::tre::optional(std::move(atom), parse_greed());
    }
    if (peek() == '{' && peek(1) >= '0' && peek(1) <= '9') {
      ++position_;
      const std::size_t minimum = parse_number(0);
      std::size_t maximum = minimum;
      if (peek() == ',') {
        ++position_;
        maximum = peek() == '}' ? scan::tre::unbounded : parse_number(0);
      }
      if (peek() != '}') throw "invalid repetition";
      ++position_;
      return scan::tre::repeat(std::move(atom), minimum, maximum,
                               parse_greed());
    }
    return atom;
  }

  [[nodiscard]] constexpr std::size_t parse_number(std::size_t value) {
    if (peek() < '0' || peek() > '9') return value;
    const std::size_t next = value * 10 + static_cast<std::size_t>(peek() - '0');
    ++position_;
    return parse_number(next);
  }

  [[nodiscard]] constexpr scan::tre::node parse_atom() {
    if (at_end()) throw "missing regular expression atom";
    if (peek() == '{') return parse_capture();
    if (peek() == '(') {
      ++position_;
      const bool noncapturing = peek() == '?' && peek(1) == ':';
      if (noncapturing) position_ += 2;
      const bool capturing = capture_parentheses_ && !noncapturing;
      const std::size_t capture =
          capturing ? capture_count_++ : std::size_t{0};
      scan::tre::node body = parse_alternative(')');
      if (peek() != ')') throw "unterminated regular expression group";
      ++position_;
      return capturing ? wrap_capture(capture, std::move(body))
                       : std::move(body);
    }
    if (peek() == '[') return parse_character_class();
    if (peek() == '.') {
      ++position_;
      std::array<bool, 256> symbols{};
      std::ranges::fill(symbols, true);
      return scan::tre::character_class(symbols);
    }
    if (peek() == '\\') return parse_escape();
    const char symbol = peek();
    ++position_;
    return scan::tre::symbol(symbol);
  }

  [[nodiscard]] constexpr scan::tre::node parse_escape() {
    ++position_;
    if (at_end()) throw "dangling regular expression escape";
    const char escaped = peek();
    ++position_;
    if (escaped == 'x') return scan::tre::symbol(parse_hex_byte());
    if (escaped == 'd') return make_range('0', '9');
    if (escaped == 's') return make_set(" \t\n\r\f\v");
    if (escaped == 'n' || escaped == 't' || escaped == 'r' || escaped == 'f' ||
        escaped == 'v' || escaped == 'a' || escaped == 'e') {
      return scan::tre::symbol(named_character(escaped));
    }
    if (escaped == 'w') {
      auto symbols = range_bits('a', 'z');
      add_range(symbols, 'A', 'Z');
      add_range(symbols, '0', '9');
      symbols[static_cast<unsigned char>('_')] = true;
      return scan::tre::character_class(symbols);
    }
    return scan::tre::symbol(escaped);
  }

  [[nodiscard]] constexpr scan::tre::node parse_character_class() {
    ++position_;
    const bool negated = peek() == '^';
    if (negated) ++position_;
    std::array<bool, 256> symbols{};
    parse_class_items(symbols);
    if (peek() != ']') throw "unterminated character class";
    ++position_;
    if (negated) {
      std::ranges::transform(symbols, symbols.begin(), std::logical_not<>{});
    }
    return scan::tre::character_class(symbols);
  }

  constexpr void parse_class_items(std::array<bool, 256>& symbols) {
    if (at_end() || peek() == ']') return;
    const char first = parse_class_character();
    if (peek() == '-' && peek(1) != ']' && peek(1) != '\0') {
      ++position_;
      const char last = parse_class_character();
      add_range(symbols, first, last);
    } else {
      symbols[static_cast<unsigned char>(first)] = true;
    }
    parse_class_items(symbols);
  }

  [[nodiscard]] constexpr char parse_class_character() {
    if (peek() == '\\') {
      ++position_;
      if (at_end()) throw "dangling character class escape";
      if (peek() == 'x') {
        ++position_;
        return parse_hex_byte();
      }
      const char value = named_character(peek());
      ++position_;
      return value;
    }
    const char value = peek();
    ++position_;
    return value;
  }

  [[nodiscard]] constexpr char parse_hex_byte() {
    const auto digit = [](char value) -> unsigned {
      if (value >= '0' && value <= '9') return value - '0';
      if (value >= 'a' && value <= 'f') return value - 'a' + 10;
      if (value >= 'A' && value <= 'F') return value - 'A' + 10;
      throw "invalid hexadecimal escape";
    };
    if (position_ + 2 > source_.size()) throw "short hexadecimal escape";
    const unsigned value = digit(peek()) * 16 + digit(peek(1));
    position_ += 2;
    return static_cast<char>(value);
  }

  static constexpr void add_range(std::array<bool, 256>& symbols, char first,
                                 char last) {
    const auto begin = static_cast<unsigned char>(first);
    const auto end = static_cast<unsigned char>(last);
    if (begin > end) throw "reversed character class range";
    for (unsigned value : std::views::iota(static_cast<unsigned>(begin),
                         static_cast<unsigned>(end) + 1)) { symbols[value] = true; }
  }

  [[nodiscard]] static constexpr std::array<bool, 256> range_bits(char first,
                                                                 char last) {
    std::array<bool, 256> symbols{};
    add_range(symbols, first, last);
    return symbols;
  }

  [[nodiscard]] static constexpr scan::tre::node make_range(char first, char last) {
    return scan::tre::character_class(range_bits(first, last));
  }

  [[nodiscard]] static constexpr scan::tre::node make_set(std::string_view set) {
    std::array<bool, 256> symbols{};
    for (char value : set) {
      symbols[static_cast<unsigned char>(value)] = true;
    }
    return scan::tre::character_class(symbols);
  }

  std::string_view source_;
  std::span<const std::string_view> defaults_;
  std::size_t& capture_count_;
  bool capture_parentheses_ = false;
  std::size_t position_ = 0;
};


// The automaton is built from the spread format, in which every place has
// already become the pattern it stands for, every declared format has been read
// against its own type, and every variant has become branches with a mark at
// the head of each. What is left is one format whose groups are the values, in
// order.

// Register allocation is on here, and it has to be: without it the register
// file keeps one slot for every register determinisation ever handed out --
// sixty-three of them for five fields -- and the scan begins by filling all of
// them. With it the file is as wide as the tags, because the first slots are
// pinned to the tags the fields are read from and the rest are coalesced away.
// Except for the machine that reads a range as it comes. That one keeps a set
// of half-read fields for every way the tags could yet turn out, and it tells
// those sets apart by dividing a register number by the number of tags -- which
// is only a meaning at all while the registers are numbered as determinisation
// handed them out. Allocation renumbers them and merges the ones that never
// overlap, and the division stops meaning anything. So that machine is built
// from the same pattern without allocation, and pays a wider register file for
// it, which costs it nothing: it never fills the file, it walks it.

// Minimisation as Moore's refinement, with the two things that make it cheap:
// symbols that behave alike everywhere are one class, and a command sequence
// is compared as the number it was interned to rather than by copying it.
[[nodiscard]] constexpr bool same_command_list(
    const std::vector<scan::tre::register_command>& lhs,
    const std::vector<scan::tre::register_command>& rhs) {
  if (lhs.size() != rhs.size()) return false;
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (lhs[index].destination != rhs[index].destination) return false;
    if (lhs[index].source != rhs[index].source) return false;
    if (lhs[index].values != rhs[index].values) return false;
  }
  return true;
}

[[nodiscard]] constexpr scan::tre::tdfa minimize_tdfa(scan::tre::tdfa automaton) {
  if (automaton.states.empty()) return automaton;
  const std::size_t count = automaton.states.size();
  const std::size_t none = std::numeric_limits<std::size_t>::max();

  // Which transition each symbol takes, per state, once.
  std::vector<std::array<std::size_t, 256>> owner(count);
  for (std::size_t state = 0; state < count; ++state) {
    owner[state].fill(none);
    const auto& transitions = automaton.states[state].transitions;
    for (std::size_t index = 0; index < transitions.size(); ++index) {
      for (std::size_t symbol = 0; symbol < 256; ++symbol) {
        if (transitions[index].symbols.test(symbol)) owner[state][symbol] = index;
      }
    }
  }

  // Interned command sequences: equal sequences share a number, so the
  // refinement compares numbers.
  std::vector<std::vector<scan::tre::register_command>> pool;
  const auto intern = [&](const std::vector<scan::tre::register_command>& commands) {
    for (std::size_t index = 0; index < pool.size(); ++index) {
      if (same_command_list(pool[index], commands)) return index;
    }
    pool.push_back(commands);
    return pool.size() - 1;
  };
  std::vector<std::uint32_t> final_command_id(count);
  std::vector<std::vector<std::uint32_t>> command_id(count);
  for (std::size_t state = 0; state < count; ++state) {
    final_command_id[state] = intern(automaton.states[state].final_commands);
    command_id[state].resize(automaton.states[state].transitions.size());
    for (std::size_t index = 0; index < command_id[state].size(); ++index) {
      command_id[state][index] =
          intern(automaton.states[state].transitions[index].commands);
    }
  }

  // Symbols that take the same transition in every state, and carry the same
  // commands, are one class: the refinement then walks classes, not bytes.
  constexpr std::uint32_t no_class = std::numeric_limits<std::uint32_t>::max();
  std::vector<std::uint32_t> symbol_class(256, no_class);
  std::vector<std::uint32_t> representatives;
  for (std::size_t symbol = 0; symbol < 256; ++symbol) {
    for (std::size_t index = 0; index < representatives.size(); ++index) {
      const std::size_t other = representatives[index];
      bool alike = true;
      for (std::size_t state = 0; state < count && alike; ++state) {
        const std::size_t lhs = owner[state][symbol];
        const std::size_t rhs = owner[state][other];
        if (lhs == none || rhs == none) {
          alike = lhs == rhs;
        } else {
          alike = automaton.states[state].transitions[lhs].target ==
                      automaton.states[state].transitions[rhs].target &&
                  command_id[state][lhs] == command_id[state][rhs];
        }
      }
      if (alike) { symbol_class[symbol] = index; break; }
    }
    if (symbol_class[symbol] == no_class) {
      symbol_class[symbol] = representatives.size();
      representatives.push_back(symbol);
    }
  }
  const std::size_t class_width = representatives.size();

  // Moore: refine until the partition stops changing. A state's signature is
  // its own class and, per symbol class, the class it goes to with which
  // commands.
  std::vector<std::uint32_t> classes(count);
  for (std::size_t state = 0; state < count; ++state) {
    classes[state] = automaton.states[state].accepting_slot.has_value()
                         ? final_command_id[state] + 1
                         : 0;
  }
  std::size_t class_count = 0;
  for (;;) {
    std::vector<std::vector<std::uint32_t>> signature(count);
    for (std::size_t state = 0; state < count; ++state) {
      // The marker for "no transition" inside a signature is the widest value
      // of what a signature holds, not of what an index is elsewhere.
      constexpr std::uint32_t no_target = std::numeric_limits<std::uint32_t>::max();
      signature[state].reserve(1 + 2 * class_width);
      signature[state].push_back(classes[state]);
      // Readings are part of what a state is: merging two that hold their tags
      // in different registers would make the answer to "which group is open"
      // depend on which of them the merge happened to keep.
      signature[state].push_back(automaton.states[state].readings.size());
      for (const std::vector<std::uint32_t>& reading :
           automaton.states[state].readings) {
        for (const std::uint32_t held : reading) {
          signature[state].push_back(held);
        }
      }
      for (std::size_t index = 0; index < class_width; ++index) {
        const std::size_t symbol = representatives[index];
        const std::size_t transition = owner[state][symbol];
        if (transition == none) {
          signature[state].push_back(no_target);
          signature[state].push_back(no_target);
        } else {
          signature[state].push_back(
              classes[automaton.states[state].transitions[transition].target]);
          signature[state].push_back(command_id[state][transition]);
        }
      }
    }
    // Number the classes by the first state that has them, which is the
    // numbering the pairwise version produced.
    std::vector<std::uint32_t> order(count);
    for (std::size_t state = 0; state < count; ++state) order[state] = state;
    std::ranges::sort(order, [&](std::size_t lhs, std::size_t rhs) {
      if (signature[lhs] != signature[rhs]) return signature[lhs] < signature[rhs];
      return lhs < rhs;
    });
    std::vector<std::uint32_t> group(count, no_class);
    std::vector<std::uint32_t> first_state;
    for (std::size_t index = 0; index < count; ++index) {
      const std::size_t state = order[index];
      if (index == 0 || signature[state] != signature[order[index - 1]]) {
        first_state.push_back(state);
      }
      group[state] = first_state.size() - 1;
    }
    std::vector<std::uint32_t> group_order(first_state.size());
    for (std::size_t index = 0; index < first_state.size(); ++index) {
      group_order[index] = index;
    }
    std::ranges::sort(group_order, [&](std::size_t lhs, std::size_t rhs) {
      return first_state[lhs] < first_state[rhs];
    });
    std::vector<std::uint32_t> renumber(first_state.size());
    for (std::size_t index = 0; index < group_order.size(); ++index) {
      renumber[group_order[index]] = index;
    }
    std::vector<std::uint32_t> refined(count);
    for (std::size_t state = 0; state < count; ++state) {
      refined[state] = renumber[group[state]];
    }
    class_count = first_state.size();
    if (refined == classes) break;
    classes = std::move(refined);
  }

  // Everything the automaton is, and not only the parts a walk over pointers
  // happened to read: which tag each register holds is as much a part of it as
  // the transitions, and leaving it behind made every register of every
  // minimised pattern say it held tag nought.
  scan::tre::tdfa minimized{.initial = classes[automaton.initial],
                      .tag_count = automaton.tag_count,
                      .register_count = automaton.register_count,
                      .initialize = std::move(automaton.initialize),
                      .states = std::vector<scan::tre::tdfa_state>(class_count),
                      .register_tag = std::move(automaton.register_tag)};
  for (std::size_t result_class = 0; result_class < class_count; ++result_class) {
    const auto representative = std::ranges::find(classes, result_class);
    const auto& source =
        automaton.states[static_cast<std::size_t>(representative - classes.begin())];
    auto& destination = minimized.states[result_class];
    destination.accepting_slot = source.accepting_slot;
    destination.final_commands = source.final_commands;
    destination.nfa_states = source.nfa_states;
    // Which register holds which tag, in each reading this state stands in.
    // Carried over, and told apart below: a machine that is fed a character at
    // a time asks this to know which group is open, and two states that agree
    // about everything else can disagree about that.
    destination.readings = source.readings;
    for (const scan::tre::tdfa_transition& transition : source.transitions) {
      destination.transitions.push_back(
          scan::tre::tdfa_transition{.symbols = transition.symbols,
                               .target = classes[transition.target],
                               .commands = transition.commands});
    }
  }
  return minimized;
}

// The pattern says which rule it is built for, so nothing here has to be told
// twice: everything keyed by the pattern -- the automaton and every table of
// states, runs and classes that names it -- follows the same value.
template <fixed_string pattern>
[[nodiscard]] consteval scan::tre::tdfa build_regex_tdfa() {
  std::size_t captures = 0;
  tre_parser parser(pattern.view(), {}, captures, true);
  return minimize_tdfa(scan::tre::optimize_tdfa(scan::tre::compile_tdfa(
      scan::tre::compile_tnfa(parser.parse_regex()), !pattern.anchored)));
}

// Which transition each symbol takes, or none. The symbol sets of a state's
// transitions do not overlap, so this is a function, and consecutive symbols
// that take the same transition are one range.
[[nodiscard]] constexpr std::array<std::size_t, 256> transition_of_symbol(
    const scan::tre::tdfa_state& state) {
  constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
  std::array<std::size_t, 256> result{};
  std::ranges::fill(result, none);
  for (std::size_t index : std::views::iota(std::size_t{0}, state.transitions.size())) {
        for (std::size_t symbol : std::views::iota(std::size_t{0}, std::size_t{256})) {
              if (state.transitions[index].symbols.test(symbol)) {
                result[symbol] = index;
              }
            }
      }
  return result;
}

[[nodiscard]] constexpr std::size_t count_symbol_ranges(
    const scan::tre::tdfa_state& state) {
  constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
  const std::array<std::size_t, 256> owner = transition_of_symbol(state);
  std::size_t count = 0;
  std::size_t previous = none;
  bool started = false;
  for (std::size_t index : owner) {
    if (index != none && (!started || index != previous)) ++count;
    started = true;
    previous = index;
  }
  return count;
}

struct packed_shape {
  std::size_t states = 0;
  // The most ranges of consecutive symbols any one state needs. A state's
  // transitions are stored as those ranges rather than as a cell per symbol:
  // the automaton for an address pattern has fifteen states and ninety-nine
  // ranges, where a cell per symbol is three thousand eight hundred and forty
  // of them, each carrying a command array. Every one of those cells is an
  // object the constant evaluator materialises and every consteval helper
  // walks all of them, so the shape of the table is most of what compiling a
  // pattern costs.
  std::size_t ranges = 0;
  // The most readings any one state stands in at once. A machine that follows a
  // reading rather than taking positions out at the end needs to be told which
  // registers make each of them up.
  std::size_t readings = 0;
  std::size_t registers = 0;
  std::size_t initial_commands = 0;
  std::size_t maximum_commands = 0;
  std::size_t maximum_final_commands = 0;
  std::size_t tags = 0;
};

[[nodiscard]] constexpr packed_shape compute_shape(const scan::tre::tdfa& tdfa) {
  packed_shape shape{.states = tdfa.states.size(),
                     .registers = tdfa.register_count,
                     .initial_commands = tdfa.initialize.size(),
                     .maximum_commands = 0,
                     .maximum_final_commands = 0,
                     .tags = tdfa.tag_count};
  for (const scan::tre::tdfa_state& state : tdfa.states) {
    shape.maximum_final_commands =
        std::max(shape.maximum_final_commands, state.final_commands.size());
    for (const scan::tre::tdfa_transition& transition : state.transitions) {
      shape.maximum_commands =
          std::max(shape.maximum_commands, transition.commands.size());
    }
    shape.ranges = std::max(shape.ranges, count_symbol_ranges(state));
    shape.readings = std::max(shape.readings, state.readings.size());
  }
  return shape;
}


struct packed_command {
  static constexpr std::size_t no_source =
      std::numeric_limits<std::size_t>::max();
  std::size_t destination = 0;
  std::size_t source = no_source;
  // -2 copies the source, -1 writes a negative tag, 0 writes current position.
  std::int8_t value = -2;
};

// One transition, over the run of symbols that take it. A dispatch compares
// the symbol against `first` and `last`, which is what the generated code
// wanted from a cell-per-symbol table anyway -- it recovered these ranges from
// it, once per instantiation, having paid to build the table first.
template <std::size_t command_capacity>
struct packed_range {
  static constexpr std::size_t reject =
      std::numeric_limits<std::size_t>::max();
  unsigned char first = 0;
  unsigned char last = 0;
  std::size_t target = reject;
  std::size_t command_count = 0;
  std::array<packed_command, command_capacity> commands{};
  // Which groups the character this move reads lies inside, and whether every
  // position that could read it agrees. Both are facts about the move, so a
  // walk written out as code knows them where it stands: what a character
  // belongs to costs nothing to find out.
  std::uint64_t groups_open = 0;
  bool groups_known = false;
};

template <std::size_t command_capacity, std::size_t final_command_capacity,
          std::size_t range_capacity, std::size_t tag_capacity = 0,
          std::size_t reading_capacity = 0>
struct packed_state {
  static constexpr std::size_t not_accepting =
      std::numeric_limits<std::size_t>::max();
  std::array<packed_range<command_capacity>, range_capacity> ranges{};
  std::size_t range_count = 0;
  std::size_t accepting_slot = not_accepting;
  std::size_t final_command_count = 0;
  std::array<packed_command, final_command_capacity> final_commands{};
  // Which register holds which tag, in each reading this state stands in.
  // Indexed the same way the accepting slot is.
  std::size_t reading_count = 0;
  std::array<std::array<std::uint32_t, tag_capacity>, reading_capacity>
      readings{};
};


// The same question asked of constants instead of searched for.
//
// `find_range` walks a state's runs while the program runs, comparing against
// numbers it has to load. Where the automaton is known while compiling -- and
// it always is, even for a machine whose state is a value -- the runs are
// constants and the walk is a handful of compares the compiler lays out
// itself. The state is still a value, so it is asked once, and from there the
// runs of that state are constants.
inline constexpr std::size_t no_run = std::numeric_limits<std::size_t>::max();

template <auto& automaton, std::size_t state>
[[nodiscard]] constexpr std::size_t run_taken_in(unsigned char symbol) {
  constexpr const auto& packed = automaton.states[state];
  for (std::size_t index = 0; index < packed.range_count; ++index) {
    if (symbol < packed.ranges[index].first) break;
    if (symbol <= packed.ranges[index].last) return index;
  }
  return no_run;
}

// Which run each symbol takes from each state, as a table.
//
// A machine whose state is a value has to ask about the state itself, and
// asking by comparing against every state in turn is a walk down the states on
// every character. A table is one load: the state indexes the row, the symbol
// indexes the cell. This is the table a generated scanner uses when it is told
// to be a table, and it is only built for the machines that need it -- the
// walks over characters in a row never ask.
template <auto& automaton>
inline constexpr auto step_table = [] consteval {
  constexpr std::size_t state_count =
      std::tuple_size_v<std::remove_cvref_t<decltype(automaton.states)>>;
  // A run index fits in two bytes: a state holds at most as many runs as there
  // are symbols.
  std::array<std::array<std::uint16_t, 256>, state_count> made{};
  for (std::size_t state = 0; state < state_count; ++state) {
    std::ranges::fill(made[state], std::uint16_t{0xffff});
    const auto& packed = automaton.states[state];
    for (std::size_t index = 0; index < packed.range_count; ++index) {
      for (std::size_t symbol = packed.ranges[index].first;
           symbol <= packed.ranges[index].last; ++symbol) {
        made[state][symbol] = static_cast<std::uint16_t>(index);
      }
    }
  }
  return made;
}();

template <auto& automaton>
[[nodiscard]] constexpr std::size_t run_taken(std::size_t here,
                                              unsigned char symbol) {
  const std::uint16_t run = step_table<automaton>[here][symbol];
  return run == 0xffff ? no_run : run;
}

// The transition a symbol takes, or nothing at all. The ranges of a state are
// in symbol order and do not overlap, so the search stops at the first range
// that starts past the symbol.
template <class packed_state_type>
[[nodiscard]] constexpr auto find_range(const packed_state_type& state,
                                        unsigned char symbol)
    -> const std::remove_cvref_t<decltype(state.ranges[0])>* {
  for (std::size_t index = 0; index < state.range_count; ++index) {
    const auto& range = state.ranges[index];
    if (symbol < range.first) break;
    if (symbol <= range.last) return &range;
  }
  return nullptr;
}

template <std::size_t state_count, std::size_t register_extent,
          std::size_t initial_command_count, std::size_t command_count,
          std::size_t final_command_count, std::size_t tag_extent,
          std::size_t range_count, std::size_t reading_count = 0>
struct packed_tdfa {
  std::size_t initial = 0;
  std::array<packed_command, initial_command_count> initialize{};
  std::array<packed_state<command_count, final_command_count, range_count,
                          tag_extent, reading_count>,
             state_count>
      states{};
  // Which tag each register holds, said rather than worked out from the number.
  std::array<std::uint32_t, register_extent> register_tag{};
  static constexpr std::size_t register_count = register_extent;
  static constexpr std::size_t tag_count = tag_extent;
};

template <std::size_t state_count>
using packed_state_index = std::conditional_t<
    (state_count < std::numeric_limits<std::uint8_t>::max()), std::uint8_t,
    std::conditional_t<
        (state_count < std::numeric_limits<std::uint16_t>::max()),
        std::uint16_t,
        std::conditional_t<
            (state_count < std::numeric_limits<std::uint32_t>::max()),
            std::uint32_t, std::size_t>>>;

template <std::size_t state_count>
struct packed_captureless_tdfa {
  using state_type = packed_state_index<state_count>;
  static constexpr state_type reject =
      std::numeric_limits<state_type>::max();
  static constexpr std::size_t register_count = 0;
  static constexpr std::size_t tag_count = 0;

  state_type initial = 0;
  std::array<std::array<state_type, 256>, state_count> transitions{};
  std::array<bool, state_count> accepting{};
};

[[nodiscard]] constexpr packed_command pack_command(
    const scan::tre::register_command& command) {
  packed_command packed{
      .destination = command.destination,
      .source = command.source.value_or(packed_command::no_source),
      .value = -2};
  if (!command.values.empty()) {
    packed.value = command.values.back() ? 0 : -1;
  }
  return packed;
}

template <std::size_t state_count, std::size_t register_count,
          std::size_t initial_command_count, std::size_t command_count,
          std::size_t final_command_count, std::size_t tag_count,
          std::size_t range_count, std::size_t reading_count = 0>
[[nodiscard]] constexpr auto pack_tdfa_value(const scan::tre::tdfa& tdfa) {
  packed_tdfa<state_count, register_count, initial_command_count,
              command_count, final_command_count, tag_count, range_count,
              reading_count>
      packed;
  packed.initial = tdfa.initial;
  for (std::size_t reg :
       std::views::iota(std::size_t{0}, tdfa.register_tag.size())) {
    packed.register_tag[reg] = tdfa.register_tag[reg];
  }
  std::ranges::transform(tdfa.initialize, packed.initialize.begin(),
                         pack_command);
  for (std::size_t state_index : std::views::iota(std::size_t{0}, tdfa.states.size())) {
        const scan::tre::tdfa_state& source = tdfa.states[state_index];
        auto& target = packed.states[state_index];
        target.accepting_slot = source.accepting_slot.value_or(
            packed_state<command_count, final_command_count, range_count,
                         tag_count, reading_count>::not_accepting);
        target.reading_count = source.readings.size();
        for (std::size_t reading :
             std::views::iota(std::size_t{0}, source.readings.size())) {
          for (std::size_t tag :
               std::views::iota(std::size_t{0}, source.readings[reading].size())) {
            target.readings[reading][tag] = source.readings[reading][tag];
          }
        }
        target.final_command_count = source.final_commands.size();
        std::ranges::transform(source.final_commands,
                               target.final_commands.begin(), pack_command);
        // Consecutive symbols taking the same transition become one range.
        constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
        const std::array<std::size_t, 256> owner =
            transition_of_symbol(source);
        std::size_t previous = none;
        for (std::size_t symbol : std::views::iota(std::size_t{0}, std::size_t{256})) {
              const std::size_t index = owner[symbol];
              if (index == none) {
                previous = none;
                continue;
              }
              if (index == previous) {
                target.ranges[target.range_count - 1].last =
                    static_cast<unsigned char>(symbol);
                continue;
              }
              const scan::tre::tdfa_transition& transition =
                  source.transitions[index];
              auto& range = target.ranges[target.range_count++];
              range.first = static_cast<unsigned char>(symbol);
              range.last = static_cast<unsigned char>(symbol);
              range.target = transition.target;
              range.groups_open = transition.groups_open;
              range.groups_known = transition.groups_known;
              range.command_count = transition.commands.size();
              std::ranges::transform(transition.commands,
                                     range.commands.begin(), pack_command);
              previous = index;
            }
      }
  return packed;
}


// Whether an automaton is built while the program runs rather than while it is
// compiled.
//
// The compiled form is the point of this library: a pattern becomes code, and
// the code costs nothing to run. It is paid for while compiling -- one
// constant evaluation of the whole determiniser per pattern, and one
// instantiation per state of the machine that walks it. Building a test suite
// is the one job where that trade is the wrong way round, so it can be turned
// around: the same determiniser, called as an ordinary function on first use,
// and an interpreter over what it returns.
#if defined(SCAN_AUTOMATA_AT_RUNTIME) && SCAN_AUTOMATA_AT_RUNTIME
inline constexpr bool automata_at_runtime = true;
#else
inline constexpr bool automata_at_runtime = false;
#endif

// Two policies, and a pattern pays for the second only where it is read both
// ways. The reading that ends at a match cuts the walks below it; the one
// anchored to the end of the input keeps them, because one of them may be the
// only walk that reaches the end.
// The same machine, keyed by the text of a pattern rather than by a type and a
// format.
//
// What a format is spread into is a pattern, and what is built from it is
// built from that pattern and nothing else -- so the layer that turns formats
// into patterns can ask for a machine here without this one ever hearing what
// a format is. Two spellings that spread to the same text are one machine.
template <fixed_string pattern>
[[nodiscard]] constexpr scan::tre::tnfa build_text_tnfa() {
  std::size_t captures = 0;
  tre_parser parser(pattern.view(), {}, captures, true);
  return scan::tre::compile_tnfa(parser.parse_regex());
}

template <fixed_string pattern, bool allocate = true, bool cut_at_match = true>
[[nodiscard]] constexpr scan::tre::tdfa build_text_tdfa() {
  std::size_t captures = 0;
  tre_parser parser(pattern.view(), {}, captures, true);
  scan::tre::node expression = parser.parse_regex();
  return scan::tre::optimize_tdfa(
      scan::tre::compile_tdfa(scan::tre::compile_tnfa(expression), cut_at_match),
      allocate);
}

template <fixed_string pattern, bool allocate = true, bool cut = true>
[[nodiscard]] consteval packed_shape compute_text_shape() {
  const scan::tre::tdfa tdfa = build_text_tdfa<pattern, allocate, cut>();
  return compute_shape(tdfa);
}

template <fixed_string pattern, bool allocate = true, bool cut = true>
[[nodiscard]] consteval auto pack_text_tdfa() {
  constexpr packed_shape shape = compute_text_shape<pattern, allocate, cut>();
  return pack_tdfa_value<shape.states, shape.registers, shape.initial_commands,
                         shape.maximum_commands, shape.maximum_final_commands,
                         shape.tags, shape.ranges, shape.readings>(
      build_text_tdfa<pattern, allocate, cut>());
}

// Built once, on first use. The determiniser is the same one the compiled form
// evaluates while compiling; asked at run time it answers in microseconds.
template <fixed_string pattern, bool allocate = true>
[[nodiscard]] inline const scan::tre::tdfa& runtime_text_automaton() {
  static const scan::tre::tdfa built = build_text_tdfa<pattern, allocate>();
  return built;
}

template <fixed_string pattern, bool allocate = true, bool cut = true>
inline constexpr auto packed_text_automaton =
    pack_text_tdfa<pattern, allocate, cut>();

// Built once, on first use. The determiniser is the same one the compiled form
// evaluates while compiling; asked at run time it answers in microseconds.

template <fixed_string pattern>
[[nodiscard]] consteval packed_shape compute_regex_shape() {
  return compute_shape(build_regex_tdfa<pattern>());
}

template <fixed_string pattern>
[[nodiscard]] consteval auto pack_regex_tdfa() {
  // One shape for every pattern, tags or none.
  //
  // A pattern without tags used to be packed as a cell for every symbol of
  // every state -- two hundred and fifty-six of them a state, recovered back
  // into runs by whoever walked it, once per instantiation. The runs are what
  // both walks want and what the tagged shape already holds, so both are that
  // shape now: fewer numbers to carry through the module, and one set of
  // questions to ask of either.
  constexpr packed_shape shape = compute_regex_shape<pattern>();
  const scan::tre::tdfa tdfa = build_regex_tdfa<pattern>();
  return pack_tdfa_value<shape.states, shape.registers,
                         shape.initial_commands, shape.maximum_commands,
                         shape.maximum_final_commands, shape.tags,
                         shape.ranges, shape.readings>(tdfa);
}

template <fixed_string pattern>
inline constexpr auto regex_automaton = pack_regex_tdfa<pattern>();


}  // namespace scan::detail
