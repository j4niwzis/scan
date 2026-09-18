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
#include <bit>
#include <expected>
#include <iterator>
#include <limits>
#include <memory>
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

template <class Type, std::size_t Index>
using field_type = typename scan::fields<Type>::template at<Index>;

// Nothing is gathered at the place a leaf that reads its own groups stands on:
// what it is built from are its groups, and they are gathered each at its own.
struct no_gathering {};

// Whether any group of a type is handed over whole. Said here because a turn
// has to know it before the question below can be asked.
template <class Held, class StateType>
[[nodiscard]] consteval bool any_group_taken_whole();

// The address a mark stands on, or none where a mark is not an address.
//
// A subject that arrives a character at a time has counts for marks and
// nothing to point into, so there is no address and no group can be handed
// over whole. Asked through a function of its own so that the question is
// answered once, where the mark's type is known, instead of in every place
// that would rather not know.
template <class Mark>
[[nodiscard]] constexpr const char* pointed_at(Mark position) {
  if constexpr (std::is_pointer_v<Mark>) {
    return position;
  } else {
    return nullptr;
  }
}

// A fold, and what it has been told.
//
// The state is the type's own -- it says how it is made and what it holds. The
// two arrays beside it are the walk's bookkeeping: which turn of each group the
// type has been told about, and whether that turn is still open. They travel
// with the state, because a reading that divides carries its fold with it, and
// what one reading has been told the other has not.
//
// A turn is known by the position its group opened at. Positions only ever move
// forward, so a group whose opening has moved is a new turn and is announced;
// one whose opening stands still is the same turn going on.
template <class Held, class MarkType = std::ptrdiff_t,
          class ToldType = scan::default_context_t>
struct fold_turn {
  using held_type = std::remove_cv_t<Held>;
  static constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
  using state_type =
      decltype(begun_groups<held_type>(std::declval<const ToldType&>()));

  constexpr fold_turn()
    requires std::default_initializable<ToldType>
      : state(begun_groups<held_type>(ToldType{})) {}
  constexpr explicit fold_turn(const ToldType& told)
      : state(begun_groups<held_type>(told)) {}

  state_type state;
  // Where the walk stood, said the way the walk says it: a count where the
  // subject arrives a character at a time, an address where it lies in a row.
  // There the address is the cursor, which the walk is holding anyway, and a
  // count would be a step of its own on every character.
  std::array<MarkType, inside> told_at{};
  // And which closing it has been told about, for the same reason: a group
  // that is taken over and over writes its closing into the same register
  // every turn, so what says a turn has ended is that the position moved --
  // not that it stands anywhere in particular.
  std::array<MarkType, inside> ended_at{};
  // Which groups are open, a bit each. A byte each was an array to index on
  // every character, and which groups those are is known while this is
  // compiled -- so the whole of it is one word and a mask.
  std::uint64_t open = 0;
  // Whether this turn has been told anything at all. A place that has not been
  // stood on yet is not a turn that ended, and a list does not begin with one.
  bool started = false;
  // Where each group asked for whole began, as the address of its first
  // character.
  //
  // Not a mark: a mark is written by the machine in the machine's own
  // reckoning, held back to the step after the character that earned it, and
  // read out again by whoever knows that. This is the character itself, and it
  // is here because the step that opens a group is standing on it. Nothing at
  // all where no group is asked for whole, which is nearly always.
  static constexpr bool asked_whole =
      any_group_taken_whole<held_type, state_type>();
  [[no_unique_address]]
  std::array<const char*, asked_whole ? inside : 0> began_at{};
  // Whether it asked for something this subject cannot give: its groups whole,
  // off a reading with nothing to point at.
  bool wanted_a_subject = false;
  // The first character of the subject this walk started on, where there is
  // one to point at. Then a group that closes is handed whole, and the
  // characters are never handed over one at a time. Off a stream this stays
  // nothing, and the fold is told the characters instead.
  const char* text = nullptr;

  constexpr fold_turn() {
    told_at.fill(nowhere());
    ended_at.fill(nowhere());
  }

  // What "nowhere" is, said the same way.
  [[nodiscard]] static constexpr MarkType nowhere() {
    if constexpr (std::is_pointer_v<MarkType>) {
      return nullptr;
    } else {
      return MarkType{-1};
    }
  }
};

// What a group stood on, out of the subject and two positions.
//
// A position is how many characters have been read, or the address one past
// the character that wrote it -- the walk says it one way or the other. Either
// way what the group stood on begins one before where its opening says.
[[nodiscard]] constexpr std::string_view stood_on(const char* text, auto began,
                                                  auto ended) {
  if constexpr (std::is_pointer_v<decltype(began)>) {
    const char* from = began > text ? began - 1 : text;
    return std::string_view(from, static_cast<std::size_t>(ended - began));
  } else {
    const char* from = began > 0 ? text + began - 1 : text;
    return std::string_view(from, static_cast<std::size_t>(ended - began));
  }
}

// Whether a position says the walk never stood there.
//
// Said as a negative count where positions are counted, and as no address at
// all where they are addresses -- the walk says them one way or the other and
// everything that reads them asks here.
[[nodiscard]] constexpr bool stood_nowhere(auto where) {
  if constexpr (std::is_pointer_v<decltype(where)>) {
    return where == nullptr;
  } else {
    return where < 0;
  }
}

// Whether a closing lies at or after an opening -- that is, whether the group
// has closed since it opened.
//
// A position that stood nowhere is a null pointer where positions are
// addresses, and asking whether a null pointer is at or past a real address is
// a question a constant evaluation refuses to answer: the two point into
// unrelated objects. So the order is asked only of two positions that both
// stood somewhere, and a closing that never happened is simply not a closing.
// A flag per register, in as many words as that takes. One word covers the
// machines that have fewer than sixty-five registers, which is most of them;
// a reading that keeps everything at its registers has far more, and a mask of
// one word silently stopped saying anything about those above the sixty-fourth.
template <std::size_t Registers>
struct one_per_register {
  std::array<std::uint64_t, (Registers + 63) / 64 == 0 ? 1
                                                       : (Registers + 63) / 64>
      words{};
  [[nodiscard]] constexpr bool test(std::size_t at) const {
    return (words[at >> 6] & (std::uint64_t{1} << (at & 63))) != 0;
  }
  constexpr void set(std::size_t at) {
    words[at >> 6] |= std::uint64_t{1} << (at & 63);
  }
};

[[nodiscard]] constexpr bool closed_since(auto ended, auto began) {
  if (stood_nowhere(ended)) return false;
  return ended >= began;
}

// The same, for a place that is taken over and over.
//
// A tag is written a step after the character that wrote it, so the end of one
// turn is recorded at the position the next one begins at. Asked plainly,
// every turn after the first looks closed the moment it opens, and nothing is
// handed to it at all. An end standing exactly where the beginning stands
// therefore belongs to the turn before.
[[nodiscard]] constexpr bool closed_since_turn(auto ended, auto began,
                                               bool repeats) {
  if (stood_nowhere(ended)) return false;
  return repeats ? ended > began : ended >= began;
}

// A fold, which is one turn being gathered and at most one turn on its way out.
//
// A place that is taken over and over -- an element of a list -- is a turn at
// a time, and the walk learns that a turn ended a step after it did: a tag is
// written when the step after the character that wrote it is taken. So at the
// moment the next turn begins, the one before it has not been told what closed
// it, and a fold that was simply started afresh there lost the end of every
// turn it gathered.
//
// It is not started afresh. The turn that is ending is moved aside and goes on
// being told what closes it, for exactly as long as that takes -- one step --
// and the element is made from it when it has been. The turn that is beginning
// gathers meanwhile. Which is the same holding back the tags do, done for the
// gatherings.
// The turn on its way out is only kept where a place is taken over and over,
// which is a place standing for an element of a list. Everywhere else a place
// is stood on once, nothing is handed over in the middle of a walk, and room
// for a second turn would double what every walk carries for nothing at all.
struct no_turn {};

template <class Held, class MarkType = std::ptrdiff_t, bool Repeats = true,
          class ToldType = scan::default_context_t>
struct fold_of {
  using held_type = std::remove_cv_t<Held>;
  static constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
  using state_type = typename fold_turn<Held, MarkType>::state_type;

  constexpr fold_of()
    requires std::default_initializable<ToldType>
  = default;
  constexpr explicit fold_of(const ToldType& told)
      : here(told), going(going_of(told)) {}

  [[nodiscard]] static constexpr auto going_of(const ToldType& told) {
    if constexpr (Repeats) {
      return fold_turn<Held, MarkType, ToldType>(told);
    } else {
      static_cast<void>(told);
      return no_turn{};
    }
  }

  fold_turn<Held, MarkType, ToldType> here{};
  [[no_unique_address]]
  std::conditional_t<Repeats, fold_turn<Held, MarkType, ToldType>, no_turn>
      going{};
  [[no_unique_address]] std::conditional_t<Repeats, bool, no_turn> has_going{};
};

// One step of a fold: what happened to the groups inside a place, said to the
// type in the order it can make sense of.
//
// Closed first, innermost outwards, because a group that opens inside another
// closes before it. Then the openings, outermost inwards, for the same reason
// read the other way. Then the character, to every group that is open once
// those two have settled it.
//
// Closings come first because of the delay. A tag is written when the walk
// takes the step after the character that wrote it, so the end of one turn of
// a repeated group and the start of the next arrive together -- and a step
// that opened before it closed would announce a turn that had not ended,
// hand the next turn's characters to nobody, and lose the closing entirely.
// `(X)*` over "XXX" was three openings, two closings and one character.
//
// Nothing here asks what step the walk is on. It asks the positions, which say
// everything: a group whose opening has moved has begun a turn, and one whose
// closing has moved has ended the turn this fold was told about. Both are
// compared against what was announced rather than against each other, because
// a group taken over and over writes into the same two registers every turn:
// what says a turn ended is that the position moved, not where it stands. So
// the same step run twice tells nothing twice, and the same step run at the end
// of the input -- where there is no character to hand over -- finishes what the
// characters left open.
// Which parts of a step a turn is to be told.
//
// A turn on its way out hears only what closes it: what a move opens belongs
// to the turn that has begun, and so does the character.
enum class fold_phase { whole, closings_only };

template <fold_phase Phase = fold_phase::whole, std::size_t Place, class Held,
          class ReadingType, class FoldType, class RegistersType>
SCAN_FORCE_INLINE constexpr void fold_one_step(
    FoldType& fold, const ReadingType& reading,
    const RegistersType& registers, char symbol, bool hands_the_character) {
  using held_type = std::remove_cv_t<Held>;
  constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
  const auto opening_of = [&](std::size_t which) {
    return slot_read(registers, reading[(Place + 1 + which) * 2]);
  };
  const auto closing_of = [&](std::size_t which) {
    return slot_read(registers, reading[(Place + 1 + which) * 2 + 1]);
  };
  [[clang::always_inline]] [&]<std::size_t... step>(std::index_sequence<step...>) {
    ((void)[&] {
      constexpr std::size_t which = inside - 1 - step;
      if ((fold.open & (std::uint64_t{1} << which)) == 0) return;
      const auto began = fold.told_at[which];
      const auto ended = closing_of(which);
      if (!closed_since(ended, began)) return;
      if (ended == fold.ended_at[which]) return;
      if constexpr (takes_the_group_whole<held_type, which,
                                          typename FoldType::state_type>) {
        // The whole of what the group stood on, pointed at rather than copied.
        //
        // A position here is how many characters have been read and not the
        // index of one: the walk writes a tag with the count after the
        // character that wrote it. So what a group stood on begins one before
        // where its opening says.
        if (fold.text != nullptr) {
          close_one_group<held_type, which>(
              fold.state,
              stood_on(fold.text, began, ended));
          fold.open &= ~(std::uint64_t{1} << which);
          fold.ended_at[which] = ended;
          return;
        }
        if constexpr (!takes_group_characters<held_type, which,
                                              typename FoldType::state_type>) {
          // It takes its groups whole and nothing else, and there is nothing
          // here to point at. Holding the characters to hand them over at the
          // end would be a hold with no bound, so this reading cannot be had --
          // said here and handed back where the value would have been, because
          // a walk has nobody to say it to.
          fold.wanted_a_subject = true;
          fold.open &= ~(std::uint64_t{1} << which);
          fold.ended_at[which] = ended;
          return;
        }
      }
      close_one_group<held_type, which>(fold.state);
      fold.open &= ~(std::uint64_t{1} << which);
      fold.ended_at[which] = ended;
    }(), ...);
  }(std::make_index_sequence<inside>{});
  if constexpr (Phase == fold_phase::closings_only) return;
  [[clang::always_inline]] [&]<std::size_t... which>(std::index_sequence<which...>) {
    ((void)[&] {
      const auto began = opening_of(which);
      if (stood_nowhere(began) || fold.told_at[which] == began) return;
      open_one_group<held_type, which>(fold.state);
      fold.told_at[which] = began;
      fold.open |= std::uint64_t{1} << which;
      fold.started = true;
    }(), ...);
  }(std::make_index_sequence<inside>{});
  if (hands_the_character) {
    [[clang::always_inline]] [&]<std::size_t... which>(std::index_sequence<which...>) {
      ((void)[&] {
        if constexpr (takes_group_characters<held_type, which,
                                             typename FoldType::state_type> &&
                      !takes_the_group_whole<
                          held_type, which,
                          typename FoldType::state_type>) {
          if ((fold.open & (std::uint64_t{1} << which)) == 0) return;
          push_one_group<held_type, which>(fold.state, symbol);
        } else if constexpr (takes_group_characters<
                                 held_type, which,
                                 typename FoldType::state_type>) {
          // It would take the group whole, and will where there is something
          // to point at. Where there is not, the characters are all there is.
          if (fold.text != nullptr) return;
          if ((fold.open & (std::uint64_t{1} << which)) == 0) return;
          push_one_group<held_type, which>(fold.state, symbol);
        }
      }(), ...);
    }(std::make_index_sequence<inside>{});
  }
}

// Whether every group of a fold would take its group whole. Then a run of
// characters the walk stepped over costs one step and not one a character:
// nothing is handed over, and what opened and what closed is the same at both
// ends of a run, because a run is where nothing is written.
template <class Held, class StateType>
[[nodiscard]] consteval bool every_group_whole() {
  using held_type = std::remove_cv_t<Held>;
  return []<std::size_t... which>(std::index_sequence<which...>) {
    return (true && ... &&
            takes_the_group_whole<held_type, which, StateType>);
  }(std::make_index_sequence<groups_a_leaf_opens<held_type>()>{});
}

// Whether every move of a machine says what the character it reads lies
// inside.
//
// Then no gathering has to follow a reading. Two readings differ in where the
// tags will land, and a fold is never told where a tag landed -- it is told
// that a group opened, that a character fell in it, that it closed. If every
// move agrees about which groups a character is inside, every reading would be
// told the very same things in the very same order, so one fold answers for
// all of them and lives in the walk itself rather than in a register.
template <auto& Automaton>
[[nodiscard]] consteval bool every_move_says_the_groups() {
  for (std::size_t state = 0; state < Automaton.states.size(); ++state) {
    const auto& here = Automaton.states[state];
    for (std::size_t move = 0; move < here.range_count; ++move) {
      if (!here.ranges[move].groups_known) return false;
    }
  }
  return true;
}

// The move a state takes to stay where it is, where it has one. A run is
// stepped over by taking that move again and again, so what it says about the
// character is what the whole run is inside of.
inline constexpr std::size_t no_move = std::numeric_limits<std::size_t>::max();

template <auto& Automaton, std::size_t State>
[[nodiscard]] consteval std::size_t staying_move() {
  const auto& here = Automaton.states[State];
  for (std::size_t move = 0; move < here.range_count; ++move) {
    if (here.ranges[move].target == State) return move;
  }
  return no_move;
}

// Whether a step can say what happened to a fold's groups without looking at
// anything.
//
// It can when both ends of the step know what they stand inside and each holds
// a single reading. Then the groups the character fell in are the ones the step
// arrived inside; what closed is what the step left and did not arrive in, and
// what opened is the other way round. All of it is a fact about two states,
// which is to say a fact about a place in the written-out code -- so the step
// costs the user's own arithmetic and nothing else.
template <auto& Automaton, std::size_t From, std::size_t Move>
[[nodiscard]] consteval bool step_says_the_groups() {
  // Asked of the whole machine and not of this move alone: a fold that is kept
  // in the walk has to be kept there for the whole of it, and what puts it
  // there is that no move anywhere needs a reading followed.
  return every_move_says_the_groups<Automaton>();
}

// The one register a fold stands at, where the state holds one reading.
template <auto& Automaton, std::size_t State, std::size_t Place>
inline constexpr std::uint32_t only_fold_register =
    Automaton.states[State].readings[0][Place * 2];

// Whether a type wants to hear where a group of its own begins and ends.
//
// A type that only takes the characters does not: it is told which group each
// character fell in and that is the whole of what it asked for. Keeping track
// of what is open for such a group is bookkeeping nobody reads -- and on a
// group that begins again on every character, as `(X|Y)*` does, it is that
// bookkeeping on every character.
template <class Held, std::size_t Which, class StateType>
[[nodiscard]] consteval bool takes_the_group_edges() {
  using scanner_type = scan::scanner<std::remove_cv_t<Held>>;
  return requires(StateType& state) {
    scanner_type{}.opened_group(state, scan::group_at<Which>{});
  } || requires(StateType& state) {
    scanner_type{}.opened_group(state, Which);
  } || requires(StateType& state) {
    scanner_type{}.closed_group(state, scan::group_at<Which>{});
  } || requires(StateType& state) {
    scanner_type{}.closed_group(state, Which);
  } || takes_the_group_whole<std::remove_cv_t<Held>, Which, StateType>;
}

// One step of a fold, told by the shape of the machine rather than by the
// positions it wrote.
// Что открыто на входе в состояние.
//
// Где каждый ход умеет сказать о своих группах, все входящие ходы согласны --
// значит набор открытых групп есть величина времени компиляции, и слово,
// которое несло его между символами, нужно было лишь чтобы прочитать уже
// известное.
template <auto& Automaton, std::size_t State>
[[nodiscard]] consteval std::uint64_t open_on_entry() {
  for (std::size_t f = 0; f < Automaton.states.size(); ++f)
    for (std::size_t m = 0; m < Automaton.states[f].range_count; ++m)
      if (Automaton.states[f].ranges[m].target == State)
        return Automaton.states[f].ranges[m].groups_open;
  return 0;
}

// Ключ -- пара масок, а не ребро.
//
// Внутри от ребра нужны только они: что открыто и что начато заново. Рёбер в
// машине много, а разных пар мало, и пока ключом стояло ребро, компилятор
// порождал тело на каждое ребро там, где по существу их несколько -- а каждое
// порождение он потом отдельно прогоняет через оптимизатор и отдельно решает
// про встраивание, по несвёрнутому размеру.
template <std::size_t Place, class Held, std::uint64_t NowMask,
          std::uint64_t AgainMask, std::uint64_t WasMask,
          bool EdgesCanMove = true, class FoldType>
constexpr void fold_by_the_step(FoldType& fold, char symbol,
                                bool hands_the_character, auto position) {
  using held_type = std::remove_cv_t<Held>;
  // Where this step stands, where that is a place in a subject at all.
  const char* const spot = pointed_at(position);
  constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
  constexpr std::uint64_t now = NowMask;
  // A group this move begins again is one whose turn has ended, however the
  // masks stand: a place taken over and over is open on both sides of it.
  constexpr std::uint64_t again = AgainMask;
  // The groups of this place are the ones just past it: place 0 is the whole
  // and its groups follow it, which is how the readings are laid out too.
  constexpr auto holds = [](std::uint64_t mask, std::size_t which) {
    return (mask & (std::uint64_t{1} << (Place + 1 + which))) != 0;
  };
  // Closed innermost outwards, then opened outermost inwards, then the
  // character to whatever the step arrived inside -- and where a move stays
  // where it is and begins nothing again, none of that can have changed since
  // the move that arrived here, so the character is all there is to do.
  if constexpr (EdgesCanMove) {
  [&]<std::size_t... step>(std::index_sequence<step...>) {
    ((void)[&] {
      constexpr std::size_t which = inside - 1 - step;
      // What this move arrives inside is a constant; what the move before it
      // arrived inside is a word the walk carries. A group closes where the
      // second says yes and the first says no.
      // And only where somebody is listening. A group whose type takes the
      // characters and asks for nothing else has no edges to be told about,
      // so what is open is bookkeeping nobody reads -- and on a group that
      // begins again on every character it is that bookkeeping on every
      // character.
      if constexpr (takes_the_group_edges<
                        held_type, which, typename FoldType::state_type>() &&
                    (!holds(now, which) || holds(again, which))) {
        if constexpr (holds(WasMask, which)) {
          // What the group stood on, where there is a subject to point into
          // and the type asked for it. The step that closes a group is
          // standing on the character just past it, so what it stood on ends
          // where this step begins.
          if constexpr (takes_the_group_whole<
                            held_type, which,
                            typename FoldType::state_type>) {
            const char* const began = fold.here.began_at[which];
            if (began != nullptr && spot >= began) {
              close_one_group<held_type, which>(
                  fold.here.state,
                  std::string_view(began,
                                   static_cast<std::size_t>(spot - began)));
            } else {
              close_one_group<held_type, which>(fold.here.state);
            }
            fold.here.began_at[which] = nullptr;
          } else {
            close_one_group<held_type, which>(fold.here.state);
          }
          // Слово, которое несло это между символами, больше не читается.
        }
      }
    }(), ...);
  }(std::make_index_sequence<inside>{});
  [&]<std::size_t... which>(std::index_sequence<which...>) {
    ((void)[&] {
      if constexpr (takes_the_group_edges<
                        held_type, which, typename FoldType::state_type>() &&
                    holds(now, which)) {
        // Открывается там, где не было открыто -- или было, но этот ход
        // начал группу заново: проход закрытия выше уже погасил её, и слово,
        // которое раньше несло это между двумя проходами, здесь не нужно.
        if constexpr (!holds(WasMask, which) || holds(AgainMask, which)) {
          open_one_group<held_type, which>(fold.here.state);
          if constexpr (takes_the_group_whole<
                            held_type, which,
                            typename FoldType::state_type>) {
            fold.here.began_at[which] = spot;
          }
          fold.here.started = true;
        }
      }
    }(), ...);
  }(std::make_index_sequence<inside>{});
  }
  if (!hands_the_character) return;
  [&]<std::size_t... which>(std::index_sequence<which...>) {
    ((void)[&] {
      if constexpr (holds(now, which)) {
        if constexpr (takes_group_characters<held_type, which,
                                             typename FoldType::state_type>) {
          // A group that goes over whole is not also told its characters --
          // that is the whole point of asking for it whole. Off a stream there
          // is nothing to point at and the characters are all there is.
          if constexpr (takes_the_group_whole<
                            held_type, which,
                            typename FoldType::state_type>) {
            if (spot == nullptr) {
              push_one_group<held_type, which>(fold.here.state, symbol);
            }
          } else {
            push_one_group<held_type, which>(fold.here.state, symbol);
          }
        }
      }
    }(), ...);
  }(std::make_index_sequence<inside>{});
}

