// The shape layer: places, fields, gatherings, and the putting together of a
// value out of what a walk found.
//
// Above the walk and knowing nothing it does not ask for. What is below reads
// a pattern and says where its groups were; what is here decides what those
// groups mean to a type -- which of them is which place, what gathers each
// one, and how the value is made when the reading is over. The walk takes
// whoever is gathering as a parameter and never looks inside it, which is what
// lets this be a module of its own rather than a half of that one.
export module scan.shape.places;

import std;
import scan.tre;
#if !SCAN_FIELDS_BY_BINDING_PACK
import boost.pfr;
#endif
export import scan.compiler;
export import scan.runtime;

namespace scan {

// What a shape is made of, asked of the type rather than assumed of it.
//
// Three questions and nothing else: how many parts, what the part at an index
// is, and how to reach it in a value. Everything in this library that puts a
// shape together asks them here, and nowhere else does it look at a type's
// members at all.
//
// The default answer is what an aggregate says about itself, read with
// Boost.PFR -- or, where `SCAN_FIELDS_BY_BINDING_PACK` is on, read by taking
// the aggregate apart with a structured binding pack, which needs no library
// at all and needs C++26. A type that is not an aggregate -- one with invariants to keep,
// or members nobody outside may touch, or an order of its own that has nothing
// to do with the order it was written in -- answers them itself:
//
//   template <> struct scan::fields<my_type> {
//     static constexpr std::size_t count = 2;
//     template <std::size_t index> using at = …;
//     template <std::size_t index> static constexpr auto& of(my_type&);
//   };
#if SCAN_FIELDS_BY_BINDING_PACK
// Asked of the language rather than of a library.
//
// A structured binding pack names every member of an aggregate at once, and
// the pack tells its own size and can be indexed -- which is all three
// questions, with nothing to fetch and nothing to build. It is C++26, so it
// is behind a switch until that is what everybody has; where it is on, this
// library has no dependencies at all.
//
// The binding is written inside a call in an unevaluated operand for the two
// questions that are about the type rather than about a value: no object has
// to exist for `sizeof...` or for the type at a place, and requiring one
// would rule out every aggregate that cannot be default-constructed.
export template <class Type>
struct fields {
 private:
  static constexpr auto taken_apart = [](Type& value) {
    auto&& [...parts] = value;
    return std::integral_constant<std::size_t, sizeof...(parts)>{};
  };

 public:
  static constexpr std::size_t count =
      decltype(taken_apart(std::declval<Type&>()))::value;

  template <std::size_t Index>
  using at = std::remove_cvref_t<decltype([](Type& value) -> decltype(auto) {
    auto&& [...parts] = value;
    return parts...[Index];
  }(std::declval<Type&>()))>;

  template <std::size_t Index>
  [[nodiscard]] static constexpr auto& of(Type& value) {
    auto&& [...parts] = value;
    return parts...[Index];
  }

  template <std::size_t Index>
  [[nodiscard]] static constexpr const auto& of(const Type& value) {
    auto&& [...parts] = value;
    return parts...[Index];
  }
};
#else
export template <class Type>
struct fields {
  static constexpr std::size_t count = boost::pfr::tuple_size_v<Type>;

  template <std::size_t Index>
  using at = std::remove_cvref_t<boost::pfr::tuple_element_t<Index, Type>>;

  template <std::size_t Index>
  [[nodiscard]] static constexpr auto& of(Type& value) {
    return boost::pfr::get<Index>(value);
  }

  template <std::size_t Index>
  [[nodiscard]] static constexpr const auto& of(const Type& value) {
    return boost::pfr::get<Index>(value);
  }
};
#endif

}  // namespace scan

