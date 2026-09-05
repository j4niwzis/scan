export module scan.scanners;

import std;
import boost.pfr;
export import scan.range;

export namespace scan {

namespace detail {

struct conversion_spec {
  std::size_t width = 0;
  std::string_view conversion;
};

[[nodiscard]] constexpr conversion_spec parse_conversion_spec(
    std::string_view parameters) {
  const auto digits = parameters | std::views::take_while([](char value) {
                        return value >= '0' && value <= '9';
                      });
  const auto width_length = static_cast<std::size_t>(std::ranges::distance(digits));
  std::size_t width = 0;
  for (char value : parameters.substr(0, width_length)) {
    width = width * 10 + static_cast<std::size_t>(value - '0');
  }
  if (width_length != 0 && width == 0) throw "scan width must be positive";
  return {.width = width, .conversion = parameters.substr(width_length)};
}

constexpr void append_number(pattern_buffer<>& output, std::size_t value) {
  std::array<char, std::numeric_limits<std::size_t>::digits10 + 1> digits{};
  const auto [end, error] = std::to_chars(digits.data(),
                                         digits.data() + digits.size(), value);
  if (error != std::errc{}) throw "scan width is too large";
  output.append(std::string_view(digits.data(), end));
}

constexpr void append_repetition(pattern_buffer<>& output,
                                 std::size_t minimum, std::size_t maximum,
                                 bool exact = false) {
  output.push_back('{');
  append_number(output, exact ? maximum : minimum);
  if (!exact) {
    output.push_back(',');
    if (maximum != 0) append_number(output, maximum);
  }
  output.push_back('}');
}

}  // namespace detail

template <>
struct scanner<std::string> {
  [[nodiscard]] static constexpr std::string_view pattern() { return ".*"; }
  using state_type = std::string;

  [[nodiscard]] static constexpr state_type begin() { return {}; }
  [[nodiscard]] static constexpr state_type begin(std::string_view) { return {}; }
  static constexpr void push(state_type& state, char value) {
    state.push_back(value);
  }
  [[nodiscard]] static constexpr std::string finish(state_type state) {
    return state;
  }

  [[nodiscard]] static constexpr std::string parse(std::string_view text) {
    return std::string(text);
  }

  [[nodiscard]] static constexpr pattern_buffer<> pattern(
      std::string_view parameters) {
    const auto spec = detail::parse_conversion_spec(parameters);
    pattern_buffer<> result;
    if (spec.conversion.empty()) {
      result.append(".*");
    } else if (spec.conversion == "s") {
      result.append("[^ \t\n\r\f\v]");
      detail::append_repetition(result, 1, spec.width);
    } else if (spec.conversion == "c") {
      result.push_back('.');
      detail::append_repetition(result, 1, spec.width == 0 ? 1 : spec.width,
                                true);
    } else if (spec.conversion.starts_with('[') &&
               spec.conversion.ends_with(']')) {
      result.append(spec.conversion);
      detail::append_repetition(result, 1, spec.width);
    } else {
      throw "unsupported string scanner parameters";
    }
    return result;
  }

  [[nodiscard]] static constexpr std::string parse(
      std::string_view text, std::string_view) {
    return std::string(text);
  }
};

template <>
struct scanner<std::string_view> {
  [[nodiscard]] static constexpr std::string_view pattern() { return ".*"; }
  [[nodiscard]] static constexpr std::string_view parse(
      std::string_view text) {
    return text;
  }

  [[nodiscard]] static constexpr auto pattern(std::string_view parameters) {
    return scanner<std::string>::pattern(parameters);
  }

  [[nodiscard]] static constexpr std::string_view parse(
      std::string_view text, std::string_view) {
    return text;
  }
};

template <>
struct scanner<char> {
  [[nodiscard]] static constexpr std::string_view pattern() { return "."; }

  [[nodiscard]] static constexpr pattern_buffer<> pattern(
      std::string_view parameters) {
    const auto spec = detail::parse_conversion_spec(parameters);
    if (!spec.conversion.empty() && spec.conversion != "c") {
      throw "char scanner requires the c conversion";
    }
    if (spec.width > 1) throw "a char field cannot hold a wide c conversion";
    pattern_buffer<> result;
    result.push_back('.');
    return result;
  }

