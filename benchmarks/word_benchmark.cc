// One pattern: a run of lowercase letters, matched against the whole input.
//
// Every benchmark here compiles exactly one pattern. Compiling a pattern is a
// constant evaluation, so a translation unit that held ten of them would cost
// ten times as much to rebuild -- and rebuilding one benchmark to look at it
// is the thing this file is shaped for.
#include <benchmark/benchmark.h>
#include <ctre.hpp>

#include "inputs.hpp"

import scan;

bool re2c_word(const char* cursor);

static void scan_word(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(scan::match<"[a-z]+">(bench::word));
  }
  state.SetBytesProcessed(state.iterations() * bench::word.size());
}
BENCHMARK(scan_word);

static void ctre_word(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(ctre::match<"[a-z]+">(bench::word));
  }
  state.SetBytesProcessed(state.iterations() * bench::word.size());
}
BENCHMARK(ctre_word);

static void re2c_word_benchmark(benchmark::State& state) {
  static const std::string subject(bench::word);
  for (auto _ : state) {
    benchmark::DoNotOptimize(re2c_word(subject.c_str()));
  }
  state.SetBytesProcessed(state.iterations() * bench::word.size());
}
BENCHMARK(re2c_word_benchmark);
