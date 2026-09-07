export module scan.compiler;

import std;
import scan.tre;
import boost.pfr;
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
// Whether a type is one of several: whatever `scan::branches` was told about,
// which is `std::variant` and anything else somebody wrote a `branches` for.
template <class type>
concept scanned_as_variant = requires {
  scan::branches<std::remove_cv_t<type>>::count;
};

// How many alternatives, and which type the k-th is, asked of whatever says
// it.
template <class type>
[[nodiscard]] consteval std::size_t branch_count() {
  return scan::branches<std::remove_cv_t<type>>::count;
}

template <class type, std::size_t which>
using branch_at =
    typename scan::branches<std::remove_cv_t<type>>::template at<which>;

template <class type>
concept says_a_format = requires {
  scan::scanner<std::remove_cv_t<type>>::scan_format;
};

// A type read by spreading the format it declares into the automaton around
// it. A type that also says how to build itself out of its own groups is read
// that way instead: the groups are the same groups, and reading them is the
// user's own code rather than this library's.
// A type read by spreading the format it declares into the automaton around
// it. A type that says it reads its own groups is read that way instead: the
// groups are the same groups, and reading them is the user's own code rather
// than this library's.
//
// Asked as a plain question and not by whether a hook is there. What such a
// hook hands back is a list of everything the reading can fail with, and
// working that list out means asking how this type is read -- which is what is
// being decided here.
template <class type>
concept scanned_by_format = says_a_format<type> && !requires {
  { scan::scanner<std::remove_cv_t<type>>{}.reads_its_groups() } -> std::same_as<bool>;
  requires scan::scanner<std::remove_cv_t<type>>{}.reads_its_groups();
};

// A type that says outright it is a list, though it could be read as one
// value.
//
// `std::string` is a range and has a scanner, and reading it as a value is
// what everybody wants -- so a type that is both is a value. Where that is the
// wrong way round, the type says so:
//
//   template <> struct scan::scanner<my_bytes> { static constexpr bool as_a_list = true; … };
template <class type>
concept says_it_is_a_list = requires {
  requires scan::scanner<std::remove_cv_t<type>>::as_a_list;
};

template <class type>
concept scanned_as_leaf = requires {
  sizeof(scan::scanner<std::remove_cv_t<type>>);
} && !scanned_by_format<type> && !says_it_is_a_list<type>;

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
concept scanned_from_values = says_a_format<type> && requires {
  &scan::scanner<std::remove_cv_t<type>>::parse;
};

