// Generated from the module interface unit of the same name. Do not edit.
#pragma once
// The shape layer: places, fields, gatherings, and the putting together of a
// value out of what a walk found.
//
// Above the walk and knowing nothing it does not ask for. What is below reads
// a pattern and says where its groups were; what is here decides what those
// groups mean to a type -- which of them is which place, what gathers each
// one, and how the value is made when the reading is over. The walk takes
// whoever is gathering as a parameter and never looks inside it, which is what
// lets this be a module of its own rather than a half of that one.

#include <algorithm>
#include <array>
#include <expected>
#include <iterator>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include "scan_tre.hpp"
#if !SCAN_FIELDS_BY_BINDING_PACK
#include <boost/pfr.hpp>
#endif
#include "scan_compiler.hpp"
#include "scan_runtime.hpp"

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
template <class Type>
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
template <class Type>
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

template <fixed_string Format, std::size_t FieldCount>
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
template <class Type>
concept scanned_as_variant = requires {
  scan::branches<std::remove_cv_t<Type>>::count;
};

// How many alternatives, and which type the k-th is, asked of whatever says
// it.
template <class Type>
[[nodiscard]] consteval std::size_t branch_count() {
  return scan::branches<std::remove_cv_t<Type>>::count;
}

template <class Type, std::size_t Which>
using branch_at =
    typename scan::branches<std::remove_cv_t<Type>>::template at<Which>;

// A type that reads itself out of the groups its own pattern opens.
//
// Either handed them when the match is done, or told which of them each
// character belongs to as it arrives -- and either way its pattern has groups
// in it, which are groups of whatever it is written into.
template <class Type>
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
template <class Type>
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

template <class Type>
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
template <class Type>
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
template <class Type>
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
template <class Type, bool = scanned_from_values<Type>>
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
template <class Type, bool = scanned_from_values<Type>>
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

template <class Type>
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

template <class Type>
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
template <class Type>
concept names_its_groups = requires {
  typename scan::scanner<std::remove_cv_t<Type>>::group;
};

// Whether the type would rather have the group whole than a character at a
// time. Only a subject that can be pointed at can offer it, so this is asked
// together with whether there is anything to point at.
template <class Type, std::size_t Which, class StateType>
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
template <class Type>
using group_state_of =
    decltype(scan::scanner<std::remove_cv_t<Type>>{}.begin_groups());

// Whether the type takes the characters of this group at all. A fold may be
// made of the edges alone -- counting the turns, saying which branch ran -- and
// then there is nothing to hand a character to.
template <class Type, std::size_t Which, class StateType>
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
template <class Type, std::size_t Which, class StateType>
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
template <class Type, std::size_t Which, class StateType>
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
template <class Type, std::size_t Which, class StateType>
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
template <class Type, std::size_t Which, class StateType>
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

template <class Type, std::size_t Which, class StateType>
constexpr void close_one_group(StateType& state);

// A group closing, and where the subject can be pointed at, the whole of what
// it stood on handed over with it. Off a stream there is no such thing to hand,
// so the type is told the characters as they arrive and told the closing on its
// own; the two are the same fold, said with what each reading has to give.
template <class Type, std::size_t Which, class StateType>
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

template <class Type, std::size_t Which, class StateType>
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
template <class Type>
[[nodiscard]] constexpr auto declared_pattern() {
  return scanner_pattern<std::remove_cv_t<Type>>(std::string_view{});
}

// And its characters, however the pattern is held.
[[nodiscard]] constexpr std::string_view pattern_view(const auto& declared) {
  if constexpr (requires { std::string_view(declared); }) {
    return std::string_view(declared);
  } else {
    return declared.view();
  }
}

template <class Type>
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

template <class Type>
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
template <class Type>
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

template <class Type, std::size_t Field>
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
template <class Subject, bool Within>
[[nodiscard]] consteval std::size_t places_chosen() {
  if constexpr (Within) {
    return places_within<Subject>();
  } else {
    return places_of<Subject>();
  }
}

template <class Subject, bool Within, std::size_t Index>
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
template <class Subject, std::size_t Index>
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

template <class Subject, std::size_t Index>
inline constexpr std::size_t leaf_offset_of_output = leaf_offset_at<
    Subject, Index,
    scanned_as_range<Subject> ? 1 : scanned_as_variant<Subject> ? 3
                                                                : 2>::value;

// A leaf that is put together from the groups its own pattern opens, rather
// than from the text it stands on. Where it opens none, it is an ordinary leaf
// and nothing below changes for it.
template <class Held>
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