// The fold of every reading that stands at this state, told the same step once.
//
// A fold lives at the register holding the place's opening, which is the
// discipline a list is gathered by: readings that share that register share
// what is gathered there, and where two readings would have to disagree the
// machine has already given them registers of their own.
template <std::size_t Place, std::size_t Slot, class Held, auto& Automaton,
          class StatesType, class RegistersType>
constexpr void fold_the_readings(
    std::size_t state, const RegistersType& registers, StatesType& states,
    char symbol, bool hands_the_character, const char* text) {
  const auto& entered = Automaton.states[state];
  std::array<bool, Automaton.register_count> told{};
  for (std::size_t reading = 0; reading < entered.reading_count; ++reading) {
    const std::uint32_t at = entered.readings[reading][Place * 2];
    // Nowhere is said as a negative count or as no address at all, and the
    // walk says it whichever way it says positions.
    if (told[at] || stood_nowhere(slot_read(registers, at))) continue;
    told[at] = true;
    auto& folding = std::get<Slot>(states[at]);
    // Said every step rather than once, because a fold is made where its place
    // opens and carried where a reading divides, and neither of those knows
    // what the walk is reading.
    folding.here.text = text;
    fold_one_step<fold_phase::whole, Place, Held>(
        folding.here, entered.readings[reading], registers, symbol,
        hands_the_character);
    if constexpr (requires { folding.has_going = true; }) {
      if (folding.has_going) {
        folding.going.text = text;
        fold_one_step<fold_phase::closings_only, Place, Held>(
            folding.going, entered.readings[reading], registers, symbol, false);
      }
    }
  }
}

// Whether the place a group stands for is taken over and over, which is what
// an element of a list is and what nothing else is.
template <class Type, std::size_t Group>
[[nodiscard]] consteval bool a_place_that_repeats() {
  if constexpr (Group == 0) {
    return false;
  } else {
    return scanned_as_range<leaf_kind_of_output<Type, Group - 1>>;
  }
}

// How one group is gathered, made once and asked at every place that gathers.
//
// A leaf that is built from the groups its own pattern opens is not handed the
// text it stands on, so its place gathers nothing and each of its groups
// gathers characters. Every other group is gathered by the reader of the type
// it holds, which is what it was before any of this.
template <class Type, fixed_string Format, std::size_t Group,
          class MarkType = std::ptrdiff_t>
struct gathering_of {
  using held_type = leaf_kind_of_output<Type, Group>;
  static constexpr bool by_groups = gathers_by_its_groups<held_type>;
  // Whether this place is stood on over and over, which an element of a list
  // is and nothing else is. Asked through a function rather than written as an
  // expression: `group > 0 && …<group - 1>` still names the type at group - 1,
  // and at group zero that is an index of every bit set.
  static constexpr bool place_repeats = a_place_that_repeats<Type, Group>();
  static constexpr bool folds = folds_by_turns<std::remove_cv_t<held_type>>;
  static constexpr bool the_place = by_groups && leaf_offset_of_output<Type, Group> == 0;
  static constexpr bool inside = by_groups && leaf_offset_of_output<Type, Group> != 0;
  // Whether this leaf is only a piece of the subject: its scanner says how to
  // read a piece handed to it whole, and says nothing about being told one
  // character at a time. The marks say where the piece stood and the subject
  // is still there to be pointed at, so there is nothing to gather as the walk
  // goes -- it is read where the value is put together. Such a leaf is refused
  // where the subject is not kept, the same as one read from its groups.
  static constexpr bool only_a_piece =
      reads_a_whole_piece_only<std::remove_cv_t<held_type>>;

  template <class ToldType>
  [[nodiscard]] static constexpr auto begin(std::string_view parameters,
                                            const ToldType& told) {
    if constexpr (the_place && folds) {
      static_cast<void>(parameters);
      return fold_of<std::remove_cv_t<held_type>, MarkType, place_repeats,
                     ToldType>(told);
    } else if constexpr (inside && folds) {
      static_cast<void>(parameters);
      static_cast<void>(told);
      return no_gathering{};
    } else if constexpr (the_place || inside || only_a_piece) {
      static_cast<void>(parameters);
      static_cast<void>(told);
      return no_gathering{};
    } else {
      return scanner_begin_given<held_type>(parameters, told);
    }
  }

  [[nodiscard]] static constexpr auto begin(std::string_view parameters) {
    if constexpr (the_place && folds) {
      // The type's own state, and the walk's note of what it has been told.
      static_cast<void>(parameters);
      return fold_of<std::remove_cv_t<held_type>, MarkType,
                     place_repeats>{};
    } else if constexpr (inside && folds) {
      // Nothing: the characters and the edges of this group go to the fold,
      // which is kept at the place the group is inside of.
      static_cast<void>(parameters);
      return no_gathering{};
    } else if constexpr (the_place || inside || only_a_piece) {
      // A leaf read from its groups after the match gathers nothing at all:
      // the positions say where each of its groups stood, and the subject is
      // still there to be pointed at -- which is why such a leaf is refused
      // where the subject is not.
      static_cast<void>(parameters);
      return no_gathering{};
    } else {
      static_assert(requires { scanner_begin<held_type>(parameters); },
                    "single-pass input requires incremental scan::scanner<T>");
      return scanner_begin<held_type>(parameters);
    }
  }

  template <class StateType>
  static constexpr void push(StateType& state, char letter) {
    if constexpr (the_place || inside || only_a_piece) {
      // A fold is handed its characters by the group they fell in, which the
      // place does for all of its groups at once and in order; a leaf read
      // after the match is handed nothing at all.
      static_cast<void>(state);
      static_cast<void>(letter);
    } else {
      scanner_push<held_type>(state, letter);
    }
  }

  // The characters of a run the walk stepped over, together. What the step in
  // vectors saved was being given back a call at a time here.
  template <class StateType>
  static constexpr void push_run(StateType& state, const char* from,
                                 const char* to) {
    if constexpr (the_place || inside || only_a_piece) {
      static_cast<void>(state);
      static_cast<void>(from);
      static_cast<void>(to);
    } else {
      scanner_push_run<held_type>(state, from, to);
    }
  }
};

// Whether any group of a type is handed over whole -- cut out of the subject
// rather than told character by character. Such a group is read from the
// positions however its edges were announced.
template <class Held, class StateType>
[[nodiscard]] consteval bool any_group_taken_whole() {
  return []<std::size_t... which>(std::index_sequence<which...>) {
    return (false || ... || takes_the_group_whole<Held, which, StateType>);
  }(std::make_index_sequence<groups_a_leaf_opens<std::remove_cv_t<Held>>()>{});
}

// Which groups' marks anybody will read.
//
// The same question as below, asked of the marks rather than of the places. A
// group inside a fold that hears what happened from the moves has no place
// anybody asks about -- but it is still a group, and below it says so, because
// what says "this group took no part" is that it stood nowhere.
//
// Nothing says that of a group whose every edge is told. That is the whole
// difference between the two, and it is worth the two names: what a walk
// writes on every character is decided here, and what it may step over in one
// go is decided below.
template <class Type, fixed_string Format, auto& Automaton>
[[nodiscard]] consteval std::uint64_t groups_whose_mark_is_read() {
  std::uint64_t made = 0;
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    ([&] {
      using how = gathering_of<Type, Format, group>;
      using held = std::remove_cv_t<leaf_kind_of_output<Type, group>>;
      constexpr std::size_t inside = groups_a_leaf_opens<held>();
      constexpr bool a_fold_of_its_own =
          how::folds && how::the_place && !how::place_repeats &&
          every_move_says_the_groups<Automaton>();
      constexpr bool told_by_the_moves = [] {
        if constexpr (a_fold_of_its_own) {
          using state_type = decltype(scan::scanner<held>{}.begin_groups());
          return !any_group_taken_whole<held, state_type>();
        } else {
          return false;
        }
      }();
      // A fold that hears everything from the moves does not need its own
      // pair either. What its place stands for is never cut out of the
      // subject: the value is what finish_groups makes of the state it was
      // handed, and where the place began and ended is read by nobody. A place
      // taken over and over is the exception, and says so by itself --
      // told_by_the_moves is false for it, because its turns are told by its
      // marks, the same as any list's.
      if constexpr (!(how::folds && (how::inside || told_by_the_moves))) {
        made |= std::uint64_t{1} << group;
      }
      // Asked of the place, not of what stands inside it. `inside` counts the
      // groups of the whole leaf, which is an answer about a place -- and a
      // group standing inside a fold is not a place, so what it counted was
      // the groups that follow it. Every group inside a fold claimed the
      // marks of the groups after it, the claims overlapped, and a fold
      // whose groups nobody asks about kept every mark it has -- a machine
      // writing tags on every character for nobody to read.
      if constexpr (how::the_place && !told_by_the_moves) {
        for (std::size_t which = 0; which < inside; ++which) {
          made |= std::uint64_t{1} << (group + 1 + which);
        }
      }
    }(), ...);
  }(std::make_index_sequence<groups_of_output<Type>()>{});
  return made;
}

// The machine everything walks: the one above, with the marks nobody will
// read left out.
//
// Which those are is a question about the type and not about the expression,
// so it cannot be asked until both are known -- and asking it wants a machine
// to ask of, which is why the whole one is built first. What it buys is not
// only the stores: two readings of the input that differ in nothing but a mark
// nobody reads are one reading here, so the machine carries fewer of them,
// hands out fewer registers, and a run that wrote such a mark on every
// character becomes a run that writes nothing -- which is a run the walk can
// step over whole.
// Which groups' tags the machine is worth writing at all.
//
// Wider than the marks anybody reads, and for one reason: a tag is also how
// the machine knows a group began again. A place taken over and over is told
// its turns by the moves, and the moves can only say so because the tags are
// there to be written -- so a group whose edges somebody listens for keeps
// them even where nothing will ever ask where it stood.
//
// Which groups a character lies inside is not a reason: that is read off the
// whole machine before anything is taken out of it.
// The groups inside one place whose edges its type listens for.
//
// Kept apart from the walk over places below so that the two lists of groups
// -- the places and what is inside each of them -- are never expanded
// together.
template <class Type, fixed_string Format, std::size_t Group>
[[nodiscard]] consteval std::uint64_t edges_listened_for() {
  using how = gathering_of<Type, Format, Group>;
  using held = std::remove_cv_t<leaf_kind_of_output<Type, Group>>;
  std::uint64_t made = 0;
  if constexpr (how::folds && how::the_place) {
    using state_type = decltype(scan::scanner<held>{}.begin_groups());
    [&]<std::size_t... which>(std::index_sequence<which...>) {
      ([&] {
        if constexpr (takes_the_group_edges<held, which, state_type>()) {
          made |= std::uint64_t{1} << (Group + 1 + which);
        }
      }(), ...);
    }(std::make_index_sequence<groups_a_leaf_opens<held>()>{});
  }
  return made;
}

// Which tags the machine is worth writing at all.
//
// Said by the tag and not by the group, because a group can be worth half of
// itself. A mark somebody reads is a pair -- where it began and where it ended
// -- and both halves have to be written for the answer to be cut out of the
// subject. A group whose edges are only listened for is not read anywhere: all
// it owes is the opening, which is what tells a turn from the turn before it,
// and its closing is told by the moves, which say which groups a character
// lies inside. Kept by the group, such a group carried a closing mark written
// on the last character of every turn for nobody to read.
// The groups of a fold whose characters it is told about.
//
// Their marks are read by nobody, and for a while that was taken to mean the
// machine need not write them. It does: a tag is what holds two readings
// apart, and two readings that differ only in which group a character fell in
// are exactly the two a fold is told apart by. Merge them and the move still
// says which groups the character lies inside -- it says the wrong ones, the
// ones of whichever reading survived. `\(((_+)((A)|(B)))*\))` then answers 11
// where it means 21, because the B is announced as an A.
//
// So a group whose characters go to a fold keeps its marks, unread as they
// are. What is left to save is the groups a fold hears nothing about.
template <class Type, fixed_string Format, std::size_t Group>
[[nodiscard]] consteval std::uint64_t characters_told_of() {
  using how = gathering_of<Type, Format, Group>;
  using held = std::remove_cv_t<leaf_kind_of_output<Type, Group>>;
  std::uint64_t made = 0;
  if constexpr (how::folds && how::the_place) {
    using state_type = decltype(scan::scanner<held>{}.begin_groups());
    [&]<std::size_t... which>(std::index_sequence<which...>) {
      ([&] {
        if constexpr (takes_group_characters<held, which, state_type>) {
          made |= std::uint64_t{1} << (Group + 1 + which);
        }
      }(), ...);
    }(std::make_index_sequence<groups_a_leaf_opens<held>()>{});
  }
  return made;
}

// Every group a fold is told the characters of, in one mask.
template <class Type, fixed_string Format>
[[nodiscard]] consteval std::uint64_t groups_told_of() {
  std::uint64_t made = 0;
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    ((made |= characters_told_of<Type, Format, group>()), ...);
  }(std::make_index_sequence<groups_of_output<Type>()>{});
  return made;
}

// Whether a machine still shows every group a fold is told about.
//
// This is what taking a tag away can cost, and the cost does not look like an
// error. Two groups that are alternatives of each other -- `(A)|(B)` -- are
// told apart by nothing but their tags, so with the tags gone their positions
// fold together and one of them wins: the machine still says which groups a
// character lies inside, and for a B it says the A. A group that has lost that
// argument does not appear in any move at all, which is a thing that can be
// looked for.
//
// A fold that tells its groups apart by the character rather than by the group
// -- `(X|Y)` read as one group -- loses nothing, and that is the common shape.
// It keeps the trimming; the other one does not.
template <class Type, fixed_string Format, auto& Automaton>
[[nodiscard]] consteval bool every_told_group_is_seen() {
  const std::uint64_t told = groups_told_of<Type, Format>();
  for (std::size_t group = 0; group < 64; ++group) {
    if (((told >> group) & 1) == 0) continue;
    bool seen = false;
    for (std::size_t state = 0; state < Automaton.states.size(); ++state) {
      const auto& here = Automaton.states[state];
      for (std::size_t move = 0; move < here.range_count; ++move) {
        if ((here.ranges[move].groups_open & (std::uint64_t{1} << group)) != 0) {
          seen = true;
        }
      }
    }
    if (!seen) return false;
  }
  return true;
}

template <class Type, fixed_string Format, auto& Automaton>
[[nodiscard]] consteval std::uint64_t tags_that_matter() {
  const std::uint64_t read = groups_whose_mark_is_read<Type, Format, Automaton>();
  std::uint64_t edges = 0;
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    ((edges |= edges_listened_for<Type, Format, group>()), ...);
  }(std::make_index_sequence<groups_of_output<Type>()>{});
  std::uint64_t made = 0;
  for (std::size_t group = 0; group < 32; ++group) {
    const std::uint64_t opening = std::uint64_t{1} << (2 * group);
    const std::uint64_t closing = std::uint64_t{1} << (2 * group + 1);
    if (((read >> group) & 1) != 0) {
      made |= opening | closing;
    } else if (((edges >> group) & 1) != 0) {
      made |= opening;
    }
  }
  return made;
}

// The tags the machine is built with, and a check that keeping so few has not
// cost it something else.
//
// A tag is not only a mark somebody reads: it is what holds two readings
// apart. Take away the tags of a group nobody asks the position of, and two
// readings that differed in nothing else become one -- and then a move can no
// longer say which groups the character it reads lies inside. That is exactly
// what a fold is told, so a fold beside such a reading stops being told which
// of its groups a character fell in, and answers with the wrong number rather
// than with an error.
//
// The question was asked of the whole machine and the answer used for the
// trimmed one, which is the mistake. It is asked of the machine that will be
// walked now, and where trimming would cost it that, nothing is trimmed.
template <class Type, fixed_string Format, bool Cut>
inline constexpr std::uint64_t tags_worth_keeping = [] {
  constexpr std::uint64_t slim =
      tags_that_matter<Type, Format,
                       streaming_automaton_whole<Type, Format, Cut>>();
  // The groups a fold is told the characters of, both halves of each.
  constexpr std::uint64_t told = [] {
    const std::uint64_t groups = groups_told_of<Type, Format>();
    std::uint64_t made = 0;
    for (std::size_t group = 0; group < 32; ++group) {
      if (((groups >> group) & 1) != 0) {
        made |= (std::uint64_t{1} << (2 * group)) |
                (std::uint64_t{1} << (2 * group + 1));
      }
    }
    return made;
  }();
  constexpr std::uint64_t wanted =
      every_told_group_is_seen<Type, Format,
                               packed_text_automaton<spread_text<Type, Format>,
                                                     false, Cut, slim>>()
          ? slim
          : (slim | told);
  if constexpr (every_move_says_the_groups<
                    packed_text_automaton<spread_text<Type, Format>, false, Cut,
                                          wanted>>()) {
    return wanted;
  } else {
    return ~std::uint64_t{0};
  }
}();

template <class Type, fixed_string Format, bool Cut = true>
inline constexpr auto& streaming_automaton =
    packed_text_automaton<spread_text<Type, Format>, false, Cut,
                          tags_worth_keeping<Type, Format, Cut>>;

// Which groups' positions anybody will read.
//
// A place is always one: where it stood is how a field is cut out of the
// subject, and whether it stood anywhere at all is how a group that took no
// part is told from one that did. The groups inside a place are another
// matter. Where the machine says what each character lies inside, a fold hears
// what opened and what closed from the moves themselves and never asks where.
// Nothing reads those positions then -- and writing them is not merely a store
// on nearly every character: it makes every run a run that writes, which is a
// run the vectors cannot step over.
template <class Type, fixed_string Format, auto& Automaton>
[[nodiscard]] consteval std::uint64_t groups_whose_place_is_read() {
  std::uint64_t made = 0;
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    ([&] {
      made |= std::uint64_t{1} << group;
      using how = gathering_of<Type, Format, group>;
      using held = std::remove_cv_t<leaf_kind_of_output<Type, group>>;
      constexpr std::size_t inside = groups_a_leaf_opens<held>();
      // Asked in steps, because only a fold has groups to be told about and
      // only a fold has a state to be told into.
      constexpr bool a_fold_of_its_own =
          how::folds && how::the_place && !how::place_repeats &&
          every_move_says_the_groups<Automaton>();
      constexpr bool told_by_the_moves = [] {
        if constexpr (a_fold_of_its_own) {
          using state_type = decltype(scan::scanner<held>{}.begin_groups());
          return !any_group_taken_whole<held, state_type>();
        } else {
          return false;
        }
      }();
      // Asked of the place, not of what stands inside it. `inside` counts the
      // groups of the whole leaf, which is an answer about a place -- and a
      // group standing inside a fold is not a place, so what it counted was
      // the groups that follow it. Every group inside a fold claimed the
      // marks of the groups after it, the claims overlapped, and a fold
      // whose groups nobody asks about kept every mark it has -- a machine
      // writing tags on every character for nobody to read.
      if constexpr (how::the_place && !told_by_the_moves) {
        for (std::size_t which = 0; which < inside; ++which) {
          made |= std::uint64_t{1} << (group + 1 + which);
        }
      }
    }(), ...);
  }(std::make_index_sequence<groups_of_output<Type>()>{});
  return made;
}

// Whether a fold in this shape can be gathered by the walk over characters in
// a row.
//
// It can where every move of the machine says what its character lies inside:
// then the fold does not follow a reading, does not live at a register, and
// nothing about it needs the positions -- so the walk that steps over runs in
// vectors can gather it, which is the walk everything else is read by.
//
// A list still cannot. Its elements are handed over turn by turn, and which
// turn a gathering belongs to is exactly what the registers are keeping
// straight.
template <class Type, fixed_string Format>
[[nodiscard]] consteval bool a_fold_the_walk_can_keep() {
  if constexpr (holds_a_range<Type>()) {
    return false;
  } else if constexpr (!holds_a_fold<Type>()) {
    return false;
  } else {
    return every_move_says_the_groups<packed_automaton<Type, Format>>();
  }
}

// Whether the gathering for a group can live in the walk rather than at a
// register.
//
// The same question as for a fold, asked of a place that gathers characters:
// where every move says what its character lies inside, whether this place is
// open is a fact about the move, so nothing has to be read to find out and
// nothing has to follow a reading. A place taken over and over is left out --
// there the gathering is handed away turn by turn, and which turn it belongs
// to is what the registers are keeping straight.
template <class Type, fixed_string Format, std::size_t Group,
          class MarkType = std::ptrdiff_t>
[[nodiscard]] consteval bool alone_in_its_slot();

template <class Type, fixed_string Format, auto& Automaton, std::size_t Group>
[[nodiscard]] consteval bool gathers_in_the_walk() {
  using how = gathering_of<Type, Format, Group>;
  // A list is left out twice over: it grows turn by turn, and which turn a
  // gathering belongs to is what the registers keep straight -- so it stays
  // where they are, and so does anything standing at a place that repeats.
  return every_move_says_the_groups<Automaton>() && !how::place_repeats &&
         !scanned_as_range<typename how::held_type> &&
         alone_in_its_slot<Type, Format, Group>();
}


// Whether a list can be gathered by the walk rather than at the registers.
//
// A list is kept at a register because its turns are told apart by the
// registers: the place an element starts at is written afresh every turn, and
// which gathering that is, is what the register says. Where every move can say
// what its character lies inside, none of that is needed -- the move says when
// a turn begins, and the walk can keep the list and the turn being gathered
// itself, the way it keeps a fold.
//
// Asked of the list and of its element together, because they are kept
// together: the list is the slot the elements are appended to and the element
// is the slot being gathered, and one of them living at a register would put
// the other back there too.
template <class Type, fixed_string Format, auto& Automaton, std::size_t Group>
[[nodiscard]] consteval bool list_gathers_in_the_walk() {
  if constexpr (!scanned_as_range<leaf_kind_of_output<Type, Group>>) {
    return false;
  } else if constexpr (Group + 1 >= groups_of_output<Type>()) {
    return false;
  } else {
    constexpr std::size_t element = Group + 1;
    using inside = leaf_kind_of_output<Type, element>;
    if constexpr (scanned_as_range<inside>) {
      return false;
    } else if constexpr (gathering_of<Type, Format, element>::folds) {
      return false;
    } else {
      return every_move_says_the_groups<Automaton>() &&
             alone_in_its_slot<Type, Format, Group>() &&
             alone_in_its_slot<Type, Format, element>();
    }
  }
}

