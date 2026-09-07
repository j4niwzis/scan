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

// A subject that is a string literal: an array of characters that cannot be
// written to, and so an array the compiler put a nul at the end of. A buffer
// somebody reads into is an array too, and a mutable one -- how much of it was
// filled is a thing only its owner knows.
template <class range_type>
concept literal_char_range =
    std::is_array_v<std::remove_reference_t<range_type>> &&
    std::is_const_v<std::remove_extent_t<std::remove_reference_t<range_type>>>;

// A subject that lies in a row, as the characters it stands for.
//
// A string literal is an array with a nul at the end of it, and nobody writing
// one means that character to be part of the subject: `scan<"{}">("450")` would
// be a scan of four characters otherwise, and would say the pattern does not
// match rather than what is wrong. So a trailing nul is left out of it.
template <class range_type>
[[nodiscard]] constexpr std::string_view characters_of(range_type&& input) {
  const char* const from = std::ranges::data(input);
  std::size_t many = std::ranges::size(input);
  // Only an array that cannot be written to, which is what a literal is. A
  // buffer somebody reads into is as long as it says it is: how much of it was
  // filled is a thing only its owner knows, and guessing at it here would
  // quietly read a different subject than the one handed over.
  if constexpr (literal_char_range<range_type>) {
    if (many != 0 && from[many - 1] == '\0') --many;
  }
  return std::string_view(from, many);
}

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

// Whether a type is a choice between several. Asked outright rather than by
// whether `variant_size` says anything about it: that one is a template with
// no definition for anything else, and asking it about a plain type is not a
// question that comes back false.
template <class type>
inline constexpr bool a_choice_of_kinds = false;
template <class... kinds>
inline constexpr bool a_choice_of_kinds<std::variant<kinds...>> = true;

// A list of types, and the two things ever done to one: put another list on the
// end of it, and drop what is already in it.
template <class... kinds>
struct kind_list {};

template <class left, class right>
struct joined_lists;
template <class... left, class... right>
struct joined_lists<kind_list<left...>, kind_list<right...>> {
  using type = kind_list<left..., right...>;
};

// Dropping what is already in a list, by asking the compiler instead of by
// comparing every type with every other.
//
// Each type adds an overload to a chain of bases, and a type already in the
// chain adds one that cannot be told from the one there -- so whether it is
// new is one overload resolution rather than a walk down the list. The idea is
// the `type_set` in do_let_is (examples/src/type_set.h); this is that, in this
// library's names and cut down to the one thing wanted here.
struct nothing_seen_yet {
  static consteval void seen();
};

template <class first = nothing_seen_yet, class rest = nothing_seen_yet>
struct seen_before : rest {
  using rest::seen;
  static consteval void seen(kind_list<first>)
    requires true;
  static consteval void seen(kind_list<first>)
    requires(requires { rest::seen(kind_list<first>{}); });

  template <class next>
  constexpr auto operator|(kind_list<next>) const {
    constexpr auto with = seen_before<next, seen_before>{};
    if constexpr (requires { with.seen(kind_list<next>{}); }) {
      return with;
    } else {
      return *this;
    }
  }
};

template <class chain>
struct chain_as_a_list;
template <class first, class rest>
struct chain_as_a_list<seen_before<first, rest>> {
  using type = typename joined_lists<kind_list<first>,
                                     typename chain_as_a_list<rest>::type>::type;
};
template <>
struct chain_as_a_list<seen_before<nothing_seen_yet, nothing_seen_yet>> {
  using type = kind_list<>;
};

template <class list>
struct without_repeats;
template <class... kinds>
struct without_repeats<kind_list<kinds...>> {
  using type = typename chain_as_a_list<
      decltype((seen_before<>{} | ... | kind_list<kinds>{}))>::type;
};

template <class list>
struct as_a_variant;
template <class... kinds>
struct as_a_variant<kind_list<kinds...>> {
  using type = std::variant<kinds...>;
};

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
  // Nothing written after the colon is empty parameters, and a scanner that
  // only takes them says the same thing when handed nothing.
  if constexpr (requires { scanner<type>{}.begin(); }) {
    return scanner<type>{}.begin();
  } else {
    return scanner<type>{}.begin(std::string_view{});
  }
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

