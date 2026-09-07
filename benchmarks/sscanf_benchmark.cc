// The same job as the standard library's, said the two ways.
//
// `sscanf` is what a C++ program reaches for when it wants numbers out of a
// string, and it is the honest thing to be measured against -- the way
// `std::format` is measured against `printf`. What is being compared is not
// only speed: the format `sscanf` is given is a string it reads at run time,
// on every call, and the places in it are matched to the arguments by nothing
// at all. Ours is read once, while the program is compiled, against the type
// the values go into.
//
// So the formats here are written to do what `sscanf` does, rather than what we
// would write for ourselves:
//
//   * `%d` skips any leading whitespace, so every number is preceded by
//     `{*\s*}` -- an unkept place that takes as much of it as there is.
//   * `sscanf` stops where the format runs out and ignores the rest of the
//     subject, so these read a head: `scan_prefix`, not `scan`.
//   * `%31[a-z]` takes at most thirty-one characters, so the fields are
//     written `[a-z]{1,31}` and stop in the same place.
//
// That leaves two differences nothing can spell away. `sscanf` hands back how
// many fields it filled and leaves the rest as they were, where a scan is all
// or nothing; and an integer too big for its type is undefined behaviour
// there, where here it is an error the caller is handed.
//
// The words are measured three ways, because otherwise the comparison would be
// picked to suit us. `sscanf` copies each field into room the caller said in
// advance; `scan_words_held` does exactly that and is the pair that compares
// like with like. `scan_words` hands back views into the subject and copies
// nothing, which `sscanf` cannot do at all, and `scan_words_strings` asks an
// allocator for the room, which it also cannot do. All three are here.
import std;
import bench.harness;
import bench.inputs;
import scan;

namespace {

struct two_numbers { int left; int right; };
struct stamp { int year; int month; int day; int hour; int minute; int second; };
struct field_views {
  std::string_view a, b, c, d, e;
};

// The same fields written into room said in advance, which is what `sscanf`
// does with `char[32]` and `%31[a-z]`. Both copy the characters; both stop at
// the brim rather than reaching for more. This is the pair that compares like
// with like.
struct field_buffers {
  scan::held<32> a, b, c, d, e;
};

inline constexpr std::string_view pair_text = "12:34";

void sscanf_two_numbers(harness::State& state) {
  const auto& texts = bench::copies_of(pair_text, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      const char* cursor = text.c_str();
      harness::DoNotOptimize(cursor);
      two_numbers value{};
      int taken = std::sscanf(cursor, "%d:%d", &value.left, &value.right);
      harness::DoNotOptimize(taken);
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() * pair_text.size());
}

void scan_two_numbers(harness::State& state) {
  const auto& texts = bench::copies_of(pair_text, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      two_numbers value = scan::scan_prefix<"{*\\s*}{}:{*\\s*}{}">(view);
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() * pair_text.size());
}

void sscanf_timestamp(harness::State& state) {
  const auto& texts = bench::copies_of(bench::timestamp, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      const char* cursor = text.c_str();
      harness::DoNotOptimize(cursor);
      stamp value{};
      int taken =
          std::sscanf(cursor, "%d-%d-%dT%d:%d:%d", &value.year, &value.month,
                      &value.day, &value.hour, &value.minute, &value.second);
      harness::DoNotOptimize(taken);
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::timestamp.size());
}

void scan_timestamp_fields(harness::State& state) {
  const auto& texts = bench::copies_of(bench::timestamp, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      stamp value = scan::scan_prefix<
          "{*\\s*}{}-{*\\s*}{}-{*\\s*}{}T{*\\s*}{}:{*\\s*}{}:{*\\s*}{}">(view);
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::timestamp.size());
}

// Five words. `sscanf` writes them into buffers whose size the caller had to
// pick, and the width in the format has to be written out again for each one
// or the whole thing is a way to overrun the stack.
void sscanf_words(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      const char* cursor = text.c_str();
      harness::DoNotOptimize(cursor);
      char a[32]{}, b[32]{}, c[32]{}, d[32]{}, e[32]{};
      int taken = std::sscanf(
          cursor, "%31[a-z],%31[a-z],%31[a-z],%31[a-z],%31[a-z]", a, b, c, d, e);
      harness::DoNotOptimize(taken);
      harness::DoNotOptimize(a);
      harness::DoNotOptimize(e);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

void scan_words(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      field_views value =
          scan::scan_prefix<
              "{[a-z]{1,31}},{[a-z]{1,31}},{[a-z]{1,31}},{[a-z]{1,31}},"
              "{[a-z]{1,31}}">(view);
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

// Copying into room said in advance, which is what `sscanf` does with
// `char[32]` and `%31[a-z]`: the pair that measures the same work.
//
// Not quite the same work, and the difference is worth knowing. A width in
// `sscanf` stops the field there -- `%31[a-z]` reads thirty-one letters and
// leaves the rest for whatever the format says next, which is usually a comma
// and usually a failure. `held<32>` is a place to put characters, not a limit
// on the match: the field takes every letter it can, the first thirty-two are
// kept, and the rest are dropped with the overflow remembered.
void scan_words_held(harness::State& state) {
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      field_buffers value =
          scan::scan_prefix<
              "{[a-z]{1,31}},{[a-z]{1,31}},{[a-z]{1,31}},{[a-z]{1,31}},"
              "{[a-z]{1,31}}">(view);
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

// And into strings, which asks an allocator for the room instead. Nothing in
// `sscanf` does this at all; it is here to price the third answer to the same
// question.
void scan_words_strings(harness::State& state) {
  struct field_strings {
    std::string a, b, c, d, e;
  };
  const auto& texts = bench::copies_of(bench::csv, 32);
  for (auto _ : state) {
    for (const std::string& text : texts) {
      std::string_view view(text);
      harness::DoNotOptimize(view);
      field_strings value =
          scan::scan_prefix<
              "{[a-z]{1,31}},{[a-z]{1,31}},{[a-z]{1,31}},{[a-z]{1,31}},"
              "{[a-z]{1,31}}">(view);
      harness::DoNotOptimize(value);
    }
  }
  state.SetBytesProcessed(state.iterations() * texts.size() *
                          bench::csv.size());
}

const int registered = [] {
  harness::RegisterBenchmark("sscanf_two_numbers", sscanf_two_numbers);
  harness::RegisterBenchmark("scan_two_numbers", scan_two_numbers);
  harness::RegisterBenchmark("sscanf_timestamp", sscanf_timestamp);
  harness::RegisterBenchmark("scan_timestamp_fields", scan_timestamp_fields);
  harness::RegisterBenchmark("sscanf_words", sscanf_words);
  harness::RegisterBenchmark("scan_words", scan_words);
  harness::RegisterBenchmark("scan_words_held", scan_words_held);
  harness::RegisterBenchmark("scan_words_strings", scan_words_strings);
  return 0;
}();

}  // namespace
