// One pattern, with captures: five comma-separated fields taken out of the
// subject rather than only recognised.
//
// re2c does not appear here. Submatch extraction needs its tagged mode, which
// is a different generator and a different shape of generated code; comparing
// recognition against extraction would say nothing. The recognition benchmarks
// next to this one are where re2c is measured.
#include <benchmark/benchmark.h>
#include <ctre.hpp>

#include <string>

#include "inputs.hpp"

import scan;

struct fields {
  std::string first;
  std::string second;
  std::string third;
  std::string fourth;
  std::string fifth;
};

static void scan_captures(benchmark::State& state) {
  const std::string& text = bench::subject_of(bench::csv);
  for (auto _ : state) {
    std::string_view view(text);
    benchmark::DoNotOptimize(view);
    benchmark::DoNotOptimize(scan::scan<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}">(view));
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * bench::csv.size());
}
BENCHMARK(scan_captures);

static void ctre_captures(benchmark::State& state) {
  const std::string& text = bench::subject_of(bench::csv);
  for (auto _ : state) {
    std::string_view view(text);
    benchmark::DoNotOptimize(view);
    benchmark::DoNotOptimize(ctre::match<"([a-z]+),([a-z]+),([a-z]+),([a-z]+),([a-z]+)">(view));
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() * bench::csv.size());
}
BENCHMARK(ctre_captures);
