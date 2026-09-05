// The inputs the benchmarks match, and nothing else: every benchmark
// translation unit compiles one pattern, so this header must stay cheap.
#pragma once

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

}  // namespace bench
