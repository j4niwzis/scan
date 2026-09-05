import std;
import scan.tre;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

TEST(tre_test, tagged_automata) {
  using scan::tre::cat;
  using scan::tre::node;
  using scan::tre::star;
  using scan::tre::symbol;
  using scan::tre::tag;

  // The tags delimit the greedy a* submatch in a*b*.
  node expression = cat({tag(0), star(symbol('a')), tag(1),
                         star(symbol('b'))});
  const scan::tre::tnfa tnfa = scan::tre::compile_tnfa(expression);
  const scan::tre::match interpreted = scan::tre::simulate(tnfa, "aaabb");
  ASSERT_TRUE(interpreted.matched);
  EXPECT_EQ(interpreted.tags, std::vector<scan::tre::tag_history>({{0}, {3}}));

  const scan::tre::tdfa tdfa = scan::tre::compile_tdfa(tnfa);
  const scan::tre::match compiled = scan::tre::simulate(tdfa, "aaabb");
  ASSERT_TRUE(compiled.matched);
  EXPECT_EQ(compiled.tags, interpreted.tags);
  EXPECT_FALSE(scan::tre::simulate(tdfa, "aaabc").matched);

  // Earlier alternatives win when both consume the same input.
  node alternative = scan::tre::alt({cat({tag(0), symbol('a')}),
                               cat({tag(1), symbol('a')})});
  const auto ambiguous = scan::tre::simulate(scan::tre::compile_tdfa(
      scan::tre::compile_tnfa(alternative)), "a");
  ASSERT_TRUE(ambiguous.matched);
  EXPECT_EQ(ambiguous.tags[0], scan::tre::tag_history({0}));
  EXPECT_EQ(ambiguous.tags[1], scan::tre::tag_history({scan::tre::negative_tag}));

  node optional = scan::tre::optional(cat({tag(0), symbol('x'), tag(1)}));
  const auto skipped = scan::tre::simulate(
      scan::tre::compile_tdfa(scan::tre::compile_tnfa(optional)), "");
  ASSERT_TRUE(skipped.matched);
  EXPECT_EQ(skipped.tags[0], scan::tre::tag_history({scan::tre::negative_tag}));
  EXPECT_EQ(skipped.tags[1], scan::tre::tag_history({scan::tre::negative_tag}));
}

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

TEST(tre_test, leftmost_greedy_repetition_consumes_before_following_repetition) {
  // Both repetitions can consume every partition of "aaaa". Leftmost-greedy
  // gives all symbols to the first repetition and leaves the second empty.
  const scan::tre::node expression = scan::tre::cat(
      {scan::tre::tag(0), scan::tre::star(scan::tre::symbol('a')), scan::tre::tag(1), scan::tre::tag(2),
       scan::tre::star(scan::tre::symbol('a')), scan::tre::tag(3)});

  expect_equivalent(expression, "aaaa", {{0}, {4}, {4}, {4}});
}