// One gathering per value the pattern reads, not one per field of the output.
//
// They are the same thing only where every field is one place. A field that is
// itself a shape is several places, and its own reader would then have to be
// handed the text and made to find the same boundaries a second time -- with a
// pattern that is a copy of the one already running. The boundaries are known
// here: the machine has a group for each of them. So each value is gathered by
// the reader of the type that value is, and the output is put together from
// those afterwards, the same way it is put together from pieces of a subject
// that can be pointed at.
template <class Type, fixed_string Format, std::size_t... Group>
[[nodiscard]] constexpr auto make_scanner_state(
    std::index_sequence<Group...>) {
  static constexpr auto spread = spread_of<Type, Format>();
  // A group that stands for a list gathers the list itself, which needs no
  // reader: what goes into it are whole elements, put there as each one ends.
  const auto one = []<std::size_t which>() {
    using held_type = leaf_kind_of_output<Type, which>;
    if constexpr (scanned_as_range<held_type>) {
      return made_range<std::remove_cv_t<held_type>, spread.turns_most[which]>(
          scan::nothing_given{});
    } else {
      return gathering_of<Type, Format, which>::begin(
          spread.parameters[which].view());
    }
  };
  return std::tuple{one.template operator()<Group>()...};
}

template <class Type, fixed_string Format>
[[nodiscard]] constexpr auto make_scanner_state() {
  return make_scanner_state<Type, Format>(
      std::make_index_sequence<groups_of_output<Type>()>{});
}

// The same, told what the place this shape stands at was told. Every place
// inside begins with what it was told, which is what a place told a context
// means one level down as much as it does at the call.
template <class Type, fixed_string Format, class ToldType, std::size_t... Group>
[[nodiscard]] constexpr auto make_scanner_state_told(
    const ToldType& told, std::index_sequence<Group...>) {
  static constexpr auto spread = spread_of<Type, Format>();
  const auto one = [&]<std::size_t which>() {
    using held_type = leaf_kind_of_output<Type, which>;
    if constexpr (scanned_as_range<held_type>) {
      return made_range<std::remove_cv_t<held_type>, spread.turns_most[which]>(
          context_at_group<Type, which>(told));
    } else {
      return gathering_of<Type, Format, which>::begin(
          spread.parameters[which].view(), context_at_group<Type, which>(told));
    }
  };
  return std::tuple{one.template operator()<Group>()...};
}

template <class Type, fixed_string Format, class ToldType>
[[nodiscard]] constexpr auto make_scanner_state_told(const ToldType& told) {
  return make_scanner_state_told<Type, Format>(
      told, std::make_index_sequence<groups_of_output<Type>()>{});
}

// One slot per kind of gathering, not one per group.
//
// The machine keeps a gathering for every register, and a register is made for
// one tag, which belongs to one group -- so all but one of the gatherings kept
// in a register are for groups that register can never hold. A set of every
// group in every register is what a row of five fields was paying a
// microsecond and a half a scan for: five strings a register, made at the head
// of the scan, copied wherever a reading divided, and thrown away at the end.
//
// So the set is by kind and not by group. Groups gathered the same way share a
// slot, because no two of them are ever gathered in one register at once, and
// each register is begun with the parameters of the group whose tag it holds.
template <class... Kinds>
struct gathering_kinds {
  using as_a_tuple = std::tuple<Kinds...>;
};

template <class List, class Kind>
struct with_kind;

template <class... Kinds, class Kind>
struct with_kind<gathering_kinds<Kinds...>, Kind> {
  using result = std::conditional_t<(std::is_same_v<Kind, Kinds> || ...),
                                    gathering_kinds<Kinds...>,
                                    gathering_kinds<Kinds..., Kind>>;
};

template <class List, class Kind>
struct where_kind;

template <class Kind>
struct where_kind<gathering_kinds<>, Kind> {
  static constexpr std::size_t at = 0;
};

template <class First, class... Rest, class Kind>
struct where_kind<gathering_kinds<First, Rest...>, Kind> {
  static constexpr std::size_t at =
      std::is_same_v<First, Kind>
          ? 0
          : 1 + where_kind<gathering_kinds<Rest...>, Kind>::at;
};

// What one group is gathered in, asked without asking for the others.
template <class Type, fixed_string Format, std::size_t Group,
          class MarkType = std::ptrdiff_t,
          class ToldType = scan::default_context_t,
          bool AList = scanned_as_range<leaf_kind_of_output<Type, Group>>>
struct gathering_state {
  using result = decltype(gathering_of<Type, Format, Group, MarkType>::begin(
      std::string_view{},
      context_at_group<Type, Group>(std::declval<const ToldType&>())));
};

template <class Type, fixed_string Format, std::size_t Group, class MarkType,
          class ToldType>
struct gathering_state<Type, Format, Group, MarkType, ToldType, true> {
  using result = std::remove_cv_t<leaf_kind_of_output<Type, Group>>;
};

template <class Type, fixed_string Format, class MarkType, class List,
          std::size_t Group, std::size_t Count,
          class ToldType = scan::default_context_t>
struct kinds_from {
  using result = typename kinds_from<
      Type, Format, MarkType,
      typename with_kind<
          List, typename gathering_state<Type, Format, Group, MarkType,
                                         ToldType>::result>::result,
      Group + 1, Count, ToldType>::result;
};

template <class Type, fixed_string Format, class MarkType, class List,
          std::size_t Count, class ToldType>
struct kinds_from<Type, Format, MarkType, List, Count, Count, ToldType> {
  using result = List;
};

template <class Type, fixed_string Format, class MarkType = std::ptrdiff_t,
          class ToldType = scan::default_context_t>
using gathering_kinds_of =
    typename kinds_from<Type, Format, MarkType, gathering_kinds<>, 0,
                        groups_of_output<Type>(), ToldType>::result;

// What one register holds.
template <class Type, fixed_string Format, class MarkType = std::ptrdiff_t,
          class ToldType = scan::default_context_t>
using register_state =
    typename gathering_kinds_of<Type, Format, MarkType, ToldType>::as_a_tuple;

// Which slot of it a group is gathered in.
template <class Type, fixed_string Format, std::size_t Group,
          class MarkType = std::ptrdiff_t,
          class ToldType = scan::default_context_t>
inline constexpr std::size_t gathering_slot =
    where_kind<gathering_kinds_of<Type, Format, MarkType, ToldType>,
               typename gathering_state<Type, Format, Group, MarkType,
                                        ToldType>::result>::at;

// The default is said once, where the name is first declared above; saying it
// again here is ill-formed and only a module unit lets it pass.
template <class Type, fixed_string Format, std::size_t Group, class MarkType>
[[nodiscard]] consteval bool alone_in_its_slot() {
  // Slots are handed out by kind, not by place: two places gathered the same
  // way share one, because at a register they are still two -- the register is
  // what tells them apart. A gathering the walk keeps has no register to be
  // told apart by, so where two places share a slot the walk can keep neither:
  // what one pushed the other would read.
  constexpr std::size_t mine = gathering_slot<Type, Format, Group, MarkType>;
  return [&]<std::size_t... other>(std::index_sequence<other...>) {
    return (true && ... &&
            (other == Group ||
             gathering_slot<Type, Format, other, MarkType> != mine));
  }(std::make_index_sequence<groups_of_output<Type>()>{});
}

// Слот, который принимает символы, держит внутри буфер с индексом времени
// выполнения -- и такой буфер не даёт поднять в регистры ничего, что лежит с
// ним в одном объекте. Поэтому слоты делятся надвое: те, что меняются на
// каждом символе, остаются значением сборщика, а собирающие обход держит
// своей переменной и даёт по ссылке.
template <class Kind>
concept keeps_characters = requires(Kind& one, char letter) {
  one.push_back(letter);
};

template <class TupleType, std::size_t... Which>
[[nodiscard]] constexpr auto pick_warm(std::index_sequence<Which...>) {
  return std::tuple_cat(
      std::conditional_t<
          keeps_characters<std::tuple_element_t<Which, TupleType>>,
          std::tuple<>,
          std::tuple<std::tuple_element_t<Which, TupleType>>>{}...);
}

template <class TupleType, std::size_t... Which>
[[nodiscard]] constexpr auto pick_cold(std::index_sequence<Which...>) {
  return std::tuple_cat(
      std::conditional_t<
          keeps_characters<std::tuple_element_t<Which, TupleType>>,
          std::tuple<std::tuple_element_t<Which, TupleType>>,
          std::tuple<>>{}...);
}

template <class TupleType>
using warm_slots_of = decltype(pick_warm<TupleType>(
    std::make_index_sequence<std::tuple_size_v<TupleType>>{}));
template <class TupleType>
using cold_slots_of = decltype(pick_cold<TupleType>(
    std::make_index_sequence<std::tuple_size_v<TupleType>>{}));

// A gathering begun again, keeping the resource it was begun with.
//
// Beginning one and assigning it into a slot loses what it was begun with: a
// container that keeps a resource keeps its own when it is assigned to, and the
// slot was made on the default resource before anybody said otherwise. So the
// slot is ended and begun where it stands, which is what "made with" means.
template <class Slot, class Made>
constexpr void begin_gathering_at(Slot& into, Made&& made) {
  if constexpr (requires { typename Slot::allocator_type; }) {
    std::destroy_at(&into);
    std::construct_at(&into, std::forward<Made>(made));
  } else {
    into = std::forward<Made>(made);
  }
}

// One gathering of every kind, each begun as the first group of that kind
// would begin it. Where two groups of a kind ask for different parameters, the
// one that is not first is begun again when its group opens, which is where
// every group but one begins in any case.
template <class Type, fixed_string Format, class MarkType = std::ptrdiff_t,
          class ToldType = scan::default_context_t>
[[nodiscard]] constexpr auto make_slots(const ToldType& told = ToldType{}) {
  register_state<Type, Format, MarkType, ToldType> made{};
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    // Backwards, so that the first group of a kind is the one that is left.
    const auto one = [&]<std::size_t which>() {
      using held_type = leaf_kind_of_output<Type, which>;
      if constexpr (scanned_as_range<held_type>) {
        static constexpr auto spread = spread_of<Type, Format>();
        std::get<gathering_slot<Type, Format, which, MarkType>>(made) =
            made_range<std::remove_cv_t<held_type>, spread.turns_most[which]>(
                context_at_group<Type, which>(told));
      } else {
        static constexpr auto spread = spread_of<Type, Format>();
        begin_gathering_at(
            std::get<gathering_slot<Type, Format, which, MarkType, ToldType>>(
                made),
            gathering_of<Type, Format, which, MarkType>::begin(
                spread.parameters[which].view(),
                context_at_group<Type, which>(told)));
      }
    };
    (one.template operator()<groups_of_output<Type>() - 1 - group>(), ...);
  }(std::make_index_sequence<groups_of_output<Type>()>{});
  return made;
}

// Every register, begun.
//
// One gathering of each kind everywhere, and then the groups that are open
// from the very first character begun where their reading says they are kept:
// those never meet the command that begins a group, because they were opened
// before there was a character to move on.
template <class Type, fixed_string Format, auto& Automaton,
          class MarkType = std::ptrdiff_t,
          class ToldType = scan::default_context_t>
[[nodiscard]] constexpr auto make_register_states(
    const ToldType& told = ToldType{}) {
  // Made one by one rather than made once and filled in. A container that
  // keeps a resource takes it when it is constructed and keeps its own when it
  // is assigned or copied, so filling an array of default-made states with a
  // well-made one leaves every register on the default resource.
  auto states = [&]<std::size_t... at>(std::index_sequence<at...>) {
    return std::array<register_state<Type, Format, MarkType, ToldType>,
                      Automaton.register_count>{
        ((void)at, make_slots<Type, Format, MarkType>(told))...};
  }(std::make_index_sequence<Automaton.register_count>{});
  const auto& initial = Automaton.states[Automaton.initial];
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    ([&] {
      using held_type = leaf_kind_of_output<Type, group>;
      for (std::size_t reading = 0; reading < initial.reading_count;
           ++reading) {
        const std::uint32_t at = initial.readings[reading][group * 2];
        if (at >= Automaton.register_count) continue;
        if constexpr (scanned_as_range<held_type>) {
          static constexpr auto spread = spread_of<Type, Format>();
          std::get<gathering_slot<Type, Format, group, MarkType>>(states[at]) =
              made_range<std::remove_cv_t<held_type>, spread.turns_most[group]>(
                  context_at_group<Type, group>(told));
        } else {
          static constexpr auto spread = spread_of<Type, Format>();
          begin_gathering_at(
              std::get<gathering_slot<Type, Format, group, MarkType, ToldType>>(
                  states[at]),
              gathering_of<Type, Format, group, MarkType>::begin(
                  spread.parameters[group].view(),
                  context_at_group<Type, group>(told)));
        }
      }
    }(), ...);
  }(std::make_index_sequence<groups_of_output<Type>()>{});
  return states;
}

// Following a reading instead of counting on the numbers.
//
// The machine stands in one state and in several readings of the input at once,
// and the registers are how the readings are kept apart. A field's text cannot
// be a piece of the subject here -- the subject is gone as it is read -- so it
// is gathered as it arrives, one gathering per register that holds the opening
// tag of that field. The gathering goes where the register goes: a command that
// copies a register copies it, a command that writes a fresh position starts it
// again.
//
// The order is the whole of the correctness. The positions are written first,
// then the gatherings are moved the way the positions moved, and only then does
// the character go in -- so the character that opens a field is inside it and
// the character that closes one is not, which is what the positions say once
// they have been written and cannot be asked before.
//
// Which register holds which tag, and which registers make up one reading, are
// both said by the automaton. They used to be worked out by dividing a register
// number by the number of tags, which was true of one way of handing registers
// out and of nothing else.
// The gatherings a transition's copies will read, and only those.
//
// A transition that copies a register needs that register's gathering as it
// was before the transition, so what was here copied the whole set on every
// character -- every register's gathering of every field, to be ready for a
// copy of one or two of them. On a subject read a character at a time that was
// most of what reading it cost.
template <class StatesType, std::size_t CommandCapacity>
struct kept_gatherings {
  using held_type = typename StatesType::value_type;
  std::array<std::size_t, CommandCapacity> which{};
  // Room for as many as a transition could ask for, and a gathering made in
  // none of them until one is asked for. A transition that copies two of them
  // used to make one for every command it could have had, and throw the rest
  // away unread -- a dozen strings a character where a field ends.
  std::array<std::optional<held_type>, CommandCapacity> held{};
  std::size_t count = 0;

  [[nodiscard]] constexpr const held_type& operator[](
      std::size_t source) const {
    for (std::size_t at = 0; at < count; ++at) {
      if (which[at] == source) return *held[at];
    }
    return *held[0];
  }
};

// A copy of one gathering that keeps the resource it was made with.
//
// A container that keeps its own resource does not hand it on when it is
// copied -- select_on_container_copy_construction says so -- so a walk that
// keeps a gathering for later would keep it on the default resource and hand
// that back at the end. Said once here, because every other copy of a
// gathering goes through an assignment, and an assignment keeps the resource
// the thing being assigned to was made with.
template <class Kind>
[[nodiscard]] constexpr Kind copied_gathering(const Kind& one) {
  if constexpr (requires {
                  typename Kind::allocator_type;
                  one.get_allocator();
                  Kind(one, one.get_allocator());
                }) {
    return Kind(one, one.get_allocator());
  } else {
    return one;
  }
}

template <class SlotsType, std::size_t... At>
[[nodiscard]] constexpr SlotsType copied_slots(const SlotsType& all,
                                                std::index_sequence<At...>) {
  return SlotsType{copied_gathering(std::get<At>(all))...};
}

template <class StatesType, std::size_t CommandCount>
[[nodiscard]] constexpr auto keep_gatherings(
    const StatesType& states,
    const std::array<packed_command, CommandCount>& commands,
    std::size_t count) {
  kept_gatherings<StatesType, CommandCount> kept;
  // Where nothing is gathered at a register there is no array to keep
  // anything out of: the walk holds every gathering itself, and asking for
  // `states[somewhere]` would be asking a row of no elements for one of them.
  if constexpr (std::tuple_size_v<StatesType> == 0) {
    return kept;
  }
  for (std::size_t index = 0; index < count; ++index) {
    if (commands[index].source == packed_command::no_source) continue;
    if (commands[index].value != -2) continue;
    bool already = false;
    for (std::size_t at = 0; at < kept.count; ++at) {
      if (kept.which[at] == commands[index].source) already = true;
    }
    if (already) continue;
    kept.which[kept.count] = commands[index].source;
    using slots_kind = std::remove_cvref_t<decltype(states[0])>;
    kept.held[kept.count].emplace(copied_slots<slots_kind>(
        states[commands[index].source],
        std::make_index_sequence<std::tuple_size_v<slots_kind>>{}));
    ++kept.count;
  }
  return kept;
}

template <std::size_t Group, class Type, fixed_string Format, auto& Automaton,
          bool HandsTheCharacter = true, bool KeptInTheWalk = false,
          class StatesType, class KeptType, class RegistersType,
          std::size_t CommandCount,
          class ToldCarrier = scan::nothing_given>
constexpr void advance_scanner(
    char symbol, std::size_t state, std::size_t left_state, auto position,
    const RegistersType& registers,
    const KeptType& old_states, StatesType& states,
    const std::array<packed_command, CommandCount>& commands,
    std::size_t count, const char* text,
    const ToldCarrier& told = ToldCarrier{}) {
  static constexpr auto spread = spread_of<Type, Format>();
  constexpr std::size_t opening = Group * 2;
  constexpr std::size_t closing = Group * 2 + 1;
  using held_type = leaf_kind_of_output<Type, Group>;
  constexpr bool gathers_a_list = scanned_as_range<held_type>;
  using how = gathering_of<Type, Format, Group>;
  // A gathering the walk keeps for itself is not at a register, so none of
  // what follows is about it: nothing to begin where a group opens, nothing to
  // copy where a reading divides, nothing to hand from one register to
  // another. That is most of what a move used to cost.
  //
  // Asked of whoever is calling, because only one of them keeps gatherings
  // that way: a walk over characters in a row does, and a reader taking a
  // stream a character at a time keeps everything at its registers.
  if constexpr (KeptInTheWalk &&
                (gathers_in_the_walk<Type, Format, Automaton, Group>() ||
                 list_gathers_in_the_walk<Type, Format, Automaton, Group>() ||
                 (Group > 0 &&
                  list_gathers_in_the_walk<Type, Format, Automaton,
                                           Group == 0 ? 0 : Group - 1>()) ||
                 (how::folds && how::the_place && !how::place_repeats &&
                  every_move_says_the_groups<Automaton>()))) {
    return;
  } else {
  // A group inside a folding place is not gathered at all: its place tells the
  // fold what happened to it, and does that for all of its groups in one go,
  // where the order can be got right.
  if constexpr (how::folds && how::inside) return;
  // A field is gathered at the register holding its opening: nothing is written
  // inside a field, so that register stands still while the characters arrive.
  //
  // It is read at the register holding its closing, and the two are joined by a
  // copy made at the moment the group closes. Readings divide: two can hold the
  // same opening and disagree about whether the field is still being read, one
  // having met what follows it and one not, and one gathering cannot be both
  // "bob" and "bob id=7". The one that goes on goes on adding to the opening;
  // the one that has closed keeps the copy taken when it closed, and that is
  // what it is read from.
  //
  // A list is gathered and read at its opening throughout. Its elements go on
  // being added to the same list however the readings divide, and where it
  // began is what says which list that is.
  // Two passes, because a transition can rename a group's opening and write
  // its closing at once: `r86(t3) <- position` beside `r77(t2) <- r80` is a
  // field ending and its opening moving house in the same breath. The copy has
  // to happen first, or the copy taken for the closing is of a register that
  // has not been given what it holds yet.
  std::size_t command_index = 0;
  if constexpr (!gathers_a_list) {
    // The groups that close on this step. What the field gathered is in the
    // opening it was being added to, whichever register that has become.
    command_index = 0;
    std::apply(
        [&](const auto&... command) {
          ([&] {
            if (command_index++ >= count) return;
            if (Automaton.register_tag[command.destination] != closing) return;
            if (slot_read(registers, command.destination) != position) return;
            // What the turn gathered is at the opening the state being left
            // named, not the one the state being entered names: this step is
            // where the group is renamed, and the register the characters went
            // to is the old one.
            const auto& left = Automaton.states[left_state];
            for (std::size_t reading = 0; reading < left.reading_count;
                 ++reading) {
              const std::uint32_t was = left.readings[reading][opening];
              if (stood_nowhere(slot_read(registers, was))) continue;
              std::get<gathering_slot<Type, Format, Group>>(
                  states[command.destination]) =
                  std::get<gathering_slot<Type, Format, Group>>(states[was]);
              break;
            }
          }(),
           ...);
        },
        commands);
  }
  // A turn that ended is set aside where it was gathered.
  //
  // The characters of a turn go to the register the readings of the state being
  // left name for this place's opening. The command that begins the next turn
  // writes a register of its own, and for the first turn of a list that is a
  // register with nothing in it yet -- so setting the turn aside by the command
  // lost the first turn of every list, and only the first: from the second turn
  // on, the register the command writes is the one the turn before it was
  // carried into. It is set aside by the readings now, which is how the turn
  // that ended is looked for everywhere else.
  if constexpr (how::folds && how::the_place && how::place_repeats) {
    bool beginning_another = false;
    std::uint32_t begins_at = 0;
    for (std::size_t index = 0; index < count; ++index) {
      const auto& command = commands[index];
      if (command.value == -2) continue;
      if (Automaton.register_tag[command.destination] != opening) continue;
      beginning_another = true;
      begins_at = command.destination;
      break;
    }
    if (beginning_another) {
      // Gathered at the register the readings of the state being left name for
      // this place. The move that begins the next turn renames the register --
      // turn one lives at one number and turn two at another -- so the turn
      // that ended is looked for where it was gathered, which is the old name.
      const auto& left = Automaton.states[left_state];
      std::array<bool, Automaton.register_count> aside{};
      for (std::size_t reading = 0; reading < left.reading_count; ++reading) {
        const std::uint32_t was = left.readings[reading][opening];
        if (aside[was]) continue;
        aside[was] = true;
        auto& fold =
            std::get<gathering_slot<Type, Format, Group>>(states[was]);
        if (!fold.here.started) continue;
        // Set aside where the next turn will be looked for. The move renames
        // the register -- the turn that ended was gathered under the old name
        // and the turn that follows it is written under the new one -- and
        // whoever makes the element of a turn that ended looks under the new
        // name, because that is where the walk is now.
        auto& into = std::get<gathering_slot<Type, Format, Group>>(
            states[begins_at]);
        into.going = std::move(fold.here);
        into.has_going = true;
        fold.here = std::remove_cvref_t<decltype(fold.here)>(
            context_at_group<Type, Group>(told));
      }
    }
  }
  command_index = 0;
  std::apply(
      [&](const auto&... command) {
        ([&] {
          if (command_index++ >= count) return;
          const std::uint32_t tag = Automaton.register_tag[command.destination];
          if (tag == opening) {
            if (command.source != packed_command::no_source &&
                command.value == -2) {
              // A reading that divides carries its gathering with it.
              std::get<gathering_slot<Type, Format, Group>>(
                  states[command.destination]) =
                  std::get<gathering_slot<Type, Format, Group>>(
                      old_states[command.source]);
            } else if constexpr (gathers_a_list) {
              // A list begins empty, and begins once. This command writes the
              // register that holds where the list opened -- which happens when
              // the list starts and again at a turn boundary, where the
              // boundary has just handed the turn that ended to this very list.
              // Emptying it there throws that turn away, and only that one: the
              // boundaries after it arrive as copies and go down the branch
              // above. So it is emptied where it is starting, and where it is
              // starting the register it is written into stood nowhere.
              if (stood_nowhere(slot_read(registers, command.destination))) {
                auto& stands_here =
                    std::get<gathering_slot<Type, Format, Group>>(
                        states[command.destination]);
                stands_here = made_like(stands_here);
              }
            } else if constexpr (how::folds && how::the_place &&
                                 how::place_repeats) {
              // The turn that ended was set aside above, where it was
              // gathered. What this register holds now is the turn that is
              // beginning, and a turn begins with what its place was told --
              // the same as the first turn did.
              auto& fold = std::get<gathering_slot<Type, Format, Group>>(
                  states[command.destination]);
              fold.here = std::remove_cvref_t<decltype(fold.here)>(
                  context_at_group<Type, Group>(told));
            } else {
              begin_gathering_at(
                  std::get<gathering_slot<Type, Format, Group>>(
                      states[command.destination]),
                  gathering_of<Type, Format, Group>::begin(
                      spread.parameters[Group].view(),
                      context_at_group<Type, Group>(told)));
            }
          } else if (tag == closing && slot_read(registers, command.destination) != position &&
                     command.source != packed_command::no_source &&
                     command.value == -2) {
            // A closing already written, only being carried along, keeps what
            // it holds.
            std::get<gathering_slot<Type, Format, Group>>(
                states[command.destination]) =
                std::get<gathering_slot<Type, Format, Group>>(
                    old_states[command.source]);
          }
        }(),
         ...);
      },
      commands);
  if constexpr (how::folds && how::the_place &&
                (how::place_repeats || !KeptInTheWalk ||
                 !every_move_says_the_groups<Automaton>())) {
    // Everything that happened inside this place on this character, told in
    // order -- and told now, before the copy below, or a fold that ends where
    // its place ends would be copied one closing short.
    //
    // Left out only where somebody else is doing the telling. The walk tells
    // the fold from the move, once, where the machine can say what a character
    // lies inside, and saying it again here would say every opening and every
    // character twice.
    //
    // Whether the machine could say it is not on its own the question, and
    // asking only that is what emptied a list read record after record: the
    // reading that steps a character at a time has no move to be told from and
    // never told the fold anything, while the machine, asked by itself,
    // answered that somebody else would.
    fold_the_readings<Group, gathering_slot<Type, Format, Group>,
                      std::remove_cv_t<held_type>, Automaton>(
        state, registers, states, symbol, HandsTheCharacter, text);
  }
  // A list gathers elements, not characters. Written as an early return this
  // would discard nothing: what follows an `if constexpr` is not the branch it
  // did not take.
  //
  // Handing the character over is skipped where the caller knows the state and
  // does it by the numbers: this walks every reading of the state and keeps a
  // flag per register to do it once, which on a subject read a character at a
  // time is most of what reading it costs.
  if constexpr (!gathers_a_list && HandsTheCharacter) {
    // Once each, however many readings share it: a register is one gathering.
    one_per_register<Automaton.register_count> filled;
    const auto& packed = Automaton.states[state];
    for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
      const std::uint32_t open = packed.readings[reading][opening];
      const std::uint32_t close = packed.readings[reading][closing];
      if (filled.test(open)) continue;
      if (stood_nowhere(slot_read(registers, open)) ||
          closed_since_turn(slot_read(registers, close), slot_read(registers, open),
                            how::place_repeats))
        continue;
      filled.set(open);
      gathering_of<Type, Format, Group>::push(
          std::get<gathering_slot<Type, Format, Group>>(states[open]), symbol);
    }
  }
  }
}