  struct state_type {
    char value = '\0';
    bool present = false;
  };

  [[nodiscard]] static constexpr state_type begin() { return {}; }
  [[nodiscard]] static constexpr state_type begin(std::string_view) { return {}; }
  static constexpr void push(state_type& state, char value) {
    if (state.present) throw scan_error("char scanner received multiple symbols");
    state = {.value = value, .present = true};
  }
  [[nodiscard]] static constexpr char finish(state_type state) {
    if (!state.present) throw scan_error("empty char field");
    return state.value;
  }
  [[nodiscard]] static constexpr char parse(std::string_view text) {
    if (text.size() != 1) throw scan_error("invalid char field");
    return text.front();
  }
  [[nodiscard]] static constexpr char parse(std::string_view text,
                                            std::string_view) {
    return parse(text);
  }
};

template <std::integral type>
  requires(!std::same_as<type, bool>)
struct scanner<type> {
  [[nodiscard]] static constexpr std::string_view pattern() {
    if constexpr (std::unsigned_integral<type>) return std::string_view("[+]?[0-9]+");
    return std::string_view("[+-]?[0-9]+");
  }

  struct state_type {
    static constexpr std::size_t capacity =
        static_cast<std::size_t>(std::numeric_limits<type>::digits) + 4;
    std::array<char, capacity> buffer{};
    std::size_t size = 0;
    bool overflow = false;
    int base = 10;
    bool automatic_base = false;
  };

  [[nodiscard]] static constexpr state_type begin() { return {}; }

  [[nodiscard]] static constexpr pattern_buffer<> pattern(
      std::string_view parameters) {
    const auto spec = integer_spec(parameters);
    pattern_buffer<> result;
    result.append(std::unsigned_integral<type> ? "[+]?" : "[+-]?");
    if (spec.automatic_base) {
      result.append("(?:0[xX][0-9A-Fa-f]+|0[0-7]*|[1-9][0-9]*)");
      return result;
    }
    if (spec.base == 16) result.append("(?:0[xX])?[0-9A-Fa-f]");
    else if (spec.base == 8) result.append("[0-7]");
    else if (spec.base == 2) result.append("[01]");
    else result.append("[0-9]");
    detail::append_repetition(result, 1, spec.width);
    return result;
  }

  [[nodiscard]] static constexpr state_type begin(
      std::string_view parameters) {
    state_type state;
    const auto spec = integer_spec(parameters);
    state.base = spec.base;
    state.automatic_base = spec.automatic_base;
    return state;
  }

  static constexpr void push(state_type& state, char value) {
    if (state.size == state.buffer.size()) {
      state.overflow = true;
      return;
    }
    state.buffer[state.size++] = value;
  }

  [[nodiscard]] static constexpr type finish(state_type state) {
    if (state.overflow) {
      throw scan_error("integer field is out of range");
    }
    return parse_integer(std::string_view(state.buffer.data(), state.size),
                         state.base, state.automatic_base);
  }

  [[nodiscard]] static constexpr type parse(std::string_view text) {
    type value{};
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
      throw scan_error("invalid integer field");
    }
    return value;
  }


  [[nodiscard]] static constexpr type parse(std::string_view text,
                                            std::string_view parameters) {
    const auto spec = integer_spec(parameters);
    return parse_integer(text, spec.base, spec.automatic_base);
  }

 private:
  struct parsed_integer_spec {
    std::size_t width = 0;
    int base = 10;
    bool automatic_base = false;
  };

  [[nodiscard]] static constexpr parsed_integer_spec integer_spec(
      std::string_view parameters) {
    const auto spec = detail::parse_conversion_spec(parameters);
    const auto conversion = spec.conversion;
    if (conversion.empty() || conversion == "d" || conversion == "u" ||
        conversion == "decimal") return {.width = spec.width, .base = 10};
    if (conversion == "i") {
      return {.width = spec.width, .base = 10, .automatic_base = true};
    }
    if (conversion == "x" || conversion == "X" || conversion == "hex") {
      return {.width = spec.width, .base = 16};
    }
    if (conversion == "o" || conversion == "octal") {
      return {.width = spec.width, .base = 8};
    }
    if (conversion == "b" || conversion == "binary") {
      return {.width = spec.width, .base = 2};
    }
    throw "unsupported integer scanner parameters";
  }

