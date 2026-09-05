// Two loops written by hand, doing the same thing in the two shapes the
// generated code takes. No library and no pattern compilation: this measures
// only what the shape of the loop is worth, so that a difference between the
// engines can be told apart from a difference between the loops they emit.
#include <benchmark/benchmark.h>

#include <string>

#include "inputs.hpp"

// What this library emits: read, classify, advance, and only then look at the
// terminator.
[[gnu::noinline]] static bool scan_shape(const char* cursor) {
  for (;;) {
    const unsigned char symbol = static_cast<unsigned char>(*cursor++);
    if (symbol >= 'a' && symbol <= 'z') continue;
    return symbol == '\0';
  }
}

// What re2c emits: advance first, then read and classify.
[[gnu::noinline]] static bool re2c_shape(const char* cursor) {
  --cursor;
  for (;;) {
    const unsigned char symbol = static_cast<unsigned char>(*++cursor);
    if (symbol >= 'a' && symbol <= 'z') continue;
    return symbol == '\0';
  }
}

static void loop_scan_shape(benchmark::State& state) {
  const std::string& text =
      bench::long_word(static_cast<std::size_t>(state.range(0)));
  for (auto _ : state) {
    const char* cursor = text.c_str();
    benchmark::DoNotOptimize(cursor);
    benchmark::DoNotOptimize(scan_shape(cursor));
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<std::size_t>(state.range(0)));
}
BENCHMARK(loop_scan_shape)->Arg(4096)->Arg(65536);

static void loop_re2c_shape(benchmark::State& state) {
  const std::string& text =
      bench::long_word(static_cast<std::size_t>(state.range(0)));
  for (auto _ : state) {
    const char* cursor = text.c_str();
    benchmark::DoNotOptimize(cursor);
    benchmark::DoNotOptimize(re2c_shape(cursor));
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<std::size_t>(state.range(0)));
}
BENCHMARK(loop_re2c_shape)->Arg(4096)->Arg(65536);
