// One pattern: a run of lowercase letters, matched against the whole input.
//
// Nothing is included here. The harness and the other engine are modules --
// see benchmarks/harness.cppm -- so this file can be compiled with the same
// constant evaluator the library is built with.
import std;
import bench.harness;
import ctre;
import scan;

import bench.inputs;

bool re2c_word(const char* cursor);

namespace {

void scan_word(harness::State& state) {
  const std::string& text =
      bench::long_word(static_cast<std::size_t>(state.range(0)));
  for (auto _ : state) {
    std::string_view view(text);
    harness::DoNotOptimize(view);
    harness::DoNotOptimize(static_cast<bool>(scan::match<"[a-z]+">(view)));
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<std::size_t>(state.range(0)));
}

void scan_word_sentinel(harness::State& state) {
  const std::string& text =
      bench::long_word(static_cast<std::size_t>(state.range(0)));
  for (auto _ : state) {
    std::string_view view(text);
    harness::DoNotOptimize(view);
    harness::DoNotOptimize(static_cast<bool>(scan::match_sentinel<"[a-z]+">(view)));
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<std::size_t>(state.range(0)));
}

void ctre_word(harness::State& state) {
  const std::string& text =
      bench::long_word(static_cast<std::size_t>(state.range(0)));
  for (auto _ : state) {
    std::string_view view(text);
    harness::DoNotOptimize(view);
    harness::DoNotOptimize(static_cast<bool>(ctre::match<"[a-z]+">(view)));
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<std::size_t>(state.range(0)));
}

void re2c_word_benchmark(harness::State& state) {
  const std::string& text =
      bench::long_word(static_cast<std::size_t>(state.range(0)));
  for (auto _ : state) {
    const char* cursor = text.c_str();
    harness::DoNotOptimize(cursor);
    harness::DoNotOptimize(re2c_word(cursor));
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<std::size_t>(state.range(0)));
}

// The same loop over inputs of very different length: a cost paid once per
// call shows at sixty-four bytes and disappears at sixty-four kilobytes, while
// a cost paid per byte does not move.
harness::Benchmark* with_lengths(harness::Benchmark* registration) {
  return registration->Arg(64)->Arg(1024)->Arg(4096)->Arg(65536);
}

const int registered = [] {
  with_lengths(harness::RegisterBenchmark("scan_word", scan_word));
  with_lengths(
      harness::RegisterBenchmark("scan_word_sentinel", scan_word_sentinel));
  with_lengths(harness::RegisterBenchmark("ctre_word", ctre_word));
  with_lengths(
      harness::RegisterBenchmark("re2c_word", re2c_word_benchmark));
  return 0;
}();

}  // namespace