// Where a value's gathering is, asked without saying how it is kept.
//
// The machine that gathers keeps one per register, and which register holds a
// place depends on the reading the walk is in. A type that folds its own groups
// keeps them all together in one state of its own. The putting together of a
// value is the same work either way, so it is written once and asks for what it
// needs through one of these.
// Nothing was kept by the walk: every gathering is at a register.
struct nothing_kept_here {};
inline constexpr nothing_kept_here nothing_was_kept{};

// What a walk kept for itself, and which places those are.
//
// The mask is carried beside the gatherings because whoever reads the value
// has the places in hand and not the machine: a place that follows a reading
// is read from the register the reading names, and one the walk kept is read
// from here.
template <class SlotsType, std::uint64_t Places>
struct kept_by_the_walk {
  static constexpr std::uint64_t which_places = Places;
  const SlotsType& slots;
  // Which of them had a turn being gathered when the reading stopped. A list
  // the walk keeps has no register to say so.
  std::uint64_t turns = 0;
};

template <class Type, fixed_string Format, class ReadingType,
          class StatesType, class RegistersType,
          class KeptType = nothing_kept_here,
          class EndingType = ReadingType>
struct gathered_by_the_registers {
  const ReadingType& reading;
  const StatesType& states;
  const RegistersType& registers;
  // What the walk kept for itself, where it kept anything: a gathering that
  // does not follow a reading is not at a register, and this is where it is.
  const KeptType& kept;
  // Where the ending put each tag, for the places the ending is what closed.
  //
  // A tag that the walk closed stands where the reading says throughout, and
  // that is what everything here reads. A group still open where the match
  // ends is closed by the commands that end it and by nothing else, and those
  // write into registers of their own -- so its closing is only to be found
  // here. Two truths rather than one because they are about two different
  // moments, and reading either by the other's registers is reading a register
  // nothing filled.
  const EndingType& ending;

  // A field still being read when the input ended is where it was being
  // gathered; one that ended earlier is the copy taken when it closed, which
  // the readings that went on adding to the opening cannot have changed.
  template <std::size_t Place>
  [[nodiscard]] SCAN_FORCE_INLINE constexpr const auto& gathering() const {
    if constexpr (kept_here<Place>()) {
      return std::get<gathering_slot<Type, Format, Place>>(kept.slots);
    } else {
      const std::uint32_t open = reading[Place * 2];
      const std::uint32_t close = reading[Place * 2 + 1];
      // A place taken over and over is read where it is being gathered: the
      // end standing where the beginning stands is the end of the turn before,
      // and the copy taken then is a turn behind.
      const bool still_reading = !closed_since_turn(
          slot_read(registers, close), slot_read(registers, open),
          gathering_of<Type, Format, Place>::place_repeats);
      return std::get<gathering_slot<Type, Format, Place>>(
          states[still_reading ? open : close]);
    }
  }

  // Whether this place's gathering is one the walk kept. Asked of the slot
  // rather than of the machine, because this is read from where the value is
  // made and the machine is not in hand there.
  template <std::size_t Place>
  [[nodiscard]] static consteval bool kept_here() {
    if constexpr (std::same_as<KeptType, nothing_kept_here>) {
      return false;
    } else {
      return (KeptType::which_places & (std::uint64_t{1} << Place)) != 0;
    }
  }

  // A list is gathered and read at its opening throughout: its elements go on
  // being added to the same list however the readings divide.
  template <std::size_t Place>
  [[nodiscard]] SCAN_FORCE_INLINE constexpr const auto& list() const {
    if constexpr (kept_here<Place>()) {
      return std::get<gathering_slot<Type, Format, Place>>(kept.slots);
    } else {
      return std::get<gathering_slot<Type, Format, Place>>(
          states[reading[Place * 2]]);
    }
  }

  template <std::size_t Place>
  [[nodiscard]] SCAN_FORCE_INLINE constexpr bool took_part() const {
    // A turn the walk was gathering is said by the walk: there is no register
    // holding where it began, because it was never at a register.
    if constexpr (kept_here<Place>()) {
      if constexpr (std::same_as<KeptType, nothing_kept_here>) {
        return false;
      } else {
        return (kept.turns & (std::uint64_t{1} << Place)) != 0;
      }
    } else {
      return !stood_nowhere(slot_read(registers, reading[Place * 2]));
    }
  }

  // What a place stood on, where the subject can be pointed at. Nothing where
  // the place took no part.
  //
  // A position here is how many characters have been read and not the index of
  // one, so what a place stood on begins one before where its opening says.
  template <std::size_t Place>
  [[nodiscard]] SCAN_FORCE_INLINE constexpr std::string_view span(const char* text) const {
    const auto began = slot_read(registers, reading[Place * 2]);
    if (stood_nowhere(began)) return {};
    const auto walked = slot_read(registers, reading[Place * 2 + 1]);
    // Closed as the walk passed, or closed by the commands that end a match
    // because the match ended while it was still open. The second is not in
    // the reading at all -- the ending writes registers of its own -- and read
    // through the reading such a group looked like one that never closed.
    const auto ended =
        closed_since(walked, began) ? walked : slot_read(registers, ending[Place * 2 + 1]);
    if (!closed_since(ended, began)) return {};
    if constexpr (std::is_pointer_v<std::remove_cvref_t<decltype(began)>>) {
      // What a group stood on is what lies between its marks. Nothing is taken
      // off: a mark is written as the walk stands on the character, and the
      // step that used to be taken off here was a character of every group.
      static_cast<void>(text);
      return std::string_view(began, static_cast<std::size_t>(ended - began));
    } else {
      return stood_on(text, began, ended);
    }
  }

  // What was written after the colon at this place, where anything was. Asked
  // of the source because the format is known here and not where the value is
  // put together.
  template <std::size_t Place>
  [[nodiscard]] static constexpr std::string_view parameters_at() {
    return format_parameters<Type, Format>::at(Place);
  }

  // A fold at this place, with the last step run into the copy: the end of the
  // input is not a character, so what it left open is closed here.
  template <std::size_t Place, class Held>
  [[nodiscard]] SCAN_FORCE_INLINE constexpr auto fold_at() const {
    auto fold = gathering<Place>();
    fold_one_step<fold_phase::whole, Place, Held>(fold.here, reading, registers,
                                                  '\0', false);
    return fold;
  }
};

// Made rather than named: the reading, the states and the registers are all
// deduced, and the type and the format are what say where a group is gathered.
template <class Type, fixed_string Format, class ReadingType,
          class StatesType, class RegistersType,
          class KeptType = nothing_kept_here>
[[nodiscard]] constexpr auto by_the_registers(
    const ReadingType& reading, const StatesType& states,
    const RegistersType& registers,
    const KeptType& kept = nothing_was_kept) {
  return gathered_by_the_registers<Type, Format, ReadingType, StatesType,
                                   RegistersType, KeptType, ReadingType>{
      reading, states, registers, kept, reading};
}

// The same, told as well where the ending put the tags it closed.
template <class Type, fixed_string Format, class ReadingType,
          class StatesType, class RegistersType, class KeptType,
          class EndingType>
[[nodiscard]] constexpr auto by_the_registers(const ReadingType& reading,
                                              const StatesType& states,
                                              const RegistersType& registers,
                                              const KeptType& kept,
                                              const EndingType& ending) {
  return gathered_by_the_registers<Type, Format, ReadingType, StatesType,
                                   RegistersType, KeptType, EndingType>{
      reading, states, registers, kept, ending};
}


template <class Root, class Type, std::size_t Offset, bool AsOutput = false,
          class FailureType = failure_for<Root>, class SourceType,
          class ToldCarrier = scan::nothing_given>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::expected<Type, FailureType> finish_value(
    const SourceType& source, const char* text,
    const ToldCarrier& given = ToldCarrier{});

// The parts of a product, and the arguments of a call, as named functions
// rather than as lambdas called where they stand. A lambda holding references
// and called inside the argument of something that itself holds references is
// more than the constant evaluator will follow.

template <class Root, class Type, std::size_t Offset, class FailureType,
          class SourceType, class ToldCarrier, std::size_t... Part>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::expected<Type, FailureType> finish_parts(
    const SourceType& source, const char* text, const ToldCarrier& given,
    std::index_sequence<Part...>) {
  auto parts =
      std::tuple{finish_value<Root, typename parts_of<Type>::template at<Part>,
                              Offset + groups_before_field<Type, Part>(), false,
                              FailureType>(
          source, text, told_for_part<Part>(given))...};
  if (auto went_wrong = what_went_wrong<FailureType>(parts)) {
    return std::unexpected(std::move(*went_wrong));
  }
  return Type{std::move(*std::get<Part>(parts))...};
}

template <class Root, class Type, std::size_t Offset, class FailureType,
          class SourceType, class ToldCarrier, std::size_t... Part>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::expected<Type, FailureType> finish_by_call(
    const SourceType& source, const char* text, const ToldCarrier& given,
    std::index_sequence<Part...>) {
  auto parts =
      std::tuple{finish_value<Root, typename parts_of<Type>::template at<Part>,
                              Offset + groups_before_field<Type, Part>(), false,
                              FailureType>(
          source, text, told_for_part<Part>(given))...};
  if (auto went_wrong = what_went_wrong<FailureType>(parts)) {
    return std::unexpected(std::move(*went_wrong));
  }
  return scan::scanner<std::remove_cv_t<Type>>::parse(
      std::move(*std::get<Part>(parts))...);
}

// An element ends where the next one begins, and where that is, is said by a
// command writing a fresh position into the group the element starts at. The
// positions still hold the turn that is ending when this runs, which is why it
// runs before they move.
// A list is written to with push_back, which is what a range is asked for. It
// is reached here through insert where the type has it, because libc++ writes
// vector::emplace_back through a helper taking two capturing lambdas, and
// clang's constant evaluator refuses those ("captures not currently allowed"):
// a list of anything but a leaf could not be read while compiling. Appending
// at the end is the same thing either way.
template <class ListType, class ElementType>
constexpr void append_to(ListType& list, ElementType&& value) {
  // `insert` only where it has to be. It is here at all because libc++ writes
  // `vector::emplace_back` through a helper taking two capturing lambdas, and
  // clang's constant evaluator refuses those -- so a list of anything could
  // not be read while compiling. That is a reason to spell it this way while
  // compiling and no reason at all to spell it this way while running, where
  // `insert` costs the best part of a nanosecond an element more.
  if constexpr (requires { list.insert(list.end(), std::move(value)); }) {
    if consteval {
      list.insert(list.end(), std::move(value));
    } else {
      if constexpr (requires { list.push_back(std::move(value)); }) {
        list.push_back(std::move(value));
      } else {
        list.insert(list.end(), std::move(value));
      }
    }
  } else {
    list.push_back(std::move(value));
  }
}

// The registers a state names for a group, in pairs.
//
// A gathering lives at the register holding the group's opening, and whether
// it is still being added to is said by the closing register of the same
// reading. Both are facts about the state, so they are worked out once here
// rather than walked at every character -- and asked in pairs, because a
// reading exists precisely to disagree with the others about whether the
// group has ended.
template <std::size_t Capacity>
struct gathered_pairs_of {
  std::array<std::uint32_t, Capacity> open{};
  std::array<std::uint32_t, Capacity> shut{};
  std::size_t count = 0;
};

// Which tag of the machine a group of the output is written in.
//
// The two counts are the same only while every group gets a mark. A place whose
// turns are told by the moves gets none -- and then every group after it stands
// one tag earlier than its number says. The mask that decides who gets a mark is
// the one the automaton was built from, so the mapping is that mask, counted.
template <class Type, fixed_string Format, auto& Automaton, std::size_t Group>
[[nodiscard]] consteval bool a_group_with_a_tag() {
  return (groups_whose_mark_is_read<Type, Format, Automaton>() >> Group) & 1;
}

template <class Type, fixed_string Format, auto& Automaton, std::size_t Group>
[[nodiscard]] consteval std::size_t tag_of_group() {
  const std::uint64_t mask = groups_whose_mark_is_read<Type, Format, Automaton>();
  return static_cast<std::size_t>(
      std::popcount(mask & ((std::uint64_t{1} << Group) - 1)));
}

template <auto& Automaton, std::size_t State, std::size_t Group>
inline constexpr auto gathered_pairs = [] consteval {
  constexpr std::size_t capacity =
      Automaton.states[State].readings.size() == 0
          ? 1
          : Automaton.states[State].readings.size();
  gathered_pairs_of<capacity> said;
  const auto& packed = Automaton.states[State];
  // Asked about a tag this machine does not have, nobody stands at it. The
  // callers say which tag they mean rather than which group, so this is a
  // backstop and not the mapping itself.
  if (Group * 2 + 1 >= packed.readings[0].size()) return said;
  for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
    const std::uint32_t open = packed.readings[reading][Group * 2];
    const std::uint32_t shut = packed.readings[reading][Group * 2 + 1];
    bool already = false;
    for (std::size_t at = 0; at < said.count; ++at) {
      if (said.open[at] == open && said.shut[at] == shut) already = true;
    }
    if (already) continue;
    said.open[said.count] = open;
    said.shut[said.count] = shut;
    ++said.count;
  }
  return said;
}();

template <std::size_t Group, class Type, fixed_string Format, auto& Automaton,
          class FailureType, class StatesType, class RegistersType,
          std::size_t CommandCount,
          class ToldCarrier = scan::nothing_given>
constexpr void collect_element(
    std::size_t state, const RegistersType& registers, StatesType& states,
    const std::array<packed_command, CommandCount>& commands,
    std::size_t count, const char* text, std::optional<FailureType>& failed,
    const ToldCarrier& told = ToldCarrier{}) {
  if constexpr (Group == 0) {
    return;
  } else if constexpr (!scanned_as_range<leaf_kind_of_output<Type, Group - 1>>) {
    return;
  } else {
    using list_type = leaf_kind_of_output<Type, Group - 1>;
    using element = std::remove_cvref_t<std::ranges::range_value_t<list_type>>;
    constexpr std::size_t list_group = Group - 1;
    if constexpr (gathering_of<Type, Format, Group>::folds) {
      // Made out of the turn that was moved aside, once that turn has been
      // told what ended it. Nothing here can be: at this moment it has not.
      return;
    } else if constexpr (list_gathers_in_the_walk<Type, Format, Automaton,
                                                  list_group>()) {
      // The walk is keeping this one and closes its turns from the moves.
      return;
    } else {
    // Does this step begin another turn? It does if it writes a fresh position
    // into a register that holds the place an element starts at.
    bool going_round = false;
    for (std::size_t index = 0; index < count; ++index) {
      const auto& command = commands[index];
      if (command.value == -2) continue;
      if (Automaton.register_tag[command.destination] == Group * 2) {
        going_round = true;
        break;
      }
    }
    if (!going_round) return;
    // The turn that is ending belongs to the state being left, and so do the
    // positions and the gatherings. Where the list goes next is the business of
    // the commands, which carry the gathering with the register.
    const auto& packed = Automaton.states[state];
    std::array<bool, Automaton.register_count> done{};
    for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
      const std::uint32_t open = packed.readings[reading][Group * 2];
      const std::uint32_t into = packed.readings[reading][list_group * 2];
      if (done[into] || stood_nowhere(slot_read(registers, open))) continue;
      done[into] = true;
      // An element of a list is a reading of its own, and what the list's place
      // was told is what it was told: a whole shape standing here reads its
      // places with it, the way one standing anywhere else does.
      auto one = finish_value<Type, element, Group, false, FailureType>(
          by_the_registers<Type, Format>(packed.readings[reading], states,
                                         registers),
          text, context_at_group<Type, list_group>(told));
      if (!one) {
        if (!failed) failed = std::move(one).error();
        continue;
      }
      append_to(std::get<gathering_slot<Type, Format, list_group>>(states[into]),
                std::move(*one));
    }
    }
  }
}

// The turn that was moved aside, made into an element now that the step which
// ended it has been said.
//
// It is looked for at every register, not at the readings of one state: a
// gathering travels with its register, and the move that ended the turn may
// have put it anywhere. What says there is one is the fold itself.
template <std::size_t Group, class Type, fixed_string Format, auto& Automaton,
          class FailureType, class StatesType, class RegistersType>
constexpr void collect_turn_that_ended(
    std::size_t state, const RegistersType& registers,
    StatesType& states, std::optional<FailureType>& failed) {
  if constexpr (Group == 0) {
    return;
  } else if constexpr (!scanned_as_range<leaf_kind_of_output<Type, Group - 1>>) {
    return;
  } else if constexpr (!gathering_of<Type, Format, Group>::folds) {
    return;
  } else {
    using list_type = leaf_kind_of_output<Type, Group - 1>;
    using element = std::remove_cvref_t<std::ranges::range_value_t<list_type>>;
    using held = std::remove_cv_t<element>;
    constexpr std::size_t list_group = Group - 1;
    const auto& packed = Automaton.states[state];
    std::array<bool, Automaton.register_count> done{};
    for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
      const std::uint32_t open = packed.readings[reading][Group * 2];
      const std::uint32_t into = packed.readings[reading][list_group * 2];
      if (done[open]) continue;
      done[open] = true;
      auto& fold = std::get<gathering_slot<Type, Format, Group>>(states[open]);
      if (!fold.has_going) continue;
      fold.has_going = false;
      if (fold.going.wanted_a_subject) {
        if (!failed) {
          failed = scan::as_a_failure<FailureType>(wrong_subject<>(
              "a fold that only takes its groups whole needs a subject that "
              "can be pointed at: give it push_group to read a stream"));
        }
        continue;
      }
      if constexpr (scan::says_what_went_wrong_folding<held>) {
        auto got =
            scan::scanner_told_finish_groups<held>(std::move(fold.going.state));
        if (!got) {
          if (!failed) {
            failed = scan::as_a_failure<FailureType>(std::move(got).error());
          }
          continue;
        }
        append_to(
            std::get<gathering_slot<Type, Format, list_group>>(states[into]),
            std::move(*got));
      } else {
        append_to(
            std::get<gathering_slot<Type, Format, list_group>>(states[into]),
            scan::scanner<held>{}.finish_groups(std::move(fold.going.state)));
      }
    }
  }
}

template <class Type, fixed_string Format, auto& Automaton, class FailureType,
          class RegistersType, class StatesType, std::size_t... Group>
constexpr void collect_turns_that_ended(
    std::size_t state, const RegistersType& registers,
    StatesType& states, std::index_sequence<Group...>,
    std::optional<FailureType>& failed) {
  (collect_turn_that_ended<Group, Type, Format, Automaton, FailureType>(
       state, registers, states, failed),
   ...);
}

template <class Type, fixed_string Format, auto& Automaton, class FailureType,
          class RegistersType, class StatesType, std::size_t CommandCount,
          class ToldCarrier = scan::nothing_given, std::size_t... Group>