// A run of characters that all belong to the same value.
//
// A walk over characters that lie in a row steps over a run of them at once --
// sixteen to a comparison -- and then has to hand them to whoever is gathering.
// Handing them one at a time gives back everything the step saved: a call a
// character, into a scanner that will do the same thing to all of them. So a
// scanner may say it takes a run, by taking a view of one, and the whole of it
// arrives in a single call. One that says nothing is handed the characters one
// at a time, exactly as before.
template <class type, class state_type>
constexpr void scanner_push_run(state_type& state, const char* from,
                                const char* to) {
  if constexpr (requires {
                  scanner<type>{}.push(state, std::string_view{});
                }) {
    scanner<type>{}.push(
        state, std::string_view(from, static_cast<std::size_t>(to - from)));
  } else {
    for (const char* letter = from; letter != to; ++letter) {
      scanner<type>{}.push(state, *letter);
    }
  }
}

// Said below, and used here: what a scanner handed back, thrown where somebody
// asked for the value itself.
template <class error_type>
[[noreturn]] void throw_what_went_wrong(error_type&& said);

template <class type, class state_type>
[[nodiscard]] constexpr type scanner_finish(state_type state) {
  // Asked for the value where the scanner hands failures back: what it handed
  // back is thrown, here at the asking, and caught nowhere. Whoever wants it
  // handed back asks the reading to try rather than to say.
  if constexpr (requires {
                  scanner<type>{}.finish(std::move(state));
                }) {
    return scanner<type>{}.finish(std::move(state));
  } else {
    auto got = scanner<type>::try_finish(std::move(state));
    if (got) return std::move(*got);
    throw_what_went_wrong(std::move(got).error());
  }
}

template <class type>
[[nodiscard]] constexpr type scanner_parse(std::string_view input) {
  if constexpr (requires { scanner<type>{}.parse(input); }) {
    return scanner<type>{}.parse(input);
  } else {
    auto got = scanner<type>::try_parse(input);
    if (got) return std::move(*got);
    throw_what_went_wrong(std::move(got).error());
  }
}

template <class type>
[[nodiscard]] constexpr type scanner_parse(std::string_view input,
                                           std::string_view parameters) {
  if constexpr (requires { scanner<type>{}.parse(input, parameters); }) {
    return scanner<type>{}.parse(input, parameters);
  } else if constexpr (requires {
                         scanner<type>::try_parse(input, parameters);
                       }) {
    auto got = scanner<type>::try_parse(input, parameters);
    if (got) return std::move(*got);
    throw_what_went_wrong(std::move(got).error());
  } else {
    if (!parameters.empty()) throw "scanner does not accept parameters";
    return scanner_parse<type>(input);
  }
}

// A scanner that says what went wrong rather than throwing it.
//
//   static std::expected<weight, too_heavy> try_parse(std::string_view);
//
// Where a scanner has this, it is what is called, and the kind of failure it
// hands back is read off its return type -- nothing has to be declared and
// nobody has to keep a list in step with the code. Where a value is asked for
// rather than tried, the failure is thrown, which is why the kinds handed back
// this way are `scan_error`s like the rest.
template <class type>
concept says_what_went_wrong = requires(std::string_view text) {
  scanner<std::remove_cv_t<type>>::try_parse(text);
} || requires(std::string_view text, std::string_view parameters) {
  scanner<std::remove_cv_t<type>>::try_parse(text, parameters);
};

template <class type>
[[nodiscard]] constexpr auto scanner_try_parse(std::string_view input,
                                               std::string_view parameters) {
  using held = std::remove_cv_t<type>;
  if constexpr (requires { scanner<held>::try_parse(input, parameters); }) {
    return scanner<held>::try_parse(input, parameters);
  } else {
    if (!parameters.empty()) throw "scanner does not accept parameters";
    return scanner<held>::try_parse(input);
  }
}

// The kind of failure such a scanner hands back.
template <class type>
using went_wrong_with =
    typename decltype(scanner_try_parse<type>(std::string_view{},
                                              std::string_view{}))::error_type;

// What is thrown for a scanner that handed a failure back, where somebody
// asked for the value itself. Where it said one kind, that kind; where it said
// several -- an `expected` over a variant of them -- whichever one it is.
template <class error_type>
[[noreturn]] void throw_what_went_wrong(error_type&& said) {
  if constexpr (a_choice_of_kinds<std::remove_cvref_t<error_type>>) {
    std::visit([](auto&& one) -> void { throw std::move(one); },
               std::forward<error_type>(said));
    throw scan_error("a failure that said it was nothing");
  } else {
    throw std::forward<error_type>(said);
  }
}

