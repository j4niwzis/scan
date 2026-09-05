// One pattern, recognised over the whole input. Nothing is included here: the
// harness and the other engine are modules, so this file is compiled with the
// same constant evaluator the library is.
import std;
import bench.harness;
import bench.inputs;
import ctre;
import scan;

bool re2c_address(const char* cursor);

namespace {

void scan_address(harness::State& state) {
  const auto& texts = bench::copies_of(bench::address, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      harness::DoNotOptimize(scan::match<"[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+(?:\\.[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+)*@(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?\\.)+[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?">(view));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::address.size());
}

void scan_address_sentinel(harness::State& state) {
  const auto& texts = bench::copies_of(bench::address, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      harness::DoNotOptimize(scan::match_sentinel<"[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+(?:\\.[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+)*@(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?\\.)+[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?">(view));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::address.size());
}

void ctre_address(harness::State& state) {
  const auto& texts = bench::copies_of(bench::address, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      harness::DoNotOptimize(ctre::match<"[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+(?:\\.[a-zA-Z0-9!#$%&'*+/=?^_`|~-]+)*@(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?\\.)+[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?">(view));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::address.size());
}

void re2c_address_benchmark(harness::State& state) {
  const auto& texts = bench::copies_of(bench::address, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      const char* cursor = text.c_str();
      harness::DoNotOptimize(cursor);
      harness::DoNotOptimize(re2c_address(cursor));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::address.size());
}

const int registered = [] {
  harness::RegisterBenchmark("scan_address", scan_address);
  harness::RegisterBenchmark("scan_address_sentinel", scan_address_sentinel);
  harness::RegisterBenchmark("ctre_address", ctre_address);
  harness::RegisterBenchmark("re2c_address", re2c_address_benchmark);
  return 0;
}();

}  // namespace
