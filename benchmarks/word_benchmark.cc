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
  const std::string& text = bench::subject_of(bench::word);
  for (auto _ : state) {
    std::string_view view(text);
    benchmark::DoNotOptimize(view);
    benchmark::DoNotOptimize(scan::match<"[a-z]+">(view));
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * bench::word.size());
}
BENCHMARK(scan_word);

static void ctre_word(benchmark::State& state) {
  const std::string& text = bench::subject_of(bench::word);
  for (auto _ : state) {
    std::string_view view(text);
    benchmark::DoNotOptimize(view);
    benchmark::DoNotOptimize(ctre::match<"[a-z]+">(view));
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * bench::word.size());
}
BENCHMARK(ctre_word);

static void re2c_word_benchmark(benchmark::State& state) {
  const std::string& text = bench::subject_of(bench::word);
  for (auto _ : state) {
    const char* cursor = text.c_str();
    benchmark::DoNotOptimize(cursor);
    benchmark::DoNotOptimize(re2c_word(cursor));
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * bench::word.size());
}
BENCHMARK(re2c_word_benchmark);
