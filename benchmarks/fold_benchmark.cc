// A fold over turns, three ways: this library, the same machine written out,
// and the same reading written by hand.
//
// All three read one subject, do the same arithmetic on the same turns, and
// hand back the same tail as a view of it. What differs is only how
// the machine is got through -- a walk for the library, labels and direct
// jumps for the machine written out, loops and a pointer for the hand-written
// one -- so the distance between the columns is the price of the walk and
// nothing else.
//
// One file, and the columns are ordinary functions in it. Separate translation
// units would buy nothing: these benchmarks are built with link-time
// optimisation, which sees across them anyway, and the library's column is
// inlined into the loop no matter what. Putting the others behind a call the
// optimiser cannot see into would not make the comparison fairer, it would
// make it uneven in the library's favour.
//
// The machine written out calls the scanner's own hooks rather than a copy of
// them, so there is no question of the two columns counting differently.
//
// Every column's answer is consumed -- the number and the tail both. Consume
// only the number and the tail stops being looked for in whichever column the
// optimiser can see through, which is the library's, and the comparison turns
// into one between doing the work and not doing it.
//
// The heaps are short on purpose -- `_X`, `__XX` -- because a word-at-a-time
// step does not pay for itself on them: a run of one to four characters is
// shorter than what a vector steps over at once. So the scalar walk is named
// as well, and both are here.
import std;
import bench.harness;
import scan;

namespace {

// What the hooks add up: a number written as weighed heaps. A heap of
// underscores says which decimal place it is, and the marks after it say how
// many -- `X` one, `Y` two -- so twelve is `__X_XX`, and `__X_Y` as well.
//
// A heap gone missing changes the number and not only its length, and so does
// a single mark: the marks of a heap are added up as they arrive, which is
// what a fold does that a match does not.
struct tally {
  unsigned long value = 0;
};

// The subject: `value=(`, then heaps of `_X` and `__XX` in turn, then `)` and
// a tail of letters. One is worth one and the other twenty, so a heap read
// wrongly shows in the number rather than hiding in it.
std::string heaps_of(std::size_t count) {
  std::string made = "value=(";
  for (std::size_t heap = 0; heap < count; ++heap)
    made += (heap % 2) ? "__XX" : "_X";
  made += ")abcdefgh";
  return made;
}

const std::string& subject_of(std::size_t count) {
  static std::map<std::size_t, std::string> kept;
  auto found = kept.find(count);
  if (found == kept.end()) found = kept.emplace(count, heaps_of(count)).first;
  return found->second;
}

}  // namespace

template <>
struct scan::scanner<tally> {
  [[nodiscard]] static constexpr std::string_view pattern() {
    return R"(\(((_+)(X|Y)*)*\))";
  }
  struct state_type {
    unsigned long total = 0;
    unsigned place = 0;
    unsigned marks = 0;
  };
  [[nodiscard]] static constexpr state_type begin_groups() { return {}; }
  static constexpr void opened_group(state_type& one, scan::group_at<0>) {
    one.place = 0;
    one.marks = 0;
  }
  static constexpr void closed_group(state_type& one, scan::group_at<0>) {
    unsigned long weight = 1;
    for (unsigned step = 1; step < one.place; ++step) weight *= 10;
    one.total += weight * one.marks;
  }
  // The underscores of this heap, one call each.
  static constexpr void push_group(state_type& one, scan::group_at<1>, char) {
    ++one.place;
  }
  // And its marks, added up as they come. Both of these groups are handed
  // every character they match, which is the work being priced.
  static constexpr void push_group(state_type& one, scan::group_at<2>,
                                   char letter) {
    one.marks += letter == 'Y' ? 2u : 1u;
  }
  // And nothing for the heap as a whole. A fold that says nothing about a
  // group is not handed that group's characters at all, which is what the two
  // columns beside this one do; an empty catch-all taking the number would say
  // the opposite, and this column would be fed every character of every heap
  // twice over to throw one of them away.
  [[nodiscard]] static constexpr tally finish_groups(state_type one) {
    return {one.total};
  }
};

