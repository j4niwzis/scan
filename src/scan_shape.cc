// The shape layer: places, fields, gatherings, and the putting together of a
// value out of what a walk found.
//
// Above the walk and knowing nothing it does not ask for. What is below reads
// a pattern and says where its groups were; what is here decides what those
// groups mean to a type -- which of them is which place, what gathers each
// one, and how the value is made when the reading is over. The walk takes
// whoever is gathering as a parameter and never looks inside it, which is what
// lets this be a module of its own rather than a half of that one.
export module scan.shape;

import std;
import scan.tre;
import boost.pfr;
export import scan.compiler;
export import scan.runtime;

export namespace scan {

// What a shape is made of, asked of the type rather than assumed of it.
//
// Three questions and nothing else: how many parts, what the part at an index
// is, and how to reach it in a value. Everything in this library that puts a
// shape together asks them here, and nowhere else does it look at a type's
// members at all.
//
// The default answer is what an aggregate says about itself, read with
// Boost.PFR. A type that is not an aggregate -- one with invariants to keep,
// or members nobody outside may touch, or an order of its own that has nothing
// to do with the order it was written in -- answers them itself:
//
//   template <> struct scan::fields<my_type> {
//     static constexpr std::size_t count = 2;
//     template <std::size_t index> using at = …;
//     template <std::size_t index> static constexpr auto& of(my_type&);
//   };
template <class type>
struct fields {
  static constexpr std::size_t count = boost::pfr::tuple_size_v<type>;

  template <std::size_t index>
  using at = std::remove_cvref_t<boost::pfr::tuple_element_t<index, type>>;

  template <std::size_t index>
  [[nodiscard]] static constexpr auto& of(type& value) {
    return boost::pfr::get<index>(value);
  }

  template <std::size_t index>
  [[nodiscard]] static constexpr const auto& of(const type& value) {
    return boost::pfr::get<index>(value);
  }
};

}  // namespace scan

