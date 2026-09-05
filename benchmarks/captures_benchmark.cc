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

// Fields as views into the subject: nothing is allocated, which is what a
// contiguous input allows and what the other two engines do. The row below it
// asks for the same fields as strings -- copies, which is not waste but the
// only thing possible when the input is a range read once that cannot be
// pointed into afterwards, and which neither of the other two can do at all.
struct field_views {
  std::string_view first;
  std::string_view second;
  std::string_view third;
  std::string_view fourth;
  std::string_view fifth;
};

struct field_strings {
  std::string first;
  std::string second;
  std::string third;
  std::string fourth;
  std::string fifth;
};

// What the loop around the work costs, and nothing else: the same subjects,
// the same two barriers, no scanning between them. Every row below carries
// this, so it is the floor none of them can go under, and the difference
// between a row and this one is the engine.
void harness_floor(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      field_views value{view, view, view, view, view};
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

// The same floor for the row that returns strings: the five fields are already
// known, and all this does is make strings of them. None is longer than seven
// characters, so none of them should reach for the allocator at all, and this
// row says what they cost when nothing is scanned.
void harness_floor_strings(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      field_strings value{std::string(view.substr(0, 5)),
                          std::string(view.substr(6, 5)),
                          std::string(view.substr(12, 7)),
                          std::string(view.substr(20, 5)),
                          std::string(view.substr(26, 4))};
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

void scan_captures_views(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      field_views value =
          scan::scan<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}">(view);
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

// The same fields, from input that carries a terminator the pattern never
// matches -- which a `std::string` always does. The loop then tests only the
// character, not the character and the end of the input.
void scan_captures_views_sentinel(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      field_views value =
          scan::scan_sentinel<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}">(
              view);
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

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
  harness::RegisterBenchmark("harness_floor", harness_floor);
  harness::RegisterBenchmark("harness_floor_strings", harness_floor_strings);
  harness::RegisterBenchmark("scan_captures_views", scan_captures_views);
  harness::RegisterBenchmark("scan_captures_views_sentinel",
                             scan_captures_views_sentinel);
  harness::RegisterBenchmark("scan_captures_strings", scan_captures_strings);
  harness::RegisterBenchmark("ctre_captures", ctre_captures);
  harness::RegisterBenchmark("re2c_captures", re2c_captures_benchmark);
  return 0;
}();

}  // namespace
