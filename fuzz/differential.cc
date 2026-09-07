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
#include <re2/re2.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import std;
import scan.tre;
import scan.compiler;

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
    atom(out, depth);
    const bool one_thing = out.size() - was == 1;
    switch (next() & 15) {
      case 0: out += '*'; break;
      case 1: out += '+'; break;
      case 2: out += '?'; break;
      case 3: out += "{2}"; break;
      case 4: out += "{1,3}"; break;
      case 5: out += "{0,2}"; break;
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

// Ours, over an automaton built from a pattern that was a string a moment ago.
answer ours(std::string_view pattern, std::string_view subject, bool anchored,
            std::size_t groups) {
  answer said;
  said.groups.resize(groups);
  std::size_t counted = 0;
  scan::detail::tre_parser parser(pattern, {}, counted, true);
  const auto tree = parser.parse_regex();
  const auto machine = scan::tre::compile_tnfa(tree);
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

answer theirs(const RE2& expression, std::string_view subject, bool anchored,
              std::size_t groups) {
  answer said;
  said.groups.resize(groups);
  std::vector<re2::StringPiece> found(groups + 1);
  const bool matched = expression.Match(
      subject, 0, subject.size(),
      anchored ? RE2::ANCHOR_BOTH : RE2::ANCHOR_START, found.data(),
      static_cast<int>(found.size()));
  said.matched = matched;
  if (!matched) return said;
  for (std::size_t group = 0; group < groups; ++group) {
    const re2::StringPiece& piece = found[group + 1];
    if (piece.data() == nullptr) continue;
    said.groups[group] = {true, piece.data() - subject.data(),
                          piece.data() + piece.size() - subject.data()};
  }
  return said;
}

void complain(std::string_view what, std::string_view pattern,
              std::string_view subject, bool anchored) {
  std::printf("%.*s\n  pattern: %.*s\n  subject: %.*s\n  anchored: %s\n",
              static_cast<int>(what.size()), what.data(),
              static_cast<int>(pattern.size()), pattern.data(),
              static_cast<int>(subject.size()), subject.data(),
              anchored ? "both ends" : "the start");
}

// True where the two agreed, or where the case says nothing.
bool one_round(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 4) return true;
  const std::size_t split = 1 + bytes[0] % (bytes.size() - 2);
  pattern_maker maker(bytes.subspan(1, split));
  const std::string pattern = maker.make();
  const std::string subject = subject_of(bytes.subspan(1 + split));
  const std::size_t groups = maker.groups();
  if (pattern.empty() || pattern.size() > 200) return true;

  RE2::Options options;
  options.set_log_errors(false);
  const RE2 expression(pattern, options);
  if (!expression.ok()) return true;
  if (static_cast<std::size_t>(expression.NumberOfCapturingGroups()) != groups) {
    return true;
  }

  for (const bool anchored : {true, false}) {
    answer mine;
    try {
      mine = ours(pattern, subject, anchored, groups);
    } catch (...) {
      // A pattern this library will not read is not a disagreement about what
      // it means. It is worth knowing about, and it is not a failure.
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
        std::printf("  group %zu: ours [%td,%td) took_part=%d, theirs [%td,%td) took_part=%d\n",
                    group, mine.groups[group].begins, mine.groups[group].ends,
                    static_cast<int>(mine.groups[group].took_part),
                    other.groups[group].begins, other.groups[group].ends,
                    static_cast<int>(other.groups[group].took_part));
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
  for (int at = 1; at + 1 < count; at += 2) {
    if (std::strcmp(arguments[at], "--seed") == 0) {
      seed = std::strtoull(arguments[at + 1], nullptr, 10);
    } else if (std::strcmp(arguments[at], "--rounds") == 0) {
      rounds = std::strtoull(arguments[at + 1], nullptr, 10);
    }
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
        std::printf("stopping after twenty\n");
        break;
      }
    }
  }
  std::printf("%zu rounds, %zu disagreements\n", rounds, disagreements);
  return disagreements == 0 ? 0 : 1;
}
#endif
