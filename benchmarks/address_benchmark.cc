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

bool re2c_address(const char* cursor);

static void scan_address(benchmark::State& state) {
  const std::string& text = bench::subject_of(bench::address);
  for (auto _ : state) {
    std::string_view view(text);
    benchmark::DoNotOptimize(view);
    benchmark::DoNotOptimize(scan::match<"[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+(?:\\.[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+)*@(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?\\.)+[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?">(view));
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * bench::address.size());
}
BENCHMARK(scan_address);

static void ctre_address(benchmark::State& state) {
  const std::string& text = bench::subject_of(bench::address);
  for (auto _ : state) {
    std::string_view view(text);
    benchmark::DoNotOptimize(view);
    benchmark::DoNotOptimize(ctre::match<"[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+(?:\\.[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+)*@(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?\\.)+[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?">(view));
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * bench::address.size());
}
BENCHMARK(ctre_address);

static void re2c_address_benchmark(benchmark::State& state) {
  const std::string& text = bench::subject_of(bench::address);
  for (auto _ : state) {
    const char* cursor = text.c_str();
    benchmark::DoNotOptimize(cursor);
    benchmark::DoNotOptimize(re2c_address(cursor));
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * bench::address.size());
}
BENCHMARK(re2c_address_benchmark);