namespace {

// A number folded out of its groups, and a tail said as a view of the subject.
//
// Pointed at rather than copied, in all three columns alike. Copying it is the
// same work in every column, so it is not what the distance between them is
// made of -- it only adds the same amount to each and makes the walk, which is
// what is being priced, a smaller part of what is measured. A subject that is
// there in one piece is read by taking a view of it, which is what a program
// reading one wants anyway.
struct reading {
  tally number;
  std::string_view tail;
};

// What the other two columns hand back, which is what the library hands back
// written out flat. The tail is the same view of the same subject, so the
// three columns differ in their walk and in nothing else.
struct answer {
  unsigned long value = 0;
  std::string_view tail{};
  bool matched = false;
};

using hooks = scan::scanner<tally>;
using tstate = hooks::state_type;

// The same machine, written out: a label for a state, a direct jump for a
// move, one switch on the character for the fork. This is the shape a scanner
// generator emits, and it came out of this library's own automaton state for
// state.
//
// It takes the longest beginning rather than the whole subject, which is what
// the automaton does. On these subjects the two are the same, because the tail
// runs to the end.
answer fold_written_out(const char* p, const char* e) {
  tstate ts{};
  const char* tail_from = nullptr;
  const char* tail_to = nullptr;
  bool matched = false;
  unsigned long best = 0;
  goto s0;

s0:
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 118: {
    ++p; goto s1;
  }
  default: goto done;
  }

s1:
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 97: {
    ++p; goto s2;
  }
  default: goto done;
  }

s2:
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 108: {
    ++p; goto s3;
  }
  default: goto done;
  }

s3:
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 117: {
    ++p; goto s4;
  }
  default: goto done;
  }

s4:
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 101: {
    ++p; goto s5;
  }
  default: goto done;
  }

s5:
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 61: {
    ++p; goto s6;
  }
  default: goto done;
  }

s6:
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 40: {
    ++p; goto s7;
  }
  default: goto done;
  }

s7:
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 41: {
    ++p; goto s8;
  }
  case 95: {
    hooks::opened_group(ts, scan::group_at<0>{});
    hooks::push_group(ts, scan::group_at<1>{}, *p);
    ++p; goto s9;
  }
  default: goto done;
  }

s8:
  matched = true; best = ts.total;
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 97 ... 122: {
    tail_from = p;
    ++p;
    tail_to = p;
    goto s10;
  }
  default: goto done;
  }

s9:
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 95: {
    hooks::push_group(ts, scan::group_at<1>{}, *p);
    ++p; goto s9;
  }
  case 88:
  case 89: {
    hooks::push_group(ts, scan::group_at<2>{}, *p);
    ++p; goto s11;
  }
  case 41: {
    hooks::closed_group(ts, scan::group_at<0>{});
    ++p; goto s8;
  }
  default: goto done;
  }

s10:
  matched = true; best = ts.total;
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 97 ... 122: {
    ++p;
    tail_to = p;
    goto s10;
  }
  default: goto done;
  }

s11:
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 88:
  case 89: {
    hooks::push_group(ts, scan::group_at<2>{}, *p);
    ++p; goto s11;
  }
  case 95: {
    hooks::closed_group(ts, scan::group_at<0>{});
    hooks::opened_group(ts, scan::group_at<0>{});
    hooks::push_group(ts, scan::group_at<1>{}, *p);
    ++p; goto s9;
  }
  case 41: {
    hooks::closed_group(ts, scan::group_at<0>{});
    ++p; goto s8;
  }
  default: goto done;
  }

done:
  answer out{};
  out.matched = matched;
  out.value = matched ? best : 0;
  if (matched && tail_from)
    out.tail = std::string_view(tail_from,
                                static_cast<std::size_t>(tail_to - tail_from));
  return out;
}

// The same reading, written the way it is usually written by hand.
//
// No machine and no tables: a pointer, a few loops, the bounds checked as they
// come, the arithmetic in the open. This is the column to beat, because it is
// what a person writes when the reading is small enough to write out and there
// is no reason to reach for anything else.
answer fold_by_hand(const char* p, const char* e) {
  answer out{};
  constexpr std::string_view head = "value=(";
  if (static_cast<std::size_t>(e - p) < head.size()) return out;
  if (std::string_view(p, head.size()) != head) return out;
  p += head.size();

  // A heap is one or more underscores and then the marks that belong to it.
  // The underscores say which decimal place the marks fall in, so `_X` is one
  // and `__XX` is twenty.
  unsigned long total = 0;
  while (p != e && *p == '_') {
    unsigned place = 0;
    while (p != e && *p == '_') {
      ++place;
      ++p;
    }
    unsigned marks = 0;
    while (p != e && (*p == 'X' || *p == 'Y')) {
      marks += *p == 'Y' ? 2u : 1u;
      ++p;
    }
    unsigned long weight = 1;
    for (unsigned step = 1; step < place; ++step) weight *= 10;
    total += weight * marks;
  }
  if (p == e || *p != ')') return out;
  ++p;

  const char* gathered_from = p;
  while (p != e && *p >= 'a' && *p <= 'z') ++p;
  const std::string_view gathered(gathered_from,
                                  static_cast<std::size_t>(p - gathered_from));

  // The whole subject or nothing, which is what the library's reading asks
  // for.
  if (p != e) return out;

  out.matched = true;
  out.value = total;
  out.tail = gathered;
  return out;
}

