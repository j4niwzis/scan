export module scan.core;

import std;

export namespace scan::detail {

// An input whose characters lie in a row and whose length is known, which is
// what a view of a subject is made from.
// Input that arrives in pieces: a reading of readings, each of them
// characters in a row. A block of a file, a datagram, a page -- the shape
// almost every real subject has.
template <class range_type>
concept piecewise_char_range =
    std::ranges::input_range<range_type> &&
    requires(std::ranges::range_reference_t<range_type> piece) {
      { std::ranges::data(piece) } -> std::convertible_to<const char*>;
      { std::ranges::size(piece) } -> std::convertible_to<std::size_t>;
    };

template <class range_type>
concept contiguous_char_range =
    std::ranges::contiguous_range<range_type> &&
    std::ranges::sized_range<range_type> &&
    std::same_as<std::ranges::range_value_t<range_type>, char>;

}  // namespace scan::detail

export namespace scan {

template <std::size_t extent>
struct fixed_string {
  char value[extent]{};
  // Whether the reading this pattern is built for is anchored to the end of
  // the subject.
  //
  // It is part of the pattern rather than a parameter beside it because
  // everything built from a pattern -- the automaton, and every table of
  // states and runs and classes that names it -- is keyed by this value. Two
  // readings of the same characters under different rules are two patterns
  // here, and nothing that answers questions about one can be handed the
  // answers about the other by mistake.
  //
  // The rule itself is leftmost-first, and the difference is only whether a
  // match ends the reading. Where it does, the walks below it have lost and
  // are cut in determinization. Where the end of the subject has to be
  // reached, they are kept: one of them may be the only walk that gets there,
  // which is `a|ab` reading "ab".
  bool anchored = false;
  // Whether every place in this format begins past whatever whitespace is in
  // front of it.
  //
  // This is what `%d` does and `{}` does not, and writing it out -- `{*\s*}`
  // before every place -- says the same thing four times in a format with four
  // fields. Said here it is said once, and it is said in the key: a format
  // that skips and a format that does not are two formats, so nothing built
  // from one can be handed to a reading of the other.
  bool space_before_places = false;

  consteval fixed_string(const char (&text)[extent]) { std::copy_n(text, extent, value); }

  [[nodiscard]] constexpr fixed_string to_the_end() const {
    fixed_string made = *this;
    made.anchored = true;
    return made;
  }

  [[nodiscard]] constexpr fixed_string past_space() const {
    fixed_string made = *this;
    made.space_before_places = true;
    return made;
  }

  [[nodiscard]] static consteval std::size_t size() { return extent - 1; }
  [[nodiscard]] constexpr char operator[](std::size_t index) const {
    return value[index];
  }
  [[nodiscard]] constexpr std::string_view view() const {
    return {value, size()};
  }
};

// How the walk over a subject held in memory is chosen.
//
// A subject long enough for words is read faster in them, and a short one is
// read faster a character at a time -- so the reading asks how much there is
// and picks, which costs one comparison. Where the caller knows which one they
// want, they say it, and then nothing is asked and nothing is spent: the
// length is not looked at, and the walk that was not chosen is not written.
enum class how_to_walk {
  by_length,      // ask once, and pick
  one_at_a_time,  // a character at a time, whatever the length
  in_words,       // in words and vectors, whatever the length
};

template <class type>
struct scanner;

// A type that is one of several, and which one is what the reading says.
//
// `std::variant` is the one everybody has, and it is the one this is written
// for -- but nothing about a sum type is peculiar to it. Three questions are
// asked of one: how many alternatives, which type the k-th is, and how to make
// the whole thing holding a value of that k-th. A type that answers them is a
// sum here, whoever wrote it:
//
//   template <class... parts>
//   struct scan::branches<my_either<parts...>> {
//     static constexpr std::size_t count = sizeof...(parts);
//     template <std::size_t which>
//     using at = std::tuple_element_t<which, std::tuple<parts...>>;
//     template <std::size_t which, class value>
//     static constexpr my_either<parts...> make(value&& one) { … }
//   };
template <class type>
struct branches;

template <class... alternatives>
struct branches<std::variant<alternatives...>> {
  static constexpr std::size_t count = sizeof...(alternatives);

