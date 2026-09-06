import std;
import scan.tre;
import scan;
import gtest;

#include "gtest/gtest-macros.h"


namespace {

void expect_equivalent(const scan::tre::node& expression, std::string_view input,
                      const std::vector<scan::tre::tag_history>& expected) {
  const scan::tre::tnfa tnfa = scan::tre::compile_tnfa(expression);
  const scan::tre::match interpreted = scan::tre::simulate(tnfa, input);
  const scan::tre::match compiled = scan::tre::simulate(scan::tre::compile_tdfa(tnfa), input);

  ASSERT_TRUE(interpreted.matched);
  ASSERT_TRUE(compiled.matched);
  EXPECT_EQ(interpreted.tags, expected);
  EXPECT_EQ(compiled.tags, expected);
}

}  // namespace


namespace {

struct user {
  std::string name;
  std::uint64_t id;
};

struct hex_id {
  std::uint64_t value;
};

struct hex_record {
  hex_id id;
};

struct roman_number {
  std::uint32_t value;
};

struct chapter {
  roman_number number;
};

struct coordinates {
  std::int32_t x;
  std::int32_t y;
};

struct rectangle {
  coordinates first;
  coordinates second;
};

struct nested_record {
  std::uint64_t id;
  rectangle bounds;
};

struct record_wrapper {
  nested_record record;
};

struct string_captures {
  std::string first;
  std::string second;
};

struct number_value {
  std::uint64_t number;
};

struct regex_values {
  std::string word;
  std::string code;
};

struct based_values {
  std::uint64_t hexadecimal;
  std::uint32_t binary;
  std::uint16_t octal;
};

struct parameterized_word {
  std::string value;
};

struct parameterized_record {
  parameterized_word word;
};

struct scanf_like_record {
  std::uint32_t hexadecimal;
  std::int32_t automatic;
  std::string word;
  std::string scanset;
  char marker;
};

struct floating_record {
  double scientific;
  double hexadecimal;
};

consteval bool scans_at_compile_time() {
  constexpr std::string_view input = "name=alice id=42";
  user user = scan::scan<"name={} id={}">(input);
  return user.name == "alice" && user.id == 42;
}

static_assert(scans_at_compile_time());

consteval bool scans_parameters_at_compile_time() {
  constexpr std::string_view input = "hex=ff bin=101101 oct=17";
  based_values values =
      scan::scan<"hex={:hex} bin={:binary} oct={:octal}">(input);
  return values.hexadecimal == 255 && values.binary == 45 &&
         values.octal == 15;
}

static_assert(scans_parameters_at_compile_time());


}  // namespace

template <>
struct scan::scanner<hex_id> {
  static constexpr std::string_view pattern = "[0-9A-Fa-f]+";

  struct state_type {
    std::array<char, 16> buffer{};
    std::size_t size = 0;
  };

  [[nodiscard]] static constexpr state_type begin() { return {}; }

  static constexpr void push(state_type& state, char value) {
    if (state.size == state.buffer.size()) {
      throw scan::scan_error("hexadecimal id is out of range");
    }
    state.buffer[state.size++] = value;
  }

  [[nodiscard]] static constexpr hex_id finish(state_type state) {
    return parse(std::string_view(state.buffer.data(), state.size));
  }

  static constexpr hex_id parse(std::string_view text) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value, 16);
    if (error != std::errc{} || end != text.data() + text.size()) {
      throw scan::scan_error("invalid hexadecimal id");
    }
    return {value};
  }
};

template <>
struct scan::scanner<parameterized_word> {
  using state_type = std::string;

  [[nodiscard]] static constexpr std::string pattern(
      std::string_view parameters) {
    if (parameters == "upper") return "[A-Z]+";
    if (parameters == "lower") return "[a-z]+";
    throw "unsupported word mode";
  }

  [[nodiscard]] static constexpr state_type begin(
      std::string_view parameters) {
    static_cast<void>(pattern(parameters));
    return {};
  }

  static constexpr void push(state_type& state, char value) {
    state.push_back(value);
  }

  [[nodiscard]] static constexpr parameterized_word finish(state_type state) {
    return {std::move(state)};
  }

  [[nodiscard]] static constexpr parameterized_word parse(
      std::string_view text, std::string_view parameters) {
    static_cast<void>(pattern(parameters));
    return {std::string(text)};
  }
};

template <>
struct scan::scanner<roman_number> {
  static constexpr std::string_view pattern =
      "M{0,3}(?:CM|CD|D?C{0,3})(?:XC|XL|L?X{0,3})"
      "(?:IX|IV|V?I{0,3})";

  struct state_type {
    std::uint32_t value = 0;
    std::uint16_t previous = 0;
  };

  [[nodiscard]] static constexpr state_type begin() { return {}; }

  static constexpr void push(state_type& state, char symbol) {
    const auto current = [](char value) -> std::uint16_t {
      switch (value) {
        case 'I': return 1;
        case 'V': return 5;
        case 'X': return 10;
        case 'L': return 50;
        case 'C': return 100;
        case 'D': return 500;
        case 'M': return 1000;
        default: return 0;
      }
    }(symbol);
    if (current == 0) throw scan::scan_error("invalid Roman numeral");
    state.value += current;
    if (current > state.previous) state.value -= 2 * state.previous;
    state.previous = current;
  }

  [[nodiscard]] static constexpr roman_number finish(state_type state) {
    return {state.value};
  }

  [[nodiscard]] static constexpr roman_number parse(std::string_view text) {
    state_type state = begin();
    std::ranges::for_each(text,
                          [&](char value) { push(state, value); });
    return finish(state);
  }
};

template <>
struct scan::scanner<coordinates>
    : scan::aggregate_scanner<"({}, {})"> {};

template <>
struct scan::scanner<rectangle>
    : scan::aggregate_scanner<"[{} -> {}]"> {};

template <>
struct scan::scanner<nested_record>
    : scan::aggregate_scanner<"id={} bounds={}"> {};

static_assert(std::is_trivially_copyable_v<
              scan::scanner<roman_number>::state_type>);


TEST(TreTest, LeftmostAlternativeWinsInsideGreedyRepetition) {
  // At every position both `a` and `aa` may lead to a complete match. The
  // earlier `a` branch wins, so its tags record all four iterations.
  const scan::tre::node one =
      scan::tre::cat({scan::tre::tag(0), scan::tre::symbol('a'), scan::tre::tag(1)});
  const scan::tre::node two = scan::tre::cat(
      {scan::tre::tag(2), scan::tre::symbol('a'), scan::tre::symbol('a'), scan::tre::tag(3)});
  const scan::tre::node expression = scan::tre::star(scan::tre::alt({one, two}));

  expect_equivalent(expression, "aaaa",
                   {{0, 1, 2, 3}, {1, 2, 3, 4},
                    {scan::tre::negative_tag, scan::tre::negative_tag, scan::tre::negative_tag,
                     scan::tre::negative_tag},
                    {scan::tre::negative_tag, scan::tre::negative_tag, scan::tre::negative_tag,
                     scan::tre::negative_tag}});
}

TEST(TreTest, PreservesCaptureHistoryAcrossRepetitions) {
  const scan::tre::node expression = scan::tre::star(
      scan::tre::cat({scan::tre::tag(0), scan::tre::symbol('x'), scan::tre::tag(1)}));

  expect_equivalent(expression, "xxx", {{0, 1, 2}, {1, 2, 3}});
}