template <class Held>
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
template <class Held>
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
template <class Held>
inline constexpr bool needs_the_turns =
    folds_by_turns<Held> && !requires(std::span<const std::string_view> given) {
      scan::scanner<std::remove_cv_t<Held>>{}.from_groups(given);
    } && !requires(std::span<const std::string_view> given) {
      scan::scanner<std::remove_cv_t<Held>>{}.from_groups(given);
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
  // How many turns a place may take, where it takes turns at all: what was
  // written after it, said as two numbers. Kept per place because whoever
  // collects the turns has a right to know -- a container with room said in
  // advance can say whether that many will fit, and it can only say it if the
  // number reached it.
  std::size_t turns_least[32]{};
  std::size_t turns_most[32]{};
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
// A repetition as one shape, whatever it was written as.
//
// A star, a plus and a question mark are the three counts everybody writes, and
// each is a count in braces said shorter: `*` is `{0,}`, `+` is `{1,}`, `?` is
// `{0,1}`. Written one way here, everything downstream reads one thing -- the
// machine, and whoever asks how many turns a list may take.
inline constexpr std::size_t turns_unbounded = ~std::size_t{0};

struct turns_written {
  std::size_t least = 1;
  std::size_t most = turns_unbounded;
};

[[nodiscard]] constexpr turns_written turns_of(std::string_view repetition) {
  if (repetition.empty()) return {1, turns_unbounded};
  switch (repetition.front()) {
    case '*':
      return {0, turns_unbounded};
    case '+':
      return {1, turns_unbounded};
    case '?':
      return {0, 1};
    default:
      break;
  }
  // A count in braces: `{n}`, `{n,}` or `{n,m}`.
  std::size_t at = 1;
  std::size_t least = 0;
  while (at < repetition.size() && repetition[at] >= '0' &&
         repetition[at] <= '9') {
    least = least * 10 + static_cast<std::size_t>(repetition[at] - '0');
    ++at;
  }
  if (at < repetition.size() && repetition[at] == '}') return {least, least};
  if (at >= repetition.size() || repetition[at] != ',') {
    throw "a count after a place is written {n}, {n,} or {n,m}";
  }
  ++at;
  if (at < repetition.size() && repetition[at] == '}') {
    return {least, turns_unbounded};
  }
  std::size_t most = 0;
  while (at < repetition.size() && repetition[at] >= '0' &&
         repetition[at] <= '9') {
    most = most * 10 + static_cast<std::size_t>(repetition[at] - '0');
    ++at;
  }
  if (most < least) throw "a place cannot take fewer turns than its own least";
  return {least, most};
}

constexpr void say_number(spread_format& made, std::size_t value) {
  char digits[20]{};
  std::size_t written = 0;
  do {
    digits[written++] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value != 0);
  while (written != 0) made.text.push_back(digits[--written]);
}

constexpr void say_turns(spread_format& made, turns_written turns) {
  made.text.push_back('{');
  say_number(made, turns.least);
  made.text.push_back(',');
  if (turns.most != turns_unbounded) say_number(made, turns.most);
  made.text.push_back('}');
}

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

template <class Type, bool Within>
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

template <class Kind>
constexpr void spread_place(spread_format& made, std::string_view body,
                            std::string_view repetition = {}) {
  if constexpr (scanned_as_range<Kind>) {
    // The body is one element, and it is read for as long as it goes on. The
    // group around it is the list; the places inside it are the element, and
    // they are written over again on every turn.
    // The group is the list and holds it whole; what repeats is inside it. The
    // other way round -- a group repeated -- would open the list again on
    // every turn, and a list opened again is an empty one.
    say_place_begin(made);
    // Which place this is, taken before the element is spread: the places of
    // the element are counted after it and would carry the number away.
    const std::size_t mine = made.leaves;
    ++made.leaves;
    say_group_begin(made);
    using element = std::remove_cvref_t<std::ranges::range_value_t<Kind>>;
    if (body.empty()) {
      // Nothing written between the braces means the same here as anywhere
      // else: read the element by whatever it says about itself. Spread as a
      // format instead, an empty body has no places in it and nothing was
      // written at all -- a list of the empty pattern, which stood on no
      // characters and gathered nothing.
      spread_place<element>(made, body);
    } else {
      spread_into<element, false>(made, body);
    }
    say_group_end(made);
    // How many turns, and one or more where the format did not say.
    //
    // The two spellings of the spread said this differently: the one the
    // format reader took wrote a mark that meant "this place repeats", and how
    // often was the reader's business, so a place with nothing written after
    // it went round for as long as the subject afforded. Written as a regular
    // expression there is no such mark -- what repeats is what carries a
    // repetition -- and when the two became one the unwritten one was lost. A
    // list read one turn and stopped: "7" was a list of one, and "1,2,3" was
    // not a list at all.
    const turns_written turns = turns_of(repetition);
    say_turns(made, turns);
    made.turns_least[mine] = turns.least;
    made.turns_most[mine] = turns.most;
    say_place_end(made);
  } else if constexpr (scanned_as_variant<Kind>) {
    // The branches, held together, each headed by a mark. Written out, the body
    // of the place says them, one per alternative, separated by a bar. Left
    // empty, each alternative is asked how it reads itself -- which it can
    // answer if it declares a format or if something knows how to read it.
    constexpr std::size_t count = branch_count<Kind>();
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
        using alternative = branch_at<Kind, branch>;
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
  } else if constexpr (!scanned_as_leaf<Kind>) {
    // Only reached by a variant place left empty, which asks each alternative
    // how it reads itself. This one does not say.
    throw "an alternative of a variant place left empty must say how it reads "
          "itself -- give it a scanner or a format of its own, or write the "
          "branches out with a bar between them";
  } else {
    if constexpr (gathers_by_its_groups<std::remove_cv_t<Kind>>) {
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
      const auto pattern = scanner_pattern<std::remove_cv_t<Kind>>(given);
      if constexpr (gathers_by_its_groups<std::remove_cv_t<Kind>>) {
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
    if constexpr (gathers_by_its_groups<std::remove_cv_t<Kind>>) {
      made.leaves += groups_a_leaf_opens<std::remove_cv_t<Kind>>();
    }
  }
}

template <class Type, bool Within>
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
      using kind = typename place_chosen<Type, Within, which>::kind;
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
  }(std::make_index_sequence<places_chosen<Type, Within>()>{});
  copy_until_kept_place(made, text, position);
  if (position != text.size()) throw "format has more places than values";
}

template <class Type, fixed_string Format>
[[nodiscard]] consteval spread_format spread_of() {
  spread_format made;
  made.space_before_places = Format.space_before_places;
  if constexpr (scanned_as_variant<Type>) {
    // The whole format is the list of branches, which is what a place standing
    // for a variant is written as anywhere else.
    spread_place<Type>(made, Format.view());
  } else {
    // The type scanned into is always opened up: its fields are the places, and
    // it is never itself one. A format of a single place standing for the whole
    // output would read differently the day that type gained a scanner or lost
    // one, without a word changing in the format, so it is not allowed to mean
    // anything. Whoever wants it writes the wrapper themselves, and then the
    // place is the field and says so.
    spread_into<Type, true>(made, Format.view());
  }
  return made;
}

// The same spread, said as a regular expression: the groups of it are the
// places of the format, in the order the format has them. This is what a type
// that declares a format matches, and what it is handed when it is handed its
// own groups.
template <class Type, fixed_string Format>
[[nodiscard]] consteval pattern_buffer<> places_pattern() {
  constexpr auto made = spread_of<Type, Format>();
  pattern_buffer<> result;
  result.append(made.text.view());
  return result;
}

// What one field of a shape matches, whatever kind of field it is.
//
// A value says it itself. A choice says a mark and a branch, over and over --
// and a branch is a field like any other, so this is written once and asks
// itself about them.
template <class FieldType>
[[nodiscard]] constexpr pattern_buffer<> field_pattern(
    std::string_view parameters);

template <class Type, std::size_t Extent, std::size_t... Index>
[[nodiscard]] constexpr auto parameterized_patterns(
    const std::array<std::string_view, Extent>& parameters,
    std::index_sequence<Index...>) {
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
  return std::array<pattern_buffer<>, Extent>{
      make_pattern.template operator()<
          typename scan::fields<Type>::template at<Index>>(
          parameters[Index])...};
}

template <class FieldType>
[[nodiscard]] constexpr pattern_buffer<> field_pattern(
    std::string_view parameters) {
  pattern_buffer<> result;
  if constexpr (scanned_as_variant<FieldType>) {
    // A mark, and then the branch in a group of its own -- the mark says which
    // branch ran, because a group that took no part points nowhere, and the
    // group beside it holds what that branch stood on.
    result.append(std::string_view("(?:"));
    [&]<std::size_t... which>(std::index_sequence<which...>) {
      ((void)[&] {
        if constexpr (which != 0) result.push_back('|');
        result.append(std::string_view("()("));
        const auto branch =
            field_pattern<std::remove_cv_t<branch_at<FieldType, which>>>(
                std::string_view{});
        result.append(branch.view());
        result.push_back(')');
      }(), ...);
    }(std::make_index_sequence<branch_count<FieldType>()>{});
    result.push_back(')');
  } else {
    const auto pattern = scanner_pattern<FieldType>(parameters);
    result.append(std::string_view{pattern});
  }
  return result;
}

template <std::size_t Extent>
[[nodiscard]] constexpr auto pattern_views(
    const std::array<pattern_buffer<>, Extent>& patterns) {
  std::array<std::string_view, Extent> result{};
  std::ranges::transform(patterns, result.begin(),
                         [](const auto& pattern) { return pattern.view(); });
  return result;
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
template <class Type, class FailureType,
          class Ending = scan::hands_a_failure_back>
[[nodiscard]] constexpr std::expected<Type, FailureType> parse_value(
    std::string_view text, std::string_view parameters) {
  using value_type = std::remove_cv_t<Type>;
  if constexpr (scan::says_what_went_wrong<value_type>) {
    return scan::as_handed_back<Type, FailureType>(
        scan::scanner_told_parse<value_type, Ending>(text, parameters));
  } else {
    static_assert(requires { scanner_parse<value_type>(text); },
                  "scan::scanner<type> must provide parse(string_view) or "
                  "try_parse(string_view)");
    return scanner_parse<value_type>(text, parameters);
  }
}

// The same reading, where the call handed this place a context.
//
// The scanner is asked in the shapes it may have written, the one that says
// most first: parameters and context, then context alone. A scanner that takes
// no context at all is read the way it always was -- which is what makes one
// context for everybody mean "whoever wants it, take it", and what makes the
// same context a mistake where the places were told apart.
//
// The context goes no further than this call. What the scanner does with it --
// keeps it in the state it hands back, gives it to what it builds, forgets it
// -- is the scanner's business; the library neither stores it nor looks inside.
template <class Type, class FailureType, bool ToldApart,
          class Ending = scan::hands_a_failure_back, class Context>
[[nodiscard]] constexpr std::expected<Type, FailureType> parse_value_given(
    std::string_view text, std::string_view parameters, Context&& given) {
  using value_type = std::remove_cv_t<Type>;
  if constexpr (requires {
                  given.told();
                  given.read(text, parameters);
                }) {
    // The call kept the context's type to itself and left this reading in its
    // place: written where that type was still known, so what it hands the
    // scanner is the caller's own thing and not a picture of it.
    if (!given.told()) return parse_value<Type, FailureType, Ending>(text, parameters);
    auto got = given.read(text, parameters);
    if (got) return std::move(*got);
    return std::unexpected(
        scan::as_a_failure<FailureType>(std::move(got).error()));
  } else if constexpr (std::same_as<std::remove_cvref_t<Context>,
                                    scan::default_context_t>) {
    // A place that was given nothing reads as it did before: said here and not
    // left to overload resolution, because a scanner whose context is a
    // template would otherwise take this standing-in-for-nothing as a context.
    return parse_value<Type, FailureType, Ending>(text, parameters);
  } else if constexpr (requires {
                         scan::scanner<value_type>::try_parse(text, parameters,
                                                              given);
                       }) {
    auto got = scan::scanner<value_type>::try_parse(text, parameters, given);
    if (got) return std::move(*got);
    return std::unexpected(
        scan::as_a_failure<FailureType>(std::move(got).error()));
  } else if constexpr (requires {
                         scan::scanner<value_type>::try_parse(text, given);
                       }) {
    auto got = scan::scanner<value_type>::try_parse(text, given);
    if (got) return std::move(*got);
    return std::unexpected(
        scan::as_a_failure<FailureType>(std::move(got).error()));
  } else if constexpr (requires {
                         scan::scanner<value_type>{}.parse(text, parameters,
                                                           given);
                       }) {
    return scan::scanner<value_type>{}.parse(text, parameters, given);
  } else if constexpr (requires {
                         scan::scanner<value_type>{}.parse(text, given);
                       }) {
    return scan::scanner<value_type>{}.parse(text, given);
  } else {
    static_assert(!ToldApart,
                  "this place was given a context of its own and its scanner "
                  "takes none: write parse(string_view, context) on "
                  "scan::scanner<T>, or write scan::default_context_t in its place");
    return parse_value<Type, FailureType, Ending>(text, parameters);
  }
}

// Where a branch's mark stands, counting from the start of the variant: each
// branch before it took a mark of its own and whatever its alternative reads.
template <class Type, std::size_t Branch>
[[nodiscard]] consteval std::size_t groups_before_branch() {
  return []<std::size_t... which>(std::index_sequence<which...>) {
    return (std::size_t{0} + ... +
            (1 + groups_of<branch_at<Type, which>>()));
  }(std::make_index_sequence<Branch>{});
}

// Whether a variant stands anywhere inside this output, at any depth. Where one
// does, a group that took no part is the ordinary state of affairs rather than
// a fault.
template <class Type>
[[nodiscard]] consteval bool holds_a_variant() {
  if constexpr (scanned_as_variant<Type>) {
    return true;
  } else if constexpr (scanned_as_leaf<Type>) {
    return false;
  } else {
    return []<std::size_t... field>(std::index_sequence<field...>) {
      return (false || ... ||
              holds_a_variant<typename parts_of<Type>::template at<field>>());
    }(std::make_index_sequence<parts_of<Type>::count>{});
  }
}

// Whether a list stands anywhere inside this output.
//
// A list is read by gathering, and it has to be: the positions a match leaves
// behind hold the last turn round the loop and nothing else, so an output with
// a list in it cannot be put together by reading them afterwards, however well
// they can be pointed at. It goes to the machine that gathers as it goes, over
// the very same characters.
template <class Type>
[[nodiscard]] consteval bool holds_a_range() {
  if constexpr (scanned_as_range<Type>) {
    return true;
  } else if constexpr (scanned_as_leaf<Type>) {
    return false;
  } else if constexpr (scanned_as_variant<Type>) {
    return []<std::size_t... which>(std::index_sequence<which...>) {
      return (false || ... ||
              holds_a_range<branch_at<Type, which>>());
    }(std::make_index_sequence<branch_count<Type>()>{});
  } else {
    return []<std::size_t... part>(std::index_sequence<part...>) {
      return (false || ... ||
              holds_a_range<typename parts_of<Type>::template at<part>>());
    }(std::make_index_sequence<parts_of<Type>::count>{});
  }
}

// The kinds a scanner hands back, where it hands back several of them: they
// are said as a variant, and this is that variant read as a list.
template <class Variant>
struct kinds_of_variant;
template <class... Kinds>
struct kinds_of_variant<std::variant<Kinds...>> {
  static_assert((std::derived_from<Kinds, scan::scan_error<>> && ...),
                "every kind a scanner hands back has to be a "
                "`scan::scan_error<>`: it ends up in the list of what a reading "
                "can fail with, and asking for a value rather than trying for "
                "it throws it");
  using list = scan::kind_list<Kinds...>;
};

template <class... Lists>
struct joined_all {
  using type = scan::kind_list<>;
};
template <class First>
struct joined_all<First> {
  using type = First;
};
template <class First, class... Rest>
struct joined_all<First, Rest...> {
  using type = typename scan::joined_lists<
      First, typename joined_all<Rest...>::type>::type;
};

// What a scanner says it can go wrong with, said in the only place it cannot
// fall out of step with the code: the type it hands back.
//
// Four places to say it, one for each of the user's functions that makes a
// value -- `try_parse`, `finish`, `from_groups`, `finish_groups` --
// and the kinds of all of them together are what a reading of this leaf can
// fail with. A scanner that throws instead says nothing here and is caught
// nowhere: it throws past the reading, to whoever asked for it.
template <class Kind>
struct kinds_handed_back {
  static_assert(std::derived_from<Kind, scan::scan_error<>>,
                "what a `try_` function hands back has to be a "
                "`scan::scan_error<>`, or a variant of them: it ends up in the "
                "list of what a reading can fail with, and asking for a value "
                "rather than trying for it throws it");
  using list = scan::kind_list<Kind>;
};
// Said as a variant where there is more than one of them.
template <class... Kinds>
struct kinds_handed_back<std::variant<Kinds...>> {
  using list = typename kinds_of_variant<std::variant<Kinds...>>::list;
};

template <class Type>
struct parse_kinds {
  using list = scan::kind_list<>;
};
template <class Type>
  requires scan::says_what_went_wrong<std::remove_cv_t<Type>>
struct parse_kinds<Type> {
  using list = typename kinds_handed_back<
      scan::went_wrong_with<std::remove_cv_t<Type>>>::list;
};

template <class Type>
struct finish_kinds {
  using list = scan::kind_list<>;
};
template <class Type>
  requires scan::says_what_went_wrong_finishing<std::remove_cv_t<Type>>
struct finish_kinds<Type> {
  using list = typename kinds_handed_back<
      scan::went_wrong_finishing<std::remove_cv_t<Type>>>::list;
};

template <class Type>
struct from_groups_kinds {
  using list = scan::kind_list<>;
};
template <class Type>
  requires scan::says_what_went_wrong_from_groups<std::remove_cv_t<Type>>
struct from_groups_kinds<Type> {
  using list = typename kinds_handed_back<
      scan::went_wrong_from_groups<std::remove_cv_t<Type>>>::list;
};

template <class Type>
struct folding_kinds {
  using list = scan::kind_list<>;
};
template <class Type>
  requires scan::says_what_went_wrong_folding<std::remove_cv_t<Type>>
struct folding_kinds<Type> {
  using list = typename kinds_handed_back<
      scan::went_wrong_folding<std::remove_cv_t<Type>>>::list;
};

template <class Type>
struct declared_kinds {
  using list = typename joined_all<typename parse_kinds<Type>::list,
                                   typename finish_kinds<Type>::list,
                                   typename from_groups_kinds<Type>::list,
                                   typename folding_kinds<Type>::list>::type;
};

// Every kind declared anywhere inside an output, walked the way everything
// else about an output is walked.
template <class Type,
          int = scanned_as_leaf<Type> ? 0
                : scanned_as_range<Type> ? 1
                : scanned_as_variant<Type> ? 3
                                           : 2>
struct kinds_in;
template <class Type>
struct kinds_in<Type, 0> {
  using list = typename declared_kinds<Type>::list;
};
template <class Type>
struct kinds_in<Type, 1> {
  using list = typename kinds_in<
      std::remove_cvref_t<std::ranges::range_value_t<Type>>>::list;
};
template <class Type>
struct kinds_in<Type, 2> {
  template <std::size_t... Part>
  static auto over(std::index_sequence<Part...>) -> typename joined_all<
      typename kinds_in<typename parts_of<Type>::template at<Part>>::list...>::
      type;
  using list = decltype(over(std::make_index_sequence<parts_of<Type>::count>{}));
};
template <class Type>
struct kinds_in<Type, 3> {
  template <std::size_t... Which>
  static auto over(std::index_sequence<Which...>) -> typename joined_all<
      typename kinds_in<branch_at<Type, Which>>::list...>::type;
  using list =
      decltype(over(std::make_index_sequence<branch_count<Type>()>{}));
};

// What reading a shape can fail with, asked of its fields.
//
// Never of the shape itself: what the shape says it hands back is this very
// list, so a list that asked the shape would be asking its own answer. Its
// fields are other types, and asking them is asking something else.
template <class Type, class Sequence>
struct kinds_of_fields;
template <class Type, std::size_t... Field>
struct kinds_of_fields<Type, std::index_sequence<Field...>> {
  using list = typename joined_all<
      typename kinds_in<typename shape_parts<
          std::remove_cv_t<Type>>::template at<Field>>::list...>::type;
};

template <class Type>
using shape_failure = typename scan::as_a_variant<typename scan::without_repeats<
    typename scan::joined_lists<
        scan::our_kinds,
        typename kinds_of_fields<
            Type, std::make_index_sequence<shape_parts<std::remove_cv_t<Type>>::
                                               count>>::list>::type>::type>::
    type;

// This library's kinds, and the ones this output's own scanners declare.
template <class Type>
using failure_for = typename scan::as_a_variant<typename scan::without_repeats<
    typename scan::joined_lists<scan::our_kinds,
                                typename kinds_in<Type>::list>::type>::type>::
    type;

// Whether a fold stands anywhere inside this output.
//
// The same question as the one above, and the same answer for the same reason:
// a fold is told its groups as the walk passes them, so an output holding one
// cannot be put together from the positions left behind, however well they can
// be pointed at. It goes to the machine that gathers as it goes.
template <class Type>
[[nodiscard]] consteval bool holds_a_fold() {
  if constexpr (scanned_as_leaf<Type>) {
    return needs_the_turns<std::remove_cv_t<Type>>;
  } else if constexpr (scanned_as_variant<Type>) {
    return []<std::size_t... which>(std::index_sequence<which...>) {
      return (false || ... || holds_a_fold<branch_at<Type, which>>());
    }(std::make_index_sequence<branch_count<Type>()>{});
  } else if constexpr (scanned_as_range<Type>) {
    return holds_a_fold<std::remove_cvref_t<std::ranges::range_value_t<Type>>>();
  } else {
    return []<std::size_t... part>(std::index_sequence<part...>) {
      return (false || ... ||
              holds_a_fold<typename parts_of<Type>::template at<part>>());
    }(std::make_index_sequence<parts_of<Type>::count>{});
  }
}

// Whether a type that reads its own groups only once the match is over stands
// anywhere inside this output.
//
// Such a type is handed views of the subject, so it needs a subject there is
// something to point at. It can stand in the gathering machine -- a list or a
// fold beside it puts the whole output there -- and then the walk has to be
// one that started on characters lying in a row.
template <class Type>
[[nodiscard]] consteval bool holds_a_flat_reader() {
  if constexpr (scanned_as_leaf<Type>) {
    // One that can also be folded is not refused: off a stream it is told its
    // groups as they arrive, which wants nothing to point at. Nor is one that
    // gathers a character at a time, which is the ordinary way a leaf is read
    // off a stream -- it is handed its groups only where they can be pointed
    // at, and read as a value everywhere else.
    // A leaf that only reads a piece handed to it whole is the same case: it
    // is handed a view of the subject and there is nothing else it can be
    // handed, so a walk with nothing to point at cannot read it either.
    return (gathers_by_its_groups<std::remove_cv_t<Type>> &&
            !folds_by_turns<std::remove_cv_t<Type>> &&
            !scan::gathers_as_it_reads<std::remove_cv_t<Type>>) ||
           reads_a_whole_piece_only<std::remove_cv_t<Type>>;
  } else if constexpr (scanned_as_variant<Type>) {
    return []<std::size_t... which>(std::index_sequence<which...>) {
      return (false || ... || holds_a_flat_reader<branch_at<Type, which>>());
    }(std::make_index_sequence<branch_count<Type>()>{});
  } else if constexpr (scanned_as_range<Type>) {
    return holds_a_flat_reader<
        std::remove_cvref_t<std::ranges::range_value_t<Type>>>();
  } else {
    return []<std::size_t... part>(std::index_sequence<part...>) {
      return (false || ... ||
              holds_a_flat_reader<typename parts_of<Type>::template at<part>>());
    }(std::make_index_sequence<parts_of<Type>::count>{});
  }
}

// Whether a shape's turns can be folded by the shape itself.
//
// Its places are told to it one at a time, so every place has to be something
// that takes characters: a value with a scanner, or a list of them. A place
// standing for a type that reads groups of its own would have to be handed
// those groups, and a fold has none to hand -- such a shape keeps the road
// that spreads its places into the automaton around it.
template <class Type, fixed_string Format>
[[nodiscard]] consteval bool turns_can_be_folded() {
  return []<std::size_t... place>(std::index_sequence<place...>) {
    return (true && ... && [] {
      using stands_for =
          std::remove_cv_t<leaf_kind_of_output<std::remove_cv_t<Type>, place>>;
      // A value that takes characters, or a type that folds its own groups and
      // can be handed the ones that are its. What cannot be told this way is a
      // type that wants its groups when the match is over: a fold has no views
      // of the subject to give it.
      // Nor can a place that only reads a piece handed to it whole: it takes
      // no characters at all, and a fold has no piece to hand it.
      return (!gathers_by_its_groups<stands_for> || folds_by_turns<stands_for>) &&
             !reads_a_whole_piece_only<stands_for>;
    }());
  }(std::make_index_sequence<groups_of_output<std::remove_cv_t<Type>>()>{});
}

// The same question, asked of what is inside an output rather than of the
// output itself.
//
// A type read by the machine that gathers is being built out of its places,
// however it looks to whatever contains it -- a shape that is one value to its
// parent is a product of places to itself. Asking the question of the type
// would ask how that type is read, and the answer to that is the machine doing
// the asking.
template <class Type>
[[nodiscard]] consteval bool a_flat_reader_inside() {
  if constexpr (scanned_as_variant<Type>) {
    return []<std::size_t... which>(std::index_sequence<which...>) {
      return (false || ... || holds_a_flat_reader<branch_at<Type, which>>());
    }(std::make_index_sequence<branch_count<Type>()>{});
  } else if constexpr (scanned_as_range<Type>) {
    return holds_a_flat_reader<
        std::remove_cvref_t<std::ranges::range_value_t<Type>>>();
  } else {
    return []<std::size_t... field>(std::index_sequence<field...>) {
      return (false || ... ||
              holds_a_flat_reader<
                  typename parts_of<Type>::template at<field>>());
    }(std::make_index_sequence<parts_of<Type>::count>{});
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
template <class Root, fixed_string Format>
struct format_parameters {
  [[nodiscard]] static constexpr std::string_view at(std::size_t place) {
    static constexpr auto spread = spread_of<Root, Format>();
    return spread.parameters[place].view();
  }
};

// The first of these that did not read, if any did not. Written once because
// every shape that is made of parts asks it: a product, and a type made by the
// call it named.
template <class FailureType, class... Parts>
[[nodiscard]] constexpr std::optional<FailureType> what_went_wrong(
    std::tuple<Parts...>& read) {
  std::optional<FailureType> went_wrong;
  [&]<std::size_t... at>(std::index_sequence<at...>) {
    ((void)[&] {
      if (went_wrong || std::get<at>(read)) return;
      went_wrong = std::move(std::get<at>(read)).error();
    }(), ...);
  }(std::index_sequence_for<Parts...>{});
  return went_wrong;
}

struct no_parameters {
  [[nodiscard]] static constexpr std::string_view at(std::size_t) { return {}; }
};

// The groups as a span and not as an array of a known length: the reading
// hands over all of them, and a type that reads its own groups hands over the
// few that are its. Both are the same thing to whoever reads them.
// What a reading does when it goes wrong, chosen by whoever asked for it.
//
// Asked for a value, there is nowhere to put a failure but a throw, and the
// throw happens where the failure is -- so nothing along the way holds one, and
// the value is written straight into the value it belongs to. Asked to try,
// every step hands its failure back, and the caller gets it as the kind it is.
//
// One builder either way. The difference is these two words and the type a



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

template <class Type, fixed_string Format, fixed_string Opening,
          std::size_t FieldCount>
constexpr void append_aggregate_pattern(
    pattern_buffer<>& output,
    const std::array<std::string_view, FieldCount>& defaults,
    std::size_t position = 0, std::size_t field = 0) {
  const std::string_view text = Format.view();
  if (position == text.size()) {
    if (field != FieldCount) throw "aggregate format field count mismatch";
    return;
  }
  if (text[position] == '\\') {
    if (position + 1 == text.size()) throw "dangling aggregate escape";
    append_literal(output, text[position + 1]);
    append_aggregate_pattern<Type, Format, Opening>(output, defaults,
                                                    position + 2, field);
    return;
  }
  if (text[position] != '{') {
    append_literal(output, text[position]);
    append_aggregate_pattern<Type, Format, Opening>(output, defaults,
                                                    position + 1, field);
    return;
  }
  if (field == FieldCount) throw "too many aggregate captures";
  const std::size_t end = find_capture_end(text, position + 1);
  output.append(Opening.view());
  if (end == position + 1 || text[position + 1] == ':') {
    output.append(defaults[field]);
  } else {
    output.append(text.substr(position + 1, end - position - 1));
  }
  output.push_back(')');
  append_aggregate_pattern<Type, Format, Opening>(output, defaults, end + 1,
                                                  field + 1);
}

template <class Type, fixed_string Format, fixed_string Opening = "(?:">
[[nodiscard]] consteval pattern_buffer<> make_aggregate_pattern() {
  constexpr std::size_t field_count = scan::fields<Type>::count;
  constexpr auto parameters = field_parameters<Format, field_count>();
  constexpr auto pattern_storage = parameterized_patterns<Type>(
      parameters, std::make_index_sequence<field_count>{});
  const auto defaults = pattern_views(pattern_storage);
  pattern_buffer<> output;
  append_aggregate_pattern<Type, Format, Opening>(output, defaults);
  return output;
}







#if defined(_MSC_VER)
#define SCAN_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define SCAN_FORCE_INLINE [[gnu::always_inline]] inline
#else
#define SCAN_FORCE_INLINE inline
#endif

// The text a format is spread into, and the machine of that text.
//
// A format is a way of writing a pattern, and everything built from it is
// built from the pattern it is written as. So the spread happens here and the
// layer below is asked for a machine by the text alone -- it has never heard
// of a format, and two formats that spread to the same characters are one
// machine to it.
template <class Type, fixed_string Format>
inline constexpr auto spread_text = [] {
  constexpr auto made = places_pattern<Type, Format>();
  fixed_string<made.length + 1> text{};
  for (std::size_t at = 0; at < made.length; ++at) {
    text.value[at] = made.storage[at];
  }
  text.anchored = Format.anchored;
  text.space_before_places = Format.space_before_places;
  return text;
}();

template <class Type, fixed_string Format, bool Cut = true>
inline constexpr auto& packed_automaton =
    packed_text_automaton<spread_text<Type, Format>, true, Cut>;

// The same, for the machine that gathers as it reads: its registers are not
// allocated, because a gathering follows the register its tag is in and
// allocation would put two tags in one place.
//
// This is the whole of it, with a mark for every tag the expression writes. It
// is what the question "which of those marks will anybody read" is asked of --
// and the machine that answer builds, which is the one everything walks, is
// further down.
template <class Type, fixed_string Format, bool Cut = true>
inline constexpr auto& streaming_automaton_whole =
    packed_text_automaton<spread_text<Type, Format>, false, Cut>;

// The head of the input and the fields out of it, in one walk.
//
// Finding where a head ends and reading what is in it were two walks over the
// same characters: the first carried no registers and threw away everything
// but the length, the second began again. The walk carries the registers, so
// what the second walk went to fetch is already in hand.
//
// The head ends at the last place the machine stood in a match, which is not
// always where it stopped: a walk can go on past a match, on the chance of a
// longer one that the order of the alternatives prefers, and die without
// finding it. `foreach|for|each` reading "fore" is that -- it goes past `for`
// after the `e`, dies at the end, and the answer is the place it kept.
template <class Type, fixed_string Format, bool AbsentIsEmpty = false>
[[nodiscard]] constexpr auto taken_prefix_fields(std::string_view input) {
  constexpr const auto& automaton = packed_automaton<Type, Format>;
  constexpr std::size_t group_count = automaton.tag_count / 2;
  struct answer {
    std::string_view head;
    std::array<std::string_view, group_count> groups{};
    bool matched = false;
  };
  answer said;
  const char* const begin = input.data();
  register_file<const char*, automaton.register_count> registers{};
  constexpr auto written_everywhere = tags_always_written<automaton>();
  [&]<std::size_t... tag>(std::index_sequence<tag...>) {
    ((written_everywhere[tag] ? void() : slot_write(registers, tag, nullptr)), ...);
  }(std::make_index_sequence<automaton.tag_count>{});
  execute_initial<automaton>(registers, begin);

  gathers_nothing nothing;
  // Where the machine can read past a match and die away from one, the
  // registers of the head are not the registers it died holding, so the note
  // keeps them. Where it cannot, the note is a pointer and nothing else.
  constexpr bool walks_past = walk_past_a_match<automaton>() != 0;
  using kept_type =
      std::conditional_t<walks_past,
                         register_file<const char*,
                                       automaton.register_count>,
                         nothing_kept>;
  walk_answer<const char*, kept_type> best;
  const char* cursor = begin;
  const char* place = begin;
  constexpr walk_shape shape{.longest = true};
  if (!run_continuation<automaton, shape, automaton.initial, shape.budget, 0,
                        const char*>(cursor, begin + input.size(), place,
                                     registers, nothing, best)) {
    return said;
  }
  said.matched = true;
  said.head =
      std::string_view(begin, static_cast<std::size_t>(*best.at - begin));
  // The registers of the head: the ones kept where it ended, or the ones in
  // hand where the machine could not have gone past it.
  const auto& said_by = [&]() -> const auto& {
    if constexpr (walks_past) {
      return best.kept;
    } else {
      return registers;
    }
  }();
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    ((said.groups[group] = [&]() -> std::string_view {
        const char* const from = mark_at<group * 2>(said_by);
        const char* const to = mark_at<group * 2 + 1>(said_by);
        // Pointing nowhere is how a group that took no part is said, here as
        // everywhere: whether that is a failure is decided by whoever asked,
        // and there is nothing to throw it at from inside a walk.
        if (from == nullptr || to == nullptr) return std::string_view{};
        return std::string_view(from, static_cast<std::size_t>(to - from));
      }()),
     ...);
  }(std::make_index_sequence<group_count>{});
  return said;
}

template <class Type, fixed_string Format>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::string_view taken_prefix_or_none(
    std::string_view input) {
  const char* const begin = input.data();
  const char* best = nullptr;
  if constexpr (automata_at_runtime) {
    best = run_prefix_runtime(runtime_text_automaton<spread_text<Type, Format>>(), begin,
                              begin + input.size());
  } else {
    constexpr const auto& automaton = packed_automaton<Type, Format>;
    register_file<const char*, automaton.register_count> registers{};
    best = run_head<automaton, automaton.initial>(begin, begin + input.size(),
                                                  registers);
  }
  if (best == nullptr) return {};
  return std::string_view(begin, static_cast<std::size_t>(best - begin));
}

// Whether the pattern is happy with nothing at all. Reading one match after
// another, such a pattern never moves and the reading never ends.
template <auto& Automaton>
[[nodiscard]] consteval bool matches_nothing() {
  return Automaton.states[Automaton.initial].accepting_slot !=
         packed_state<0, 0, 0>::not_accepting;
}

template <class Type, fixed_string Format, int Sentinel, bool Terminated,
          bool AbsentIsEmpty, how_to_walk Walk, class Ending,
          std::size_t... Index>
[[nodiscard]] [[gnu::flatten]] SCAN_FORCE_INLINE constexpr auto scan_fields(
    std::string_view input, std::index_sequence<Index...>) ->
    typename Ending::template result<std::array<std::string_view,
                                                sizeof...(Index)>,
                                     scan::failure> {
  // A group that took no part points nowhere, and that is how it is said all
  // the way through here: the three walks below each write it, and one place
  // at the end decides whether it is a failure or the ordinary state of
  // affairs. Nothing throws, because nothing here would be caught.
  using groups_type = std::array<std::string_view, sizeof...(Index)>;
  using answer_type = typename Ending::template result<groups_type,
                                                       scan::failure>;
  const auto answer = [](groups_type made) -> answer_type {
    if constexpr (!AbsentIsEmpty) {
      for (const std::string_view one : made) {
        if (one.data() == nullptr) {
          return Ending::template went_wrong<groups_type, scan::failure>(
              no_group<>("capture group did not participate in the match"));
        }
      }
    }
    return made;
  };
  if consteval {
    const auto matched = scan::tre::simulate(build_text_tnfa<spread_text<Type, Format>>(), input);
    if (!matched.matched) {
      return Ending::template went_wrong<groups_type, scan::failure>(
          no_match<>("input does not match scan expression"));
    }
    const auto capture = [&]<std::size_t capture_index>() -> std::string_view {
      const auto& begins = matched.tags[capture_index * 2];
      const auto& ends = matched.tags[capture_index * 2 + 1];
      if (begins.empty() || ends.empty()) return std::string_view{};
      const auto begin = begins.back();
      const auto end = ends.back();
      if (begin < 0 || end < begin) return std::string_view{};
      return input.substr(static_cast<std::size_t>(begin),
                          static_cast<std::size_t>(end - begin));
    };
    return answer(std::array{capture.template operator()<Index>()...});
  } else {
    // A compound statement, because that is what `if consteval` is written
    // with: the branch that is not the constant-evaluated one is a block, and
    // the choice of machine is made inside it.
    if constexpr (automata_at_runtime) {
      // Nothing here is a constant: the automaton is a value built on first use
      // and the walk is a loop over it. Not one instantiation per state, not one
      // determinisation per pattern while compiling.
      const scan::tre::tdfa& automaton = runtime_text_automaton<spread_text<Type, Format>>();
      std::vector<const char*> registers(automaton.register_count, nullptr);
      if (!run_tagged_runtime(automaton, input.data(),
                              input.data() + input.size(), registers)) {
        return Ending::template went_wrong<groups_type, scan::failure>(
            no_match<>("input does not match scan expression"));
      }
      const auto capture = [&]<std::size_t capture_index>() -> std::string_view {
        const char* const begin = mark_at<capture_index * 2>(registers);
        const char* const end = mark_at<capture_index * 2 + 1>(registers);
        // A group that took no part is an error where every group was meant to
        // take part, and the ordinary state of affairs where the format has
        // branches and only one of them ran. Which it is, is decided once, at
        // the end.
        if (begin == nullptr || end == nullptr) return std::string_view{};
        return std::string_view(begin, static_cast<std::size_t>(end - begin));
      };
      return answer(std::array{capture.template operator()<Index>()...});
    } else {
      // Anchored to both ends of the subject, so the walks below a match are
      // kept: one of them may be the only walk that reaches the end, and the
      // answer is the first still accepting when it does.
      constexpr const auto& automaton = packed_automaton<Type, Format, false>;
      // What is made of the marks, said here and done inside the walk.
      //
      // The walk cannot be folded into this function -- its states are labels,
      // and a label is not a thing an inliner moves -- so a mark file this
      // function owns is one whose address that walk has seen, and one no
      // compiler will take apart: it stays on the stack, written at every
      // boundary and read again here. Handed this instead, the walk owns the
      // marks and keeps them wherever values go, and what comes back is the
      // answer.
      const auto build = [&](const auto& registers, bool matched) {
        if (!matched) {
          return Ending::template went_wrong<groups_type, scan::failure>(
              no_match<>("input does not match scan expression"));
        }
      constexpr auto always_written = tags_always_written<automaton>();
        const auto capture = [&]<std::size_t capture_index>() -> std::string_view {
          const auto begin = mark_at<capture_index * 2>(registers);
          const auto end = mark_at<capture_index * 2 + 1>(registers);
          if constexpr (!(always_written[capture_index * 2] &&
                          always_written[capture_index * 2 + 1])) {
            // A group that took no part points nowhere, which no group that did
            // take part does. Whether that is a failure is decided once, at the
            // end, and not here.
            if (begin == nullptr) return std::string_view{};
          }
          return std::string_view(begin, static_cast<std::size_t>(end - begin));
        };
        // Where the automaton writes every tag on every path, no group can have
        // taken no part, and the question the walk at the end asks is already
        // answered -- which is what the capture above leaves out, and not what
        // this hands back. Said as the same kind on every road out of here: one
        // road out of this lambda is the failure above, so a road that hands
        // back the bare array is a return type that cannot be deduced at all.
        // It compiled for as long as nobody tried rather than asked on a
        // pattern whose every tag is always written.
        return answer(std::array{capture.template operator()<Index>()...});
      };
      constexpr unsigned char terminator =
          Sentinel >= 0 ? static_cast<unsigned char>(Sentinel) : 0;
      constexpr bool by_terminator =
          Sentinel >= 0 ||
          (Terminated && is_safe_tagged_sentinel<automaton, terminator>());
      static_assert(!by_terminator ||
                        is_safe_tagged_sentinel<automaton, terminator>(),
                    "the terminator must be rejected in every state");
      // One walk, said four ways and called once.
      //
      // Whether there is a terminator is a fact about the caller and the
      // pattern, and whether the subject is worth reading in words is a fact
      // about its length -- so there are four shapes and one call. Said as
      // four call sites, each would be a place where the marks could be owned
      // by somebody else again, which is what this was before.
      constexpr auto shape_for = [](bool in_words) {
        walk_shape made{.in_words = in_words,
                        .budget = bodies_worth_writing<automaton>()};
        if constexpr (by_terminator) {
          made.by_terminator = true;
          made.terminator = terminator;
        }
        return made;
      };
      const char* const from = input.data();
      const char* const upto = from + input.size();
      const auto go = [&]<walk_shape shape>() {
        return run_owning<automaton, shape, automaton.initial, shape.budget, 0,
                          const char*, false, const char*, const char*,
                          automaton.register_count, gathers_nothing,
                          walk_answer<const char*>, decltype(build)>(
            from, upto, from, from, build);
      };
      constexpr std::size_t worth_a_word = worth_reading_in_words<automaton>();
      constexpr bool asks = Walk == how_to_walk::by_length;
      if (asks ? input.size() < worth_a_word
               : Walk == how_to_walk::one_at_a_time) {
        return go.template operator()<shape_for(false)>();
      }
      return go.template operator()<shape_for(true)>();
    }
  }
}

template <class Type, fixed_string Format, int Sentinel = -1,
          bool Terminated = false,
          how_to_walk Walk = how_to_walk::by_length,
          class Ending = hands_a_failure_back>
[[nodiscard]] SCAN_FORCE_INLINE constexpr auto scan_fields(
    std::string_view input) {
  return scan_fields<Type, Format, Sentinel, Terminated, false, Walk, Ending>(
      input, std::make_index_sequence<groups_of_output<Type>()>{});
}

// Every group of every branch, with the ones that took no part left empty.
//
// The count comes from the automaton and not from the output type: a format
// with branches has a group for each branch on top of the ones written down,
// and that is how the scan says which branch the input took.
template <class Type, fixed_string Format, int Sentinel = -1,
          bool Terminated = false,
          how_to_walk Walk = how_to_walk::by_length,
          class Ending = hands_a_failure_back>
[[nodiscard]] SCAN_FORCE_INLINE constexpr auto scan_branch_fields(
    std::string_view input) {
  return scan_fields<Type, Format, Sentinel, Terminated, true, Walk, Ending>(
      input, std::make_index_sequence<groups_of_output<Type>()>{});
}

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
template <class Type, bool AsOutput = false>
[[nodiscard]] consteval bool never_fails() {
  constexpr bool a_value = scanned_as_leaf<Type> && !AsOutput;
  if constexpr (a_value && reads_its_own_groups<Type>) {
    return false;
  } else if constexpr (a_value) {
    return !scan::says_what_went_wrong<std::remove_cv_t<Type>>;
  } else if constexpr (scanned_as_variant<Type>) {
    return false;
  } else if constexpr (scanned_as_range<Type>) {
    return false;
  } else {
    return []<std::size_t... index>(std::index_sequence<index...>) {
      return (true && ... &&
              never_fails<typename parts_of<Type>::template at<index>>());
    }(std::make_index_sequence<parts_of<Type>::count>{});
  }
}

// Contexts told place by place, with braces for the parts of a place.
//
// `of` calls at once, so everything a caller writes in its arguments is alive
// for the whole of the call. That is what makes braces possible: a parameter
// whose type does not depend on what the caller hands over can be written out,
// and a braced list needs nothing deduced. The type of the thing handed over is
// forgotten at the door and the call that needs it back is written where it is
// still known -- so the scanner is handed the caller's own thing, by its own
// type, and nothing about it reaches any type the automaton is built from.
//
// The places are the fields of the output, in the order they are read. A value
// at a place is that place's and every part of it; a braced list at a place says
// its parts one by one, as deep as the shape goes. Eight places at a level,
// which is where the writing-out stops.

// Where there is no place to say anything at.
struct no_place {
  static constexpr bool told_apart = false;
  constexpr no_place() = default;
  constexpr no_place(scan::default_context_t) {}
  template <class It>
  [[nodiscard]] static constexpr no_place spread(const It&) {
    return {};
  }
  template <class Store, class It>
  [[nodiscard]] static constexpr no_place wire(Store&, It&&) {
    return {};
  }
  template <std::size_t>
  [[nodiscard]] constexpr scan::nothing_given for_part() const {
    return {};
  }
  [[nodiscard]] constexpr scan::default_context_t leaf() const { return {}; }
};

// How many parts a place opens up into, and zero for one that is read whole.
template <class Type>
[[nodiscard]] consteval std::size_t parts_under() {
  if constexpr (scanned_as_variant<Type> || scanned_as_leaf<Type>) {
    return 0;
  } else if constexpr (std::is_aggregate_v<Type> &&
                       requires { parts_of<Type>::count; }) {
    return parts_of<Type>::count;
  } else {
    return 0;
  }
}

// Which field of a shape a group falls in: the last one that begins at or
// before it.
template <class Type, std::size_t Group>
[[nodiscard]] consteval std::size_t field_holding_group() {
  std::size_t found = 0;
  [&]<std::size_t... which>(std::index_sequence<which...>) {
    ((groups_before_field<Type, which>() <= Group ? (found = which) : found),
     ...);
  }(std::make_index_sequence<parts_of<Type>::count>{});
  return found;
}

// Which branch of one-of-several a group belongs to: the branches stand in
// order, each taking its mark and whatever it reads after it.
template <class Type, std::size_t Group>
[[nodiscard]] consteval std::size_t branch_holding_group() {
  std::size_t found = 0;
  [&]<std::size_t... which>(std::index_sequence<which...>) {
    ((groups_before_branch<Type, which>() <= Group ? (found = which) : found),
     ...);
  }(std::make_index_sequence<branch_count<Type>()>{});
  return found;
}

// What a part of a place was told. A carrier knows its parts; a place told
// nothing has no parts to ask about and says the same nothing to each of them,
// which is what the whole-place forms do one level up.
template <std::size_t Part, class ToldCarrier>
[[nodiscard]] constexpr auto told_for_part(const ToldCarrier& given) {
  if constexpr (requires { given.template for_part<Part>(); }) {
    return given.template for_part<Part>();
  } else {
    return given;
  }
}

// The context said at the place a group belongs to.
//
// The walk knows groups; the caller said places. This walks down the shape the
// same way the value is built, so a group of a place inside a place inside the
// output ends at the context that place was given.
template <class Type, std::size_t Group, class Carrier>
[[nodiscard]] constexpr auto context_at_group(const Carrier& given) {
  if constexpr (scanned_as_variant<std::remove_cv_t<Type>> &&
                requires { given.template for_part<0>(); }) {
    // One of several opens up into its branches: the group belongs to the one
    // that stands where it stands, and that branch was told its own. Asked of
    // a place that was handed a context rather than a list of them, this is
    // not the way down -- the same one goes to whichever branch runs.
    using kind = std::remove_cv_t<Type>;
    constexpr std::size_t which = branch_holding_group<kind, Group>();
    constexpr std::size_t inside = Group - groups_before_branch<kind, which>();
    return context_at_group<std::remove_cv_t<branch_at<kind, which>>,
                            (inside == 0 ? 0 : inside - 1)>(
        told_for_part<which>(given));
  } else if constexpr (!requires { given.leaf(); } &&
                       !requires { given.template for_part<0>(); }) {
    // Already a context and not a carrier: a place that was handed one
    // directly hands the same one down. A carrier says one of these two --
    // a leaf answers `leaf`, a shape answers `for_part` -- and a shape used
    // to be mistaken for a context here, which is how a context said in
    // braces stopped at the door of a place that gathers.
    return given;
  } else if constexpr (parts_under<std::remove_cv_t<Type>>() == 0) {
    return given.leaf();
  } else {
    constexpr std::size_t which = field_holding_group<Type, Group>();
    using part = std::remove_cv_t<typename parts_of<Type>::template at<which>>;
    return context_at_group<part, Group - groups_before_field<Type, which>()>(
        given.template for_part<which>());
  }
}

// One leaf read with a context, said as an interface that knows the field.
//
// The field is what the answer is made of, so the interface can be written out
// here, where the place is; the context is what the answer is made with, and it
// is the one thing this cannot name. So the interface is declared knowing the
// field and implemented knowing the context, and the implementation is one
// object per pair of them -- constant, shared, and alive as long as the program
// is. What crosses the door is a pointer to the interface and a pointer to the
// context, and both are pointers to something whose type is known again on the
// other side.
template <class Held, bool ToldApart, class ContextType>
[[nodiscard]] constexpr std::expected<Held, failure_for<Held>> groups_value(
    std::span<const std::string_view> groups, ContextType&& given);

template <class Told>
[[nodiscard]] constexpr std::pmr::memory_resource* resource_of(const Told& given);

// The state a scanner that gathers begins with. A scanner told a context and a
// scanner told none begin the same kind of state -- that is what lets a place
// hand the context over without the type of it crossing the door.
// The state a scanner that is handed pieces begins with -- the other gathering
// protocol, and the same rule: told a context or told none, what it begins is
// the same kind of thing, so a place can hand the context over without the type
// of it crossing the door.
template <class Held>
using gather_state_for = decltype([] {
  if constexpr (requires {
                  scan::scanner<std::remove_cv_t<Held>>{}.begin(
                      std::string_view{});
                }) {
    return scan::scanner<std::remove_cv_t<Held>>{}.begin(std::string_view{});
  } else if constexpr (requires {
                         scan::scanner<std::remove_cv_t<Held>>{}.begin();
                       }) {
    return scan::scanner<std::remove_cv_t<Held>>{}.begin();
  } else {
    return scan::nothing_given{};
  }
}());

template <class Held>
using fold_state_for = decltype([] {
  if constexpr (requires { scan::scanner<std::remove_cv_t<Held>>{}.begin_groups(); }) {
    return scan::scanner<std::remove_cv_t<Held>>{}.begin_groups();
  } else {
    return scan::nothing_given{};
  }
}());

template <class FieldType>
struct reading_of {
  using held = std::remove_cv_t<FieldType>;
  using answer = std::expected<held, failure_for<held>>;

  constexpr virtual ~reading_of() = default;
  // The resource the context keeps, where it keeps one. Answered rather than
  // named: a resource is already a thing asked at runtime.
  [[nodiscard]] constexpr virtual std::pmr::memory_resource* told_resource()
      const = 0;
  // The same, for a scanner handed its pieces rather than its groups.
  [[nodiscard]] constexpr virtual gather_state_for<held> begin_gather(
      std::string_view parameters) = 0;
  // Begun where the context still has its type, handed back as the state the
  // scanner would have begun anyway.
  [[nodiscard]] constexpr virtual fold_state_for<held> begin_fold() = 0;
  [[nodiscard]] constexpr virtual answer read(std::string_view text,
                                              std::string_view parameters) = 0;
  [[nodiscard]] constexpr virtual answer read_groups(
      std::span<const std::string_view> groups) = 0;
};

// The implementation is written where the context still has its type, so it
// holds the caller's own thing -- not a copy of it, and not a const picture of
// it either: a context is a place to keep things while a reading runs, and a
// scanner that is told one may write in it. It is made at the place the context
// is said, as a default argument, so it lives exactly as long as the call.
template <class FieldType, class ContextType, bool ToldApart>
struct reading_by final : reading_of<FieldType> {
  using held = std::remove_cv_t<FieldType>;
  using answer = std::expected<held, failure_for<held>>;

  ContextType* kept = nullptr;

  static constexpr bool by_groups =
      requires { scan::scanner<held>{}.begin_groups(); } ||
      requires(std::span<const std::string_view> some) {
        scan::scanner<held>{}.from_groups(some);
      };

  // Both of these are written out with the table whether anybody calls them or
  // not, so each has to say what it does for a leaf that is read the other way
  // -- and saying it is an answer, not a failure to compile.
  static constexpr bool takes_a_context =
      requires(std::string_view text, std::string_view parameters,
               ContextType& told) {
        scan::scanner<held>{}.parse(text, told);
      } || requires(std::string_view text, std::string_view parameters,
                    ContextType& told) {
        scan::scanner<held>{}.parse(text, parameters, told);
      } || requires(std::string_view text, ContextType& told) {
        scan::scanner<held>::try_parse(text, told);
      } || requires(std::string_view text, std::string_view parameters,
                    ContextType& told) {
        scan::scanner<held>::try_parse(text, parameters, told);
      };

  static constexpr bool reads_a_piece =
      requires(std::string_view text) { scan::scanner<held>{}.parse(text); } ||
      requires(std::string_view text) { scan::scanner<held>::try_parse(text); };

  [[nodiscard]] constexpr std::pmr::memory_resource* told_resource()
      const override {
    return resource_of(*kept);
  }

  [[nodiscard]] constexpr gather_state_for<held> begin_gather(
      std::string_view parameters) override {
    if constexpr (requires {
                    scan::scanner<held>{}.begin(parameters, *kept);
                  }) {
      return scan::scanner<held>{}.begin(parameters, *kept);
    } else if constexpr (requires { scan::scanner<held>{}.begin(*kept); }) {
      return scan::scanner<held>{}.begin(*kept);
    } else if constexpr (requires { scan::scanner<held>{}.begin(parameters); }) {
      return scan::scanner<held>{}.begin(parameters);
    } else if constexpr (requires { scan::scanner<held>{}.begin(); }) {
      return scan::scanner<held>{}.begin();
    } else {
      return scan::nothing_given{};
    }
  }

  [[nodiscard]] constexpr fold_state_for<held> begin_fold() override {
    if constexpr (requires { scan::scanner<held>{}.begin_groups(*kept); }) {
      return scan::scanner<held>{}.begin_groups(*kept);
    } else if constexpr (requires { scan::scanner<held>{}.begin_groups(); }) {
      return scan::scanner<held>{}.begin_groups();
    } else {
      return scan::nothing_given{};
    }
  }

  [[nodiscard]] constexpr answer read(std::string_view text,
                                      std::string_view parameters) override {
    if constexpr (takes_a_context) {
      return parse_value_given<held, failure_for<held>, ToldApart>(
          text, parameters, *kept);
    } else if constexpr (reads_a_piece) {
      static_assert(!ToldApart || by_groups,
                    "this place was given a context of its own and its scanner "
                    "takes none: write parse(string_view, context) on "
                    "scan::scanner<T>, or write scan::default_context in its "
                    "place");
      return parse_value<held, failure_for<held>>(text, parameters);
    } else {
      static_cast<void>(text);
      static_cast<void>(parameters);
      return std::unexpected(scan::as_a_failure<failure_for<held>>(
          scan::no_group<>("this place is not read from a piece")));
    }
  }

  // A virtual is written out with the table, whether anybody calls it or not,
  // so a leaf that is not read from its groups must still have something here
  // -- and what it has says so rather than failing to compile.
  [[nodiscard]] constexpr answer read_groups(
      std::span<const std::string_view> groups) override {
    if constexpr (by_groups) {
      return groups_value<held, ToldApart>(groups, *kept);
    } else {
      static_cast<void>(groups);
      return std::unexpected(scan::as_a_failure<failure_for<held>>(
          scan::no_group<>("this place is not read from its groups")));
    }
  }
};

// One context said for a whole shape needs one reading per leaf under it, and
// those readings have to outlive the call that says it. They are made as a
// default argument -- in the caller's own full expression -- and wired to the
// places here.
template <class Carrier, class It>
struct readings_for;

// One leaf read with a context, said as an interface that knows the field.
//
// The field is what the answer is made of, so the interface can be written out
// here, where the place is; the context is what the answer is made with, and it
// is the one thing this cannot name. So the type of the context is worked out
// where it is said -- deduced in the constructor below, from the thing itself
// -- and the reading that knows it is made there too, as a default argument, so
// it is alive for as long as the expression that said it. What crosses the door
// is a pointer to the interface, and nothing here ever names a context type.
template <class FieldType>
class context_leaf {
 public:
  using held = std::remove_cv_t<FieldType>;
  using answer = std::expected<held, failure_for<held>>;
  static constexpr bool told_apart = false;

  constexpr context_leaf() = default;
  constexpr context_leaf(scan::default_context_t) {}

  // The context keeps whatever it was said as: a named thing stays that named
  // thing, a temporary lives to the end of the expression, and a const one
  // stays const because the caller wrote it that way -- nothing here adds a
  // const of its own.
  template <class It>
    requires(!std::same_as<std::remove_cvref_t<It>, context_leaf> &&
             !std::same_as<std::remove_cvref_t<It>, scan::default_context_t> &&
             !std::same_as<std::remove_cvref_t<It>, no_place>)
  constexpr context_leaf(
      It&& given,
      reading_by<held, std::remove_reference_t<It>, true>&& made = {})
      : how_(&made) {
    made.kept = &given;
  }

  // The same leaf, told a context that was said for the shape above it.
  template <class Store, class It>
  [[nodiscard]] static constexpr context_leaf wire(Store& made, It&& given) {
    made.kept = &given;
    context_leaf done;
    done.how_ = &made;
    return done;
  }

  // A leaf's inside is the leaf's own business: a context said at this place was
  // said about the value here, not about what this value is built from.
  template <std::size_t>
  [[nodiscard]] constexpr scan::nothing_given for_part() const {
    return {};
  }
  [[nodiscard]] constexpr const context_leaf& leaf() const { return *this; }

  [[nodiscard]] constexpr bool told() const { return how_ != nullptr; }
  [[nodiscard]] constexpr fold_state_for<held> begin_fold() const {
    return how_->begin_fold();
  }
  [[nodiscard]] constexpr gather_state_for<held> begin_gather(
      std::string_view parameters) const {
    return how_->begin_gather(parameters);
  }
  [[nodiscard]] constexpr std::pmr::memory_resource* told_resource() const {
    return how_ == nullptr ? nullptr : how_->told_resource();
  }
  [[nodiscard]] constexpr answer read(std::string_view text,
                                      std::string_view parameters) const {
    return how_->read(text, parameters);
  }
  [[nodiscard]] constexpr answer read_groups(
      std::span<const std::string_view> groups) const {
    return how_->read_groups(groups);
  }

 private:
  reading_of<held>* how_ = nullptr;
};

template <class... Parts>
class context_shape {
 public:
  static constexpr bool told_apart = true;

  constexpr context_shape() = default;
  constexpr context_shape(scan::default_context_t) {}

  // One value at a place is that place's and every part of it. The readings for
  // everything under it are made here, as a default argument, and wired below.
  template <class It>
    requires(!std::same_as<std::remove_cvref_t<It>, context_shape> &&
             !std::same_as<std::remove_cvref_t<It>, scan::default_context_t> &&
             !std::same_as<std::remove_cvref_t<It>, no_place>)
  constexpr context_shape(It&& given,
                          typename readings_for<context_shape, It>::type&& made =
                              {})
      : context_shape(wire(made, given)) {}

  // Or its parts, one by one, in braces -- all of them, because a shape whose
  // parts are being told apart is being told apart. Each part is a carrier, so
  // a part in braces goes as deep as the shape does, and a part said as a plain
  // context works out its own type where it stands.
  constexpr context_shape(Parts... given) : parts_(given...) {}

  template <class Store, class It>
  [[nodiscard]] static constexpr context_shape wire(Store& made, It&& given) {
    return wire_parts(made, given, std::index_sequence_for<Parts...>{});
  }

  template <std::size_t K>
  [[nodiscard]] constexpr auto for_part() const {
    if constexpr (K < sizeof...(Parts)) {
      return std::get<K>(parts_);
    } else {
      return scan::nothing_given{};
    }
  }

 private:
  template <class Store, class It, std::size_t... K>
  [[nodiscard]] static constexpr context_shape wire_parts(
      Store& made, It&& given, std::index_sequence<K...>) {
    return context_shape(Parts::wire(std::get<K>(made), given)...);
  }

  std::tuple<Parts...> parts_{};
};

template <class FieldType, class It>
struct readings_for<context_leaf<FieldType>, It> {
  using type = reading_by<std::remove_cv_t<FieldType>,
                          std::remove_reference_t<It>, false>;
};

template <class It, class... Parts>
struct readings_for<context_shape<Parts...>, It> {
  using type = std::tuple<typename readings_for<Parts, It>::type...>;
};

template <class It>
struct readings_for<no_place, It> {
  using type = scan::nothing_given;
};

// How many places a context may be said at, for one field. A shape opens up
// into its parts; one of several opens up into its branches, because exactly
// one of them runs and the caller may want to say something to each; anything
// read whole is one place and takes one context.
template <class FieldType>
[[nodiscard]] consteval std::size_t carrier_places() {
  if constexpr (scanned_as_variant<FieldType>) {
    return branch_count<FieldType>();
  } else {
    return parts_under<FieldType>();
  }
}

template <class FieldType, std::size_t K>
[[nodiscard]] consteval auto carrier_place_kind() {
  if constexpr (scanned_as_variant<FieldType>) {
    return std::type_identity<std::remove_cv_t<branch_at<FieldType, K>>>{};
  } else {
    return std::type_identity<
        std::remove_cv_t<typename parts_of<FieldType>::template at<K>>>{};
  }
}

template <class FieldType, std::size_t K>
using carrier_place_for = typename decltype(carrier_place_kind<FieldType, K>())::type;

// Which carrier a field wants: read whole, and it is a leaf; opening up into
// places, and it is a shape over their carriers, worked out the same way.
template <class FieldType, bool Whole = (carrier_places<FieldType>() == 0)>
struct carrier_of;

template <class FieldType>
struct carrier_of<FieldType, true> {
  using type = context_leaf<FieldType>;
};

template <class FieldType>
struct carrier_of<FieldType, false> {
  template <std::size_t... K>
  static auto made(std::index_sequence<K...>)
      -> context_shape<
          typename carrier_of<carrier_place_for<FieldType, K>>::type...>;
  using type =
      decltype(made(std::make_index_sequence<carrier_places<FieldType>()>{}));
};

template <class FieldType>
using carrier_for = typename carrier_of<std::remove_cv_t<FieldType>>::type;

template <class Type, std::size_t Place>
[[nodiscard]] consteval auto context_place_kind() {
  if constexpr (carrier_places<Type>() == 0) {
    if constexpr (Place == 0) {
      return std::type_identity<context_leaf<std::remove_cv_t<Type>>>{};
    } else {
      return std::type_identity<no_place>{};
    }
  } else if constexpr (Place < carrier_places<Type>()) {
    return std::type_identity<carrier_for<carrier_place_for<Type, Place>>>{};
  } else {
    return std::type_identity<no_place>{};
  }
}

template <class Type, std::size_t Place>
using context_place_of = typename decltype(context_place_kind<Type, Place>())::type;

// The memory resource a context keeps, where it keeps one.
//
// An allocator says it, a resource is one, a thing that hands either back says
// it too, and a place told in braces is asked through the interface that
// carries it -- a resource is already a thing answered at runtime, so nothing
// of the context's type has to cross the door for this.
template <class Told>
[[nodiscard]] constexpr std::pmr::memory_resource* resource_of(
    const Told& given) {
  using kind = std::remove_cvref_t<Told>;
  if constexpr (std::same_as<kind, std::pmr::memory_resource*>) {
    return given;
  } else if constexpr (requires { given.resource(); }) {
    return given.resource();
  } else if constexpr (requires { given.get_allocator().resource(); }) {
    return given.get_allocator().resource();
  } else if constexpr (requires { given.told_resource(); }) {
    return given.told_resource();
  } else if constexpr (requires { given.leaf(); }) {
    // A context said at a call is carried in a wrapper -- one for everybody,
    // one per place -- and what it keeps is inside. Asked after the carrier,
    // because a carrier answers `leaf` with itself.
    return resource_of(given.leaf());
  } else {
    static_cast<void>(given);
    return nullptr;
  }
}

// A list built where its place said to build it. A container that takes an
// allocator is given the one its place was told about; one that takes none is
// made the way it always was.
template <class Held, std::size_t Most = turns_unbounded, class Told>
[[nodiscard]] constexpr Held made_range(const Told& given) {
  // What the format could ask for, against what this container holds. A
  // container that says nothing says nothing here either.
  if constexpr (requires { scan::room_for<Held>::most; }) {
    static_assert(Most <= scan::room_for<Held>::most,
                  "this place may take more turns than the container it is "
                  "read into has room for: say a count in braces after the "
                  "place, or read it into something with more room");
  }
  const auto begun = [&] {
    if constexpr (requires {
                    typename Held::value_type;
                    Held(std::pmr::polymorphic_allocator<
                         typename Held::value_type>{});
                  }) {
      if (std::pmr::memory_resource* where = resource_of(given)) {
        return Held(
            std::pmr::polymorphic_allocator<typename Held::value_type>(where));
      }
      return Held{};
    } else {
      static_cast<void>(given);
      return Held{};
    }
  };
  Held made = begun();
  // Room for everything the format could ask for, taken once. A list whose
  // count has no end asks for nothing here: what it will be is not known, and
  // guessing it is the container's business and not this one's.
  if constexpr (Most != turns_unbounded) {
    if constexpr (requires { made.reserve(Most); }) made.reserve(Most);
  }
  return made;
}

// An empty list of the same kind as one that stands here already, keeping the
// resource that one was made with. A turn ending and the next one beginning is
// not a reason to go back to the default resource.
template <class Held>
[[nodiscard]] constexpr Held made_like(const Held& other) {
  if constexpr (requires { typename Held::allocator_type; }) {
    return Held(other.get_allocator());
  } else {
    static_cast<void>(other);
    return Held{};
  }
}

// A scanner begun with the context its place was given, asked for in the shapes
// it may have been written in, and begun the way it always was where it takes
// none.
template <class Held, class ToldType>
[[nodiscard]] constexpr auto scanner_begin_given(std::string_view parameters,
                                                 const ToldType& told) {
  // Only where what the carrier begins is the very thing this call hands back:
  // a scanner with no gathering of its own begins nothing, and the branches
  // below must all agree on one return type.
  if constexpr (requires {
                  told.told();
                  told.begin_gather(parameters);
                  requires std::same_as<decltype(told.begin_gather(parameters)),
                                        decltype(scanner_begin<Held>(parameters))>;
                }) {
    // A place told in braces: the context is behind an interface that knows the
    // field, and beginning is part of that interface.
    if (told.told()) return told.begin_gather(parameters);
    return scanner_begin<Held>(parameters);
  } else if constexpr (!std::same_as<ToldType, scan::default_context_t> &&
                requires { scan::scanner<Held>{}.begin(parameters, told); }) {
    return scan::scanner<Held>{}.begin(parameters, told);
  } else if constexpr (!std::same_as<ToldType, scan::default_context_t> &&
                       requires { scan::scanner<Held>{}.begin(told); }) {
    return scan::scanner<Held>{}.begin(told);
  } else if constexpr (requires {
                         scan::scanner<Held>{}.begin(
                             parameters, std::pmr::polymorphic_allocator<>{});
                       }) {
    // The place was told something that keeps a resource, and this scanner
    // knows what to do with one. Nobody wrote anything for this to happen.
    if (std::pmr::memory_resource* where = resource_of(told)) {
      return scan::scanner<Held>{}.begin(
          parameters, std::pmr::polymorphic_allocator<>(where));
    }
    return scanner_begin<Held>(parameters);
  } else {
    static_cast<void>(told);
    return scanner_begin<Held>(parameters);
  }
}

// The state of a leaf that is built from its own groups, told the context its
// place was given. The same rule as everywhere: asked for with the context
// first, and a scanner that takes none is begun the way it always was.
template <class Held, class Context>
[[nodiscard]] constexpr auto begun_groups(Context&& given) {
  // A place told in braces carries its context behind an interface that knows
  // the field. The type of the context does not cross the door, but the
  // beginning of a fold does: the carrier begins it where the type is still
  // known and hands back the state the scanner would have begun anyway.
  if constexpr (requires {
                  given.told();
                  given.begin_fold();
                }) {
    if (given.told()) return given.begin_fold();
    return scan::scanner<std::remove_cv_t<Held>>{}.begin_groups();
  } else if constexpr (!std::same_as<std::remove_cvref_t<Context>,
                              scan::default_context_t> &&
                !requires { given.told(); } &&
                requires { scan::scanner<Held>{}.begin_groups(given); }) {
    return scan::scanner<Held>{}.begin_groups(given);
  } else {
    static_cast<void>(given);
    return scan::scanner<Held>{}.begin_groups();
  }
}

// A leaf read from its own groups, told the context its place was given.
//
// Everything this leaf needs is here at once -- the groups are already found --
// so nothing of it crosses the door: the state is made, told and finished
// inside this one call, where the context still has its type.
template <class Held, bool ToldApart, class ContextType>
[[nodiscard]] constexpr std::expected<Held, failure_for<Held>> groups_value(
    std::span<const std::string_view> groups, ContextType&& given) {
  constexpr std::size_t inside = groups_a_leaf_opens<Held>();
  if constexpr (requires {
                  scan::scanner<Held>{}.from_groups(groups, given);
                }) {
    return scan::scanner<Held>{}.from_groups(groups, given);
  } else if constexpr (requires { scan::scanner<Held>{}.from_groups(groups); }) {
    return scan::scanner<Held>{}.from_groups(groups);
  } else {
    auto state = begun_groups<Held>(given);
    [&]<std::size_t... at>(std::index_sequence<at...>) {
      ((void)[&] {
        if (groups[at].data() == nullptr) return;
        open_one_group<Held, at>(state);
        close_one_group<Held, at>(state, groups[at]);
      }(), ...);
    }(std::make_index_sequence<inside>{});
    return scan::scanner<Held>{}.finish_groups(std::move(state));
  }
}

// The value itself, for a reading that cannot go wrong.
template <class Parameters, class Type, std::size_t Offset,
          bool AsOutput = false, class GivenType = scan::nothing_given>
[[nodiscard]] constexpr Type built_value(
    std::span<const std::string_view> groups,
    const GivenType& given = GivenType{}) {
  constexpr bool a_value = scanned_as_leaf<Type> && !AsOutput;
  if constexpr (a_value) {
    if constexpr (std::same_as<GivenType, scan::nothing_given>) {
      return scanner_parse<std::remove_cv_t<Type>>(groups[Offset],
                                                   Parameters::at(Offset));
    } else {
      return or_thrown(
          parse_value_given<std::remove_cv_t<Type>,
                            failure_for<std::remove_cv_t<Type>>,
                            GivenType::told_apart, scan::throws_a_failure>(
              groups[Offset], Parameters::at(Offset), given.leaf()));
    }
  } else if constexpr (scanned_from_values<Type>) {
    return [&]<std::size_t... index>(std::index_sequence<index...>) {
      return scan::scanner<std::remove_cv_t<Type>>{}.parse(
          built_value<Parameters, typename parts_of<Type>::template at<index>,
                      Offset + groups_before_field<Type, index>()>(
              groups, given.template for_part<index>())...);
    }(std::make_index_sequence<parts_of<Type>::count>{});
  } else {
    return [&]<std::size_t... index>(std::index_sequence<index...>) {
      return Type{
          built_value<Parameters, typename parts_of<Type>::template at<index>,
                      Offset + groups_before_field<Type, index>()>(
              groups, given.template for_part<index>())...};
    }(std::make_index_sequence<parts_of<Type>::count>{});
  }
}

template <class FailureType, class Parameters, class Type,
          std::size_t Offset, bool AsOutput = false,
          class Ending = hands_a_failure_back,
          class GivenType = scan::nothing_given>
[[nodiscard]] constexpr typename Ending::template result<Type, FailureType>
build_value(std::span<const std::string_view> groups,
            const GivenType& given = GivenType{}) {
  // Where this is the whole of what is being read, a shape that reads its own
  // groups is a product of places rather than a value in a place.
  constexpr bool a_value = scanned_as_leaf<Type> && !AsOutput;
  if constexpr (never_fails<Type, AsOutput>() &&
                !std::same_as<Ending, throws_a_failure>) {
    // Nothing here can hand a failure back, so nothing here holds one -- even
    // where the caller asked to be handed one.
    return built_value<Parameters, Type, Offset, AsOutput>(groups, given);
  } else if constexpr (a_value && reads_its_own_groups<Type>) {
    // The type's own groups are groups of this match, already found. It is
    // handed them, or told which of them each character belongs to -- the same
    // reading it gets where a subject arrives as it is read, so it reads the
    // same way in both places.
    using held = std::remove_cv_t<Type>;
    constexpr std::size_t inside = groups_a_leaf_opens<held>();
    // Where this place was told a context, the whole of this reading is that
    // context's: the groups are cut out here and handed over in one call.
    if constexpr (requires {
                    given.leaf().told();
                    given.leaf().read_groups(
                        std::span<const std::string_view>{});
                  }) {
      const auto told_here = given.leaf();
      if (told_here.told()) {
        std::array<std::string_view, inside> mine{};
        [&]<std::size_t... at>(std::index_sequence<at...>) {
          ((mine[at] = groups[Offset + 1 + at]), ...);
        }(std::make_index_sequence<inside>{});
        auto got = told_here.read_groups(std::span<const std::string_view>(mine));
        if (got) return std::move(*got);
        return Ending::template went_wrong<Type, FailureType>(
            scan::as_a_failure<FailureType>(std::move(got).error()));
      }
    }
    if constexpr (scan::says_what_went_wrong_from_groups<held> ||
                  requires(std::span<const std::string_view> given) {
                    scan::scanner<held>{}.from_groups(given);
                  }) {
      std::array<std::string_view, inside> theirs{};
      [&]<std::size_t... at>(std::index_sequence<at...>) {
        ((theirs[at] = groups[Offset + 1 + at]), ...);
      }(std::make_index_sequence<inside>{});
      const auto pieces = std::span<const std::string_view>(theirs);
      if constexpr (scan::says_what_went_wrong_from_groups<held>) {
        // Told where the type can take it, and asked the old way where it
        // cannot: a shape that reads its own groups need not take a context.
        auto got = [&] {
          if constexpr (requires {
                          scan::scanner_told_from_groups<held, Ending>(pieces,
                                                                       given);
                        }) {
            return scan::scanner_told_from_groups<held, Ending>(pieces, given);
          } else {
            return scan::scanner_told_from_groups<held, Ending>(pieces);
          }
        }();
        if (got) return std::move(*got);
        return Ending::template went_wrong<Type, FailureType>(
            std::move(got).error());
      } else if constexpr (requires {
                             scan::scanner<held>{}.from_groups(pieces, given);
                           }) {
        return scan::scanner<held>{}.from_groups(pieces, given);
      } else {
        return scan::scanner<held>{}.from_groups(pieces);
      }
    } else {
      auto state = begun_groups<held>(given.leaf());
      [&]<std::size_t... at>(std::index_sequence<at...>) {
        ((void)[&] {
          // A group that took no part in the match is not opened at all, which
          // is how the type is told it was not there.
          if (groups[Offset + 1 + at].data() == nullptr) return;
          open_one_group<held, at>(state);
          close_one_group<held, at>(state, groups[Offset + 1 + at]);
        }(), ...);
      }(std::make_index_sequence<inside>{});
      if constexpr (scan::says_what_went_wrong_folding<held>) {
        auto got =
            scan::scanner_told_finish_groups<held, Ending>(std::move(state));
        if (got) return std::move(*got);
        return Ending::template went_wrong<Type, FailureType>(
            std::move(got).error());
      } else {
        return scan::scanner<held>{}.finish_groups(std::move(state));
      }
    }
  } else if constexpr (a_value) {
    auto got = [&] {
      if constexpr (std::same_as<GivenType, scan::nothing_given>) {
        return parse_value<std::remove_cv_t<Type>, FailureType>(
            groups[Offset], Parameters::at(Offset));
      } else {
        return parse_value_given<std::remove_cv_t<Type>, FailureType,
                                 GivenType::told_apart>(
            groups[Offset], Parameters::at(Offset), given.leaf());
      }
    }();
    if (got) return std::move(*got);
    return Ending::template went_wrong<Type, FailureType>(
        std::move(got).error());
  } else if constexpr (scanned_as_variant<Type>) {
    // Exactly one branch ran, and its mark says so: a mark that took part
    // points into the subject, and the others point nowhere.
    using answer = typename Ending::template result<Type, FailureType>;
    return [&]<std::size_t... branch>(std::index_sequence<branch...>) -> answer {
      std::optional<answer> made;
      const auto take = [&]<std::size_t which>() {
        constexpr std::size_t mark = Offset + groups_before_branch<Type, which>();
        if (made || groups[mark].data() == nullptr) return;
        using alternative = branch_at<Type, which>;
        auto part = build_value<FailureType, Parameters, alternative, mark + 1,
                                false, Ending>(groups,
                                               told_for_part<which>(given));
        if (!Ending::read(part)) {
          made = Ending::template went_wrong<Type, FailureType>(
              Ending::failure(std::move(part)));
          return;
        }
        made = scan::branches<std::remove_cv_t<Type>>::template make<which>(
            Ending::value(std::move(part)));
      };
      (take.template operator()<branch>(), ...);
      if (!made) {
        return Ending::template went_wrong<Type, FailureType>(
            no_match<>("no branch of the format took the input"));
      }
      return std::move(*made);
    }(std::make_index_sequence<branch_count<Type>()>{});
  } else if constexpr (scanned_from_values<Type>) {
    // Made by the call it named, out of the values its places stood for. Each
    // of them is read first and the call is made after, because a value that
    // did not read is not an argument.
    using answer = typename Ending::template result<Type, FailureType>;
    return [&]<std::size_t... index>(std::index_sequence<index...>) -> answer {
      // Asked for a value, every step is the value it read and the call is
      // written out of them where they stand. Asked to try, each is held until
      // they are all in hand, because a call cannot be half made.
      if constexpr (std::same_as<Ending, throws_a_failure>) {
        return scan::scanner<std::remove_cv_t<Type>>{}.parse(
            build_value<FailureType, Parameters,
                        typename parts_of<Type>::template at<index>,
                        Offset + groups_before_field<Type, index>(), false,
                        Ending>(groups, given.template for_part<index>())...);
      } else {
        auto parts = std::tuple{build_value<
            FailureType, Parameters, typename parts_of<Type>::template at<index>,
            Offset + groups_before_field<Type, index>(), false, Ending>(
            groups, given.template for_part<index>())...};
        if (auto went_wrong = what_went_wrong<FailureType>(parts)) {
          return std::unexpected(std::move(*went_wrong));
        }
        return scan::scanner<std::remove_cv_t<Type>>{}.parse(
            std::move(*std::get<index>(parts))...);
      }
    }(std::make_index_sequence<parts_of<Type>::count>{});
  } else {
    using answer = typename Ending::template result<Type, FailureType>;
    return [&]<std::size_t... index>(std::index_sequence<index...>) -> answer {
      // The same two ways: written straight into the value, or held until they
      // are all in hand.
      if constexpr (std::same_as<Ending, throws_a_failure>) {
        return Type{build_value<FailureType, Parameters,
                                typename parts_of<Type>::template at<index>,
                                Offset + groups_before_field<Type, index>(),
                                false, Ending>(
            groups, given.template for_part<index>())...};
      } else {
        auto parts = std::tuple{build_value<
            FailureType, Parameters, typename parts_of<Type>::template at<index>,
            Offset + groups_before_field<Type, index>(), false, Ending>(
            groups, given.template for_part<index>())...};
        if (auto went_wrong = what_went_wrong<FailureType>(parts)) {
          return std::unexpected(std::move(*went_wrong));
        }
        return Type{std::move(*std::get<index>(parts))...};
      }
    }(std::make_index_sequence<parts_of<Type>::count>{});
  }
}

}  // namespace scan::detail

#undef SCAN_FORCE_INLINE

// A module keeps its macros to itself and a header does not, so they are
// taken back here rather than handed to whoever includes this.
#undef SCAN_FORCE_INLINE
