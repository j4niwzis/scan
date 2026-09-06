import std;
import scan.tre;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

[[nodiscard]] std::size_t operation_count(const scan::tre::tdfa& automaton) {
  return automaton.initialize.size() + std::ranges::fold_left(
      automaton.states |
          std::views::transform([](const scan::tre::tdfa_state& state) {
            return state.final_commands.size() + std::ranges::fold_left(
                state.transitions |
                    std::views::transform(
                        [](const scan::tre::tdfa_transition& transition) {
                          return transition.commands.size();
                        }),
                std::size_t{0}, std::plus<>{});
          }),
      std::size_t{0}, std::plus<>{});
}

scan::tre::node character_class_except(std::string_view excluded) {
  std::array<bool, 256> symbols;
  std::ranges::fill(symbols, true);
  for (unsigned char symbol : excluded) symbols[symbol] = false;
  return scan::tre::character_class(symbols);
}

scan::tre::node any_character() {
  std::array<bool, 256> symbols;
  std::ranges::fill(symbols, true);
  return scan::tre::character_class(symbols);
}

scan::tre::node make_expression() {
  const scan::tre::node local = scan::tre::cat(
      {scan::tre::tag(0), scan::tre::star(character_class_except("!@")), scan::tre::tag(1)});
  const scan::tre::node route = scan::tre::optional(scan::tre::cat(
      {scan::tre::symbol('!'), scan::tre::tag(2),
       scan::tre::star(character_class_except("@")), scan::tre::tag(3)}));
  const scan::tre::node host = scan::tre::optional(scan::tre::cat(
      {scan::tre::symbol('@'), scan::tre::tag(4), scan::tre::star(any_character()), scan::tre::tag(5)}));
  return scan::tre::cat({local, route, host});
}


TEST(TreRuntimeTest, DifferentialShortInputCorpus) {
  const std::vector<scan::tre::node> expressions{
      scan::tre::cat({scan::tre::tag(0), scan::tre::star(scan::tre::alt({scan::tre::symbol('a'),
                                                 scan::tre::symbol('b')})),
                scan::tre::tag(1)}),
      scan::tre::star(scan::tre::alt({
          scan::tre::cat({scan::tre::tag(0), scan::tre::symbol('a'), scan::tre::tag(1)}),
          scan::tre::cat({scan::tre::tag(2), scan::tre::symbol('a'), scan::tre::symbol('a'),
                    scan::tre::tag(3)})})),
      scan::tre::cat({scan::tre::optional(scan::tre::cat(
                    {scan::tre::tag(0), scan::tre::symbol('a'), scan::tre::tag(1)})),
                scan::tre::tag(2), scan::tre::star(scan::tre::symbol('b')), scan::tre::tag(3)}),
      scan::tre::cat({scan::tre::tag(0),
                scan::tre::repeat(scan::tre::alt({scan::tre::symbol('a'), scan::tre::symbol('b')}),
                            1, 3),
                scan::tre::tag(1), scan::tre::optional(scan::tre::symbol('a'))}),
      scan::tre::alt({scan::tre::cat({scan::tre::tag(0), scan::tre::symbol('a'), scan::tre::tag(1)}),
                scan::tre::cat({scan::tre::tag(2), scan::tre::symbol('a'), scan::tre::tag(3)})}),
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

  for (const scan::tre::node& expression : expressions) {
    const scan::tre::tnfa tnfa = scan::tre::compile_tnfa(expression);
    const scan::tre::tdfa original = scan::tre::compile_tdfa(tnfa);
    const scan::tre::tdfa optimized = scan::tre::optimize_tdfa(original);
    for (const std::string& input : inputs) {
      const scan::tre::match interpreted = scan::tre::simulate(tnfa, input);
      const scan::tre::match baseline = scan::tre::simulate(original, input);
      const scan::tre::match result = scan::tre::simulate(optimized, input);
      EXPECT_EQ(result.matched, baseline.matched) << input;
      EXPECT_EQ(result.tags, baseline.tags) << input;
      EXPECT_EQ(result.matched, interpreted.matched) << input;
      EXPECT_EQ(result.tags, interpreted.tags) << input;
    }
  }
}

}  // namespace
