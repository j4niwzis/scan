// One pattern: a run of lowercase letters, matched against the whole input.
//
// Every benchmark here compiles exactly one pattern. Compiling a pattern is a
// constant evaluation, so a translation unit that held ten of them would cost
// ten times as much to rebuild -- and rebuilding one benchmark to look at it
// is the thing this file is shaped for.
#include <benchmark/benchmark.h>
#include <ctre.hpp>

#include <string>

#include "inputs.hpp"

import scan;

bool re2c_word(const char* cursor);

static void scan_word(benchmark::State& state) {
  const std::string& text = bench::long_word();
  for (auto _ : state) {
    std::string_view view(text);
    benchmark::DoNotOptimize(view);
    benchmark::DoNotOptimize(scan::match<"[a-z]+">(view));
  }
  state.SetBytesProcessed(state.iterations() * bench::long_word().size());
}
BENCHMARK(scan_word);

// The same expression, matched the way the generated scanner is given it: a
// terminator instead of a length. The bounded form above carries an end
// pointer and tests it for every character; this one lets the terminator fall
// out of the class test, which is what re2c does, and what makes the two rows
// comparable.
static void scan_word_sentinel(benchmark::State& state) {
  const std::string& text = bench::long_word();
  for (auto _ : state) {
    std::string_view view(text);
    benchmark::DoNotOptimize(view);
    benchmark::DoNotOptimize(scan::match_sentinel<"[a-z]+">(view));
  }
  state.SetBytesProcessed(state.iterations() * bench::long_word().size());
}
BENCHMARK(scan_word_sentinel);


static void ctre_word(benchmark::State& state) {
  const std::string& text = bench::long_word();
  for (auto _ : state) {
    std::string_view view(text);
    benchmark::DoNotOptimize(view);
    benchmark::DoNotOptimize(ctre::match<"[a-z]+">(view));
  }
  state.SetBytesProcessed(state.iterations() * bench::long_word().size());
}
BENCHMARK(ctre_word);

static void re2c_word_benchmark(benchmark::State& state) {
  const std::string& text = bench::long_word();
  for (auto _ : state) {
    const char* cursor = text.c_str();
    benchmark::DoNotOptimize(cursor);
    benchmark::DoNotOptimize(re2c_word(cursor));
  }
  state.SetBytesProcessed(state.iterations() * bench::long_word().size());
}
BENCHMARK(re2c_word_benchmark);
