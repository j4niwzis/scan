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
  const auto& texts = bench::copies_of(bench::address, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      benchmark::DoNotOptimize(view);
      benchmark::DoNotOptimize(scan::match<"[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+(?:\\.[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+)*@(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?\\.)+[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?">(view));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() * bench::address.size());
}
BENCHMARK(scan_address);

// The same expression, matched the way the generated scanner is given it: a
// terminator instead of a length. The bounded form above carries an end
// pointer and tests it for every character; this one lets the terminator fall
// out of the class test, which is what re2c does, and what makes the two rows
// comparable.
static void scan_address_sentinel(benchmark::State& state) {
  const auto& texts = bench::copies_of(bench::address, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      benchmark::DoNotOptimize(view);
      benchmark::DoNotOptimize(scan::match_sentinel<"[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+(?:\\.[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+)*@(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?\\.)+[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?">(view));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() * bench::address.size());
}
BENCHMARK(scan_address_sentinel);


static void ctre_address(benchmark::State& state) {
  const auto& texts = bench::copies_of(bench::address, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      benchmark::DoNotOptimize(view);
      benchmark::DoNotOptimize(ctre::match<"[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+(?:\\.[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+)*@(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?\\.)+[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?">(view));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() * bench::address.size());
}
BENCHMARK(ctre_address);

static void re2c_address_benchmark(benchmark::State& state) {
  const auto& texts = bench::copies_of(bench::address, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      const char* cursor = text.c_str();
      benchmark::DoNotOptimize(cursor);
      benchmark::DoNotOptimize(re2c_address(cursor));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() * bench::address.size());
}
BENCHMARK(re2c_address_benchmark);
