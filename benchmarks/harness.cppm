// google benchmark as a module.
//
// A translation unit that imports a module built against the `std` module and
// also includes a standard header textually gives clang's bytecode interpreter
// two views of the same entity, and nothing in that file evaluates afterwards.
// The harness is a header, so it is wrapped here once: its declarations enter
// through the global module fragment and leave as exports, and the benchmarks
// import them.
//
// The registration macros do not survive -- a macro is not an export -- so the
// benchmarks register through `RegisterBenchmark`, which is what those macros
// call anyway.
module;

#include <benchmark/benchmark.h>

export module bench.harness;

export namespace harness {

using benchmark::ClobberMemory;
using benchmark::DoNotOptimize;
using benchmark::RegisterBenchmark;
using benchmark::State;

using Benchmark = benchmark::internal::Benchmark;

}  // namespace harness