template <class type>
using scanner_state_t = decltype(scanner_begin<type>());

// The rest of the user's functions, each in the same two shapes: the one that
// hands a failure back and the one that does not. Where both are there the
// first is used, and the kind it hands back joins the list a reading of that
// output can fail with.
// Whether the type gathers a character at a time at all. Asked first, because
// what it gathers into is what the question below is about, and a type with no
// gathering has no such thing to name.
template <class type>
concept gathers_as_it_reads = requires {
  scanner<std::remove_cv_t<type>>{}.begin();
} || requires(std::string_view parameters) {
  scanner<std::remove_cv_t<type>>{}.begin(parameters);
};

template <class type>
concept says_what_went_wrong_finishing =
    gathers_as_it_reads<type> && requires(scanner_state_t<type> state) {
      scanner<std::remove_cv_t<type>>::try_finish(std::move(state));
    };

template <class type>
concept says_what_went_wrong_from_groups =
    requires(std::span<const std::string_view> given) {
      scanner<std::remove_cv_t<type>>{}.try_from_groups(given);
    };

template <class type>
concept says_what_went_wrong_folding = requires {
  scanner<std::remove_cv_t<type>>{}.try_finish_groups(
      scanner<std::remove_cv_t<type>>{}.begin_groups());
};

template <class type>
using went_wrong_finishing = typename decltype(scanner<std::remove_cv_t<type>>::
                                                   try_finish(
                                                       std::declval<
                                                           scanner_state_t<
                                                               type>>()))::
    error_type;

template <class type>
using went_wrong_from_groups =
    typename decltype(scanner<std::remove_cv_t<type>>{}.try_from_groups(
        std::declval<std::span<const std::string_view>>()))::error_type;

template <class type>
using went_wrong_folding =
    typename decltype(scanner<std::remove_cv_t<type>>{}.try_finish_groups(
        scanner<std::remove_cv_t<type>>{}.begin_groups()))::error_type;

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

// A failure handed back, holding the kind it is.
//
// A hierarchy cannot be held by value -- a base keeps the message and drops
// the kind -- so what is handed back is a choice between the kinds themselves,
// and only the ones anything ever hands back. `field_error` is a name for
// catching two things and is handed back by nothing, so nothing here is ever
// it. `scan_error` is on the list because a scanner of your own can hand that
// one back, and because a reading asked for rather than tried for throws these
// and a scanner of your own may throw that.
//
// The kinds this library itself hands back.
using our_kinds =
    kind_list<scan_error, no_match, no_group, bad_field, out_of_range,
              wrong_subject>;

using failure = typename as_a_variant<our_kinds>::type;

// What it said, whichever kind it is.
template <class... kinds>
[[nodiscard]] const char* what(const std::variant<kinds...>& said) {
  return std::visit([](const scan_error& one) { return one.what(); }, said);
}

// The value, or the failure thrown.
//
// The only place in this library where a reading that went wrong becomes a
// throw, and nothing anywhere catches it. Asking for a value has nowhere to
// put a failure; trying for one does, and then nothing is thrown at all.
template <class type, class failure_type>
[[nodiscard]] constexpr type or_thrown(std::expected<type, failure_type> got) {
  if (got) return std::move(*got);
  throw_what_went_wrong(std::move(got).error());
}

// One kind of failure said as another: what a scanner handed back, put into the
// list of everything the reading it belongs to can hand back. Where a scanner
// says several kinds it hands back a variant of them, and then which one it is
// is what that variant says.
//
// Nothing here catches anything. A reading that is tried rather than asked for
// hands its failure back the whole way up, and a scanner that throws instead
// throws past all of this, to whoever called.
template <class failure_type, class error_type>
[[nodiscard]] constexpr failure_type as_a_failure(error_type&& said) {
  if constexpr (a_choice_of_kinds<std::remove_cvref_t<error_type>>) {
    return std::visit(
        [](auto&& one) -> failure_type { return failure_type(std::move(one)); },
        std::forward<error_type>(said));
  } else {
    return failure_type(std::forward<error_type>(said));
  }
}

}  // namespace scan