// A type that holds as many of something as the input turns out to have.
//
// Nothing in the format says how many; the type does, by being a range that can
// be grown. The place stands for the whole list and its body is the format of
// one element, read over again for as long as it goes on -- so a separator is
// written the way anything matched and not kept is written, and the element may
// be a value, a shape, a variant or another list.
// A type read as a list: as many turns as the subject affords.
//
// A type that is both a value and a range is read as a value, because that is
// what a `std::string` field means. `as_a_list` is how a type says otherwise,
// and saying it stops the type being a leaf at all.
template <class type>
concept scanned_as_range =
    !scanned_as_leaf<type> && !scanned_by_format<type> &&
    !scanned_as_variant<type> && std::ranges::range<type> &&
    requires(type& into, std::ranges::range_value_t<type> element) {
      into.push_back(std::move(element));
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
  requires scanned_as_range<type>
struct parts_of<type, false> {
  static constexpr std::size_t count = 1;
  template <std::size_t index>
  using at = std::remove_cvref_t<std::ranges::range_value_t<type>>;
};
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

// What a shape is made of, asked of what it says and not of how it is read.
//
// The same answer as the general one -- the arguments of the call that makes
// it, or its fields -- but reached without asking whether the type is a list or
// a leaf or a shape, because those are questions this one is used to answer.
template <class type, bool = scanned_from_values<type>>
struct shape_parts {
  static constexpr std::size_t count =
      boost::pfr::tuple_size_v<std::remove_cv_t<type>>;
  template <std::size_t index>
  using at = std::remove_cvref_t<
      boost::pfr::tuple_element_t<index, std::remove_cv_t<type>>>;
};
template <class type>
struct shape_parts<type, true> {
  using call = call_parameters<
      decltype(&scan::scanner<std::remove_cv_t<type>>::parse)>;
  static constexpr std::size_t count = call::count;
  template <std::size_t index>
  using at = typename call::template at<index>;
};

// Whether a list stands anywhere inside a shape, asked of what the types say
// and never of how this library reads them.
//
// The difference matters here and nowhere else: how a shape is read is decided
// by whether it can hand its groups over, that is decided by whether it holds
// a list, and a question that asks its own answer has none. So this one asks
// only what a type is: a range that can be pushed into is a list, a type that
// says it is one is one, and a shape or a choice is asked about what is in it.
template <class type>
concept a_list_by_itself =
    !requires { sizeof(scan::scanner<std::remove_cv_t<type>>); } &&
    std::ranges::range<type> &&
    requires(type& into, std::ranges::range_value_t<type> one) {
      into.push_back(std::move(one));
    };

template <class type>
concept a_choice_by_itself = requires {
  scan::branches<std::remove_cv_t<type>>::count;
};

template <class type>
[[nodiscard]] consteval bool says_a_list_inside();

template <class type>
[[nodiscard]] consteval bool a_list_field() {
  if constexpr (says_it_is_a_list<type> || a_list_by_itself<type>) {
    return true;
  } else if constexpr (says_a_format<type>) {
    return says_a_list_inside<type>();
  } else if constexpr (a_choice_by_itself<type>) {
    return []<std::size_t... which>(std::index_sequence<which...>) {
      return (false || ... ||
              a_list_field<std::remove_cv_t<branch_at<type, which>>>());
    }(std::make_index_sequence<branch_count<type>()>{});
  } else {
    return false;
  }
}

template <class type>
[[nodiscard]] consteval bool says_a_list_inside() {
  if constexpr (!says_a_format<type>) {
    return a_list_field<type>();
  } else {
    // What a shape is made of: the arguments of the call that makes it, where
    // there is one, and its fields otherwise. Asked this way rather than by
    // reflection, because a shape made by a call has no fields to reflect on
    // and its arguments are as much a part of it as fields are of anything.
    return []<std::size_t... field>(std::index_sequence<field...>) {
      return (false || ... ||
              a_list_field<typename shape_parts<
                  std::remove_cv_t<type>>::template at<field>>());
    }(std::make_index_sequence<shape_parts<std::remove_cv_t<type>>::count>{});
  }
}

// What one place in a format stands for. A leaf takes one; a type with a format
// of its own takes one and spends it on the format it declared; anything else
// is opened up and its fields take places of their own, which is why a
// structure of structures can be written out flat.
template <class type>
[[nodiscard]] consteval std::size_t places_of() {
  if constexpr (scanned_as_leaf<type> || scanned_by_format<type> ||
                scanned_as_variant<type> || scanned_as_range<type>) {
    return 1;
  } else {
    return []<std::size_t... index>(std::index_sequence<index...>) {
      return (std::size_t{0} + ... +
              places_of<typename parts_of<type>::template at<index>>());
    }(std::make_index_sequence<parts_of<type>::count>{});
  }
}

// Whether the type names its groups with types rather than with numbers.
//
//   using group = std::variant<major, minor, patch>;
//
// Then the k-th alternative is the name of the k-th group, and the pushes can
// be overloads rather than a switch.
template <class type>
concept names_its_groups = requires {
  typename scan::scanner<std::remove_cv_t<type>>::group;
};

// Whether the type would rather have the group whole than a character at a
// time. Only a subject that can be pointed at can offer it, so this is asked
// together with whether there is anything to point at.
template <class type, std::size_t which, class state_type>
concept takes_the_group_whole =
    requires(state_type& state, std::string_view text) {
      scan::scanner<std::remove_cv_t<type>>{}.closed_group(
          state, scan::group_at<which>{}, text);
    } || requires(state_type& state, std::string_view text) {
      scan::scanner<std::remove_cv_t<type>>{}.closed_group(state, which, text);
    } || (names_its_groups<type> &&
          requires(state_type& state, std::string_view text) {
            scan::scanner<std::remove_cv_t<type>>{}.closed_group(
                state,
                std::variant_alternative_t<
                    which,
                    typename scan::scanner<std::remove_cv_t<type>>::group>{},
                text);
          });

// The state a type folds its groups in, as a type.
template <class type>
using group_state_of =
    decltype(scan::scanner<std::remove_cv_t<type>>{}.begin_groups());

// Whether the type takes the characters of this group at all. A fold may be
// made of the edges alone -- counting the turns, saying which branch ran -- and
// then there is nothing to hand a character to.
template <class type, std::size_t which, class state_type>
concept takes_group_characters =
    requires(state_type& state, char letter) {
      scan::scanner<std::remove_cv_t<type>>{}.push_group(
          state, scan::group_at<which>{}, letter);
    } || requires(state_type& state, char letter) {
      scan::scanner<std::remove_cv_t<type>>{}.push_group(state, which, letter);
    } || (names_its_groups<type> && requires(state_type& state, char letter) {
      scan::scanner<std::remove_cv_t<type>>{}.push_group(
          state,
          std::variant_alternative_t<
              which, typename scan::scanner<std::remove_cv_t<type>>::group>{},
          letter);
    });

// One character, handed to the group it belongs to, in whichever of the three
// ways the type asked for: the name of the group, the group as a variant, or
// its number. The choice is made here, where the number is a constant.
template <class type, std::size_t which, class state_type>
constexpr void push_one_group(state_type& state, char letter) {
  using scanner_type = scan::scanner<std::remove_cv_t<type>>;
  if constexpr (requires {
                  scanner_type{}.push_group(state, scan::group_at<which>{},
                                           letter);
                }) {
    scanner_type{}.push_group(state, scan::group_at<which>{}, letter);
  } else if constexpr (names_its_groups<type>) {
    using named = typename scanner_type::group;
    using one = std::variant_alternative_t<which, named>;
    if constexpr (requires { scanner_type{}.push_group(state, one{}, letter); }) {
      scanner_type{}.push_group(state, one{}, letter);
    } else if constexpr (requires {
                           scanner_type{}.push_group(
                               state, named(std::in_place_index<which>),
                               letter);
                         }) {
      scanner_type{}.push_group(state, named(std::in_place_index<which>),
                               letter);
    } else {
      scanner_type{}.push_group(state, which, letter);
    }
  } else {
    scanner_type{}.push_group(state, which, letter);
  }
}

// The two edges of a group, said the same three ways a push is said. A type
// that only wants the characters says neither, and then nothing is said to it.
template <class type, std::size_t which, class state_type>
constexpr void open_one_group(state_type& state) {
  using scanner_type = scan::scanner<std::remove_cv_t<type>>;
  if constexpr (requires {
                  scanner_type{}.opened_group(state, scan::group_at<which>{});
                }) {
    scanner_type{}.opened_group(state, scan::group_at<which>{});
  } else if constexpr (names_its_groups<type>) {
    using named = typename scanner_type::group;
    using one = std::variant_alternative_t<which, named>;
    if constexpr (requires { scanner_type{}.opened_group(state, one{}); }) {
      scanner_type{}.opened_group(state, one{});
    } else if constexpr (requires {
                           scanner_type{}.opened_group(
                               state, named(std::in_place_index<which>));
                         }) {
      scanner_type{}.opened_group(state, named(std::in_place_index<which>));
    } else if constexpr (requires { scanner_type{}.opened_group(state, which); }) {
      scanner_type{}.opened_group(state, which);
    }
  } else if constexpr (requires { scanner_type{}.opened_group(state, which); }) {
    scanner_type{}.opened_group(state, which);
  }
}

template <class type, std::size_t which, class state_type>
constexpr void close_one_group(state_type& state);

// A group closing, and where the subject can be pointed at, the whole of what
// it stood on handed over with it. Off a stream there is no such thing to hand,
// so the type is told the characters as they arrive and told the closing on its
// own; the two are the same fold, said with what each reading has to give.
template <class type, std::size_t which, class state_type>
constexpr void close_one_group(state_type& state, std::string_view text) {
  using scanner_type = scan::scanner<std::remove_cv_t<type>>;
  if constexpr (requires {
                  scanner_type{}.closed_group(state, scan::group_at<which>{},
                                             text);
                }) {
    scanner_type{}.closed_group(state, scan::group_at<which>{}, text);
  } else if constexpr (requires {
                         scanner_type{}.closed_group(state, which, text);
                       }) {
    scanner_type{}.closed_group(state, which, text);
  } else if constexpr (names_its_groups<type> && requires {
                         scanner_type{}.closed_group(
                             state,
                             std::variant_alternative_t<
                                 which, typename scanner_type::group>{},
                             text);
                       }) {
    scanner_type{}.closed_group(
        state,
        std::variant_alternative_t<which, typename scanner_type::group>{},
        text);
  } else {
    for (char letter : text) push_one_group<type, which>(state, letter);
    close_one_group<type, which>(state);
  }
}

template <class type, std::size_t which, class state_type>
constexpr void close_one_group(state_type& state) {
  using scanner_type = scan::scanner<std::remove_cv_t<type>>;
  if constexpr (requires {
                  scanner_type{}.closed_group(state, scan::group_at<which>{});
                }) {
    scanner_type{}.closed_group(state, scan::group_at<which>{});
  } else if constexpr (names_its_groups<type>) {
    using named = typename scanner_type::group;
    using one = std::variant_alternative_t<which, named>;
    if constexpr (requires { scanner_type{}.closed_group(state, one{}); }) {
      scanner_type{}.closed_group(state, one{});
    } else if constexpr (requires {
                           scanner_type{}.closed_group(
                               state, named(std::in_place_index<which>));
                         }) {
      scanner_type{}.closed_group(state, named(std::in_place_index<which>));
    } else if constexpr (requires { scanner_type{}.closed_group(state, which); }) {
      scanner_type{}.closed_group(state, which);
    }
  } else if constexpr (requires { scanner_type{}.closed_group(state, which); }) {
    scanner_type{}.closed_group(state, which);
  }
}

// A type that reads itself out of the groups its own pattern opens.
//
// Either handed them when the match is done, or told which of them each
// character belongs to as it arrives -- and either way its pattern has groups
// in it, which are groups of whatever it is written into.
template <class type>
concept reads_its_own_groups =
    requires { scan::scanner<std::remove_cv_t<type>>{}.begin_groups(); } ||
    requires(std::span<const std::string_view> given) {
      scan::scanner<std::remove_cv_t<type>>{}.from_groups(given);
    } || requires(std::span<const std::string_view> given) {
      scan::scanner<std::remove_cv_t<type>>{}.try_from_groups(given);
    };

// How many groups a leaf's own pattern opens.
//
// Nought for every leaf that does not read itself out of them, which is every
// leaf there was until now -- so the counting below is the counting that was
// there before, for everything that came before.
// The pattern a type declares, asked for in the way that works however it is
// declared.
//
// A scanner may say `pattern()`, or `pattern(parameters)`, or both. Asked
// without parameters, one that only has the second answers with the member
// itself -- a pointer to a function, not a pattern -- and everything after
// that is a puzzle. Asked with empty parameters, both answer with a pattern,
// and empty parameters are what a place with nothing written after the colon
// hands over anyway.
template <class type>
[[nodiscard]] constexpr auto declared_pattern() {
  return scanner_pattern<std::remove_cv_t<type>>(std::string_view{});
}

// And its characters, however the pattern is held.
[[nodiscard]] constexpr std::string_view pattern_view(const auto& declared) {
  if constexpr (requires { std::string_view(declared); }) {
    return std::string_view(declared);
  } else {
    return declared.view();
  }
}

template <class type>
[[nodiscard]] consteval std::size_t groups_a_leaf_opens() {
  if constexpr (!reads_its_own_groups<type>) {
    return 0;
  } else {
    std::size_t counted = 0;
    const auto declared = declared_pattern<type>();
    tre_parser reading(pattern_view(declared), {}, counted, true);
    static_cast<void>(reading.parse_regex());
    return counted;
  }
}

template <class type>
[[nodiscard]] consteval std::size_t groups_of() {
  if constexpr (scanned_as_leaf<type>) {
    // The place itself, and the groups the type's own pattern opens inside
    // it, which are groups of this match like any others.
    return 1 + groups_a_leaf_opens<type>();
  } else if constexpr (scanned_as_variant<type>) {
    // A mark for each branch, and then whatever that branch's alternative
    // reads, in the order the branches are written.
    return []<std::size_t... which>(std::index_sequence<which...>) {
      return (std::size_t{0} + ... +
              (1 + groups_of<branch_at<type, which>>()));
    }(std::make_index_sequence<branch_count<type>()>{});
  } else if constexpr (scanned_as_range<type>) {
    // One for the list itself, and then whatever one element reads -- written
    // over again on every turn round the loop.
    return 1 + groups_of<std::remove_cvref_t<std::ranges::range_value_t<type>>>();
  } else {
    return []<std::size_t... index>(std::index_sequence<index...>) {
      return (std::size_t{0} + ... +
              groups_of<typename parts_of<type>::template at<index>>());
    }(std::make_index_sequence<parts_of<type>::count>{});
  }
}

// The same count, asked of a type as the output of its own format rather than
// as a value standing in somebody else's.
//
// A shape that reads its own groups is one value to whatever contains it -- a
// place, and the groups its pattern opens after it -- and a product of places
// to itself. Everything that builds a reading of a format asks the second
// question, and asking the first would count a place nobody wrote.
template <class type>
[[nodiscard]] consteval std::size_t groups_of_output() {
  if constexpr (scanned_as_variant<type>) {
    return []<std::size_t... which>(std::index_sequence<which...>) {
      return (std::size_t{0} + ... + (1 + groups_of<branch_at<type, which>>()));
    }(std::make_index_sequence<branch_count<type>()>{});
  } else if constexpr (scanned_as_range<type>) {
    return 1 +
           groups_of<std::remove_cvref_t<std::ranges::range_value_t<type>>>();
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
                 scanned_as_variant<subject> || scanned_as_range<subject>>
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
                scanned_as_variant<subject> || scanned_as_range<subject>);
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

template <class held_type>
struct kind_is {
  using kind = held_type;
};

// Which branch of a variant a group falls in, and where within it: nothing
// means the mark that stands for the branch itself, and anything after it
// belongs to what that branch reads.
template <class type>
[[nodiscard]] consteval std::pair<std::size_t, std::size_t> branch_holding(
    std::size_t index) {
  constexpr auto counts = []<std::size_t... which>(
                              std::index_sequence<which...>) {
    return std::array<std::size_t, sizeof...(which)>{
        (1 + groups_of<branch_at<type, which>>())...};
  }(std::make_index_sequence<branch_count<type>()>{});
  for (std::size_t branch = 0; branch < counts.size(); ++branch) {
    if (index < counts[branch]) return {branch, index};
    index -= counts[branch];
  }
  throw "group index past the end of the variant";
}

// Which type gathers the value at this group. A list gathers at the group that
// stands for the list itself -- the first of the ones it takes -- and its
// element gathers at the ones after it, over and over.
template <class subject, std::size_t index,
          int = scanned_as_leaf<subject> ? 0
                : scanned_as_range<subject> ? 1
                : scanned_as_variant<subject> ? 3
                                              : 2>
struct leaf_at;
template <class subject, std::size_t index>
struct leaf_at<subject, index, 0> {
  using kind = subject;
};
template <class subject, std::size_t index>
struct leaf_at<subject, index, 1> {
  using element = std::remove_cvref_t<std::ranges::range_value_t<subject>>;
  using kind = typename std::conditional_t<
      index == 0, kind_is<subject>,
      leaf_at<element, (index == 0 ? 0 : index - 1)>>::kind;
};
template <class subject, std::size_t index>
struct leaf_at<subject, index, 2> {
  static constexpr auto where = field_holding<subject>(index);
  using next = typename parts_of<subject>::template at<where.first>;
  using kind = typename leaf_at<next, where.second>::kind;
};

// A variant is not a product and cannot be opened up like one: its groups are
// a mark for each branch, followed by whatever that branch reads.
template <class subject, std::size_t index>
struct leaf_at<subject, index, 3> {
  static constexpr auto where = branch_holding<subject>(index);
  using branch = branch_at<subject, where.first>;
  using kind = typename std::conditional_t<
      where.second == 0, kind_is<branch_mark>,
      leaf_at<branch, (where.second == 0 ? 0 : where.second - 1)>>::kind;
};

template <class subject, std::size_t index>
using leaf_kind = typename leaf_at<subject, index>::kind;

// The same two, asked of a type as the output of its own format: never as a
// value standing in somebody else's place, which is what it looks like to
// whatever contains it.
template <class subject, std::size_t index>
using leaf_kind_of_output = typename leaf_at<
    subject, index,
    scanned_as_range<subject> ? 1 : scanned_as_variant<subject> ? 3 : 2>::kind;


// Where within that type the group falls: nothing means the place the type
// stands at, and anything after it is one of the groups the type's own pattern
// opens, counted in the order they are written.
//
// The walk is the one above, step for step. Only the answer differs, so if one
// of them ever learns a new shape the other has to learn it too.
template <class subject, std::size_t index,
          int = scanned_as_leaf<subject> ? 0
                : scanned_as_range<subject> ? 1
                : scanned_as_variant<subject> ? 3
                                              : 2>
struct leaf_offset_at;
template <class subject, std::size_t index>
struct leaf_offset_at<subject, index, 0> {
  static constexpr std::size_t value = index;
};
template <class subject, std::size_t index>
struct leaf_offset_at<subject, index, 1> {
  using element = std::remove_cvref_t<std::ranges::range_value_t<subject>>;
  static constexpr std::size_t value =
      index == 0 ? 0
                 : leaf_offset_at<element, (index == 0 ? 0 : index - 1)>::value;
};
template <class subject, std::size_t index>
struct leaf_offset_at<subject, index, 2> {
  static constexpr auto where = field_holding<subject>(index);
  using next = typename parts_of<subject>::template at<where.first>;
  static constexpr std::size_t value = leaf_offset_at<next, where.second>::value;
};
template <class subject, std::size_t index>
struct leaf_offset_at<subject, index, 3> {
  static constexpr auto where = branch_holding<subject>(index);
  using branch = branch_at<subject, where.first>;
  static constexpr std::size_t value =
      where.second == 0
          ? 0
          : leaf_offset_at<branch, (where.second == 0 ? 0 : where.second - 1)>::
                value;
};

template <class subject, std::size_t index>
inline constexpr std::size_t leaf_offset_of = leaf_offset_at<subject, index>::value;

template <class subject, std::size_t index>
inline constexpr std::size_t leaf_offset_of_output = leaf_offset_at<
    subject, index,
    scanned_as_range<subject> ? 1 : scanned_as_variant<subject> ? 3
                                                                : 2>::value;

// A leaf that is put together from the groups its own pattern opens, rather
// than from the text it stands on. Where it opens none, it is an ordinary leaf
// and nothing below changes for it.
template <class held>
inline constexpr bool gathers_by_its_groups =
    reads_its_own_groups<held> && groups_a_leaf_opens<held>() > 0;

// A type that folds its groups as they happen, rather than reading them once
// the match is over.
//
// The two are not a matter of taste. A machine with tags keeps a bounded number
// of positions -- one per tag -- so what is there at the end is the last turn
// round a loop and nothing before it. A type whose groups repeat can only be
// built by folding the turns as they go, which is what a list has always done
// here. So a type that says `begin_groups` is told its groups during the walk,
// and one that only says `from_groups` is handed them afterwards, which is all
// that can be handed to it.
template <class type>
concept folds_its_groups = requires {
  scan::scanner<std::remove_cv_t<type>>{}.begin_groups();
};

template <class held>
inline constexpr bool folds_by_turns =
    gathers_by_its_groups<held> && folds_its_groups<held>;

// A type that can only be told its groups as they happen: it folds them and
// cannot be handed them afterwards.
//
// The two are not the same question. A type that can do both is folded where
// the subject is read once -- there is no other way there -- and handed its
// groups whole where they can be pointed at, which is the faster of the two
// and the one that copies nothing. Only a type that cannot be handed them
// forces the reading that gathers as it goes.
template <class held>
inline constexpr bool needs_the_turns =
    folds_by_turns<held> && !requires(std::span<const std::string_view> given) {
      scan::scanner<std::remove_cv_t<held>>{}.from_groups(given);
    } && !requires(std::span<const std::string_view> given) {
      scan::scanner<std::remove_cv_t<held>>{}.try_from_groups(given);
    };

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
  // Carried rather than passed: the format says it, and everything that
  // spreads a format is already handed this.
  bool space_before_places = false;
};

// The spelling the spread writes.
//
// A place is a group, a mark is a group of nothing, a repeated group is a
// repeated group, and text that stands for itself is written so that it still
// does. That is a plain regular expression: the groups of it are the places of
// the format, in the order the format has them, so a type handed its own groups
// is handed its places, and the thing that reads a format is the thing that
// reads an expression.
constexpr void say_place_begin(spread_format& made) { made.text.push_back('('); }

constexpr void say_place_end(spread_format& made) { made.text.push_back(')'); }

constexpr void say_group_begin(spread_format& made) {
  made.text.append(std::string_view("(?:"));
}

constexpr void say_group_end(spread_format& made) { made.text.push_back(')'); }

constexpr void say_branch(spread_format& made) { made.text.push_back('|'); }

constexpr void say_mark(spread_format& made) {
  made.text.append(std::string_view("()"));
}

constexpr void say_raw_begin(spread_format& made) {
  made.text.append(std::string_view("(?:"));
}

constexpr void say_raw_end(spread_format& made) { made.text.push_back(')'); }

// A character of the format that stands for itself.
//
// In this library's own spelling that is what it is: the format parser reads
// anything it has no meaning for as the character it is. Said as a regular
// expression it has to be written so that it still stands for itself, because
// there a dot is every character and a plus is a repetition.
constexpr void say_literal(spread_format& made, char value) {
  if (std::string_view(".^$|()[]*+?{}\\").contains(value)) {
    made.text.push_back('\\');
  }
  made.text.push_back(value);
}

// A pattern somebody wrote in the format: copied as it stands, except that a
// group written there keeps nothing.
//
// A place is one value, and what is written inside it says what that value
// matches -- not how many values there are. So `{(\d+)-(\d+)}` is one value
// however many brackets it has, and the brackets are made into the kind that
// group without keeping.
constexpr void say_written_pattern(spread_format& made, std::string_view text) {
  bool character_class = false;
  for (std::size_t at = 0; at < text.size(); ++at) {
    const char value = text[at];
    if (value == '\\' && at + 1 < text.size()) {
      made.text.push_back(value);
      made.text.push_back(text[at + 1]);
      ++at;
      continue;
    }
    if (value == '[') character_class = true;
    if (value == ']') character_class = false;
    if (!character_class && value == '(' &&
        !(at + 1 < text.size() && text[at + 1] == '?')) {
      made.text.append(std::string_view("(?:"));
      continue;
    }
    made.text.push_back(value);
  }
}

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
    say_literal(made, text[position]);
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
    say_raw_begin(made);
    made.text.append(body.substr(1));
    say_raw_end(made);
    position = close + 1;
  }
}

