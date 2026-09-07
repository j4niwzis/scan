// The other engine, wrapped once.
//
// RE2 is a header, and a translation unit that includes one textually while
// also importing modules built against the `std` module gets two views of the
// standard library. The benchmarks solved this by wrapping each such library
// in a module of its own; this is the same thing for the fuzzer, and it has a
// second use: nothing outside here names an RE2 type, so which string view its
// `Match` takes -- its own, or abseil's, depending on the version -- is not
// something the rest of the fuzzer has to know.
module;

#include <absl/strings/string_view.h>
#include <re2/re2.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

export module fuzz.oracle;

export namespace oracle {

// Where a group was, or that it took no part.
struct span {
  bool took_part = false;
  long begins = 0;
  long ends = 0;
};

class expression {
 public:
  explicit expression(const std::string& pattern)
      : expression_(pattern, quietly()) {}

  [[nodiscard]] bool ok() const { return expression_.ok(); }

  [[nodiscard]] int groups() const {
    return expression_.NumberOfCapturingGroups();
  }

  // Anchored at both ends, or only at the start -- which are the two readings
  // this library offers, and RE2's default rule for both is leftmost-first.
  [[nodiscard]] bool match(const std::string& subject, bool anchored,
                           std::vector<span>& groups) const {
    const int wanted = static_cast<int>(groups.size()) + 1;
    std::vector<absl::string_view> pieces(static_cast<std::size_t>(wanted));
    const bool matched = expression_.Match(
        subject, 0, subject.size(),
        anchored ? RE2::ANCHOR_BOTH : RE2::ANCHOR_START, pieces.data(),
        wanted);
    if (!matched) return false;
    for (std::size_t at = 0; at < groups.size(); ++at) {
      const absl::string_view& piece = pieces[at + 1];
      if (piece.data() == nullptr) {
        groups[at] = {};
        continue;
      }
      groups[at] = {true, static_cast<long>(piece.data() - subject.data()),
                    static_cast<long>(piece.data() + piece.size() -
                                      subject.data())};
    }
    return true;
  }

 private:
  static RE2::Options quietly() {
    RE2::Options options;
    options.set_log_errors(false);
    return options;
  }

  RE2 expression_;
};

}  // namespace oracle
