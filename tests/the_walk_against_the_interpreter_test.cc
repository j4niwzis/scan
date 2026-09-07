// The code that runs, against the code that says what running means.
//
// The fuzzer compares this library with another engine, and what it compares
// is everything that happens while a pattern is compiled: reading it, building
// the machine, cutting the walks below a match, minimizing. What it cannot
// reach is the walk itself -- the code written out state by state, which is
// what actually runs in a program and exists only where something is
// compiled.
//
// So that walk is compared here against the interpreter over the same
// automaton, on every short subject there is. They are two readings of one
// machine: if the packing, the run tables, the register layout or the chain
// written out state by state has anything wrong with it, the two part company
// and this says on which subject.
import std;
import scan;
import scan.tre;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

struct place {
  bool took_part = false;
  std::ptrdiff_t begins = 0;
  std::ptrdiff_t ends = 0;
};

// What the interpreter says, over an automaton built from the same pattern by
// the same code, for the anchored reading that `match` does.
std::pair<bool, std::vector<place>> interpreted(std::string_view pattern,
                                                std::string_view subject,
                                                std::size_t groups) {
  std::size_t counted = 0;
  scan::detail::tre_parser parser(pattern, {}, counted, true);
  const auto tree = parser.parse_regex();
  const auto machine = scan::tre::compile_tnfa(tree);
  const auto automaton =
      scan::tre::optimize_tdfa(scan::tre::compile_tdfa(machine, false), true);
  const auto got = scan::tre::simulate(automaton, subject);
  std::vector<place> places(groups);
  if (!got.matched) return {false, places};
  for (std::size_t group = 0; group < groups; ++group) {
    if (group * 2 + 1 >= got.tags.size()) break;
    const auto& begins = got.tags[group * 2];
    const auto& ends = got.tags[group * 2 + 1];
    if (begins.empty() || ends.empty()) continue;
    if (begins.back() < 0 || ends.back() < begins.back()) continue;
    places[group] = {true, begins.back(), ends.back()};
  }
  return {true, places};
}

std::vector<std::string> subjects_over(std::string_view alphabet,
                                       std::size_t longest) {
  std::vector<std::string> made{""};
  std::vector<std::string> edge{""};
  for (std::size_t length = 0; length < longest; ++length) {
    std::vector<std::string> next;
    for (const std::string& prefix : edge) {
      for (char symbol : alphabet) next.push_back(prefix + symbol);
    }
    made.append_range(next);
    edge = std::move(next);
  }
  return made;
}

// One pattern, both readings, every subject.
template <scan::fixed_string pattern, std::size_t groups>
void agree_on(std::string_view alphabet, std::size_t longest) {
  for (const std::string& subject : subjects_over(alphabet, longest)) {
    const auto walked = scan::match<pattern>(std::string_view(subject));
    const auto [matched, places] =
        interpreted(pattern.view(), subject, groups);
    ASSERT_EQ(static_cast<bool>(walked), matched)
        << "pattern " << pattern.view() << " subject \"" << subject << '"';
    if (!matched) continue;
    [&]<std::size_t... group>(std::index_sequence<group...>) {
      (([&] {
         const std::string_view got = walked.template get<group + 1>().to_view();
         const bool took_part = got.data() != nullptr;
         ASSERT_EQ(took_part, places[group].took_part)
             << "pattern " << pattern.view() << " subject \"" << subject
             << "\" group " << group;
         if (!took_part) return;
         ASSERT_EQ(got.data() - subject.data(), places[group].begins)
             << "pattern " << pattern.view() << " subject \"" << subject
             << "\" group " << group;
         ASSERT_EQ(static_cast<std::ptrdiff_t>(got.size()),
                   places[group].ends - places[group].begins)
             << "pattern " << pattern.view() << " subject \"" << subject
             << "\" group " << group;
       }()),
       ...);
    }(std::make_index_sequence<groups>{});
  }
}


TEST(TheWalkAgainstTheInterpreter, WhereTheOrderOfTheBranchesDecides) {
  agree_on<"(a|ab)", 1>("ab", 4);
  agree_on<"(ab|a)", 1>("ab", 4);
}

TEST(TheWalkAgainstTheInterpreter, WhereGreedAndLazinessDecide) {
  agree_on<"(a*)(a*)", 2>("ab", 4);
  agree_on<"(a*?)(a*)", 2>("ab", 4);
  agree_on<"([ab]*?)(b)", 2>("ab", 4);
}

TEST(TheWalkAgainstTheInterpreter, WhereAGroupMayTakeNoPart) {
  agree_on<"(a+)b|(b)", 2>("ab", 4);
  agree_on<"(a)?(b)", 2>("ab", 3);
}

TEST(TheWalkAgainstTheInterpreter, WhereTheCountIsWrittenDown) {
  agree_on<"([ab]{2})([ab]?)", 2>("ab", 4);
  agree_on<"([ab]{1,3})", 1>("ab", 4);
}

}  // namespace
