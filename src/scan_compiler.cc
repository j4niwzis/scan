export module scan.compiler;

import std;
import scan.tre;
import boost.pfr;
export import scan.core;
export import scan.views;

export namespace scan::detail {


// Three characters the spread writes and this parser reads, and which no format
// anyone writes can contain. A format has no grouping of its own and no
// alternation except at the top, and a variant standing in a field needs both:
// its branches have to be held together and each one has to be marked, so that
// which branch ran can be read off afterwards. Rather than give the written
// language a syntax for that, the spread says it in a spelling of its own.
inline constexpr char format_group_begin = '\x01';
inline constexpr char format_group_end = '\x02';
inline constexpr char format_branch = '\x03';
inline constexpr char format_mark = '\x04';
inline constexpr char format_raw_begin = '\x05';
inline constexpr char format_raw_end = '\x06';

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

  [[nodiscard]] constexpr scan::tre::node parse_format() {
    std::vector<std::size_t> groups;
    std::vector<scan::tre::node> branches = parse_format_branches(groups);
    if (position_ != source_.size()) throw "invalid scan format";
    if (branches.size() == 1) return std::move(branches.front());
    return scan::tre::alt(std::move(branches));
  }

  // The branches of a format, in order, with how many groups each one holds.
  //
  // A bar at the top level of a format separates one whole shape of input from
  // another, the way it separates one rule of a lexer from the next. Inside a
  // group it has always meant alternation; outside one it used to be an
  // ordinary character, and `\\|` is that character now.
  [[nodiscard]] constexpr std::vector<scan::tre::node> parse_format_branches(
      std::vector<std::size_t>& groups_in_branch) {
    std::vector<scan::tre::node> branches;
    while (true) {
      const std::size_t before = capture_count_;
      branches.push_back(parse_format_sequence());
      groups_in_branch.push_back(capture_count_ - before);
      if (at_end()) break;
      if (peek() != '|') throw "invalid scan format";
      ++position_;
    }
    return branches;
  }

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

  // A group of branches, held together, as the spread writes it.
  [[nodiscard]] constexpr scan::tre::node parse_format_group() {
    ++position_;
    std::vector<scan::tre::node> branches;
    while (true) {
      branches.push_back(parse_format_sequence());
      if (peek() != format_branch) break;
      ++position_;
    }
    if (peek() != format_group_end) throw "unterminated group of branches";
    ++position_;
    if (branches.size() == 1) return std::move(branches.front());
    return scan::tre::alt(std::move(branches));
  }

  [[nodiscard]] constexpr scan::tre::node parse_format_sequence() {
    if (at_end() || peek() == '|' || peek() == format_group_end ||
        peek() == format_branch) {
      return scan::tre::epsilon();
    }
    if (peek() == format_group_begin) {
      scan::tre::node group = parse_format_group();
      return scan::tre::cat({std::move(group), parse_format_sequence()});
    }
    // A group that captures nothing and stands at the head of a branch. What it
    // captured is never read; that it captured at all is how the branch is
    // known to have run, and it is the only way to know for a branch whose
    // alternative captures nothing of its own.
    if (peek() == format_mark) {
      ++position_;
      const std::size_t capture = capture_count_++;
      return scan::tre::cat(
          {wrap_capture(capture, scan::tre::epsilon()), parse_format_sequence()});
    }
    // A pattern that has to be matched and is not kept: the run of spaces
    // between two fields, the field somebody else's format has and this output
    // does not want. It captures nothing, so it is no group and no value, and
    // the places on either side of it go on counting as if it were not there.
    if (peek() == format_raw_begin) {
      ++position_;
      scan::tre::node body = parse_alternative(format_raw_end);
      if (peek() != format_raw_end) throw "unterminated unkept pattern";
      ++position_;
      return scan::tre::cat({std::move(body), parse_format_sequence()});
    }
    if (peek() == '\\') {
      if (peek(1) == '\0') throw "dangling format escape";
      const char literal = named_character(peek(1));
      position_ += 2;
      return scan::tre::cat({scan::tre::symbol(literal), parse_format_sequence()});
    }
    if (peek() == '{') {
      scan::tre::node capture = parse_capture();
      return scan::tre::cat({std::move(capture), parse_format_sequence()});
    }
    const char literal = peek();
    ++position_;
    return scan::tre::cat({scan::tre::symbol(literal), parse_format_sequence()});
  }