void scan_fold(harness::State& state) {
  const std::string& text = subject_of(static_cast<std::size_t>(state.range(0)));
  for (auto _ : state) {
    std::string_view view(text);
    harness::DoNotOptimize(view);
    const reading got =
        scan::scan<"value={}{[a-z]*}">.scalar()(view).of<reading>();
    harness::DoNotOptimize(got.number.value);
    harness::DoNotOptimize(got.tail);
  }
  state.SetBytesProcessed(state.iterations() * text.size());
}

void scan_fold_vectors(harness::State& state) {
  const std::string& text = subject_of(static_cast<std::size_t>(state.range(0)));
  for (auto _ : state) {
    std::string_view view(text);
    harness::DoNotOptimize(view);
    const reading got = scan::scan<"value={}{[a-z]*}">(view).of<reading>();
    harness::DoNotOptimize(got.number.value);
    harness::DoNotOptimize(got.tail);
  }
  state.SetBytesProcessed(state.iterations() * text.size());
}

void written_out(harness::State& state) {
  const std::string& text = subject_of(static_cast<std::size_t>(state.range(0)));
  for (auto _ : state) {
    const char* from = text.data();
    harness::DoNotOptimize(from);
    const answer got = fold_written_out(from, from + text.size());
    harness::DoNotOptimize(got.value);
    harness::DoNotOptimize(got.tail);
  }
  state.SetBytesProcessed(state.iterations() * text.size());
}

void by_hand(harness::State& state) {
  const std::string& text = subject_of(static_cast<std::size_t>(state.range(0)));
  for (auto _ : state) {
    const char* from = text.data();
    harness::DoNotOptimize(from);
    const answer got = fold_by_hand(from, from + text.size());
    harness::DoNotOptimize(got.value);
    harness::DoNotOptimize(got.tail);
  }
  state.SetBytesProcessed(state.iterations() * text.size());
}

// The columns are worth comparing only if they agree, so they are made to say
// so before anything is timed. A column that stopped reading early would
// otherwise show as the fast one.
void they_agree() {
  for (std::size_t count : {std::size_t{10}, std::size_t{100}, std::size_t{1000}}) {
    const std::string& text = subject_of(count);
    const std::string_view view(text);
    const reading walked =
        scan::scan<"value={}{[a-z]*}">.scalar()(view).of<reading>();
    const reading stepped = scan::scan<"value={}{[a-z]*}">(view).of<reading>();
    const answer written = fold_written_out(view.data(), view.data() + view.size());
    const answer by_the_hand = fold_by_hand(view.data(), view.data() + view.size());
    const bool agreed =
        written.matched && by_the_hand.matched &&
        walked.number.value == stepped.number.value &&
        walked.number.value == written.value &&
        walked.number.value == by_the_hand.value && walked.tail == stepped.tail &&
        walked.tail == written.tail && walked.tail == by_the_hand.tail;
    if (!agreed) {
      std::println("the columns do not agree at {} heaps: {} {} {} {}, "
                   "tails \"{}\" \"{}\" \"{}\" \"{}\"",
                   count, walked.number.value, stepped.number.value,
                   written.value, by_the_hand.value, walked.tail, stepped.tail,
                   written.tail, by_the_hand.tail);
      std::abort();
    }
  }
}

const int registered = [] {
  they_agree();
  for (auto* one : {harness::RegisterBenchmark("scan_fold", scan_fold),
                    harness::RegisterBenchmark("scan_fold_vectors", scan_fold_vectors),
                    harness::RegisterBenchmark("fold_written_out", written_out),
                    harness::RegisterBenchmark("fold_by_hand", by_hand)})
    one->Arg(10)->Arg(100)->Arg(1000);
  return 0;
}();

}  // namespace