constexpr void collect_elements(
    std::size_t state, const RegistersType& registers, StatesType& states,
    const std::array<packed_command, CommandCount>& commands,
    std::size_t count, std::index_sequence<Group...>, const char* text,
    std::optional<FailureType>& failed,
    const ToldCarrier& told = ToldCarrier{}) {
  (collect_element<Group, Type, Format, Automaton, FailureType>(
       state, registers, states, commands, count, text, failed, told),
   ...);
}

template <class Type, fixed_string Format, auto& Automaton,
          bool HandsTheCharacter = true, bool KeptInTheWalk = false,
          class RegistersType, class StatesType, std::size_t CommandCount,
          class ToldCarrier = scan::nothing_given, std::size_t... Group>
constexpr void advance_scanners(
    char symbol, std::size_t state, std::size_t left_state, auto position,
    const RegistersType& registers,
    StatesType& states,
    const std::array<packed_command, CommandCount>& commands,
    std::size_t count, std::index_sequence<Group...>,
    const char* text = nullptr, const ToldCarrier& told = ToldCarrier{}) {
  // The old gatherings are only needed where a command copies one, and inside a
  // field nothing is copied and nothing is written -- that is what holding the
  // tags back bought. Copying the whole set on every character to be ready for
  // a copy that is not coming is the difference between reading a long command
  // and reading it twice for nothing.
  bool copies = false;
  for (std::size_t index = 0; index < count; ++index) {
    if (commands[index].source != packed_command::no_source &&
        commands[index].value == -2) {
      copies = true;
      break;
    }
  }
  if (copies) {
    const auto old_states = keep_gatherings(states, commands, count);
    (advance_scanner<Group, Type, Format, Automaton, HandsTheCharacter,
                     KeptInTheWalk>(
         symbol, state, left_state, position, registers, old_states, states,
         commands, count, text, told),
     ...);
  } else {
    (advance_scanner<Group, Type, Format, Automaton, HandsTheCharacter,
                     KeptInTheWalk>(
         symbol, state, left_state, position, registers, states, states,
         commands, count, text, told),
     ...);
  }
}

template <class Type, class StateType, std::size_t... Index>
[[nodiscard]] constexpr Type finish_scanners(
    StateType state, std::index_sequence<Index...>) {
  Type result{};
  ((scan::fields<Type>::template of<Index>(result) =
        scanner_finish<field_type<Type, Index>>(
            std::move(std::get<Index>(state)))),
   ...);
  return result;
}

// The output put together from the gatherings, walked the same way it is walked
// when the values are pieces of a subject that can be pointed at: a value asks
// its own reader to finish, a product asks its parts, a type made by a call
// makes it. Each value is taken from the gathering of the register that holds
// its opening tag in the reading that accepted.
template <class Root, class Type, std::size_t Offset, bool AsOutput,
          class FailureType, class SourceType, class ToldCarrier>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::expected<Type, FailureType> finish_value(
    const SourceType& source, const char* text, const ToldCarrier& given) {
  // A shape that reads its own groups is a value where it stands in somebody
  // else's format and a product of places in its own. Where this is the whole
  // of what is being read, it is the second.
  constexpr bool a_value = scanned_as_leaf<Type> && !AsOutput;
  if constexpr (a_value && folds_by_turns<std::remove_cv_t<Type>>) {
    // A leaf that was told its groups as the walk passed them. What is left is
    // the end of the input, which is not a character and so was never handed
    // over: a group that opened where nothing followed it, and every group
    // still open when the reading stopped. The same step the walk runs says
    // both, asked once more with nothing to hand over.
    using held = std::remove_cv_t<Type>;
    static_assert(
        requires { source.template fold_at<Offset, held>(); },
        "a shape whose places include a type that folds its own groups is read "
        "by the machine that gathers, not by a fold of its own");
    auto fold = source.template fold_at<Offset, held>();
    if (fold.here.wanted_a_subject) {
      return std::unexpected(scan::as_a_failure<FailureType>(wrong_subject<>(
          "a fold that only takes its groups whole needs a subject that can be "
          "pointed at: give it push_group to read a stream")));
    }
    if constexpr (scan::says_what_went_wrong_folding<held>) {
      auto got =
          scan::scanner_told_finish_groups<held>(std::move(fold.here.state));
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<FailureType>(std::move(got).error()));
    } else {
      return scan::scanner_told_finish_groups<held>(std::move(fold.here.state));
    }
  } else if constexpr (a_value && gathers_by_its_groups<Type>) {
    // A leaf built from its own groups once the match is over. They are groups
    // of this match like any others and the positions say where each one
    // stood, so what it is handed are views of the subject: nothing was
    // gathered for it and nothing was copied. That it has a subject to point
    // at is settled where the walk is made.
    using held = std::remove_cv_t<Type>;
    constexpr std::size_t inside = groups_a_leaf_opens<held>();
    std::array<std::string_view, inside> theirs{};
    std::array<bool, inside> took{};
    static_assert(
        requires { source.template span<Offset>(text); },
        "a shape whose places include a type built from its own groups is read "
        "by the machine that gathers, not by a fold of its own");
    [&]<std::size_t... at>(std::index_sequence<at...>) {
      ((void)[&] {
        constexpr std::size_t which = Offset + 1 + at;
        const std::string_view stood_on = source.template span<which>(text);
        if (stood_on.data() == nullptr) return;
        took[at] = true;
        theirs[at] = stood_on;
      }(), ...);
    }(std::make_index_sequence<inside>{});
    const auto pieces = std::span<const std::string_view>(theirs);
    if constexpr (scan::says_what_went_wrong_from_groups<held>) {
      auto got = [&] {
        if constexpr (requires {
                        scan::scanner_told_from_groups<held>(pieces, given);
                      }) {
          return scan::scanner_told_from_groups<held>(pieces, given);
        } else {
          return scan::scanner_told_from_groups<held>(pieces);
        }
      }();
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<FailureType>(std::move(got).error()));
    } else if constexpr (requires {
                           scan::scanner<held>{}.from_groups(pieces, given);
                         }) {
      return scan::scanner<held>{}.from_groups(pieces, given);
    } else if constexpr (requires {
                           scan::scanner<held>{}.from_groups(pieces);
                         }) {
      return scan::scanner<held>{}.from_groups(pieces);
    } else {
      auto state = begun_groups<held>(given);
      [&]<std::size_t... at>(std::index_sequence<at...>) {
        ((void)[&] {
          if (!took[at]) return;
          open_one_group<held, at>(state);
          close_one_group<held, at>(state, theirs[at]);
        }(), ...);
      }(std::make_index_sequence<inside>{});
      if constexpr (scan::says_what_went_wrong_folding<held>) {
        auto got =
            scan::scanner_told_finish_groups<held>(std::move(state));
        if (got) return std::move(*got);
        return std::unexpected(
            scan::as_a_failure<FailureType>(std::move(got).error()));
      } else {
        return scan::scanner<held>{}.finish_groups(std::move(state));
      }
    }
  } else if constexpr (a_value &&
                       reads_a_whole_piece_only<std::remove_cv_t<Type>>) {
    // A leaf that only reads a piece handed to it whole. Nothing was gathered
    // for it: the marks say where its piece stood, and it is handed a view of
    // the subject, so nothing was copied to get here either. That there is a
    // subject to point at is settled where the walk is made, the same as for a
    // leaf built from its own groups.
    using held = std::remove_cv_t<Type>;
    static_assert(
        requires { source.template span<Offset>(text); },
        "a place whose type only reads a piece handed to it whole is read by "
        "the machine that keeps the subject, not by one that is fed");
    const std::string_view piece = source.template span<Offset>(text);
    constexpr std::string_view parameters =
        SourceType::template parameters_at<Offset>();
    if constexpr (scan::says_what_went_wrong<held>) {
      auto got =
          scan::scanner_told_parse<held, scan::hands_a_failure_back>(
              piece, parameters);
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<FailureType>(std::move(got).error()));
    } else {
      if constexpr (std::same_as<ToldCarrier, scan::nothing_given>) {
        return scan::scanner_parse<held>(piece, parameters);
      } else {
        auto got = parse_value_given<held, FailureType,
                                     ToldCarrier::told_apart>(piece, parameters,
                                                               given.leaf());
        if (got) return std::move(*got);
        return std::unexpected(std::move(got).error());
      }
    }
  } else if constexpr (a_value) {
    // Finished from a copy that keeps what the gathering was made with. A
    // scanner takes its state by value, and a container that keeps a resource
    // does not hand it on when it is copied -- so handing the gathering over as
    // it stands would finish a value on the default resource, however the place
    // was told to gather it.
    const auto& gathered = source.template gathering<Offset>();
    if constexpr (scan::says_what_went_wrong_finishing<Type>) {
      auto got = scan::scanner_told_finish<std::remove_cv_t<Type>>(
          copied_gathering(gathered));
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<FailureType>(std::move(got).error()));
    } else {
      return scanner_finish<Type>(copied_gathering(gathered));
    }
  } else if constexpr (scanned_as_range<Type>) {
    // What has been put in as each element ended, and then the one that was
    // still being read when the whole thing ended.
    using element = std::remove_cvref_t<std::ranges::range_value_t<Type>>;
    // Taken with the allocator it was gathered with. A container that keeps a
    // resource does not hand it on when it is copied -- that is what
    // select_on_container_copy_construction says -- so a list gathered into
    // the caller's resource would arrive holding the default one.
    // Handed back on the resource the caller said, whatever the walk gathered
    // it on. What a walk allocates while it reads is its own business -- it may
    // keep a gathering, copy it between registers, begin it again on a turn --
    // and none of that should decide where the value the caller is handed
    // lives. So the list is taken onto the resource its place was told about,
    // and onto the one it was gathered with where its place was told nothing.
    Type made = [&] -> Type {
      const auto& gathered = source.template list<Offset>();
      // Asked of the very construction that would be used: a container with an
      // allocator of its own is not thereby a container that takes a resource,
      // and asking the wrong question here says yes for every one of them.
      if constexpr (requires {
                      Type(gathered,
                           std::pmr::polymorphic_allocator<
                               typename Type::value_type>{});
                    }) {
        if (std::pmr::memory_resource* where = resource_of(given)) {
          return Type(gathered,
                      std::pmr::polymorphic_allocator<typename Type::value_type>(
                          where));
        }
        return Type(gathered, gathered.get_allocator());
      } else if constexpr (requires {
                             Type(gathered, gathered.get_allocator());
                           }) {
        return Type(gathered, gathered.get_allocator());
      } else {
        return gathered;
      }
    }();
    // The turn that was still going when the whole thing ended. Where the list
    // is written to be allowed none at all, there may not have been one.
    if (source.template took_part<Offset + 1>()) {
      auto last = finish_value<Root, element, Offset + 1, false, FailureType>(
          source, text, given);
      if (!last) return std::unexpected(std::move(last).error());
      append_to(made, std::move(*last));
    }
    return made;
  } else if constexpr (scanned_as_variant<Type>) {
    // Exactly one branch ran, and its mark says so: the mark of the branch
    // that took part was opened, and the others never were. The same question
    // the subject that can be pointed at answers by whether the group points
    // anywhere.
    return [&]<std::size_t... branch>(std::index_sequence<branch...>)
               -> std::expected<Type, FailureType> {
      std::optional<std::expected<Type, FailureType>> made;
      const auto take = [&]<std::size_t which>() {
        constexpr std::size_t mark =
            Offset + groups_before_branch<Type, which>();
        if (made || !source.template took_part<mark>()) return;
        using alternative = branch_at<Type, which>;
        auto part = finish_value<Root, alternative, mark + 1, false,
                                 FailureType>(source, text,
                                               told_for_part<which>(given));
        if (!part) {
          made = std::unexpected(std::move(part).error());
          return;
        }
        made = scan::branches<std::remove_cv_t<Type>>::template make<which>(
            std::move(*part));
      };
      (take.template operator()<branch>(), ...);
      if (!made) {
        return std::unexpected(scan::as_a_failure<FailureType>(
            no_match<>("no branch of the format took the input")));
      }
      return std::move(*made);
    }(std::make_index_sequence<branch_count<Type>()>{});
  } else if constexpr (scanned_from_values<Type>) {
    return finish_by_call<Root, Type, Offset, FailureType>(
        source, text, given, std::make_index_sequence<parts_of<Type>::count>{});
  } else {
    return finish_parts<Root, Type, Offset, FailureType>(
        source, text, given, std::make_index_sequence<parts_of<Type>::count>{});
  }
}

// A shape's places, gathered together, and what the walk has told it.
//
// This is the other way of answering the builder's questions. Where the machine
// keeps a gathering per register and works out which register holds a place,
// this keeps them all in one object -- which is what a type is handed when it
// is told its own groups, and it is told them because its places take turns.
template <class Type, fixed_string Format,
          class ToldType = scan::default_context_t>
struct shape_turns {
  using held = std::remove_cv_t<Type>;
  static constexpr std::size_t places = groups_of_output<held>();
  using gatherings_type =
      decltype(make_scanner_state_told<held, Format>(
          std::declval<const ToldType&>()));

  constexpr shape_turns() = default;
  constexpr explicit shape_turns(const ToldType& given)
      : gatherings(make_scanner_state_told<held, Format>(given)),
        told(given) {}

  gatherings_type gatherings =
      make_scanner_state_told<held, Format>(ToldType{});
  // An element that did not read, kept until there is somebody to hand it to:
  // a turn ends in the middle of a walk, where there is nowhere to say so.
  std::optional<shape_failure<held>> went_wrong{};
  // Which places have been opened since they were last read out. A choice says
  // which branch ran by which mark opened; a list says whether a turn is going.
  std::array<bool, places == 0 ? 1 : places> took{};
  // What this shape was told. A turn ends inside the walk, where the caller is
  // long out of reach, so what was said at the door is kept here until the
  // element a turn makes asks for it.
  [[no_unique_address]] ToldType told{};
};

template <class ShapeType>
struct gathered_by_a_fold {
  ShapeType& state;

  template <std::size_t Place>
  [[nodiscard]] constexpr const auto& gathering() const {
    return std::get<Place>(state.gatherings);
  }

  template <std::size_t Place>
  [[nodiscard]] constexpr const auto& list() const {
    return std::get<Place>(state.gatherings);
  }

  template <std::size_t Place>
  [[nodiscard]] constexpr bool took_part() const {
    return state.took[Place];
  }

  // A fold at this place has been told everything as it happened -- the walk
  // hands the edges on and this hands them further -- so there is nothing left
  // to run into it here.
  template <std::size_t Place, class Held>
  [[nodiscard]] constexpr auto fold_at() const {
    return std::get<Place>(state.gatherings);
  }
};

// Where a group of a shape belongs: the place it is, or the place it is inside
// of and which of that type's own groups it is.
template <class Type, std::size_t Group>
inline constexpr std::size_t shape_place_of =
    Group - leaf_offset_of_output<std::remove_cv_t<Type>, Group>;

template <class Type, std::size_t Group>
inline constexpr std::size_t shape_place_inside =
    leaf_offset_of_output<std::remove_cv_t<Type>, Group>;

// One character, to the place it fell in -- or to the type standing at that
// place, where the group is one of that type's own. A list's own group holds no
// characters: what is inside it are the places of one turn, and they take them.
template <class Type, fixed_string Format, std::size_t Group, class ShapeType>
constexpr void push_shape_place(ShapeType& state, char letter) {
  using held = std::remove_cv_t<Type>;
  constexpr std::size_t place = shape_place_of<held, Group>;
  constexpr std::size_t inside = shape_place_inside<held, Group>;
  using stands_for = std::remove_cv_t<leaf_kind_of_output<held, Group>>;
  if constexpr (inside != 0) {
    push_one_group<stands_for, inside - 1>(
        std::get<place>(state.gatherings).here.state, letter);
  } else if constexpr (gathers_by_its_groups<stands_for> ||
                       scanned_as_range<stands_for>) {
    // Neither takes characters of its own: a list holds turns and the places
    // inside it take them, and a type that reads its own groups is told them
    // by the groups, which are the places after this one.
    static_cast<void>(state);
    static_cast<void>(letter);
  } else {
    scanner_push<stands_for>(std::get<place>(state.gatherings), letter);
  }
}

// A place opened. Said so that a choice can be asked which branch ran and a
// list whether a turn is going -- and handed on where the group belongs to the
// type standing at that place.
template <class Type, fixed_string Format, std::size_t Group, class ShapeType>
constexpr void open_shape_place(ShapeType& state) {
  using held = std::remove_cv_t<Type>;
  constexpr std::size_t place = shape_place_of<held, Group>;
  constexpr std::size_t inside = shape_place_inside<held, Group>;
  if constexpr (inside != 0) {
    using stands_for = std::remove_cv_t<leaf_kind_of_output<held, Group>>;
    open_one_group<stands_for, inside - 1>(
        std::get<place>(state.gatherings).here.state);
  } else {
    state.took[place] = true;
  }
}

// A place closed. Where it is a list, that is one turn: the element is put
// together out of the places inside it, added to the list, and those places
// begin again for the turn that may follow.
template <class Type, fixed_string Format, std::size_t Group,
          class FailureType, class ShapeType>
constexpr void close_shape_place(ShapeType& state,
                                 std::optional<FailureType>& failed) {
  using held = std::remove_cv_t<Type>;
  constexpr std::size_t place = shape_place_of<held, Group>;
  constexpr std::size_t inside = shape_place_inside<held, Group>;
  using stands_for = std::remove_cv_t<leaf_kind_of_output<held, Group>>;
  if constexpr (inside != 0) {
    close_one_group<stands_for, inside - 1>(
        std::get<place>(state.gatherings).here.state);
  } else if constexpr (scanned_as_range<stands_for>) {
    using element = std::remove_cvref_t<std::ranges::range_value_t<stands_for>>;
    if (!state.took[place + 1]) return;
    // Told nothing, this is the walk it always was: the same call, with
    // nothing in the place of a context. Told something, the element is made
    // with what its place was given.
    auto one = [&] {
      if constexpr (std::same_as<std::remove_cvref_t<decltype(state.told)>,
                                 scan::default_context_t>) {
        return finish_value<held, element, place + 1, false, FailureType>(
            gathered_by_a_fold<ShapeType>{state}, nullptr);
      } else {
        return finish_value<held, element, place + 1, false, FailureType>(
            gathered_by_a_fold<ShapeType>{state}, nullptr,
            context_at_group<held, Group>(state.told));
      }
    }();
    if (!one) {
      if (!failed) failed = std::move(one).error();
      return;
    }
    append_to(std::get<place>(state.gatherings), std::move(*one));
    // The turn is over: what its places gathered belongs to the element that
    // has just been taken, and the next turn starts from nothing.
    static constexpr auto spread = spread_of<held, Format>();
    [&]<std::size_t... inside>(std::index_sequence<inside...>) {
      ((void)[&] {
        constexpr std::size_t which = place + 1 + inside;
        std::get<which>(state.gatherings) =
            gathering_of<held, Format, which>::begin(
                spread.parameters[which].view());
        state.took[which] = false;
      }(), ...);
    }(std::make_index_sequence<groups_of<element>()>{});
  }
}

// Fed a character at a time. Whoever holds it says where the input ends, so
// the reading is anchored by default -- the walks below a match are kept, and
// the answer is the first still accepting when the feeding stops. A prefix
// read off a stream asks for the other policy, and stops where the match ends.
template <class Type, fixed_string Format, bool Cut = true,
          class FailureType = failure_for<Type>>
class stream_state {
 private:
  static_assert(
      !a_flat_reader_inside<Type>(),
      "a type built from its groups after the match cannot read a stream: "
      "there is nothing left to point at by the time it would be handed them "
      "-- give it begin_groups and push_group to be told its groups as they "
      "arrive");
  // The whole machine, marks and all.
  //
  // Everything else walks the machine with the marks nobody reads taken out --
  // it hears what happened to a place from the moves. This reading has no
  // moves to hear it from: it steps the states one character at a time and
  // asks the positions, so it needs every mark the expression writes.
  inline static constexpr const auto& automaton =
      streaming_automaton_whole<Type, Format, Cut>;
  inline static constexpr std::size_t field_count = groups_of_output<Type>();
  // One gathering per register, because a gathering follows the register it
  // belongs to and there is no arithmetic that says which registers go
  // together.
  inline static constexpr std::size_t slot_count = automaton.register_count;
  using field_states = register_state<Type, Format>;

 public:
  constexpr stream_state() {
    scanner_states_ = make_register_states<Type, Format, automaton>();
    std::ranges::fill(registers_, scan::tre::negative_tag);
    execute_initial<automaton>(registers_, std::ptrdiff_t{0});
  }

  constexpr void push(char symbol) {
    if (!offer(symbol)) state_ = packed_range<0>::reject;
  }

  // The same step, told rather than assumed: false means the machine could not
  // take this character and has not moved. Whoever is feeding it then knows
  // that what came before is as far as the pattern goes, and holds a character
  // that belongs to whatever comes next.
  [[nodiscard]] constexpr bool offer(char symbol) {
    if (state_ == packed_range<0>::reject) return false;
    const std::size_t run =
        run_taken<automaton>(state_, static_cast<unsigned char>(symbol));
    if (run == no_run) return false;
    const auto* transition = &automaton.states[state_].ranges[run];
    collect_elements<Type, Format, automaton, FailureType>(
        state_, registers_, scanner_states_, transition->commands,
        transition->command_count, std::make_index_sequence<field_count>{},
        nullptr, failed_);
    execute_commands(transition->commands, transition->command_count, registers_,
                     ++position_);
    advance_scanners<Type, Format, automaton>(
        symbol, transition->target, state_, position_, registers_,
        scanner_states_,
        transition->commands, transition->command_count,
        std::make_index_sequence<field_count>{});
    state_ = transition->target;
    return true;
  }

  // Where the machine stands now: would what it has read so far be a whole
  // match? Asked between characters, this is how something that reads a command
  // as it is typed knows the command has arrived.
  [[nodiscard]] constexpr bool accepting() const {
    return state_ != packed_range<0>::reject &&
           automaton.states[state_].accepting_slot !=
               packed_state<0, 0, 0>::not_accepting;
  }

  // What has been gathered for a field so far, in the reading the machine would
  // prefer.
  //
  // Standing in several readings at once, the machine has several answers for a
  // field until the input decides between them; the readings are held in the
  // order the pattern gives them, so the first is the one that would win if the
  // match ended here. That is the one worth showing while a command is still
  // being typed.
  //
  // What comes back is the state of that field's own scanner, as far as it has
  // been fed -- for a field of fixed room, the characters themselves.
  template <std::size_t Field>
  [[nodiscard]] constexpr const auto& gathering() const {
    const std::size_t here =
        state_ == packed_range<0>::reject ? automaton.initial : state_;
    // The same question the reading asks: a field still being typed is where
    // it is being gathered, and one that has closed is the copy taken then.
    const auto& reading = automaton.states[here].readings[0];
    const std::uint32_t open = reading[Field * 2];
    const std::uint32_t close = reading[Field * 2 + 1];
    const bool still_reading = registers_[close] < registers_[open];
    return std::get<Field>(scanner_states_[still_reading ? open : close]);
  }

