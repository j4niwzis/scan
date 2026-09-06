// Two whole scans, answered while this file is compiled.
//
// They were written beside the tests that share their types, and when those
// tests were broken apart they were carried into every piece -- thirteen files
// each building the same two automata and running them in the evaluator. They
// belong in one.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct user {
  std::string name;
  std::uint64_t id;
};

struct based_values {
  std::uint64_t hexadecimal;
  std::uint32_t binary;
  std::uint16_t octal;
};

consteval bool scans_at_compile_time() {
  constexpr std::string_view input = "name=alice id=42";
  user user = scan::scan<"name={} id={}">(input);
  return user.name == "alice" && user.id == 42;
}

static_assert(scans_at_compile_time());

consteval bool scans_parameters_at_compile_time() {
  constexpr std::string_view input = "hex=ff bin=101101 oct=17";
  based_values values =
      scan::scan<"hex={:hex} bin={:binary} oct={:octal}">(input);
  return values.hexadecimal == 255 && values.binary == 45 &&
         values.octal == 15;
}

static_assert(scans_parameters_at_compile_time());

TEST(CompileTimeScans, TheyBothHold) {
  // Answered before this runs; that they are answered is the test.
  SUCCEED();
}

}  // namespace
