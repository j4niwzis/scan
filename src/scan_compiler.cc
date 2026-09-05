export module scan.compiler;

import std;
import tre;
import boost.pfr;
export import scan.core;
export import scan.views;

export namespace scan::detail {


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

  [[nodiscard]] constexpr tre::node parse_format() {
    tre::node result = parse_format_sequence();
    if (position_ != source_.size()) throw "invalid scan format";
    return result;
  }

  [[nodiscard]] constexpr tre::node parse_regex() {
    tre::node result = parse_alternative('\0');
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

  [[nodiscard]] constexpr tre::node parse_format_sequence() {
    if (at_end()) return tre::epsilon();
    if (peek() == '\\') {
      if (peek(1) == '\0') throw "dangling format escape";
      const char literal = peek(1);
      position_ += 2;
      return tre::cat({tre::symbol(literal), parse_format_sequence()});
    }
    if (peek() == '{') {
      tre::node capture = parse_capture();
      return tre::cat({std::move(capture), parse_format_sequence()});
    }
    const char literal = peek();
    ++position_;
    return tre::cat({tre::symbol(literal), parse_format_sequence()});
  }

  [[nodiscard]] constexpr tre::node parse_capture() {
    ++position_;
    const std::size_t capture = capture_count_++;
    tre::node body;
    if (peek() == '}' || peek() == ':') {
      if (capture >= defaults_.size()) throw "too many capture groups";
      if (peek() == ':') skip_parameters();
      std::size_t nested_count = capture_count_;
      tre_parser parser(defaults_[capture], defaults_, nested_count);
      body = parser.parse_regex();
    } else {
      body = parse_alternative('}');
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

  [[nodiscard]] static constexpr tre::node wrap_capture(
      std::size_t capture, tre::node body) {
    return tre::cat({tre::tag(static_cast<tre::tag_id>(capture * 2)),
                     std::move(body),
                     tre::tag(static_cast<tre::tag_id>(capture * 2 + 1))});
  }

  [[nodiscard]] constexpr tre::node parse_alternative(char stop) {
    tre::node left = parse_sequence(stop);
    if (peek() != '|') return left;
    ++position_;
    return tre::alt(
        {std::move(left), parse_alternative(stop)});
  }

  [[nodiscard]] constexpr tre::node parse_sequence(char stop) {
    if (at_end() || peek() == stop || peek() == '|' || peek() == ')') {
      return tre::epsilon();
    }
    tre::node head = parse_quantified();
    return tre::cat({std::move(head), parse_sequence(stop)});
  }

  [[nodiscard]] constexpr tre::node parse_quantified() {
    tre::node atom = parse_atom();
    if (peek() == '*') {
      ++position_;
      if (peek() == '+') ++position_;
      return tre::star(std::move(atom));
    }
    if (peek() == '+') {
      ++position_;
      if (peek() == '+') ++position_;
      return tre::plus(std::move(atom));
    }
    if (peek() == '?') {
      ++position_;
      return tre::optional(std::move(atom));
    }
    if (peek() == '{' && peek(1) >= '0' && peek(1) <= '9') {
      ++position_;
      const std::size_t minimum = parse_number(0);
      std::size_t maximum = minimum;
      if (peek() == ',') {
        ++position_;
        maximum = peek() == '}' ? tre::unbounded : parse_number(0);
      }
      if (peek() != '}') throw "invalid repetition";
      ++position_;
      return tre::repeat(std::move(atom), minimum, maximum);
    }
    return atom;
  }

  [[nodiscard]] constexpr std::size_t parse_number(std::size_t value) {
    if (peek() < '0' || peek() > '9') return value;
    const std::size_t next = value * 10 + static_cast<std::size_t>(peek() - '0');
    ++position_;
    return parse_number(next);
  }

  [[nodiscard]] constexpr tre::node parse_atom() {
    if (at_end()) throw "missing regular expression atom";
    if (peek() == '{') return parse_capture();
    if (peek() == '(') {
      ++position_;
      const bool noncapturing = peek() == '?' && peek(1) == ':';
      if (noncapturing) position_ += 2;
      const bool capturing = capture_parentheses_ && !noncapturing;
      const std::size_t capture =
          capturing ? capture_count_++ : std::size_t{0};
      tre::node body = parse_alternative(')');
      if (peek() != ')') throw "unterminated regular expression group";
      ++position_;
      return capturing ? wrap_capture(capture, std::move(body))
                       : std::move(body);
    }
    if (peek() == '[') return parse_character_class();
    if (peek() == '.') {
      ++position_;
      std::array<bool, 256> symbols;
      std::ranges::fill(symbols, true);
      return tre::character_class(symbols);
    }
    if (peek() == '\\') return parse_escape();
    const char symbol = peek();
    ++position_;
    return tre::symbol(symbol);
  }

  [[nodiscard]] constexpr tre::node parse_escape() {
    ++position_;
    if (at_end()) throw "dangling regular expression escape";
    const char escaped = peek();
    ++position_;
    if (escaped == 'x') return tre::symbol(parse_hex_byte());
    if (escaped == 'd') return make_range('0', '9');
    if (escaped == 's') return make_set(" \t\n\r\f\v");
    if (escaped == 'w') {
      auto symbols = range_bits('a', 'z');
      add_range(symbols, 'A', 'Z');
      add_range(symbols, '0', '9');
      symbols[static_cast<unsigned char>('_')] = true;
      return tre::character_class(symbols);
    }
    return tre::symbol(escaped);
  }

  [[nodiscard]] constexpr tre::node parse_character_class() {
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
    return tre::character_class(symbols);
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
    std::ranges::for_each(
        std::views::iota(static_cast<unsigned>(begin),
                         static_cast<unsigned>(end) + 1),
        [&](unsigned value) { symbols[value] = true; });
  }

  [[nodiscard]] static constexpr std::array<bool, 256> range_bits(char first,
                                                                 char last) {
    std::array<bool, 256> symbols{};
    add_range(symbols, first, last);
    return symbols;
  }

  [[nodiscard]] static constexpr tre::node make_range(char first, char last) {
    return tre::character_class(range_bits(first, last));
  }

  [[nodiscard]] static constexpr tre::node make_set(std::string_view set) {
    std::array<bool, 256> symbols{};
    std::ranges::for_each(set, [&](char value) {
      symbols[static_cast<unsigned char>(value)] = true;
    });
    return tre::character_class(symbols);
  }

  std::string_view source_;
  std::span<const std::string_view> defaults_;
  std::size_t& capture_count_;
  bool capture_parentheses_ = false;
  std::size_t position_ = 0;
};

template <class type, std::size_t... index>
[[nodiscard]] constexpr auto default_patterns(std::index_sequence<index...>) {
  static_assert(
      (requires { scanner_pattern<std::remove_cvref_t<decltype(
          boost::pfr::get<index>(std::declval<type&>()))>>(); } && ...),
      "scan::scanner<type> must provide pattern");
  return std::array<std::string_view, sizeof...(index)>{
      scanner_pattern<std::remove_cvref_t<decltype(
          boost::pfr::get<index>(std::declval<type&>()))>>()...};
}

template <fixed_string format, std::size_t field_count>
[[nodiscard]] consteval auto field_parameters() {
  std::array<std::string_view, field_count> result{};
  std::size_t field = 0;
  std::size_t capture_depth = 0;
  bool character_class = false;
  const auto text = format.view();
  for (std::size_t position = 0; position < text.size(); ++position) {
    if (text[position] == '\\') {
      ++position;
      continue;
    }
    if (capture_depth != 0 && text[position] == '[') character_class = true;
    if (capture_depth != 0 && text[position] == ']') character_class = false;
    if (!character_class && text[position] == '{' &&
        position + 1 < text.size() && text[position + 1] >= '0' &&
        text[position + 1] <= '9') {
      while (position < text.size() && text[position] != '}') ++position;
      if (position == text.size()) throw "unterminated repetition";
      continue;
    }
    if (!character_class && text[position] == '}') {
      if (capture_depth != 0) --capture_depth;
      continue;
    }
    if (character_class || text[position] != '{') {
      continue;
    }
    if (field == field_count) throw "too many capture groups";
    ++capture_depth;
    if (position + 1 < text.size() && text[position + 1] == ':') {
      const auto begin = position + 2;
      auto end = begin;
      while (end < text.size() && text[end] != '}') ++end;
      if (end == text.size()) throw "unterminated scanner parameters";
      result[field] = text.substr(begin, end - begin);
    }
    ++field;
  }
  if (field != field_count) throw "capture count does not match output";
  return result;
}

template <class type, std::size_t extent, std::size_t... index>
[[nodiscard]] constexpr auto parameterized_patterns(
    const std::array<std::string_view, extent>& parameters,
    std::index_sequence<index...>) {
  const auto make_pattern = []<class field_type>(
                                std::string_view field_parameters) {
    pattern_buffer<> result;
    const auto pattern = scanner_pattern<field_type>(field_parameters);
    result.append(std::string_view{pattern});
    return result;
  };
  return std::array<pattern_buffer<>, extent>{
      make_pattern.template operator()<std::remove_cvref_t<decltype(
          boost::pfr::get<index>(std::declval<type&>()))>>(
          parameters[index])...};
}

template <std::size_t extent>
[[nodiscard]] constexpr auto pattern_views(
    const std::array<pattern_buffer<>, extent>& patterns) {
  std::array<std::string_view, extent> result{};
  std::ranges::transform(patterns, result.begin(),
                         [](const auto& pattern) { return pattern.view(); });
  return result;
}

template <class type, fixed_string format>
[[nodiscard]] constexpr tre::tnfa build_tnfa() {
  constexpr std::size_t field_count = boost::pfr::tuple_size_v<type>;
  constexpr auto parameters = field_parameters<format, field_count>();
  constexpr auto pattern_storage = parameterized_patterns<type>(
      parameters, std::make_index_sequence<field_count>{});
  const auto defaults = pattern_views(pattern_storage);
  std::size_t captures = 0;
  tre_parser parser(format.view(), defaults, captures);
  tre::node expression = parser.parse_format();
  if (captures != field_count) throw "capture count does not match output";
  return tre::compile_tnfa(expression);
}

template <class type, fixed_string format>
[[nodiscard]] constexpr tre::tdfa build_tdfa() {
  return tre::optimize_tdfa(tre::compile_tdfa(build_tnfa<type, format>()),
                            false);
}

[[nodiscard]] constexpr tre::tdfa minimize_tdfa(tre::tdfa automaton) {
  if (automaton.states.empty()) return automaton;

  const auto same_commands = [](const auto& lhs, const auto& rhs) {
    return std::ranges::equal(
        lhs, rhs, {},
        [](const tre::register_command& command) {
          return std::tuple(command.destination, command.source,
                            command.values);
        },
        [](const tre::register_command& command) {
          return std::tuple(command.destination, command.source,
                            command.values);
        });
  };

  const auto targets =
      std::views::iota(std::size_t{0}, automaton.states.size()) |
      std::views::transform([&](std::size_t state) {
        return std::views::iota(std::size_t{0}, std::size_t{256}) |
               std::views::transform([&](std::size_t symbol) {
                 const auto& transitions = automaton.states[state].transitions;
                 const auto found = std::ranges::find_if(
                     transitions,
                     [&](const tre::tdfa_transition& transition) {
                       return transition.symbols[symbol];
                     });
                 return found == transitions.end() ? automaton.states.size()
                                                   : found->target;
               }) |
               views::to_array<256>;
      }) |
      std::ranges::to<std::vector>();

  std::vector<std::size_t> classes =
      automaton.states |
      std::views::transform([](const tre::tdfa_state& state) {
        return static_cast<std::size_t>(state.accepting_slot.has_value());
      }) |
      std::ranges::to<std::vector>();

  bool changed = true;
  std::size_t class_count = 0;
  while (changed) {
    std::vector<std::size_t> refined(automaton.states.size());
    class_count = 0;
    std::ranges::for_each(
        std::views::iota(std::size_t{0}, automaton.states.size()),
        [&](std::size_t state) {
          const auto candidates = std::views::iota(std::size_t{0}, state);
          const auto equivalent = std::ranges::find_if(
              candidates,
              [&](std::size_t candidate) {
                if (automaton.states[state].accepting_slot.has_value() !=
                    automaton.states[candidate].accepting_slot.has_value()) {
                  return false;
                }
                if (!same_commands(automaton.states[state].final_commands,
                                   automaton.states[candidate]
                                       .final_commands)) {
                  return false;
                }
                return std::ranges::all_of(
                    std::views::iota(std::size_t{0}, std::size_t{256}),
                    [&](std::size_t symbol) {
                      const std::size_t lhs = targets[state][symbol];
                      const std::size_t rhs = targets[candidate][symbol];
                      if (lhs == automaton.states.size() ||
                          rhs == automaton.states.size()) {
                        return lhs == rhs;
                      }
                      if (classes[lhs] != classes[rhs]) return false;
                      const auto& lhs_transitions =
                          automaton.states[state].transitions;
                      const auto& rhs_transitions =
                          automaton.states[candidate].transitions;
                      const auto lhs_transition = std::ranges::find_if(
                          lhs_transitions, [&](const auto& transition) {
                            return transition.symbols[symbol];
                          });
                      const auto rhs_transition = std::ranges::find_if(
                          rhs_transitions, [&](const auto& transition) {
                            return transition.symbols[symbol];
                          });
                      return same_commands(lhs_transition->commands,
                                           rhs_transition->commands);
                    });
              });
          if (equivalent == candidates.end()) {
            refined[state] = class_count++;
          } else {
            refined[state] = refined[*equivalent];
          }
        });
    changed = refined != classes;
    classes = std::move(refined);
  }

  tre::tdfa minimized{.initial = classes[automaton.initial],
                       .tag_count = automaton.tag_count,
                       .register_count = automaton.register_count,
                       .initialize = std::move(automaton.initialize),
                       .states = std::vector<tre::tdfa_state>(class_count)};
  std::ranges::for_each(
      std::views::iota(std::size_t{0}, class_count),
      [&](std::size_t result_class) {
        const auto representative = std::ranges::find(classes, result_class);
        const auto& source = automaton.states[representative - classes.begin()];
        auto& destination = minimized.states[result_class];
        destination.accepting_slot = source.accepting_slot;
        destination.final_commands = source.final_commands;
        std::ranges::for_each(
            source.transitions, [&](tre::tdfa_transition transition) {
              transition.target = classes[transition.target];
              const auto equivalent = std::ranges::find_if(
                  destination.transitions,
                  [&](const tre::tdfa_transition& candidate) {
                    return candidate.target == transition.target &&
                           same_commands(candidate.commands,
                                         transition.commands);
                  });
              if (equivalent == destination.transitions.end()) {
                destination.transitions.push_back(std::move(transition));
              } else {
                std::ranges::transform(
                    equivalent->symbols, transition.symbols,
                    equivalent->symbols.begin(), std::logical_or<>{});
              }
            });
      });
  return minimized;
}

template <fixed_string pattern>
[[nodiscard]] consteval tre::tdfa build_regex_tdfa() {
  std::size_t captures = 0;
  tre_parser parser(pattern.view(), {}, captures, true);
  return minimize_tdfa(tre::optimize_tdfa(
      tre::compile_tdfa(tre::compile_tnfa(parser.parse_regex()))));
}

// Which transition each symbol takes, or none. The symbol sets of a state's
// transitions do not overlap, so this is a function, and consecutive symbols
// that take the same transition are one range.
[[nodiscard]] constexpr std::array<std::size_t, 256> transition_of_symbol(
    const tre::tdfa_state& state) {
  constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
  std::array<std::size_t, 256> result{};
  std::ranges::fill(result, none);
  std::ranges::for_each(
      std::views::iota(std::size_t{0}, state.transitions.size()),
      [&](std::size_t index) {
        std::ranges::for_each(
            std::views::iota(std::size_t{0}, std::size_t{256}),
            [&](std::size_t symbol) {
              if (state.transitions[index].symbols[symbol]) {
                result[symbol] = index;
              }
            });
      });
  return result;
}

[[nodiscard]] constexpr std::size_t count_symbol_ranges(
    const tre::tdfa_state& state) {
  constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
  const std::array<std::size_t, 256> owner = transition_of_symbol(state);
  std::size_t count = 0;
  std::size_t previous = none;
  bool started = false;
  std::ranges::for_each(owner, [&](std::size_t index) {
    if (index != none && (!started || index != previous)) ++count;
    started = true;
    previous = index;
  });
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
  std::size_t registers = 0;
  std::size_t initial_commands = 0;
  std::size_t maximum_commands = 0;
  std::size_t maximum_final_commands = 0;
  std::size_t tags = 0;
};

[[nodiscard]] constexpr packed_shape compute_shape(const tre::tdfa& tdfa) {
  packed_shape shape{.states = tdfa.states.size(),
                     .registers = tdfa.register_count,
                     .initial_commands = tdfa.initialize.size(),
                     .maximum_commands = 0,
                     .maximum_final_commands = 0,
                     .tags = tdfa.tag_count};
  std::ranges::for_each(tdfa.states, [&](const tre::tdfa_state& state) {
    shape.maximum_final_commands =
        std::max(shape.maximum_final_commands, state.final_commands.size());
    std::ranges::for_each(state.transitions,
                          [&](const tre::tdfa_transition& transition) {
      shape.maximum_commands =
          std::max(shape.maximum_commands, transition.commands.size());
    });
    shape.ranges = std::max(shape.ranges, count_symbol_ranges(state));
  });
  return shape;
}

template <class type, fixed_string format>
[[nodiscard]] consteval packed_shape compute_shape() {
  const tre::tdfa tdfa = build_tdfa<type, format>();
  return compute_shape(tdfa);
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
};

template <std::size_t command_capacity, std::size_t final_command_capacity,
          std::size_t range_capacity>
struct packed_state {
  static constexpr std::size_t not_accepting =
      std::numeric_limits<std::size_t>::max();
  std::array<packed_range<command_capacity>, range_capacity> ranges{};
  std::size_t range_count = 0;
  std::size_t accepting_slot = not_accepting;
  std::size_t final_command_count = 0;
  std::array<packed_command, final_command_capacity> final_commands{};
};

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
          std::size_t range_count>
struct packed_tdfa {
  std::size_t initial = 0;
  std::array<packed_command, initial_command_count> initialize{};
  std::array<packed_state<command_count, final_command_count, range_count>,
             state_count>
      states{};
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
    const tre::register_command& command) {
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
          std::size_t range_count>
[[nodiscard]] constexpr auto pack_tdfa_value(const tre::tdfa& tdfa) {
  packed_tdfa<state_count, register_count, initial_command_count,
              command_count, final_command_count, tag_count, range_count>
      packed;
  packed.initial = tdfa.initial;
  std::ranges::transform(tdfa.initialize, packed.initialize.begin(),
                         pack_command);
  std::ranges::for_each(
      std::views::iota(std::size_t{0}, tdfa.states.size()),
      [&](std::size_t state_index) {
        const tre::tdfa_state& source = tdfa.states[state_index];
        auto& target = packed.states[state_index];
        target.accepting_slot = source.accepting_slot.value_or(
            packed_state<command_count, final_command_count,
                         range_count>::not_accepting);
        target.final_command_count = source.final_commands.size();
        std::ranges::transform(source.final_commands,
                               target.final_commands.begin(), pack_command);
        // Consecutive symbols taking the same transition become one range.
        constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
        const std::array<std::size_t, 256> owner =
            transition_of_symbol(source);
        std::size_t previous = none;
        std::ranges::for_each(
            std::views::iota(std::size_t{0}, std::size_t{256}),
            [&](std::size_t symbol) {
              const std::size_t index = owner[symbol];
              if (index == none) {
                previous = none;
                return;
              }
              if (index == previous) {
                target.ranges[target.range_count - 1].last =
                    static_cast<unsigned char>(symbol);
                return;
              }
              const tre::tdfa_transition& transition =
                  source.transitions[index];
              auto& range = target.ranges[target.range_count++];
              range.first = static_cast<unsigned char>(symbol);
              range.last = static_cast<unsigned char>(symbol);
              range.target = transition.target;
              range.command_count = transition.commands.size();
              std::ranges::transform(transition.commands,
                                     range.commands.begin(), pack_command);
              previous = index;
            });
      });
  return packed;
}

template <class type, fixed_string format>
[[nodiscard]] consteval auto pack_tdfa() {
  constexpr packed_shape shape = compute_shape<type, format>();
  return pack_tdfa_value<shape.states, shape.registers,
                         shape.initial_commands, shape.maximum_commands,
                         shape.maximum_final_commands, shape.tags,
                         shape.ranges>(build_tdfa<type, format>());
}

template <class type, fixed_string format>
inline constexpr auto packed_automaton = pack_tdfa<type, format>();

template <fixed_string pattern>
[[nodiscard]] consteval packed_shape compute_regex_shape() {
  return compute_shape(build_regex_tdfa<pattern>());
}

template <fixed_string pattern>
[[nodiscard]] consteval auto pack_regex_tdfa() {
  constexpr packed_shape shape = compute_regex_shape<pattern>();
  const tre::tdfa tdfa = build_regex_tdfa<pattern>();
  if constexpr (shape.tags == 0) {
    packed_captureless_tdfa<shape.states> packed;
    packed.initial = static_cast<typename decltype(packed)::state_type>(
        tdfa.initial);
    std::ranges::for_each(packed.transitions, [](auto& transitions) {
      std::ranges::fill(transitions, decltype(packed)::reject);
    });
    std::ranges::for_each(
        std::views::iota(std::size_t{0}, tdfa.states.size()),
        [&](std::size_t state_index) {
          const auto& source = tdfa.states[state_index];
          packed.accepting[state_index] = source.accepting_slot.has_value();
          std::ranges::for_each(
              source.transitions, [&](const tre::tdfa_transition& transition) {
                std::ranges::for_each(
                    std::views::iota(std::size_t{0}, transition.symbols.size()) |
                        std::views::filter([&](std::size_t symbol) {
                          return transition.symbols[symbol];
                        }),
                    [&](std::size_t symbol) {
                      packed.transitions[state_index][symbol] =
                          static_cast<typename decltype(packed)::state_type>(
                              transition.target);
                    });
              });
        });
    return packed;
  } else {
    return pack_tdfa_value<shape.states, shape.registers,
                           shape.initial_commands, shape.maximum_commands,
                           shape.maximum_final_commands, shape.tags,
                           shape.ranges>(tdfa);
  }
}

template <fixed_string pattern>
inline constexpr auto regex_automaton = pack_regex_tdfa<pattern>();


}  // namespace scan::detail