namespace scan::detail {

template <class Type, std::size_t... Index>
[[nodiscard]] constexpr auto default_patterns(std::index_sequence<Index...>) {
  static_assert(
      (requires {
        scanner_pattern<typename scan::fields<Type>::template at<Index>>();
      } && ...),
      "scan::scanner<type> must provide pattern");
  return std::array<std::string_view, sizeof...(Index)>{
      scanner_pattern<typename scan::fields<Type>::template at<Index>>()...};
}

export template <fixed_string Format, std::size_t FieldCount>
[[nodiscard]] consteval auto field_parameters() {
  std::array<std::string_view, FieldCount> result{};
  std::size_t field = 0;
  std::size_t capture_depth = 0;
  bool character_class = false;
  const auto text = Format.view();
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
    if (field == FieldCount) throw "too many capture groups";
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
  if (field != FieldCount) throw "capture count does not match output";
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
export template <class Type>
concept scanned_as_variant = requires {
  scan::branches<std::remove_cv_t<Type>>::count;
};

// How many alternatives, and which type the k-th is, asked of whatever says
// it.
export template <class Type>
[[nodiscard]] consteval std::size_t branch_count() {
  return scan::branches<std::remove_cv_t<Type>>::count;
}

export template <class Type, std::size_t Which>
using branch_at =
    typename scan::branches<std::remove_cv_t<Type>>::template at<Which>;

// A type that reads itself out of the groups its own pattern opens.
//
// Either handed them when the match is done, or told which of them each
// character belongs to as it arrives -- and either way its pattern has groups
// in it, which are groups of whatever it is written into.
export template <class Type>
concept reads_its_own_groups =
    requires { scan::scanner<std::remove_cv_t<Type>>{}.begin_groups(); } ||
    requires(std::span<const std::string_view> given) {
      scan::scanner<std::remove_cv_t<Type>>{}.from_groups(given);
    } || requires(std::span<const std::string_view> given) {
      scan::scanner<std::remove_cv_t<Type>>{}.from_groups(given);
    };

// A type that says outright that it reads its own groups.
//
// Asked as a plain question, and not by whether a hook is there. What such a
// hook hands back is a list of everything the reading can fail with, and
// working that list out means asking how this type is read -- which is what is
// being decided here. A member that is a plain bool has no such circle in it,
// and saying it is the whole of what a shape has to do to be one.
export template <class Type>
concept says_it_reads_its_groups = requires {
  { scan::scanner<std::remove_cv_t<Type>>{}.reads_its_groups() } -> std::same_as<bool>;
  requires scan::scanner<std::remove_cv_t<Type>>{}.reads_its_groups();
};

// A type that says outright it is a list, though it could be read as one
// value.
//
// `std::string` is a range and has a scanner, and reading it as a value is
// what everybody wants -- so a type that is both is a value. Where that is the
// wrong way round, the type says so:
//
//   template <> struct scan::scanner<my_bytes> { static constexpr bool as_a_list = true; … };
template <class Type>
concept says_it_is_a_list = requires {
  requires scan::scanner<std::remove_cv_t<Type>>::as_a_list;
};

export template <class Type>
concept scanned_as_leaf = requires {
  sizeof(scan::scanner<std::remove_cv_t<Type>>);
} && !says_it_is_a_list<Type>;

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
export template <class Type>
concept scanned_from_values = says_it_reads_its_groups<Type> && requires {
  &scan::scanner<std::remove_cv_t<Type>>::parse;
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
export template <class Type>
concept scanned_as_range =
    !scanned_as_leaf<Type> &&
    !scanned_as_variant<Type> && std::ranges::range<Type> &&
    requires(Type& into, std::ranges::range_value_t<Type> element) {
      into.push_back(std::move(element));
    };

template <class FunctionType>
struct call_parameters;
template <class ResultType, class... ArgumentTypes>
struct call_parameters<ResultType (*)(ArgumentTypes...)> {
  static constexpr std::size_t count = sizeof...(ArgumentTypes);
  template <std::size_t Index>
  using at = std::remove_cvref_t<
      std::tuple_element_t<Index, std::tuple<ArgumentTypes...>>>;
};

// What a type is made of, for the purpose of reading it: the arguments of the
// call that makes it, where there is one, and its fields otherwise.
export template <class Type, bool = scanned_from_values<Type>>
struct parts_of;
template <class Type>
  requires scanned_as_range<Type>
struct parts_of<Type, false> {
  static constexpr std::size_t count = 1;
  template <std::size_t Index>
  using at = std::remove_cvref_t<std::ranges::range_value_t<Type>>;
};
template <class Type>
struct parts_of<Type, false> {
  static constexpr std::size_t count = scan::fields<Type>::count;
  template <std::size_t Index>
  using at = typename scan::fields<Type>::template at<Index>;
};
template <class Type>
struct parts_of<Type, true> {
  using call = call_parameters<
      decltype(&scan::scanner<std::remove_cv_t<Type>>::parse)>;
  static constexpr std::size_t count = call::count;
  template <std::size_t Index>
  using at = typename call::template at<Index>;
};

// What a shape is made of, asked of what it says and not of how it is read.
//
// The same answer as the general one -- the arguments of the call that makes
// it, or its fields -- but reached without asking whether the type is a list or
// a leaf or a shape, because those are questions this one is used to answer.
export template <class Type, bool = scanned_from_values<Type>>
struct shape_parts {
  static constexpr std::size_t count =
      scan::fields<std::remove_cv_t<Type>>::count;
  template <std::size_t Index>
  using at =
      typename scan::fields<std::remove_cv_t<Type>>::template at<Index>;
};
template <class Type>
struct shape_parts<Type, true> {
  using call = call_parameters<
      decltype(&scan::scanner<std::remove_cv_t<Type>>::parse)>;
  static constexpr std::size_t count = call::count;
  template <std::size_t Index>
  using at = typename call::template at<Index>;
};

// Whether a list stands anywhere inside a shape, asked of what the types say
// and never of how this library reads them.
//
// The difference matters here and nowhere else: how a shape is read is decided
// by whether it can hand its groups over, that is decided by whether it holds
// a list, and a question that asks its own answer has none. So this one asks
// only what a type is: a range that can be pushed into is a list, a type that
// says it is one is one, and a shape or a choice is asked about what is in it.
template <class Type>
concept a_list_by_itself =
    !requires { sizeof(scan::scanner<std::remove_cv_t<Type>>); } &&
    std::ranges::range<Type> &&
    requires(Type& into, std::ranges::range_value_t<Type> one) {
      into.push_back(std::move(one));
    };

template <class Type>
concept a_choice_by_itself = requires {
  scan::branches<std::remove_cv_t<Type>>::count;
};

export template <class Type>
[[nodiscard]] consteval bool says_a_list_inside();

template <class Type>
[[nodiscard]] consteval bool a_list_field() {
  if constexpr (says_it_is_a_list<Type> || a_list_by_itself<Type>) {
    return true;
  } else if constexpr (says_it_reads_its_groups<Type>) {
    return says_a_list_inside<Type>();
  } else if constexpr (a_choice_by_itself<Type>) {
    return []<std::size_t... which>(std::index_sequence<which...>) {
      return (false || ... ||
              a_list_field<std::remove_cv_t<branch_at<Type, which>>>());
    }(std::make_index_sequence<branch_count<Type>()>{});
  } else {
    return false;
  }
}

export template <class Type>
[[nodiscard]] consteval bool says_a_list_inside() {
  if constexpr (!says_it_reads_its_groups<Type>) {
    return a_list_field<Type>();
  } else {
    // What a shape is made of: the arguments of the call that makes it, where
    // there is one, and its fields otherwise. Asked this way rather than by
    // reflection, because a shape made by a call has no fields to reflect on
    // and its arguments are as much a part of it as fields are of anything.
    return []<std::size_t... field>(std::index_sequence<field...>) {
      return (false || ... ||
              a_list_field<typename shape_parts<
                  std::remove_cv_t<Type>>::template at<field>>());
    }(std::make_index_sequence<shape_parts<std::remove_cv_t<Type>>::count>{});
  }
}

// What one place in a format stands for. A leaf takes one; a type with a format
// of its own takes one and spends it on the format it declared; anything else
// is opened up and its fields take places of their own, which is why a
// structure of structures can be written out flat.
template <class Type>
[[nodiscard]] consteval std::size_t places_of() {
  if constexpr (scanned_as_leaf<Type> ||
                scanned_as_variant<Type> || scanned_as_range<Type>) {
    return 1;
  } else {
    return []<std::size_t... index>(std::index_sequence<index...>) {
      return (std::size_t{0} + ... +
              places_of<typename parts_of<Type>::template at<index>>());
    }(std::make_index_sequence<parts_of<Type>::count>{});
  }
}

// Whether the type names its groups with types rather than with numbers.
//
//   using group = std::variant<major, minor, patch>;
//
// Then the k-th alternative is the name of the k-th group, and the pushes can
// be overloads rather than a switch.
export template <class Type>
concept names_its_groups = requires {
  typename scan::scanner<std::remove_cv_t<Type>>::group;
};

// Whether the type would rather have the group whole than a character at a
// time. Only a subject that can be pointed at can offer it, so this is asked
// together with whether there is anything to point at.
export template <class Type, std::size_t Which, class StateType>
concept takes_the_group_whole =
    requires(StateType& state, std::string_view text) {
      scan::scanner<std::remove_cv_t<Type>>{}.closed_group(
          state, scan::group_at<Which>{}, text);
    } || requires(StateType& state, std::string_view text) {
      scan::scanner<std::remove_cv_t<Type>>{}.closed_group(state, Which, text);
    } || (names_its_groups<Type> &&
          requires(StateType& state, std::string_view text) {
            scan::scanner<std::remove_cv_t<Type>>{}.closed_group(
                state,
                std::variant_alternative_t<
                    Which,
                    typename scan::scanner<std::remove_cv_t<Type>>::group>{},
                text);
          });

// The state a type folds its groups in, as a type.
export template <class Type>
using group_state_of =
    decltype(scan::scanner<std::remove_cv_t<Type>>{}.begin_groups());

// Whether the type takes the characters of this group at all. A fold may be
// made of the edges alone -- counting the turns, saying which branch ran -- and
// then there is nothing to hand a character to.
export template <class Type, std::size_t Which, class StateType>
concept takes_group_characters =
    requires(StateType& state, char letter) {
      scan::scanner<std::remove_cv_t<Type>>{}.push_group(
          state, scan::group_at<Which>{}, letter);
    } || requires(StateType& state, char letter) {
      scan::scanner<std::remove_cv_t<Type>>{}.push_group(state, Which, letter);
    } || (names_its_groups<Type> && requires(StateType& state, char letter) {
      scan::scanner<std::remove_cv_t<Type>>{}.push_group(
          state,
          std::variant_alternative_t<
              Which, typename scan::scanner<std::remove_cv_t<Type>>::group>{},
          letter);
    });

// Whether a group would take a whole run in one call.
//
// A type that says so is better handed the run: one call, and inside it a
// length rather than a loop. A type that only takes characters is no better
// off for it, and finding where the run ends so that it can be walked again is
// a pass over it the reading was not going to make.
export template <class Type, std::size_t Which, class StateType>
concept takes_group_runs =
    requires(StateType& state, std::string_view run) {
      scan::scanner<std::remove_cv_t<Type>>{}.push_group(
          state, scan::group_at<Which>{}, run);
    } || requires(StateType& state, std::string_view run) {
      scan::scanner<std::remove_cv_t<Type>>{}.push_group(state, Which, run);
    };

// One character, handed to the group it belongs to, in whichever of the three
// ways the type asked for: the name of the group, the group as a variant, or
// its number. The choice is made here, where the number is a constant.
export template <class Type, std::size_t Which, class StateType>
constexpr void push_one_group(StateType& state, char letter) {
  using scanner_type = scan::scanner<std::remove_cv_t<Type>>;
  if constexpr (requires {
                  scanner_type{}.push_group(state, scan::group_at<Which>{},
                                           letter);
                }) {
    scanner_type{}.push_group(state, scan::group_at<Which>{}, letter);
  } else if constexpr (names_its_groups<Type>) {
    using named = typename scanner_type::group;
    using one = std::variant_alternative_t<Which, named>;
    if constexpr (requires { scanner_type{}.push_group(state, one{}, letter); }) {
      scanner_type{}.push_group(state, one{}, letter);
    } else if constexpr (requires {
                           scanner_type{}.push_group(
                               state, named(std::in_place_index<Which>),
                               letter);
                         }) {
      scanner_type{}.push_group(state, named(std::in_place_index<Which>),
                               letter);
    } else {
      scanner_type{}.push_group(state, Which, letter);
    }
  } else {
    scanner_type{}.push_group(state, Which, letter);
  }
}

// The same, handed a run of characters rather than one.
//
// A walk that steps over a run in vectors has the whole of it at once, and a
// type that says it can take a run is handed it that way: `count += run.size()`
// instead of a call a character. A type that says nothing of the sort is handed
// the characters one at a time, which is what it asked for.
export template <class Type, std::size_t Which, class StateType>
constexpr void push_one_group(StateType& state, std::string_view run) {
  using scanner_type = scan::scanner<std::remove_cv_t<Type>>;
  if constexpr (requires {
                  scanner_type{}.push_group(state, scan::group_at<Which>{},
                                            run);
                }) {
    scanner_type{}.push_group(state, scan::group_at<Which>{}, run);
  } else if constexpr (requires {
                         scanner_type{}.push_group(state, Which, run);
                       }) {
    scanner_type{}.push_group(state, Which, run);
  } else {
    for (const char letter : run) push_one_group<Type, Which>(state, letter);
  }
}

// The two edges of a group, said the same three ways a push is said. A type
// that only wants the characters says neither, and then nothing is said to it.
export template <class Type, std::size_t Which, class StateType>
constexpr void open_one_group(StateType& state) {
  using scanner_type = scan::scanner<std::remove_cv_t<Type>>;
  if constexpr (requires {
                  scanner_type{}.opened_group(state, scan::group_at<Which>{});
                }) {
    scanner_type{}.opened_group(state, scan::group_at<Which>{});
  } else if constexpr (names_its_groups<Type>) {
    using named = typename scanner_type::group;
    using one = std::variant_alternative_t<Which, named>;
    if constexpr (requires { scanner_type{}.opened_group(state, one{}); }) {
      scanner_type{}.opened_group(state, one{});
    } else if constexpr (requires {
                           scanner_type{}.opened_group(
                               state, named(std::in_place_index<Which>));
                         }) {
      scanner_type{}.opened_group(state, named(std::in_place_index<Which>));
    } else if constexpr (requires { scanner_type{}.opened_group(state, Which); }) {
      scanner_type{}.opened_group(state, Which);
    }
  } else if constexpr (requires { scanner_type{}.opened_group(state, Which); }) {
    scanner_type{}.opened_group(state, Which);
  }
}

export template <class Type, std::size_t Which, class StateType>
constexpr void close_one_group(StateType& state);

// A group closing, and where the subject can be pointed at, the whole of what
// it stood on handed over with it. Off a stream there is no such thing to hand,
// so the type is told the characters as they arrive and told the closing on its
// own; the two are the same fold, said with what each reading has to give.
export template <class Type, std::size_t Which, class StateType>
constexpr void close_one_group(StateType& state, std::string_view text) {
  using scanner_type = scan::scanner<std::remove_cv_t<Type>>;
  if constexpr (requires {
                  scanner_type{}.closed_group(state, scan::group_at<Which>{},
                                             text);
                }) {
    scanner_type{}.closed_group(state, scan::group_at<Which>{}, text);
  } else if constexpr (requires {
                         scanner_type{}.closed_group(state, Which, text);
                       }) {
    scanner_type{}.closed_group(state, Which, text);
  } else if constexpr (names_its_groups<Type> && requires {
                         scanner_type{}.closed_group(
                             state,
                             std::variant_alternative_t<
                                 Which, typename scanner_type::group>{},
                             text);
                       }) {
    scanner_type{}.closed_group(
        state,
        std::variant_alternative_t<Which, typename scanner_type::group>{},
        text);
  } else {
    for (char letter : text) push_one_group<Type, Which>(state, letter);
    close_one_group<Type, Which>(state);
  }
}

export template <class Type, std::size_t Which, class StateType>
constexpr void close_one_group(StateType& state) {
  using scanner_type = scan::scanner<std::remove_cv_t<Type>>;
  if constexpr (requires {
                  scanner_type{}.closed_group(state, scan::group_at<Which>{});
                }) {
    scanner_type{}.closed_group(state, scan::group_at<Which>{});
  } else if constexpr (names_its_groups<Type>) {
    using named = typename scanner_type::group;
    using one = std::variant_alternative_t<Which, named>;
    if constexpr (requires { scanner_type{}.closed_group(state, one{}); }) {
      scanner_type{}.closed_group(state, one{});
    } else if constexpr (requires {
                           scanner_type{}.closed_group(
                               state, named(std::in_place_index<Which>));
                         }) {
      scanner_type{}.closed_group(state, named(std::in_place_index<Which>));
    } else if constexpr (requires { scanner_type{}.closed_group(state, Which); }) {
      scanner_type{}.closed_group(state, Which);
    }
  } else if constexpr (requires { scanner_type{}.closed_group(state, Which); }) {
    scanner_type{}.closed_group(state, Which);
  }
}

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
export template <class Type>
[[nodiscard]] constexpr auto declared_pattern() {
  return scanner_pattern<std::remove_cv_t<Type>>(std::string_view{});
}

// And its characters, however the pattern is held.
export [[nodiscard]] constexpr std::string_view pattern_view(const auto& declared) {
  if constexpr (requires { std::string_view(declared); }) {
    return std::string_view(declared);
  } else {
    return declared.view();
  }
}

export template <class Type>
[[nodiscard]] consteval std::size_t groups_a_leaf_opens() {
  // Asked of the hooks and not of the plain answer: this is a count and not a
  // decision. A type with `from_groups` and nothing else said opens the groups
  // its pattern opens, and counting them is what tells the reading around it
  // where its own places begin.
  if constexpr (!reads_its_own_groups<Type>) {
    return 0;
  } else {
    std::size_t counted = 0;
    const auto declared = declared_pattern<Type>();
    tre_parser reading(pattern_view(declared), {}, counted, true);
    static_cast<void>(reading.parse_regex());
    return counted;
  }
}

export template <class Type>
[[nodiscard]] consteval std::size_t groups_of() {
  if constexpr (scanned_as_leaf<Type>) {
    // The place itself, and the groups the type's own pattern opens inside
    // it, which are groups of this match like any others.
    return 1 + groups_a_leaf_opens<Type>();
  } else if constexpr (scanned_as_variant<Type>) {
    // A mark for each branch, and then whatever that branch's alternative
    // reads, in the order the branches are written.
    return []<std::size_t... which>(std::index_sequence<which...>) {
      return (std::size_t{0} + ... +
              (1 + groups_of<branch_at<Type, which>>()));
    }(std::make_index_sequence<branch_count<Type>()>{});
  } else if constexpr (scanned_as_range<Type>) {
    // One for the list itself, and then whatever one element reads -- written
    // over again on every turn round the loop.
    return 1 + groups_of<std::remove_cvref_t<std::ranges::range_value_t<Type>>>();
  } else {
    return []<std::size_t... index>(std::index_sequence<index...>) {
      return (std::size_t{0} + ... +
              groups_of<typename parts_of<Type>::template at<index>>());
    }(std::make_index_sequence<parts_of<Type>::count>{});
  }
}

// The same count, asked of a type as the output of its own format rather than
// as a value standing in somebody else's.
//
// A shape that reads its own groups is one value to whatever contains it -- a
// place, and the groups its pattern opens after it -- and a product of places
// to itself. Everything that builds a reading of a format asks the second
// question, and asking the first would count a place nobody wrote.
export template <class Type>
[[nodiscard]] consteval std::size_t groups_of_output() {
  if constexpr (scanned_as_variant<Type>) {
    return []<std::size_t... which>(std::index_sequence<which...>) {
      return (std::size_t{0} + ... + (1 + groups_of<branch_at<Type, which>>()));
    }(std::make_index_sequence<branch_count<Type>()>{});
  } else if constexpr (scanned_as_range<Type>) {
    return 1 +
           groups_of<std::remove_cvref_t<std::ranges::range_value_t<Type>>>();
  } else {
    return []<std::size_t... index>(std::index_sequence<index...>) {
      return (std::size_t{0} + ... +
              groups_of<typename parts_of<Type>::template at<index>>());
    }(std::make_index_sequence<parts_of<Type>::count>{});
  }
}

export template <class Type, std::size_t Field>
[[nodiscard]] consteval std::size_t groups_before_field() {
  return []<std::size_t... index>(std::index_sequence<index...>) {
    return (std::size_t{0} + ... +
            groups_of<typename parts_of<Type>::template at<index>>());
  }(std::make_index_sequence<Field>{});
}

// Which field of a product holds the group at this position, and where in that
// field it falls.
// Which field of a product holds the place at this position, and where in that
// field it falls. The same walk as for groups, counting places.
template <class Subject>
[[nodiscard]] consteval std::pair<std::size_t, std::size_t> field_of_place(
    std::size_t index) {
  constexpr auto counts = []<std::size_t... field>(
                              std::index_sequence<field...>) {
    return std::array<std::size_t, sizeof...(field)>{
        places_of<typename parts_of<Subject>::template at<field>>()...};
  }(std::make_index_sequence<parts_of<Subject>::count>{});
  for (std::size_t field = 0; field < counts.size(); ++field) {
    if (index < counts[field]) return {field, index};
    index -= counts[field];
  }
  throw "format has more places than the output type has values";
}

template <class Subject, std::size_t Index,
          bool = scanned_as_leaf<Subject> ||
                 scanned_as_variant<Subject> || scanned_as_range<Subject>>
struct place_at;
template <class Subject, std::size_t Index>
struct place_at<Subject, Index, true> {
  using kind = Subject;
};
template <class Subject, std::size_t Index>
struct place_at<Subject, Index, false> {
  static constexpr auto where = field_of_place<Subject>(Index);
  using next = typename parts_of<Subject>::template at<where.first>;
  using kind = typename place_at<next, where.second>::kind;
};

template <class Subject, std::size_t Index>
using place_kind = typename place_at<Subject, Index>::kind;

// The places of a type's own format are its fields, not itself. A type that
// declares a format is one place where it is used and a product of its fields
// where that format is read, and the difference is the whole reason the reading
// terminates.
template <class Subject>
[[nodiscard]] consteval std::size_t places_within() {
  return []<std::size_t... field>(std::index_sequence<field...>) {
    return (std::size_t{0} + ... +
            places_of<typename parts_of<Subject>::template at<field>>());
  }(std::make_index_sequence<parts_of<Subject>::count>{});
}

template <class Subject, std::size_t Index>
using place_within_kind = typename place_at<Subject, Index, false>::kind;

// Choosing between the two by a conditional would ask for both, and asking a
// variant how many places its fields make is asking a variant for fields. Only
// the one taken may be named.
export template <class Subject, bool Within>
[[nodiscard]] consteval std::size_t places_chosen() {
  if constexpr (Within) {
    return places_within<Subject>();
  } else {
    return places_of<Subject>();
  }
}

export template <class Subject, bool Within, std::size_t Index>
struct place_chosen {
  static constexpr bool stands_alone =
      Within ? false
             : (scanned_as_leaf<Subject> ||
                scanned_as_variant<Subject> || scanned_as_range<Subject>);
  using kind = typename place_at<Subject, Index, stands_alone>::kind;
};

template <class Subject>
[[nodiscard]] consteval std::pair<std::size_t, std::size_t> field_holding(
    std::size_t index) {
  constexpr auto counts = []<std::size_t... field>(
                              std::index_sequence<field...>) {
    return std::array<std::size_t, sizeof...(field)>{
        groups_of<typename parts_of<Subject>::template at<field>>()...};
  }(std::make_index_sequence<parts_of<Subject>::count>{});
  for (std::size_t field = 0; field < counts.size(); ++field) {
    if (index < counts[field]) return {field, index};
    index -= counts[field];
  }
  throw "group index past the end of the output type";
}

template <class HeldType>
struct kind_is {
  using kind = HeldType;
};

// Which branch of a variant a group falls in, and where within it: nothing
// means the mark that stands for the branch itself, and anything after it
// belongs to what that branch reads.
template <class Type>
[[nodiscard]] consteval std::pair<std::size_t, std::size_t> branch_holding(
    std::size_t index) {
  constexpr auto counts = []<std::size_t... which>(
                              std::index_sequence<which...>) {
    return std::array<std::size_t, sizeof...(which)>{
        (1 + groups_of<branch_at<Type, which>>())...};
  }(std::make_index_sequence<branch_count<Type>()>{});
  for (std::size_t branch = 0; branch < counts.size(); ++branch) {
    if (index < counts[branch]) return {branch, index};
    index -= counts[branch];
  }
  throw "group index past the end of the variant";
}

// Which type gathers the value at this group. A list gathers at the group that
// stands for the list itself -- the first of the ones it takes -- and its
// element gathers at the ones after it, over and over.
template <class Subject, std::size_t Index,
          int = scanned_as_leaf<Subject> ? 0
                : scanned_as_range<Subject> ? 1
                : scanned_as_variant<Subject> ? 3
                                              : 2>
struct leaf_at;
template <class Subject, std::size_t Index>
struct leaf_at<Subject, Index, 0> {
  using kind = Subject;
};
template <class Subject, std::size_t Index>
struct leaf_at<Subject, Index, 1> {
  using element = std::remove_cvref_t<std::ranges::range_value_t<Subject>>;
  using kind = typename std::conditional_t<
      Index == 0, kind_is<Subject>,
      leaf_at<element, (Index == 0 ? 0 : Index - 1)>>::kind;
};
template <class Subject, std::size_t Index>
struct leaf_at<Subject, Index, 2> {
  static constexpr auto where = field_holding<Subject>(Index);
  using next = typename parts_of<Subject>::template at<where.first>;
  using kind = typename leaf_at<next, where.second>::kind;
};

// A variant is not a product and cannot be opened up like one: its groups are
// a mark for each branch, followed by whatever that branch reads.
template <class Subject, std::size_t Index>
struct leaf_at<Subject, Index, 3> {
  static constexpr auto where = branch_holding<Subject>(Index);
  using branch = branch_at<Subject, where.first>;
  using kind = typename std::conditional_t<
      where.second == 0, kind_is<branch_mark>,
      leaf_at<branch, (where.second == 0 ? 0 : where.second - 1)>>::kind;
};

template <class Subject, std::size_t Index>
using leaf_kind = typename leaf_at<Subject, Index>::kind;

// The same two, asked of a type as the output of its own format: never as a
// value standing in somebody else's place, which is what it looks like to
// whatever contains it.
export template <class Subject, std::size_t Index>
using leaf_kind_of_output = typename leaf_at<
    Subject, Index,
    scanned_as_range<Subject> ? 1 : scanned_as_variant<Subject> ? 3 : 2>::kind;


// Where within that type the group falls: nothing means the place the type
// stands at, and anything after it is one of the groups the type's own pattern
// opens, counted in the order they are written.
//
// The walk is the one above, step for step. Only the answer differs, so if one
// of them ever learns a new shape the other has to learn it too.
template <class Subject, std::size_t Index,
          int = scanned_as_leaf<Subject> ? 0
                : scanned_as_range<Subject> ? 1
                : scanned_as_variant<Subject> ? 3
                                              : 2>
struct leaf_offset_at;
template <class Subject, std::size_t Index>
struct leaf_offset_at<Subject, Index, 0> {
  static constexpr std::size_t value = Index;
};
template <class Subject, std::size_t Index>
struct leaf_offset_at<Subject, Index, 1> {
  using element = std::remove_cvref_t<std::ranges::range_value_t<Subject>>;
  static constexpr std::size_t value =
      Index == 0 ? 0
                 : leaf_offset_at<element, (Index == 0 ? 0 : Index - 1)>::value;
};
template <class Subject, std::size_t Index>
struct leaf_offset_at<Subject, Index, 2> {
  static constexpr auto where = field_holding<Subject>(Index);
  using next = typename parts_of<Subject>::template at<where.first>;
  static constexpr std::size_t value = leaf_offset_at<next, where.second>::value;
};
template <class Subject, std::size_t Index>
struct leaf_offset_at<Subject, Index, 3> {
  static constexpr auto where = branch_holding<Subject>(Index);
  using branch = branch_at<Subject, where.first>;
  static constexpr std::size_t value =
      where.second == 0
          ? 0
          : leaf_offset_at<branch, (where.second == 0 ? 0 : where.second - 1)>::
                value;
};

template <class Subject, std::size_t Index>
inline constexpr std::size_t leaf_offset_of = leaf_offset_at<Subject, Index>::value;

export template <class Subject, std::size_t Index>
inline constexpr std::size_t leaf_offset_of_output = leaf_offset_at<
    Subject, Index,
    scanned_as_range<Subject> ? 1 : scanned_as_variant<Subject> ? 3
                                                                : 2>::value;

// A leaf that is put together from the groups its own pattern opens, rather
// than from the text it stands on. Where it opens none, it is an ordinary leaf
// and nothing below changes for it.
export template <class Held>
inline constexpr bool gathers_by_its_groups =
    reads_its_own_groups<Held> && groups_a_leaf_opens<Held>() > 0;

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
template <class Type>
concept folds_its_groups = requires {
  scan::scanner<std::remove_cv_t<Type>>{}.begin_groups();
};

export template <class Held>
inline constexpr bool folds_by_turns =
    gathers_by_its_groups<Held> && folds_its_groups<Held>;

// A leaf whose scanner only reads a piece handed to it whole: it says how to
// read one and says nothing about being told a character at a time.
//
// Nothing is gathered for such a leaf as the walk goes. The marks say where
// its piece stood and the subject is still there to be pointed at, so it is
// read where the value is put together -- which is why it is refused where the
// subject is not kept, exactly as a leaf built from its own groups is. The
// question is asked of the scanner and not of the type, so a scanner of
// somebody's own that reads a view of the subject is read the same way.
export template <class Held>
inline constexpr bool reads_a_whole_piece_only =
    !gathers_by_its_groups<Held> &&
    scan::can_be_told_to_parse<std::remove_cv_t<Held>,
                               scan::throws_a_failure> &&
    !requires { scan::scanner<std::remove_cv_t<Held>>{}.begin(); } &&
    !requires {
      scan::scanner<std::remove_cv_t<Held>>{}.begin(std::string_view{});
    };

// A type that can only be told its groups as they happen: it folds them and
// cannot be handed them afterwards.
//
// The two are not the same question. A type that can do both is folded where
// the subject is read once -- there is no other way there -- and handed its
// groups whole where they can be pointed at, which is the faster of the two
// and the one that copies nothing. Only a type that cannot be handed them
// forces the reading that gathers as it goes.
export template <class Held>
inline constexpr bool needs_the_turns =
    folds_by_turns<Held> && !requires(std::span<const std::string_view> given) {
      scan::scanner<std::remove_cv_t<Held>>{}.from_groups(given);
    } && !requires(std::span<const std::string_view> given) {
      scan::scanner<std::remove_cv_t<Held>>{}.from_groups(given);
    };

}  // namespace scan::detail

#undef SCAN_FORCE_INLINE
