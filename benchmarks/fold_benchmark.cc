// A fold over turns, three ways: this library, the same machine written out,
// and the same reading written by hand.
//
// All three read one subject, do the same arithmetic on the same turns, and
// gather the same tail into the same room. What differs is only how
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
// only the number and the tail stops being gathered in whichever column the
// optimiser can see through, which is the library's, and the comparison turns
// into one between doing the work and not doing it.
//
// The turns are short on purpose -- `_A`, `__B` -- because a word-at-a-time
// step does not pay for itself on them: a run of one to four characters is
// shorter than what a vector steps over at once. So the scalar walk is named
// as well, and both are here.
import std;
import bench.harness;
import scan;

namespace {

// What the hooks count: each turn puts its digit in its own decimal place, so
// a turn gone missing changes the number and not only its length.
struct tally {
  unsigned long value = 0;
};

// The subject: `value=(`, then turns of `_A` and `__B` in turn, then `)` and a
// tail of letters.
std::string turns_of(std::size_t count) {
  std::string made = "value=(";
  for (std::size_t turn = 0; turn < count; ++turn)
    made += (turn % 2) ? "__B" : "_A";
  made += ")abcdefgh";
  return made;
}

const std::string& subject_of(std::size_t count) {
  static std::map<std::size_t, std::string> kept;
  auto found = kept.find(count);
  if (found == kept.end()) found = kept.emplace(count, turns_of(count)).first;
  return found->second;
}

}  // namespace

template <>
struct scan::scanner<tally> {
  [[nodiscard]] static constexpr std::string_view pattern() {
    return R"(\(((_+)((A)|(B)))*\))";
  }
  struct state_type {
    unsigned long total = 0;
    unsigned place = 0;
    unsigned digit = 0;
  };
  [[nodiscard]] static constexpr state_type begin_groups() { return {}; }
  static constexpr void opened_group(state_type& one, scan::group_at<0>) {
    one.place = 0;
    one.digit = 0;
  }
  static constexpr void closed_group(state_type& one, scan::group_at<0>) {
    unsigned long weight = 1;
    for (unsigned step = 1; step < one.place; ++step) weight *= 10;
    one.total += weight * one.digit;
  }
  static constexpr void push_group(state_type& one, scan::group_at<1>, char) {
    ++one.place;
  }
  static constexpr void push_group(state_type& one, scan::group_at<3>, char) {
    one.digit = 1;
  }
  static constexpr void push_group(state_type& one, scan::group_at<4>, char) {
    one.digit = 2;
  }
  static constexpr void push_group(state_type&, std::size_t, char) {}
  [[nodiscard]] static constexpr tally finish_groups(state_type one) {
    return {one.total};
  }
};

namespace {

// A number folded out of its groups, and a tail gathered into room said in
// advance.
//
// The tail is gathered rather than pointed at on purpose: gathering is the
// work being priced here, and a column that only wrote down where the tail
// began would be measured against two columns that wrote it out.
struct reading {
  tally number;
  scan::held<8> tail;
};

// What the other two columns hand back, which is what the library hands back
// written out flat. The tail is gathered into the same room, by the same
// calls, so the three columns differ in their walk and in nothing else.
struct answer {
  unsigned long value = 0;
  scan::held<8> tail{};
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
  scan::held<8> tail{};
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
    tail.push_back(*p);
    ++p; goto s10;
  }
  default: goto done;
  }

s9:
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 65: {
    hooks::push_group(ts, scan::group_at<3>{}, *p);
    ++p; goto s11;
  }
  case 66: {
    hooks::push_group(ts, scan::group_at<4>{}, *p);
    ++p; goto s12;
  }
  case 95: {
    hooks::push_group(ts, scan::group_at<1>{}, *p);
    ++p; goto s13;
  }
  default: goto done;
  }

s10:
  matched = true; best = ts.total;
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 97 ... 122: {
    tail.push_back(*p);
    ++p; goto s10;
  }
  default: goto done;
  }

s11:
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 41: {
    hooks::closed_group(ts, scan::group_at<0>{});
    ++p; goto s8;
  }
  case 95: {
    hooks::closed_group(ts, scan::group_at<0>{});
    hooks::opened_group(ts, scan::group_at<0>{});
    hooks::push_group(ts, scan::group_at<1>{}, *p);
    ++p; goto s9;
  }
  default: goto done;
  }

s12:
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 41: {
    hooks::closed_group(ts, scan::group_at<0>{});
    ++p; goto s8;
  }
  case 95: {
    hooks::closed_group(ts, scan::group_at<0>{});
    hooks::opened_group(ts, scan::group_at<0>{});
    hooks::push_group(ts, scan::group_at<1>{}, *p);
    ++p; goto s9;
  }
  default: goto done;
  }

s13:
  if (p == e) goto done;
  switch (static_cast<unsigned char>(*p)) {
  case 65: {
    hooks::push_group(ts, scan::group_at<3>{}, *p);
    ++p; goto s11;
  }
  case 66: {
    hooks::push_group(ts, scan::group_at<4>{}, *p);
    ++p; goto s12;
  }
  case 95: {
    hooks::push_group(ts, scan::group_at<1>{}, *p);
    ++p; goto s13;
  }
  default: goto done;
  }
done:
  answer out{};
  out.matched = matched;
  out.value = matched ? best : 0;
  if (matched) out.tail = tail;
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

  // A turn is one or more marks and then a letter. The marks say which place
  // the letter's digit falls in, so `_A` is one and `__B` is twenty.
  unsigned long total = 0;
  while (p != e && *p == '_') {
    unsigned place = 0;
    while (p != e && *p == '_') {
      ++place;
      ++p;
    }
    unsigned digit = 0;
    if (p != e && *p == 'A') {
      digit = 1;
    } else if (p != e && *p == 'B') {
      digit = 2;
    } else {
      return out;
    }
    ++p;
    unsigned long weight = 1;
    for (unsigned step = 1; step < place; ++step) weight *= 10;
    total += weight * digit;
  }
  if (p == e || *p != ')') return out;
  ++p;

  scan::held<8> gathered{};
  while (p != e && *p >= 'a' && *p <= 'z') {
    gathered.push_back(*p);
    ++p;
  }

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
    harness::DoNotOptimize(got.tail.view());
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
    harness::DoNotOptimize(got.tail.view());
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
    harness::DoNotOptimize(got.tail.view());
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
    harness::DoNotOptimize(got.tail.view());
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
        walked.number.value == by_the_hand.value && walked.tail.view() == stepped.tail.view() &&
        walked.tail.view() == written.tail.view() && walked.tail.view() == by_the_hand.tail.view();
    if (!agreed) {
      std::println("the columns do not agree at {} turns: {} {} {} {}, "
                   "tails \"{}\" \"{}\" \"{}\" \"{}\"",
                   count, walked.number.value, stepped.number.value,
                   written.value, by_the_hand.value, walked.tail.view(), stepped.tail.view(),
                   written.tail.view(), by_the_hand.tail.view());
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
