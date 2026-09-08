// The same five fields off a subject that can only be read once.
//
// A subject that can be pointed at is read by pointing: a field is where it
// began and how long it was, and the reading keeps nothing. A subject that
// arrives once and never again has to be gathered as it goes by -- and what it
// is gathered into is what this measures, because on a short record the
// gathering, and not the machine, is what such a reading costs.
//
// Three ways of keeping a field: a string, which asks an allocator for room; a
// field of room said in advance, which never allocates and stops at the brim;
// and a number, which keeps no characters at all. Two ways of feeding: one
// character at a time, and in pieces -- where the characters of a piece lie in
// a row, so the walk can step over a run of them in vectors and hand the run
// over in one call rather than one a character.
//
// The vectors only pay where the runs are long. Five fields of six characters
// in a piece of sixty-four are five runs too short to step over, and all the
// buffering buys them is a copy of every character; five fields of two hundred
// are the other way round. Both are here.
import std;
import bench.harness;
import bench.inputs;
import scan;

namespace {

// Fields kept three ways.
struct field_strings {
  std::string first;
  std::string second;
  std::string third;
  std::string fourth;
  std::string fifth;
};

struct field_rooms {
  scan::held<16> first;
  scan::held<16> second;
  scan::held<16> third;
  scan::held<16> fourth;
  scan::held<16> fifth;
};

struct field_numbers {
  int first = 0;
  int second = 0;
  int third = 0;
  int fourth = 0;
  int fifth = 0;
};

// Characters really in a row, handed over one at a time and never again.
class read_once {
 public:
  class cursor {
   public:
    using iterator_concept = std::input_iterator_tag;
    using value_type = char;
    using difference_type = std::ptrdiff_t;

    cursor() = default;
    cursor(std::string_view text, std::size_t* at) : text_(text), at_(at) {}

    [[nodiscard]] char operator*() const { return text_[*at_]; }
    cursor& operator++() {
      ++*at_;
      return *this;
    }
    void operator++(int) { ++*this; }
    [[nodiscard]] bool operator==(std::default_sentinel_t) const {
      return *at_ == text_.size();
    }

   private:
    std::string_view text_;
    std::size_t* at_ = nullptr;
  };

  read_once(std::string_view text, std::size_t* at) : text_(text), at_(at) {}
  [[nodiscard]] cursor begin() const { return cursor(text_, at_); }
  [[nodiscard]] std::default_sentinel_t end() const { return {}; }

 private:
  std::string_view text_;
  std::size_t* at_ = nullptr;
};

// Five numbers, in a subject of the same shape as the letters one.
const std::string& numeric_csv() {
  static const std::string storage = "10,200,3000,40000,5";
  return storage;
}

constexpr scan::fixed_string letters =
    "{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+},{[a-z]+}";
constexpr scan::fixed_string numbers = "{},{},{},{},{}";

// What the loop around the work costs, and nothing else. Every row below
// carries this.
void harness_floor(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

// The floor of the gathering itself: the same fields, kept the same way, off a
// subject that can be pointed at. What a row below costs over this one is what
// reading once costs.
void pointed_at_room_said(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      field_rooms value = scan::scan<letters>(view);
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

void read_once_strings(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::size_t at = 0;
      field_strings value = scan::scan<letters>(read_once(text, &at));
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

void read_once_room_said(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::size_t at = 0;
      field_rooms value = scan::scan<letters>(read_once(text, &at));
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

void read_once_numbers(harness::State& state) {
  const std::string& text = numeric_csv();
  for (auto _ : state) {
    std::size_t at = 0;
    field_numbers value = scan::scan<numbers>(read_once(text, &at));
    harness::DoNotOptimize(value);
  }
  state.SetBytesProcessed(state.iterations() * text.size());
}

void in_pieces_strings(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::size_t at = 0;
      field_strings value =
          scan::scan<letters>(read_once(text, &at) | scan::in_pieces<64>);
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

void in_pieces_room_said(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::size_t at = 0;
      field_rooms value =
          scan::scan<letters>(read_once(text, &at) | scan::in_pieces<64>);
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

void in_pieces_numbers(harness::State& state) {
  const std::string& text = numeric_csv();
  for (auto _ : state) {
    std::size_t at = 0;
    field_numbers value =
        scan::scan<numbers>(read_once(text, &at) | scan::in_pieces<64>);
    harness::DoNotOptimize(value);
  }
  state.SetBytesProcessed(state.iterations() * text.size());
}

// A thousand characters, where a run is two hundred long and stepping over it
// is worth the piece it is read out of.
void read_once_long(harness::State& state) {
  const std::string& text = bench::long_csv();
  for (auto _ : state) {
    std::size_t at = 0;
    field_rooms value = scan::scan<letters>(read_once(text, &at));
    harness::DoNotOptimize(value);
  }
  state.SetBytesProcessed(state.iterations() * text.size());
}

void in_pieces_long(harness::State& state) {
  const std::string& text = bench::long_csv();
  for (auto _ : state) {
    std::size_t at = 0;
    field_rooms value =
        scan::scan<letters>(read_once(text, &at) | scan::in_pieces<512>);
    harness::DoNotOptimize(value);
  }
  state.SetBytesProcessed(state.iterations() * text.size());
}

const int registered = [] {
  const auto row = [](const char* name, void (*body)(harness::State&)) {
    harness::RegisterBenchmark(name, body)
        ->Repetitions(7)
        ->ReportAggregatesOnly(true);
  };
  row("harness_floor", harness_floor);
  row("scan_pointed_at_room_said", pointed_at_room_said);
  row("scan_read_once_strings", read_once_strings);
  row("scan_read_once_room_said", read_once_room_said);
  row("scan_read_once_numbers", read_once_numbers);
  row("scan_in_pieces_strings", in_pieces_strings);
  row("scan_in_pieces_room_said", in_pieces_room_said);
  row("scan_in_pieces_numbers", in_pieces_numbers);
  row("scan_read_once_long", read_once_long);
  row("scan_in_pieces_long", in_pieces_long);
  return 0;
}();

}  // namespace
