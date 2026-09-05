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

bool re2c_csv(const char* cursor);

static void scan_csv(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(scan::match<"[a-z]+,[a-z]+,[a-z]+,[a-z]+,[a-z]+">(bench::csv));
  }
  state.SetBytesProcessed(state.iterations() * bench::csv.size());
}
BENCHMARK(scan_csv);

static void ctre_csv(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(ctre::match<"[a-z]+,[a-z]+,[a-z]+,[a-z]+,[a-z]+">(bench::csv));
  }
  state.SetBytesProcessed(state.iterations() * bench::csv.size());
}
BENCHMARK(ctre_csv);

static void re2c_csv_benchmark(benchmark::State& state) {
  static const std::string subject(bench::csv);
  for (auto _ : state) {
    benchmark::DoNotOptimize(re2c_csv(subject.c_str()));
  }
  state.SetBytesProcessed(state.iterations() * bench::csv.size());
}
BENCHMARK(re2c_csv_benchmark);
