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

bool re2c_address(const char* cursor);

static void scan_address(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(scan::match<"[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+(?:\\.[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+)*@(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?\\.)+[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?">(bench::address));
  }
  state.SetBytesProcessed(state.iterations() * bench::address.size());
}
BENCHMARK(scan_address);

static void ctre_address(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(ctre::match<"[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+(?:\\.[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+)*@(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?\\.)+[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?">(bench::address));
  }
  state.SetBytesProcessed(state.iterations() * bench::address.size());
}
BENCHMARK(ctre_address);

static void re2c_address_benchmark(benchmark::State& state) {
  static const std::string subject(bench::address);
  for (auto _ : state) {
    benchmark::DoNotOptimize(re2c_address(subject.c_str()));
  }
  state.SetBytesProcessed(state.iterations() * bench::address.size());
}
BENCHMARK(re2c_address_benchmark);
