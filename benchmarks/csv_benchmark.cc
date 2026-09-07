// One pattern, recognised over the whole input. Nothing is included here: the
// harness and the other engine are modules, so this file is compiled with the
// same constant evaluator the library is.
import std;
import bench.harness;
import bench.inputs;
import bench.re2;
import ctre;
import scan;

bool re2c_csv(const char* cursor);

namespace {

void scan_csv(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      harness::DoNotOptimize(static_cast<bool>(scan::match<"[a-z]+,[a-z]+,[a-z]+,[a-z]+,[a-z]+">(view)));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

void scan_csv_sentinel(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      harness::DoNotOptimize(static_cast<bool>(scan::match<"[a-z]+,[a-z]+,[a-z]+,[a-z]+,[a-z]+">.sentinel()(view)));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

void ctre_csv(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      harness::DoNotOptimize(static_cast<bool>(ctre::match<"[a-z]+,[a-z]+,[a-z]+,[a-z]+,[a-z]+">(view)));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

void re2c_csv_benchmark(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      const char* cursor = text.c_str();
      harness::DoNotOptimize(cursor);
      harness::DoNotOptimize(re2c_csv(cursor));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}


// The same question of an engine that reads its pattern while the program
// runs. What it compiles is not in the measurement; what its walk does with a
// pattern it did not know about is.
void re2_csv(harness::State& state) {
  const bench::re2_engine engine("[a-z]+,[a-z]+,[a-z]+,[a-z]+,[a-z]+");
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      harness::DoNotOptimize(engine.whole(view));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

const int registered = [] {
  harness::RegisterBenchmark("scan_csv", scan_csv);
  harness::RegisterBenchmark("scan_csv_sentinel", scan_csv_sentinel);
  harness::RegisterBenchmark("ctre_csv", ctre_csv);
  harness::RegisterBenchmark("re2_csv", re2_csv);
  harness::RegisterBenchmark("re2c_csv", re2c_csv_benchmark);
  return 0;
}();

}  // namespace
