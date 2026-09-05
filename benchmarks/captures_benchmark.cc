// One pattern, with captures: five comma-separated fields taken out of the
// subject rather than only recognised.
//
// All three engines take the fields out. re2c does it with tags -- `@name`
// binds the position where it stands -- so this compares three pieces of code
// doing the same work, rather than extraction against recognition.
import std;
import bench.harness;
import bench.inputs;
import ctre;
import scan;

bool re2c_captures(const char* cursor, const char** positions);

namespace {

struct field_strings {
  std::string first;
  std::string second;
  std::string third;
  std::string fourth;
  std::string fifth;
};

void scan_captures_strings(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      field_strings value =
          scan::scan<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}">(view);
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

void ctre_captures(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      auto value =
          ctre::match<"([a-z]+),([a-z]+),([a-z]+),([a-z]+),([a-z]+)">(view);
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

void re2c_captures_benchmark(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      const char* cursor = text.c_str();
      const char* positions[10] = {};
      harness::DoNotOptimize(cursor);
      harness::DoNotOptimize(re2c_captures(cursor, positions));
      harness::DoNotOptimize(positions);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

const int registered = [] {
  harness::RegisterBenchmark("scan_captures_strings", scan_captures_strings);
  harness::RegisterBenchmark("ctre_captures", ctre_captures);
  harness::RegisterBenchmark("re2c_captures", re2c_captures_benchmark);
  return 0;
}();

}  // namespace
