export module scan.core;

import std;

export namespace scan {

template <std::size_t extent>
struct fixed_string {
  char value[extent]{};

  consteval fixed_string(const char (&text)[extent]) { std::copy_n(text, extent, value); }

  [[nodiscard]] static consteval std::size_t size() { return extent - 1; }
  [[nodiscard]] constexpr char operator[](std::size_t index) const {
    return value[index];
  }
  [[nodiscard]] constexpr std::string_view view() const {
    return {value, size()};
  }
};

template <class type>
struct scanner;

template <class customization>
struct scanner_target;

template <class type>
struct scanner_target<scanner<type>> {
  using type_t = type;
};

template <class customization>
using scanner_target_t = typename scanner_target<
    std::remove_cvref_t<customization>>::type_t;

template <class type>
inline constexpr auto scanner_pattern_value = [] {
  constexpr scanner<type> customization;
  if constexpr (requires { customization.pattern(); }) {
    return customization.pattern();
  } else {
    return customization.pattern;
  }
}();

template <class type>
[[nodiscard]] constexpr const auto& scanner_pattern() {
  return scanner_pattern_value<type>;
}

template <class type>
[[nodiscard]] constexpr auto scanner_pattern(std::string_view parameters) {
  constexpr scanner<type> customization;
  if constexpr (requires { customization.pattern(parameters); }) {
    return customization.pattern(parameters);
  } else {
    if (!parameters.empty()) throw "scanner does not accept parameters";
    return scanner_pattern<type>();
  }
}

template <class type>
[[nodiscard]] constexpr auto scanner_begin() {
  return scanner<type>{}.begin();
}

template <class type>
[[nodiscard]] constexpr auto scanner_begin(std::string_view parameters) {
  if constexpr (requires { scanner<type>{}.begin(parameters); }) {
    return scanner<type>{}.begin(parameters);
  } else {
    if (!parameters.empty()) throw "scanner does not accept parameters";
    return scanner_begin<type>();
  }
}

template <class type, class state_type>
constexpr void scanner_push(state_type& state, char value) {
  scanner<type>{}.push(state, value);
}

template <class type, class state_type>
[[nodiscard]] constexpr type scanner_finish(state_type state) {
  return scanner<type>{}.finish(std::move(state));
}

template <class type>
[[nodiscard]] constexpr type scanner_parse(std::string_view input) {
  return scanner<type>{}.parse(input);
}

template <class type>
[[nodiscard]] constexpr type scanner_parse(std::string_view input,
                                           std::string_view parameters) {
  if constexpr (requires { scanner<type>{}.parse(input, parameters); }) {
    return scanner<type>{}.parse(input, parameters);
  } else {
    if (!parameters.empty()) throw "scanner does not accept parameters";
    return scanner_parse<type>(input);
  }
}

template <class type>
using scanner_state_t = decltype(scanner_begin<type>());

template <std::size_t capacity = 8192>
struct pattern_buffer {
  std::array<char, capacity> storage{};
  std::size_t length = 0;

  constexpr void push_back(char value) {
    if (length == capacity) throw "scanner pattern is too large";
    storage[length++] = value;
  }

  constexpr void append(std::string_view value) {
    std::ranges::for_each(value,
                          [&](char symbol) { push_back(symbol); });
  }

  [[nodiscard]] constexpr std::string_view view() const noexcept {
    return {storage.data(), length};
  }

  [[nodiscard]] constexpr operator std::string_view() const noexcept {
    return view();
  }
};

class scan_error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

}  // namespace scan