export namespace scan::detail {

template <class type, std::size_t... index>
[[nodiscard]] constexpr auto default_patterns(std::index_sequence<index...>) {
  static_assert(
      (requires {
        scanner_pattern<typename scan::fields<type>::template at<index>>();
      } && ...),
      "scan::scanner<type> must provide pattern");
  return std::array<std::string_view, sizeof...(index)>{
      scanner_pattern<typename scan::fields<type>::template at<index>>()...};
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

// A type that says outright that it reads its own groups.
//
// Asked as a plain question, and not by whether a hook is there. What such a
// hook hands back is a list of everything the reading can fail with, and
// working that list out means asking how this type is read -- which is what is
// being decided here. A member that is a plain bool has no such circle in it,
// and saying it is the whole of what a shape has to do to be one.
template <class type>
concept says_it_reads_its_groups = requires {
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
} && !says_it_is_a_list<type>;

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
concept scanned_from_values = says_it_reads_its_groups<type> && requires {
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
    !scanned_as_leaf<type> &&
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
  static constexpr std::size_t count = scan::fields<type>::count;
  template <std::size_t index>
  using at = typename scan::fields<type>::template at<index>;
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
      scan::fields<std::remove_cv_t<type>>::count;
  template <std::size_t index>
  using at =
      typename scan::fields<std::remove_cv_t<type>>::template at<index>;
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
  } else if constexpr (says_it_reads_its_groups<type>) {
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
  if constexpr (!says_it_reads_its_groups<type>) {
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
  if constexpr (scanned_as_leaf<type> ||
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

// The same, handed a run of characters rather than one.
//
// A walk that steps over a run in vectors has the whole of it at once, and a
// type that says it can take a run is handed it that way: `count += run.size()`
// instead of a call a character. A type that says nothing of the sort is handed
// the characters one at a time, which is what it asked for.
template <class type, std::size_t which, class state_type>
constexpr void push_one_group(state_type& state, std::string_view run) {
  using scanner_type = scan::scanner<std::remove_cv_t<type>>;
  if constexpr (requires {
                  scanner_type{}.push_group(state, scan::group_at<which>{},
                                            run);
                }) {
    scanner_type{}.push_group(state, scan::group_at<which>{}, run);
  } else if constexpr (requires {
                         scanner_type{}.push_group(state, which, run);
                       }) {
    scanner_type{}.push_group(state, which, run);
  } else {
    for (const char letter : run) push_one_group<type, which>(state, letter);
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
  // Asked of the hooks and not of the plain answer: this is a count and not a
  // decision. A type with `from_groups` and nothing else said opens the groups
  // its pattern opens, and counting them is what tells the reading around it
  // where its own places begin.
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
          bool = scanned_as_leaf<subject> ||
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
             : (scanned_as_leaf<subject> ||
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
    using element = std::remove_cvref_t<std::ranges::range_value_t<kind>>;
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
    made.text.append(repetition.empty() ? std::string_view("+") : repetition);
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
      make_pattern.template operator()<
          typename scan::fields<type>::template at<index>>(
          parameters[index])...};
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
// What a reading does when it goes wrong, chosen by whoever asked for it.
//
// Asked for a value, there is nowhere to put a failure but a throw, and the
// throw happens where the failure is -- so nothing along the way holds one, and
// the value is written straight into the value it belongs to. Asked to try,
// every step hands its failure back, and the caller gets it as the kind it is.
//
// One builder either way. The difference is these two words and the type a
// step comes back as.
struct hands_a_failure_back {
  template <class type, class failure_type>
  using result = std::expected<type, failure_type>;

  template <class type, class failure_type, class error_type>
  [[nodiscard]] static constexpr result<type, failure_type> went_wrong(
      error_type&& said) {
    return std::unexpected(
        scan::as_a_failure<failure_type>(std::forward<error_type>(said)));
  }

  template <class step_type>
  [[nodiscard]] static constexpr bool read(const step_type& step) {
    return step.has_value();
  }

  template <class step_type>
  [[nodiscard]] static constexpr decltype(auto) value(step_type&& step) {
    return *std::forward<step_type>(step);
  }

  template <class step_type>
  [[nodiscard]] static constexpr decltype(auto) failure(step_type&& step) {
    return std::forward<step_type>(step).error();
  }
};

struct throws_a_failure {
  template <class type, class failure_type>
  using result = type;

  template <class type, class failure_type, class error_type>
  [[noreturn]] static constexpr type went_wrong(error_type&& said) {
    scan::throw_what_went_wrong(std::forward<error_type>(said));
  }

  template <class step_type>
  [[nodiscard]] static constexpr bool read(const step_type&) {
    return true;
  }

  template <class step_type>
  [[nodiscard]] static constexpr decltype(auto) value(step_type&& step) {
    return std::forward<step_type>(step);
  }

  template <class step_type>
  [[nodiscard]] static constexpr scan::scan_error failure(step_type&&) {
    return scan::scan_error("a reading that throws has nothing to hand back");
  }
};



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
  constexpr std::size_t field_count = scan::fields<type>::count;
  constexpr auto parameters = field_parameters<format, field_count>();
  constexpr auto pattern_storage = parameterized_patterns<type>(
      parameters, std::make_index_sequence<field_count>{});
  const auto defaults = pattern_views(pattern_storage);
  pattern_buffer<> output;
  append_aggregate_pattern<type, format, opening>(output, defaults);
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
template <class type, fixed_string format>
inline constexpr auto spread_text = [] {
  constexpr auto made = places_pattern<type, format>();
  fixed_string<made.length + 1> text{};
  for (std::size_t at = 0; at < made.length; ++at) {
    text.value[at] = made.storage[at];
  }
  text.anchored = format.anchored;
  text.space_before_places = format.space_before_places;
  return text;
}();

template <class type, fixed_string format, bool cut = true>
inline constexpr auto& packed_automaton =
    packed_text_automaton<spread_text<type, format>, true, cut>;

// The same, for the machine that gathers as it reads: its registers are not
// allocated, because a gathering follows the register its tag is in and
// allocation would put two tags in one place.
template <class type, fixed_string format, bool cut = true>
inline constexpr auto& streaming_automaton =
    packed_text_automaton<spread_text<type, format>, false, cut>;

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
template <class type, fixed_string format, bool absent_is_empty = false>
[[nodiscard]] constexpr auto taken_prefix_fields(std::string_view input) {
  constexpr const auto& automaton = packed_automaton<type, format>;
  constexpr std::size_t group_count = automaton.tag_count / 2;
  struct answer {
    std::string_view head;
    std::array<std::string_view, group_count> groups{};
    bool matched = false;
  };
  answer said;
  const char* const begin = input.data();
  std::array<const char*, automaton.register_count> registers{};
  constexpr auto written_everywhere = tags_always_written<automaton>();
  [&]<std::size_t... tag>(std::index_sequence<tag...>) {
    ((written_everywhere[tag] ? void() : void(registers[tag] = nullptr)), ...);
  }(std::make_index_sequence<automaton.tag_count>{});
  execute_commands(automaton.initialize, automaton.initialize.size(), registers,
                   begin);

  gathers_nothing nothing;
  // Where the machine can read past a match and die away from one, the
  // registers of the head are not the registers it died holding, so the note
  // keeps them. Where it cannot, the note is a pointer and nothing else.
  constexpr bool walks_past = walk_past_a_match<automaton>() != 0;
  using kept_type =
      std::conditional_t<walks_past,
                         std::array<const char*, automaton.register_count>,
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
        const char* const from = said_by[group * 2];
        const char* const to = said_by[group * 2 + 1];
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

template <class type, fixed_string format>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::string_view taken_prefix_or_none(
    std::string_view input) {
  const char* const begin = input.data();
  const char* best = nullptr;
  if constexpr (automata_at_runtime) {
    best = run_prefix_runtime(runtime_text_automaton<spread_text<type, format>>(), begin,
                              begin + input.size());
  } else {
    constexpr const auto& automaton = packed_automaton<type, format>;
    std::array<const char*, automaton.register_count> registers{};
    best = run_head<automaton, automaton.initial>(begin, begin + input.size(),
                                                  registers);
  }
  if (best == nullptr) return {};
  return std::string_view(begin, static_cast<std::size_t>(best - begin));
}

// Whether the pattern is happy with nothing at all. Reading one match after
// another, such a pattern never moves and the reading never ends.
template <auto& automaton>
[[nodiscard]] consteval bool matches_nothing() {
  return automaton.states[automaton.initial].accepting_slot !=
         packed_state<0, 0, 0>::not_accepting;
}

template <class type, fixed_string format, int sentinel, bool terminated,
          bool absent_is_empty, how_to_walk walk, class ending,
          std::size_t... index>
[[nodiscard]] [[gnu::flatten]] SCAN_FORCE_INLINE constexpr auto scan_fields(
    std::string_view input, std::index_sequence<index...>) ->
    typename ending::template result<std::array<std::string_view,
                                                sizeof...(index)>,
                                     scan::failure> {
  // A group that took no part points nowhere, and that is how it is said all
  // the way through here: the three walks below each write it, and one place
  // at the end decides whether it is a failure or the ordinary state of
  // affairs. Nothing throws, because nothing here would be caught.
  using groups_type = std::array<std::string_view, sizeof...(index)>;
  using answer_type = typename ending::template result<groups_type,
                                                       scan::failure>;
  const auto answer = [](groups_type made) -> answer_type {
    if constexpr (!absent_is_empty) {
      for (const std::string_view one : made) {
        if (one.data() == nullptr) {
          return ending::template went_wrong<groups_type, scan::failure>(
              no_group("capture group did not participate in the match"));
        }
      }
    }
    return made;
  };
  if consteval {
    const auto matched = scan::tre::simulate(build_text_tnfa<spread_text<type, format>>(), input);
    if (!matched.matched) {
      return ending::template went_wrong<groups_type, scan::failure>(
          no_match("input does not match scan expression"));
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
    return answer(std::array{capture.template operator()<index>()...});
  } else {
    // A compound statement, because that is what `if consteval` is written
    // with: the branch that is not the constant-evaluated one is a block, and
    // the choice of machine is made inside it.
    if constexpr (automata_at_runtime) {
      // Nothing here is a constant: the automaton is a value built on first use
      // and the walk is a loop over it. Not one instantiation per state, not one
      // determinisation per pattern while compiling.
      const scan::tre::tdfa& automaton = runtime_text_automaton<spread_text<type, format>>();
      std::vector<const char*> registers(automaton.register_count, nullptr);
      if (!run_tagged_runtime(automaton, input.data(),
                              input.data() + input.size(), registers)) {
        return ending::template went_wrong<groups_type, scan::failure>(
            no_match("input does not match scan expression"));
      }
      const auto capture = [&]<std::size_t capture_index>() -> std::string_view {
        const char* const begin = registers[capture_index * 2];
        const char* const end = registers[capture_index * 2 + 1];
        // A group that took no part is an error where every group was meant to
        // take part, and the ordinary state of affairs where the format has
        // branches and only one of them ran. Which it is, is decided once, at
        // the end.
        if (begin == nullptr || end == nullptr) return std::string_view{};
        return std::string_view(begin, static_cast<std::size_t>(end - begin));
      };
      return answer(std::array{capture.template operator()<index>()...});
    } else {
      // Anchored to both ends of the subject, so the walks below a match are
      // kept: one of them may be the only walk that reaches the end, and the
      // answer is the first still accepting when it does.
      constexpr const auto& automaton = packed_automaton<type, format, false>;
      std::array<const char*, automaton.register_count> registers{};
      // Only the slots that can still be unwritten when the machine accepts are
      // given the value that says a field took no part. Everything past the tags
      // is a working register, never read before it is written -- there is no
      // initialisation left at all -- and a tag written on every path does not
      // need telling either. For a pattern whose fields all take part, which is
      // most of them, there is nothing here to do.
      constexpr auto written_everywhere = tags_always_written<automaton>();
      [&]<std::size_t... tag>(std::index_sequence<tag...>) {
        ((written_everywhere[tag]
              ? void()
              : void(registers[tag] = nullptr)),
         ...);
      }(std::make_index_sequence<automaton.tag_count>{});
      execute_commands(automaton.initialize, automaton.initialize.size(),
                       registers, input.data());
      // The generated form, not an interpreter.
      //
      // This walked the automaton one character at a time: a search through the
      // ranges of the current state, then a loop over that transition's
      // commands, both through pointers, and a state index carried in a
      // variable. The same automaton unrolled into comparisons against
      // constants is what the captureless path has always used, and it is the
      // difference between reading a table and running code.
      const char* cursor = input.data();
      bool matched = false;
      // Which terminator, if any.
      //
      // Asked for outright it is whatever the caller named, and it must be one
      // the pattern rejects in every state, which is required here. Not asked
      // for, it is still taken when the type of the input promises a null
      // character past its last -- a `std::string` always does -- and when the
      // pattern happens to reject that character. Where it does not, the loop
      // that tests the end of the input runs, and the caller never has to know
      // the question was asked.
      //
      // Not `sentinel != 0`, which is what this used to ask. Zero is the
      // terminator of every `std::string`, and so the one worth asking for; it
      // is also what a defaulted template parameter of a character type is,
      // which meant that asking for it politely was the same as not asking. The
      // absence is its own value now.
      constexpr unsigned char terminator =
          sentinel >= 0 ? static_cast<unsigned char>(sentinel) : 0;
      constexpr bool by_terminator =
          sentinel >= 0 ||
          (terminated && is_safe_tagged_sentinel<automaton, terminator>());
      if constexpr (by_terminator) {
        static_assert(is_safe_tagged_sentinel<automaton, terminator>(),
                      "the terminator must be rejected in every state");
        // Two machines, and the subject picks one. A field of five characters
        // is read faster one at a time than by a loop that first asks whether
        // a whole word will fit; a field of two hundred is read four times
        // faster in words. Asking once, here, costs one comparison for the
        // match -- asking inside would cost one for every state it passes
        // through.
        //
        // Where the caller said which walk they want, nothing is asked: the
        // length is not looked at, and only the walk they named is written.
        constexpr std::size_t worth_a_word =
            worth_reading_in_words<automaton>();
        constexpr bool asks = walk == how_to_walk::by_length;
        if (asks ? input.size() < worth_a_word
                 : walk == how_to_walk::one_at_a_time) {
          [[clang::always_inline]] matched =
              run_to_terminator<automaton, terminator, false,
                                automaton.initial>(
                  cursor, cursor + input.size(), registers);
    } else {
          [[clang::always_inline]] matched =
              run_to_terminator<automaton, terminator, true,
                                automaton.initial>(
                  cursor, cursor + input.size(), registers);
        }
    } else {
        const char* const end = cursor + input.size();
        constexpr std::size_t worth_a_word =
            worth_reading_in_words<automaton>();
        constexpr bool asks = walk == how_to_walk::by_length;
        if (asks ? input.size() < worth_a_word
                 : walk == how_to_walk::one_at_a_time) {
          [[clang::always_inline]] matched =
              run_from_here<automaton, false, automaton.initial>(
                  cursor, end, registers);
    } else {
          [[clang::always_inline]] matched =
              run_from_here<automaton, true, automaton.initial>(
                  cursor, end, registers);
        }
      }
      if (!matched) {
        return ending::template went_wrong<groups_type, scan::failure>(
            no_match("input does not match scan expression"));
      }
      // Two of the three tests this used to make were asking whether the machine
      // had done something it cannot do. A position is written as the cursor
      // stands somewhere inside the subject, so it is never past the end; the
      // opening slot of a group is written before its closing one, so the length
      // is never negative. Only the third question is real, and only for a group
      // that some path can reach the end without entering -- which the automaton
      // is asked about while it is being compiled.
      constexpr auto always_written = tags_always_written<automaton>();
      const auto capture = [&]<std::size_t capture_index>() -> std::string_view {
        const auto begin = registers[capture_index * 2];
        const auto end = registers[capture_index * 2 + 1];
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
      // taken no part, and the walk over them at the end is a walk over a
      // question already answered.
      constexpr bool any_can_be_absent =
          !(true && ... && (always_written[index * 2] &&
                            always_written[index * 2 + 1]));
      if constexpr (!any_can_be_absent) {
        return std::array{capture.template operator()<index>()...};
      } else {
        return answer(std::array{capture.template operator()<index>()...});
      }
    }
  }
}

template <class type, fixed_string format, int sentinel = -1,
          bool terminated = false,
          how_to_walk walk = how_to_walk::by_length,
          class ending = hands_a_failure_back>
[[nodiscard]] SCAN_FORCE_INLINE constexpr auto scan_fields(
    std::string_view input) {
  return scan_fields<type, format, sentinel, terminated, false, walk, ending>(
      input, std::make_index_sequence<groups_of_output<type>()>{});
}

// Every group of every branch, with the ones that took no part left empty.
//
// The count comes from the automaton and not from the output type: a format
// with branches has a group for each branch on top of the ones written down,
// and that is how the scan says which branch the input took.
template <class type, fixed_string format, int sentinel = -1,
          bool terminated = false,
          how_to_walk walk = how_to_walk::by_length,
          class ending = hands_a_failure_back>
[[nodiscard]] SCAN_FORCE_INLINE constexpr auto scan_branch_fields(
    std::string_view input) {
  return scan_fields<type, format, sentinel, terminated, true, walk, ending>(
      input, std::make_index_sequence<groups_of_output<type>()>{});
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
          std::size_t offset, bool as_output = false,
          class ending = hands_a_failure_back>
[[nodiscard]] constexpr typename ending::template result<type, failure_type>
build_value(std::span<const std::string_view> groups) {
  // Where this is the whole of what is being read, a shape that reads its own
  // groups is a product of places rather than a value in a place.
  constexpr bool a_value = scanned_as_leaf<type> && !as_output;
  if constexpr (never_fails<type, as_output>() &&
                !std::same_as<ending, throws_a_failure>) {
    // Nothing here can hand a failure back, so nothing here holds one -- even
    // where the caller asked to be handed one.
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
        return ending::template went_wrong<type, failure_type>(
            std::move(got).error());
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
        return ending::template went_wrong<type, failure_type>(
            std::move(got).error());
      } else {
        return scan::scanner<held>{}.finish_groups(std::move(state));
      }
    }
  } else if constexpr (a_value) {
    auto got = parse_value<std::remove_cv_t<type>, failure_type>(
        groups[offset], parameters::at(offset));
    if (got) return std::move(*got);
    return ending::template went_wrong<type, failure_type>(
        std::move(got).error());
  } else if constexpr (scanned_as_variant<type>) {
    // Exactly one branch ran, and its mark says so: a mark that took part
    // points into the subject, and the others point nowhere.
    using answer = typename ending::template result<type, failure_type>;
    return [&]<std::size_t... branch>(std::index_sequence<branch...>) -> answer {
      std::optional<answer> made;
      const auto take = [&]<std::size_t which>() {
        constexpr std::size_t mark = offset + groups_before_branch<type, which>();
        if (made || groups[mark].data() == nullptr) return;
        using alternative = branch_at<type, which>;
        auto part = build_value<failure_type, parameters, alternative, mark + 1,
                                false, ending>(groups);
        if (!ending::read(part)) {
          made = ending::template went_wrong<type, failure_type>(
              ending::failure(std::move(part)));
          return;
        }
        made = scan::branches<std::remove_cv_t<type>>::template make<which>(
            ending::value(std::move(part)));
      };
      (take.template operator()<branch>(), ...);
      if (!made) {
        return ending::template went_wrong<type, failure_type>(
            no_match("no branch of the format took the input"));
      }
      return std::move(*made);
    }(std::make_index_sequence<branch_count<type>()>{});
  } else if constexpr (scanned_from_values<type>) {
    // Made by the call it named, out of the values its places stood for. Each
    // of them is read first and the call is made after, because a value that
    // did not read is not an argument.
    using answer = typename ending::template result<type, failure_type>;
    return [&]<std::size_t... index>(std::index_sequence<index...>) -> answer {
      // Asked for a value, every step is the value it read and the call is
      // written out of them where they stand. Asked to try, each is held until
      // they are all in hand, because a call cannot be half made.
      if constexpr (std::same_as<ending, throws_a_failure>) {
        return scan::scanner<std::remove_cv_t<type>>{}.parse(
            build_value<failure_type, parameters,
                        typename parts_of<type>::template at<index>,
                        offset + groups_before_field<type, index>(), false,
                        ending>(groups)...);
      } else {
        auto parts = std::tuple{build_value<
            failure_type, parameters, typename parts_of<type>::template at<index>,
            offset + groups_before_field<type, index>(), false, ending>(
            groups)...};
        if (auto went_wrong = what_went_wrong<failure_type>(parts)) {
          return std::unexpected(std::move(*went_wrong));
        }
        return scan::scanner<std::remove_cv_t<type>>{}.parse(
            std::move(*std::get<index>(parts))...);
      }
    }(std::make_index_sequence<parts_of<type>::count>{});
  } else {
    using answer = typename ending::template result<type, failure_type>;
    return [&]<std::size_t... index>(std::index_sequence<index...>) -> answer {
      // The same two ways: written straight into the value, or held until they
      // are all in hand.
      if constexpr (std::same_as<ending, throws_a_failure>) {
        return type{build_value<failure_type, parameters,
                                typename parts_of<type>::template at<index>,
                                offset + groups_before_field<type, index>(),
                                false, ending>(groups)...};
      } else {
        auto parts = std::tuple{build_value<
            failure_type, parameters, typename parts_of<type>::template at<index>,
            offset + groups_before_field<type, index>(), false, ending>(
            groups)...};
        if (auto went_wrong = what_went_wrong<failure_type>(parts)) {
          return std::unexpected(std::move(*went_wrong));
        }
        return type{std::move(*std::get<index>(parts))...};
      }
    }(std::make_index_sequence<parts_of<type>::count>{});
  }
}

template <class type, std::size_t index>
using field_type = typename scan::fields<type>::template at<index>;

// Nothing is gathered at the place a leaf that reads its own groups stands on:
// what it is built from are its groups, and they are gathered each at its own.
struct no_gathering {};

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
template <class held, class mark_type = std::ptrdiff_t>
struct fold_turn {
  using held_type = std::remove_cv_t<held>;
  static constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
  using state_type = decltype(scan::scanner<held_type>{}.begin_groups());

  state_type state = scan::scanner<held_type>{}.begin_groups();
  // Where the walk stood, said the way the walk says it: a count where the
  // subject arrives a character at a time, an address where it lies in a row.
  // There the address is the cursor, which the walk is holding anyway, and a
  // count would be a step of its own on every character.
  std::array<mark_type, inside> told_at{};
  // And which closing it has been told about, for the same reason: a group
  // that is taken over and over writes its closing into the same register
  // every turn, so what says a turn has ended is that the position moved --
  // not that it stands anywhere in particular.
  std::array<mark_type, inside> ended_at{};
  // Which groups are open, a bit each. A byte each was an array to index on
  // every character, and which groups those are is known while this is
  // compiled -- so the whole of it is one word and a mask.
  std::uint64_t open = 0;
  // Whether this turn has been told anything at all. A place that has not been
  // stood on yet is not a turn that ended, and a list does not begin with one.
  bool started = false;
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
  [[nodiscard]] static constexpr mark_type nowhere() {
    if constexpr (std::is_pointer_v<mark_type>) {
      return nullptr;
    } else {
      return mark_type{-1};
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
    return stood_on(text, began, ended);
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

template <class held, class mark_type = std::ptrdiff_t, bool repeats = true>
struct fold_of {
  using held_type = std::remove_cv_t<held>;
  static constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
  using state_type = typename fold_turn<held, mark_type>::state_type;

  fold_turn<held, mark_type> here;
  [[no_unique_address]]
  std::conditional_t<repeats, fold_turn<held, mark_type>, no_turn> going;
  [[no_unique_address]] std::conditional_t<repeats, bool, no_turn> has_going{};
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

template <fold_phase phase = fold_phase::whole, std::size_t place, class held,
          class reading_type, class fold_type, class registers_type>
constexpr void fold_one_step(
    fold_type& fold, const reading_type& reading,
    const registers_type& registers, char symbol, bool hands_the_character) {
  using held_type = std::remove_cv_t<held>;
  constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
  const auto opening_of = [&](std::size_t which) {
    return registers[reading[(place + 1 + which) * 2]];
  };
  const auto closing_of = [&](std::size_t which) {
    return registers[reading[(place + 1 + which) * 2 + 1]];
  };
  [&]<std::size_t... step>(std::index_sequence<step...>) {
    ((void)[&] {
      constexpr std::size_t which = inside - 1 - step;
      if ((fold.open & (std::uint64_t{1} << which)) == 0) return;
      const auto began = fold.told_at[which];
      const auto ended = closing_of(which);
      if (stood_nowhere(ended) || ended < began) return;
      if (ended == fold.ended_at[which]) return;
      if constexpr (takes_the_group_whole<held_type, which,
                                          typename fold_type::state_type>) {
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
                                              typename fold_type::state_type>) {
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
  if constexpr (phase == fold_phase::closings_only) return;
  [&]<std::size_t... which>(std::index_sequence<which...>) {
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
    [&]<std::size_t... which>(std::index_sequence<which...>) {
      ((void)[&] {
        if constexpr (takes_group_characters<held_type, which,
                                             typename fold_type::state_type> &&
                      !takes_the_group_whole<
                          held_type, which,
                          typename fold_type::state_type>) {
          if ((fold.open & (std::uint64_t{1} << which)) == 0) return;
          push_one_group<held_type, which>(fold.state, symbol);
        } else if constexpr (takes_group_characters<
                                 held_type, which,
                                 typename fold_type::state_type>) {
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
template <class held, class state_type>
[[nodiscard]] consteval bool every_group_whole() {
  using held_type = std::remove_cv_t<held>;
  return []<std::size_t... which>(std::index_sequence<which...>) {
    return (true && ... &&
            takes_the_group_whole<held_type, which, state_type>);
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
template <auto& automaton>
[[nodiscard]] consteval bool every_move_says_the_groups() {
  for (std::size_t state = 0; state < automaton.states.size(); ++state) {
    const auto& here = automaton.states[state];
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

template <auto& automaton, std::size_t state>
[[nodiscard]] consteval std::size_t staying_move() {
  const auto& here = automaton.states[state];
  for (std::size_t move = 0; move < here.range_count; ++move) {
    if (here.ranges[move].target == state) return move;
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
template <auto& automaton, std::size_t from, std::size_t move>
[[nodiscard]] consteval bool step_says_the_groups() {
  // Asked of the whole machine and not of this move alone: a fold that is kept
  // in the walk has to be kept there for the whole of it, and what puts it
  // there is that no move anywhere needs a reading followed.
  return every_move_says_the_groups<automaton>();
}

// The one register a fold stands at, where the state holds one reading.
template <auto& automaton, std::size_t state, std::size_t place>
inline constexpr std::uint32_t only_fold_register =
    automaton.states[state].readings[0][place * 2];

// Whether a type wants to hear where a group of its own begins and ends.
//
// A type that only takes the characters does not: it is told which group each
// character fell in and that is the whole of what it asked for. Keeping track
// of what is open for such a group is bookkeeping nobody reads -- and on a
// group that begins again on every character, as `(X|Y)*` does, it is that
// bookkeeping on every character.
template <class held, std::size_t which, class state_type>
[[nodiscard]] consteval bool takes_the_group_edges() {
  using scanner_type = scan::scanner<std::remove_cv_t<held>>;
  return requires(state_type& state) {
    scanner_type{}.opened_group(state, scan::group_at<which>{});
  } || requires(state_type& state) {
    scanner_type{}.opened_group(state, which);
  } || requires(state_type& state) {
    scanner_type{}.closed_group(state, scan::group_at<which>{});
  } || requires(state_type& state) {
    scanner_type{}.closed_group(state, which);
  } || takes_the_group_whole<std::remove_cv_t<held>, which, state_type>;
}

// One step of a fold, told by the shape of the machine rather than by the
// positions it wrote.
template <std::size_t place, class held, auto& automaton, std::size_t from,
          std::size_t move, bool edges_can_move = true, class fold_type>
constexpr void fold_by_the_step(fold_type& fold, char symbol,
                                bool hands_the_character) {
  using held_type = std::remove_cv_t<held>;
  constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
  constexpr std::uint64_t now = automaton.states[from].ranges[move].groups_open;
  // A group this move begins again is one whose turn has ended, however the
  // masks stand: a place taken over and over is open on both sides of it.
  constexpr std::uint64_t again =
      automaton.states[from].ranges[move].groups_reopened;
  // The groups of this place are the ones just past it: place 0 is the whole
  // and its groups follow it, which is how the readings are laid out too.
  constexpr auto holds = [](std::uint64_t mask, std::size_t which) {
    return (mask & (std::uint64_t{1} << (place + 1 + which))) != 0;
  };
  // Closed innermost outwards, then opened outermost inwards, then the
  // character to whatever the step arrived inside -- and where a move stays
  // where it is and begins nothing again, none of that can have changed since
  // the move that arrived here, so the character is all there is to do.
  if constexpr (edges_can_move) {
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
                        held_type, which, typename fold_type::state_type>() &&
                    (!holds(now, which) || holds(again, which))) {
        if (((fold.here.open >> which) & 1) != 0) {
          close_one_group<held_type, which>(fold.here.state);
          fold.here.open &= ~(std::uint64_t{1} << which);
        }
      }
    }(), ...);
  }(std::make_index_sequence<inside>{});
  [&]<std::size_t... which>(std::index_sequence<which...>) {
    ((void)[&] {
      if constexpr (takes_the_group_edges<
                        held_type, which, typename fold_type::state_type>() &&
                    holds(now, which)) {
        if ((fold.here.open & (std::uint64_t{1} << which)) == 0) {
          open_one_group<held_type, which>(fold.here.state);
          fold.here.open |= std::uint64_t{1} << which;
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
                                             typename fold_type::state_type>) {
          push_one_group<held_type, which>(fold.here.state, symbol);
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
template <std::size_t place, std::size_t slot, class held, auto& automaton,
          class states_type, class registers_type>
constexpr void fold_the_readings(
    std::size_t state, const registers_type& registers, states_type& states,
    char symbol, bool hands_the_character, const char* text) {
  const auto& entered = automaton.states[state];
  std::array<bool, automaton.register_count> told{};
  for (std::size_t reading = 0; reading < entered.reading_count; ++reading) {
    const std::uint32_t at = entered.readings[reading][place * 2];
    // Nowhere is said as a negative count or as no address at all, and the
    // walk says it whichever way it says positions.
    if (told[at] || stood_nowhere(registers[at])) continue;
    told[at] = true;
    auto& folding = std::get<slot>(states[at]);
    // Said every step rather than once, because a fold is made where its place
    // opens and carried where a reading divides, and neither of those knows
    // what the walk is reading.
    folding.here.text = text;
    fold_one_step<fold_phase::whole, place, held>(
        folding.here, entered.readings[reading], registers, symbol,
        hands_the_character);
    if constexpr (requires { folding.has_going = true; }) {
      if (folding.has_going) {
        folding.going.text = text;
        fold_one_step<fold_phase::closings_only, place, held>(
            folding.going, entered.readings[reading], registers, symbol, false);
      }
    }
  }
}

// Whether the place a group stands for is taken over and over, which is what
// an element of a list is and what nothing else is.
template <class type, std::size_t group>
[[nodiscard]] consteval bool a_place_that_repeats() {
  if constexpr (group == 0) {
    return false;
  } else {
    return scanned_as_range<leaf_kind_of_output<type, group - 1>>;
  }
}

// How one group is gathered, made once and asked at every place that gathers.
//
// A leaf that is built from the groups its own pattern opens is not handed the
// text it stands on, so its place gathers nothing and each of its groups
// gathers characters. Every other group is gathered by the reader of the type
// it holds, which is what it was before any of this.
template <class type, fixed_string format, std::size_t group,
          class mark_type = std::ptrdiff_t>
struct gathering_of {
  using held_type = leaf_kind_of_output<type, group>;
  static constexpr bool by_groups = gathers_by_its_groups<held_type>;
  // Whether this place is stood on over and over, which an element of a list
  // is and nothing else is. Asked through a function rather than written as an
  // expression: `group > 0 && …<group - 1>` still names the type at group - 1,
  // and at group zero that is an index of every bit set.
  static constexpr bool place_repeats = a_place_that_repeats<type, group>();
  static constexpr bool folds = folds_by_turns<std::remove_cv_t<held_type>>;
  static constexpr bool the_place = by_groups && leaf_offset_of_output<type, group> == 0;
  static constexpr bool inside = by_groups && leaf_offset_of_output<type, group> != 0;

  [[nodiscard]] static constexpr auto begin(std::string_view parameters) {
    if constexpr (the_place && folds) {
      // The type's own state, and the walk's note of what it has been told.
      static_cast<void>(parameters);
      return fold_of<std::remove_cv_t<held_type>, mark_type,
                     place_repeats>{};
    } else if constexpr (inside && folds) {
      // Nothing: the characters and the edges of this group go to the fold,
      // which is kept at the place the group is inside of.
      static_cast<void>(parameters);
      return no_gathering{};
    } else if constexpr (the_place || inside) {
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

  template <class state_type>
  static constexpr void push(state_type& state, char letter) {
    if constexpr (the_place || inside) {
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
  template <class state_type>
  static constexpr void push_run(state_type& state, const char* from,
                                 const char* to) {
    if constexpr (the_place || inside) {
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
template <class held, class state_type>
[[nodiscard]] consteval bool any_group_taken_whole() {
  return []<std::size_t... which>(std::index_sequence<which...>) {
    return (false || ... || takes_the_group_whole<held, which, state_type>);
  }(std::make_index_sequence<groups_a_leaf_opens<std::remove_cv_t<held>>()>{});
}

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
template <class type, fixed_string format, auto& automaton>
[[nodiscard]] consteval std::uint64_t groups_whose_place_is_read() {
  std::uint64_t made = 0;
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    ([&] {
      made |= std::uint64_t{1} << group;
      using how = gathering_of<type, format, group>;
      using held = std::remove_cv_t<leaf_kind_of_output<type, group>>;
      constexpr std::size_t inside = groups_a_leaf_opens<held>();
      // Asked in steps, because only a fold has groups to be told about and
      // only a fold has a state to be told into.
      constexpr bool a_fold_of_its_own =
          how::folds && how::the_place && !how::place_repeats &&
          every_move_says_the_groups<automaton>();
      constexpr bool told_by_the_moves = [] {
        if constexpr (a_fold_of_its_own) {
          using state_type = decltype(scan::scanner<held>{}.begin_groups());
          return !any_group_taken_whole<held, state_type>();
        } else {
          return false;
        }
      }();
      if constexpr (!told_by_the_moves) {
        for (std::size_t which = 0; which < inside; ++which) {
          made |= std::uint64_t{1} << (group + 1 + which);
        }
      }
    }(), ...);
  }(std::make_index_sequence<groups_of_output<type>()>{});
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
template <class type, fixed_string format>
[[nodiscard]] consteval bool a_fold_the_walk_can_keep() {
  if constexpr (holds_a_range<type>()) {
    return false;
  } else if constexpr (!holds_a_fold<type>()) {
    return false;
  } else {
    return every_move_says_the_groups<packed_automaton<type, format>>();
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
template <class type, fixed_string format, auto& automaton, std::size_t group>
[[nodiscard]] consteval bool gathers_in_the_walk() {
  using how = gathering_of<type, format, group>;
  // A list is left out twice over: it grows turn by turn, and which turn a
  // gathering belongs to is what the registers keep straight -- so it stays
  // where they are, and so does anything standing at a place that repeats.
  return every_move_says_the_groups<automaton>() && !how::place_repeats &&
         !scanned_as_range<typename how::held_type>;
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
template <class type, fixed_string format, std::size_t... group>
[[nodiscard]] constexpr auto make_scanner_state(
    std::index_sequence<group...>) {
  static constexpr auto spread = spread_of<type, format>();
  // A group that stands for a list gathers the list itself, which needs no
  // reader: what goes into it are whole elements, put there as each one ends.
  const auto one = []<std::size_t which>() {
    using held_type = leaf_kind_of_output<type, which>;
    if constexpr (scanned_as_range<held_type>) {
      return held_type{};
    } else {
      return gathering_of<type, format, which>::begin(
          spread.parameters[which].view());
    }
  };
  return std::tuple{one.template operator()<group>()...};
}

template <class type, fixed_string format>
[[nodiscard]] constexpr auto make_scanner_state() {
  return make_scanner_state<type, format>(
      std::make_index_sequence<groups_of_output<type>()>{});
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
template <class... kinds>
struct gathering_kinds {
  using as_a_tuple = std::tuple<kinds...>;
};

template <class list, class kind>
struct with_kind;

template <class... kinds, class kind>
struct with_kind<gathering_kinds<kinds...>, kind> {
  using result = std::conditional_t<(std::is_same_v<kind, kinds> || ...),
                                    gathering_kinds<kinds...>,
                                    gathering_kinds<kinds..., kind>>;
};

template <class list, class kind>
struct where_kind;

template <class kind>
struct where_kind<gathering_kinds<>, kind> {
  static constexpr std::size_t at = 0;
};

template <class first, class... rest, class kind>
struct where_kind<gathering_kinds<first, rest...>, kind> {
  static constexpr std::size_t at =
      std::is_same_v<first, kind>
          ? 0
          : 1 + where_kind<gathering_kinds<rest...>, kind>::at;
};

// What one group is gathered in, asked without asking for the others.
template <class type, fixed_string format, std::size_t group,
          class mark_type = std::ptrdiff_t,
          bool a_list = scanned_as_range<leaf_kind_of_output<type, group>>>
struct gathering_state {
  using result = decltype(gathering_of<type, format, group, mark_type>::begin(
      std::string_view{}));
};

template <class type, fixed_string format, std::size_t group, class mark_type>
struct gathering_state<type, format, group, mark_type, true> {
  using result = std::remove_cv_t<leaf_kind_of_output<type, group>>;
};

template <class type, fixed_string format, class mark_type, class list,
          std::size_t group, std::size_t count>
struct kinds_from {
  using result = typename kinds_from<
      type, format, mark_type,
      typename with_kind<
          list, typename gathering_state<type, format, group,
                                         mark_type>::result>::result,
      group + 1, count>::result;
};

template <class type, fixed_string format, class mark_type, class list,
          std::size_t count>
struct kinds_from<type, format, mark_type, list, count, count> {
  using result = list;
};

template <class type, fixed_string format, class mark_type = std::ptrdiff_t>
using gathering_kinds_of =
    typename kinds_from<type, format, mark_type, gathering_kinds<>, 0,
                        groups_of_output<type>()>::result;

// What one register holds.
template <class type, fixed_string format, class mark_type = std::ptrdiff_t>
using register_state =
    typename gathering_kinds_of<type, format, mark_type>::as_a_tuple;

// Which slot of it a group is gathered in.
template <class type, fixed_string format, std::size_t group,
          class mark_type = std::ptrdiff_t>
inline constexpr std::size_t gathering_slot =
    where_kind<gathering_kinds_of<type, format, mark_type>,
               typename gathering_state<type, format, group,
                                        mark_type>::result>::at;

// One gathering of every kind, each begun as the first group of that kind
// would begin it. Where two groups of a kind ask for different parameters, the
// one that is not first is begun again when its group opens, which is where
// every group but one begins in any case.
template <class type, fixed_string format, class mark_type = std::ptrdiff_t>
[[nodiscard]] constexpr auto make_slots() {
  register_state<type, format, mark_type> made{};
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    // Backwards, so that the first group of a kind is the one that is left.
    const auto one = [&]<std::size_t which>() {
      using held_type = leaf_kind_of_output<type, which>;
      if constexpr (scanned_as_range<held_type>) {
        std::get<gathering_slot<type, format, which, mark_type>>(made) =
            std::remove_cv_t<held_type>{};
      } else {
        static constexpr auto spread = spread_of<type, format>();
        std::get<gathering_slot<type, format, which, mark_type>>(made) =
            gathering_of<type, format, which, mark_type>::begin(
                spread.parameters[which].view());
      }
    };
    (one.template operator()<groups_of_output<type>() - 1 - group>(), ...);
  }(std::make_index_sequence<groups_of_output<type>()>{});
  return made;
}

// Every register, begun.
//
// One gathering of each kind everywhere, and then the groups that are open
// from the very first character begun where their reading says they are kept:
// those never meet the command that begins a group, because they were opened
// before there was a character to move on.
template <class type, fixed_string format, auto& automaton,
          class mark_type = std::ptrdiff_t>
[[nodiscard]] constexpr auto make_register_states() {
  std::array<register_state<type, format, mark_type>, automaton.register_count>
      states{};
  std::ranges::fill(states, make_slots<type, format, mark_type>());
  const auto& initial = automaton.states[automaton.initial];
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    ([&] {
      using held_type = leaf_kind_of_output<type, group>;
      for (std::size_t reading = 0; reading < initial.reading_count;
           ++reading) {
        const std::uint32_t at = initial.readings[reading][group * 2];
        if (at >= automaton.register_count) continue;
        if constexpr (scanned_as_range<held_type>) {
          std::get<gathering_slot<type, format, group, mark_type>>(states[at]) =
              std::remove_cv_t<held_type>{};
        } else {
          static constexpr auto spread = spread_of<type, format>();
          std::get<gathering_slot<type, format, group, mark_type>>(states[at]) =
              gathering_of<type, format, group, mark_type>::begin(
                  spread.parameters[group].view());
        }
      }
    }(), ...);
  }(std::make_index_sequence<groups_of_output<type>()>{});
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
template <class states_type, std::size_t command_capacity>
struct kept_gatherings {
  using held_type = typename states_type::value_type;
  std::array<std::size_t, command_capacity> which{};
  // Room for as many as a transition could ask for, and a gathering made in
  // none of them until one is asked for. A transition that copies two of them
  // used to make one for every command it could have had, and throw the rest
  // away unread -- a dozen strings a character where a field ends.
  std::array<std::optional<held_type>, command_capacity> held{};
  std::size_t count = 0;

  [[nodiscard]] constexpr const held_type& operator[](
      std::size_t source) const {
    for (std::size_t at = 0; at < count; ++at) {
      if (which[at] == source) return *held[at];
    }
    return *held[0];
  }
};

template <class states_type, std::size_t command_count>
[[nodiscard]] constexpr auto keep_gatherings(
    const states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count) {
  kept_gatherings<states_type, command_count> kept;
  for (std::size_t index = 0; index < count; ++index) {
    if (commands[index].source == packed_command::no_source) continue;
    if (commands[index].value != -2) continue;
    bool already = false;
    for (std::size_t at = 0; at < kept.count; ++at) {
      if (kept.which[at] == commands[index].source) already = true;
    }
    if (already) continue;
    kept.which[kept.count] = commands[index].source;
    kept.held[kept.count].emplace(states[commands[index].source]);
    ++kept.count;
  }
  return kept;
}

template <std::size_t group, class type, fixed_string format, auto& automaton,
          bool hands_the_character = true, bool kept_in_the_walk = false,
          class states_type, class kept_type, class registers_type,
          std::size_t command_count>
constexpr void advance_scanner(
    char symbol, std::size_t state, auto position,
    const registers_type& registers,
    const kept_type& old_states, states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count, const char* text) {
  static constexpr auto spread = spread_of<type, format>();
  constexpr std::size_t opening = group * 2;
  constexpr std::size_t closing = group * 2 + 1;
  using held_type = leaf_kind_of_output<type, group>;
  constexpr bool gathers_a_list = scanned_as_range<held_type>;
  using how = gathering_of<type, format, group>;
  // A gathering the walk keeps for itself is not at a register, so none of
  // what follows is about it: nothing to begin where a group opens, nothing to
  // copy where a reading divides, nothing to hand from one register to
  // another. That is most of what a move used to cost.
  //
  // Asked of whoever is calling, because only one of them keeps gatherings
  // that way: a walk over characters in a row does, and a reader taking a
  // stream a character at a time keeps everything at its registers.
  if constexpr (kept_in_the_walk &&
                (gathers_in_the_walk<type, format, automaton, group>() ||
                 (how::folds && how::the_place && !how::place_repeats &&
                  every_move_says_the_groups<automaton>()))) {
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
  std::apply(
      [&](const auto&... command) {
        ([&] {
          if (command_index++ >= count) return;
          const std::uint32_t tag = automaton.register_tag[command.destination];
          if (tag == opening) {
            if (command.source != packed_command::no_source &&
                command.value == -2) {
              // A reading that divides carries its gathering with it.
              std::get<gathering_slot<type, format, group>>(
                  states[command.destination]) =
                  std::get<gathering_slot<type, format, group>>(
                      old_states[command.source]);
            } else if constexpr (gathers_a_list) {
              std::get<gathering_slot<type, format, group>>(
                  states[command.destination]) = held_type{};
            } else if constexpr (how::folds && how::the_place &&
                                 how::place_repeats) {
              // A turn ending and the next one beginning. The one that is
              // ending has not been told what closed it -- that arrives on
              // this very step, a moment from now -- so it is moved aside
              // rather than thrown away, and the element is made from it once
              // it has heard the rest.
              auto& fold = std::get<gathering_slot<type, format, group>>(
                  states[command.destination]);
              if (fold.here.started) {
                fold.going = std::move(fold.here);
                fold.has_going = true;
              }
              fold.here = {};
            } else {
              std::get<gathering_slot<type, format, group>>(
                  states[command.destination]) =
                  gathering_of<type, format, group>::begin(
                      spread.parameters[group].view());
            }
          } else if (tag == closing && registers[command.destination] != position &&
                     command.source != packed_command::no_source &&
                     command.value == -2) {
            // A closing already written, only being carried along, keeps what
            // it holds.
            std::get<gathering_slot<type, format, group>>(
                states[command.destination]) =
                std::get<gathering_slot<type, format, group>>(
                    old_states[command.source]);
          }
        }(),
         ...);
      },
      commands);
  if constexpr (how::folds && how::the_place &&
                (how::place_repeats || !every_move_says_the_groups<automaton>())) {
    // Everything that happened inside this place on this character, told in
    // order -- and told now, before the copy below, or a fold that ends where
    // its place ends would be copied one closing short.
    //
    // Only where the machine cannot say it. Where it can, the walk tells the
    // fold once, from the move, and telling it again here would say every
    // opening and every character twice: what stops that in this telling is
    // comparing positions, and the other telling has no positions to compare.
    fold_the_readings<group, gathering_slot<type, format, group>,
                      std::remove_cv_t<held_type>, automaton>(
        state, registers, states, symbol, hands_the_character, text);
  }
  if constexpr (!gathers_a_list) {
    // The groups that close on this step. What the field gathered is in the
    // opening it was being added to, whichever register that has become.
    command_index = 0;
    std::apply(
        [&](const auto&... command) {
          ([&] {
            if (command_index++ >= count) return;
            if (automaton.register_tag[command.destination] != closing) return;
            if (registers[command.destination] != position) return;
            const auto& entered = automaton.states[state];
            for (std::size_t reading = 0; reading < entered.reading_count;
                 ++reading) {
              if (entered.readings[reading][closing] != command.destination) {
                continue;
              }
              std::get<gathering_slot<type, format, group>>(
                  states[command.destination]) =
                  std::get<gathering_slot<type, format, group>>(
                      states[entered.readings[reading][opening]]);
              break;
            }
          }(),
           ...);
        },
        commands);
  }
  // A list gathers elements, not characters. Written as an early return this
  // would discard nothing: what follows an `if constexpr` is not the branch it
  // did not take.
  //
  // Handing the character over is skipped where the caller knows the state and
  // does it by the numbers: this walks every reading of the state and keeps a
  // flag per register to do it once, which on a subject read a character at a
  // time is most of what reading it costs.
  if constexpr (!gathers_a_list && hands_the_character) {
    // Once each, however many readings share it: a register is one gathering.
    std::array<bool, automaton.register_count> filled{};
    const auto& packed = automaton.states[state];
    for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
      const std::uint32_t open = packed.readings[reading][opening];
      const std::uint32_t close = packed.readings[reading][closing];
      if (filled[open]) continue;
      if (stood_nowhere(registers[open]) || registers[close] >= registers[open]) continue;
      filled[open] = true;
      gathering_of<type, format, group>::push(
          std::get<gathering_slot<type, format, group>>(states[open]), symbol);
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
template <class slots_type, std::uint64_t places>
struct kept_by_the_walk {
  static constexpr std::uint64_t which_places = places;
  const slots_type& slots;
};

template <class type, fixed_string format, class reading_type,
          class states_type, class registers_type,
          class kept_type = nothing_kept_here>
struct gathered_by_the_registers {
  const reading_type& reading;
  const states_type& states;
  const registers_type& registers;
  // What the walk kept for itself, where it kept anything: a gathering that
  // does not follow a reading is not at a register, and this is where it is.
  const kept_type& kept;

  // A field still being read when the input ended is where it was being
  // gathered; one that ended earlier is the copy taken when it closed, which
  // the readings that went on adding to the opening cannot have changed.
  template <std::size_t place>
  [[nodiscard]] constexpr const auto& gathering() const {
    if constexpr (kept_here<place>()) {
      return std::get<gathering_slot<type, format, place>>(kept.slots);
    } else {
      const std::uint32_t open = reading[place * 2];
      const std::uint32_t close = reading[place * 2 + 1];
      const bool still_reading = registers[close] < registers[open];
      return std::get<gathering_slot<type, format, place>>(
          states[still_reading ? open : close]);
    }
  }

  // Whether this place's gathering is one the walk kept. Asked of the slot
  // rather than of the machine, because this is read from where the value is
  // made and the machine is not in hand there.
  template <std::size_t place>
  [[nodiscard]] static consteval bool kept_here() {
    if constexpr (std::same_as<kept_type, nothing_kept_here>) {
      return false;
    } else {
      return (kept_type::which_places & (std::uint64_t{1} << place)) != 0;
    }
  }

  // A list is gathered and read at its opening throughout: its elements go on
  // being added to the same list however the readings divide.
  template <std::size_t place>
  [[nodiscard]] constexpr const auto& list() const {
    return std::get<gathering_slot<type, format, place>>(
        states[reading[place * 2]]);
  }

  template <std::size_t place>
  [[nodiscard]] constexpr bool took_part() const {
    return !stood_nowhere(registers[reading[place * 2]]);
  }

  // What a place stood on, where the subject can be pointed at. Nothing where
  // the place took no part.
  //
  // A position here is how many characters have been read and not the index of
  // one, so what a place stood on begins one before where its opening says.
  template <std::size_t place>
  [[nodiscard]] constexpr std::string_view span(const char* text) const {
    const auto began = registers[reading[place * 2]];
    const auto ended = registers[reading[place * 2 + 1]];
    if (stood_nowhere(began) || ended < began) return {};
    return stood_on(text, began, ended);
  }

  // A fold at this place, with the last step run into the copy: the end of the
  // input is not a character, so what it left open is closed here.
  template <std::size_t place, class held>
  [[nodiscard]] constexpr auto fold_at() const {
    auto fold = gathering<place>();
    fold_one_step<fold_phase::whole, place, held>(fold.here, reading, registers,
                                                  '\0', false);
    return fold;
  }
};

// Made rather than named: the reading, the states and the registers are all
// deduced, and the type and the format are what say where a group is gathered.
template <class type, fixed_string format, class reading_type,
          class states_type, class registers_type,
          class kept_type = nothing_kept_here>
[[nodiscard]] constexpr auto by_the_registers(
    const reading_type& reading, const states_type& states,
    const registers_type& registers,
    const kept_type& kept = nothing_was_kept) {
  return gathered_by_the_registers<type, format, reading_type, states_type,
                                   registers_type, kept_type>{
      reading, states, registers, kept};
}


template <class root, class type, std::size_t offset, bool as_output = false,
          class failure_type = failure_for<root>, class source_type>
[[nodiscard]] constexpr std::expected<type, failure_type> finish_value(
    const source_type& source, const char* text);

// The parts of a product, and the arguments of a call, as named functions
// rather than as lambdas called where they stand. A lambda holding references
// and called inside the argument of something that itself holds references is
// more than the constant evaluator will follow.
template <class root, class type, std::size_t offset, class failure_type,
          class source_type, std::size_t... part>
[[nodiscard]] constexpr std::expected<type, failure_type> finish_parts(
    const source_type& source, const char* text,
    std::index_sequence<part...>) {
  auto parts =
      std::tuple{finish_value<root, typename parts_of<type>::template at<part>,
                              offset + groups_before_field<type, part>(), false,
                              failure_type>(source, text)...};
  if (auto went_wrong = what_went_wrong<failure_type>(parts)) {
    return std::unexpected(std::move(*went_wrong));
  }
  return type{std::move(*std::get<part>(parts))...};
}

template <class root, class type, std::size_t offset, class failure_type,
          class source_type, std::size_t... part>
[[nodiscard]] constexpr std::expected<type, failure_type> finish_by_call(
    const source_type& source, const char* text,
    std::index_sequence<part...>) {
  auto parts =
      std::tuple{finish_value<root, typename parts_of<type>::template at<part>,
                              offset + groups_before_field<type, part>(), false,
                              failure_type>(source, text)...};
  if (auto went_wrong = what_went_wrong<failure_type>(parts)) {
    return std::unexpected(std::move(*went_wrong));
  }
  return scan::scanner<std::remove_cv_t<type>>::parse(
      std::move(*std::get<part>(parts))...);
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
template <class list_type, class element_type>
constexpr void append_to(list_type& list, element_type&& value) {
  if constexpr (requires { list.insert(list.end(), std::move(value)); }) {
    list.insert(list.end(), std::move(value));
  } else {
    list.push_back(std::move(value));
  }
}

template <std::size_t group, class type, fixed_string format, auto& automaton,
          class failure_type, class states_type, class registers_type,
          std::size_t command_count>
constexpr void collect_element(
    std::size_t state, const registers_type& registers, states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count, const char* text,
    std::optional<failure_type>& failed) {
  if constexpr (group == 0) {
    return;
  } else if constexpr (!scanned_as_range<leaf_kind_of_output<type, group - 1>>) {
    return;
  } else {
    using list_type = leaf_kind_of_output<type, group - 1>;
    using element = std::remove_cvref_t<std::ranges::range_value_t<list_type>>;
    constexpr std::size_t list_group = group - 1;
    if constexpr (gathering_of<type, format, group>::folds) {
      // Made out of the turn that was moved aside, once that turn has been
      // told what ended it. Nothing here can be: at this moment it has not.
      return;
    } else {
    // Does this step begin another turn? It does if it writes a fresh position
    // into a register that holds the place an element starts at.
    bool going_round = false;
    for (std::size_t index = 0; index < count; ++index) {
      const auto& command = commands[index];
      if (command.value == -2) continue;
      if (automaton.register_tag[command.destination] == group * 2) {
        going_round = true;
        break;
      }
    }
    if (!going_round) return;
    // The turn that is ending belongs to the state being left, and so do the
    // positions and the gatherings. Where the list goes next is the business of
    // the commands, which carry the gathering with the register.
    const auto& packed = automaton.states[state];
    std::array<bool, automaton.register_count> done{};
    for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
      const std::uint32_t open = packed.readings[reading][group * 2];
      const std::uint32_t into = packed.readings[reading][list_group * 2];
      if (done[into] || stood_nowhere(registers[open])) continue;
      done[into] = true;
      auto one = finish_value<type, element, group, false, failure_type>(
          by_the_registers<type, format>(packed.readings[reading], states,
                                         registers),
          text);
      if (!one) {
        if (!failed) failed = std::move(one).error();
        continue;
      }
      append_to(std::get<gathering_slot<type, format, list_group>>(states[into]),
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
template <std::size_t group, class type, fixed_string format, auto& automaton,
          class failure_type, class states_type, class registers_type>
constexpr void collect_turn_that_ended(
    std::size_t state, const registers_type& registers,
    states_type& states, std::optional<failure_type>& failed) {
  if constexpr (group == 0) {
    return;
  } else if constexpr (!scanned_as_range<leaf_kind_of_output<type, group - 1>>) {
    return;
  } else if constexpr (!gathering_of<type, format, group>::folds) {
    return;
  } else {
    using list_type = leaf_kind_of_output<type, group - 1>;
    using element = std::remove_cvref_t<std::ranges::range_value_t<list_type>>;
    using held = std::remove_cv_t<element>;
    constexpr std::size_t list_group = group - 1;
    const auto& packed = automaton.states[state];
    std::array<bool, automaton.register_count> done{};
    for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
      const std::uint32_t open = packed.readings[reading][group * 2];
      const std::uint32_t into = packed.readings[reading][list_group * 2];
      if (done[open]) continue;
      done[open] = true;
      auto& fold = std::get<gathering_slot<type, format, group>>(states[open]);
      if (!fold.has_going) continue;
      fold.has_going = false;
      if (fold.going.wanted_a_subject) {
        if (!failed) {
          failed = scan::as_a_failure<failure_type>(wrong_subject(
              "a fold that only takes its groups whole needs a subject that "
              "can be pointed at: give it push_group to read a stream"));
        }
        continue;
      }
      if constexpr (scan::says_what_went_wrong_folding<held>) {
        auto got =
            scan::scanner<held>{}.try_finish_groups(std::move(fold.going.state));
        if (!got) {
          if (!failed) {
            failed = scan::as_a_failure<failure_type>(std::move(got).error());
          }
          continue;
        }
        append_to(
            std::get<gathering_slot<type, format, list_group>>(states[into]),
            std::move(*got));
      } else {
        append_to(
            std::get<gathering_slot<type, format, list_group>>(states[into]),
            scan::scanner<held>{}.finish_groups(std::move(fold.going.state)));
      }
    }
  }
}

template <class type, fixed_string format, auto& automaton, class failure_type,
          class registers_type, class states_type, std::size_t... group>
constexpr void collect_turns_that_ended(
    std::size_t state, const registers_type& registers,
    states_type& states, std::index_sequence<group...>,
    std::optional<failure_type>& failed) {
  (collect_turn_that_ended<group, type, format, automaton, failure_type>(
       state, registers, states, failed),
   ...);
}

template <class type, fixed_string format, auto& automaton, class failure_type,
          class registers_type, class states_type, std::size_t command_count,
          std::size_t... group>
constexpr void collect_elements(
    std::size_t state, const registers_type& registers, states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count, std::index_sequence<group...>, const char* text,
    std::optional<failure_type>& failed) {
  (collect_element<group, type, format, automaton, failure_type>(
       state, registers, states, commands, count, text, failed),
   ...);
}

template <class type, fixed_string format, auto& automaton,
          bool hands_the_character = true, bool kept_in_the_walk = false,
          class registers_type, class states_type, std::size_t command_count,
          std::size_t... group>
constexpr void advance_scanners(
    char symbol, std::size_t state, auto position,
    const registers_type& registers,
    states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count, std::index_sequence<group...>,
    const char* text = nullptr) {
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
    (advance_scanner<group, type, format, automaton, hands_the_character,
                     kept_in_the_walk>(
         symbol, state, position, registers, old_states, states, commands,
         count, text),
     ...);
  } else {
    (advance_scanner<group, type, format, automaton, hands_the_character,
                     kept_in_the_walk>(
         symbol, state, position, registers, states, states, commands,
         count, text),
     ...);
  }
}

template <class type, class state_type, std::size_t... index>
[[nodiscard]] constexpr type finish_scanners(
    state_type state, std::index_sequence<index...>) {
  type result{};
  ((scan::fields<type>::template of<index>(result) =
        scanner_finish<field_type<type, index>>(
            std::move(std::get<index>(state)))),
   ...);
  return result;
}

// The output put together from the gatherings, walked the same way it is walked
// when the values are pieces of a subject that can be pointed at: a value asks
// its own reader to finish, a product asks its parts, a type made by a call
// makes it. Each value is taken from the gathering of the register that holds
// its opening tag in the reading that accepted.
template <class root, class type, std::size_t offset, bool as_output,
          class failure_type, class source_type>
[[nodiscard]] constexpr std::expected<type, failure_type> finish_value(
    const source_type& source, const char* text) {
  // A shape that reads its own groups is a value where it stands in somebody
  // else's format and a product of places in its own. Where this is the whole
  // of what is being read, it is the second.
  constexpr bool a_value = scanned_as_leaf<type> && !as_output;
  if constexpr (a_value && folds_by_turns<std::remove_cv_t<type>>) {
    // A leaf that was told its groups as the walk passed them. What is left is
    // the end of the input, which is not a character and so was never handed
    // over: a group that opened where nothing followed it, and every group
    // still open when the reading stopped. The same step the walk runs says
    // both, asked once more with nothing to hand over.
    using held = std::remove_cv_t<type>;
    static_assert(
        requires { source.template fold_at<offset, held>(); },
        "a shape whose places include a type that folds its own groups is read "
        "by the machine that gathers, not by a fold of its own");
    auto fold = source.template fold_at<offset, held>();
    if (fold.here.wanted_a_subject) {
      return std::unexpected(scan::as_a_failure<failure_type>(wrong_subject(
          "a fold that only takes its groups whole needs a subject that can be "
          "pointed at: give it push_group to read a stream")));
    }
    if constexpr (scan::says_what_went_wrong_folding<held>) {
      auto got =
          scan::scanner<held>{}.try_finish_groups(std::move(fold.here.state));
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<failure_type>(std::move(got).error()));
    } else {
      return scan::scanner<held>{}.finish_groups(std::move(fold.here.state));
    }
  } else if constexpr (a_value && gathers_by_its_groups<type>) {
    // A leaf built from its own groups once the match is over. They are groups
    // of this match like any others and the positions say where each one
    // stood, so what it is handed are views of the subject: nothing was
    // gathered for it and nothing was copied. That it has a subject to point
    // at is settled where the walk is made.
    using held = std::remove_cv_t<type>;
    constexpr std::size_t inside = groups_a_leaf_opens<held>();
    std::array<std::string_view, inside> theirs{};
    std::array<bool, inside> took{};
    static_assert(
        requires { source.template span<offset>(text); },
        "a shape whose places include a type built from its own groups is read "
        "by the machine that gathers, not by a fold of its own");
    [&]<std::size_t... at>(std::index_sequence<at...>) {
      ((void)[&] {
        constexpr std::size_t which = offset + 1 + at;
        const std::string_view stood_on = source.template span<which>(text);
        if (stood_on.data() == nullptr) return;
        took[at] = true;
        theirs[at] = stood_on;
      }(), ...);
    }(std::make_index_sequence<inside>{});
    const auto given = std::span<const std::string_view>(theirs);
    if constexpr (scan::says_what_went_wrong_from_groups<held>) {
      auto got = scan::scanner<held>{}.try_from_groups(given);
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<failure_type>(std::move(got).error()));
    } else if constexpr (requires {
                           scan::scanner<held>{}.from_groups(given);
                         }) {
      return scan::scanner<held>{}.from_groups(given);
    } else {
      auto state = scan::scanner<held>{}.begin_groups();
      [&]<std::size_t... at>(std::index_sequence<at...>) {
        ((void)[&] {
          if (!took[at]) return;
          open_one_group<held, at>(state);
          close_one_group<held, at>(state, theirs[at]);
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
    const auto& gathered = source.template gathering<offset>();
    if constexpr (scan::says_what_went_wrong_finishing<type>) {
      auto got = scan::scanner<std::remove_cv_t<type>>{}.try_finish(gathered);
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<failure_type>(std::move(got).error()));
    } else {
      return scanner_finish<type>(gathered);
    }
  } else if constexpr (scanned_as_range<type>) {
    // What has been put in as each element ended, and then the one that was
    // still being read when the whole thing ended.
    using element = std::remove_cvref_t<std::ranges::range_value_t<type>>;
    type made = source.template list<offset>();
    // The turn that was still going when the whole thing ended. Where the list
    // is written to be allowed none at all, there may not have been one.
    if (source.template took_part<offset + 1>()) {
      auto last = finish_value<root, element, offset + 1, false, failure_type>(
          source, text);
      if (!last) return std::unexpected(std::move(last).error());
      append_to(made, std::move(*last));
    }
    return made;
  } else if constexpr (scanned_as_variant<type>) {
    // Exactly one branch ran, and its mark says so: the mark of the branch
    // that took part was opened, and the others never were. The same question
    // the subject that can be pointed at answers by whether the group points
    // anywhere.
    return [&]<std::size_t... branch>(std::index_sequence<branch...>)
               -> std::expected<type, failure_type> {
      std::optional<std::expected<type, failure_type>> made;
      const auto take = [&]<std::size_t which>() {
        constexpr std::size_t mark =
            offset + groups_before_branch<type, which>();
        if (made || !source.template took_part<mark>()) return;
        using alternative = branch_at<type, which>;
        auto part = finish_value<root, alternative, mark + 1, false,
                                 failure_type>(source, text);
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
    return finish_by_call<root, type, offset, failure_type>(
        source, text, std::make_index_sequence<parts_of<type>::count>{});
  } else {
    return finish_parts<root, type, offset, failure_type>(
        source, text, std::make_index_sequence<parts_of<type>::count>{});
  }
}

// A shape's places, gathered together, and what the walk has told it.
//
// This is the other way of answering the builder's questions. Where the machine
// keeps a gathering per register and works out which register holds a place,
// this keeps them all in one object -- which is what a type is handed when it
// is told its own groups, and it is told them because its places take turns.
template <class type, fixed_string format>
struct shape_turns {
  using held = std::remove_cv_t<type>;
  static constexpr std::size_t places = groups_of_output<held>();
  using gatherings_type = decltype(make_scanner_state<held, format>());

  gatherings_type gatherings = make_scanner_state<held, format>();
  // An element that did not read, kept until there is somebody to hand it to:
  // a turn ends in the middle of a walk, where there is nowhere to say so.
  std::optional<shape_failure<held>> went_wrong{};
  // Which places have been opened since they were last read out. A choice says
  // which branch ran by which mark opened; a list says whether a turn is going.
  std::array<bool, places == 0 ? 1 : places> took{};
};

template <class shape_type>
struct gathered_by_a_fold {
  shape_type& state;

  template <std::size_t place>
  [[nodiscard]] constexpr const auto& gathering() const {
    return std::get<place>(state.gatherings);
  }

  template <std::size_t place>
  [[nodiscard]] constexpr const auto& list() const {
    return std::get<place>(state.gatherings);
  }

  template <std::size_t place>
  [[nodiscard]] constexpr bool took_part() const {
    return state.took[place];
  }

  // A fold at this place has been told everything as it happened -- the walk
  // hands the edges on and this hands them further -- so there is nothing left
  // to run into it here.
  template <std::size_t place, class held>
  [[nodiscard]] constexpr auto fold_at() const {
    return std::get<place>(state.gatherings);
  }
};

// Where a group of a shape belongs: the place it is, or the place it is inside
// of and which of that type's own groups it is.
template <class type, std::size_t group>
inline constexpr std::size_t shape_place_of =
    group - leaf_offset_of_output<std::remove_cv_t<type>, group>;

template <class type, std::size_t group>
inline constexpr std::size_t shape_place_inside =
    leaf_offset_of_output<std::remove_cv_t<type>, group>;

// One character, to the place it fell in -- or to the type standing at that
// place, where the group is one of that type's own. A list's own group holds no
// characters: what is inside it are the places of one turn, and they take them.
template <class type, fixed_string format, std::size_t group, class shape_type>
constexpr void push_shape_place(shape_type& state, char letter) {
  using held = std::remove_cv_t<type>;
  constexpr std::size_t place = shape_place_of<held, group>;
  constexpr std::size_t inside = shape_place_inside<held, group>;
  using stands_for = std::remove_cv_t<leaf_kind_of_output<held, group>>;
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
template <class type, fixed_string format, std::size_t group, class shape_type>
constexpr void open_shape_place(shape_type& state) {
  using held = std::remove_cv_t<type>;
  constexpr std::size_t place = shape_place_of<held, group>;
  constexpr std::size_t inside = shape_place_inside<held, group>;
  if constexpr (inside != 0) {
    using stands_for = std::remove_cv_t<leaf_kind_of_output<held, group>>;
    open_one_group<stands_for, inside - 1>(
        std::get<place>(state.gatherings).here.state);
  } else {
    state.took[place] = true;
  }
}

// A place closed. Where it is a list, that is one turn: the element is put
// together out of the places inside it, added to the list, and those places
// begin again for the turn that may follow.
template <class type, fixed_string format, std::size_t group,
          class failure_type, class shape_type>
constexpr void close_shape_place(shape_type& state,
                                 std::optional<failure_type>& failed) {
  using held = std::remove_cv_t<type>;
  constexpr std::size_t place = shape_place_of<held, group>;
  constexpr std::size_t inside = shape_place_inside<held, group>;
  using stands_for = std::remove_cv_t<leaf_kind_of_output<held, group>>;
  if constexpr (inside != 0) {
    close_one_group<stands_for, inside - 1>(
        std::get<place>(state.gatherings).here.state);
  } else if constexpr (scanned_as_range<stands_for>) {
    using element = std::remove_cvref_t<std::ranges::range_value_t<stands_for>>;
    if (!state.took[place + 1]) return;
    auto one = finish_value<held, element, place + 1, false, failure_type>(
        gathered_by_a_fold<shape_type>{state}, nullptr);
    if (!one) {
      if (!failed) failed = std::move(one).error();
      return;
    }
    append_to(std::get<place>(state.gatherings), std::move(*one));
    // The turn is over: what its places gathered belongs to the element that
    // has just been taken, and the next turn starts from nothing.
    static constexpr auto spread = spread_of<held, format>();
    [&]<std::size_t... inside>(std::index_sequence<inside...>) {
      ((void)[&] {
        constexpr std::size_t which = place + 1 + inside;
        std::get<which>(state.gatherings) =
            gathering_of<held, format, which>::begin(
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
template <class type, fixed_string format, bool cut = true,
          class failure_type = failure_for<type>>
class stream_state {
 private:
  static_assert(
      !a_flat_reader_inside<type>(),
      "a type built from its groups after the match cannot read a stream: "
      "there is nothing left to point at by the time it would be handed them "
      "-- give it begin_groups and push_group to be told its groups as they "
      "arrive");
  inline static constexpr const auto& automaton =
      streaming_automaton<type, format, cut>;
  inline static constexpr std::size_t field_count = groups_of_output<type>();
  // One gathering per register, because a gathering follows the register it
  // belongs to and there is no arithmetic that says which registers go
  // together.
  inline static constexpr std::size_t slot_count = automaton.register_count;
  using field_states = register_state<type, format>;

 public:
  constexpr stream_state() {
    scanner_states_ = make_register_states<type, format, automaton>();
    std::ranges::fill(registers_, scan::tre::negative_tag);
    execute_commands(automaton.initialize, automaton.initialize.size(),
                     registers_, std::ptrdiff_t{0});
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
    collect_elements<type, format, automaton, failure_type>(
        state_, registers_, scanner_states_, transition->commands,
        transition->command_count, std::make_index_sequence<field_count>{},
        nullptr, failed_);
    execute_commands(transition->commands, transition->command_count, registers_,
                     ++position_);
    advance_scanners<type, format, automaton>(
        symbol, transition->target, position_, registers_, scanner_states_,
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
  template <std::size_t field>
  [[nodiscard]] constexpr const auto& gathering() const {
    const std::size_t here =
        state_ == packed_range<0>::reject ? automaton.initial : state_;
    // The same question the reading asks: a field still being typed is where
    // it is being gathered, and one that has closed is the copy taken then.
    const auto& reading = automaton.states[here].readings[0];
    const std::uint32_t open = reading[field * 2];
    const std::uint32_t close = reading[field * 2 + 1];
    const bool still_reading = registers_[close] < registers_[open];
    return std::get<field>(scanner_states_[still_reading ? open : close]);
  }

  // Whether that field is being read right now: begun and not yet ended.
  template <std::size_t field>
  [[nodiscard]] constexpr bool reading() const {
    if (state_ == packed_range<0>::reject) return false;
    const auto& packed = automaton.states[state_];
    if (packed.reading_count == 0) return false;
    const std::uint32_t open = packed.readings[0][field * 2];
    const std::uint32_t close = packed.readings[0][field * 2 + 1];
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

  [[nodiscard]] constexpr std::expected<type, failure_type> finish() const& {
    stream_state copy = *this;
    return std::move(copy).finish();
  }

  [[nodiscard]] constexpr std::expected<type, failure_type> finish() && {
    if (failed_) return std::unexpected(std::move(*failed_));
    if (state_ == packed_range<0>::reject) {
      return std::unexpected(scan::as_a_failure<failure_type>(
          no_match("input does not match scan expression")));
    }
    const auto slot = automaton.states[state_].accepting_slot;
    if (slot == packed_state<0, 0, 0>::not_accepting) {
      return std::unexpected(scan::as_a_failure<failure_type>(
          no_match("input does not match scan expression")));
    }
    // The reading that accepted says which register holds each value. Nothing
    // is written here: the commands that end a match are not run by this
    // machine, so a group that never closed is read from where it was being
    // gathered, which is what the registers say.
    const auto& reached = automaton.states[state_];
    // Nothing to point at: this machine is fed and never holds the subject.
    return finish_value<type, type, 0, true, failure_type>(
        by_the_registers<type, format>(reached.readings[slot], scanner_states_,
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
  std::optional<failure_type> failed_;
};

// Which gatherings a group is added to where the machine stands.
//
// A state stands in several readings at once and they can share a register, so
// what a character is added to is the set of the registers holding the group's
// opening across those readings, each of them once. Walking the readings to
// find that out on every character is what a machine that does not know where
// it stands has to do; where the state is known, the set is known, and this is
// it.
template <auto& automaton, std::size_t state, std::size_t group>
inline constexpr auto gathered_at = [] consteval {
  constexpr const auto& packed = automaton.states[state];
  struct answer {
    std::array<std::uint32_t, automaton.register_count> at{};
    std::size_t count = 0;
  };
  answer said;
  for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
    const std::uint32_t opening = packed.readings[reading][group * 2];
    bool already = false;
    for (std::size_t at = 0; at < said.count; ++at) {
      if (said.at[at] == opening) already = true;
    }
    if (!already) said.at[said.count++] = opening;
  }
  return said;
}();

// The gatherer the format reader hands to the walk: the fields' own scanners,
// following the moves.
//
// What the machine that can be stopped and started looks up on every character
// -- the state, its runs, the commands of the run it took, the readings that
// say which register holds which group -- is a constant here, because the walk
// knows where it stands. The work each character does is what it was: apply
// what the move writes to the gatherings, and give the character to the fields
// that are open.
template <class type, fixed_string format, auto& automaton,
          bool pointable = false, class mark_kind = std::ptrdiff_t>
class field_gatherer {
 public:
  // A type built from its groups once the match is over is handed views of the
  // subject. Where the subject is gone as it is read there is nothing to give
  // it, and holding the characters until the end to give it something would be
  // a hold with no bound.
  static_assert(
      pointable || !a_flat_reader_inside<type>(),
      "a type built from its groups after the match needs a subject that can "
      "be pointed at: give it begin_groups and push_group to be told its "
      "groups as they are read, or scan it from something contiguous");
  static constexpr std::size_t field_count = groups_of_output<type>();
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
        using how = gathering_of<type, format, group>;
        if constexpr (how::folds && how::inside) return true;
        if constexpr (gathers_in_the_walk<type, format, automaton, group>()) {
          return true;
        }
        if constexpr (how::folds && how::the_place && !how::place_repeats &&
                      every_move_says_the_groups<automaton>()) {
          return true;
        }
        return false;
      }());
    }(std::make_index_sequence<groups_of_output<type>()>{});
  }();

  using states_type =
      std::array<register_state<type, format, mark_kind>,
                 nothing_at_a_register ? 0 : automaton.register_count>;

  constexpr field_gatherer() {
    if constexpr (!nothing_at_a_register) {
      states_ = make_register_states<type, format, automaton, mark_kind>();
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
  template <std::size_t state, std::size_t move, class registers_type>
  constexpr void moving(const registers_type& registers, auto) {
    constexpr const auto& taken = automaton.states[state].ranges[move];
    collect_elements<type, format, automaton, failure_for<type>>(
        state, registers, states_, taken.commands, taken.command_count,
        std::make_index_sequence<field_count>{}, text_, failed_);
  }

  // A run the walk stepped over in vectors: the fields that are open take all
  // of it, which is one pass over the piece rather than one call a character.
  template <std::size_t state, class registers_type>
  constexpr void took_run(const char* from, const char* to,
                          const registers_type& registers, auto) {
    hand_run<state>(from, to, registers,
                    std::make_index_sequence<field_count>{});
  }

  template <std::size_t state, std::size_t landed, std::size_t move,
            class registers_type>
  SCAN_FORCE_INLINE constexpr void moved(char letter,
                                         const registers_type& registers,
                                         auto position) {
    // A move that writes nothing leaves the gatherings where they are, and
    // most of the characters of a subject are read by one: inside a field
    // nothing is written, which is what holding the tags back bought. So the
    // whole of what a move does to the gatherings is skipped for it, and what
    // is left is handing the character to the fields that are open.
    if constexpr (state != landed || staying_writes<automaton, state>()) {
      constexpr const auto& taken = automaton.states[state].ranges[move];
      advance_scanners<type, format, automaton, false, true>(
          letter, landed, position, registers, states_, taken.commands,
          taken.command_count, std::make_index_sequence<field_count>{}, text_);
    }
    hand_over<state, landed, move>(letter, registers,
                                   std::make_index_sequence<field_count>{});
    collect_turns_that_ended<type, format, automaton, failure_for<type>>(
        landed, registers, states_, std::make_index_sequence<field_count>{},
        failed_);
  }

  // Where the walk ended is a constant, so the reading that accepted is one
  // too, and the value is put together here rather than looked for afterwards.
  // A walk keeps every place it passes, and the last one it keeps is the
  // answer -- so this runs more than once and the last of them wins, which is
  // what it has always done with the value. It has to do the same with a
  // failure: a place passed early where a field was not read yet is not this
  // reading's answer, and holding on to that would lose every match after it.
  template <std::size_t state, class registers_type>
  constexpr void ended(const registers_type& registers) {
    constexpr const auto& packed = automaton.states[state];
    // What the walk kept is told where the walk stands, and then read from
    // where it is.
    //
    // Reading it runs one more step, by the positions, because the end of the
    // input is not a character and whatever is still open has to be closed.
    // Everything else was said by the moves as they were taken, so the fold is
    // set to the positions as they stand: nothing has moved since, and that
    // last step announces nothing twice.
    auto kept = plain_folds_;
    [&]<std::size_t... group>(std::index_sequence<group...>) {
      ([&] {
        using how = gathering_of<type, format, group>;
        if constexpr (how::folds && how::the_place && !how::place_repeats &&
                      every_move_says_the_groups<automaton>()) {
          auto& one = std::get<gathering_slot<type, format, group, mark_kind>>(kept);
          using held = std::remove_cv_t<leaf_kind_of_output<type, group>>;
          constexpr std::size_t inside = groups_a_leaf_opens<held>();
          const auto& reading = packed.readings[packed.accepting_slot];
          for (std::size_t which = 0; which < inside; ++which) {
            one.here.told_at[which] =
                registers[reading[(group + 1 + which) * 2]];
            one.here.ended_at[which] =
                registers[reading[(group + 1 + which) * 2 + 1]];
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
          using how = gathering_of<type, format, group>;
          if constexpr (gathers_in_the_walk<type, format, automaton, group>() ||
                        (how::folds && how::the_place && !how::place_repeats &&
                         every_move_says_the_groups<automaton>())) {
            made |= std::uint64_t{1} << group;
          }
        }(), ...);
      }(std::make_index_sequence<field_count>{});
      return made;
    }();
    const kept_by_the_walk<decltype(kept), mine> mine_kept{kept};
    auto got = finish_value<type, type, 0, true>(
        by_the_registers<type, format>(packed.readings[packed.accepting_slot],
                                       states_, registers, mine_kept),
        text_);
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
  [[nodiscard]] constexpr std::expected<type, failure_for<type>> taken() {
    if (failed_) return std::unexpected(std::move(*failed_));
    if (!made_) {
      return std::unexpected(scan::as_a_failure<failure_for<type>>(
          no_match("input does not match scan expression")));
    }
    return std::move(*made_);
  }

  [[nodiscard]] constexpr std::optional<failure_for<type>>& went_wrong() {
    return failed_;
  }

  [[nodiscard]] constexpr std::optional<type>& made() { return made_; }

  // Where the subject begins, for a walk that has all of it in front of it. A
  // fold reading such a subject is handed each of its groups whole.
  // Said once, where the subject is handed over, rather than on every
  // character: what a fold points at is the subject, and the subject does not
  // move.
  constexpr void points_at(const char* text) {
    text_ = text;
    [&]<std::size_t... group>(std::index_sequence<group...>) {
      ([&] {
        using how = gathering_of<type, format, group>;
        if constexpr (how::folds && how::the_place) {
          std::get<gathering_slot<type, format, group, mark_kind>>(plain_folds_).here.text =
              text;
        }
      }(), ...);
    }(std::make_index_sequence<field_count>{});
  }

 private:
  template <std::size_t state, class registers_type, std::size_t... group>
  constexpr void hand_run(const char* from, const char* to,
                          const registers_type& registers,
                          std::index_sequence<group...>) {
    (hand_run_group<state, group>(from, to, registers), ...);
  }

  template <std::size_t state, std::size_t group, class registers_type>
  constexpr void hand_run_group(const char* from, const char* to,
                                const registers_type& registers) {
    using held_type = leaf_kind_of_output<type, group>;
    using how = gathering_of<type, format, group>;
    if constexpr (scanned_as_range<held_type>) {
      return;
    } else if constexpr (how::folds && how::inside) {
      return;
    } else if constexpr (how::folds && how::the_place && !how::place_repeats &&
                         every_move_says_the_groups<automaton>()) {
      // A run, handed to the groups it fell in, whole.
      //
      // Nothing is written across a run -- that is what makes it a run -- so
      // what is open at its first character is open at its last, and the move
      // that takes it says which groups those are. There is one question for
      // the whole run and, for a type that takes a run, one call.
      constexpr std::size_t staying = staying_move<automaton, state>();
      if constexpr (staying != no_move) {
        constexpr std::uint64_t inside_now =
            automaton.states[state].ranges[staying].groups_open;
        // A run that begins a group again on every character is a run of
        // turns, and a turn is not something to hand over in bulk: what the
        // type is told has to be what happened.
        constexpr std::uint64_t begins_again =
            automaton.states[state].ranges[staying].groups_reopened;
        using held = std::remove_cv_t<held_type>;
        constexpr std::size_t inside = groups_a_leaf_opens<held>();
        auto& fold = std::get<gathering_slot<type, format, group, mark_kind>>(plain_folds_);
        const std::string_view run(from, static_cast<std::size_t>(to - from));
        [&]<std::size_t... which>(std::index_sequence<which...>) {
          ((void)[&] {
            if constexpr ((inside_now &
                           (std::uint64_t{1} << (group + 1 + which))) != 0 &&
                          (begins_again &
                           (std::uint64_t{1} << (group + 1 + which))) == 0) {
              if constexpr (takes_group_characters<
                                held, which, typename std::remove_cvref_t<
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
          fold_of<std::remove_cv_t<held_type>, mark_kind,
                  gathering_of<type, format, group>::place_repeats>;
      if constexpr (every_group_whole<held_type, typename folded::state_type>()) {
        if (from != to) {
          fold_the_readings<group, gathering_slot<type, format, group, mark_kind>,
                            std::remove_cv_t<held_type>, automaton>(
              state, registers, states_, *from, true, text_);
        }
      } else {
        for (const char* letter = from; letter != to; ++letter) {
          fold_the_readings<group, gathering_slot<type, format, group, mark_kind>,
                            std::remove_cv_t<held_type>, automaton>(
              state, registers, states_, *letter, true, text_);
        }
      }
    } else if constexpr (gathers_in_the_walk<type, format, automaton, group>()) {
      // A run is where nothing is written, so what was open at its first
      // character is open at its last: one question for the whole of it.
      constexpr std::uint64_t staying =
          staying_move<automaton, state>() == no_move
              ? 0
              : automaton.states[state]
                    .ranges[staying_move<automaton, state>()]
                    .groups_open;
      if constexpr ((staying & (std::uint64_t{1} << group)) != 0) {
        gathering_of<type, format, group>::push_run(
            std::get<gathering_slot<type, format, group, mark_kind>>(plain_folds_), from,
            to);
      }
    } else {
      constexpr auto at = gathered_at<automaton, state, group>;
      constexpr std::uint32_t closing =
          automaton.states[state].reading_count == 0
              ? 0
              : automaton.states[state].readings[0][group * 2 + 1];
      for (std::size_t which = 0; which < at.count; ++which) {
        const std::uint32_t opening = at.at[which];
        if (stood_nowhere(registers[opening])) continue;
        if (registers[closing] >= registers[opening]) continue;
        gathering_of<type, format, group>::push_run(
            std::get<gathering_slot<type, format, group, mark_kind>>(states_[opening]),
            from, to);
      }
    }
  }

  template <std::size_t from, std::size_t landed, std::size_t move,
            class registers_type, std::size_t... group>
  constexpr void hand_over(char letter, const registers_type& registers,
                           std::index_sequence<group...>) {
    (hand_group<from, landed, move, group>(letter, registers), ...);
  }

  template <std::size_t from, std::size_t landed, std::size_t move,
            std::size_t group, class registers_type>
  constexpr void hand_group(char letter, const registers_type& registers) {
    using held_type = leaf_kind_of_output<type, group>;
    using how = gathering_of<type, format, group>;
    if constexpr (scanned_as_range<held_type>) {
      return;
    } else if constexpr (how::folds && how::inside) {
      return;
    } else if constexpr (how::folds && how::the_place && !how::place_repeats &&
                         step_says_the_groups<automaton, from, move>()) {
      // The machine says what happened; nothing is read to find out, and the
      // fold is where the walk keeps it rather than where a register points.
      auto& fold = std::get<gathering_slot<type, format, group, mark_kind>>(plain_folds_);
      constexpr bool stays_put =
          automaton.states[from].ranges[move].target == from &&
          automaton.states[from].ranges[move].groups_reopened == 0;
      fold_by_the_step<group, std::remove_cv_t<held_type>, automaton, from,
                       move, !stays_put>(fold, letter, true);
    } else if constexpr (how::folds && how::the_place) {
      fold_the_readings<group, gathering_slot<type, format, group, mark_kind>,
                        std::remove_cv_t<held_type>, automaton>(
          landed, registers, states_, letter, true, text_);
    } else if constexpr (gathers_in_the_walk<type, format, automaton, group>()) {
      // Open where the move says so, and gathered where the walk keeps it.
      constexpr std::uint64_t now =
          automaton.states[from].ranges[move].groups_open;
      constexpr std::uint64_t again =
          automaton.states[from].ranges[move].groups_reopened;
      if constexpr ((now & (std::uint64_t{1} << group)) != 0) {
        static constexpr auto spread = spread_of<type, format>();
        auto& made = std::get<gathering_slot<type, format, group, mark_kind>>(plain_folds_);
        if constexpr ((again & (std::uint64_t{1} << group)) != 0) {
          made = gathering_of<type, format, group>::begin(
              spread.parameters[group].view());
        }
        gathering_of<type, format, group>::push(made, letter);
      }
    } else {
      static constexpr auto spread = spread_of<type, format>();
      constexpr auto at = gathered_at<automaton, landed, group>;
      constexpr std::uint32_t closing =
          automaton.states[landed].reading_count == 0
              ? 0
              : automaton.states[landed].readings[0][group * 2 + 1];
      for (std::size_t which = 0; which < at.count; ++which) {
        const std::uint32_t opening = at.at[which];
        if (stood_nowhere(registers[opening])) continue;
        if (registers[closing] >= registers[opening]) continue;
        gathering_of<type, format, group>::push(
            std::get<gathering_slot<type, format, group, mark_kind>>(states_[opening]),
            letter);
      }
    }
  }


  using plain_folds_type = decltype(make_slots<type, format, mark_kind>());
  // Kept first, and by itself.
  //
  // What the walk touches on every character is here; everything else it holds
  // -- the answer, the failure, where the subject begins, the gatherings that
  // do follow a register -- is touched once a match or once a field. Put first
  // in the object, the hot part shares no cache line with the cold, and an
  // optimiser that will not promote a whole gatherer to registers can still
  // keep this much of it in one.
  [[no_unique_address]] plain_folds_type plain_folds_ =
      make_slots<type, format, mark_kind>();
  states_type states_;
  std::optional<type> made_;
  std::optional<failure_for<type>> failed_;
  const char* text_ = nullptr;
};

// One match off input that arrives in pieces, and where the next one starts.
//
// The head of the reading, in the sense the format reader means: the machine
// takes what it takes and stops, and where it stopped is where the reading
// goes on from -- which for pieces is a place in the piece it is holding, so
// nothing has to be put back.
template <class type, fixed_string format, class source_type>
struct taken_from_pieces {
  std::optional<type> value;
  bool matched = false;
};

// How much a reading of this format has to carry between one match and the
// next, where the subject arrives in pieces. The same number the stream
// reading asks for: the longest walk out of a match that finds no other one.
template <class type, fixed_string format>
inline constexpr std::size_t pieces_hold = [] consteval {
  constexpr std::size_t window =
      walk_past_a_match<streaming_automaton<type, format>>();
  if constexpr (window == std::numeric_limits<std::size_t>::max()) {
    return std::size_t{0};
  } else {
    return window * 2 + 1;
  }
}();

template <class type, fixed_string format, class source_type>
[[nodiscard]] constexpr auto take_from_pieces(source_type& into,
                                              const char*& cursor,
                                              const char*& last,
                                              std::ptrdiff_t& place) {
  constexpr const auto& automaton = streaming_automaton<type, format>;
  taken_from_pieces<type, format, source_type> said;
  std::array<std::ptrdiff_t, automaton.register_count> registers{};
  std::ranges::fill(registers, scan::tre::negative_tag);
  execute_commands(automaton.initialize, automaton.initialize.size(), registers,
                   place);
  // The same note as everywhere else, and here it costs nothing to go back to:
  // the place is an address inside a piece the reading is still holding.
  constexpr bool walks_past = walk_past_a_match<automaton>() != 0;
  using kept_type =
      std::conditional_t<walks_past,
                         std::array<std::ptrdiff_t, automaton.register_count>,
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
template <class type, fixed_string format, piecewise_char_range pieces_type>
[[nodiscard]] constexpr std::expected<type, failure_for<type>> scan_pieces(
    pieces_type&& pieces) {
  constexpr const auto& automaton = streaming_automaton<type, format>;
  std::array<std::ptrdiff_t, automaton.register_count> registers{};
  std::ranges::fill(registers, scan::tre::negative_tag);
  execute_commands(automaton.initialize, automaton.initialize.size(), registers,
                   std::ptrdiff_t{0});
  auto view = std::views::all(std::forward<pieces_type>(pieces));
  gathers_from_pieces<field_gatherer<type, format, automaton>, decltype(view),
                      pieces_hold<type, format>>
      into(field_gatherer<type, format, automaton>{}, std::move(view));
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
    return std::unexpected(scan::as_a_failure<failure_for<type>>(
        no_match("input does not match scan expression")));
  }
  return into.taken();
}

template <class type, fixed_string format, std::ranges::input_range range_type>
[[nodiscard]] constexpr std::expected<type, failure_for<type>> scan_stream(
    range_type&& input) {
  // The whole of the reading, so the walks below a match are kept.
  constexpr const auto& automaton = streaming_automaton<type, format, false>;
  // Positions as addresses where the subject lies in a row.
  //
  // An address is the cursor, which the walk is holding anyway; a count is a
  // step of its own on every character, and a cursor that is a base and an
  // index rather than one pointer. Where the subject arrives a character at a
  // time there is nothing to point at and the count is what there is.
  constexpr bool in_a_row = std::ranges::contiguous_range<range_type>;
  using mark_kind = std::conditional_t<in_a_row, const char*, std::ptrdiff_t>;
  std::array<mark_kind, automaton.register_count> registers{};
  if constexpr (in_a_row) {
    std::ranges::fill(registers, nullptr);
    execute_commands(automaton.initialize, automaton.initialize.size(),
                     registers, static_cast<const char*>(nullptr));
  } else {
    std::ranges::fill(registers, scan::tre::negative_tag);
    execute_commands(automaton.initialize, automaton.initialize.size(),
                     registers, std::ptrdiff_t{0});
  }
  field_gatherer<type, format, automaton, in_a_row, mark_kind> into;
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
  if constexpr (std::ranges::contiguous_range<range_type> &&
                !holds_a_range<type>()) {
    into.points_at(std::ranges::data(input));
    const char* cursor = std::ranges::data(input);
    const char* const last = cursor + std::ranges::size(input);
    const char* position = cursor;
    constexpr walk_shape shape{
        .in_words = true,
        .tags_read = groups_whose_place_is_read<type, format, automaton>(),
        .budget = bodies_worth_writing<automaton>()};
    walk_answer<const char*> best;
    if (!run_continuation<automaton, shape, automaton.initial, shape.budget, 0,
                          const char*>(cursor, last, position, registers, into,
                                       best)) {
      return std::unexpected(scan::as_a_failure<failure_for<type>>(
          no_match("input does not match scan expression")));
    }
    return into.taken();
  } else {
    auto cursor = std::ranges::begin(input);
    mark_kind position = 0;
    constexpr walk_shape shape{.budget = bodies_worth_writing<automaton>()};
    walk_answer<decltype(cursor)> best;
    if (!run_continuation<automaton, shape, automaton.initial, shape.budget, 0,
                          mark_kind>(cursor, std::ranges::end(input), position,
                                     registers, into, best)) {
      return std::unexpected(scan::as_a_failure<failure_for<type>>(
          no_match("input does not match scan expression")));
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
template <class type>
struct taken_ahead {
  type value;
  std::optional<char> stopped;
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
template <std::size_t hold>
struct stream_carry {
  std::array<char, hold == 0 ? 1 : hold> held{};
  std::size_t count = 0;
  std::size_t at = 0;

  [[nodiscard]] constexpr bool empty() const { return at == count; }
  [[nodiscard]] constexpr char front() const { return held[at]; }
  constexpr void pop() { ++at; }

  // In front of whatever is still unread here, because they were read first.
  constexpr void put_in_front(const char* from, std::size_t many) {
    if (many == 0) return;
    const std::size_t left = count - at;
    std::array<char, hold == 0 ? 1 : hold> made{};
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
template <class type, fixed_string format, class iterator_type,
          class sentinel_type, std::size_t hold>
[[nodiscard]] constexpr std::expected<taken_ahead<type>, failure_for<type>>
scan_stream_prefix(iterator_type& first, sentinel_type last,
                   stream_carry<hold>& carry) {
  constexpr const auto& automaton = streaming_automaton<type, format>;
  constexpr std::size_t window = walk_past_a_match<automaton>();
  constexpr bool can_go_back = std::forward_iterator<iterator_type>;
  static_assert(
      can_go_back || window != std::numeric_limits<std::size_t>::max(),
      "this pattern can read any number of characters past a match without "
      "finding another one, so the reading that has to give them back would "
      "have to hold any number of them: read it from something that can be "
      "gone back over -- a forward range, characters in a row, or input in "
      "pieces");
  stream_state<type, format> state;
  std::optional<char> stopped;
  std::optional<stream_state<type, format>> note;
  // The place the note was taken at, kept the way this reading can keep it: an
  // iterator where the reading can be gone back over, and nothing at all where
  // it cannot -- an iterator of such a range cannot even be copied.
  using place_type =
      std::conditional_t<can_go_back, iterator_type, nothing_kept>;
  place_type note_at{};
  constexpr std::size_t held_here =
      can_go_back || window == 0 ||
              window == std::numeric_limits<std::size_t>::max()
          ? 1
          : window;
  std::array<char, held_here> since{};
  std::size_t since_count = 0;
  if (state.accepting()) note = state;
  while (true) {
    char symbol = 0;
    if (!carry.empty()) {
      symbol = carry.front();
    } else if (first != last) {
      symbol = static_cast<char>(*first);
    } else {
      break;
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
      ++first;
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
      [](std::expected<type, failure_for<type>> got,
         std::optional<char> ended_it)
      -> std::expected<taken_ahead<type>, failure_for<type>> {
    if (!got) return std::unexpected(std::move(got).error());
    return taken_ahead<type>{std::move(*got), ended_it};
  };
  if (state.accepting()) return handed_back(std::move(state).finish(), stopped);
  if (note) {
    // Past the match and dead. The answer is the place that was kept, and what
    // was read after it goes back in front of the reading.
    if constexpr (can_go_back) {
      first = note_at;
    } else {
      carry.put_in_front(since.data(), since_count);
    }
    return handed_back(std::move(*note).finish(), std::optional<char>{});
  }
  // Nothing matched; `finish` says so in the way the caller expects.
  return handed_back(std::move(state).finish(), stopped);
}

// How much a reading of this format has to be able to hold: nothing where it
// can be gone back over, and nothing where no walk out of a match ever fails
// to find another. Otherwise the characters read past a match, and the ones
// already held when that happened.
template <class type, fixed_string format, class iterator_type>
inline constexpr std::size_t stream_hold = [] consteval {
  if constexpr (std::forward_iterator<iterator_type>) {
    return std::size_t{0};
  } else {
    constexpr std::size_t window =
        walk_past_a_match<streaming_automaton<type, format>>();
    if constexpr (window == std::numeric_limits<std::size_t>::max()) {
      // Refused where it is used; sized so that saying so is what the caller
      // sees, rather than an array of every address there is.
      return std::size_t{0};
    } else {
      return window * 2;
    }
  }
}();

template <class type, fixed_string format, class iterator_type>
using stream_carry_for = stream_carry<stream_hold<type, format, iterator_type>>;

template <class type, fixed_string format, std::ranges::input_range range_type>
[[nodiscard]] constexpr std::expected<taken_ahead<type>, failure_for<type>>
scan_stream_prefix(range_type&& input) {
  auto first = std::ranges::begin(input);
  stream_carry<stream_hold<type, format, decltype(first)>> carry;
  return scan_stream_prefix<type, format>(first, std::ranges::end(input),
                                          carry);
}



}  // namespace scan::detail

// The helper that reads a shape, which is ordinary code and says so.
//
// It is here rather than higher up because the reading it does is the reading
// everything else does -- one builder, one gathering, one fold -- and a
// consumer that writes its own says the same things through the same hooks.
// Nothing below this line knows that a type has fields; this is where that
// knowledge lives.
export namespace scan {




template <fixed_string format>
struct aggregate_scanner {
  // The shape read out of groups that this format made, for a type that was
  // named rather than inherited from.
  //
  // A caller writing `scan<"{},{}">.of<point>()` says the format at the call
  // and the type at the call, and `point` may have no scanner at all. What
  // reads it is this, asked for both: the library below hands over the groups
  // and asks nothing about what a point is made of.
  template <class type>
  [[nodiscard]] static constexpr auto read(
      std::span<const std::string_view> groups)
      -> std::expected<type, detail::failure_for<type>> {
    return detail::build_value<detail::failure_for<type>,
                               detail::format_parameters<type, format>, type, 0,
                               true>(groups);
  }

  // The same, where the caller asked for the value itself: what went wrong is
  // thrown at the asking, which is the only place anything is thrown.
  template <class type>
  [[nodiscard]] static constexpr type read_or_throw(
      std::span<const std::string_view> groups) {
    return detail::build_value<detail::failure_for<type>,
                               detail::format_parameters<type, format>, type, 0,
                               true, detail::throws_a_failure>(groups);
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
    return detail::places_pattern<type, format>();
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
  template <class self_type>
    requires(!detail::says_a_list_inside<scanner_target_t<self_type>>())
  [[nodiscard]] constexpr auto try_from_groups(
      this const self_type& self, std::span<const std::string_view> groups)
      -> std::expected<scanner_target_t<self_type>,
                       detail::shape_failure<scanner_target_t<self_type>>> {
    using type = scanner_target_t<self_type>;
    static_cast<void>(self);
    return detail::build_value<detail::shape_failure<type>,
                               detail::format_parameters<type, format>, type, 0,
                               true>(groups);
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
  template <class self_type>
    requires(detail::turns_can_be_folded<scanner_target_t<self_type>, format>())
  [[nodiscard]] constexpr auto begin_groups(this const self_type& self) {
    static_cast<void>(self);
    return detail::shape_turns<scanner_target_t<self_type>, format>{};
  }

  template <class self_type, std::size_t place, class state_type>
    requires(detail::turns_can_be_folded<scanner_target_t<self_type>, format>())
  constexpr void push_group(this const self_type& self, state_type& state,
                            scan::group_at<place>, char letter) {
    static_cast<void>(self);
    detail::push_shape_place<scanner_target_t<self_type>, format, place>(
        state, letter);
  }

  template <class self_type, std::size_t place, class state_type>
    requires(detail::turns_can_be_folded<scanner_target_t<self_type>, format>())
  constexpr void opened_group(this const self_type& self, state_type& state,
                              scan::group_at<place>) {
    static_cast<void>(self);
    detail::open_shape_place<scanner_target_t<self_type>, format, place>(state);
  }

  template <class self_type, std::size_t place, class state_type>
    requires(detail::turns_can_be_folded<scanner_target_t<self_type>, format>())
  constexpr void closed_group(this const self_type& self, state_type& state,
                              scan::group_at<place>) {
    using type = scanner_target_t<self_type>;
    static_cast<void>(self);
    detail::close_shape_place<type, format, place,
                              detail::shape_failure<type>>(state,
                                                           state.went_wrong);
  }

  template <class self_type, class state_type>
    requires(detail::turns_can_be_folded<scanner_target_t<self_type>, format>())
  [[nodiscard]] constexpr auto try_finish_groups(this const self_type& self,
                                                 state_type state) {
    using type = scanner_target_t<self_type>;
    static_cast<void>(self);
    using failure_type = detail::shape_failure<type>;
    if (state.went_wrong) {
      return std::expected<type, failure_type>(
          std::unexpected(std::move(*state.went_wrong)));
    }
    return detail::finish_value<type, type, 0, true, failure_type>(
        detail::gathered_by_a_fold<state_type>{state}, nullptr);
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
  template <class self_type>
    requires(!requires { &scanner<scanner_target_t<self_type>>::parse; })
  [[nodiscard]] constexpr auto begin(this const self_type& self) {
    using type = scanner_target_t<self_type>;
    static_cast<void>(self);
    return detail::stream_state<type, format, false,
                                detail::shape_failure<type>>{};
  }

  constexpr void push(this const auto&, auto& state, char value) {
    state.push(value);
  }

  // Handed back rather than thrown, both of them: this type is read by the
  // same machine everything else is, and that machine says what went wrong
  // instead of throwing it. Which means a shape used as a field of another
  // shape carries its kinds up into what that reading can fail with.
  [[nodiscard]] constexpr auto try_finish(this const auto& self, auto state) {
    static_cast<void>(self);
    return std::move(state).finish();
  }

  [[nodiscard]] constexpr auto try_parse(this const auto& self,
                                         std::string_view input) {
    auto state = self.begin();
    for (char value : input) { self.push(state, value); }
    return self.try_finish(std::move(state));
  }
};

}  // namespace scan

export namespace scan::detail {

#undef SCAN_FORCE_INLINE

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

