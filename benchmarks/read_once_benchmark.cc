// The same five fields off a subject that can only be read once, at the size
// where reading is the work.
//
// The comparison is between two ways of reading one source, not between two
// sources. A range that hands over a character at a time is read either as it
// comes, or copied into a window and read out of the window -- and the second
// is what `in_pieces` is for, because characters in a window lie in a row and
// the walk can step over a run of them in vectors. Both rows pay the same
// source, so what is between them is the buffering and nothing else.
//
// Two rows stand outside that comparison and are not it. The subject pointed
// at is the floor: no gathering at all, a field is where it began and how long
// it was. Blocks handed over as they are -- a range of views, which is what a
// reader that already holds them gives -- is a different subject, not a better
// way of reading this one; it is here because it says what the seams between
// blocks cost when nothing is copied to make them.
//
// And the same on a short record, because what a machine costs to enter is not
// what it costs to run.
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

struct field_views {
  std::string_view first;
  std::string_view second;
  std::string_view third;
  std::string_view fourth;
  std::string_view fifth;
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

// Four megabytes, where a field is eight hundred thousand characters long.
const std::string& wide_csv() {
  static const std::string storage = [] {
    std::string made;
    made.reserve(4000004);
    for (std::size_t field = 0; field < 5; ++field) {
      if (field) made.push_back(',');
      made.append(800000, static_cast<char>('a' + field));
    }
    return made;
  }();
  return storage;
}

// The blocks a reader would hand over, made once.
const std::vector<std::string_view>& blocks_of(std::size_t size) {
  static std::vector<std::string_view> made;
  static std::size_t theirs = 0;
  if (theirs != size) {
    theirs = size;
    made.clear();
    const std::string_view whole(wide_csv());
    for (std::size_t at = 0; at < whole.size(); at += size) {
      made.push_back(whole.substr(at, std::min(size, whole.size() - at)));
    }
  }
  return made;
}

void wide_pointed_at(harness::State& state) {
  const std::string& text = wide_csv();
  for (auto _ : state) {
    std::string_view view(text);
    harness::DoNotOptimize(view);
    field_views value = scan::scan<letters>(view);
    harness::DoNotOptimize(value);
  }
  state.SetBytesProcessed(state.iterations() * text.size());
}

void wide_read_once(harness::State& state) {
  const std::string& text = wide_csv();
  for (auto _ : state) {
    std::size_t at = 0;
    field_strings value = scan::scan<letters>(read_once(text, &at));
    harness::DoNotOptimize(value);
  }
  state.SetBytesProcessed(state.iterations() * text.size());
}

void wide_in_pieces(harness::State& state) {
  const std::string& text = wide_csv();
  for (auto _ : state) {
    std::size_t at = 0;
    field_strings value =
        scan::scan<letters>(read_once(text, &at) | scan::in_pieces<65536>);
    harness::DoNotOptimize(value);
  }
  state.SetBytesProcessed(state.iterations() * text.size());
}

// Blocks handed over as they are, which is a different subject and not another
// way of reading the one above: nothing is copied into a window, and the walk
// reads each block the way it reads a string.
void wide_blocks_handed(harness::State& state) {
  const auto& blocks = blocks_of(65536);
  for (auto _ : state) {
    field_strings value = scan::scan<letters>(std::views::all(blocks));
    harness::DoNotOptimize(value);
  }
  state.SetBytesProcessed(state.iterations() * wide_csv().size());
}

// The same subject in one block, which says what the seams between them cost.
void wide_one_block(harness::State& state) {
  const auto& blocks = blocks_of(4194304);
  for (auto _ : state) {
    field_strings value = scan::scan<letters>(std::views::all(blocks));
    harness::DoNotOptimize(value);
  }
  state.SetBytesProcessed(state.iterations() * wide_csv().size());
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
  row("scan_wide_pointed_at", wide_pointed_at);
  row("scan_wide_read_once", wide_read_once);
  row("scan_wide_in_pieces", wide_in_pieces);
  row("scan_wide_blocks_handed", wide_blocks_handed);
  row("scan_wide_one_block", wide_one_block);
  return 0;
}();

}  // namespace