  [[nodiscard]] static constexpr type parse_integer(std::string_view text,
                                                     int selected_base,
                                                     bool automatic_base) {
    bool negative = false;
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
      negative = text.front() == '-';
      text.remove_prefix(1);
    }
    if (automatic_base) {
      if (text.starts_with("0x") || text.starts_with("0X")) {
        selected_base = 16;
        text.remove_prefix(2);
      } else if (text.size() > 1 && text.front() == '0') {
        selected_base = 8;
      } else {
        selected_base = 10;
      }
    } else if (selected_base == 16 &&
               (text.starts_with("0x") || text.starts_with("0X"))) {
      text.remove_prefix(2);
    }
    type value{};
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value, selected_base);
    if (error != std::errc{} || end != text.data() + text.size()) {
      throw scan_error("invalid integer field");
    }
    if (negative) {
      if constexpr (std::unsigned_integral<type>) {
        throw scan_error("negative value for unsigned integer");
      } else {
        value = static_cast<type>(-value);
      }
    }
    return value;
  }
};

template <std::floating_point type>
struct scanner<type> {
  [[nodiscard]] static constexpr std::string_view pattern() {
    return "[+-]?([0-9]+(\\.[0-9]*)?|\\.[0-9]+)([eE][+-]?[0-9]+)?";
  }

  struct state_type {
    std::array<char, 128> buffer{};
    std::size_t size = 0;
    bool overflow = false;
    std::chars_format format = std::chars_format::general;
  };

  [[nodiscard]] static constexpr pattern_buffer<> pattern(
      std::string_view parameters) {
    const auto spec = floating_spec(parameters);
    pattern_buffer<> result;
    if (spec.width != 0) {
      result.append(spec.format == std::chars_format::hex
                        ? "[+\\-0-9A-Fa-fXxPp.]"
                        : "[+\\-0-9Ee.]");
      detail::append_repetition(result, 1, spec.width);
    } else if (spec.format == std::chars_format::hex) {
      result.append(
          "[+-]?0[xX]([0-9A-Fa-f]+(\\.[0-9A-Fa-f]*)?|"
          "\\.[0-9A-Fa-f]+)[pP][+-]?[0-9]+");
    } else {
      result.append(pattern());
    }
    return result;
  }

  [[nodiscard]] static constexpr state_type begin(std::string_view parameters) {
    return {.format = floating_spec(parameters).format};
  }

  static constexpr void push(state_type& state, char value) {
    if (state.size == state.buffer.size()) {
      state.overflow = true;
      return;
    }
    state.buffer[state.size++] = value;
  }

  [[nodiscard]] static constexpr type finish(state_type state) {
    if (state.overflow) throw scan_error("floating-point field is too long");
    return parse_floating(std::string_view(state.buffer.data(), state.size),
                          state.format);
  }

  [[nodiscard]] static constexpr type parse(std::string_view text) {
    return parse_floating(text, std::chars_format::general);
  }

  [[nodiscard]] static constexpr type parse(std::string_view text,
                                            std::string_view parameters) {
    return parse_floating(text, floating_spec(parameters).format);
  }

 private:
  struct parsed_floating_spec {
    std::size_t width = 0;
    std::chars_format format = std::chars_format::general;
  };

  [[nodiscard]] static constexpr parsed_floating_spec floating_spec(
      std::string_view parameters) {
    const auto spec = detail::parse_conversion_spec(parameters);
    if (spec.conversion.empty() ||
        (spec.conversion.size() == 1 &&
         std::string_view("fFgG").contains(spec.conversion.front()))) {
      return {.width = spec.width, .format = std::chars_format::general};
    }
    if (spec.conversion.size() == 1 &&
        std::string_view("eE").contains(spec.conversion.front())) {
      return {.width = spec.width, .format = std::chars_format::scientific};
    }
    if (spec.conversion.size() == 1 &&
        std::string_view("aA").contains(spec.conversion.front())) {
      return {.width = spec.width, .format = std::chars_format::hex};
    }
    throw "unsupported floating-point scanner parameters";
  }

