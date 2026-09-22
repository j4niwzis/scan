export module scan.core;

import std;

namespace scan::detail {

// An input whose characters lie in a row and whose length is known, which is
// what a view of a subject is made from.
// Input that arrives in pieces: a reading of readings, each of them
// characters in a row. A block of a file, a datagram, a page -- the shape
// almost every real subject has.
export template <class RangeType>
concept piecewise_char_range =
    std::ranges::input_range<RangeType> &&
    requires(std::ranges::range_reference_t<RangeType> piece) {
      { std::ranges::data(piece) } -> std::convertible_to<const char*>;
      { std::ranges::size(piece) } -> std::convertible_to<std::size_t>;
    };

export template <class RangeType>
concept contiguous_char_range =
    std::ranges::contiguous_range<RangeType> &&
    std::ranges::sized_range<RangeType> &&
    std::same_as<std::ranges::range_value_t<RangeType>, char>;

// A subject that is a string literal: an array of characters that cannot be
// written to, and so an array the compiler put a nul at the end of. A buffer
// somebody reads into is an array too, and a mutable one -- how much of it was
// filled is a thing only its owner knows.
export template <class RangeType>
concept literal_char_range =
    std::is_array_v<std::remove_reference_t<RangeType>> &&
    std::is_const_v<std::remove_extent_t<std::remove_reference_t<RangeType>>>;

// A subject that lies in a row, as the characters it stands for.
//
// A string literal is an array with a nul at the end of it, and nobody writing
// one means that character to be part of the subject: `scan<"{}">("450")` would
// be a scan of four characters otherwise, and would say the pattern does not
// match rather than what is wrong. So a trailing nul is left out of it.
export template <class RangeType>
[[nodiscard]] constexpr std::string_view characters_of(RangeType&& input) {
  const char* const from = std::ranges::data(input);
  std::size_t many = std::ranges::size(input);
  // Only an array that cannot be written to, which is what a literal is. A
  // buffer somebody reads into is as long as it says it is: how much of it was
  // filled is a thing only its owner knows, and guessing at it here would
  // quietly read a different subject than the one handed over.
  if constexpr (literal_char_range<RangeType>) {
    if (many != 0 && from[many - 1] == '\0') --many;
  }
  return std::string_view(from, many);
}

}  // namespace scan::detail

namespace scan {

export template <std::size_t Extent>
struct fixed_string {
  char value[Extent]{};
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

  fixed_string() = default;
  consteval fixed_string(const char (&text)[Extent]) {
    std::copy_n(text, Extent, value);
  }

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

  [[nodiscard]] static consteval std::size_t size() { return Extent - 1; }
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
export enum class how_to_walk {
  by_length,      // ask once, and pick
  one_at_a_time,  // a character at a time, whatever the length
  in_words,       // in words and vectors, whatever the length
};

export template <class Type>
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
// How much room a container has, where it has a fixed amount of it.
//
// A list place says how many turns it may take -- `{2,5}`, or `?`, or nothing
// at all for as many as there are -- and a container written with room said in
// advance can be asked whether that many will fit. Said here rather than
// guessed: `reserve` says a container can grow, and nothing in the standard
// says one cannot.
//
//   template <> struct scan::room_for<my_small_list> {
//     static constexpr std::size_t most = 8;
//   };
//
// Where a container says it and the format could ask for more, the reading is
// refused where it is compiled. Where it says nothing, nothing is checked.
export template <class List>
struct room_for;

export template <class Type>
struct branches;

template <class... Alternatives>
struct branches<std::variant<Alternatives...>> {
  static constexpr std::size_t count = sizeof...(Alternatives);

  template <std::size_t Which>
  using at = std::variant_alternative_t<Which, std::variant<Alternatives...>>;

