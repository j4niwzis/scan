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
          scan::scan<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}">.sentinel()(
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

// The fields, and not ten offsets into the subject.
//
// This row used to hand back the positions the generated scanner had left and
// stop there, while the rows above it handed back five views -- a pointer and a
// length each, which is what a caller asked for. That is two nanoseconds of work
// on a thirty byte subject, and all of the difference between the two engines
// on it: matching the same pattern without taking anything out costs this
// library the same six nanoseconds it costs re2c to match and place its tags.
// So both sides now build the same thing.
[[nodiscard]] inline field_views views_from(const char* const* positions) {
  const auto field = [&](std::size_t index) {
    return std::string_view(
        positions[index * 2],
        static_cast<std::size_t>(positions[index * 2 + 1] -
                                 positions[index * 2]));
  };
  return field_views{field(0), field(1), field(2), field(3), field(4)};
}

// The same work on a subject a thousand bytes long, for both engines. What a
// row like this reports is a cost per byte, which is what the loop decides;
// everything around the match is the same handful of nanoseconds it was, and is
// now a fortieth of the total rather than half of it.
void scan_captures_long(harness::State& state) {
  const std::string& text = bench::long_csv();
  for (auto _ : state) {
    std::string_view view(text);
    harness::DoNotOptimize(view);
    field_views value =
        scan::scan<"{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}">.sentinel()(
            view);
    harness::DoNotOptimize(value);
  }
  state.SetBytesProcessed(state.iterations() * text.size());
}

void re2c_captures_long(harness::State& state) {
  const std::string& text = bench::long_csv();
  for (auto _ : state) {
    const char* cursor = text.c_str();
    const char* positions[10] = {};
    harness::DoNotOptimize(cursor);
    if (re2c_captures(cursor, positions)) {
      field_views value = views_from(positions);
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * text.size());
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
      if (re2c_captures(cursor, positions)) {
        field_views value = views_from(positions);
        harness::DoNotOptimize(value);
      }
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

// Seven runs of each row, reported as median and spread.
//
// One run of each says nothing about a difference of a few per cent. Between
// two builds of this file with nothing changed in it, the row timing an
// unmodified library moved by eight per cent, which is more than several of the
// steps that were worth taking. A median of seven with the spread beside it
// says which differences are real and which are the machine.
const int registered = [] {
  const auto row = [](const char* name, void (*body)(harness::State&)) {
    harness::RegisterBenchmark(name, body)
        ->Repetitions(7)
        ->ReportAggregatesOnly(true);
  };
  row("harness_floor", harness_floor);
  row("harness_floor_strings", harness_floor_strings);
  row("scan_captures_views", scan_captures_views);
  row("scan_captures_views_sentinel", scan_captures_views_sentinel);
  row("scan_captures_strings", scan_captures_strings);
  row("scan_captures_long", scan_captures_long);
  row("re2c_captures_long", re2c_captures_long);
  row("ctre_captures", ctre_captures);
  row("re2c_captures", re2c_captures_benchmark);
  return 0;
}();

}  // namespace