TEST(tre_test, leftmost_alternative_wins_inside_greedy_repetition) {
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

TEST(tre_test, preserves_capture_history_across_repetitions) {
  const scan::tre::node expression = scan::tre::star(
      scan::tre::cat({scan::tre::tag(0), scan::tre::symbol('x'), scan::tre::tag(1)}));

  expect_equivalent(expression, "xxx", {{0, 1, 2}, {1, 2, 3}});
}

TEST(tre_test, handles_bounded_greedy_repetition) {
  const scan::tre::node expression = scan::tre::cat(
      {scan::tre::tag(0), scan::tre::repeat(scan::tre::symbol('a'), 2, 4), scan::tre::tag(1),
       scan::tre::optional(scan::tre::symbol('a'))});

  // Four repetitions are preferred, leaving the optional suffix empty.
  expect_equivalent(expression, "aaaa", {{0}, {4}});
  EXPECT_FALSE(scan::tre::simulate(scan::tre::compile_tdfa(scan::tre::compile_tnfa(expression)),
                             "a")
                   .matched);
  EXPECT_FALSE(scan::tre::simulate(scan::tre::compile_tdfa(scan::tre::compile_tnfa(expression)),
                             "aaaaaa")
                   .matched);
}

TEST(tre_test, optimizes_register_program_without_changing_captures) {
  const scan::tre::node expression = scan::tre::star(scan::tre::alt({
      scan::tre::cat({scan::tre::tag(0), scan::tre::symbol('a'), scan::tre::tag(1)}),
      scan::tre::cat({scan::tre::tag(2), scan::tre::symbol('a'), scan::tre::symbol('a'),
                scan::tre::tag(3)})}));
  const scan::tre::tdfa original = scan::tre::compile_tdfa(scan::tre::compile_tnfa(expression));
  const scan::tre::tdfa optimized = scan::tre::optimize_tdfa(original);
  const auto operation_count = [](const scan::tre::tdfa& automaton) {
    return automaton.initialize.size() +
           std::ranges::fold_left(
               automaton.states | std::views::transform([](const auto& state) {
                 return std::ranges::fold_left(
                     state.transitions |
                         std::views::transform([](const auto& transition) {
                           return transition.commands.size();
                         }),
                     std::size_t{0}, std::plus<>{});
               }),
               std::size_t{0}, std::plus<>{});
  };

  EXPECT_EQ(scan::tre::simulate(optimized, "aaaa").tags,
            scan::tre::simulate(original, "aaaa").tags);
  EXPECT_LT(operation_count(optimized), operation_count(original));
}

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

TEST(scan_test, scans_borrowed_contiguous_range) {
  const std::string input = "name=alice id=42";
  user user = scan::scan<"name={} id={}">(input);
  EXPECT_EQ(user.name, "alice");
  EXPECT_EQ(user.id, 42);
}

TEST(scan_test, scans_single_pass_input_range) {
  std::istringstream input("name=bob id=7");
  input >> std::noskipws;
  user user = scan::scan<"name={} id={}">(
      std::views::istream<char>(input));
  EXPECT_EQ(user.name, "bob");
  EXPECT_EQ(user.id, 7);
}

TEST(scan_test, applies_scanner_parameters_to_borrowed_input) {
  based_values values =
      scan::scan<"hex={:hex} bin={:binary} oct={:octal}">(
          std::string_view("hex=DeAd bin=101101 oct=17"));

  EXPECT_EQ(values.hexadecimal, 0xDEAD);
  EXPECT_EQ(values.binary, 45);
  EXPECT_EQ(values.octal, 15);
}

TEST(scan_test, applies_scanner_parameters_to_single_pass_input) {
  std::istringstream input("hex=BEEF bin=11001 oct=377");
  input >> std::noskipws;

  based_values values =
      scan::scan<"hex={:x} bin={:b} oct={:o}">(
          std::views::istream<char>(input));

  EXPECT_EQ(values.hexadecimal, 0xBEEF);
  EXPECT_EQ(values.binary, 25);
  EXPECT_EQ(values.octal, 255);
}

TEST(scan_test, scanner_can_build_an_owning_pattern_from_parameters) {
  std::istringstream input("word=CONSTEXPR");
  input >> std::noskipws;

  parameterized_record value = scan::scan<"word={:upper}">(
      std::views::istream<char>(input));

  EXPECT_EQ(value.word.value, "CONSTEXPR");
}

TEST(scan_test, supports_sscanf_like_widths_and_conversions) {
  scanf_like_record value =
      scan::scan<"hex={:4x} auto={:i} word={:5s} set={:4[a-z]} mark={:c}">(
          std::string_view("hex=BEEF auto=077 word=hello set=abcd mark=!"));

  EXPECT_EQ(value.hexadecimal, 0xBEEF);
  EXPECT_EQ(value.automatic, 63);
  EXPECT_EQ(value.word, "hello");
  EXPECT_EQ(value.scanset, "abcd");
  EXPECT_EQ(value.marker, '!');
}

TEST(scan_test, streams_sscanf_like_floating_conversions) {
  std::istringstream input("scientific=1.25e2 hex=0x1.8p1");
  input >> std::noskipws;

  floating_record value =
      scan::scan<"scientific={:e} hex={:a}">(
          std::views::istream<char>(input));

  EXPECT_DOUBLE_EQ(value.scientific, 125.0);
  EXPECT_DOUBLE_EQ(value.hexadecimal, 3.0);
}

TEST(scan_test, scans_stream_without_changing_formatting_mode) {
  std::istringstream input("name=carol id=9");
  user user = scan::scan<"name={} id={}">(input);
  EXPECT_EQ(user.name, "carol");
  EXPECT_EQ(user.id, 9);
}

TEST(scan_test, uses_custom_scanner) {
  hex_record record =
      scan::scan<"id={}">(std::string_view("id=ff"));
  EXPECT_EQ(record.id.value, 255);
}

TEST(scan_test, uses_custom_default_with_single_pass_input_range) {
  std::istringstream input("id=BEEF");
  input >> std::noskipws;
  hex_record record =
      scan::scan<"id={}">(std::views::istream<char>(input));
  EXPECT_EQ(record.id.value, 0xBEEF);
}

TEST(scan_test, streams_unbounded_custom_value_without_input_storage) {
  std::istringstream input("chapter=MMMDCCCLXXXVIII");
  input >> std::noskipws;

  chapter result = scan::scan<"chapter={}">(
      std::views::istream<char>(input));

  EXPECT_EQ(result.number.value, 3888);
}

TEST(scan_test, recursively_composes_typed_aggregate_scanners) {
  std::istringstream input(
      "record=id=17 bounds=[(1, -2) -> (300, 4000)]");
  input >> std::noskipws;

  record_wrapper result = scan::scan<"record={}">(
      std::views::istream<char>(input));

  EXPECT_EQ(result.record.id, 17);
  EXPECT_EQ(result.record.bounds.first.x, 1);
  EXPECT_EQ(result.record.bounds.first.y, -2);
  EXPECT_EQ(result.record.bounds.second.x, 300);
  EXPECT_EQ(result.record.bounds.second.y, 4000);
}

TEST(scan_test, borrows_vector_and_span_storage) {
  std::vector<char> input{'n', 'a', 'm', 'e', '=', 'e', 'v', 'e',
                          ' ', 'i', 'd', '=', '1', '1'};
  user from_vector = scan::scan<"name={} id={}">(input);
  user from_span = scan::scan<"name={} id={}">(std::span<char>(input));

  EXPECT_EQ(from_vector.name, "eve");
  EXPECT_EQ(from_vector.id, 11);
  EXPECT_EQ(from_span.name, "eve");
  EXPECT_EQ(from_span.id, 11);
}

TEST(scan_regex_contract_test, enforces_bounded_repetition_from_input_range) {
  std::istringstream input("name=alice id=123456789");
  input >> std::noskipws;

  EXPECT_THROW(
      ((void)static_cast<user>(scan::scan<"name={} id={[0-9]{1,8}}">(
          std::views::istream<char>(input)))),
      scan::scan_error);
}

TEST(scan_regex_contract_test, accepts_bounded_repetition_from_input_range) {
  std::istringstream input("name=alice id=12345678");
  input >> std::noskipws;

  user user = scan::scan<"name={} id={[0-9]{1,8}}">(
      std::views::istream<char>(input));

  EXPECT_EQ(user.name, "alice");
  EXPECT_EQ(user.id, 12345678);
}

TEST(scan_regex_contract_test, supports_alternation_classes_and_repetition) {
  std::istringstream input("word=Codex code=aba");
  input >> std::noskipws;

  regex_values values =
      scan::scan<"word={[A-Za-z]+} code={(?:ab|a){2,3}}">(
          std::views::istream<char>(input));

  EXPECT_EQ(values.word, "Codex");
  EXPECT_EQ(values.code, "aba");
}

TEST(scan_regex_contract_test, captures_nested_groups_in_opening_order) {
  std::istringstream input("aaaa");
  input >> std::noskipws;
  string_captures captures =
      scan::scan<"{{[a]{2,4}}}">(std::views::istream<char>(input));

  EXPECT_EQ(captures.first, "aaaa");
  EXPECT_EQ(captures.second, "aaaa");
}

TEST(scan_regex_contract_test, treats_escaped_braces_as_literals) {
  std::istringstream input("{42}");
  input >> std::noskipws;
  number_value value = scan::scan<"\\{{[0-9]+}\\}">(
      std::views::istream<char>(input));

  EXPECT_EQ(value.number, 42);
}

TEST(scan_regex_contract_test, uses_leftmost_greedy_for_adjacent_captures) {
  std::istringstream input("aaaa");
  input >> std::noskipws;
  string_captures captures = scan::scan<"{[a]*}{[a]*}">(
      std::views::istream<char>(input));

  EXPECT_EQ(captures.first, "aaaa");
  EXPECT_EQ(captures.second, "");
}