template <class type, bool within>
constexpr void spread_into(spread_format& made, std::string_view text);

// How many turns a place is written to take, as it is written: a star, a plus,
// a question mark or a count in braces. Nothing written at all is one or more,
// which is what a list of something usually is.
[[nodiscard]] constexpr std::string_view repetition_after(std::string_view text,
                                                          std::size_t at) {
  if (at >= text.size()) return {};
  const char first = text[at];
  if (first == '*' || first == '+' || first == '?') return text.substr(at, 1);
  if (first != '{') return {};
  if (at + 1 >= text.size() || text[at + 1] < '0' || text[at + 1] > '9') {
    return {};
  }
  std::size_t close = at + 1;
  while (close < text.size() && text[close] != '}') ++close;
  if (close == text.size()) throw "unterminated repetition";
  return text.substr(at, close - at + 1);
}

template <class kind>
constexpr void spread_place(spread_format& made, std::string_view body,
                            std::string_view repetition = {}) {
  if constexpr (scanned_as_range<kind>) {
    // The body is one element, and it is read for as long as it goes on. The
    // group around it is the list; the places inside it are the element, and
    // they are written over again on every turn.
    // The group is the list and holds it whole; what repeats is inside it. The
    // other way round -- a group repeated -- would open the list again on
    // every turn, and a list opened again is an empty one.
    say_place_begin(made);
    ++made.leaves;
    say_group_begin(made);
    spread_into<std::remove_cvref_t<std::ranges::range_value_t<kind>>, false>(
        made, body);
    say_group_end(made);
    made.text.append(repetition);
    say_place_end(made);
  } else if constexpr (scanned_as_variant<kind>) {
    // The branches, held together, each headed by a mark. Written out, the body
    // of the place says them, one per alternative, separated by a bar. Left
    // empty, each alternative is asked how it reads itself -- which it can
    // answer if it declares a format or if something knows how to read it.
    constexpr std::size_t count = branch_count<kind>();
    std::size_t written = 0;
    std::array<std::pair<std::size_t, std::size_t>, 16> parts{};
    if (!body.empty()) {
      parts = branches_of(body, written);
      if (written != count) {
        throw "a variant place must have one branch for each alternative";
      }
    }
    say_group_begin(made);
    [&]<std::size_t... which>(std::index_sequence<which...>) {
      const auto one = [&]<std::size_t branch>() {
        if constexpr (branch != 0) say_branch(made);
        say_mark(made);
        // The mark is a group like any other and takes a number, so what comes
        // after it reads its own parameters and not the ones before.
        ++made.leaves;
        using alternative = branch_at<kind, branch>;
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
    say_group_end(made);
  } else if constexpr (scanned_by_format<kind>) {
    // Still spread into the pattern around it, which is what a shape made of
    // turns needs. A shape that reads its own groups is not one of these: it
    // stands in a place like any other value, and its groups follow it.
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
    if constexpr (gathers_by_its_groups<std::remove_cv_t<kind>>) {
      // The groups are counted off the pattern the type declares, so that is
      // the pattern it has to be read with. Written over, the count and the
      // groups would be two different things.
      if (!body.empty() && body.front() != ':') {
        throw "a type that reads its own groups keeps its own pattern -- a "
              "place standing for it takes parameters but not a pattern";
      }
    }
    say_place_begin(made);
    if (body.empty() || body.front() == ':') {
      const std::string_view given = body.empty() ? body : body.substr(1);
      made.parameters[made.leaves].append(given);
      const auto pattern = scanner_pattern<std::remove_cv_t<kind>>(given);
      if constexpr (gathers_by_its_groups<std::remove_cv_t<kind>>) {
        // Its groups are what it is handed, so they are groups here.
        made.text.append(pattern_view(pattern));
      } else {
        // A value is one place however its pattern is written, so whatever it
        // wrote in brackets groups without keeping.
        say_written_pattern(made, pattern_view(pattern));
      }
    } else {
      say_written_pattern(made, body);
    }
    say_place_end(made);
    // The place, and then the groups its pattern opens, which take the numbers
    // straight after it. What is counted here is group numbers: the parameters
    // are read by the group that gathers, so everything that takes a number has
    // to move this on.
    ++made.leaves;
    if constexpr (gathers_by_its_groups<std::remove_cv_t<kind>>) {
      made.leaves += groups_a_leaf_opens<std::remove_cv_t<kind>>();
    }
  }
}

template <class type, bool within>
constexpr void spread_into(spread_format& made, std::string_view text) {
  std::size_t position = 0;
  [&]<std::size_t... place>(std::index_sequence<place...>) {
    const auto one = [&]<std::size_t which>() {
      copy_until_kept_place(made, text, position);
      // A place can hold another, and the one inside is a value of its own:
      // `{{[a]+}}` is a group around a group, two values, one place at this
      // level. The body is copied as it stands, so the places within it are
      // still groups when the pattern is read, and the values they take are
      // the ones this walk has no place left for. Running out here is that,
      // and not a format with too little in it.
      if (position == text.size()) return;
      const std::size_t close = end_of_place(text, position);
      using kind = typename place_chosen<type, within, which>::kind;
      std::string_view repetition;
      if constexpr (scanned_as_range<kind>) {
        repetition = repetition_after(text, close + 1);
      }
      // Whatever whitespace is in front of the place, taken and not kept --
      // which is what `%d` does, and what this format asked for by being
      // written `past_space`.
      if (made.space_before_places) {
        say_raw_begin(made);
        made.text.append("\\s*");
        say_raw_end(made);
      }
      spread_place<kind>(
          made, text.substr(position + 1, close - position - 1), repetition);
      position = close + 1 + repetition.size();
    };
    (one.template operator()<place>(), ...);
  }(std::make_index_sequence<places_chosen<type, within>()>{});
  copy_until_kept_place(made, text, position);
  if (position != text.size()) throw "format has more places than values";
}

template <class type, fixed_string format>
[[nodiscard]] consteval spread_format spread_of() {
  spread_format made;
  made.space_before_places = format.space_before_places;
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

// The same spread, said as a regular expression: the groups of it are the
// places of the format, in the order the format has them. This is what a type
// that declares a format matches, and what it is handed when it is handed its
// own groups.
template <class type, fixed_string format>
[[nodiscard]] consteval pattern_buffer<> places_pattern() {
  constexpr auto made = spread_of<type, format>();
  pattern_buffer<> result;
  result.append(made.text.view());
  return result;
}

// What one field of a shape matches, whatever kind of field it is.
//
// A value says it itself. A choice says a mark and a branch, over and over --
// and a branch is a field like any other, so this is written once and asks
// itself about them.
template <class field_type>
[[nodiscard]] constexpr pattern_buffer<> field_pattern(
    std::string_view parameters);

template <class type, std::size_t extent, std::size_t... index>
[[nodiscard]] constexpr auto parameterized_patterns(
    const std::array<std::string_view, extent>& parameters,
    std::index_sequence<index...>) {
  const auto make_pattern = []<class field_type>(
                                std::string_view field_parameters) {
    // A choice is written so that which branch ran can be read off the match:
    // a group of nothing in front of each branch, which took part only if that
    // branch did. The same mark the spread writes, said in the language
    // everybody else can read.
    return field_pattern<field_type>(field_parameters);
  };
  // By field, and not by the values a field opens up into: this builds the
  // pattern a whole aggregate matches for the paths that match it whole -- the
  // streaming one -- and there a field that is itself a shape contributes its
  // own pattern, recursively, rather than being spread out here.
  return std::array<pattern_buffer<>, extent>{
      make_pattern.template operator()<std::remove_cvref_t<
          boost::pfr::tuple_element_t<index, type>>>(parameters[index])...};
}

template <class field_type>
[[nodiscard]] constexpr pattern_buffer<> field_pattern(
    std::string_view parameters) {
  pattern_buffer<> result;
  if constexpr (scanned_as_variant<field_type>) {
    // A mark, and then the branch in a group of its own -- the mark says which
    // branch ran, because a group that took no part points nowhere, and the
    // group beside it holds what that branch stood on.
    result.append(std::string_view("(?:"));
    [&]<std::size_t... which>(std::index_sequence<which...>) {
      ((void)[&] {
        if constexpr (which != 0) result.push_back('|');
        result.append(std::string_view("()("));
        const auto branch =
            field_pattern<std::remove_cv_t<branch_at<field_type, which>>>(
                std::string_view{});
        result.append(branch.view());
        result.push_back(')');
      }(), ...);
    }(std::make_index_sequence<branch_count<field_type>()>{});
    result.push_back(')');
  } else {
    const auto pattern = scanner_pattern<field_type>(parameters);
    result.append(std::string_view{pattern});
  }
  return result;
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
  // The spread writes a regular expression whose groups are the places of the
  // format, in the order the format has them -- so what reads it is the reader
  // of expressions, and there is no second language and no second reader.
  constexpr auto spread = spread_of<type, format>();
  std::size_t captures = 0;
  tre_parser parser(spread.text.view(), {}, captures, true);
  scan::tre::node expression = parser.parse_regex();
  if (captures != groups_of_output<type>()) {
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
template <class type, fixed_string format, bool allocate = true,
          bool cut_at_match = true>
[[nodiscard]] constexpr scan::tre::tdfa build_tdfa() {
  return scan::tre::optimize_tdfa(
      scan::tre::compile_tdfa(build_tnfa<type, format>(), cut_at_match),
      allocate);
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

template <class type, fixed_string format, bool allocate = true,
          bool cut = true>
[[nodiscard]] consteval packed_shape compute_shape() {
  const scan::tre::tdfa tdfa = build_tdfa<type, format, allocate, cut>();
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
              range.command_count = transition.commands.size();
              std::ranges::transform(transition.commands,
                                     range.commands.begin(), pack_command);
              previous = index;
            }
      }
  return packed;
}

template <class type, fixed_string format, bool allocate = true,
          bool cut = true>
[[nodiscard]] consteval auto pack_tdfa() {
  constexpr packed_shape shape = compute_shape<type, format, allocate, cut>();
  return pack_tdfa_value<shape.states, shape.registers,
                         shape.initial_commands, shape.maximum_commands,
                         shape.maximum_final_commands, shape.tags, shape.ranges,
                         shape.readings>(
      build_tdfa<type, format, allocate, cut>());
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
template <class type, fixed_string format, bool cut = true>
inline constexpr auto packed_automaton = pack_tdfa<type, format, true, cut>();

// Built once, on first use. The determiniser is the same one the compiled form
// evaluates while compiling; asked at run time it answers in microseconds.
template <class type, fixed_string format, bool allocate = true>
[[nodiscard]] inline const scan::tre::tdfa& runtime_automaton() {
  static const scan::tre::tdfa built = build_tdfa<type, format, allocate>();
  return built;
}

template <class type, fixed_string format, bool cut = true>
inline constexpr auto streaming_automaton =
    pack_tdfa<type, format, false, cut>();

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

// Values built out of the groups a match left behind.
//
// This lived beside the format reader, which is the only thing that used it.
// The pattern reader wants it too: where a group is written with the very
// pattern a type declares for itself, the groups inside it are the type's own
// values and the type is built from them -- no second automaton over the same
// characters, and no reading of the same text twice.

// One place's text, read as its type -- or what went wrong instead.
//
// A scanner that says `try_parse` never throws and its failure comes back from
// here as one of the kinds this reading can fail with. One that says only
// `parse` throws whatever it throws, past all of this: nothing in this library
// catches, so a scanner that wants its failure handed back says so by handing
// it back.
template <class type, class failure_type>
[[nodiscard]] constexpr std::expected<type, failure_type> parse_value(
    std::string_view text, std::string_view parameters) {
  using value_type = std::remove_cv_t<type>;
  if constexpr (scan::says_what_went_wrong<value_type>) {
    auto got = scan::scanner_try_parse<value_type>(text, parameters);
    if (got) return std::move(*got);
    return std::unexpected(
        scan::as_a_failure<failure_type>(std::move(got).error()));
  } else {
    static_assert(requires { scanner_parse<value_type>(text); },
                  "scan::scanner<type> must provide parse(string_view) or "
                  "try_parse(string_view)");
    return scanner_parse<value_type>(text, parameters);
  }
}

// Where a branch's mark stands, counting from the start of the variant: each
// branch before it took a mark of its own and whatever its alternative reads.
template <class type, std::size_t branch>
[[nodiscard]] consteval std::size_t groups_before_branch() {
  return []<std::size_t... which>(std::index_sequence<which...>) {
    return (std::size_t{0} + ... +
            (1 + groups_of<branch_at<type, which>>()));
  }(std::make_index_sequence<branch>{});
}

// Whether a variant stands anywhere inside this output, at any depth. Where one
// does, a group that took no part is the ordinary state of affairs rather than
// a fault.
template <class type>
[[nodiscard]] consteval bool holds_a_variant() {
  if constexpr (scanned_as_variant<type>) {
    return true;
  } else if constexpr (scanned_as_leaf<type>) {
    return false;
  } else {
    return []<std::size_t... field>(std::index_sequence<field...>) {
      return (false || ... ||
              holds_a_variant<typename parts_of<type>::template at<field>>());
    }(std::make_index_sequence<parts_of<type>::count>{});
  }
}

// Whether a list stands anywhere inside this output.
//
// A list is read by gathering, and it has to be: the positions a match leaves
// behind hold the last turn round the loop and nothing else, so an output with
// a list in it cannot be put together by reading them afterwards, however well
// they can be pointed at. It goes to the machine that gathers as it goes, over
// the very same characters.
template <class type>
[[nodiscard]] consteval bool holds_a_range() {
  if constexpr (scanned_as_range<type>) {
    return true;
  } else if constexpr (scanned_as_leaf<type>) {
    return false;
  } else if constexpr (scanned_as_variant<type>) {
    return []<std::size_t... which>(std::index_sequence<which...>) {
      return (false || ... ||
              holds_a_range<branch_at<type, which>>());
    }(std::make_index_sequence<branch_count<type>()>{});
  } else {
    return []<std::size_t... part>(std::index_sequence<part...>) {
      return (false || ... ||
              holds_a_range<typename parts_of<type>::template at<part>>());
    }(std::make_index_sequence<parts_of<type>::count>{});
  }
}

// The kinds a scanner hands back, where it hands back several of them: they
// are said as a variant, and this is that variant read as a list.
template <class variant>
struct kinds_of_variant;
template <class... kinds>
struct kinds_of_variant<std::variant<kinds...>> {
  static_assert((std::derived_from<kinds, scan::scan_error> && ...),
                "every kind a scanner hands back has to be a "
                "`scan::scan_error`: it ends up in the list of what a reading "
                "can fail with, and asking for a value rather than trying for "
                "it throws it");
  using list = scan::kind_list<kinds...>;
};

template <class... lists>
struct joined_all {
  using type = scan::kind_list<>;
};
template <class first>
struct joined_all<first> {
  using type = first;
};
template <class first, class... rest>
struct joined_all<first, rest...> {
  using type = typename scan::joined_lists<
      first, typename joined_all<rest...>::type>::type;
};

// What a scanner says it can go wrong with, said in the only place it cannot
// fall out of step with the code: the type it hands back.
//
// Four places to say it, one for each of the user's functions that makes a
// value -- `try_parse`, `try_finish`, `try_from_groups`, `try_finish_groups` --
// and the kinds of all of them together are what a reading of this leaf can
// fail with. A scanner that throws instead says nothing here and is caught
// nowhere: it throws past the reading, to whoever asked for it.
template <class kind>
struct kinds_handed_back {
  static_assert(std::derived_from<kind, scan::scan_error>,
                "what a `try_` function hands back has to be a "
                "`scan::scan_error`, or a variant of them: it ends up in the "
                "list of what a reading can fail with, and asking for a value "
                "rather than trying for it throws it");
  using list = scan::kind_list<kind>;
};
// Said as a variant where there is more than one of them.
template <class... kinds>
struct kinds_handed_back<std::variant<kinds...>> {
  using list = typename kinds_of_variant<std::variant<kinds...>>::list;
};

template <class type>
struct parse_kinds {
  using list = scan::kind_list<>;
};
template <class type>
  requires scan::says_what_went_wrong<std::remove_cv_t<type>>
struct parse_kinds<type> {
  using list = typename kinds_handed_back<
      scan::went_wrong_with<std::remove_cv_t<type>>>::list;
};

template <class type>
struct finish_kinds {
  using list = scan::kind_list<>;
};
template <class type>
  requires scan::says_what_went_wrong_finishing<std::remove_cv_t<type>>
struct finish_kinds<type> {
  using list = typename kinds_handed_back<
      scan::went_wrong_finishing<std::remove_cv_t<type>>>::list;
};

template <class type>
struct from_groups_kinds {
  using list = scan::kind_list<>;
};
template <class type>
  requires scan::says_what_went_wrong_from_groups<std::remove_cv_t<type>>
struct from_groups_kinds<type> {
  using list = typename kinds_handed_back<
      scan::went_wrong_from_groups<std::remove_cv_t<type>>>::list;
};

template <class type>
struct folding_kinds {
  using list = scan::kind_list<>;
};
template <class type>
  requires scan::says_what_went_wrong_folding<std::remove_cv_t<type>>
struct folding_kinds<type> {
  using list = typename kinds_handed_back<
      scan::went_wrong_folding<std::remove_cv_t<type>>>::list;
};

template <class type>
struct declared_kinds {
  using list = typename joined_all<typename parse_kinds<type>::list,
                                   typename finish_kinds<type>::list,
                                   typename from_groups_kinds<type>::list,
                                   typename folding_kinds<type>::list>::type;
};

// Every kind declared anywhere inside an output, walked the way everything
// else about an output is walked.
template <class type,
          int = scanned_as_leaf<type> ? 0
                : scanned_as_range<type> ? 1
                : scanned_as_variant<type> ? 3
                                           : 2>
struct kinds_in;
template <class type>
struct kinds_in<type, 0> {
  using list = typename declared_kinds<type>::list;
};
template <class type>
struct kinds_in<type, 1> {
  using list = typename kinds_in<
      std::remove_cvref_t<std::ranges::range_value_t<type>>>::list;
};
template <class type>
struct kinds_in<type, 2> {
  template <std::size_t... part>
  static auto over(std::index_sequence<part...>) -> typename joined_all<
      typename kinds_in<typename parts_of<type>::template at<part>>::list...>::
      type;
  using list = decltype(over(std::make_index_sequence<parts_of<type>::count>{}));
};
template <class type>
struct kinds_in<type, 3> {
  template <std::size_t... which>
  static auto over(std::index_sequence<which...>) -> typename joined_all<
      typename kinds_in<branch_at<type, which>>::list...>::type;
  using list =
      decltype(over(std::make_index_sequence<branch_count<type>()>{}));
};

// What reading a shape can fail with, asked of its fields.
//
// Never of the shape itself: what the shape says it hands back is this very
// list, so a list that asked the shape would be asking its own answer. Its
// fields are other types, and asking them is asking something else.
template <class type, class sequence>
struct kinds_of_fields;
template <class type, std::size_t... field>
struct kinds_of_fields<type, std::index_sequence<field...>> {
  using list = typename joined_all<
      typename kinds_in<typename shape_parts<
          std::remove_cv_t<type>>::template at<field>>::list...>::type;
};

template <class type>
using shape_failure = typename scan::as_a_variant<typename scan::without_repeats<
    typename scan::joined_lists<
        scan::our_kinds,
        typename kinds_of_fields<
            type, std::make_index_sequence<shape_parts<std::remove_cv_t<type>>::
                                               count>>::list>::type>::type>::
    type;

// This library's kinds, and the ones this output's own scanners declare.
template <class type>
using failure_for = typename scan::as_a_variant<typename scan::without_repeats<
    typename scan::joined_lists<scan::our_kinds,
                                typename kinds_in<type>::list>::type>::type>::
    type;

// Whether a fold stands anywhere inside this output.
//
// The same question as the one above, and the same answer for the same reason:
// a fold is told its groups as the walk passes them, so an output holding one
// cannot be put together from the positions left behind, however well they can
// be pointed at. It goes to the machine that gathers as it goes.
template <class type>
[[nodiscard]] consteval bool holds_a_fold() {
  if constexpr (scanned_as_leaf<type>) {
    return needs_the_turns<std::remove_cv_t<type>>;
  } else if constexpr (scanned_as_variant<type>) {
    return []<std::size_t... which>(std::index_sequence<which...>) {
      return (false || ... || holds_a_fold<branch_at<type, which>>());
    }(std::make_index_sequence<branch_count<type>()>{});
  } else if constexpr (scanned_as_range<type>) {
    return holds_a_fold<std::remove_cvref_t<std::ranges::range_value_t<type>>>();
  } else {
    return []<std::size_t... part>(std::index_sequence<part...>) {
      return (false || ... ||
              holds_a_fold<typename parts_of<type>::template at<part>>());
    }(std::make_index_sequence<parts_of<type>::count>{});
  }
}

// Whether a type that reads its own groups only once the match is over stands
// anywhere inside this output.
//
// Such a type is handed views of the subject, so it needs a subject there is
// something to point at. It can stand in the gathering machine -- a list or a
// fold beside it puts the whole output there -- and then the walk has to be
// one that started on characters lying in a row.
template <class type>
[[nodiscard]] consteval bool holds_a_flat_reader() {
  if constexpr (scanned_as_leaf<type>) {
    // One that can also be folded is not refused: off a stream it is told its
    // groups as they arrive, which wants nothing to point at. Nor is one that
    // gathers a character at a time, which is the ordinary way a leaf is read
    // off a stream -- it is handed its groups only where they can be pointed
    // at, and read as a value everywhere else.
    return gathers_by_its_groups<std::remove_cv_t<type>> &&
           !folds_by_turns<std::remove_cv_t<type>> &&
           !scan::gathers_as_it_reads<std::remove_cv_t<type>>;
  } else if constexpr (scanned_as_variant<type>) {
    return []<std::size_t... which>(std::index_sequence<which...>) {
      return (false || ... || holds_a_flat_reader<branch_at<type, which>>());
    }(std::make_index_sequence<branch_count<type>()>{});
  } else if constexpr (scanned_as_range<type>) {
    return holds_a_flat_reader<
        std::remove_cvref_t<std::ranges::range_value_t<type>>>();
  } else {
    return []<std::size_t... part>(std::index_sequence<part...>) {
      return (false || ... ||
              holds_a_flat_reader<typename parts_of<type>::template at<part>>());
    }(std::make_index_sequence<parts_of<type>::count>{});
  }
}

// Whether a shape's turns can be folded by the shape itself.
//
// Its places are told to it one at a time, so every place has to be something
// that takes characters: a value with a scanner, or a list of them. A place
// standing for a type that reads groups of its own would have to be handed
// those groups, and a fold has none to hand -- such a shape keeps the road
// that spreads its places into the automaton around it.
template <class type, fixed_string format>
[[nodiscard]] consteval bool turns_can_be_folded() {
  return []<std::size_t... place>(std::index_sequence<place...>) {
    return (true && ... && [] {
      using stands_for =
          std::remove_cv_t<leaf_kind_of_output<std::remove_cv_t<type>, place>>;
      // A value that takes characters, or a type that folds its own groups and
      // can be handed the ones that are its. What cannot be told this way is a
      // type that wants its groups when the match is over: a fold has no views
      // of the subject to give it.
      return !gathers_by_its_groups<stands_for> || folds_by_turns<stands_for>;
    }());
  }(std::make_index_sequence<groups_of_output<std::remove_cv_t<type>>()>{});
}

// The same question, asked of what is inside an output rather than of the
// output itself.
//
// A type read by the machine that gathers is being built out of its places,
// however it looks to whatever contains it -- a shape that is one value to its
// parent is a product of places to itself. Asking the question of the type
// would ask how that type is read, and the answer to that is the machine doing
// the asking.
template <class type>
[[nodiscard]] consteval bool a_flat_reader_inside() {
  if constexpr (scanned_as_variant<type>) {
    return []<std::size_t... which>(std::index_sequence<which...>) {
      return (false || ... || holds_a_flat_reader<branch_at<type, which>>());
    }(std::make_index_sequence<branch_count<type>()>{});
  } else if constexpr (scanned_as_range<type>) {
    return holds_a_flat_reader<
        std::remove_cvref_t<std::ranges::range_value_t<type>>>();
  } else {
    return []<std::size_t... field>(std::index_sequence<field...>) {
      return (false || ... ||
              holds_a_flat_reader<
                  typename parts_of<type>::template at<field>>());
    }(std::make_index_sequence<parts_of<type>::count>{});
  }
}


// A leaf is read from its one group; a product is built from its fields, each
// of which takes as many groups as it needs, in order. Nothing about the
// nesting is written in the format: a structure of structures is spelled out
// flat, because a product of products is flat.
// The parameters come from the same spread the automaton was built from, so a
// colon written at the place a value is read from reaches that value however
// deeply it sits, and a format declared by a type is read against that type
// here exactly as it was there.
// What a leaf was given after the colon, where anything was. A format says it
// per place; a pattern written by hand says nothing at all.
template <class root, fixed_string format>
struct format_parameters {
  [[nodiscard]] static constexpr std::string_view at(std::size_t place) {
    static constexpr auto spread = spread_of<root, format>();
    return spread.parameters[place].view();
  }
};

// The first of these that did not read, if any did not. Written once because
// every shape that is made of parts asks it: a product, and a type made by the
// call it named.
template <class failure_type, class... parts>
[[nodiscard]] constexpr std::optional<failure_type> what_went_wrong(
    std::tuple<parts...>& read) {
  std::optional<failure_type> went_wrong;
  [&]<std::size_t... at>(std::index_sequence<at...>) {
    ((void)[&] {
      if (went_wrong || std::get<at>(read)) return;
      went_wrong = std::move(std::get<at>(read)).error();
    }(), ...);
  }(std::index_sequence_for<parts...>{});
  return went_wrong;
}

struct no_parameters {
  [[nodiscard]] static constexpr std::string_view at(std::size_t) { return {}; }
};

// The groups as a span and not as an array of a known length: the reading
// hands over all of them, and a type that reads its own groups hands over the
// few that are its. Both are the same thing to whoever reads them.
// Whether putting this value together out of groups can go wrong at all.
//
// Most readings cannot. A value whose scanner throws rather than hands a
// failure back throws past all of this; a value whose scanner does neither
// cannot fail; a product of such values cannot fail. What can are the readings
// that have something to say: a scanner that hands a failure back, a choice
// where no branch may have run, a list, and a type built from its own groups.
//
// Where nothing can, the value is built as it was before any of this: straight
// into the aggregate, with no `expected` held anywhere along the way. That is
// the path most scans take and it costs what it used to.
template <class type, bool as_output = false>
[[nodiscard]] consteval bool never_fails() {
  constexpr bool a_value = scanned_as_leaf<type> && !as_output;
  if constexpr (a_value && reads_its_own_groups<type>) {
    return false;
  } else if constexpr (a_value) {
    return !scan::says_what_went_wrong<std::remove_cv_t<type>>;
  } else if constexpr (scanned_as_variant<type>) {
    return false;
  } else if constexpr (scanned_as_range<type>) {
    return false;
  } else {
    return []<std::size_t... index>(std::index_sequence<index...>) {
      return (true && ... &&
              never_fails<typename parts_of<type>::template at<index>>());
    }(std::make_index_sequence<parts_of<type>::count>{});
  }
}

// The value itself, for a reading that cannot go wrong.
template <class parameters, class type, std::size_t offset,
          bool as_output = false>
[[nodiscard]] constexpr type built_value(
    std::span<const std::string_view> groups) {
  constexpr bool a_value = scanned_as_leaf<type> && !as_output;
  if constexpr (a_value) {
    return scanner_parse<std::remove_cv_t<type>>(groups[offset],
                                                 parameters::at(offset));
  } else if constexpr (scanned_from_values<type>) {
    return [&]<std::size_t... index>(std::index_sequence<index...>) {
      return scan::scanner<std::remove_cv_t<type>>{}.parse(
          built_value<parameters, typename parts_of<type>::template at<index>,
                      offset + groups_before_field<type, index>()>(groups)...);
    }(std::make_index_sequence<parts_of<type>::count>{});
  } else {
    return [&]<std::size_t... index>(std::index_sequence<index...>) {
      return type{
          built_value<parameters, typename parts_of<type>::template at<index>,
                      offset + groups_before_field<type, index>()>(groups)...};
    }(std::make_index_sequence<parts_of<type>::count>{});
  }
}

template <class failure_type, class parameters, class type,
          std::size_t offset, bool as_output = false>
[[nodiscard]] constexpr std::expected<type, failure_type> build_value(
    std::span<const std::string_view> groups) {
  // Where this is the whole of what is being read, a shape that reads its own
  // groups is a product of places rather than a value in a place.
  constexpr bool a_value = scanned_as_leaf<type> && !as_output;
  if constexpr (never_fails<type, as_output>()) {
    // Nothing here can hand a failure back, so nothing here holds one.
    return built_value<parameters, type, offset, as_output>(groups);
  } else if constexpr (a_value && reads_its_own_groups<type>) {
    // The type's own groups are groups of this match, already found. It is
    // handed them, or told which of them each character belongs to -- the same
    // reading it gets where a subject arrives as it is read, so it reads the
    // same way in both places.
    using held = std::remove_cv_t<type>;
    constexpr std::size_t inside = groups_a_leaf_opens<held>();
    if constexpr (scan::says_what_went_wrong_from_groups<held> ||
                  requires(std::span<const std::string_view> given) {
                    scan::scanner<held>{}.from_groups(given);
                  }) {
      std::array<std::string_view, inside> theirs{};
      [&]<std::size_t... at>(std::index_sequence<at...>) {
        ((theirs[at] = groups[offset + 1 + at]), ...);
      }(std::make_index_sequence<inside>{});
      const auto given = std::span<const std::string_view>(theirs);
      if constexpr (scan::says_what_went_wrong_from_groups<held>) {
        auto got = scan::scanner<held>{}.try_from_groups(given);
        if (got) return std::move(*got);
        return std::unexpected(
            scan::as_a_failure<failure_type>(std::move(got).error()));
      } else {
        return scan::scanner<held>{}.from_groups(given);
      }
    } else {
      auto state = scan::scanner<held>{}.begin_groups();
      [&]<std::size_t... at>(std::index_sequence<at...>) {
        ((void)[&] {
          // A group that took no part in the match is not opened at all, which
          // is how the type is told it was not there.
          if (groups[offset + 1 + at].data() == nullptr) return;
          open_one_group<held, at>(state);
          close_one_group<held, at>(state, groups[offset + 1 + at]);
        }(), ...);
      }(std::make_index_sequence<inside>{});
      if constexpr (scan::says_what_went_wrong_folding<held>) {
        auto got = scan::scanner<held>{}.try_finish_groups(std::move(state));
        if (got) return std::move(*got);
        return std::unexpected(
            scan::as_a_failure<failure_type>(std::move(got).error()));
      } else {
        return scan::scanner<held>{}.finish_groups(std::move(state));
      }
    }
  } else if constexpr (a_value) {
    return parse_value<std::remove_cv_t<type>, failure_type>(
        groups[offset], parameters::at(offset));
  } else if constexpr (scanned_as_variant<type>) {
    // Exactly one branch ran, and its mark says so: a mark that took part
    // points into the subject, and the others point nowhere.
    return [&]<std::size_t... branch>(std::index_sequence<branch...>)
               -> std::expected<type, failure_type> {
      std::optional<std::expected<type, failure_type>> made;
      const auto take = [&]<std::size_t which>() {
        constexpr std::size_t mark = offset + groups_before_branch<type, which>();
        if (made || groups[mark].data() == nullptr) return;
        using alternative = branch_at<type, which>;
        auto part =
            build_value<failure_type, parameters, alternative, mark + 1>(groups);
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
    // Made by the call it named, out of the values its places stood for. Each
    // of them is read first and the call is made after, because a value that
    // did not read is not an argument.
    return [&]<std::size_t... index>(std::index_sequence<index...>)
               -> std::expected<type, failure_type> {
      auto parts = std::tuple{
          build_value<failure_type, parameters,
                      typename parts_of<type>::template at<index>,
                      offset + groups_before_field<type, index>()>(groups)...};
      if (auto went_wrong = what_went_wrong<failure_type>(parts)) {
        return std::unexpected(std::move(*went_wrong));
      }
      return scan::scanner<std::remove_cv_t<type>>::parse(
          std::move(*std::get<index>(parts))...);
    }(std::make_index_sequence<parts_of<type>::count>{});
  } else {
    return [&]<std::size_t... index>(std::index_sequence<index...>)
               -> std::expected<type, failure_type> {
      auto parts = std::tuple{
          build_value<failure_type, parameters,
                      typename parts_of<type>::template at<index>,
                      offset + groups_before_field<type, index>()>(groups)...};
      if (auto went_wrong = what_went_wrong<failure_type>(parts)) {
        return std::unexpected(std::move(*went_wrong));
      }
      return type{std::move(*std::get<index>(parts))...};
    }(std::make_index_sequence<parts_of<type>::count>{});
  }
}

[[nodiscard]] constexpr bool is_regex_meta(char value) {
  return std::string_view(".^$|()[]*+?{}\\").contains(value);
}

constexpr void append_literal(pattern_buffer<>& output, char value) {
  if (is_regex_meta(value)) output.push_back('\\');
  output.push_back(value);
}

[[nodiscard]] constexpr std::size_t find_capture_end(
    std::string_view format, std::size_t position, std::size_t depth = 0) {
  if (position == format.size()) throw "unterminated aggregate capture";
  if (format[position] == '\\') {
    return find_capture_end(format, position + 2, depth);
  }
  if (format[position] == '{') {
    return find_capture_end(format, position + 1, depth + 1);
  }
  if (format[position] == '}') {
    return depth == 0
               ? position
               : find_capture_end(format, position + 1, depth - 1);
  }
  return find_capture_end(format, position + 1, depth);
}

template <class type, fixed_string format, fixed_string opening,
          std::size_t field_count>
constexpr void append_aggregate_pattern(
    pattern_buffer<>& output,
    const std::array<std::string_view, field_count>& defaults,
    std::size_t position = 0, std::size_t field = 0) {
  const std::string_view text = format.view();
  if (position == text.size()) {
    if (field != field_count) throw "aggregate format field count mismatch";
    return;
  }
  if (text[position] == '\\') {
    if (position + 1 == text.size()) throw "dangling aggregate escape";
    append_literal(output, text[position + 1]);
    append_aggregate_pattern<type, format, opening>(output, defaults,
                                                    position + 2, field);
    return;
  }
  if (text[position] != '{') {
    append_literal(output, text[position]);
    append_aggregate_pattern<type, format, opening>(output, defaults,
                                                    position + 1, field);
    return;
  }
  if (field == field_count) throw "too many aggregate captures";
  const std::size_t end = find_capture_end(text, position + 1);
  output.append(opening.view());
  if (end == position + 1 || text[position + 1] == ':') {
    output.append(defaults[field]);
  } else {
    output.append(text.substr(position + 1, end - position - 1));
  }
  output.push_back(')');
  append_aggregate_pattern<type, format, opening>(output, defaults, end + 1,
                                                  field + 1);
}

template <class type, fixed_string format, fixed_string opening = "(?:">
[[nodiscard]] consteval pattern_buffer<> make_aggregate_pattern() {
  constexpr std::size_t field_count = boost::pfr::tuple_size_v<type>;
  constexpr auto parameters = field_parameters<format, field_count>();
  constexpr auto pattern_storage = parameterized_patterns<type>(
      parameters, std::make_index_sequence<field_count>{});
  const auto defaults = pattern_views(pattern_storage);
  pattern_buffer<> output;
  append_aggregate_pattern<type, format, opening>(output, defaults);
  return output;
}


// The same pattern, written so that the values are groups.
//
// What a type declares for itself is written to match and nothing else, so its
// places are `(?:...)`: the format layer puts its own marks around them and
// has no use for groups. A pattern written by hand has no marks, so where one
// stands for a type the values have to be groups -- and this is that pattern.
template <class type>
[[nodiscard]] consteval pattern_buffer<> capturing_pattern() {
  return make_aggregate_pattern<
      std::remove_cv_t<type>,
      scan::scanner<std::remove_cv_t<type>>::scan_format, "(">();
}

// The same, for whoever already has the format in hand.
template <class type, fixed_string format>
[[nodiscard]] consteval pattern_buffer<> capturing_pattern() {
  return make_aggregate_pattern<std::remove_cv_t<type>, format, "(">();
}


template <fixed_string pattern>
inline constexpr auto regex_automaton = pack_regex_tdfa<pattern>();


}  // namespace scan::detail

export namespace scan {

// How many groups a type's own pattern opens.
//
// Said out loud because a type built from its groups may want to hand some of
// them on: a shape whose field is another shape has that field's groups inside
// its own, and it can only pass them along if it knows how many there are.
// What is counted is the pattern the type declares, which is the pattern the
// match was made with.
template <class type>
[[nodiscard]] consteval std::size_t groups_in() {
  return detail::groups_a_leaf_opens<std::remove_cv_t<type>>();
}

}  // namespace scan