  // Whether that field is being read right now: begun and not yet ended.
  template <std::size_t Field>
  [[nodiscard]] constexpr bool reading() const {
    if (state_ == packed_range<0>::reject) return false;
    const auto& packed = automaton.states[state_];
    if (packed.reading_count == 0) return false;
    const std::uint32_t open = packed.readings[0][Field * 2];
    const std::uint32_t close = packed.readings[0][Field * 2 + 1];
    return registers_[open] >= 0 && registers_[close] < registers_[open];
  }

  [[nodiscard]] constexpr bool rejected() const {
    return state_ == packed_range<0>::reject;
  }

  // Accepting, with nowhere to go from here: the match is over and saying so
  // costs nothing, where finding out by offering the next character costs that
  // character. A pattern that ends in the thing that ends it -- a newline at
  // the end of a command -- settles like this, and then reading one after
  // another loses nothing at all.
  [[nodiscard]] constexpr bool settled() const {
    if (!accepting()) return false;
    const auto& packed = automaton.states[state_];
    for (std::size_t index = 0; index < packed.range_count; ++index) {
      if (packed.ranges[index].target != packed.ranges[index].reject) {
        return false;
      }
    }
    return true;
  }

  constexpr void restart() { *this = stream_state{}; }

  [[nodiscard]] constexpr std::expected<Type, FailureType> finish() const& {
    stream_state copy = *this;
    return std::move(copy).finish();
  }

  [[nodiscard]] constexpr std::expected<Type, FailureType> finish() && {
    if (failed_) return std::unexpected(std::move(*failed_));
    if (state_ == packed_range<0>::reject) {
      return std::unexpected(scan::as_a_failure<FailureType>(
          no_match<>("input does not match scan expression")));
    }
    const auto slot = automaton.states[state_].accepting_slot;
    if (slot == packed_state<0, 0, 0>::not_accepting) {
      return std::unexpected(scan::as_a_failure<FailureType>(
          no_match<>("input does not match scan expression")));
    }
    // The reading that accepted says which register holds each value. Nothing
    // is written here: the commands that end a match are not run by this
    // machine, so a group that never closed is read from where it was being
    // gathered, which is what the registers say.
    const auto& reached = automaton.states[state_];
    // Nothing to point at: this machine is fed and never holds the subject.
    return finish_value<Type, Type, 0, true, FailureType>(
        by_the_registers<Type, Format>(reached.readings[slot], scanner_states_,
                                       registers_),
        nullptr);
  }

 private:
  std::array<field_states, slot_count> scanner_states_{};
  std::array<std::ptrdiff_t, automaton.register_count> registers_{};
  std::size_t state_ = automaton.initial;
  std::ptrdiff_t position_ = 0;
  // An element of a list that did not read: met in the middle of the walk,
  // where there is nothing to hand it back to yet, so it waits here.
  std::optional<FailureType> failed_;
};

// Which gatherings a group is added to where the machine stands.
//
// A state stands in several readings at once and they can share a register, so
// what a character is added to is the set of the registers holding the group's
// opening across those readings, each of them once. Walking the readings to
// find that out on every character is what a machine that does not know where
// it stands has to do; where the state is known, the set is known, and this is
// it.
template <auto& Automaton, std::size_t State, std::size_t Group>
inline constexpr auto gathered_at = [] consteval {
  constexpr const auto& packed = Automaton.states[State];
  struct answer {
    std::array<std::uint32_t, Automaton.register_count> at{};
    std::size_t count = 0;
  };
  answer said;
  for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
    const std::uint32_t opening = packed.readings[reading][Group * 2];
    bool already = false;
    for (std::size_t at = 0; at < said.count; ++at) {
      if (said.at[at] == opening) already = true;
    }
    if (!already) said.at[said.count++] = opening;
  }
  return said;
}();

// The cold slots, each begun with what its place was told. A slot that has
// nothing to be told is begun the way it always was.
template <class SlotType, class ToldType>
[[nodiscard]] constexpr SlotType made_slot(const ToldType& told) {
  if constexpr (requires { SlotType(told); }) {
    return SlotType(told);
  } else {
    static_cast<void>(told);
    return SlotType{};
  }
}

// The cold slots, taken out of the slots as they are begun for their places.
// Taken rather than made: make_slots asks each place what it was told, and a
// slot built from the whole carrier instead finds no constructor that takes it
// and quietly begins the untold way -- which is where a context said at a
// place used to stop on its way to a scanner that gathers.
template <class ColdType, class AllType, std::size_t... Which>
[[nodiscard]] constexpr ColdType cold_out_of(AllType& all,
                                              std::index_sequence<Which...>) {
  return std::tuple_cat([&] {
    if constexpr (keeps_characters<std::tuple_element_t<Which, AllType>>) {
      return std::tuple<std::tuple_element_t<Which, AllType>>(
          std::move(std::get<Which>(all)));
    } else {
      return std::tuple<>{};
    }
  }()...);
}

template <class Type, fixed_string Format, class MarkType, class ColdType,
          class ToldType>
[[nodiscard]] constexpr ColdType made_cold_at_places(const ToldType& told) {
  auto all = make_slots<Type, Format, MarkType, ToldType>(told);
  return cold_out_of<ColdType>(
      all, std::make_index_sequence<std::tuple_size_v<decltype(all)>>{});
}

template <class ColdType, class ToldType>
[[nodiscard]] constexpr ColdType made_cold(const ToldType& told) {
  if constexpr (requires { std::tuple_size<ColdType>::value; }) {
    return [&]<std::size_t... k>(std::index_sequence<k...>) {
      return ColdType{made_slot<std::tuple_element_t<k, ColdType>>(told)...};
    }(std::make_index_sequence<std::tuple_size_v<ColdType>>{});
  } else {
    static_cast<void>(told);
    return ColdType{};
  }
}

// The gatherer the format reader hands to the walk: the fields' own scanners,
// following the moves.
//
// What the machine that can be stopped and started looks up on every character
// -- the state, its runs, the commands of the run it took, the readings that
// say which register holds which group -- is a constant here, because the walk
// knows where it stands. The work each character does is what it was: apply
// what the move writes to the gatherings, and give the character to the fields
// that are open.
template <class Type, fixed_string Format, auto& Automaton,
          bool Pointable = false, class MarkKind = std::ptrdiff_t,
          class ToldType = scan::default_context_t>
class field_gatherer {
 public:
  using plain_folds_type = decltype(make_slots<Type, Format, MarkKind,
                                             ToldType>());
  // Where the gathering slots lie. They are not part of the gatherer: the
  // walk carries the gatherer from character to character, and a slot that
  // gathers characters would hold the whole of it down in memory.
  using cold_type = cold_slots_of<plain_folds_type>;

 private:
  using warm_type = warm_slots_of<plain_folds_type>;

 public:
  // A type built from its groups once the match is over is handed views of the
  // subject. Where the subject is gone as it is read there is nothing to give
  // it, and holding the characters until the end to give it something would be
  // a hold with no bound.
  static_assert(
      Pointable || !a_flat_reader_inside<Type>(),
      "a type built from its groups after the match needs a subject that can "
      "be pointed at: give it begin_groups and push_group to be told its "
      "groups as they are read, or scan it from something contiguous");
  static constexpr std::size_t field_count = groups_of_output<Type>();
  // Whether anything at all is gathered at a register.
  //
  // Where nothing is -- every place kept by the walk, every group inside one
  // of those told through it -- there is no reason to carry a gathering per
  // register, and carrying one is not free: it is what the walk begins by
  // clearing, and it is large enough that the optimiser will not put any of it
  // in a register of the processor.
  static constexpr bool nothing_at_a_register = [] {
    return [&]<std::size_t... group>(std::index_sequence<group...>) {
      return (true && ... && [] {
        using how = gathering_of<Type, Format, group>;
        if constexpr (how::folds && how::inside) return true;
        if constexpr (gathers_in_the_walk<Type, Format, Automaton, group>()) {
          return true;
        }
        // A list the walk gathers, and the turn of it being gathered, are both
        // held by the walk: neither is at a register, and where nothing else
        // is either there is no array of them to carry, to clear, or to take
        // a copy of at every move.
        if constexpr (list_gathers_in_the_walk<Type, Format, Automaton,
                                               group>()) {
          return true;
        }
        if constexpr (group > 0 &&
                      list_gathers_in_the_walk<Type, Format, Automaton,
                                               group == 0 ? 0 : group - 1>()) {
          return true;
        }
        if constexpr (how::folds && how::the_place && !how::place_repeats &&
                      every_move_says_the_groups<Automaton>()) {
          return true;
        }
        return false;
      }());
    }(std::make_index_sequence<groups_of_output<Type>()>{});
  }();

  using states_type =
      std::array<register_state<Type, Format, MarkKind, ToldType>,
                 nothing_at_a_register ? 0 : Automaton.register_count>;

  // A slot by its number: a warm one out of this gatherer's own tuple, a
  // gathering one out of the tuple the walk owns.
  template <std::size_t Which>
  [[nodiscard]] constexpr auto& slot() {
    using kind = std::tuple_element_t<Which, plain_folds_type>;
    if constexpr (keeps_characters<kind>) return std::get<kind>(*cold_);
    else return std::get<kind>(warm_);
  }
  template <std::size_t Which>
  [[nodiscard]] constexpr const auto& slot() const {
    using kind = std::tuple_element_t<Which, plain_folds_type>;
    if constexpr (keeps_characters<kind>) return std::get<kind>(*cold_);
    else return std::get<kind>(warm_);
  }
  // Все слоты вместе -- для того одного места в конце, где нужен целый набор.
  [[nodiscard]] constexpr plain_folds_type all_slots() const {
    plain_folds_type made{};
    [&]<std::size_t... which>(std::index_sequence<which...>) {
      ((std::get<which>(made) = slot<which>()), ...);
    }(std::make_index_sequence<std::tuple_size_v<plain_folds_type>>{});
    return made;
  }

  // The gathering slots lie outside, so there is no gatherer without them:
  // there is no default constructor, and every place that makes one has to
  // say where they lie. Making that the type's rule rather than a thing to
  // remember is what keeps a gatherer from ever pointing at nothing.
  // Told at the door, because the slots and the register states are made in
  // this body: a context that arrives after the gatherer is built arrives after
  // every state it was meant for has already begun.
  constexpr explicit field_gatherer(cold_type& cold,
                                    const ToldType& told = ToldType{})
      : cold_(&cold), told_(told) {
    begin_again();
  }

  // The slots, made where the places were told rather than made empty and
  // assigned afterwards. A container that keeps a resource does not take the
  // other one's resource when it is assigned, so a slot that begins empty
  // stays on the default resource whatever is put in it later.
  template <class Other>
  [[nodiscard]] static constexpr cold_type cold_for(const Other& told) {
    return made_cold_at_places<Type, Format, MarkKind, cold_type>(told);
  }

  // Where the gathering slots lie, said again.
  //
  // The gatherer holds where they are, not the slots themselves, so a gatherer
  // that has been carried to a new home has to be told the new one.
  constexpr void lives_in(cold_type& cold) { cold_ = &cold; }

  // What the places were told, for the slots this makes as the walk goes.
  constexpr void was_told(const ToldType& told) { told_ = told; }

  // Gathering from the beginning, leaving the slots where they lie.
  //
  // Between one match and the next the gathering starts over while the room
  // the slots lie in stays put. Assigning a fresh gatherer over this one would
  // start the gathering over too, but it would also carry over where that
  // fresh one thinks its slots are, which is nowhere.
  constexpr void begin_again() {
    turns_open_ = 0;
    made_.reset();
    failed_.reset();
    text_ = nullptr;
    auto all = make_slots<Type, Format, MarkKind, ToldType>(told_);
    [&]<std::size_t... which>(std::index_sequence<which...>) {
      // Begun where they stand rather than assigned over: a slot that keeps a
      // resource keeps its own when it is assigned to, and these were made on
      // whatever their places were told about.
      ((begin_gathering_at(slot<which>(), std::move(std::get<which>(all)))),
       ...);
    }(std::make_index_sequence<std::tuple_size_v<plain_folds_type>>{});
    if constexpr (!nothing_at_a_register) {
      // Built where it stands, for the same reason: assigning these would
      // leave every container on whatever resource it was made with.
      std::destroy_at(&states_);
      std::construct_at(
          &states_,
          make_register_states<Type, Format, Automaton, MarkKind, ToldType>(
              told_));
    }
  }

 private:
  // The folds that answer for every reading at once, kept here rather than at
  // a register. Where the machine says what each character lies inside, every
  // reading would be told the same things, so there is one fold to tell and it
  // can live in the walk -- which is what lets it stay in a register of the
  // processor rather than in an array indexed by a number read from memory.

 public:

  // A list takes in the turn that has just ended, and what says it ended is
  // the registers as they stood before this move wrote anything.
  template <std::size_t State, std::size_t Move, class RegistersType>
  constexpr void moving(const RegistersType& registers, auto) {
    constexpr const auto& taken = Automaton.states[State].ranges[Move];
    collect_elements<Type, Format, Automaton, failure_for<Type>>(
        State, registers, states_, taken.commands, taken.command_count,
        std::make_index_sequence<field_count>{}, text_, failed_, told_);
  }

  // Whether anything open on this state's run would rather have it whole.
  //
  // A gathering that appends by the length of what it is given, or a group
  // whose type takes a run in one call, is better handed the run: the walk
  // finds where it ends and hands it over once. Where nothing would -- every
  // open group is told a character at a time whatever happens -- finding the
  // end first and walking the run again to say what is in it is one pass more
  // than the reading needs, and for a run of two or three characters that pass
  // is most of what the run costs.
  // Whether anything at all is open to be handed this state's run.
  //
  // A state can keep a run with nothing gathering inside it: a head walked
  // over, a field whose characters nobody reads. Handing that run over is a
  // call that does nothing, and finding where it ends first is a pass over
  // characters the walk was going to pass over anyway. In words that pass is
  // thirty-two characters to a step and pays for itself; read one character at
  // a time it is the same walk done twice.
  template <std::size_t State>
  [[nodiscard]] static consteval bool anything_takes_the_run() {
    constexpr std::size_t staying = staying_move<Automaton, State>();
    if constexpr (staying == no_move) {
      return false;
    } else {
      return [&]<std::size_t... group>(std::index_sequence<group...>) {
        return (false || ... || in_one_call<State, staying, group>());
      }(std::make_index_sequence<field_count>{});
    }
  }

  // Whether this place, open here, takes a run in one call rather than a
  // character at a time. Room written into with one append does; a fold told
  // what each character was does not, and handing it a run only moves the
  // walking of that run from one place to another.
  template <std::size_t State, std::size_t Move, std::size_t Group>
  [[nodiscard]] static consteval bool in_one_call() {
    constexpr const auto& taken = Automaton.states[State].ranges[Move];
    if constexpr ((taken.groups_open & (std::uint64_t{1} << Group)) == 0) {
      return false;
    } else {
      using held_type = std::remove_cv_t<leaf_kind_of_output<Type, Group>>;
      return scanned_as_range<held_type> || keeps_characters<held_type>;
    }
  }

  template <std::size_t State>
  [[nodiscard]] static consteval bool wants_a_run_whole() {
    constexpr std::size_t staying = staying_move<Automaton, State>();
    if constexpr (staying == no_move) {
      return true;
    } else {
      return [&]<std::size_t... group>(std::index_sequence<group...>) {
        return (false || ... || takes_it_whole<State, staying, group>());
      }(std::make_index_sequence<field_count>{});
    }
  }

  template <std::size_t State, std::size_t Move, std::size_t Group>
  [[nodiscard]] static consteval bool takes_it_whole() {
    using held_type = leaf_kind_of_output<Type, Group>;
    using how = gathering_of<Type, Format, Group>;
    constexpr const auto& taken = Automaton.states[State].ranges[Move];
    if constexpr (scanned_as_range<held_type>) {
      return true;
    } else if constexpr (gathers_in_the_walk<Type, Format, Automaton, Group>()) {
      return (taken.groups_open & (std::uint64_t{1} << Group)) != 0;
    } else if constexpr (how::folds && how::the_place && !how::place_repeats &&
                         every_move_says_the_groups<Automaton>()) {
      using held = std::remove_cv_t<held_type>;
      using state_type = decltype(scan::scanner<held>{}.begin_groups());
      return [&]<std::size_t... which>(std::index_sequence<which...>) {
        return (false || ... || [] {
          constexpr std::uint64_t bit = std::uint64_t{1} << (Group + 1 + which);
          if constexpr ((taken.groups_open & bit) == 0) {
            return false;
          } else {
            return takes_group_runs<held, which, state_type> ||
                   takes_the_group_whole<held, which, state_type>;
          }
        }());
      }(std::make_index_sequence<groups_a_leaf_opens<held>()>{});
    } else {
      return true;
    }
  }

  // Whether exactly one place does the work of this state's run, and does it a
  // character at a time.
  template <std::size_t State, std::size_t Group>
  [[nodiscard]] static consteval bool this_place_reads_the_run() {
    constexpr std::size_t staying = staying_move<Automaton, State>();
    if constexpr (staying == no_move) {
      return false;
    } else if constexpr (takes_it_whole<State, staying, Group>()) {
      return false;
    } else {
      using held_type = leaf_kind_of_output<Type, Group>;
      using how = gathering_of<Type, Format, Group>;
      if constexpr (!(how::folds && how::the_place && !how::place_repeats &&
                      every_move_says_the_groups<Automaton>()) ||
                    scanned_as_range<held_type>) {
        return false;
      } else {
        constexpr const auto& taken = Automaton.states[State].ranges[staying];
        using held = std::remove_cv_t<held_type>;
        using state_type = decltype(scan::scanner<held>{}.begin_groups());
        return [&]<std::size_t... which>(std::index_sequence<which...>) {
          return (false || ... || [] {
            constexpr std::uint64_t bit = std::uint64_t{1} << (Group + 1 + which);
            return (taken.groups_open & bit) != 0 &&
                   takes_group_characters<held, which, state_type>;
          }());
        }(std::make_index_sequence<groups_a_leaf_opens<held>()>{});
      }
    }
  }

  // And whether every other place has nothing to do with it.
  template <std::size_t State, std::size_t Group>
  [[nodiscard]] static consteval bool this_place_is_idle() {
    constexpr std::size_t staying = staying_move<Automaton, State>();
    using how = gathering_of<Type, Format, Group>;
    if constexpr (how::folds && how::inside) {
      // A group inside a fold is its place's business and never its own: it is
      // open whenever the place is standing in it, and counting it as work of
      // its own is counting the same work twice.
      return true;
    } else if constexpr (staying == no_move) {
      return false;
    } else {
      constexpr const auto& taken = Automaton.states[State].ranges[staying];
      using held_type = leaf_kind_of_output<Type, Group>;
      constexpr std::size_t inside =
          groups_a_leaf_opens<std::remove_cv_t<held_type>>();
      constexpr std::uint64_t mine = [] {
        std::uint64_t made = std::uint64_t{1} << Group;
        for (std::size_t which = 0; which < inside; ++which) {
          made |= std::uint64_t{1} << (Group + 1 + which);
        }
        return made;
      }();
      return (taken.groups_open & mine) == 0 &&
             (taken.groups_reopened & mine) == 0;
    }
  }

  // Which place that is, where there is exactly one.
  template <std::size_t State>
  [[nodiscard]] static consteval std::size_t the_place_that_reads_the_run() {
    std::size_t found = no_move;
    std::size_t count = 0;
    [&]<std::size_t... group>(std::index_sequence<group...>) {
      ([&] {
        if constexpr (this_place_reads_the_run<State, group>()) {
          found = group;
          ++count;
        } else if constexpr (!this_place_is_idle<State, group>()) {
          ++count;
          ++count;
        }
      }(), ...);
    }(std::make_index_sequence<field_count>{});
    return count == 1 ? found : no_move;
  }

  // The run this state keeps, read and handed over in one loop -- and the loop
  // is here rather than in the walk.
  //
  // Where the walk owns the loop, what the type gathers into is an object the
  // loop writes through, and a store to it is a store on every character: the
  // optimiser cannot keep it in a register of the processor because it cannot
  // see where the loop will stop. Here it is a local. Taken out once, kept in a
  // register for the whole run, put back when the run ends -- which is what
  // somebody writing this reading by hand would have done without thinking
  // about it.
  template <std::size_t State, staying_class Klass>
    requires(the_place_that_reads_the_run<State>() != no_move)
  [[nodiscard]] SCAN_FORCE_INLINE constexpr const char* took_class(
      const char* from, const char* limit) {
    constexpr std::size_t group = the_place_that_reads_the_run<State>();
    constexpr std::size_t staying = staying_move<Automaton, State>();
    constexpr std::uint64_t now =
        Automaton.states[State].ranges[staying].groups_open;
    using held = std::remove_cv_t<leaf_kind_of_output<Type, group>>;
    auto& fold =
        slot<gathering_slot<Type, Format, group, MarkKind, ToldType>>();
    auto gathered = fold.here.state;
    const char* cursor = from;
    while (cursor != limit &&
           inside_of<Klass>(static_cast<unsigned char>(*cursor))) {
      const char letter = *cursor;
      [&]<std::size_t... which>(std::index_sequence<which...>) {
        ((void)[&] {
          constexpr std::uint64_t bit = std::uint64_t{1} << (group + 1 + which);
          if constexpr ((now & bit) != 0) {
            if constexpr (takes_group_characters<held, which,
                                                 decltype(gathered)>) {
              push_one_group<held, which>(gathered, letter);
            }
          }
        }(), ...);
      }(std::make_index_sequence<groups_a_leaf_opens<held>()>{});
      ++cursor;
    }
    fold.here.state = gathered;
    return cursor;
  }

  // A run the walk stepped over in vectors: the fields that are open take all
  // of it, which is one pass over the piece rather than one call a character.
  template <std::size_t State, class RegistersType>
  constexpr void took_run(const char* from, const char* to,
                          const RegistersType& registers, auto) {
    hand_run<State>(from, to, registers,
                    std::make_index_sequence<field_count>{});
  }

  template <std::size_t State, std::size_t Landed, std::size_t Move,
            class RegistersType>
  SCAN_FORCE_INLINE constexpr void moved(char letter,
                                         const RegistersType& registers,
                                         auto position) {
    // A move that writes nothing leaves the gatherings where they are, and
    // most of the characters of a subject are read by one: inside a field
    // nothing is written, which is what holding the tags back bought. So the
    // whole of what a move does to the gatherings is skipped for it, and what
    // is left is handing the character to the fields that are open.
    constexpr const auto& taken = Automaton.states[State].ranges[Move];
    if constexpr (State != Landed || staying_writes<Automaton, State>()) {
      advance_scanners<Type, Format, Automaton, false, true>(
          letter, Landed, State, position, registers, states_, taken.commands,
          taken.command_count, std::make_index_sequence<field_count>{}, text_,
          told_);
    }
    hand_over<taken.groups_open, taken.groups_reopened,
              open_on_entry<Automaton, State>(),
              taken.target == State && taken.groups_reopened == 0, Landed>(
        letter, registers, position,
        std::make_index_sequence<field_count>{});
    collect_turns_that_ended<Type, Format, Automaton, failure_for<Type>>(
        Landed, registers, states_, std::make_index_sequence<field_count>{},
        failed_);
  }