  template <std::size_t which>
  using at = std::variant_alternative_t<which, std::variant<alternatives...>>;

  template <std::size_t which, class value>
  [[nodiscard]] static constexpr std::variant<alternatives...> make(
      value&& one) {
    return std::variant<alternatives...>(std::in_place_index<which>,
                                         std::forward<value>(one));
  }
};

// A group that is nothing but a mark.
//
// A variant standing in a format takes one group for each branch, around what
// that branch reads, and which of those took part is how the reading says
// which branch the input went down. Nothing is read out of the mark itself, so
// nothing gathers it and nothing is kept.
namespace detail {
struct branch_mark {};
}  // namespace detail

template <>
struct scanner<detail::branch_mark> {
  static constexpr int begin() { return 0; }
  static constexpr void push(int&, char) {}
  static constexpr detail::branch_mark finish(int) { return {}; }
};

template <class customization>
struct scanner_target;

template <class type>
struct scanner_target<scanner<type>> {
  using type_t = type;
};

template <class customization>
using scanner_target_t = typename scanner_target<
    std::remove_cvref_t<customization>>::type_t;

template <class type>
inline constexpr auto scanner_pattern_value = [] {
  constexpr scanner<type> customization;
  if constexpr (requires { customization.pattern(); }) {
    return customization.pattern();
  } else {
    return customization.pattern;
  }
}();

template <class type>
[[nodiscard]] constexpr const auto& scanner_pattern() {
  return scanner_pattern_value<type>;
}

template <class type>
[[nodiscard]] constexpr auto scanner_pattern(std::string_view parameters) {
  constexpr scanner<type> customization;
  if constexpr (requires { customization.pattern(parameters); }) {
    return customization.pattern(parameters);
  } else {
    if (!parameters.empty()) throw "scanner does not accept parameters";
    return scanner_pattern<type>();
  }
}

// The number of a group, said as a type.
//
// A group is known by its number, and a number is not a thing you can overload
// on. This is that number said so that you can: write one `push_group` per
// group and let the compiler pick, instead of switching on a value inside one.
// The number is still there for whoever wants it.
template <std::size_t which>
struct group_at {
  static constexpr std::size_t value = which;
};

template <class type>
[[nodiscard]] constexpr auto scanner_begin() {
  return scanner<type>{}.begin();
}

template <class type>
[[nodiscard]] constexpr auto scanner_begin(std::string_view parameters) {
  if constexpr (requires { scanner<type>{}.begin(parameters); }) {
    return scanner<type>{}.begin(parameters);
  } else {
    if (!parameters.empty()) throw "scanner does not accept parameters";
    return scanner_begin<type>();
  }
}

template <class type, class state_type>
constexpr void scanner_push(state_type& state, char value) {
  scanner<type>{}.push(state, value);
}

template <class type, class state_type>
[[nodiscard]] constexpr type scanner_finish(state_type state) {
  return scanner<type>{}.finish(std::move(state));
}

template <class type>
[[nodiscard]] constexpr type scanner_parse(std::string_view input) {
  return scanner<type>{}.parse(input);
}

template <class type>
[[nodiscard]] constexpr type scanner_parse(std::string_view input,
                                           std::string_view parameters) {
  if constexpr (requires { scanner<type>{}.parse(input, parameters); }) {
    return scanner<type>{}.parse(input, parameters);
  } else {
    if (!parameters.empty()) throw "scanner does not accept parameters";
    return scanner_parse<type>(input);
  }
}

template <class type>
using scanner_state_t = decltype(scanner_begin<type>());

template <std::size_t capacity = 8192>
struct pattern_buffer {
  std::array<char, capacity> storage{};
  std::size_t length = 0;

  constexpr void push_back(char value) {
    if (length == capacity) throw "scanner pattern is too large";
    storage[length++] = value;
  }