  [[nodiscard]] constexpr scan::tre::node parse_capture() {
    ++position_;
    const std::size_t capture = capture_count_++;
    scan::tre::node body;
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

 public:
  [[nodiscard]] static constexpr scan::tre::node wrap_branch(
      std::size_t capture, scan::tre::node body) {
    return wrap_capture(capture, std::move(body));
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

  [[nodiscard]] constexpr scan::tre::node parse_quantified() {
    scan::tre::node atom = parse_atom();
    if (peek() == '*') {
      ++position_;
      if (peek() == '+') ++position_;
      return scan::tre::star(std::move(atom));
    }
    if (peek() == '+') {
      ++position_;
      if (peek() == '+') ++position_;
      return scan::tre::plus(std::move(atom));
    }
    if (peek() == '?') {
      ++position_;
      return scan::tre::optional(std::move(atom));
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
      return scan::tre::repeat(std::move(atom), minimum, maximum);
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

// A type is a leaf when something knows how to read it out of text, and a
// product when it does not and is an aggregate. A leaf takes one group; a
// product takes as many as its fields take between them, in order, and its
// fields may be products themselves. Nothing about that needs saying in the
// format: a structure of structures is written out flat, because that is what
// it is.
// The question is whether a scanner has been written for this type, and it has
// to be asked of the class and not of the call: naming `scanner_parse<type>` is
// well formed for any type at all, because its declaration says nothing about
// the body. Only asking for the size of `scanner<type>` makes the compiler
// decide whether the specialisation is there.
// A type may say how it is read as a format of its own, and then it is not a
// leaf but a shape: the places in its format stand for its own fields, and its
// groups are groups of whatever it is written into.
template <class type>
inline constexpr bool scanned_as_variant = false;
template <class... alternatives>
inline constexpr bool scanned_as_variant<std::variant<alternatives...>> = true;

template <class type>
concept scanned_by_format = requires {
  scan::scanner<std::remove_cv_t<type>>::scan_format;
};

template <class type>
concept scanned_as_leaf = requires {
  sizeof(scan::scanner<std::remove_cv_t<type>>);
} && !scanned_by_format<type>;

// A type that says how it is read and also how it is made.
//
//   template <> struct scan::scanner<point> : scan::aggregate_scanner<"({}, {})"> {
//     static constexpr point parse(int x, int y) { return point(x, y); }
//   };
//
// The places of its format then stand for the arguments of that call rather
// than for the fields of the type, and the type is built by making the call. So
// it need not be an aggregate at all: it may have invariants to keep, members
// nobody outside may touch, or an order of its own that has nothing to do with
// the order it is written in.
//
// Both halves are required. A scanner with a `parse` and no format is an
// ordinary leaf and reads itself from the text of one place; the format is what
// says the places are the arguments.
template <class type>
concept scanned_from_values = scanned_by_format<type> && requires {
  &scan::scanner<std::remove_cv_t<type>>::parse;
};

template <class function_type>
struct call_parameters;
template <class result_type, class... argument_types>
struct call_parameters<result_type (*)(argument_types...)> {
  static constexpr std::size_t count = sizeof...(argument_types);
  template <std::size_t index>
  using at = std::remove_cvref_t<
      std::tuple_element_t<index, std::tuple<argument_types...>>>;
};

// What a type is made of, for the purpose of reading it: the arguments of the
// call that makes it, where there is one, and its fields otherwise.
template <class type, bool = scanned_from_values<type>>
struct parts_of;
template <class type>
struct parts_of<type, false> {
  static constexpr std::size_t count = boost::pfr::tuple_size_v<type>;
  template <std::size_t index>
  using at = std::remove_cvref_t<boost::pfr::tuple_element_t<index, type>>;
};
template <class type>
struct parts_of<type, true> {
  using call = call_parameters<
      decltype(&scan::scanner<std::remove_cv_t<type>>::parse)>;
  static constexpr std::size_t count = call::count;
  template <std::size_t index>
  using at = typename call::template at<index>;
};

// What one place in a format stands for. A leaf takes one; a type with a format
// of its own takes one and spends it on the format it declared; anything else
// is opened up and its fields take places of their own, which is why a
// structure of structures can be written out flat.
template <class type>
[[nodiscard]] consteval std::size_t places_of() {
  if constexpr (scanned_as_leaf<type> || scanned_by_format<type> ||
                scanned_as_variant<type>) {
    return 1;
  } else {
    return []<std::size_t... index>(std::index_sequence<index...>) {
      return (std::size_t{0} + ... +
              places_of<typename parts_of<type>::template at<index>>());
    }(std::make_index_sequence<parts_of<type>::count>{});
  }
}

template <class type>
[[nodiscard]] consteval std::size_t groups_of() {
  if constexpr (scanned_as_leaf<type>) {
    return 1;
  } else if constexpr (scanned_as_variant<type>) {
    // A mark for each branch, and then whatever that branch's alternative
    // reads, in the order the branches are written.
    return []<std::size_t... which>(std::index_sequence<which...>) {
      return (std::size_t{0} + ... +
              (1 + groups_of<std::variant_alternative_t<which, type>>()));
    }(std::make_index_sequence<std::variant_size_v<type>>{});
  } else {
    return []<std::size_t... index>(std::index_sequence<index...>) {
      return (std::size_t{0} + ... +
              groups_of<typename parts_of<type>::template at<index>>());
    }(std::make_index_sequence<parts_of<type>::count>{});
  }
}

template <class type, std::size_t field>
[[nodiscard]] consteval std::size_t groups_before_field() {
  return []<std::size_t... index>(std::index_sequence<index...>) {
    return (std::size_t{0} + ... +
            groups_of<typename parts_of<type>::template at<index>>());
  }(std::make_index_sequence<field>{});
}

// Which field of a product holds the group at this position, and where in that
// field it falls.
// Which field of a product holds the place at this position, and where in that
// field it falls. The same walk as for groups, counting places.
template <class subject>
[[nodiscard]] consteval std::pair<std::size_t, std::size_t> field_of_place(
    std::size_t index) {
  constexpr auto counts = []<std::size_t... field>(
                              std::index_sequence<field...>) {
    return std::array<std::size_t, sizeof...(field)>{
        places_of<typename parts_of<subject>::template at<field>>()...};
  }(std::make_index_sequence<parts_of<subject>::count>{});
  for (std::size_t field = 0; field < counts.size(); ++field) {
    if (index < counts[field]) return {field, index};
    index -= counts[field];
  }
  throw "format has more places than the output type has values";
}

template <class subject, std::size_t index,
          bool = scanned_as_leaf<subject> || scanned_by_format<subject> ||
                 scanned_as_variant<subject>>
struct place_at;
template <class subject, std::size_t index>
struct place_at<subject, index, true> {
  using kind = subject;
};
template <class subject, std::size_t index>
struct place_at<subject, index, false> {
  static constexpr auto where = field_of_place<subject>(index);
  using next = typename parts_of<subject>::template at<where.first>;
  using kind = typename place_at<next, where.second>::kind;
};

template <class subject, std::size_t index>
using place_kind = typename place_at<subject, index>::kind;

// The places of a type's own format are its fields, not itself. A type that
// declares a format is one place where it is used and a product of its fields
// where that format is read, and the difference is the whole reason the reading
// terminates.
template <class subject>
[[nodiscard]] consteval std::size_t places_within() {
  return []<std::size_t... field>(std::index_sequence<field...>) {
    return (std::size_t{0} + ... +
            places_of<typename parts_of<subject>::template at<field>>());
  }(std::make_index_sequence<parts_of<subject>::count>{});
}

template <class subject, std::size_t index>
using place_within_kind = typename place_at<subject, index, false>::kind;

// Choosing between the two by a conditional would ask for both, and asking a
// variant how many places its fields make is asking a variant for fields. Only
// the one taken may be named.
template <class subject, bool within>
[[nodiscard]] consteval std::size_t places_chosen() {
  if constexpr (within) {
    return places_within<subject>();
  } else {
    return places_of<subject>();
  }
}

template <class subject, bool within, std::size_t index>
struct place_chosen {
  static constexpr bool stands_alone =
      within ? false
             : (scanned_as_leaf<subject> || scanned_by_format<subject> ||
                scanned_as_variant<subject>);
  using kind = typename place_at<subject, index, stands_alone>::kind;
};

template <class subject>
[[nodiscard]] consteval std::pair<std::size_t, std::size_t> field_holding(
    std::size_t index) {
  constexpr auto counts = []<std::size_t... field>(
                              std::index_sequence<field...>) {
    return std::array<std::size_t, sizeof...(field)>{
        groups_of<typename parts_of<subject>::template at<field>>()...};
  }(std::make_index_sequence<parts_of<subject>::count>{});
  for (std::size_t field = 0; field < counts.size(); ++field) {
    if (index < counts[field]) return {field, index};
    index -= counts[field];
  }
  throw "group index past the end of the output type";
}

template <class subject, std::size_t index, bool = scanned_as_leaf<subject>>
struct leaf_at;
template <class subject, std::size_t index>
struct leaf_at<subject, index, true> {
  using kind = subject;
};
template <class subject, std::size_t index>
struct leaf_at<subject, index, false> {
  static constexpr auto where = field_holding<subject>(index);
  using next = typename parts_of<subject>::template at<where.first>;
  using kind = typename leaf_at<next, where.second>::kind;
};

template <class subject, std::size_t index>
using leaf_kind = typename leaf_at<subject, index>::kind;

// Reading a format against the type it is scanned into, and writing out the one
// the automaton is built from.
//
// A place standing for a leaf becomes that leaf's own pattern, with whatever
// was written after the colon handed to it and kept for the reading afterwards.
// A place standing for a type that declared a format becomes that format, read
// against that type -- so the places inside it mean that type's fields and
// nothing about where it was used. That is the whole of the hygiene: a format
// is only ever read against the type it belongs to.
struct spread_format {
  pattern_buffer<2048> text{};
  pattern_buffer<64> parameters[32]{};
  std::size_t leaves = 0;
};

constexpr void copy_until_place(spread_format& made, std::string_view text,
                                std::size_t& position) {
  while (position < text.size()) {
    if (text[position] == '\\' && position + 1 < text.size()) {
      made.text.push_back(text[position]);
      made.text.push_back(text[position + 1]);
      position += 2;
      continue;
    }
    if (text[position] == '{') return;
    made.text.push_back(text[position]);
    ++position;
  }
}

// The body of a place, and where it ends. A brace inside a character class or a
// repetition is not the end of one.
[[nodiscard]] constexpr std::size_t end_of_place(std::string_view text,
                                                 std::size_t open) {
  std::size_t position = open + 1;
  bool character_class = false;
  while (position < text.size()) {
    const char symbol = text[position];
    if (symbol == '\\' && position + 1 < text.size()) {
      position += 2;
      continue;
    }
    if (symbol == '[') character_class = true;
    if (symbol == ']') character_class = false;
    if (!character_class && symbol == '{') {
      position = end_of_place(text, position) + 1;
      continue;
    }
    if (!character_class && symbol == '}') return position;
    ++position;
  }
  throw "unterminated placeholder";
}

// Read outside a type, one place is the whole of it; read inside its own
// format, one place is one of its fields. The same walk, told which it is.
// Where the top level of a format has branches, each is read against the
// alternative standing in the same place, and each one's places mean that
// alternative's values.
[[nodiscard]] constexpr std::array<std::pair<std::size_t, std::size_t>, 16>
branches_of(std::string_view text, std::size_t& count) {
  std::array<std::pair<std::size_t, std::size_t>, 16> found{};
  std::size_t begin = 0;
  std::size_t position = 0;
  count = 0;
  while (position < text.size()) {
    if (text[position] == '\\' && position + 1 < text.size()) {
      position += 2;
      continue;
    }
    if (text[position] == '{') {
      position = end_of_place(text, position) + 1;
      continue;
    }
    if (text[position] == '|') {
      if (count == found.size()) throw "too many branches in a format";
      found[count++] = {begin, position};
      begin = position + 1;
    }
    ++position;
  }
  if (count == found.size()) throw "too many branches in a format";
  found[count++] = {begin, text.size()};
  return found;
}

// Literal text, and any place that keeps nothing, up to the next place that
// does. A place whose body begins with a star is matched and thrown away, the
// way `%*d` is read and not stored, and it takes no value with it.
constexpr void copy_until_kept_place(spread_format& made, std::string_view text,
                                     std::size_t& position) {
  while (true) {
    copy_until_place(made, text, position);
    if (position == text.size()) return;
    const std::size_t close = end_of_place(text, position);
    const std::string_view body =
        text.substr(position + 1, close - position - 1);
    if (body.empty() || body.front() != '*') return;
    made.text.push_back(format_raw_begin);
    made.text.append(body.substr(1));
    made.text.push_back(format_raw_end);
    position = close + 1;
  }
}

template <class type, bool within>
constexpr void spread_into(spread_format& made, std::string_view text);

template <class kind>
constexpr void spread_place(spread_format& made, std::string_view body) {
  if constexpr (scanned_as_variant<kind>) {
    // The branches, held together, each headed by a mark. Written out, the body
    // of the place says them, one per alternative, separated by a bar. Left
    // empty, each alternative is asked how it reads itself -- which it can
    // answer if it declares a format or if something knows how to read it.
    constexpr std::size_t count = std::variant_size_v<kind>;
    std::size_t written = 0;
    std::array<std::pair<std::size_t, std::size_t>, 16> parts{};
    if (!body.empty()) {
      parts = branches_of(body, written);
      if (written != count) {
        throw "a variant place must have one branch for each alternative";
      }
    }
    made.text.push_back(format_group_begin);
    [&]<std::size_t... which>(std::index_sequence<which...>) {
      const auto one = [&]<std::size_t branch>() {
        if constexpr (branch != 0) made.text.push_back(format_branch);
        made.text.push_back(format_mark);
        using alternative = std::variant_alternative_t<branch, kind>;
        if (body.empty()) {
          spread_place<alternative>(made, std::string_view{});
        } else {
          spread_into<alternative, false>(
              made, body.substr(parts[branch].first,
                                parts[branch].second - parts[branch].first));
        }
      };
      (one.template operator()<which>(), ...);
    }(std::make_index_sequence<count>{});
    made.text.push_back(format_group_end);
  } else if constexpr (scanned_by_format<kind>) {
    if (!body.empty()) throw "a type that declares a format takes no body";
    spread_into<kind, true>(
        made, scan::scanner<std::remove_cv_t<kind>>::scan_format.view());
  } else if constexpr (!scanned_as_leaf<kind>) {
    // Only reached by a variant place left empty, which asks each alternative
    // how it reads itself. This one does not say.
    throw "an alternative of a variant place left empty must say how it reads "
          "itself -- give it a scanner or a format of its own, or write the "
          "branches out with a bar between them";
  } else {
    made.text.push_back('{');
    if (body.empty() || body.front() == ':') {
      const std::string_view given = body.empty() ? body : body.substr(1);
      made.parameters[made.leaves].append(given);
      const auto pattern = scanner_pattern<std::remove_cv_t<kind>>(given);
      made.text.append(std::string_view{pattern});
    } else {
      made.text.append(body);
    }
    made.text.push_back('}');
    ++made.leaves;
  }
}

template <class type, bool within>
constexpr void spread_into(spread_format& made, std::string_view text) {
  std::size_t position = 0;
  [&]<std::size_t... place>(std::index_sequence<place...>) {
    const auto one = [&]<std::size_t which>() {
      copy_until_kept_place(made, text, position);
      if (position == text.size()) throw "format has fewer places than values";
      const std::size_t close = end_of_place(text, position);
      using kind = typename place_chosen<type, within, which>::kind;
      spread_place<kind>(made,
                         text.substr(position + 1, close - position - 1));
      position = close + 1;
    };
    (one.template operator()<place>(), ...);
  }(std::make_index_sequence<places_chosen<type, within>()>{});
  copy_until_kept_place(made, text, position);
  if (position != text.size()) throw "format has more places than values";
}

template <class type, fixed_string format>
[[nodiscard]] consteval spread_format spread_of() {
  spread_format made;
  if constexpr (scanned_as_variant<type>) {
    // The whole format is the list of branches, which is what a place standing
    // for a variant is written as anywhere else.
    spread_place<type>(made, format.view());
  } else {
    // The type scanned into is always opened up: its fields are the places, and
    // it is never itself one. A format of a single place standing for the whole
    // output would read differently the day that type gained a scanner or lost
    // one, without a word changing in the format, so it is not allowed to mean
    // anything. Whoever wants it writes the wrapper themselves, and then the
    // place is the field and says so.
    spread_into<type, true>(made, format.view());
  }
  return made;
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
  // By field, and not by the values a field opens up into: this builds the
  // pattern a whole aggregate matches for the paths that match it whole -- the
  // streaming one -- and there a field that is itself a shape contributes its
  // own pattern, recursively, rather than being spread out here.
  return std::array<pattern_buffer<>, extent>{
      make_pattern.template operator()<std::remove_cvref_t<
          boost::pfr::tuple_element_t<index, type>>>(parameters[index])...};
}

template <std::size_t extent>
[[nodiscard]] constexpr auto pattern_views(
    const std::array<pattern_buffer<>, extent>& patterns) {
  std::array<std::string_view, extent> result{};
  std::ranges::transform(patterns, result.begin(),
                         [](const auto& pattern) { return pattern.view(); });
  return result;
}

// The automaton is built from the spread format, in which every place has
// already become the pattern it stands for, every declared format has been read
// against its own type, and every variant has become branches with a mark at
// the head of each. What is left is one format whose groups are the values, in
// order.
template <class type, fixed_string format>
[[nodiscard]] constexpr scan::tre::tnfa build_tnfa() {
  constexpr auto spread = spread_of<type, format>();
  std::size_t captures = 0;
  tre_parser parser(spread.text.view(), {}, captures);
  scan::tre::node expression = parser.parse_format();
  if (captures != groups_of<type>()) {
    throw "capture count does not match output";
  }
  return scan::tre::compile_tnfa(expression);
}

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
template <class type, fixed_string format, bool allocate = true>
[[nodiscard]] constexpr scan::tre::tdfa build_tdfa() {
  return scan::tre::optimize_tdfa(
      scan::tre::compile_tdfa(build_tnfa<type, format>()), allocate);
}

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

  scan::tre::tdfa minimized{.initial = classes[automaton.initial],
                      .tag_count = automaton.tag_count,
                      .register_count = automaton.register_count,
                      .initialize = std::move(automaton.initialize),
                      .states = std::vector<scan::tre::tdfa_state>(class_count)};
  for (std::size_t result_class = 0; result_class < class_count; ++result_class) {
    const auto representative = std::ranges::find(classes, result_class);
    const auto& source =
        automaton.states[static_cast<std::size_t>(representative - classes.begin())];
    auto& destination = minimized.states[result_class];
    destination.accepting_slot = source.accepting_slot;
    destination.final_commands = source.final_commands;
    destination.nfa_states = source.nfa_states;
    for (const scan::tre::tdfa_transition& transition : source.transitions) {
      destination.transitions.push_back(
          scan::tre::tdfa_transition{.symbols = transition.symbols,
                               .target = classes[transition.target],
                               .commands = transition.commands});
    }
  }
  return minimized;
}

template <fixed_string pattern>
[[nodiscard]] consteval scan::tre::tdfa build_regex_tdfa() {
  std::size_t captures = 0;
  tre_parser parser(pattern.view(), {}, captures, true);
  return minimize_tdfa(scan::tre::optimize_tdfa(
      scan::tre::compile_tdfa(scan::tre::compile_tnfa(parser.parse_regex()))));
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

template <class type, fixed_string format, bool allocate = true>
[[nodiscard]] consteval packed_shape compute_shape() {
  const scan::tre::tdfa tdfa = build_tdfa<type, format, allocate>();
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
              range.command_count = transition.commands.size();
              std::ranges::transform(transition.commands,
                                     range.commands.begin(), pack_command);
              previous = index;
            }
      }
  return packed;
}

template <class type, fixed_string format, bool allocate = true>
[[nodiscard]] consteval auto pack_tdfa() {
  constexpr packed_shape shape = compute_shape<type, format, allocate>();
  return pack_tdfa_value<shape.states, shape.registers,
                         shape.initial_commands, shape.maximum_commands,
                         shape.maximum_final_commands, shape.tags, shape.ranges,
                         shape.readings>(build_tdfa<type, format, allocate>());
}

template <class type, fixed_string format>
inline constexpr auto packed_automaton = pack_tdfa<type, format>();

template <class type, fixed_string format>
inline constexpr auto streaming_automaton = pack_tdfa<type, format, false>();

template <fixed_string pattern>
[[nodiscard]] consteval packed_shape compute_regex_shape() {
  return compute_shape(build_regex_tdfa<pattern>());
}

template <fixed_string pattern>
[[nodiscard]] consteval auto pack_regex_tdfa() {
  constexpr packed_shape shape = compute_regex_shape<pattern>();
  const scan::tre::tdfa tdfa = build_regex_tdfa<pattern>();
  if constexpr (shape.tags == 0) {
    packed_captureless_tdfa<shape.states> packed;
    packed.initial = static_cast<typename decltype(packed)::state_type>(
        tdfa.initial);
    for (auto& transitions : packed.transitions) {
      std::ranges::fill(transitions, decltype(packed)::reject);
    }
    for (std::size_t state_index : std::views::iota(std::size_t{0}, tdfa.states.size())) {
          const auto& source = tdfa.states[state_index];
          packed.accepting[state_index] = source.accepting_slot.has_value();
          for (const scan::tre::tdfa_transition& transition : source.transitions) {
            for (std::size_t symbol = 0; symbol < 256; ++symbol) {
              if (!transition.symbols.test(symbol)) continue;
              packed.transitions[state_index][symbol] =
                  static_cast<typename decltype(packed)::state_type>(
                      transition.target);
            }
              }
        }
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