  // Where the walk ended is a constant, so the reading that accepted is one
  // too, and the value is put together here rather than looked for afterwards.
  // A walk keeps every place it passes, and the last one it keeps is the
  // answer -- so this runs more than once and the last of them wins, which is
  // what it has always done with the value. It has to do the same with a
  // failure: a place passed early where a field was not read yet is not this
  // reading's answer, and holding on to that would lose every match after it.
  template <std::size_t State, class RegistersType>
  constexpr void ended(const RegistersType& registers) {
    constexpr const auto& packed = Automaton.states[State];
    // What the walk kept is told where the walk stands, and then read from
    // where it is.
    //
    // Reading it runs one more step, by the positions, because the end of the
    // input is not a character and whatever is still open has to be closed.
    // Everything else was said by the moves as they were taken, so the fold is
    // set to the positions as they stand: nothing has moved since, and that
    // last step announces nothing twice.
    auto kept = all_slots();
    [&]<std::size_t... group>(std::index_sequence<group...>) {
      ([&] {
        using how = gathering_of<Type, Format, group>;
        if constexpr (how::folds && how::the_place && !how::place_repeats &&
                      every_move_says_the_groups<Automaton>()) {
          auto& one = std::get<gathering_slot<Type, Format, group, MarkKind, ToldType>>(kept);
          using held = std::remove_cv_t<leaf_kind_of_output<Type, group>>;
          constexpr std::size_t inside = groups_a_leaf_opens<held>();
          const auto& reading = packed.readings[packed.accepting_slot];
          for (std::size_t which = 0; which < inside; ++which) {
            one.here.told_at[which] =
                slot_read(registers, reading[(group + 1 + which) * 2]);
            one.here.ended_at[which] =
                slot_read(registers, reading[(group + 1 + which) * 2 + 1]);
          }
        }
      }(), ...);
    }(std::make_index_sequence<field_count>{});
    // Which places the walk kept: a fact about the shape and the machine,
    // worked out once here and carried with the gatherings.
    constexpr std::uint64_t mine = [] {
      std::uint64_t made = 0;
      [&]<std::size_t... group>(std::index_sequence<group...>) {
        ([&] {
          using how = gathering_of<Type, Format, group>;
          if constexpr (gathers_in_the_walk<Type, Format, Automaton, group>() ||
                        list_gathers_in_the_walk<Type, Format, Automaton,
                                                 group>() ||
                        (group > 0 &&
                         list_gathers_in_the_walk<Type, Format, Automaton,
                                                  group == 0 ? 0
                                                             : group - 1>()) ||
                        (how::folds && how::the_place && !how::place_repeats &&
                         every_move_says_the_groups<Automaton>())) {
            made |= std::uint64_t{1} << group;
          }
        }(), ...);
      }(std::make_index_sequence<field_count>{});
      return made;
    }();
    const kept_by_the_walk<decltype(kept), mine> mine_kept{kept, turns_open_};
    // What this costs, so that the next reader does not have to find it
    // again: the bundle handed over holds the walk's register file, and a
    // register file whose address any call has seen is one no compiler will
    // take apart. So the whole of it stays on the stack and is cleared before
    // a character is read -- 632 bytes for this pattern.
    //
    // Folding this one call in does not help: it makes another to finish the
    // parts, and that one is handed the same bundle. Nothing short of the
    // whole chain being written out lets the registers go, and the whole chain
    // is the reading written out again for every place it has.
    // Handed both readings: where the walk held each tag, and where the
    // ending put the ones it closed.
    //
    // A tag the walk closed stands where the reading says, all the way to the
    // end, and everything is read from there. A group still open where the
    // match ends is closed by the commands that end it and by nothing else,
    // and those write registers of their own -- so its closing is not in the
    // reading at all, and was read as a group that never closed. The two are
    // about two different moments, so both are carried and each is asked where
    // it is the one that knows.
    auto got = finish_value<Type, Type, 0, true>(
        by_the_registers<Type, Format>(packed.readings[packed.accepting_slot],
                                       states_, registers, mine_kept,
                                       packed.ending_reading),
        text_, told_);
    if (!got) {
      failed_ = std::move(got).error();
      made_.reset();
      return;
    }
    failed_.reset();
    made_.emplace(std::move(*got));
  }

  // What was read, or what went wrong instead. An element of a list that did
  // not read is kept here too: the walk that met it is not over, and there is
  // nowhere to say so until it is.
  [[nodiscard]] constexpr std::expected<Type, failure_for<Type>> taken() {
    if (failed_) return std::unexpected(std::move(*failed_));
    if (!made_) {
      return std::unexpected(scan::as_a_failure<failure_for<Type>>(
          no_match<>("input does not match scan expression")));
    }
    return std::move(*made_);
  }

  [[nodiscard]] constexpr std::optional<failure_for<Type>>& went_wrong() {
    return failed_;
  }

  [[nodiscard]] constexpr std::optional<Type>& made() { return made_; }

  // Where the subject begins, for a walk that has all of it in front of it. A
  // fold reading such a subject is handed each of its groups whole.
  // Said once, where the subject is handed over, rather than on every
  // character: what a fold points at is the subject, and the subject does not
  // move.
  constexpr void points_at(const char* text) {
    text_ = text;
    [&]<std::size_t... group>(std::index_sequence<group...>) {
      ([&] {
        using how = gathering_of<Type, Format, group>;
        if constexpr (how::folds && how::the_place) {
          slot<gathering_slot<Type, Format, group, MarkKind, ToldType>>().here.text =
              text;
        }
      }(), ...);
    }(std::make_index_sequence<field_count>{});
  }

 private:
  template <std::size_t State, class RegistersType, std::size_t... Group>
  constexpr void hand_run(const char* from, const char* to,
                          const RegistersType& registers,
                          std::index_sequence<Group...>) {
    (hand_run_group<State, Group>(from, to, registers), ...);
  }

  template <std::size_t State, std::size_t Group, class RegistersType>
  constexpr void hand_run_group(const char* from, const char* to,
                                const RegistersType& registers) {
    using held_type = leaf_kind_of_output<Type, Group>;
    using how = gathering_of<Type, Format, Group>;
    if constexpr (scanned_as_range<held_type>) {
      return;
    } else if constexpr (how::folds && how::inside) {
      return;
    } else if constexpr (how::folds && how::the_place && !how::place_repeats &&
                         every_move_says_the_groups<Automaton>()) {
      // A run, handed to the groups it fell in, whole.
      //
      // Nothing is written across a run -- that is what makes it a run -- so
      // what is open at its first character is open at its last, and the move
      // that takes it says which groups those are. There is one question for
      // the whole run and, for a type that takes a run, one call.
      constexpr std::size_t staying = staying_move<Automaton, State>();
      if constexpr (staying != no_move) {
        constexpr std::uint64_t inside_now =
            Automaton.states[State].ranges[staying].groups_open;
        // A run that begins a group again on every character is a run of
        // turns, and a turn is not something to hand over in bulk: what the
        // type is told has to be what happened.
        constexpr std::uint64_t begins_again =
            Automaton.states[State].ranges[staying].groups_reopened;
        using held = std::remove_cv_t<held_type>;
        constexpr std::size_t inside = groups_a_leaf_opens<held>();
        auto& fold = slot<gathering_slot<Type, Format, Group, MarkKind, ToldType>>();
        const std::string_view run(from, static_cast<std::size_t>(to - from));
        [&]<std::size_t... which>(std::index_sequence<which...>) {
          ((void)[&] {
            if constexpr ((inside_now &
                           (std::uint64_t{1} << (Group + 1 + which))) != 0 &&
                          (begins_again &
                           (std::uint64_t{1} << (Group + 1 + which))) == 0) {
              // A group that goes over whole is not also told its
              // characters, and a run is nothing but characters.
              if constexpr (takes_group_characters<
                                held, which,
                                typename std::remove_cvref_t<
                                    decltype(fold.here)>::state_type> &&
                            !takes_the_group_whole<
                                held, which,
                                typename std::remove_cvref_t<
                                    decltype(fold.here)>::state_type>) {
                push_one_group<held, which>(fold.here.state, run);
              }
            }
          }(), ...);
        }(std::make_index_sequence<inside>{});
      }
    } else if constexpr (how::folds && how::the_place) {
      // The characters of a run, each to the group it fell in. The positions
      // stand still across a run, so what opened and what closed is said once,
      // and where the subject can be pointed at nothing is handed over at all.
      using folded =
          fold_of<std::remove_cv_t<held_type>, MarkKind,
                  gathering_of<Type, Format, Group>::place_repeats>;
      if constexpr (every_group_whole<held_type, typename folded::state_type>()) {
        if (from != to) {
          fold_the_readings<Group, gathering_slot<Type, Format, Group, MarkKind, ToldType>,
                            std::remove_cv_t<held_type>, Automaton>(
              State, registers, states_, *from, true, text_);
        }
      } else {
        for (const char* letter = from; letter != to; ++letter) {
          fold_the_readings<Group, gathering_slot<Type, Format, Group, MarkKind, ToldType>,
                            std::remove_cv_t<held_type>, Automaton>(
              State, registers, states_, *letter, true, text_);
        }
      }
    } else if constexpr (gathers_in_the_walk<Type, Format, Automaton, Group>()) {
      // A run is where nothing is written, so what was open at its first
      // character is open at its last: one question for the whole of it.
      constexpr std::uint64_t staying =
          staying_move<Automaton, State>() == no_move
              ? 0
              : Automaton.states[State]
                    .ranges[staying_move<Automaton, State>()]
                    .groups_open;
      if constexpr ((staying & (std::uint64_t{1} << Group)) != 0) {
        gathering_of<Type, Format, Group>::push_run(
            slot<gathering_slot<Type, Format, Group, MarkKind, ToldType>>(), from,
            to);
      }
    } else {
      // The same pairs, for a run handed over whole.
      constexpr auto& pairs =
          gathered_pairs<Automaton, State,
                         tag_of_group<Type, Format, Automaton, Group>()>;
      one_per_register<Automaton.register_count> given;
      for (std::size_t which = 0; which < pairs.count; ++which) {
        const std::uint32_t opening = pairs.open[which];
        const std::uint32_t closing = pairs.shut[which];
        if (given.test(opening) || stood_nowhere(slot_read(registers, opening))) continue;
        if (closed_since_turn(slot_read(registers, closing), slot_read(registers, opening),
                              how::place_repeats)) continue;
        given.set(opening);
        gathering_of<Type, Format, Group>::push_run(
            std::get<gathering_slot<Type, Format, Group, MarkKind, ToldType>>(states_[opening]),
            from, to);
      }
    }
  }

  template <std::uint64_t NowMask, std::uint64_t AgainMask,
            std::uint64_t WasMask, bool StaysPutFlag, std::size_t Landed,
            std::size_t... Group, class RegistersType>
  constexpr void hand_over(char letter, const RegistersType& registers,
                           auto position, std::index_sequence<Group...>) {
    (hand_group<NowMask, AgainMask, WasMask, StaysPutFlag, Landed,
                Group>(letter, registers, position),
     ...);
  }

  template <std::uint64_t NowMask, std::uint64_t AgainMask,
            std::uint64_t WasMask, bool StaysPutFlag, std::size_t Landed,
            std::size_t Group, class RegistersType>
  constexpr void hand_group(char letter, const RegistersType& registers,
                            auto position) {
    using held_type = leaf_kind_of_output<Type, Group>;
    using how = gathering_of<Type, Format, Group>;
    if constexpr (scanned_as_range<held_type>) {
      return;
    } else if constexpr (how::folds && how::inside) {
      return;
    } else if constexpr (how::folds && how::the_place && !how::place_repeats &&
                         every_move_says_the_groups<Automaton>()) {
      // The machine says what happened; nothing is read to find out, and the
      // fold is where the walk keeps it rather than where a register points.
      //
      // Asked of every move and not of this one. Where the fold is written is
      // a fact about the step, but where it is read out again is a fact about
      // the whole machine -- so a machine with one move that cannot say what
      // its character lies inside keeps the whole fold at its registers, and
      // asking step by step here put half of it in the other place, where
      // nobody looked for it.
      auto& fold = slot<gathering_slot<Type, Format, Group, MarkKind, ToldType>>();
      constexpr bool stays_put = StaysPutFlag;
      fold_by_the_step<Group, std::remove_cv_t<held_type>, NowMask,
                       AgainMask, WasMask, !stays_put>(fold, letter, true,
                                                         position);
    } else if constexpr (how::folds && how::the_place) {
      fold_the_readings<Group, gathering_slot<Type, Format, Group, MarkKind, ToldType>,
                        std::remove_cv_t<held_type>, Automaton>(
          Landed, registers, states_, letter, true, text_);
    } else if constexpr (Group > 0 &&
                         list_gathers_in_the_walk<
                             Type, Format, Automaton,
                             Group == 0 ? 0 : Group - 1>()) {
      // A turn of a list, told by the move rather than by the registers.
      //
      // The move says when a turn begins, so the one before it ends there: it
      // is finished and put in the list, and a fresh one is begun. Nothing is
      // read out of a register and nothing is copied between them, which is
      // what a turn boundary used to be made of.
      constexpr std::size_t list_group = Group == 0 ? 0 : Group - 1;
      constexpr std::uint64_t now =
          NowMask;
      constexpr std::uint64_t again =
          AgainMask;
      constexpr std::uint64_t mine = std::uint64_t{1} << Group;
      static constexpr auto spread = spread_of<Type, Format>();
      using element = std::remove_cvref_t<std::ranges::range_value_t<
          std::remove_cv_t<leaf_kind_of_output<Type, list_group>>>>;
      if constexpr ((again & mine) != 0) {
        auto& made =
            slot<gathering_slot<Type, Format, Group, MarkKind, ToldType>>();
        if ((turns_open_ & mine) != 0) {
          auto& list = slot<gathering_slot<Type, Format, list_group, MarkKind, ToldType>>();
          append_to(list, scanner_finish<element>(std::move(made)));
        }
        made = gathering_of<Type, Format, Group>::begin(
            spread.parameters[Group].view(),
            context_at_group<Type, Group>(told_));
        turns_open_ |= mine;
      }
      if constexpr ((now & mine) != 0) {
        gathering_of<Type, Format, Group>::push(
            slot<gathering_slot<Type, Format, Group, MarkKind, ToldType>>(),
            letter);
        turns_open_ |= mine;
      }
    } else if constexpr (gathers_in_the_walk<Type, Format, Automaton, Group>()) {
      // Open where the move says so, and gathered where the walk keeps it.
      constexpr std::uint64_t now =
          NowMask;
      constexpr std::uint64_t again =
          AgainMask;
      if constexpr ((now & (std::uint64_t{1} << Group)) != 0) {
        static constexpr auto spread = spread_of<Type, Format>();
        auto& made = slot<gathering_slot<Type, Format, Group, MarkKind, ToldType>>();
        if constexpr ((again & (std::uint64_t{1} << Group)) != 0) {
          made = gathering_of<Type, Format, Group>::begin(
              spread.parameters[Group].view(),
              context_at_group<Type, Group>(told_));
        }
        gathering_of<Type, Format, Group>::push(made, letter);
      }
    } else {
      static constexpr auto spread = spread_of<Type, Format>();
      // The pairs this state names, worked out while compiling; a register is
      // one gathering, so one of them going on is enough.
      constexpr auto& pairs =
          gathered_pairs<Automaton, Landed,
                         tag_of_group<Type, Format, Automaton, Group>()>;
      one_per_register<Automaton.register_count> given;
      for (std::size_t which = 0; which < pairs.count; ++which) {
        const std::uint32_t opening = pairs.open[which];
        const std::uint32_t closing = pairs.shut[which];
        if (given.test(opening) || stood_nowhere(slot_read(registers, opening))) continue;
        if (closed_since_turn(slot_read(registers, closing), slot_read(registers, opening),
                              how::place_repeats)) continue;
        given.set(opening);
        gathering_of<Type, Format, Group>::push(
            std::get<gathering_slot<Type, Format, Group, MarkKind, ToldType>>(states_[opening]),
            letter);
      }
    }
  }


  // Kept first, and by itself.
  //
  // What the walk touches on every character is here; everything else it holds
  // -- the answer, the failure, where the subject begins, the gatherings that
  // do follow a register -- is touched once a match or once a field. Put first
  // in the object, the hot part shares no cache line with the cold, and an
  // optimiser that will not promote a whole gatherer to registers can still
  // keep this much of it in one.
  // Which walk-kept lists have a turn open. A turn is closed by the move that
  // begins the next one, and the first turn of all has nothing before it to
  // close -- so being open is remembered rather than guessed at.
  std::uint64_t turns_open_ = 0;
  [[no_unique_address]] warm_type warm_{};
  cold_type* cold_ = nullptr;
  // What the places were told, kept for the slots that are made as the
  // walk goes. Empty where nothing was told, so it costs nothing there.
  [[no_unique_address]] ToldType told_{};
  states_type states_;
  std::optional<Type> made_;
  std::optional<failure_for<Type>> failed_;
  const char* text_ = nullptr;
};

// One match off input that arrives in pieces, and where the next one starts.
//
// The head of the reading, in the sense the format reader means: the machine
// takes what it takes and stops, and where it stopped is where the reading
// goes on from -- which for pieces is a place in the piece it is holding, so
// nothing has to be put back.
template <class Type, fixed_string Format, class SourceType>
struct taken_from_pieces {
  std::optional<Type> value;
  bool matched = false;
};

// How much a reading of this format has to carry between one match and the
// next, where the subject arrives in pieces. The same number the stream
// reading asks for: the longest walk out of a match that finds no other one.
template <class Type, fixed_string Format>
inline constexpr std::size_t pieces_hold = [] consteval {
  constexpr std::size_t window =
      walk_past_a_match<streaming_automaton<Type, Format>>();
  if constexpr (window == std::numeric_limits<std::size_t>::max()) {
    return std::size_t{0};
  } else {
    return window * 2 + 1;
  }
}();

template <class Type, fixed_string Format, class SourceType>
[[nodiscard]] constexpr auto take_from_pieces(SourceType& into,
                                              const char*& cursor,
                                              const char*& last,
                                              std::ptrdiff_t& place) {
  constexpr const auto& automaton = streaming_automaton<Type, Format>;
  taken_from_pieces<Type, Format, SourceType> said;
  register_file<std::ptrdiff_t, automaton.register_count> registers{};
  registers.fill(scan::tre::negative_tag);
  execute_initial<automaton>(registers, place);
  // The same note as everywhere else, and here it costs nothing to go back to:
  // the place is an address inside a piece the reading is still holding.
  constexpr bool walks_past = walk_past_a_match<automaton>() != 0;
  using kept_type =
      std::conditional_t<walks_past,
                         register_file<std::ptrdiff_t,
                                       automaton.register_count>,
                         nothing_kept>;
  walk_answer<const char*, kept_type> best;
  constexpr walk_shape shape{.in_words = true, .longest = true};
  into.watch(best.at, best.upto);
  if (!run_continuation<automaton, shape, automaton.initial, shape.budget, 0,
                        std::ptrdiff_t>(cursor, last, place, registers, into,
                                        best)) {
    return said;
  }
  // Where the match ended is where the next reading begins. Which characters
  // those are is the source's business: the piece they were in may have been
  // given up while the walk read past them, and then they were carried.
  into.go_back_to(cursor, last);
  auto got = into.taken();
  if (!got) return said;
  said.value = std::move(*got);
  said.matched = true;
  return said;
}

// A scan of input that arrives in pieces.
//
// The walk reads a piece the way it reads a string -- in words and vectors --
// and asks for the next one where it runs out. Nothing is buffered: what a
// field gathers, it gathers as the characters go by, and a piece is not looked
// at again once the walk has left it.
template <class Type, fixed_string Format,
          class ToldType = scan::default_context_t,
          piecewise_char_range PiecesType>
[[nodiscard]] constexpr std::expected<Type, failure_for<Type>> scan_pieces(
    PiecesType&& pieces, const ToldType& told = ToldType{}) {
  constexpr const auto& automaton = streaming_automaton<Type, Format>;
  register_file<std::ptrdiff_t, automaton.register_count> registers{};
  registers.fill(scan::tre::negative_tag);
  execute_initial<automaton>(registers, std::ptrdiff_t{0});
  auto view = std::views::all(std::forward<PiecesType>(pieces));
  typename field_gatherer<Type, Format, automaton, false, std::ptrdiff_t,
                          ToldType>::cold_type collected =
      made_cold_at_places<
          Type, Format, std::ptrdiff_t,
          typename field_gatherer<Type, Format, automaton, false,
                                  std::ptrdiff_t, ToldType>::cold_type>(told);
  gathers_from_pieces<field_gatherer<Type, Format, automaton, false,
                                     std::ptrdiff_t, ToldType>,
                      decltype(view),
                      pieces_hold<Type, Format>>
      into(field_gatherer<Type, Format, automaton, false, std::ptrdiff_t,
                          ToldType>{collected, told},
           std::move(view));
  // Nothing in hand to begin with, so the first thing the walk does is ask.
  const char* cursor = nullptr;
  const char* last = nullptr;
  std::ptrdiff_t place = 0;
  walk_answer<const char*> best;
  constexpr walk_shape shape{.in_words = true,
                             .budget = bodies_worth_writing<automaton>()};
  if (!run_continuation<automaton, shape, automaton.initial, shape.budget, 0,
                        std::ptrdiff_t>(cursor, last, place, registers, into,
                                        best)) {
    return std::unexpected(scan::as_a_failure<failure_for<Type>>(
        no_match<>("input does not match scan expression")));
  }
  return into.taken();
}

template <class Type, fixed_string Format,
          how_to_walk Walk = how_to_walk::by_length,
          class ToldType = scan::default_context_t,
          std::ranges::input_range RangeType>