  template <std::size_t Which, class Value>
  [[nodiscard]] static constexpr std::variant<Alternatives...> make(
      Value&& one) {
    return std::variant<Alternatives...>(std::in_place_index<Which>,
                                         std::forward<Value>(one));
  }
};

// A group that is nothing but a mark.
//
// A variant standing in a format takes one group for each branch, around what
// that branch reads, and which of those took part is how the reading says
// which branch the input went down. Nothing is read out of the mark itself, so
// nothing gathers it and nothing is kept.
namespace detail {
export struct branch_mark {};
}  // namespace detail

template <>
struct scanner<detail::branch_mark> {
  static constexpr int begin() { return 0; }
  static constexpr void push(int&, char) {}
  static constexpr detail::branch_mark finish(int) { return {}; }
};

export template <class Customization>
struct scanner_target;

template <class Type>
struct scanner_target<scanner<Type>> {
  using type_t = Type;
};

export template <class Customization>
using scanner_target_t = typename scanner_target<
    std::remove_cvref_t<Customization>>::type_t;

export template <class Type>
inline constexpr auto scanner_pattern_value = [] {
  constexpr scanner<Type> customization;
  if constexpr (requires { customization.pattern(); }) {
    return customization.pattern();
  } else {
    return customization.pattern;
  }
}();

export template <class Type>
[[nodiscard]] constexpr const auto& scanner_pattern() {
  return scanner_pattern_value<Type>;
}

export template <class Type>
[[nodiscard]] constexpr auto scanner_pattern(std::string_view parameters) {
  constexpr scanner<Type> customization;
  if constexpr (requires { customization.pattern(parameters); }) {
    return customization.pattern(parameters);
  } else {
    if (!parameters.empty()) throw "scanner does not accept parameters";
    return scanner_pattern<Type>();
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
// Nothing at all, and the base of a failure that is handed back.
//
// A failure that inherits std::exception has a virtual destructor, and that
// makes every variant, optional and expected it is put in non-trivial -- which
// is the whole of a reading, written with a landing pad at every step. A
// failure that is only ever returned pays none of that, so the kinds below are
// told what to stand on, and what they stand on is nothing.
export struct handed_back {};

// The kinds, each of them over whatever it is told to stand on.
//
// Handed back, they stand on nothing and are values like any other. Thrown,
// they stand on std::exception and are caught the way anything else is; the
// two are different types, and the throw below turns the one into the other,
// which is a pointer copied.
export template <class Base = handed_back>
class scan_error : public Base {
 public:
  constexpr explicit scan_error(const char* said) noexcept
      : said_(said) {}

  // Overrides where the base has it to override, and is an ordinary member
  // where the base is nothing.
  [[nodiscard]] constexpr const char* what() const noexcept { return said_; }

 private:
  const char* said_;
};

// The subject is not what the pattern says it is: nothing matched, or nothing
// matched at the head, or no branch of a format took it.
export template <class Base = handed_back>
class no_match : public scan_error<Base> {
 public:
  using scan_error<Base>::scan_error;
};

// A value was asked for out of a group that took no part in the match. The
// match was fine; this group of it was not there.
export template <class Base = handed_back>
class no_group : public scan_error<Base> {
 public:
  using scan_error<Base>::scan_error;
};

// Something about a field, and never thrown itself: the two below are what is
// thrown, and this is the name for catching either.
template <class Base = handed_back>
class field_error : public scan_error<Base> {
 public:
  using scan_error<Base>::scan_error;
};

// A place matched, and what stood there is not that type: `abc` where an
// integer was written, an empty field where one character was.
export template <class Base = handed_back>
class bad_field : public field_error<Base> {
 public:
  using field_error<Base>::field_error;
};

// It is that type, and it does not fit in it. A different question from the one
// above, and usually a different answer: the input is well formed and the
// output type is too small for it.
export template <class Base = handed_back>
class out_of_range : public field_error<Base> {
 public:
  using field_error<Base>::field_error;
};

// The reading that was asked for cannot be had off this kind of subject -- a
// fold that takes its groups whole, asked to read a stream, where there is
// nothing to point at and holding the characters would be a hold with no
// bound.
export template <class Base = handed_back>
class wrong_subject : public scan_error<Base> {
 public:
  using scan_error<Base>::scan_error;
};

// The thrown kind that goes with a kind that was handed back.
//
// Every kind is a template over what it stands on, so the one that is thrown
// is the same template standing on std::exception -- which holds for a failure
// of somebody's own as much as for the ones here, so long as it is written the
// same way. A kind that is not a template at all is thrown as what it says.
template <class Kind>
struct thrown_kind {
  using type = scan_error<std::exception>;
};
template <template <class> class Kind, class Base>
struct thrown_kind<Kind<Base>> {
  using type = Kind<std::exception>;
};
template <class Kind>
using thrown_kind_t = typename thrown_kind<std::remove_cvref_t<Kind>>::type;

// Whether a type is a choice between several. Asked outright rather than by
// whether `variant_size` says anything about it: that one is a template with
// no definition for anything else, and asking it about a plain type is not a
// question that comes back false.
template <class Type>
inline constexpr bool a_choice_of_kinds = false;
export template <class... Kinds>
inline constexpr bool a_choice_of_kinds<std::variant<Kinds...>> = true;

// A list of types, and the two things ever done to one: put another list on the
// end of it, and drop what is already in it.
export template <class... Kinds>
struct kind_list {};

export template <class Left, class Right>
struct joined_lists;
template <class... Left, class... Right>
struct joined_lists<kind_list<Left...>, kind_list<Right...>> {
  using type = kind_list<Left..., Right...>;
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

template <class First = nothing_seen_yet, class Rest = nothing_seen_yet>
struct seen_before : Rest {
  using Rest::seen;
  static consteval void seen(kind_list<First>)
    requires true;
  static consteval void seen(kind_list<First>)
    requires(requires { Rest::seen(kind_list<First>{}); });

  template <class Next>
  constexpr auto operator|(kind_list<Next>) const {
    constexpr auto with = seen_before<Next, seen_before>{};
    if constexpr (requires { with.seen(kind_list<Next>{}); }) {
      return with;
    } else {
      return *this;
    }
  }
};

template <class Chain>
struct chain_as_a_list;
template <class First, class Rest>
struct chain_as_a_list<seen_before<First, Rest>> {
  using type = typename joined_lists<kind_list<First>,
                                     typename chain_as_a_list<Rest>::type>::type;
};
template <>
struct chain_as_a_list<seen_before<nothing_seen_yet, nothing_seen_yet>> {
  using type = kind_list<>;
};

export template <class List>
struct without_repeats;
template <class... Kinds>
struct without_repeats<kind_list<Kinds...>> {
  using type = typename chain_as_a_list<
      decltype((seen_before<>{} | ... | kind_list<Kinds>{}))>::type;
};

export template <class List>
struct as_a_variant;
template <class... Kinds>
struct as_a_variant<kind_list<Kinds...>> {
  using type = std::variant<Kinds...>;
};

// The number of a group, said as a type.
//
// A group is known by its number, and a number is not a thing you can overload
// on. This is that number said so that you can: write one `push_group` per
// group and let the compiler pick, instead of switching on a value inside one.
// The number is still there for whoever wants it.
export template <std::size_t Which>
struct group_at {
  static constexpr std::size_t value = Which;
};

export template <class Type>
[[nodiscard]] constexpr auto scanner_begin() {
  // Nothing written after the colon is empty parameters, and a scanner that
  // only takes them says the same thing when handed nothing.
  if constexpr (requires { scanner<Type>{}.begin(); }) {
    return scanner<Type>{}.begin();
  } else {
    return scanner<Type>{}.begin(std::string_view{});
  }
}

export template <class Type>
[[nodiscard]] constexpr auto scanner_begin(std::string_view parameters) {
  if constexpr (requires { scanner<Type>{}.begin(parameters); }) {
    return scanner<Type>{}.begin(parameters);
  } else {
    if (!parameters.empty()) throw "scanner does not accept parameters";
    return scanner_begin<Type>();
  }
}

// Whether a scanner takes its characters by handing back a fresh state rather
// than by changing the one it was given.
//
// A scanner says how it takes a character once, in one of two shapes:
//
//   state push(state, char);   // the state it hands back is the answer
//   void  push(state&, char);  // the state it was given is changed
//
// The first is written as a function of what it was given -- nothing outside
// the state is touched and nothing is left behind -- which is what lets the
// walk keep the state it had before the call and go back to it. The second is
// what a state too big to hand about wants, and is what the library's own
// scanners have always been.
//
// Asked of the shape and not of the name: taking by value accepts an lvalue as
// happily as anything else, so a scanner that only changes what it is given
// would answer yes to "can I call this with a state" either way. What tells
// them apart is what comes back -- a state, or nothing.
template <class Type, class StateType>
concept hands_the_state_back = requires(StateType held, char value) {
  { scanner<Type>{}.push(std::move(held), value) } -> std::same_as<StateType>;
};

template <class Type, class StateType>
concept changes_the_state_it_was_given =
    requires(StateType& held, char value) {
      { scanner<Type>{}.push(held, value) } -> std::same_as<void>;
    };

export template <class Type, class StateType>
constexpr void scanner_push(StateType& state, char value) {
  static_assert(!(hands_the_state_back<Type, StateType> &&
                  changes_the_state_it_was_given<Type, StateType>),
                "a scanner says how it takes a character once: either "
                "`state push(state, char)` or `void push(state&, char)`, "
                "and not both");
  if constexpr (hands_the_state_back<Type, StateType>) {
    state = scanner<Type>{}.push(std::move(state), value);
  } else {
    scanner<Type>{}.push(state, value);
  }
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
export template <class Type, class StateType>
constexpr void scanner_push_run(StateType& state, const char* from,
                                const char* to) {
  if constexpr (requires(StateType held) {
                  {
                    scanner<Type>{}.push(std::move(held), std::string_view{})
                  } -> std::same_as<StateType>;
                }) {
    state = scanner<Type>{}.push(
        std::move(state),
        std::string_view(from, static_cast<std::size_t>(to - from)));
  } else if constexpr (requires {
                         {
                           scanner<Type>{}.push(state, std::string_view{})
                         } -> std::same_as<void>;
                       }) {
    scanner<Type>{}.push(
        state, std::string_view(from, static_cast<std::size_t>(to - from)));
  } else {
    for (const char* letter = from; letter != to; ++letter) {
      scanner_push<Type>(state, *letter);
    }
  }
}

// Said below, and used here: what a scanner handed back, thrown where somebody
// asked for the value itself.
template <class ErrorType>
[[noreturn]] void throw_what_went_wrong(ErrorType&& said);

// What a scanner handed back, made into what the caller asked for.
//
// The template argument told it which way this reading goes; it was free to
// ignore that, and either of the two answers is a good answer. So the making
// is done here, where what was asked for is known, and a scanner that already
// said it the right way is not made to say it twice.
export template <class Type, class FailureType, class GotType>
[[nodiscard]] constexpr std::expected<Type, FailureType> as_handed_back(
    GotType&& got) {
  if constexpr (requires { got.error(); }) {
    if (got) return std::expected<Type, FailureType>(std::move(*got));
    return std::unexpected(as_a_failure<FailureType>(std::move(got).error()));
  } else {
    return std::expected<Type, FailureType>(std::forward<GotType>(got));
  }
}

export template <class Type, class GotType>
[[nodiscard]] constexpr Type as_thrown(GotType&& got) {
  if constexpr (requires { got.error(); }) {
    if (!got) throw_what_went_wrong(std::move(got).error());
    return std::move(*got);
  } else {
    return std::forward<GotType>(got);
  }
}

export template <class Type, class StateType>
[[nodiscard]] constexpr Type scanner_finish(StateType state) {
  // Asked for the value where the scanner hands failures back: what it handed
  // back is thrown, here at the asking, and caught nowhere. Whoever wants it
  // handed back asks the reading to try rather than to say.
  if constexpr (requires {
                  scanner<Type>{}.finish(std::move(state));
                }) {
    return as_thrown<Type>(scanner<Type>{}.finish(std::move(state)));
  } else {
    return as_thrown<Type>(scanner<Type>::finish(std::move(state)));
  }
}

// The failure a reading hands back, made out of whatever a scanner said.
export template <class FailureType, class ErrorType>
[[nodiscard]] constexpr FailureType as_a_failure(ErrorType&& said);

// Which way the caller is reading: handed a failure back, or thrown one.
//
// Said as a type rather than a flag so that a scanner of somebody's own can be
// written against it. Told which way it is being read, it can hand its failure
// back or throw it where it stands -- and the one that throws never builds the
// expected that would only be unwrapped and thrown again.
export struct hands_a_failure_back {
  template <class Type, class FailureType>
  using result = std::expected<Type, FailureType>;

  template <class Type, class FailureType, class ErrorType>
  [[nodiscard]] static constexpr result<Type, FailureType> went_wrong(
      ErrorType&& said) {
    return std::unexpected(
        scan::as_a_failure<FailureType>(std::forward<ErrorType>(said)));
  }

  template <class StepType>
  [[nodiscard]] static constexpr bool read(const StepType& step) {
    return step.has_value();
  }

  template <class StepType>
  [[nodiscard]] static constexpr decltype(auto) value(StepType&& step) {
    return *std::forward<StepType>(step);
  }

  template <class StepType>
  [[nodiscard]] static constexpr decltype(auto) failure(StepType&& step) {
    return std::forward<StepType>(step).error();
  }
};

export struct throws_a_failure {
  template <class Type, class FailureType>
  using result = Type;

  template <class Type, class FailureType, class ErrorType>
  [[noreturn]] static constexpr Type went_wrong(ErrorType&& said) {
    scan::throw_what_went_wrong(std::forward<ErrorType>(said));
  }

  template <class StepType>
  [[nodiscard]] static constexpr bool read(const StepType&) {
    return true;
  }

  template <class StepType>
  [[nodiscard]] static constexpr decltype(auto) value(StepType&& step) {
    return std::forward<StepType>(step);
  }

  template <class StepType>
  [[nodiscard]] static constexpr scan::scan_error<> failure(StepType&&) {
    return scan::scan_error<>("a reading that throws has nothing to hand back");
  }
};

// The same for the hooks that gather rather than read a field: one name, and
// the template argument says which way the caller is reading. A hook written
// without it is asked the way it always was.
// A gathering kept where a reading divides, and put back where one dies.
//
// The walk stands in several readings of the subject at once and carries a
// fold with each. A scanner that keeps something an assignment should not walk
// over says how both are done; one that says nothing is copied and assigned
// the way it always was.
export template <class Held, class StateType>
[[nodiscard]] constexpr StateType kept_groups(const StateType& made) {
  if constexpr (requires {
                  { scanner<Held>::keep_groups(made) } -> std::same_as<StateType>;
                }) {
    return scanner<Held>::keep_groups(made);
  } else {
    return made;
  }
}

export template <class Held, class StateType>
constexpr void groups_go_back_to(StateType& live, const StateType& kept) {
  if constexpr (requires { scanner<Held>::groups_go_back_to(live, kept); }) {
    scanner<Held>::groups_go_back_to(live, kept);
  } else {
    live = kept;
  }
}

// A fold kept in one place rather than one for every reading the walk stands
// in.
//
// The walk stands in several readings at once, and a fold told its groups as
// they go is kept with each of them -- which is what makes a turn cost a write
// and nothing else. Where the state is a handful of numbers that is the right
// trade; where it holds something that allocates, it is a great many of them
// made and thrown away.
//
// A scanner says this, and then its turns are held back until the walk stands
// in one reading, and told in one go. The same hooks, the same order, told
// later: what it costs is that a turn is not seen the moment it happens.
export template <class Type>
inline constexpr bool gathers_in_one_place = [] {
  using held = std::remove_cv_t<Type>;
  if constexpr (requires { scanner<held>::gathers_in_one_place; }) {
    return static_cast<bool>(scanner<held>::gathers_in_one_place);
  } else {
    return false;
  }
}();

// A scanner that cannot be told through a braced list, and says so.
//
// Braces deduce nothing, so what a place is told arrives behind an interface
// whose type was fixed before the call. A scanner whose hooks differ by the
// type of the context -- which groups it wants, what its state is made of --
// cannot be written against that, because the type it would overload on is
// gone by the time it is asked. Such a scanner says this, and a braced list
// that would reach it is refused where it is written rather than quietly
// taking the hook that takes nothing.
export template <class Type>
inline constexpr bool takes_its_context_deduced = [] {
  using held = std::remove_cv_t<Type>;
  if constexpr (requires { scanner<held>::takes_its_context_deduced; }) {
    return static_cast<bool>(scanner<held>::takes_its_context_deduced);
  } else {
    return false;
  }
}();

template <class Type, class StateType, class Ending>
concept can_be_told_to_finish =
    requires(StateType state) {
      scanner<std::remove_cv_t<Type>>::template finish<Ending>(
          std::move(state));
    } || requires(StateType state) {
      scanner<std::remove_cv_t<Type>>::finish(std::move(state));
    };

export template <class Type, class Ending = hands_a_failure_back, class StateType>
  requires can_be_told_to_finish<Type, StateType, Ending>
[[nodiscard]] constexpr decltype(auto) scanner_told_finish(StateType state) {
  using held = std::remove_cv_t<Type>;
  if constexpr (requires {
                  scanner<held>::template finish<Ending>(std::move(state));
                }) {
    return scanner<held>::template finish<Ending>(std::move(state));
  } else {
    return scanner<held>::finish(std::move(state));
  }
}

template <class Type, class Ending>
concept can_be_told_from_groups =
    requires(std::span<const std::string_view> given) {
      scanner<std::remove_cv_t<Type>>{}.template from_groups<Ending>(given);
    } || requires(std::span<const std::string_view> given) {
      scanner<std::remove_cv_t<Type>>{}.from_groups(given);
    };

export template <class Type, class Ending = hands_a_failure_back>
  requires can_be_told_from_groups<Type, Ending>
[[nodiscard]] constexpr decltype(auto) scanner_told_from_groups(
    std::span<const std::string_view> given) {
  using held = std::remove_cv_t<Type>;
  if constexpr (requires {
                  scanner<held>{}.template from_groups<Ending>(given);
                }) {
    return scanner<held>{}.template from_groups<Ending>(given);
  } else {
    return scanner<held>{}.from_groups(given);
  }
}

// The same, where the place this shape stands at was told a context. A shape
// that reads its own groups is a reading like any other inside, and what its
// places were told reaches them through here.
export template <class Type, class Ending = hands_a_failure_back, class CarrierType>
[[nodiscard]] [[gnu::always_inline]] inline constexpr decltype(auto)
scanner_told_from_groups(
    std::span<const std::string_view> given, const CarrierType& told) {
  using held = std::remove_cv_t<Type>;
  if constexpr (requires {
                  scanner<held>{}.template from_groups<Ending>(given, told);
                }) {
    return scanner<held>{}.template from_groups<Ending>(given, told);
  } else if constexpr (requires { scanner<held>{}.from_groups(given, told); }) {
    return scanner<held>{}.from_groups(given, told);
  } else if constexpr (requires {
                         scanner<held>{}.template from_groups<Ending>(given);
                       }) {
    // Said here rather than by calling the shape without a context: that one
    // is constrained, and a body that names it is a body that cannot be
    // compiled for a type which says no from_groups at all -- and a return
    // type deduced from a body that cannot be compiled takes this overload out
    // of the set, which reads at the call as no such function.
    return scanner<held>{}.template from_groups<Ending>(given);
  } else {
    return scanner<held>{}.from_groups(given);
  }
}

// What a place was told, out of whatever routes it there.
//
// A carrier is the library's own thing: it says which context belongs to which
// place, and a leaf of one holds the reading that knows the caller's type. A
// scanner is written against the caller's type and nothing else, so every road
// that asks a scanner asks through here. Written once because the two roads
// that did it themselves disagreed, and the one that handed the carrier over
// fell to the hook that takes nothing -- quietly, with whatever the context
// carried left out of the answer.
export template <class CarrierType>
[[nodiscard]] constexpr decltype(auto) told_itself(CarrierType&& given) {
  if constexpr (requires { given.leaf(); }) {
    return given.leaf();
  } else {
    return (given);
  }
}

// A leaf read from its own groups, told what its place was told.
export template <class Type, class Ending = hands_a_failure_back,
                 class CarrierType>
[[nodiscard]] constexpr decltype(auto) told_from_groups(
    std::span<const std::string_view> pieces, const CarrierType& given) {
  if constexpr (requires {
                  scanner_told_from_groups<Type, Ending>(pieces,
                                                         told_itself(given));
                }) {
    return scanner_told_from_groups<Type, Ending>(pieces, told_itself(given));
  } else if constexpr (requires {
                         scanner_told_from_groups<Type, Ending>(pieces, given);
                       }) {
    return scanner_told_from_groups<Type, Ending>(pieces, given);
  } else {
    return scanner_told_from_groups<Type, Ending>(pieces);
  }
}

// The same, where the scanner hands back the value itself.
export template <class Type, class CarrierType>
[[nodiscard]] constexpr auto told_from_groups_plain(
    std::span<const std::string_view> pieces, const CarrierType& given) {
  using held = std::remove_cv_t<Type>;
  if constexpr (requires {
                  scanner<held>{}.from_groups(pieces, told_itself(given));
                }) {
    return scanner<held>{}.from_groups(pieces, told_itself(given));
  } else if constexpr (requires {
                         scanner<held>{}.from_groups(pieces, given);
                       }) {
    return scanner<held>{}.from_groups(pieces, given);
  } else {
    return scanner<held>{}.from_groups(pieces);
  }
}


template <class Type, class StateType, class Ending>
concept can_be_told_to_finish_groups =
    requires(StateType state) {
      scanner<std::remove_cv_t<Type>>{}.template finish_groups<Ending>(
          std::move(state));
    } || requires(StateType state) {
      scanner<std::remove_cv_t<Type>>{}.finish_groups(std::move(state));
    // A fold kept inside the reading that knows the context's type: the state
    // is not here to be handed over, so the reading is asked to finish it.
    } || requires(StateType state) { state.finish(); };

export template <class Type, class Ending = hands_a_failure_back, class StateType>
  requires can_be_told_to_finish_groups<Type, StateType, Ending>
[[nodiscard]] constexpr decltype(auto) scanner_told_finish_groups(
    StateType state) {
  using held = std::remove_cv_t<Type>;
  if constexpr (requires { state.finish(); }) {
    return state.finish();
  } else if constexpr (requires {
                         scanner<held>{}.template finish_groups<Ending>(
                             std::move(state));
                       }) {
    return scanner<held>{}.template finish_groups<Ending>(std::move(state));
  } else {
    return scanner<held>{}.finish_groups(std::move(state));
  }
}

// The same, where the scanner says nothing can go wrong: a fold kept in the
// reading still answers with what it made, and there is nothing in it to
// report.
export template <class Type, class StateType>
  requires(requires(StateType one) { one.finish(); } ||
           requires(StateType one) {
             scanner<std::remove_cv_t<Type>>{}.finish_groups(std::move(one));
           })
[[nodiscard]] constexpr std::remove_cv_t<Type> finished_groups(
    StateType state) {
  using held = std::remove_cv_t<Type>;
  if constexpr (requires { state.finish(); }) {
    auto got = state.finish();
    return std::move(*got);
  } else {
    return scanner<held>{}.finish_groups(std::move(state));
  }
}

// Whether there is a reading to ask for at all. Asked first, so that a type
// whose scanner reads by groups and not by fields answers no rather than
// failing in a body nobody can see into.
export template <class Type, class Ending>
concept can_be_told_to_parse =
    requires(std::string_view text, std::string_view parameters) {
      scanner<std::remove_cv_t<Type>>::template parse<Ending>(text, parameters);
    } || requires(std::string_view text) {
      scanner<std::remove_cv_t<Type>>::template parse<Ending>(text);
    } || requires(std::string_view text, std::string_view parameters) {
      scanner<std::remove_cv_t<Type>>{}.parse(text, parameters);
    } || requires(std::string_view text) {
      scanner<std::remove_cv_t<Type>>{}.parse(text);
    };

// A scanner asked to read, told which way the caller is reading.
//
// One name, and the template argument is a hint and nothing more: a scanner
// written without it is asked the way it always was, and one written with it
// may still hand a failure back even where the caller said it would throw.
// What comes back is whatever the scanner said; making that into what the
// caller asked for is done where the asking was.
export template <class Type, class Ending = hands_a_failure_back>
  requires can_be_told_to_parse<Type, Ending>
[[nodiscard]] constexpr decltype(auto) scanner_told_parse(
    std::string_view input, std::string_view parameters) {
  using held = std::remove_cv_t<Type>;
  if constexpr (requires {
                  scanner<held>::template parse<Ending>(input, parameters);
                }) {
    return scanner<held>::template parse<Ending>(input, parameters);
  } else if constexpr (requires {
                         scanner<held>::template parse<Ending>(input);
                       }) {
    if (!parameters.empty()) throw "scanner does not accept parameters";
    return scanner<held>::template parse<Ending>(input);
  } else if constexpr (requires { scanner<held>{}.parse(input, parameters); }) {
    return scanner<held>{}.parse(input, parameters);
  } else {
    if (!parameters.empty()) throw "scanner does not accept parameters";
    return scanner<held>{}.parse(input);
  }
}

// Whether what a scanner hands back holds a failure rather than throwing one.
//
// Asked of what comes back and not of how it is written: a scanner that takes
// the template argument and one that does not are both answered the same way,
// by whether the thing they return names the failure it can hold.
export template <class Type>
concept says_what_went_wrong =
    can_be_told_to_parse<Type, hands_a_failure_back> && requires {
      typename std::remove_cvref_t<
          decltype(scanner_told_parse<Type, hands_a_failure_back>(
              std::string_view{}, std::string_view{}))>::error_type;
    };


export template <class Type>
[[nodiscard]] constexpr Type scanner_parse(std::string_view input) {
  return as_thrown<Type>(
      scanner_told_parse<Type, throws_a_failure>(input, {}));
}

export template <class Type>
[[nodiscard]] constexpr Type scanner_parse(std::string_view input,
                                           std::string_view parameters) {
  if constexpr (requires { scanner<Type>{}.parse(input, parameters); }) {
    return scanner<Type>{}.parse(input, parameters);
  } else if constexpr (requires {
                         scanner<Type>::try_parse(input, parameters);
                       }) {
    auto got = scanner<Type>::try_parse(input, parameters);
    if (got) return std::move(*got);
    throw_what_went_wrong(std::move(got).error());
  } else {
    if (!parameters.empty()) throw "scanner does not accept parameters";
    return scanner_parse<Type>(input);
  }
}

// A place that was given no context of its own.
//
// Stands in a list of contexts for a place that wants none, so that the places
// after it keep their numbers. A scanner is never handed this: where it stands
// the reading is the one that was there before contexts existed.
export struct default_context_t {};

// Said as a value, because that is how it is written at a call: one place of a
// list of contexts wants none, and `scan::default_context` is what stands there.
export inline constexpr default_context_t default_context{};

// No contexts at all, which is what every call said before there were any.
export struct no_contexts {
  static constexpr bool for_everyone = false;
  static constexpr bool told_apart = false;
  static constexpr std::size_t count = 0;
  template <std::size_t>
  [[nodiscard]] constexpr no_contexts for_part() const {
    return {};
  }
  [[nodiscard]] constexpr default_context_t leaf() const { return {}; }
};

// One context, and everything below the place it was said at.
//
// A place that is a shape hands the same one to each of its parts, which is
// what "a value at a place is that place's and all of its parts'" means.
template <class It, bool Apart>
struct one_context {
  using held = std::remove_reference_t<It>;
  static constexpr bool told_apart = Apart;
  // The caller's own thing and not a copy of it: a context is a place to keep
  // things while a reading runs, and a scanner told one may write in it. What
  // it was written as it stays -- a const one stays const, because the caller
  // wrote it that way. It lives as long as the call that said it, which is the
  // rule a context said in braces goes by as well.
  held* thing = nullptr;
  template <std::size_t>
  [[nodiscard]] constexpr one_context for_part() const {
    return *this;
  }
  [[nodiscard]] constexpr held& leaf() const { return *thing; }
};

// The contexts a call was given, in the order the places are read.
//
// One context is everybody's -- whoever wants it takes it, and a scanner that
// takes none is read as it always was. More than one is one per place, and
// then a context handed to a place whose scanner does not take one is a
// mistake said at compile time rather than a thing quietly dropped.
//
// A place picks its own by the same number its parameters are picked by, so
// the contexts line up with the places without anything being said twice.
// Held by value: a reading may be carried away from the call that made it, and
// a context is a handle -- an allocator, a pool, a pointer to the caller's
// world -- so the copy costs a word and cannot dangle.
// Whether a thing said at a place is the parts of that place rather than a
// context for the whole of it.
template <class>
inline constexpr bool said_as_parts = false;

export template <class... Contexts>
struct contexts_at_places {
  // What is held for one thing said at a place.
  //
  // The address of a context, for the reason said above `one_context`: what a
  // scanner writes into one is written into the caller's own. But the parts of
  // a place by value -- they are a handful of addresses already, and the thing
  // that said them is a temporary of the call that says it, so holding its
  // address would be holding the address of something that dies first.
  // A context of the caller's own is held as the address of it: what a scanner
  // writes into one is written into theirs. One said at the call is nobody
  // else's, so it is moved in and held here -- and then there is nothing for
  // it to outlive, which is what lets a reading that hands back a view take
  // one. The parts of a place are held the second way for the same reason.
  template <class One>
  struct owns {
    std::remove_cvref_t<One> value;
  };

  // What a place is told, said as a type: the caller's own thing as they wrote
  // it, and one made at the call as const. The carrier is read as const
  // wherever it is handed about -- it held nothing but addresses until now, so
  // that cost nothing -- and a context it owns is reached through it. Said
  // here rather than cast away: a scanner that wants to write into a context
  // wants the caller's own, and one made at the call is nobody's to write to.


  template <class One>
  using held_as = std::conditional_t<
      said_as_parts<std::remove_cvref_t<One>>, owns<One>,
      std::conditional_t<std::is_lvalue_reference_v<One>,
                         std::remove_reference_t<One>*, owns<One>>>;

  static constexpr bool for_everyone = [] {
    if constexpr (sizeof...(Contexts) == 1) {
      return !said_as_parts<
          std::remove_cvref_t<std::tuple_element_t<0, std::tuple<Contexts...>>>>;
    } else {
      return false;
    }
  }();
  static constexpr bool told_apart = !for_everyone;
  static constexpr std::size_t count = sizeof...(Contexts);

  constexpr contexts_at_places() = default;

  template <class... Given>
    requires(sizeof...(Given) == sizeof...(Contexts))
  constexpr explicit contexts_at_places(Given&&... given)
      : all(held_for<Contexts, Given>(given)...) {}

  template <class One, class Given>
  [[nodiscard]] static constexpr held_as<One> held_for(Given& given) {
    if constexpr (!std::is_lvalue_reference_v<One> ||
                  said_as_parts<std::remove_cvref_t<One>>) {
      return held_as<One>{std::forward<Given>(given)};
    } else {
      // The address of it, and nothing said here about how long it has to
      // live. A reading that runs inside the call it was written in is over
      // before the full expression is, so a context made at that call is
      // still there -- an allocator wrapped around a resource, say. A reading
      // that outlives the call has to ask for more than that, and asks where
      // it is written.
      return std::addressof(given);
    }
  }

  // What a place is told, said as a type, where the carrier is read as const.
  //
  // A context of the caller's own is an address either way -- const on the
  // carrier is const on the pointer, never on what it points at -- so it
  // arrives as they wrote it. One the carrier owns is reached through the
  // carrier, so a const carrier hands it over as const, and that is said here
  // rather than cast away.
  template <class One>
  using told_as =
      std::conditional_t<std::is_lvalue_reference_v<One>, One,
                         const std::remove_cvref_t<One>&>;

  // Where the thing said at a place is, whichever way it is held.
  template <std::size_t Place>
  [[nodiscard]] constexpr auto* at_place() const {
    auto& held = std::get<Place>(all);
    if constexpr (std::is_pointer_v<std::remove_cvref_t<decltype(held)>>) {
      return held;
    } else {
      return std::addressof(held.value);
    }
  }

  template <std::size_t Place>
  [[nodiscard]] constexpr auto for_part() const {
    if constexpr (for_everyone) {
      using first = std::tuple_element_t<0, std::tuple<Contexts...>>;
      return one_context<told_as<first>, false>{at_place<0>()};
    } else if constexpr (Place < sizeof...(Contexts)) {
      using here = std::tuple_element_t<Place, std::tuple<Contexts...>>;
      if constexpr (said_as_parts<std::remove_cvref_t<here>>) {
        return std::get<Place>(all).value;
      } else {
        return one_context<told_as<here>, true>{at_place<Place>()};
      }
    } else {
      static_assert(Place < sizeof...(Contexts),
                    "this reading has more places than it was given contexts: "
                    "give one for every place, one for all of them, or write "
                    "scan::default_context where a place wants none");
      return no_contexts{};
    }
  }

  [[nodiscard]] constexpr decltype(auto) leaf() const {
    using first = std::remove_cvref_t<
        std::tuple_element_t<0, std::tuple<Contexts...>>>;
    if constexpr (said_as_parts<first>) {
      return std::get<0>(all).value;
    } else {
      return *at_place<0>();
    }
  }

  template <std::size_t Place>
  [[nodiscard]] constexpr auto* at_place() {
    auto& held = std::get<Place>(all);
    if constexpr (std::is_pointer_v<std::remove_cvref_t<decltype(held)>>) {
      return held;
    } else {
      return std::addressof(held.value);
    }
  }

  template <std::size_t Place>
  [[nodiscard]] constexpr auto for_part() {
    if constexpr (for_everyone) {
      using first = std::tuple_element_t<0, std::tuple<Contexts...>>;
      return one_context<first, false>{at_place<0>()};
    } else if constexpr (Place < sizeof...(Contexts)) {
      using here = std::tuple_element_t<Place, std::tuple<Contexts...>>;
      if constexpr (said_as_parts<std::remove_cvref_t<here>>) {
        // The parts of this place, handed on as they were said. A place that
        // is a shape asks them for its own parts in turn, which is the whole
        // of what saying them in one does.
        return std::get<Place>(all).value;
      } else {
        return one_context<here, true>{at_place<Place>()};
      }
    } else {
      static_assert(Place < sizeof...(Contexts),
                    "this reading has more places than it was given contexts: "
                    "give one for every place, one for all of them, or write "
                    "scan::default_context where a place wants none");
      return no_contexts{};
    }
  }

  [[nodiscard]] constexpr decltype(auto) leaf() {
    using first = std::remove_cvref_t<
        std::tuple_element_t<0, std::tuple<Contexts...>>>;
    if constexpr (said_as_parts<first>) {
      return std::get<0>(all).value;
    } else {
      return *at_place<0>();
    }
  }

  std::tuple<held_as<Contexts>...> all;
};

// The contexts of one call, made where they are said.
//
// Which kind each is -- the caller's own thing held as its address, or one made
// at the call and held here -- is `contexts_at_places`' own business, and this
// is the one door into it. Every entry point that takes contexts comes through
// here, because the one that worked the kinds out for itself got them wrong:
// it stripped the reference, which made a named thing the caller still owned
// into something to move out of.
export template <class... Contexts>
using contexts_said = contexts_at_places<Contexts...>;

export template <class... Contexts>
[[nodiscard]] constexpr contexts_said<Contexts...> contexts_from(
    Contexts&&... given) {
  return contexts_said<Contexts...>(given...);
}


// The parts of one place, said where the place is said.
//
// A braced list says the same thing and says it behind an interface: a braced
// list deduces nothing, so what the places under it are handed has to be one
// type for every field and the context's own type does not cross. Said this
// way the types are deduced and nothing is erased, so the calls into a scanner
// are the calls that were written -- and a reading that runs after the call it
// was written in can take these, where it cannot take braces.
//
//     scan<f>(text).of<nest>(scan::parts{slow, fast}, fast)
//     scan<f>(text).with(scan::parts{fast, slow}, slow).of<nest>()
//
// Held by value wherever it is said, because it is a handful of addresses and
// the thing that said it is a temporary of that call.
export template <class... Parts>
struct parts : contexts_at_places<Parts...> {
  // Made empty as well, because the walk makes a carrier of its own before it
  // is told anything.
  constexpr parts() = default;
  template <class... Given>
    requires(sizeof...(Given) == sizeof...(Parts))
  constexpr explicit parts(Given&&... given)
      : contexts_at_places<Parts...>(static_cast<Given&&>(given)...) {}
};

export template <class... Parts>
parts(Parts&&...) -> parts<Parts...>;

export template <class... Parts>
inline constexpr bool said_as_parts<parts<Parts...>> = true;


// A scanner that says what went wrong rather than throwing it.
//
//   static std::expected<weight, too_heavy> try_parse(std::string_view);
//
// Where a scanner has this, it is what is called, and the kind of failure it
// hands back is read off its return type -- nothing has to be declared and
// nobody has to keep a list in step with the code. Where a value is asked for
// rather than tried, the failure is thrown, which is why the kinds handed back
// this way are `scan_error`s like the rest.

// Which way the caller is reading: handed a failure back, or thrown one.
//
// Said as a type rather than a flag so that a scanner of somebody's own can be
// written against it -- told which way it is being read, it can hand its
// failure back or throw it where it stands, and the one that throws never
// builds the expected that would only be unwrapped and thrown again.


// The kind of failure such a scanner hands back.
export template <class Type>
using went_wrong_with = typename decltype(scanner_told_parse<
    Type, hands_a_failure_back>(std::string_view{},
                                std::string_view{}))::error_type;

// What is thrown for a scanner that handed a failure back, where somebody
// asked for the value itself. Where it said one kind, that kind; where it said
// several -- an `expected` over a variant of them -- whichever one it is.
template <class ErrorType>
[[noreturn]] void throw_what_went_wrong(ErrorType&& said) {
  if constexpr (a_choice_of_kinds<std::remove_cvref_t<ErrorType>>) {
    std::visit(
        [](auto&& one) -> void {
          throw thrown_kind_t<decltype(one)>(one.what());
        },
        std::forward<ErrorType>(said));
    throw scan_error<std::exception>("a failure that said it was nothing");
  } else {
    throw thrown_kind_t<ErrorType>(said.what());
  }
}

template <class Type>
using scanner_state_t = decltype(scanner_begin<Type>());

// The rest of the user's functions, each in the same two shapes: the one that
// hands a failure back and the one that does not. Where both are there the
// first is used, and the kind it hands back joins the list a reading of that
// output can fail with.
// Whether the type gathers a character at a time at all. Asked first, because
// what it gathers into is what the question below is about, and a type with no
// gathering has no such thing to name.
export template <class Type>
concept gathers_as_it_reads = requires {
  scanner<std::remove_cv_t<Type>>{}.begin();
} || requires(std::string_view parameters) {
  scanner<std::remove_cv_t<Type>>{}.begin(parameters);
};

export template <class Type>
concept says_what_went_wrong_finishing =
    gathers_as_it_reads<Type> &&
    can_be_told_to_finish<Type, scanner_state_t<Type>,
                          hands_a_failure_back> &&
    requires(scanner_state_t<Type> state) {
      typename std::remove_cvref_t<decltype(
          scanner_told_finish<Type, hands_a_failure_back>(
              std::move(state)))>::error_type;
    };

export template <class Type>
concept says_what_went_wrong_from_groups =
    can_be_told_from_groups<Type, hands_a_failure_back> &&
    requires(std::span<const std::string_view> given) {
      typename std::remove_cvref_t<decltype(
          scanner_told_from_groups<Type, hands_a_failure_back>(
              given))>::error_type;
    };

export template <class Type>
concept says_what_went_wrong_folding =
    can_be_told_to_finish_groups<
        Type, decltype(scanner<std::remove_cv_t<Type>>{}.begin_groups()),
        hands_a_failure_back> && requires {
  typename std::remove_cvref_t<decltype(
      scanner_told_finish_groups<Type, hands_a_failure_back>(
          scanner<std::remove_cv_t<Type>>{}.begin_groups()))>::error_type;
};

export template <class Type>
using went_wrong_finishing = typename decltype(scanner<std::remove_cv_t<Type>>::
                                                   finish(
                                                       std::declval<
                                                           scanner_state_t<
                                                               Type>>()))::
    error_type;

export template <class Type>
using went_wrong_from_groups =
    typename decltype(scanner<std::remove_cv_t<Type>>{}.from_groups(
        std::declval<std::span<const std::string_view>>()))::error_type;

export template <class Type>
using went_wrong_folding =
    typename decltype(scanner<std::remove_cv_t<Type>>{}.finish_groups(
        scanner<std::remove_cv_t<Type>>{}.begin_groups()))::error_type;

export template <std::size_t Capacity = 8192>
struct pattern_buffer {
  std::array<char, Capacity> storage{};
  std::size_t length = 0;

  constexpr void push_back(char value) {
    if (length == Capacity) throw "scanner pattern is too large";
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
export using our_kinds =
    kind_list<scan_error<>, no_match<>, no_group<>, bad_field<>, out_of_range<>,
              wrong_subject<>>;

export using failure = typename as_a_variant<our_kinds>::type;

// What it said, whichever kind it is.
export template <class... Kinds>
[[nodiscard]] const char* what(const std::variant<Kinds...>& said) {
  return std::visit([](const scan_error<>& one) { return one.what(); }, said);
}

// The value, or the failure thrown.
//
// The only place in this library where a reading that went wrong becomes a
// throw, and nothing anywhere catches it. Asking for a value has nowhere to
// put a failure; trying for one does, and then nothing is thrown at all.
export template <class Type, class FailureType>
[[nodiscard]] constexpr Type or_thrown(std::expected<Type, FailureType> got) {
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
export template <class FailureType, class ErrorType>
[[nodiscard]] constexpr FailureType as_a_failure(ErrorType&& said) {
  if constexpr (a_choice_of_kinds<std::remove_cvref_t<ErrorType>>) {
    return std::visit(
        [](auto&& one) -> FailureType { return FailureType(std::move(one)); },
        std::forward<ErrorType>(said));
  } else {
    return FailureType(std::forward<ErrorType>(said));
  }
}

}  // namespace scan
