export module scan.range;

import std;
import boost.pfr;
export import scan.runtime;

export namespace scan::detail {

template <class type>
[[nodiscard]] constexpr type parse_value(std::string_view text,
                                         std::string_view parameters) {
  using value_type = std::remove_cv_t<type>;
  static_assert(requires { scanner_parse<value_type>(text); },
                "scan::scanner<type> must provide parse(string_view)");
  return scanner_parse<value_type>(text, parameters);
}

// A leaf is read from its one group; a product is built from its fields, each
// of which takes as many groups as it needs, in order. Nothing about the
// nesting is written in the format: a structure of structures is spelled out
// flat, because a product of products is flat.
// The parameters come from the same spread the automaton was built from, so a
// colon written at the place a value is read from reaches that value however
// deeply it sits, and a format declared by a type is read against that type
// here exactly as it was there.
template <class root, class type, fixed_string format, std::size_t offset,
          std::size_t extent>
[[nodiscard]] constexpr type build_value(
    const std::array<std::string_view, extent>& groups) {
  if constexpr (scanned_as_leaf<type>) {
    static constexpr auto spread = spread_of<root, format>();
    return parse_value<std::remove_cv_t<type>>(
        groups[offset], spread.parameters[offset].view());
  } else {
    return [&]<std::size_t... index>(std::index_sequence<index...>) {
      return type{build_value<
          root,
          std::remove_cvref_t<boost::pfr::tuple_element_t<index, type>>, format,
          offset + groups_before_field<type, index>()>(groups)...};
    }(std::make_index_sequence<boost::pfr::tuple_size_v<type>>{});
  }
}
template <class type, fixed_string format, std::size_t extent, std::size_t... index>
[[nodiscard]] constexpr type convert(
    const std::array<std::string_view, extent>& fields,
    std::index_sequence<index...>) {
  static_assert(groups_of<type>() == extent,
                "placeholder count must equal the number of values the output "
                "type reads");
  // Built, not built empty and then written over. The aggregate used to be
  // default-constructed and each field assigned a temporary afterwards, which
  // for a field that owns storage is a construction, a move-assignment that
  // must first ask whether the destination is holding any, and a destruction --
  // three times what initialising it once costs. It also demanded that every
  // field be default-constructible and assignable, which is more than an
  // aggregate has to be.
  return build_value<type, type, format, 0>(fields);
}

template <class type, std::size_t branch>
[[nodiscard]] consteval std::size_t groups_before_branch() {
  constexpr auto counts = fields_of_each_alternative<type>();
  std::size_t before = 0;
  for (std::size_t index = 0; index < branch; ++index) before += counts[index];
  return before;
}

template <fixed_string format, int sentinel = -1, bool terminated = false>
class borrowed_result {
 public:
  constexpr explicit borrowed_result(std::string_view input) : input_(input) {}

  template <class type>
    requires std::is_aggregate_v<type>
  constexpr operator type() const {
    static_assert(!scanned_by_format<type>,
                  "a type that declares its own format is read as a field, not "
                  "as the whole of what is scanned into: wrap it in a struct "
                  "with one member and scan into that");
    const auto fields = scan_fields<type, format, sentinel, terminated>(input_);
    return convert<type, format>(fields,
                                 std::make_index_sequence<groups_of<type>()>{});
  }

  // The same scan, for a format that says the input may be one of several
  // shapes. Which branch ran is read from the group each branch was wrapped in:
  // exactly one of them took part, and the others point nowhere.
  template <class type>
    requires scanned_as_variant<type>
  constexpr operator type() const {
    constexpr std::size_t branches = std::variant_size_v<type>;
    constexpr std::size_t total = fields_of_all_alternatives<type>();
    const auto groups =
        scan_branch_fields<type, format, sentinel, terminated>(input_);
    return [&]<std::size_t... branch>(std::index_sequence<branch...>) -> type {
      std::optional<type> made;
      const auto take = [&]<std::size_t which>() {
        if (made || groups[total + which].data() == nullptr) return false;
        using alternative = std::variant_alternative_t<which, type>;
        made.emplace(std::in_place_index<which>,
                     build_value<type, alternative, format,
                                 groups_before_branch<type, which>()>(groups));
        return true;
      };
      (void)(take.template operator()<branch>() || ...);
      if (!made) throw scan_error("no branch of the format took the input");
      return std::move(*made);
    }(std::make_index_sequence<branches>{});
  }

