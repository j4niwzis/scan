// The inputs the benchmarks match, and nothing else: every benchmark
// translation unit compiles one pattern, so this header must stay cheap.
#pragma once

#include <string>
#include <string_view>

namespace bench {

// Matching is anchored and consumes the whole input, so each of these is an
// exact subject for the pattern of the benchmark that uses it.
inline constexpr std::string_view word =
    "thequickbrownfoxjumpsoverthelazydogandkeepsrunningpastthefence";
inline constexpr std::string_view csv =
    "alpha,bravo,charlie,delta,echo";
inline constexpr std::string_view address =
    "first.last@subdomain.example.com";
inline constexpr std::string_view timestamp =
    "2026-09-05T04:20:59";


// A subject the optimiser cannot see through.
//
// Matching is constexpr and the texts above are constants, so a benchmark that
// matches one of them directly measures nothing: the compiler evaluates the
// match while compiling and the loop is left holding the answer. Copying into
// a string with static storage and hiding the pointer from the optimiser makes
// the work happen where it is being timed.
inline const std::string& subject_of(std::string_view text) {
  static std::string storage;
  storage.assign(text);
  return storage;
}

}  // namespace bench
