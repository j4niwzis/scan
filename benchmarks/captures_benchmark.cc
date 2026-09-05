// One pattern, with captures: five comma-separated fields taken out of the
// subject rather than only recognised.
//
// All three engines take the fields out here. re2c does it with tags -- `@name`
// binds the position where it stands -- so this compares three pieces of code
// doing the same work, rather than extraction against recognition.
//
// This library appears twice, because the two rows are different work. Into
// `string_view`, the fields are positions in the subject and nothing is
// allocated, which is what a contiguous input allows and what the other two
// engines do. Into `string`, each field is a copy -- which is not waste but the
// only thing possible when the input is a range that is read once and cannot be
// pointed into afterwards.
#include <benchmark/benchmark.h>
#include <ctre.hpp>

#include <string>
#include <string_view>

#include "inputs.hpp"

import scan;

bool re2c_captures(const char* cursor, const char** positions);

// Fields as views into the subject would allocate nothing, which is what the
// other two engines do -- but asking for them does not compile today, so this
// row is on hold until it does. See the note below the string row.
struct field_strings {
  std::string first;
  std::string second;
  std::string third;
  std::string fourth;
  std::string fifth;
};



static void scan_captures_strings(benchmark::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      benchmark::DoNotOptimize(view);
      field_strings value =
          scan::scan<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}">(view);
      benchmark::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() * bench::csv.size());
}
BENCHMARK(scan_captures_strings);

static void ctre_captures(benchmark::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      benchmark::DoNotOptimize(view);
      auto value =
          ctre::match<"([a-z]+),([a-z]+),([a-z]+),([a-z]+),([a-z]+)">(view);
      benchmark::DoNotOptimize(value);
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
