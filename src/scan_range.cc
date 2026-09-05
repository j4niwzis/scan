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

template <class type, fixed_string format, std::size_t extent, std::size_t... index>
[[nodiscard]] constexpr type convert(
    const std::array<std::string_view, extent>& fields,
    std::index_sequence<index...>) {
  static_assert(boost::pfr::tuple_size_v<type> == extent,
                "placeholder count must equal aggregate field count");
  type result{};
  constexpr auto parameters = field_parameters<format, extent>();
  ((boost::pfr::get<index>(result) =
        parse_value<std::remove_cvref_t<decltype(boost::pfr::get<index>(result))>>(
            fields[index], parameters[index])),
   ...);
  return result;
}

template <fixed_string format, int sentinel = -1>
class borrowed_result {
 public:
  constexpr explicit borrowed_result(std::string_view input) : input_(input) {}

  template <class type>
    requires std::is_aggregate_v<type>
  constexpr operator type() const {
    const auto fields = scan_fields<type, format, sentinel>(input_);
    return convert<type, format>(fields,
                      std::make_index_sequence<boost::pfr::tuple_size_v<type>>{});
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

// This overload borrows the original contiguous storage and allocates nothing.
template <fixed_string format, detail::contiguous_char_range range_type>
  requires(std::is_lvalue_reference_v<range_type&&> || std::ranges::borrowed_range<range_type>)
[[nodiscard]] constexpr auto scan(range_type&& input) {
  return detail::borrowed_result<format>(std::string_view(
      std::ranges::data(input), std::ranges::size(input)));
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