[[nodiscard]] constexpr std::expected<Type, failure_for<Type>> scan_stream(
    RangeType&& input, const ToldType& told = ToldType{}) {
  // The whole of the reading, so the walks below a match are kept.
  constexpr const auto& automaton = streaming_automaton<Type, Format, false>;
  // Positions as addresses where the subject lies in a row.
  //
  // An address is the cursor, which the walk is holding anyway; a count is a
  // step of its own on every character, and a cursor that is a base and an
  // index rather than one pointer. Where the subject arrives a character at a
  // time there is nothing to point at and the count is what there is.
  constexpr bool in_a_row = std::ranges::contiguous_range<RangeType>;
  using mark_kind = std::conditional_t<in_a_row, const char*, std::ptrdiff_t>;
  // Written out, the same as every other walk. A subject handed over a
  // character at a time is read by the machine written as code -- what it
  // cannot have is the vectors, because there is nothing in a row to read.
  //
  // Where there is, it can. The characters of a run keep the machine where it
  // stands and write nothing, so the walk steps over the whole run at once and
  // hands it to whoever is gathering as a run -- which is one call for a
  // hundred characters where the type can take one. That wants addresses
  // rather than iterators, which is what a range in a row has.
  // A list is left out of it: its elements are handed over turn by turn and a
  // run stepped over in one go is one turn as far as the walk can tell.
  if constexpr (std::ranges::contiguous_range<RangeType>) {
    // Where the reading holds a list, its elements are turns: the runs cannot
    // be stepped over whole -- a run stepped over in one go is one turn as far
    // as the walk can tell -- and there is nothing to point at through them.
    // Everything else about the two walks is the same, and they were written
    // out twice until one of the two went without the contexts the caller
    // said, which is a thing a second copy of an argument list will do.
    constexpr bool points_at_it = !holds_a_range<Type>();
    const char* cursor = std::ranges::data(input);
    const char* const last = cursor + std::ranges::size(input);
    // Runs stepped over whole, unless the caller asked for a character at a
    // time.
    //
    // Only the one walk the caller will use is written. The other paths write
    // both and pick by the length of the subject, which they can afford
    // because a walk to a terminator is a state and a comparison; a walk that
    // gathers is a body a state, and two of them is twice the code for a
    // question that a subject of any length answers the same way.
    constexpr walk_shape shape =
        points_at_it
            ? walk_shape{
                  .in_words = Walk != how_to_walk::one_at_a_time,
                  .tags_read =
                      groups_whose_place_is_read<Type, Format, automaton>(),
                  .tags_written =
                      groups_whose_mark_is_read<Type, Format, automaton>(),
                  .budget = bodies_worth_writing<automaton>()}
            : walk_shape{.budget = bodies_worth_writing<automaton>()};
    // Nothing the reading fills in is made here.
    //
    // A walk written as labels is a walk no inliner will fold into this one,
    // so anything handed to it by reference is an address a call has seen and
    // must stay in memory until the call returns -- which is every character
    // of the subject. Told to make its own instead, the gatherer and the
    // registers are values of the walk and go wherever values go.
    return run_owning<automaton, shape, automaton.initial, shape.budget, 0,
                      const char*, points_at_it, const char*, const char*,
                      automaton.register_count,
                      field_gatherer<Type, Format, automaton, in_a_row,
                                     mark_kind, ToldType>,
                      walk_answer<const char*>>(
        cursor, last, points_at_it ? cursor : nullptr,
        points_at_it ? cursor : mark_kind{}, {}, told);
  } else {
    register_file<mark_kind, automaton.register_count> registers{};
    if constexpr (in_a_row) {
      registers.fill(nullptr);
      execute_initial<automaton>(registers, static_cast<const char*>(nullptr));
    } else {
      registers.fill(scan::tre::negative_tag);
      execute_initial<automaton>(registers, std::ptrdiff_t{0});
    }
    using gatherer_type =
        field_gatherer<Type, Format, automaton, in_a_row, mark_kind, ToldType>;
    typename gatherer_type::cold_type collected =
        made_cold_at_places<Type, Format, mark_kind,
                            typename gatherer_type::cold_type>(told);
    gatherer_type into{collected, told};
    auto cursor = std::ranges::begin(input);
    mark_kind position = 0;
    constexpr walk_shape shape{.budget = bodies_worth_writing<automaton>()};
    walk_answer<decltype(cursor)> best;
    if (!run_continuation<automaton, shape, automaton.initial, shape.budget, 0,
                          mark_kind>(cursor, std::ranges::end(input), position,
                                     registers, into, best)) {
      return std::unexpected(scan::as_a_failure<failure_for<Type>>(
          no_match<>("input does not match scan expression")));
    }
    return into.taken();
  }
}

// The head of a range that is read once, and the character that ended it.
//
// Nothing is buffered: the characters go through the machine as they come, the
// values are gathered by the scanners of the fields themselves, and the one
// character the machine could not take is handed back with them, because it has
// been read and cannot be put back where it came from.
template <class Type, std::size_t Hold = 0>
struct taken_ahead {
  Type value;
  // The character that ended the match: offered, looked at, and left where it
  // stood. One is enough, because only one is ever looked at.
  std::optional<char> stopped;
  // The characters read after the match on the chance of a longer one, where
  // the walk went past it and then died. Those were taken out of the reading
  // and have to come back somewhere: a reading that goes on holds them for the
  // next match, and a single head hands them here. Room for as many as the
  // machine can read past a match, which is a number it is asked for while
  // this is compiled.
  std::array<char, Hold == 0 ? 1 : Hold> given{};
  std::size_t count = 0;

  // What was given back, as text. A view of what this holds, so it lives as
  // long as this does.
  [[nodiscard]] constexpr std::string_view given_back() const {
    return std::string_view(given.data(), count);
  }
};

// What a reading holds between one match and the next.
//
// A walk can go past a match and die away from one, and then the answer is the
// place it passed -- which means the characters it read after that place have
// to be read again by whoever reads next. There is nowhere to put them back
// on a subject that is gone once it is read, so they are kept here, in front
// of the reading, and taken before anything else.
//
// How many there can be is what the automaton says: the longest walk out of a
// match that finds no other match. Where that is nothing, this is nothing.
template <std::size_t Hold>
struct stream_carry {
  std::array<char, Hold == 0 ? 1 : Hold> held{};
  std::size_t count = 0;
  std::size_t at = 0;

  [[nodiscard]] constexpr bool empty() const { return at == count; }
  [[nodiscard]] constexpr char front() const { return held[at]; }
  constexpr void pop() { ++at; }

  // In front of whatever is still unread here, because they were read first.
  constexpr void put_in_front(const char* from, std::size_t many) {
    if (many == 0) return;
    const std::size_t left = count - at;
    std::array<char, Hold == 0 ? 1 : Hold> made{};
    for (std::size_t index = 0; index < many; ++index) made[index] = from[index];
    for (std::size_t index = 0; index < left; ++index) {
      made[many + index] = held[at + index];
    }
    held = made;
    count = many + left;
    at = 0;
  }
};

// The head of a subject read as it arrives, and where it ended.
//
// The machine is offered the character before it is taken out of the reading,
// so the one that ends a match is never lost -- it is looked at, reported, and
// left where it is. What has to be given back is the other thing: the
// characters read after the last match on the chance of a longer one, where
// the walk went past a match and then died. Those were taken, and they go into
// the carry for the next reading.
//
// A reading that can be gone back over needs neither: the place is an iterator
// and going back is assigning it. That is why a forward range holds nothing at
// all here, and why the only reading that has to name a number is the one that
// cannot be gone back over.
template <class Type, fixed_string Format, class IteratorType,
          class SentinelType, std::size_t Hold>
[[nodiscard]] constexpr std::expected<taken_ahead<Type>, failure_for<Type>>
scan_stream_prefix(IteratorType& first, SentinelType last,
                   stream_carry<Hold>& carry, bool read_on = true) {
  constexpr const auto& automaton = streaming_automaton<Type, Format>;
  constexpr std::size_t window = walk_past_a_match<automaton>();
  constexpr bool can_go_back = std::forward_iterator<IteratorType>;
  static_assert(
      can_go_back || window != std::numeric_limits<std::size_t>::max(),
      "this pattern can read any number of characters past a match without "
      "finding another one, so the reading that has to give them back would "
      "have to hold any number of them: read it from something that can be "
      "gone back over -- a forward range, characters in a row, or input in "
      "pieces");
  stream_state<Type, Format> state;
  std::optional<char> stopped;
  std::optional<stream_state<Type, Format>> note;
  // The place the note was taken at, kept the way this reading can keep it: an
  // iterator where the reading can be gone back over, and nothing at all where
  // it cannot -- an iterator of such a range cannot even be copied.
  using place_type =
      std::conditional_t<can_go_back, IteratorType, nothing_kept>;
  place_type note_at{};
  constexpr std::size_t held_here =
      can_go_back || window == 0 ||
              window == std::numeric_limits<std::size_t>::max()
          ? 1
          : window;
  std::array<char, held_here> since{};
  std::size_t since_count = 0;
  if (state.accepting()) note = state;
  // A character that has been taken but not stepped over yet.
  //
  // Stepping over one reads the next: an iterator of a subject that arrives as
  // it is read does its reading in `++`. So the step is put off until another
  // character is actually wanted, and a match that settles where it stands
  // never causes the one after it to be read at all. What is read is what the
  // machine asked for, and nothing beyond it.
  bool taken_here = false;
  const auto step_over_it = [&] {
    if (!taken_here) return;
    ++first;
    taken_here = false;
  };
  while (true) {
    char symbol = 0;
    if (!carry.empty()) {
      symbol = carry.front();
    } else {
      step_over_it();
      if (first == last) break;
      symbol = static_cast<char>(*first);
    }
    if (!state.offer(symbol)) {
      // Looked at and not taken: it stays where it is, and is said here so
      // that whoever asked knows what ended the match.
      stopped = symbol;
      break;
    }
    if (!carry.empty()) {
      carry.pop();
    } else {
      taken_here = true;
    }
    if constexpr (!can_go_back && window != 0) since[since_count++] = symbol;
    if (state.accepting()) {
      note = state;
      since_count = 0;
      if constexpr (can_go_back) note_at = first;
      // Where the machine can go nowhere from where it stands, it is over, and
      // nothing needs to be read to find that out.
      if (state.settled()) break;
    }
  }
  const auto handed_back =
      [](std::expected<Type, failure_for<Type>> got,
         std::optional<char> ended_it)
      -> std::expected<taken_ahead<Type>, failure_for<Type>> {
    if (!got) return std::unexpected(std::move(got).error());
    return taken_ahead<Type>{std::move(*got), ended_it};
  };
  // Whoever reads on from here needs the reading to stand after what was
  // taken; whoever does not would only make it read one more character.
  if (read_on) step_over_it();
  if (state.accepting()) return handed_back(std::move(state).finish(), stopped);
  if (note) {
    // Past the match and dead. The answer is the place that was kept, and what
    // was read after it goes back in front of the reading.
    if constexpr (can_go_back) {
      first = note_at;
    } else {
      carry.put_in_front(since.data(), since_count);
    }
    // And the one that ended it, where one was looked at: going past a match
    // and dying does not make the character that stopped the walk any less
    // read. Thrown away here, it was the one thing the caller could not get
    // back by any other means.
    return handed_back(std::move(*note).finish(), stopped);
  }
  // Nothing matched; `finish` says so in the way the caller expects.
  return handed_back(std::move(state).finish(), stopped);
}

// How much a reading of this format has to be able to hold: nothing where it
// can be gone back over, and nothing where no walk out of a match ever fails
// to find another. Otherwise the characters read past a match, and the ones
// already held when that happened.
template <class Type, fixed_string Format, class IteratorType>
inline constexpr std::size_t stream_hold = [] consteval {
  if constexpr (std::forward_iterator<IteratorType>) {
    return std::size_t{0};
  } else {
    constexpr std::size_t window =
        walk_past_a_match<streaming_automaton_whole<Type, Format>>();
    if constexpr (window == std::numeric_limits<std::size_t>::max()) {
      // Refused where it is used; sized so that saying so is what the caller
      // sees, rather than an array of every address there is.
      return std::size_t{0};
    } else {
      return window * 2;
    }
  }
}();

template <class Type, fixed_string Format, class IteratorType>
using stream_carry_for = stream_carry<stream_hold<Type, Format, IteratorType>>;

template <class Type, fixed_string Format, std::ranges::input_range RangeType>
[[nodiscard]] constexpr auto scan_stream_prefix(RangeType&& input) {
  auto first = std::ranges::begin(input);
  // One head and no reading after it, so what the walk read past the match has
  // nowhere to go: the carry below goes out of scope with this call. A reading
  // that goes on -- one record after another -- holds it between matches and
  // reads them again. A single head has no next reading, so they are handed to
  // the caller instead, and nothing is eaten.
  constexpr std::size_t hold = stream_hold<Type, Format, decltype(first)>;
  stream_carry<hold> carry;
  auto got = scan_stream_prefix<Type, Format>(first, std::ranges::end(input),
                                              carry, false);
  using answer = taken_ahead<Type, hold>;
  if (!got) {
    return std::expected<answer, failure_for<Type>>(
        std::unexpected(std::move(got).error()));
  }
  answer made{std::move(got->value), got->stopped, {}, 0};
  while (!carry.empty()) {
    made.given[made.count++] = carry.front();
    carry.pop();
  }
  return std::expected<answer, failure_for<Type>>(std::move(made));
}



}  // namespace scan::detail

// The helper that reads a shape, which is ordinary code and says so.
//
// It is here rather than higher up because the reading it does is the reading
// everything else does -- one builder, one gathering, one fold -- and a
// consumer that writes its own says the same things through the same hooks.
// Nothing below this line knows that a type has fields; this is where that
// knowledge lives.
 namespace scan {




template <fixed_string Format>
struct aggregate_scanner {
  // The shape read out of groups that this format made, for a type that was
  // named rather than inherited from.
  //
  // A caller writing `scan<"{},{}">.of<point>()` says the format at the call
  // and the type at the call, and `point` may have no scanner at all. What
  // reads it is this, asked for both: the library below hands over the groups
  // and asks nothing about what a point is made of.
  template <class Type, class GivenType = scan::nothing_given>
  [[nodiscard]] static constexpr auto read(
      std::span<const std::string_view> groups,
      const GivenType& given = GivenType{})
      -> std::expected<Type, detail::failure_for<Type>> {
    return detail::build_value<detail::failure_for<Type>,
                               detail::format_parameters<Type, Format>, Type, 0,
                               true, scan::hands_a_failure_back>(groups, given);
  }

  // The same, where the caller asked for the value itself: what went wrong is
  // thrown at the asking, which is the only place anything is thrown.
  template <class Type, class GivenType = scan::nothing_given>
  [[nodiscard]] static constexpr Type read_or_throw(
      std::span<const std::string_view> groups,
      const GivenType& given = GivenType{}) {
    return detail::build_value<detail::failure_for<Type>,
                               detail::format_parameters<Type, Format>, Type, 0,
                               true, scan::throws_a_failure>(groups, given);
  }

  // Nothing here is a member the library reads and reacts to. What this class
  // does, it does through the hooks any scanner may write: the pattern its
  // places make, the building from the groups that pattern opens, and the
  // telling of those groups as they arrive. The format is a parameter of this
  // class and is spoken by nobody else -- the library below knows patterns,
  // groups and hooks, and has never heard of a format.

  [[nodiscard]] constexpr auto pattern(this const auto& self) {
    using type = scanner_target_t<decltype(self)>;
    // What this shape matches, with its places as groups and in the order the
    // format has them -- the very order the reading below counts on, because
    // both come out of the one walk over the format.
    return detail::places_pattern<type, Format>();
  }

  // Whether this shape is read from its own groups, said outright.
  //
  // Asked as a question and not found out by whether the hook below is there:
  // what that hook hands back is the list of everything reading this shape can
  // fail with, and working that list out means knowing how the shape is read,
  // which is what the question decides.
  //
  // A shape made only of places can be handed its groups when the match is
  // over. One with a list in it is made of turns, and the positions a match
  // leaves behind hold the last turn and nothing before it -- so it has to be
  // told its groups as they happen, which every place of it has to be able to
  // take. Where neither is true, the shape keeps the road that spreads its
  // places into the automaton around it.
  [[nodiscard]] constexpr bool reads_its_groups(this const auto& self) {
    using type = scanner_target_t<decltype(self)>;
    static_cast<void>(self);
    return true;
  }

  // The shape, out of the groups its pattern opened.
  //
  // Its groups are its places, so what builds it from them is what builds it
  // from the places of the reading around it: one builder, and this is a call
  // to it.
  template <class SelfType>
    requires(!detail::says_a_list_inside<scanner_target_t<SelfType>>())
  [[nodiscard]] constexpr auto from_groups(
      this const SelfType& self, std::span<const std::string_view> groups)
      -> std::expected<scanner_target_t<SelfType>,
                       detail::shape_failure<scanner_target_t<SelfType>>> {
    using type = scanner_target_t<SelfType>;
    static_cast<void>(self);
    return detail::build_value<detail::shape_failure<type>,
                               detail::format_parameters<type, Format>, type, 0,
                               true>(groups);
  }

  // The same, told what the place this shape stands at was told. A shape
  // standing inside another shape is read by the same builder, so what reaches
  // it here reaches its places the way it reaches everything else.
  template <class SelfType, class ToldType>
    requires(!detail::says_a_list_inside<scanner_target_t<SelfType>>())
  [[nodiscard]] constexpr auto from_groups(
      this const SelfType& self, std::span<const std::string_view> groups,
      const ToldType& told)
      -> std::expected<scanner_target_t<SelfType>,
                       detail::shape_failure<scanner_target_t<SelfType>>> {
    using type = scanner_target_t<SelfType>;
    static_cast<void>(self);
    return detail::build_value<detail::shape_failure<type>,
                               detail::format_parameters<type, Format>, type, 0,
                               true>(groups, told);
  }

  // Told its groups as they happen.
  //
  // Every shape that can be says this, not only one made of turns: a subject
  // that is read once has nothing to point at, and a shape standing inside a
  // shape that is being told its groups has to be told its own. Where the
  // groups can be handed over instead, they are -- that is the faster of the
  // two and the one that copies nothing.
  //
  // Every place gathers into the
  // reader of the value it stands for, a turn ends where the list's own group
  // closes, and the element is put together there and added -- by the same
  // builder that puts together everything else, asked for the gatherings a
  // different way.
  template <class SelfType, class Told>
    requires(detail::turns_can_be_folded<scanner_target_t<SelfType>, Format>())
  [[nodiscard]] constexpr auto begin_groups(this const SelfType& self,
                                            const Told& given) {
    static_cast<void>(self);
    return detail::shape_turns<scanner_target_t<SelfType>, Format, Told>(
        given);
  }

  template <class SelfType>
    requires(detail::turns_can_be_folded<scanner_target_t<SelfType>, Format>())
  [[nodiscard]] constexpr auto begin_groups(this const SelfType& self) {
    static_cast<void>(self);
    return detail::shape_turns<scanner_target_t<SelfType>, Format>{};
  }

  template <class SelfType, std::size_t Place, class StateType>
    requires(detail::turns_can_be_folded<scanner_target_t<SelfType>, Format>())
  constexpr void push_group(this const SelfType& self, StateType& state,
                            scan::group_at<Place>, char letter) {
    static_cast<void>(self);
    detail::push_shape_place<scanner_target_t<SelfType>, Format, Place>(
        state, letter);
  }

  template <class SelfType, std::size_t Place, class StateType>
    requires(detail::turns_can_be_folded<scanner_target_t<SelfType>, Format>())
  constexpr void opened_group(this const SelfType& self, StateType& state,
                              scan::group_at<Place>) {
    static_cast<void>(self);
    detail::open_shape_place<scanner_target_t<SelfType>, Format, Place>(state);
  }

  template <class SelfType, std::size_t Place, class StateType>
    requires(detail::turns_can_be_folded<scanner_target_t<SelfType>, Format>())
  constexpr void closed_group(this const SelfType& self, StateType& state,
                              scan::group_at<Place>) {
    using type = scanner_target_t<SelfType>;
    static_cast<void>(self);
    detail::close_shape_place<type, Format, Place,
                              detail::shape_failure<type>>(state,
                                                           state.went_wrong);
  }

  template <class SelfType, class StateType>
    requires(detail::turns_can_be_folded<scanner_target_t<SelfType>, Format>())
  [[nodiscard]] constexpr auto finish_groups(this const SelfType& self,
                                                 StateType state) {
    using type = scanner_target_t<SelfType>;
    static_cast<void>(self);
    using failure_type = detail::shape_failure<type>;
    if (state.went_wrong) {
      return std::expected<type, failure_type>(
          std::unexpected(std::move(*state.went_wrong)));
    }
    // Told what the place this shape stands at was told. The state kept it
    // from the moment it was begun -- a shape standing inside another shape is
    // told at the door, the same as one standing on its own -- and its places
    // read with it, which is the whole of what a context said at a place
    // means.
    if constexpr (requires { state.told; }) {
      return detail::finish_value<type, type, 0, true, failure_type>(
          detail::gathered_by_a_fold<StateType>{state}, nullptr, state.told);
    } else {
      return detail::finish_value<type, type, 0, true, failure_type>(
          detail::gathered_by_a_fold<StateType>{state}, nullptr);
    }
  }

  // Gathered a character at a time, for whoever holds the characters and not
  // the subject.
  //
  // Said as a question rather than as a refusal inside it: whether a type
  // gathers this way is asked by things that are deciding how to read it, and
  // an answer that stops the compiler is not an answer. A shape made by the
  // call it named says no -- its places stand for that call's arguments, and
  // there is nothing to hand a half-read one to.
  //
  // The list of what can go wrong is built from this shape's parts and never
  // from the shape: what it says it hands back is that very list, and a list
  // that asked the shape would be asking its own answer.
  template <class SelfType>
    requires(!requires { &scanner<scanner_target_t<SelfType>>::parse; })
  [[nodiscard]] constexpr auto begin(this const SelfType& self) {
    using type = scanner_target_t<SelfType>;
    static_cast<void>(self);
    return detail::stream_state<type, Format, false,
                                detail::shape_failure<type>>{};
  }

  constexpr void push(this const auto&, auto& state, char value) {
    state.push(value);
  }

  // Handed back rather than thrown, both of them: this type is read by the
  // same machine everything else is, and that machine says what went wrong
  // instead of throwing it. Which means a shape used as a field of another
  // shape carries its kinds up into what that reading can fail with.
  [[nodiscard]] constexpr auto finish(this const auto& self, auto state) {
    static_cast<void>(self);
    return std::move(state).finish();
  }

  [[nodiscard]] constexpr auto try_parse(this const auto& self,
                                         std::string_view input) {
    auto state = self.begin();
    for (char value : input) { self.push(state, value); }
    return self.finish(std::move(state));
  }
};

}  // namespace scan

 namespace scan::detail {

#undef SCAN_FORCE_INLINE

}  // namespace scan::detail


 namespace scan {

// How many groups a type's own pattern opens.
//
// Said out loud because a type built from its groups may want to hand some of
// them on: a shape whose field is another shape has that field's groups inside
// its own, and it can only pass them along if it knows how many there are.
// What is counted is the pattern the type declares, which is the pattern the
// match was made with.
template <class Type>
[[nodiscard]] consteval std::size_t groups_in() {
  return detail::groups_a_leaf_opens<std::remove_cv_t<Type>>();
}

}  // namespace scan


// A module keeps its macros to itself and a header does not, so they are
// taken back here rather than handed to whoever includes this.
#undef SCAN_FORCE_INLINE
