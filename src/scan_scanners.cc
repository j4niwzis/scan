export module scan.scanners;

import std;
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
  // A whole run of characters, which is one copy rather than one call each.
  static constexpr void push(state_type& state, std::string_view run) {
    state.append(run);
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

// A field of characters with nowhere to grow.
//
// A machine reading a range as it comes has to gather each field as it arrives,
// and gathering into a `std::string` asks an allocator for room. Where there is
// no allocator -- and where what has to be kept is small even though what has
// to be matched may not be -- the room is said in advance and the gathering
// stops at the brim rather than reaching for more.
//
// What overflows is dropped and remembered as having overflowed, because the
// alternative is either an allocation or a lie.
template <std::size_t capacity>
struct held {
  std::array<char, capacity> storage{};
  std::size_t length = 0;
  bool overflowed = false;

  [[nodiscard]] constexpr std::string_view view() const noexcept {
    return {storage.data(), length};
  }
  [[nodiscard]] constexpr operator std::string_view() const noexcept {
    return view();
  }
  constexpr void push_back(char value) {
    if (length == capacity) {
      overflowed = true;
      return;
    }
    storage[length++] = value;
  }
  constexpr void append(std::string_view run) {
    if (run.size() > capacity - length) {
      overflowed = true;
      for (std::size_t at = 0; at < capacity - length; ++at) {
        storage[length + at] = run[at];
      }
      length = capacity;
      return;
    }
    for (std::size_t at = 0; at < run.size(); ++at) {
      storage[length + at] = run[at];
    }
    length += run.size();
  }
};

template <std::size_t capacity>
struct scanner<held<capacity>> {
  [[nodiscard]] static constexpr std::string_view pattern() { return ".*"; }
  using state_type = held<capacity>;

  [[nodiscard]] static constexpr state_type begin() { return {}; }
  [[nodiscard]] static constexpr state_type begin(std::string_view) {
    return {};
  }
  static constexpr void push(state_type& state, char value) {
    state.push_back(value);
  }
  static constexpr void push(state_type& state, std::string_view run) {
    state.append(run);
  }
  [[nodiscard]] static constexpr held<capacity> finish(state_type state) {
    return state;
  }

  [[nodiscard]] static constexpr held<capacity> parse(std::string_view text) {
    held<capacity> made;
    for (char value : text) made.push_back(value);
    return made;
  }

  [[nodiscard]] static constexpr auto pattern(std::string_view parameters) {
    return scanner<std::string>::pattern(parameters);
  }

  [[nodiscard]] static constexpr held<capacity> parse(std::string_view text,
                                                      std::string_view) {
    return parse(text);
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
    bool twice = false;
  };

  [[nodiscard]] static constexpr state_type begin() { return {}; }
  [[nodiscard]] static constexpr state_type begin(std::string_view) { return {}; }
  // A push cannot say anything: the walk is not over and there is nobody to
  // say it to. It is said in the state and handed back at the end, which is
  // what every fold does.
  static constexpr void push(state_type& state, char value) {
    if (state.present) {
      state.twice = true;
      return;
    }
    state = {.value = value, .present = true};
  }
  [[nodiscard]] static constexpr std::expected<char, bad_field> try_finish(
      state_type state) {
    if (state.twice) {
      return std::unexpected(bad_field("char scanner received two symbols"));
    }
    if (!state.present) return std::unexpected(bad_field("empty char field"));
    return state.value;
  }
  [[nodiscard]] static constexpr std::expected<char, bad_field> try_parse(
      std::string_view text) {
    if (text.size() != 1) return std::unexpected(bad_field("invalid char field"));
    return text.front();
  }
  [[nodiscard]] static constexpr std::expected<char, bad_field> try_parse(
      std::string_view text, std::string_view) {
    return try_parse(text);
  }
};

template <std::integral type>
  requires(!std::same_as<type, bool>)
struct scanner<type> {
  [[nodiscard]] static constexpr std::string_view pattern() {
    if constexpr (std::unsigned_integral<type>) return std::string_view("[+]?[0-9]+");
    return std::string_view("[+-]?[0-9]+");
  }

  // The number as it is read, rather than the characters it was written with.
  //
  // A field of digits used to be kept whole and read at the end: room for as
  // many characters as the widest number can be written in, and a pass over
  // them when the field closed. The machine that reads a subject once keeps a
  // gathering per register, so that room was paid for in every one of them --
  // sixty-four bytes a register for a number that is eight.
  //
  // Digits arrive one at a time and in a known base, and what they mean is a
  // number, so the number is what is kept. What the characters were is gone as
  // they are read, and nothing is left to parse when the field closes.
  struct state_type {
    // The magnitude, which is where the range is decided: a signed type
    // reaches one further down than up, and the sign is not known until the
    // field is over anyway.
    std::uint64_t magnitude = 0;
    std::size_t digits = 0;
    std::uint8_t base = 10;
    bool automatic_base = false;
    bool negative = false;
    bool overflowed = false;
    bool refused = false;
    // What the first characters looked like, for a base that is read off them.
    bool leading_zero = false;
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
    state.base = static_cast<std::uint8_t>(spec.base);
    state.automatic_base = spec.automatic_base;
    return state;
  }

  // What a character is worth in this base, or nothing.
  [[nodiscard]] static constexpr int digit_of(char value, int base) {
    int worth = -1;
    if (value >= '0' && value <= '9') worth = value - '0';
    else if (value >= 'a' && value <= 'f') worth = value - 'a' + 10;
    else if (value >= 'A' && value <= 'F') worth = value - 'A' + 10;
    if (worth < 0 || worth >= base) return -1;
    return worth;
  }

  static constexpr void push(state_type& state, char value) {
    // The sign, which only the first character may be.
    if (state.digits == 0 && !state.leading_zero &&
        (value == '+' || value == '-')) {
      if (state.negative || state.magnitude != 0) {
        state.refused = true;
        return;
      }
      state.negative = value == '-';
      return;
    }
    // A base said by the characters themselves: `0x` for sixteen, a leading
    // zero for eight, and anything else for ten.
    if (state.leading_zero && (value == 'x' || value == 'X') &&
        (state.automatic_base || state.base == 16)) {
      state.base = 16;
      state.leading_zero = false;
      state.digits = 0;
      return;
    }
    if (state.digits == 0 && !state.leading_zero && value == '0' &&
        (state.automatic_base || state.base == 16)) {
      state.leading_zero = true;
      return;
    }
    if (state.leading_zero && state.automatic_base && state.digits == 0) {
      // A zero and then a digit: written the way a machine writes eight.
      state.base = 8;
    }
    const int worth = digit_of(value, state.base);
    if (worth < 0) {
      state.refused = true;
      return;
    }
    ++state.digits;
    const std::uint64_t base = state.base;
    constexpr std::uint64_t ceiling = std::numeric_limits<std::uint64_t>::max();
    if (state.magnitude > (ceiling - static_cast<std::uint64_t>(worth)) / base) {
      state.overflowed = true;
      return;
    }
    state.magnitude =
        state.magnitude * base + static_cast<std::uint64_t>(worth);
  }

  using went_wrong = std::variant<bad_field, out_of_range>;

  [[nodiscard]] static constexpr std::expected<type, went_wrong> try_finish(
      state_type state) {
    if (state.refused) {
      return std::unexpected(went_wrong(bad_field("invalid integer field")));
    }
    if (state.digits == 0 && !state.leading_zero) {
      return std::unexpected(went_wrong(bad_field("empty integer field")));
    }
    if (state.overflowed) {
      return std::unexpected(
          went_wrong(out_of_range("integer field is out of range")));
    }
    // Which magnitudes this type can hold, on each side of nothing.
    constexpr std::uint64_t upward =
        static_cast<std::uint64_t>(std::numeric_limits<type>::max());
    if (state.negative) {
      if constexpr (std::unsigned_integral<type>) {
        if (state.magnitude != 0) {
          return std::unexpected(
              went_wrong(out_of_range("negative value for unsigned integer")));
        }
        return type{};
      } else {
        constexpr std::uint64_t downward = upward + 1;
        if (state.magnitude > downward) {
          return std::unexpected(
              went_wrong(out_of_range("integer field is out of range")));
        }
        if (state.magnitude == downward) {
          return std::numeric_limits<type>::min();
        }
        return static_cast<type>(-static_cast<std::int64_t>(state.magnitude));
      }
    }
    if (state.magnitude > upward) {
      return std::unexpected(
          went_wrong(out_of_range("integer field is out of range")));
    }
    return static_cast<type>(state.magnitude);
  }

  [[nodiscard]] static constexpr std::expected<type, went_wrong> try_parse(
      std::string_view text) {
    return parse_integer(text, 10, false);
  }

  [[nodiscard]] static constexpr std::expected<type, went_wrong> try_parse(
      std::string_view text, std::string_view parameters) {
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

  [[nodiscard]] static constexpr std::expected<type, went_wrong> parse_integer(
      std::string_view text, int selected_base, bool automatic_base) {
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
    // The magnitude, and the sign applied to it afterwards.
    //
    // Read into the type itself, the most negative value a type can hold is
    // refused: its magnitude is one past what the type can hold the other way
    // up, and it is only reachable through the sign. The walk that gathers a
    // number as it reads has always to keep the magnitude apart from the sign,
    // because the sign is known first and the range only at the end -- so this
    // is the same rule said in the same way.
    std::uint64_t magnitude = 0;
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), magnitude, selected_base);
    if (error == std::errc::result_out_of_range) {
      return std::unexpected(
          went_wrong(out_of_range("integer field is out of range")));
    }
    if (error != std::errc{} || end != text.data() + text.size()) {
      return std::unexpected(went_wrong(bad_field("invalid integer field")));
    }
    constexpr std::uint64_t upward =
        static_cast<std::uint64_t>(std::numeric_limits<type>::max());
    if (negative) {
      if constexpr (std::unsigned_integral<type>) {
        if (magnitude != 0) {
          return std::unexpected(
              went_wrong(out_of_range("negative value for unsigned integer")));
        }
        return type{};
      } else {
        constexpr std::uint64_t downward = upward + 1;
        if (magnitude > downward) {
          return std::unexpected(
              went_wrong(out_of_range("integer field is out of range")));
        }
        if (magnitude == downward) return std::numeric_limits<type>::min();
        return static_cast<type>(-static_cast<std::int64_t>(magnitude));
      }
    }
    if (magnitude > upward) {
      return std::unexpected(
          went_wrong(out_of_range("integer field is out of range")));
    }
    return static_cast<type>(magnitude);
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

  using went_wrong = std::variant<bad_field, out_of_range>;

  [[nodiscard]] static constexpr std::expected<type, went_wrong> try_finish(
      state_type state) {
    if (state.overflow) {
      return std::unexpected(
          went_wrong(out_of_range("floating-point field is too long")));
    }
    return parse_floating(std::string_view(state.buffer.data(), state.size),
                          state.format);
  }

  [[nodiscard]] static constexpr std::expected<type, went_wrong> try_parse(
      std::string_view text) {
    return parse_floating(text, std::chars_format::general);
  }

  [[nodiscard]] static constexpr std::expected<type, went_wrong> try_parse(
      std::string_view text, std::string_view parameters) {
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

  [[nodiscard]] static constexpr std::expected<type, went_wrong> parse_floating(
      std::string_view text, std::chars_format format) {
    if (format == std::chars_format::hex &&
        (text.starts_with("0x") || text.starts_with("0X"))) {
      text.remove_prefix(2);
    }
    type value{};
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value, format);
    if (error == std::errc::result_out_of_range) {
      return std::unexpected(
          went_wrong(out_of_range("floating-point field is out of range")));
    }
    if (error != std::errc{} || end != text.data() + text.size()) {
      return std::unexpected(
          went_wrong(bad_field("invalid floating-point field")));
    }
    return value;
  }
};

template <>
struct scanner<bool> {
  static constexpr std::string_view pattern = "(?:true|false|1|0)";
  [[nodiscard]] static constexpr std::expected<bool, bad_field> try_parse(
      std::string_view text) {
    if (text == "true" || text == "1") return true;
    if (text == "false" || text == "0") return false;
    return std::unexpected(bad_field("invalid boolean field"));
  }
};

template <class type>
  requires std::is_enum_v<type>
struct scanner<type> {
  using underlying_type = std::underlying_type_t<type>;
  [[nodiscard]] static constexpr std::string_view pattern() {
    return scanner_pattern<underlying_type>();
  }
  [[nodiscard]] static constexpr auto try_parse(std::string_view text)
      -> std::expected<type,
                       typename scanner<underlying_type>::went_wrong> {
    auto got = scanner<underlying_type>::try_parse(text);
    if (!got) return std::unexpected(std::move(got).error());
    return static_cast<type>(*got);
  }
};

namespace detail {

}  // namespace detail

}  // namespace scan
