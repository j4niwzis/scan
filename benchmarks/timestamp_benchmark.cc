// One pattern, recognised over the whole input. Nothing is included here: the
// harness and the other engine are modules, so this file is compiled with the
// same constant evaluator the library is.
import std;
import bench.harness;
import bench.inputs;
import bench.re2;
import ctre;
import scan;

bool re2c_timestamp(const char* cursor);

namespace {

void scan_timestamp(harness::State& state) {
  const auto& texts = bench::copies_of(bench::timestamp, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      harness::DoNotOptimize(static_cast<bool>(scan::match<"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}">(view)));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::timestamp.size());
}

void scan_timestamp_sentinel(harness::State& state) {
  const auto& texts = bench::copies_of(bench::timestamp, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      harness::DoNotOptimize(static_cast<bool>(scan::match<"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}">.sentinel()(view)));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::timestamp.size());
}

void ctre_timestamp(harness::State& state) {
  const auto& texts = bench::copies_of(bench::timestamp, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      harness::DoNotOptimize(static_cast<bool>(ctre::match<"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}">(view)));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::timestamp.size());
}

void re2c_timestamp_benchmark(harness::State& state) {
  const auto& texts = bench::copies_of(bench::timestamp, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      const char* cursor = text.c_str();
      harness::DoNotOptimize(cursor);
      harness::DoNotOptimize(re2c_timestamp(cursor));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::timestamp.size());
}


// The same question of an engine that reads its pattern while the program
// runs. What it compiles is not in the measurement; what its walk does with a
// pattern it did not know about is.
void re2_timestamp(harness::State& state) {
  const bench::re2_engine engine("[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}");
  const auto& texts = bench::copies_of(bench::timestamp, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      harness::DoNotOptimize(engine.whole(view));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::timestamp.size());
}

const int registered = [] {
  harness::RegisterBenchmark("scan_timestamp", scan_timestamp);
  harness::RegisterBenchmark("scan_timestamp_sentinel", scan_timestamp_sentinel);
  harness::RegisterBenchmark("ctre_timestamp", ctre_timestamp);
  harness::RegisterBenchmark("re2_timestamp", re2_timestamp);
  harness::RegisterBenchmark("re2c_timestamp", re2c_timestamp_benchmark);
  return 0;
}();

}  // namespace
