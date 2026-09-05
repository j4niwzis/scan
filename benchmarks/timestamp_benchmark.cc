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

bool re2c_timestamp(const char* cursor);

static void scan_timestamp(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(scan::match<"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}">(bench::timestamp));
  }
  state.SetBytesProcessed(state.iterations() * bench::timestamp.size());
}
BENCHMARK(scan_timestamp);

static void ctre_timestamp(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(ctre::match<"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}">(bench::timestamp));
  }
  state.SetBytesProcessed(state.iterations() * bench::timestamp.size());
}
BENCHMARK(ctre_timestamp);

static void re2c_timestamp_benchmark(benchmark::State& state) {
  static const std::string subject(bench::timestamp);
  for (auto _ : state) {
    benchmark::DoNotOptimize(re2c_timestamp(subject.c_str()));
  }
  state.SetBytesProcessed(state.iterations() * bench::timestamp.size());
}
BENCHMARK(re2c_timestamp_benchmark);