 private:
  std::string_view input_;
};

template <fixed_string format, std::ranges::input_range range_type>
class streaming_result {
 public:
  constexpr explicit streaming_result(range_type input)
      : input_(std::move(input)) {}

  streaming_result(streaming_result&&) = default;
  streaming_result& operator=(streaming_result&&) = default;
  streaming_result(const streaming_result&) = delete;
  streaming_result& operator=(const streaming_result&) = delete;

  template <class type>
    requires std::is_aggregate_v<type>
  constexpr operator type() {
    return scan_stream<type, format>(input_);
  }

 private:
  range_type input_;
};

template <class range_type>
concept contiguous_char_range =
    std::ranges::contiguous_range<range_type> &&
    std::ranges::sized_range<range_type> &&
    std::same_as<std::ranges::range_value_t<range_type>, char>;


}  // namespace scan::detail

export namespace scan {

// Whether the type of the input promises a terminator past its last character.
//
// A `std::string` does, and has since the language said `data()` and `c_str()`
// are the same thing: there is a null character at `data() + size()`. A string
// literal does. A `string_view` into the middle of something does not, and
// neither does a vector of characters.
template <class range_type>
concept terminated_char_range =
    detail::contiguous_char_range<range_type> &&
    (std::same_as<std::remove_cvref_t<range_type>, std::string> ||
     std::same_as<std::remove_cvref_t<range_type>,
                  std::basic_string<char, std::char_traits<char>,
                                    std::allocator<char>>>);

// This overload borrows the original contiguous storage and allocates nothing.
//
// Where the input is of a type that carries a terminator, this says so, and the
// scan then runs the loop that tests only the character and not the end of the
// input as well -- provided the pattern is one the terminator cannot appear in,
// which is asked while the pattern is compiled and answered without the caller
// having to know it was asked.
template <fixed_string format, detail::contiguous_char_range range_type>
  requires(std::is_lvalue_reference_v<range_type&&> || std::ranges::borrowed_range<range_type>)
[[nodiscard]] constexpr auto scan(range_type&& input) {
  return detail::borrowed_result<format, -1, terminated_char_range<range_type>>(
      std::string_view(std::ranges::data(input), std::ranges::size(input)));
}

// The same, for input that carries a terminator the pattern never matches.
//
// Without one the loop tests the end of the input on every character as well
// as the character itself; with one the terminator fails the class test like
// any other symbol no transition takes. It is the caller's promise that the
// terminator is there -- a `std::string` always has it, a `string_view` into
// the middle of something does not.
template <fixed_string format, int sentinel = 0,
          detail::contiguous_char_range range_type>
  requires(std::is_lvalue_reference_v<range_type&&> || std::ranges::borrowed_range<range_type>)
[[nodiscard]] constexpr auto scan_sentinel(range_type&& input) {
  return detail::borrowed_result<format, sentinel>(std::string_view(
      std::ranges::data(input), std::ranges::size(input)));
}

// The proxy owns the view and consumes it once after the output type is known.
template <fixed_string format, std::ranges::input_range range_type>
  requires std::same_as<std::ranges::range_value_t<range_type>, char> &&
           (!detail::contiguous_char_range<range_type> ||
            (!std::is_lvalue_reference_v<range_type&&> &&
             !std::ranges::borrowed_range<range_type>))
[[nodiscard]] constexpr auto scan(range_type&& input) {
  auto view = std::views::all(std::forward<range_type>(input));
  return detail::streaming_result<format, decltype(view)>(std::move(view));
}

// Stream-buffer iteration is unformatted and therefore preserves whitespace
// regardless of the stream's skipws formatting flag.
template <fixed_string format>
[[nodiscard]] constexpr auto scan(std::istream& input) {
  auto range = std::ranges::subrange(std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>());
  return detail::streaming_result<format, decltype(range)>(std::move(range));
}

template <class type, fixed_string format, std::ranges::input_range range_type>
[[nodiscard]] constexpr type scan_as(range_type&& input) {
  return static_cast<type>(scan<format>(std::forward<range_type>(input)));
}

template <class type, fixed_string format>
[[nodiscard]] constexpr type scan_as(std::istream& input) {
  return static_cast<type>(scan<format>(input));
}

}  // namespace scan
