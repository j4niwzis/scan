// Two loops written by hand, doing the same thing in the two shapes the
// generated code takes. No library and no pattern compilation: this measures
// only what the shape of the loop is worth, so that a difference between the
// engines can be told apart from a difference between the loops they emit.
import std;
import bench.harness;
import bench.inputs;

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

void loop_scan_shape(harness::State& state) {
  const std::string& text =
      bench::long_word(static_cast<std::size_t>(state.range(0)));
  for (auto _ : state) {
    const char* cursor = text.c_str();
    harness::DoNotOptimize(cursor);
    harness::DoNotOptimize(scan_shape(cursor));
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<std::size_t>(state.range(0)));
}


void loop_re2c_shape(harness::State& state) {
  const std::string& text =
      bench::long_word(static_cast<std::size_t>(state.range(0)));
  for (auto _ : state) {
    const char* cursor = text.c_str();
    harness::DoNotOptimize(cursor);
    harness::DoNotOptimize(re2c_shape(cursor));
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<std::size_t>(state.range(0)));
}

namespace {
const int registered = [] {
  harness::RegisterBenchmark("loop_scan_shape", loop_scan_shape)
      ->Arg(4096)->Arg(65536);
  harness::RegisterBenchmark("loop_re2c_shape", loop_re2c_shape)
      ->Arg(4096)->Arg(65536);
  return 0;
}();
}  // namespace
