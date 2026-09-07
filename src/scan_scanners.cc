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

  using went_wrong = std::variant<bad_field, out_of_range>;

  [[nodiscard]] static constexpr std::expected<type, went_wrong> try_finish(
      state_type state) {
    if (state.overflow) {
      return std::unexpected(
          went_wrong(out_of_range("integer field is out of range")));
    }
    return parse_integer(std::string_view(state.buffer.data(), state.size),
                         state.base, state.automatic_base);
  }

  [[nodiscard]] static constexpr std::expected<type, went_wrong> try_parse(
      std::string_view text) {
    type value{};
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error == std::errc::result_out_of_range) {
      return std::unexpected(
          went_wrong(out_of_range("integer field is out of range")));
    }
    if (error != std::errc{} || end != text.data() + text.size()) {
      return std::unexpected(went_wrong(bad_field("invalid integer field")));
    }
    return value;
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
    type value{};
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value, selected_base);
    if (error == std::errc::result_out_of_range) {
      return std::unexpected(
          went_wrong(out_of_range("integer field is out of range")));
    }
    if (error != std::errc{} || end != text.data() + text.size()) {
      return std::unexpected(went_wrong(bad_field("invalid integer field")));
    }
    if (negative) {
      if constexpr (std::unsigned_integral<type>) {
        return std::unexpected(
            went_wrong(out_of_range("negative value for unsigned integer")));
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

template <fixed_string format>
struct aggregate_scanner {
  // The format, said out loud, so that whatever reads this type can read it as
  // a shape and not as a value.
  //
  // What is done with it depends on the shape. A shape made only of places is
  // read from its own groups, below, which is a thing any type can say for
  // itself -- so this library does nothing for it that you could not. A shape
  // with a list or a choice in it is made of turns rather than of places, and
  // those are spread into the automaton around it, which is the one thing here
  // that is still a privilege.
  static constexpr auto scan_format = format;

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
  // A shape with a list inside says no. A list is made of turns, and the
  // positions a match leaves behind hold the last turn and nothing before it,
  // so such a shape keeps the road that spreads its places into the automaton
  // around it.
  [[nodiscard]] constexpr bool reads_its_groups(this const auto& self) {
    using type = scanner_target_t<decltype(self)>;
    static_cast<void>(self);
    // A shape made by the call it named says no: its places stand for the
    // arguments of that call and not for fields anybody can look at, so there
    // is nothing here to hand groups to.
    if constexpr (requires { &scanner<type>::parse; }) {
      return false;
    } else if constexpr (!detail::says_a_list_inside<type>()) {
      // Made only of places: handed its groups when the match is over.
      return true;
    } else {
      // Made of turns as well: told its groups as they happen, where every
      // place is something that can be told characters.
      return detail::turns_can_be_folded<type, format>();
    }
  }

  // The shape, out of the groups its pattern opened.
  //
  // Its groups are its places, so what builds it from them is what builds it
  // from the places of the reading around it: one builder, and this is a call
  // to it.
  template <class self_type>
    requires(!requires { &scanner<scanner_target_t<self_type>>::parse; } &&
             !detail::says_a_list_inside<scanner_target_t<self_type>>())
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

  // Told its groups as they happen, for a shape whose places take turns.
  //
  // A list is made of turns and the positions a match leaves behind hold the
  // last turn and nothing before it, so such a shape cannot be handed its
  // groups at the end. It is told them instead: every place gathers into the
  // reader of the value it stands for, a turn ends where the list's own group
  // closes, and the element is put together there and added -- by the same
  // builder that puts together everything else, asked for the gatherings a
  // different way.
  template <class self_type>
    requires(detail::says_a_list_inside<scanner_target_t<self_type>>() &&
             !requires { &scanner<scanner_target_t<self_type>>::parse; } &&
             detail::turns_can_be_folded<scanner_target_t<self_type>, format>())
  [[nodiscard]] constexpr auto begin_groups(this const self_type& self) {
    static_cast<void>(self);
    return detail::shape_turns<scanner_target_t<self_type>, format>{};
  }

  template <class self_type, std::size_t place, class state_type>
    requires(detail::says_a_list_inside<scanner_target_t<self_type>>())
  constexpr void push_group(this const self_type& self, state_type& state,
                            scan::group_at<place>, char letter) {
    static_cast<void>(self);
    detail::push_shape_place<scanner_target_t<self_type>, format, place>(
        state, letter);
  }

  template <class self_type, std::size_t place, class state_type>
    requires(detail::says_a_list_inside<scanner_target_t<self_type>>())
  constexpr void opened_group(this const self_type& self, state_type& state,
                              scan::group_at<place>) {
    static_cast<void>(self);
    detail::open_shape_place<scanner_target_t<self_type>, format, place>(state);
  }

  template <class self_type, std::size_t place, class state_type>
    requires(detail::says_a_list_inside<scanner_target_t<self_type>>())
  constexpr void closed_group(this const self_type& self, state_type& state,
                              scan::group_at<place>) {
    using type = scanner_target_t<self_type>;
    static_cast<void>(self);
    detail::close_shape_place<type, format, place,
                              detail::shape_failure<type>>(state,
                                                           state.went_wrong);
  }

  template <class self_type, class state_type>
    requires(detail::says_a_list_inside<scanner_target_t<self_type>>())
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

  [[nodiscard]] constexpr auto begin(this const auto& self) {
    using type = scanner_target_t<decltype(self)>;
    static_assert(
        !requires { &scanner<type>::parse; },
        "a type made by the call it named is read by spreading its format into "
        "the automaton, which a range that is read once is not scanned by: "
        "read it from something contiguous, or give the type a scanner that "
        "gathers it a character at a time");
    // The list of what can go wrong is built from this shape's fields and
    // never from the shape: what the shape says it hands back is that very
    // list, and a list that asked the shape would be asking its own answer.
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