  constexpr void append(std::string_view value) {
    for (char symbol : value) { push_back(symbol); }
  }

  [[nodiscard]] constexpr std::string_view view() const noexcept {
    return {storage.data(), length};
  }

  [[nodiscard]] constexpr operator std::string_view() const noexcept {
    return view();
  }
};

// What went wrong, said by the type and not by a string.
//
// The text is a literal and is held as a pointer to it, so making one of these
// allocates nothing -- which matters, because everything else in this library
// allocates nothing either, and a failure is not the moment to start. It is
// also why this is an `std::exception` and not an `std::runtime_error`: the
// latter keeps its message in a `std::string`.
//
// The kinds below are what is caught. Catching `scan_error` catches all of
// them, which is what most callers want; catching one of the kinds says which
// question you are answering -- "is this line of the right shape" is a
// different question from "does this number fit", and only the first is worth
// trying the next line after.
class scan_error : public std::exception {
 public:
  constexpr explicit scan_error(const char* said) noexcept : said_(said) {}

  [[nodiscard]] const char* what() const noexcept override { return said_; }

 private:
  const char* said_;
};

// The subject is not what the pattern says it is: nothing matched, or nothing
// matched at the head, or no branch of a format took it.
class no_match : public scan_error {
 public:
  using scan_error::scan_error;
};

// A value was asked for out of a group that took no part in the match. The
// match was fine; this group of it was not there.
class no_group : public scan_error {
 public:
  using scan_error::scan_error;
};

// Something about a field, and never thrown itself: the two below are what is
// thrown, and this is the name for catching either.
class field_error : public scan_error {
 public:
  using scan_error::scan_error;
};

// A place matched, and what stood there is not that type: `abc` where an
// integer was written, an empty field where one character was.
class bad_field : public field_error {
 public:
  using field_error::field_error;
};

// It is that type, and it does not fit in it. A different question from the one
// above, and usually a different answer: the input is well formed and the
// output type is too small for it.
class out_of_range : public field_error {
 public:
  using field_error::field_error;
};

// The reading that was asked for cannot be had off this kind of subject -- a
// fold that takes its groups whole, asked to read a stream, where there is
// nothing to point at and holding the characters would be a hold with no
// bound.
class wrong_subject : public scan_error {
 public:
  using scan_error::scan_error;
};

// A failure handed back rather than thrown, holding the kind it was.
//
// Thrown, the kind is the type and `catch` picks it. Handed back, there is
// nowhere to put a hierarchy: a base by value keeps the message and drops the
// kind. So what is handed back is one of each kind that is ever thrown, and
// only those -- `field_error` is a name for catching two things and is never
// thrown itself, so nothing here is ever it. `scan_error` is on the list
// because a scanner of your own throws that one.
using failure =
    std::variant<scan_error, no_match, no_group, bad_field, out_of_range,
                 wrong_subject>;

// What it said, whichever kind it is.
[[nodiscard]] inline const char* what(const failure& said) {
  return std::visit([](const scan_error& one) { return one.what(); }, said);
}

// The kinds, tried from the bottom of the hierarchy up, so the answer is the
// kind that was thrown and not one of its names.
//
// Written once here rather than at every place that hands a failure back: they
// all catch the same six things in the same order, and getting that order
// wrong turns an `out_of_range` into a `field_error` quietly.
template <class function>
[[nodiscard]] constexpr auto caught(function&& run)
    -> std::expected<decltype(run()), failure> {
  try {
    return std::forward<function>(run)();
  } catch (const out_of_range& said) {
    return std::unexpected(failure(said));
  } catch (const bad_field& said) {
    return std::unexpected(failure(said));
  } catch (const no_match& said) {
    return std::unexpected(failure(said));
  } catch (const no_group& said) {
    return std::unexpected(failure(said));
  } catch (const wrong_subject& said) {
    return std::unexpected(failure(said));
  } catch (const scan_error& said) {
    return std::unexpected(failure(said));
  }
}

}  // namespace scan
