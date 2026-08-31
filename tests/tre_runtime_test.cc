import std;
import tre;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

[[nodiscard]] std::size_t operation_count(const tre::tdfa& automaton) {
  return automaton.initialize.size() + std::ranges::fold_left(
      automaton.states |
          std::views::transform([](const tre::tdfa_state& state) {
            return state.final_commands.size() + std::ranges::fold_left(
                state.transitions |
                    std::views::transform(
                        [](const tre::tdfa_transition& transition) {
                          return transition.commands.size();
                        }),
                std::size_t{0}, std::plus<>{});
          }),
      std::size_t{0}, std::plus<>{});
}

tre::node character_class_except(std::string_view excluded) {
  std::array<bool, 256> symbols;
  std::ranges::fill(symbols, true);
  for (unsigned char symbol : excluded) symbols[symbol] = false;
  return tre::character_class(symbols);
}

tre::node any_character() {
  std::array<bool, 256> symbols;
  std::ranges::fill(symbols, true);
  return tre::character_class(symbols);
}

tre::node make_expression() {
  const tre::node local = tre::cat(
      {tre::tag(0), tre::star(character_class_except("!@")), tre::tag(1)});
  const tre::node route = tre::optional(tre::cat(
      {tre::symbol('!'), tre::tag(2),
       tre::star(character_class_except("@")), tre::tag(3)}));
  const tre::node host = tre::optional(tre::cat(
      {tre::symbol('@'), tre::tag(4), tre::star(any_character()), tre::tag(5)}));
  return tre::cat({local, route, host});
}

TEST(tre_runtime_test, optimized_register_program_preserves_captures) {
  const tre::tdfa original = tre::compile_tdfa(tre::compile_tnfa(
      make_expression()));
  const tre::tdfa optimized = tre::optimize_tdfa(original);
  constexpr std::array<std::string_view, 8> inputs{
      "local", "local!bang", "local@example.org",
      "local!route@example.org", "@host", "!route@host",
      "a-b_c!x.y@long.example.net", ""};

  for (std::string_view input : inputs) {
    EXPECT_EQ(tre::simulate(optimized, input).tags,
              tre::simulate(original, input).tags);
  }
  EXPECT_LT(optimized.register_count, original.register_count);
  EXPECT_LT(operation_count(optimized), operation_count(original));
}

TEST(tre_runtime_test, optimized_register_program_preserves_repetition_history) {
  const tre::node expression = tre::star(tre::alt({
      tre::cat({tre::tag(0), tre::symbol('a'), tre::tag(1)}),
      tre::cat({tre::tag(2), tre::symbol('a'), tre::symbol('a'),
                tre::tag(3)})}));
  const tre::tdfa original = tre::compile_tdfa(tre::compile_tnfa(expression));
  const tre::tdfa optimized = tre::optimize_tdfa(original);

  for (std::string_view input : std::array<std::string_view, 4>{"", "a", "aa",
                                                               "aaaa"}) {
    EXPECT_EQ(tre::simulate(optimized, input).tags,
              tre::simulate(original, input).tags);
  }
}

TEST(tre_runtime_test, differential_short_input_corpus) {
  const std::vector<tre::node> expressions{
      tre::cat({tre::tag(0), tre::star(tre::alt({tre::symbol('a'),
                                                 tre::symbol('b')})),
                tre::tag(1)}),
      tre::star(tre::alt({
          tre::cat({tre::tag(0), tre::symbol('a'), tre::tag(1)}),
          tre::cat({tre::tag(2), tre::symbol('a'), tre::symbol('a'),
                    tre::tag(3)})})),
      tre::cat({tre::optional(tre::cat(
                    {tre::tag(0), tre::symbol('a'), tre::tag(1)})),
                tre::tag(2), tre::star(tre::symbol('b')), tre::tag(3)}),
      tre::cat({tre::tag(0),
                tre::repeat(tre::alt({tre::symbol('a'), tre::symbol('b')}),
                            1, 3),
                tre::tag(1), tre::optional(tre::symbol('a'))}),
      tre::alt({tre::cat({tre::tag(0), tre::symbol('a'), tre::tag(1)}),
                tre::cat({tre::tag(2), tre::symbol('a'), tre::tag(3)})}),
      make_expression()};

  std::vector<std::string> inputs{""};
  for (std::size_t length : std::views::iota(std::size_t{1}, std::size_t{6})) {
    std::vector<std::string> level;
    std::vector<std::string> prefixes =
        inputs | std::views::filter([&](const std::string& input) {
          return input.size() + 1 == length;
        }) |
        std::ranges::to<std::vector>();
    for (const std::string& prefix : prefixes) {
      for (char symbol : std::string_view("ab!@")) {
        level.push_back(prefix + symbol);
      }
    }
    inputs.append_range(level);
  }

  for (const tre::node& expression : expressions) {
    const tre::tnfa tnfa = tre::compile_tnfa(expression);
    const tre::tdfa original = tre::compile_tdfa(tnfa);
    const tre::tdfa optimized = tre::optimize_tdfa(original);
    for (const std::string& input : inputs) {
      const tre::match interpreted = tre::simulate(tnfa, input);
      const tre::match baseline = tre::simulate(original, input);
      const tre::match result = tre::simulate(optimized, input);
      EXPECT_EQ(result.matched, baseline.matched) << input;
      EXPECT_EQ(result.tags, baseline.tags) << input;
      EXPECT_EQ(result.matched, interpreted.matched) << input;
      EXPECT_EQ(result.tags, interpreted.tags) << input;
    }
  }
}

}  // namespace
