# TRE, TNFA, and TDFA

This is a compact C++23 implementation using C++20 named modules for tagged
regular expressions and the
leftmost-greedy TNFA/TDFA construction described in Ulya Trofimovich's
*Tagged Deterministic Finite Automata with Lookahead*.

The library uses a layered module graph: `scan.core`, `scan.views`,
`scan.compiler`, `scan.runtime`, `scan.range`, and `scan.scanners`. The `scan`
module is only an umbrella that re-exports this graph. All units import the
standard library with `import std;`. The AST uses a forward-declared
`tre::node`. Concrete node structures live in `tre::ast`, while the completed
`tre::node` publicly derives from their `std::variant`. This permits recursive
`std::vector<node>` fields without owning pointers.

The public pipeline is:

```cpp
tre::node expression = tre::cat({
    tre::tag(0), tre::star(tre::symbol('a')), tre::tag(1)});
tre::tnfa tnfa = tre::compile_tnfa(expression);
tre::tdfa tdfa = tre::compile_tdfa(tnfa);
tre::match match = tre::simulate(tdfa, "aaa");
```

Matches are anchored and consume the whole input. Tag histories contain input
offsets; `tre::negative_tag` represents a negative tag. Alternation order and the
consume-before-exit priority of repetition implement leftmost-greedy
disambiguation. TDFA epsilon actions are attached to the preceding consuming
transition, giving the one-symbol-lookahead form.

Single-pass input ranges are not materialized. The conversion proxy retains
the range until the aggregate output type is known, then consumes it once.
Incremental scanners expose `state_type`, `begin()`, `push()`, and `finish()`;
their states are propagated through speculative TDFA slots. Integral scanners
use a fixed-size stack buffer derived from `numeric_limits<T>` and finish with
`from_chars`, so they require neither an input allocation nor per-character
arithmetic conversion.

Typed placeholders accept scanner-specific parameters after `:`. The complete
parameter string is evaluated at compile time and is passed to `pattern`,
`begin` and `parse` when those overloads are provided:

```cpp
auto value = scan::scan<"hex={:hex} word={:upper}">(input);

template <>
struct scan::scanner<word> {
  static constexpr std::string pattern(std::string_view parameters);
  // begin(parameters) selects the incremental mode; parse(text, parameters)
  // selects the direct contiguous-input conversion mode.
};
```

`pattern(parameters)` may return an owning `std::string`. It is copied into
compile-time pattern storage before the TRE is parsed; the resulting TDFA alone
is retained in static fixed-size arrays. Use `{\\:...}` when a colon at the
start of a capture is regex text rather than a parameter introducer.

Aggregate scanners compose recursively without an intermediate input buffer:

```cpp
template <>
struct scan::scanner<coordinates>
    : scan::aggregate_scanner<"({}, {})"> {};

template <>
struct scan::scanner<rectangle>
    : scan::aggregate_scanner<"[{} -> {}]"> {};
```

`aggregate_scanner` uses an explicit object parameter to recover the target of
the derived `scanner<T>` specialization, so no CRTP type argument is needed.
Its nested TDFA state directly contains the incremental states of child
scanners.

GoogleTest is the modular fork `j4niwzis/googletest-modules`, pinned and
downloaded by CPM.cmake. A local CMake 4.3.4 distribution is expected at
`.tools/cmake-4.3.4-linux-x86_64`:

```sh
.tools/cmake-4.3.4-linux-x86_64/bin/cmake -S . -B build-modules -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++
.tools/cmake-4.3.4-linux-x86_64/bin/cmake --build build-modules
.tools/cmake-4.3.4-linux-x86_64/bin/ctest --test-dir build-modules
```

The regex comparison benchmark uses Google Benchmark, CTRE, and code generated
by re2c. Build it separately with full LTO:

```sh
.tools/cmake-4.3.4-linux-x86_64/bin/cmake -S . -B build-benchmark -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ \
  -DSCAN_BUILD_BENCHMARKS=ON
.tools/cmake-4.3.4-linux-x86_64/bin/cmake --build build-benchmark \
  --target regex_benchmark
./build-benchmark/regex_benchmark
```
