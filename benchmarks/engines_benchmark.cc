// Four engines on two shapes, so that the difference between them is the
// engine and nothing else.
//
// The shapes are the two the other benchmarks already use: a fixed-width
// timestamp, recognised and not taken apart, and five fields with the fields
// kept. What each engine is given is as close to the same thing as the engines
// allow, and where it is not, the row says so:
//
//   * this library is given a `string_view` and a terminator, and hands back
//     views into the subject;
//   * CTRE is given the same view and hands back views;
//   * RE2 is given the same view and hands back views, but its pattern is
//     read at run time -- the compiling is not in the measurement, the
//     dispatch inside its walk is;
//   * re2c is given a pointer and a terminator, with no length at all, and
//     is generated ahead of time from the same pattern.
import std;
import bench.harness;
import bench.inputs;
import bench.re2;
import ctre;
import scan;

bool re2c_timestamp(const char* cursor);
bool re2c_captures(const char* cursor, const char** positions);

namespace {

constexpr auto stamp_pattern =
    "[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}";
constexpr auto row_pattern = "([a-z]+),([a-z]+),([a-z]+),([a-z]+),([a-z]+)";

struct field_views {
  std::string_view first, second, third, fourth, fifth;
};

// Recognising a timestamp.

void engines_timestamp_scan(harness::State& state) {
  const auto& texts = bench::copies_of(bench::timestamp, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      harness::DoNotOptimize(static_cast<bool>(
          scan::match<stamp_pattern>.sentinel().scalar()(view)));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::timestamp.size());
}

void engines_timestamp_ctre(harness::State& state) {
  const auto& texts = bench::copies_of(bench::timestamp, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      harness::DoNotOptimize(
          static_cast<bool>(ctre::match<stamp_pattern>(view)));
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::timestamp.size());
}

void engines_timestamp_re2(harness::State& state) {
  const bench::re2_engine engine(stamp_pattern);
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

void engines_timestamp_re2c(harness::State& state) {
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

// Taking five fields out.

void engines_fields_scan(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      field_views value =
          scan::scan<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}">.sentinel()(
              view);
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

void engines_fields_ctre(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      if (auto found = ctre::match<row_pattern>(view)) {
        field_views value{found.get<1>(), found.get<2>(), found.get<3>(),
                          found.get<4>(), found.get<5>()};
        harness::DoNotOptimize(value);
      }
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

void engines_fields_re2(harness::State& state) {
  const bench::re2_engine engine(row_pattern);
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      std::array<std::string_view, 5> found{};
      if (engine.whole_with_five(view, found)) {
        field_views value{found[0], found[1], found[2], found[3], found[4]};
        harness::DoNotOptimize(value);
      }
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

void engines_fields_re2c(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      const char* cursor = text.c_str();
      harness::DoNotOptimize(cursor);
      const char* positions[10] = {};
      if (re2c_captures(cursor, positions)) {
        field_views value{
            std::string_view(positions[0],
                             static_cast<std::size_t>(positions[1] - positions[0])),
            std::string_view(positions[2],
                             static_cast<std::size_t>(positions[3] - positions[2])),
            std::string_view(positions[4],
                             static_cast<std::size_t>(positions[5] - positions[4])),
            std::string_view(positions[6],
                             static_cast<std::size_t>(positions[7] - positions[6])),
            std::string_view(positions[8],
                             static_cast<std::size_t>(positions[9] - positions[8]))};
        harness::DoNotOptimize(value);
      }
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

const int registered = [] {
  const auto row = [](const char* name, void (*body)(harness::State&)) {
    harness::RegisterBenchmark(name, body)
        ->Repetitions(7)
        ->ReportAggregatesOnly(true);
  };
  row("engines_timestamp_scan", engines_timestamp_scan);
  row("engines_timestamp_ctre", engines_timestamp_ctre);
  row("engines_timestamp_re2", engines_timestamp_re2);
  row("engines_timestamp_re2c", engines_timestamp_re2c);
  row("engines_fields_scan", engines_fields_scan);
  row("engines_fields_ctre", engines_fields_ctre);
  row("engines_fields_re2", engines_fields_re2);
  row("engines_fields_re2c", engines_fields_re2c);
  return 0;
}();

}  // namespace
