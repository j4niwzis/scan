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

bool re2c_timestamp(const char* cursor);

static void scan_timestamp(benchmark::State& state) {
  const std::string& text = bench::subject_of(bench::timestamp);
  for (auto _ : state) {
    std::string_view view(text);
    benchmark::DoNotOptimize(view);
    benchmark::DoNotOptimize(scan::match<"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}">(view));
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * bench::timestamp.size());
}
BENCHMARK(scan_timestamp);

// The same expression, matched the way the generated scanner is given it: a
// terminator instead of a length. The bounded form above carries an end
// pointer and tests it for every character; this one lets the terminator fall
// out of the class test, which is what re2c does, and what makes the two rows
// comparable.
static void scan_timestamp_sentinel(benchmark::State& state) {
  const std::string& text = bench::subject_of(bench::timestamp);
  for (auto _ : state) {
    std::string_view view(text);
    benchmark::DoNotOptimize(view);
    benchmark::DoNotOptimize(scan::match_sentinel<"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}">(view));
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * bench::timestamp.size());
}
BENCHMARK(scan_timestamp_sentinel);


static void ctre_timestamp(benchmark::State& state) {
  const std::string& text = bench::subject_of(bench::timestamp);
  for (auto _ : state) {
    std::string_view view(text);
    benchmark::DoNotOptimize(view);
    benchmark::DoNotOptimize(ctre::match<"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}">(view));
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * bench::timestamp.size());
}
BENCHMARK(ctre_timestamp);

static void re2c_timestamp_benchmark(benchmark::State& state) {
  const std::string& text = bench::subject_of(bench::timestamp);
  for (auto _ : state) {
    const char* cursor = text.c_str();
    benchmark::DoNotOptimize(cursor);
    benchmark::DoNotOptimize(re2c_timestamp(cursor));
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * bench::timestamp.size());
}
BENCHMARK(re2c_timestamp_benchmark);
