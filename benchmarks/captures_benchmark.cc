// One pattern, with captures: five comma-separated fields taken out of the
// subject rather than only recognised.
//
// All three engines take the fields out here. re2c does it with tags -- `@name`
// binds the position where it stands -- so the comparison is between three
// pieces of code doing the same work, not between extraction and recognition.
#include <benchmark/benchmark.h>
#include <ctre.hpp>

#include <string>
#include <string_view>

#include "inputs.hpp"

import scan;

bool re2c_captures(const char* cursor, const char** positions);

// Views, not strings: re2c hands back positions and ctre hands back views, so
// a row that copied each field into a string would be timing allocation and
// calling it matching.
struct fields {
  std::string_view first;
  std::string_view second;
  std::string_view third;
  std::string_view fourth;
  std::string_view fifth;
};

static void scan_captures(benchmark::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      benchmark::DoNotOptimize(view);
      benchmark::DoNotOptimize(scan::scan<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}">(view));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() * bench::csv.size());
}
BENCHMARK(scan_captures);

static void ctre_captures(benchmark::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      benchmark::DoNotOptimize(view);
      benchmark::DoNotOptimize(ctre::match<"([a-z]+),([a-z]+),([a-z]+),([a-z]+),([a-z]+)">(view));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() * bench::csv.size());
}
BENCHMARK(ctre_captures);

static void re2c_captures_benchmark(benchmark::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      const char* cursor = text.c_str();
      const char* positions[10] = {};
      benchmark::DoNotOptimize(cursor);
      benchmark::DoNotOptimize(re2c_captures(cursor, positions));
      benchmark::DoNotOptimize(positions);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() * bench::csv.size());
}
BENCHMARK(re2c_captures_benchmark);
