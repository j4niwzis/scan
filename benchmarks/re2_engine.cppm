// The third engine, wrapped once.
//
// A benchmark imports this library, which is built against the `std` module,
// so a header included textually beside it gives the translation unit two
// views of the standard library. Every outside library here is wrapped in a
// module of its own for that reason, and RE2 is no different -- with the
// second use that nothing outside names an RE2 type, so which string view its
// `Match` hands back is not something the benchmarks have to know.
module;

#include <absl/strings/string_view.h>
#include <re2/re2.h>

#include <array>
#include <string>
#include <string_view>

export module bench.re2;

export namespace bench {

class re2_engine {
 public:
  explicit re2_engine(std::string_view pattern)
      : expression_(std::string(pattern), quietly()) {}

  [[nodiscard]] bool ok() const { return expression_.ok(); }

  // The whole of the subject, and nothing kept.
  [[nodiscard]] bool whole(std::string_view subject) const {
    return expression_.Match(subject, 0, subject.size(), RE2::ANCHOR_BOTH,
                             nullptr, 0);
  }

  // The whole of it, with five groups: what the capture benchmarks ask for.
  [[nodiscard]] bool whole_with_five(std::string_view subject,
                                     std::array<std::string_view, 5>& into)
      const {
    std::array<absl::string_view, 6> found{};
    if (!expression_.Match(subject, 0, subject.size(), RE2::ANCHOR_BOTH,
                           found.data(), 6)) {
      return false;
    }
    for (std::size_t at = 0; at < 5; ++at) {
      into[at] = std::string_view(found[at + 1].data(), found[at + 1].size());
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

}  // namespace bench