  [[nodiscard]] static constexpr type parse_floating(
      std::string_view text, std::chars_format format) {
    if (format == std::chars_format::hex &&
        (text.starts_with("0x") || text.starts_with("0X"))) {
      text.remove_prefix(2);
    }
    type value{};
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value, format);
    if (error != std::errc{} || end != text.data() + text.size()) {
      throw scan_error("invalid floating-point field");
    }
    return value;
  }
};

template <>
struct scanner<bool> {
  static constexpr std::string_view pattern = "(?:true|false|1|0)";
  [[nodiscard]] static constexpr bool parse(std::string_view text) {
    if (text == "true" || text == "1") return true;
    if (text == "false" || text == "0") return false;
    throw scan_error("invalid boolean field");
  }
};

template <class type>
  requires std::is_enum_v<type>
struct scanner<type> {
  using underlying_type = std::underlying_type_t<type>;
  [[nodiscard]] static constexpr std::string_view pattern() {
    return scanner_pattern<underlying_type>();
  }
  [[nodiscard]] static constexpr type parse(std::string_view text) {
    return static_cast<type>(scanner<underlying_type>::parse(text));
  }
};

namespace detail {

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

template <class type, fixed_string format, std::size_t field_count>
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
    append_aggregate_pattern<type, format>(output, defaults, position + 2,
                                           field);
    return;
  }
  if (text[position] != '{') {
    append_literal(output, text[position]);
    append_aggregate_pattern<type, format>(output, defaults, position + 1,
                                           field);
    return;
  }
  if (field == field_count) throw "too many aggregate captures";
  const std::size_t end = find_capture_end(text, position + 1);
  output.append("(?:");
  if (end == position + 1 || text[position + 1] == ':') {
    output.append(defaults[field]);
  } else {
    output.append(text.substr(position + 1, end - position - 1));
  }
  output.push_back(')');
  append_aggregate_pattern<type, format>(output, defaults, end + 1,
                                         field + 1);
}

template <class type, fixed_string format>
[[nodiscard]] consteval pattern_buffer<> make_aggregate_pattern() {
  constexpr std::size_t field_count = boost::pfr::tuple_size_v<type>;
  constexpr auto parameters = field_parameters<format, field_count>();
  constexpr auto pattern_storage = parameterized_patterns<type>(
      parameters, std::make_index_sequence<field_count>{});
  const auto defaults = pattern_views(pattern_storage);
  pattern_buffer<> output;
  append_aggregate_pattern<type, format>(output, defaults);
  return output;
}

}  // namespace detail

template <fixed_string format>
struct aggregate_scanner {
  // The format, said out loud, so that whatever reads this type can read it as
  // a shape and not as a value: the places below stand for this type's fields,
  // and a scan that knows that spreads them into its own automaton instead of
  // matching the whole thing and taking it apart again afterwards. The members
  // beneath still do the taking apart, for the paths that cannot spread -- an
  // input that is read once and not looked at twice.
  static constexpr auto scan_format = format;

  [[nodiscard]] constexpr auto pattern(this const auto& self) {
    using type = scanner_target_t<decltype(self)>;
    return detail::make_aggregate_pattern<type, format>();
  }

  [[nodiscard]] constexpr auto begin(this const auto& self) {
    using type = scanner_target_t<decltype(self)>;
    return detail::stream_state<type, format>{};
  }

  constexpr void push(this const auto&, auto& state, char value) {
    state.push(value);
  }

  [[nodiscard]] constexpr auto finish(this const auto& self, auto state) {
    static_cast<void>(self);
    return std::move(state).finish();
  }

  [[nodiscard]] constexpr auto parse(this const auto& self,
                                     std::string_view input) {
    auto state = self.begin();
    for (char value : input) { self.push(state, value); }
    return self.finish(std::move(state));
  }
};

}  // namespace scan
