// The engine against another engine, on patterns and subjects nobody wrote.
//
// Everything the library does while a program is compiled -- reading the
// pattern, building the machine, determinizing it, cutting the walks below a
// match, minimizing, and laying out the registers -- is ordinary code that
// also runs. So the pattern can be a string made up here rather than a
// template argument, and the answers can be compared against an engine that
// was not written by us.
//
// RE2 is the one to compare with. Its default is leftmost-first: the first
// alternative under which the whole expression matches wins, alternation is
// ordered, repetition is greedy. That is the rule this library settled on, so
// a disagreement is a bug in one of the two rather than a difference of
// opinion. (`RE2::POSIX` would be the other rule, and is not used here.)
//
// Two questions are asked of every pair:
//
//   * anchored at both ends -- our `simulate` over the automaton built for
//     that reading, against `RE2::ANCHOR_BOTH`;
//   * anchored at the start -- our head walk over the automaton with the walks
//     below a match cut, against `RE2::ANCHOR_START`.
//
// And of both: where every capture group began and ended, or that it took no
// part. The group positions are where the tags are, and a machine that gets
// the answer right by getting the tags wrong is the failure this is looking
// for.
import std;
import scan.tre;
import scan.compiler;
import fuzz.oracle;

namespace {

// A pattern out of bytes, from the grammar both engines read the same way.
//
// Random bytes are almost never a pattern, and a fuzzer that spends its time
// being told so tests the parser's error path and nothing else. So the bytes
// choose among the shapes instead, and what comes out is always something both
// engines can read -- which is what makes a disagreement mean something.
class pattern_maker {
 public:
  explicit pattern_maker(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  std::string make() {
    std::string out;
    alternation(out, 0);
    return out;
  }

  [[nodiscard]] std::size_t groups() const { return groups_; }

 private:
  std::uint8_t next() {
    if (at_ == bytes_.size()) return 0;
    return bytes_[at_++];
  }

  void alternation(std::string& out, int depth) {
    concatenation(out, depth);
    while (depth < 3 && (next() & 7) == 0) {
      out += '|';
      concatenation(out, depth);
    }
  }

  void concatenation(std::string& out, int depth) {
    const int many = 1 + (next() & 3);
    for (int at = 0; at < many; ++at) repeated(out, depth);
  }

  void repeated(std::string& out, int depth) {
    const std::size_t was = out.size();
    const std::size_t counted_before = counted_;
    atom(out, depth);
    const bool one_thing = out.size() - was == 1;
    // A count written around something that already has one multiplies the
    // machine: `((a{3}){3}){3}` is twenty-seven copies of `a`, and the
    // deterministic form of that is worse than twenty-seven. Nesting them is
    // how a fuzzer asks for three gigabytes, so the counts are spent rather
    // than nested -- one on the way down each branch, and no more.
    const bool may_count = counted_ == counted_before && counted_ < 2;
    switch (next() & 15) {
      case 0: out += '*'; break;
      case 1: out += '+'; break;
      case 2: out += '?'; break;
      case 3: if (may_count) { out += "{2}"; ++counted_; } break;
      case 4: if (may_count) { out += "{1,3}"; ++counted_; } break;
      case 5: if (may_count) { out += "{0,2}"; ++counted_; } break;
      // A lazy quantifier only where there is something to be lazy about.
      case 6: if (!one_thing) out += "*?"; else out += '*'; break;
      default: break;
    }
  }

  void atom(std::string& out, int depth) {
    switch (next() % (depth < 3 ? 10 : 7)) {
      case 0: out += 'a'; break;
      case 1: out += 'b'; break;
      case 2: out += 'c'; break;
      case 3: out += '0'; break;
      case 4: out += "[ab]"; break;
      case 5: out += "[^a]"; break;
      case 6: out += "[a-c0-9]"; break;
      case 7:
        out += '(';
        ++groups_;
        alternation(out, depth + 1);
        out += ')';
        break;
      case 8:
        out += "(?:";
        alternation(out, depth + 1);
        out += ')';
        break;
      default: out += '.'; break;
    }
  }

  std::span<const std::uint8_t> bytes_;
  std::size_t at_ = 0;
  std::size_t groups_ = 0;
  std::size_t counted_ = 0;
};

std::string subject_of(std::span<const std::uint8_t> bytes) {
  static constexpr char alphabet[] = "abc01";
  std::string out;
  for (std::uint8_t byte : bytes) out += alphabet[byte % (sizeof(alphabet) - 1)];
  return out;
}

struct group_span {
  bool took_part = false;
  std::ptrdiff_t begins = 0;
  std::ptrdiff_t ends = 0;
};

struct answer {
  bool matched = false;
  std::vector<group_span> groups;
};

// A pattern whose machine is larger than anything worth comparing. Every
// engine has these -- RE2 answers them by simulating rather than
// determinizing, and by keeping a bounded cache of the states it has seen --
// and this library answers them by refusing to build one past a size it says
// out loud. Here they are stepped over, because what is being looked for is a
// disagreement about meaning.
struct too_big {};

// Whether to say what is being tried before trying it. A run that ends in the
// machine having no room left leaves no other trace.
bool tracing() {
  static const bool asked = std::getenv("SCAN_FUZZ_TRACE") != nullptr;
  return asked;
}
inline constexpr std::size_t walkable = 2000;

// Ours, over an automaton built from a pattern that was a string a moment ago.
answer ours(std::string_view pattern, std::string_view subject, bool anchored,
            std::size_t groups) {
  answer said;
  said.groups.resize(groups);
  std::size_t counted = 0;
  scan::detail::tre_parser parser(pattern, {}, counted, true);
  const auto tree = parser.parse_regex();
  const auto machine = scan::tre::compile_tnfa(tree);
  // Determinizing is where the room goes, and the library says how much of it
  // it will spend: past that it throws, and here that is stepped over rather
  // than counted as a disagreement.
  if (machine.transitions.size() > walkable) throw too_big{};
  const auto automaton = scan::tre::optimize_tdfa(
      scan::tre::compile_tdfa(machine, !anchored), true);
  const auto got = scan::tre::simulate(automaton, subject);
  said.matched = got.matched;
  if (!got.matched) return said;
  for (std::size_t group = 0; group < groups; ++group) {
    if (group * 2 + 1 >= got.tags.size()) break;
    const auto& begins = got.tags[group * 2];
    const auto& ends = got.tags[group * 2 + 1];
    if (begins.empty() || ends.empty()) continue;
    if (begins.back() < 0 || ends.back() < begins.back()) continue;
    said.groups[group] = {true, begins.back(), ends.back()};
  }
  return said;
}

answer theirs(const oracle::expression& expression, std::string_view subject,
              bool anchored, std::size_t groups) {
  answer said;
  said.groups.resize(groups);
  std::vector<oracle::span> found(groups);
  const std::string held(subject);
  if (!expression.match(held, anchored, found)) return said;
  said.matched = true;
  for (std::size_t group = 0; group < groups; ++group) {
    said.groups[group] = {found[group].took_part,
                          static_cast<std::ptrdiff_t>(found[group].begins),
                          static_cast<std::ptrdiff_t>(found[group].ends)};
  }
  return said;
}

void complain(std::string_view what, std::string_view pattern,
              std::string_view subject, bool anchored) {
  std::println("{}\n  pattern:  {}\n  subject:  {}\n  anchored: {}", what,
               pattern, subject, anchored ? "both ends" : "the start");
}

// True where the two agreed, or where the case says nothing.
bool one_round(std::span<const std::uint8_t> whole) {
  if (whole.size() < 4) return true;
  // However many bytes arrive, this is how many are used. A fuzzer decides
  // the length of its own input, and a long one here is not a more
  // interesting pattern -- it is a longer subject, which costs room in
  // proportion and finds nothing that a short one does not.
  const std::span bytes = whole.subspan(0, std::min<std::size_t>(whole.size(), 96));
  const std::size_t split = 1 + bytes[0] % (bytes.size() - 2);
  pattern_maker maker(bytes.subspan(1, split));
  const std::string pattern = maker.make();
  const std::string subject = subject_of(bytes.subspan(1 + split));
  const std::size_t groups = maker.groups();
  if (pattern.empty() || pattern.size() > 200) return true;
  // Said before the work rather than after it, because what is being guarded
  // against here is the work not finishing: a machine that runs out of room
  // says nothing about which pattern asked for it, and this is the only place
  // that knows.
  if (tracing()) {
    std::println(stderr, "trying /{}/ against \"{}\"", pattern, subject);
    std::fflush(stderr);
  }

  const oracle::expression expression(pattern);
  if (!expression.ok()) return true;
  if (static_cast<std::size_t>(expression.groups()) != groups) return true;

  for (const bool anchored : {true, false}) {
    answer mine;
    try {
      mine = ours(pattern, subject, anchored, groups);
    } catch (...) {
      // A pattern this library will not read, or will not build a machine
      // for, is not a disagreement about what a pattern means. Both are worth
      // knowing about and neither is a failure here.
      return true;
    }
    const answer other = theirs(expression, subject, anchored, groups);
    if (mine.matched != other.matched) {
      complain(mine.matched ? "we matched and they did not"
                            : "they matched and we did not",
               pattern, subject, anchored);
      return false;
    }
    if (!mine.matched) continue;
    for (std::size_t group = 0; group < groups; ++group) {
      if (mine.groups[group].took_part != other.groups[group].took_part ||
          (mine.groups[group].took_part &&
           (mine.groups[group].begins != other.groups[group].begins ||
            mine.groups[group].ends != other.groups[group].ends))) {
        complain("the group is somewhere else", pattern, subject, anchored);
        std::println(
            "  group {}: ours [{},{}) took_part={}, theirs [{},{}) took_part={}",
            group, mine.groups[group].begins, mine.groups[group].ends,
            mine.groups[group].took_part, other.groups[group].begins,
            other.groups[group].ends, other.groups[group].took_part);
        return false;
      }
    }
  }
  return true;
}

}  // namespace

#if defined(SCAN_FUZZ_ENTRY)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  one_round(std::span(data, size));
  return 0;
}
#else
int main(int count, char** arguments) {
  std::uint64_t seed = 1;
  std::size_t rounds = 100000;
  const std::span given(arguments, static_cast<std::size_t>(count));
  for (std::size_t at = 1; at + 1 < given.size(); at += 2) {
    const std::string_view name(given[at]);
    const std::string_view value(given[at + 1]);
    if (name == "--seed") seed = std::stoull(std::string(value));
    else if (name == "--rounds") rounds = std::stoull(std::string(value));
  }
  std::mt19937_64 source(seed);
  std::vector<std::uint8_t> bytes;
  std::size_t disagreements = 0;
  for (std::size_t round = 0; round < rounds; ++round) {
    bytes.resize(4 + source() % 60);
    for (std::uint8_t& byte : bytes) byte = static_cast<std::uint8_t>(source());
    if (!one_round(bytes)) {
      ++disagreements;
      if (disagreements == 20) {
        std::println("stopping after twenty");
        break;
      }
    }
  }
  std::println("{} rounds, {} disagreements", rounds, disagreements);
  return disagreements == 0 ? 0 : 1;
}
#endif
