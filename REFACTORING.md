# A plan for the library

Twenty thousand lines in nine interface units, one of which is seven and a
half thousand. Everything below is written against that shape, and the measure
of every item is one of three things: **what a reader has to hold in his head**,
**what the compiler has to do per translation unit**, and **what can be got
wrong without a test failing**.

Each step stands on its own and can go in as one commit with CI behind it.

```
src/scan.cc            5      umbrella
src/scan_views.cc    208      in_pieces, static_chunk, to_array
src/scan_scanners.cc 716      the built-in scanners
src/scan_compiler.cc 997      determinization, the automaton tables
src/scan_core.cc    1099      the vocabulary: fixed_string, scanner, branches, failures, contexts
src/scan_range.cc   1190      the entry points and their results
src/scan_tre.cc     1750      the expression: parse, the NFA, the tags
src/scan_regex.cc   3110      the pattern layer: results, collectors, closures, views
src/scan_runtime.cc 3147      the walk: the ladder, the vectors, the registers
src/scan_shape.cc   7780      the format layer: places, shapes, contexts, gatherings, the walk over a format
```

## 1. Split `scan_shape.cc`

It is five things, and only the last of them is what anybody imports:

| what | roughly | why it is its own thing |
| --- | --- | --- |
| `scan.shape.fields` | `fields`, `spread_format`, places, `turns_of`, the format walk over a type | pure compile-time description; no walk, no contexts |
| `scan.shape.contexts` | `reading_of`, `reading_by`, `context_leaf`, `context_shape`, `carrier_places`, `readings_for`, `wire`, `resource_of`, `told_for_part` | the type-erasure layer; changes for reasons nothing else shares |
| `scan.shape.values` | `made_range`, `made_like`, `copied_gathering`, the glue calling `parse`/`from_groups`/`begin_groups`/`begin` | where a scanner is actually asked for a value |
| `scan.shape.walk` | `run_owning`, the gathering slots, the turn boundaries, the list and fold state | the delicate part, and the expensive one to compile |
| `scan.shape` | `aggregate_scanner`, `shape_turns`, what the entry points call | the public face |

Two hundred and fifty top-level declarations in one file is past what anybody
reads; more to the point, a translation unit that wants only the context layer
today parses the walk as well. The partitions must stay acyclic in the order
above, because the generated headers are a topological sort of them.

**Do the split by moving text, in one commit per partition, with no other
change in the same commit.** Renames and behaviour go in later commits, so that
a bisect over a CI failure lands on something readable.

## 2. Say one thing one way

The context work left four words for three ideas:

* **context** -- what the caller hands over. Keep.
* **told** (`told_type`, `told_for_part`, `told_resource`, `state.told`) --
  sometimes the carrier, sometimes the context inside it. Pick one: a *carrier*
  is what routes contexts to places, and a *reading* is the type-erased thing
  that makes a value with one.
* **given** (`nothing_given`, `one_given`, `contexts_given`, `given_type`) --
  the same carrier again, in a different word. Fold into *carrier*:
  `no_contexts`, `one_context`, `contexts_for_places`.
* `scan::context` (`scan_core.cc:839`) is a marker nothing requires any more --
  nothing in `src/` and nothing in `tests/` names it. **Delete it**; a context
  inherits nothing, which is what the tests already say.

`held` is a template parameter name in the shape layer and a public type
(`scan::held<N>`) in the scanners. Rename the parameter.

## 3. PascalCase template parameters

`template <auto Arg>`, not `template <auto arg>`. This is the one place where
the library's names and the language's names collide -- `type`, `format`,
`pattern`, `it`, `held`, `context` are all both -- and the case is what tells
them apart at a glance.

Sweep it file by file, smallest first (`scan_views.cc`, `scan_scanners.cc`,
`scan_core.cc`, `scan_compiler.cc`, `scan_range.cc`, `scan_tre.cc`,
`scan_regex.cc`, `scan_runtime.cc`, `scan_shape.cc`), one commit each, nothing
else in the commit. A parameter's scope is its own template, so the rename is
local and the compiler catches what the eye misses; what it does not catch is a
string literal or a comment, so leave the diagnostic texts alone and read them
afterwards.

Names that the public writes -- `scanner<T>`, `branches<T>`, `room_for<T>`,
`aggregate_scanner<f>`, `group_at<k>`, `held<N>`, `in_pieces<N>` -- change their
parameter names too, and they are also what the README shows, so the two move
together.

## 4. The entry points repeat themselves

`scan_range.cc` has the same API written three times over -- `borrowed_result`
(62), `pieces_result` (284), `streaming_result` (355) -- each with `read`,
`read_or_throw`, `of`, `try_of`, `with` and the context overloads, and again in
`prefix_scan` (467) and `prefix_stream_scan` (751), and again in `each_scan`
(801), `each_stream_scan` (824) and `each_pieces_scan` (895).

What differs between them is one function: how a subject of that kind is
walked. Everything else -- the four ways of naming the output type, the carrier
overloads, the policy methods -- is the same text. Make it one mixin
parameterised on that function:

```cpp
template <class Reading>
struct names_its_output { /* of, try_of, with, read, read_or_throw */ };
template <class Derived>
struct takes_the_policies { /* sentinel, scalar, vec, sized, by_length, past_space */ };
```

Three hundred lines go, and a new policy stops being a thing to remember to add
in nine places. The same is true of the closures in `scan_regex.cc`:
`match_closure`, `starts_with_closure`, `search_closure`, `search_all_closure`,
`split_closure` each carry their own `into`.

## 5. The delicate parts want tests below the surface

Two faults this year were both in the same half-page: which register a turn's
gathering lives in when a list takes another turn, and whether a place's
context reaches the places inside it. Both were found by instrumenting the
walk, and both were only visible end to end.

* **Turn boundaries.** Lift the aside-by-readings logic out of `run_owning`
  into a named function over (automaton, left state, commands, slots), and test
  it against a hand-written automaton and a fake fold -- no format, no subject.
  Every fault in it is a wrong number, and a wrong number is what a unit test
  is for.
* **Register cutting.** The same for whatever decides that a register is dead;
  it is what silently drops a turn.
* **Contexts reaching places.** One table: (output shape) × (carrier form) →
  which place is told what. It exists as `every_subject_agrees_test.cc` for the
  subject axis; the carrier axis wants the same treatment.
### Three holes the table found

Both are in `every_subject_agrees_test.cc`, both are about a place whose type
is read by a shape scanner of its own -- `struct deep { both left; both
right; };` with `scanner<both> : aggregate_scanner<"{}:{}">`.

* **Two places of one kind share a gathering off a subject read once.**
  `DISABLED_AShapeOfShapesIsReadOffAStreamToo`: reading `"1:ab 2:cd"` gives both
  halves the same value, with the digits of both concatenated and the letters of
  both counted. The walk keeps a gathering per *kind* and not per *place*
  (`gathering_slot`, `scan_shape.cc:4546`, and the guard at `:4891` that hands a
  non-repeating fold place to the walk itself). Two places of the same kind are
  two values; the slot has to be told them apart -- either by keying a fold
  place's slot on its group, or by beginning it again where its place opens, the
  way a place kept at a register already is.
* **A string gathered off a stream loses the resource its place was told
  about.** `DISABLED_AStringGatheredKeepsTheResourceToo`: a `std::pmr::string`
  field read in a row keeps the resource and the same field gathered a character
  at a time comes back on the default one. Three places that used to assign a
  gathering over an empty one now build it where it stands
  (`begin_gathering_at`, `make_slots`, `make_register_states`, `begin_again`),
  and the value is finished from `copied_gathering`, which keeps the resource --
  and the answer is still on the default one, so the drop is somewhere between
  those. Worth an afternoon with the reading instrumented: every place that
  holds a gathering, printing `get_allocator().resource()`.
* **A braced list of contexts does not reach into such a place.**
  `of<deep>(fast, slow)` works; `of<deep>({{fast, slow}, {slow, fast}})` does
  not compile (`scan_shape.cc:2703` and `:3167`: the carrier hands the leaf
  itself where a carrier was wanted). A plain aggregate place takes both forms.
  Either the braced form learns to descend through `carrier_places` into a
  scanner-shape, or it says so where it is compiled instead of failing inside
  the library.

## 6. Compile time is the scarce resource

* **The ladder** (`scan_runtime.cc:1766-2540`) lays out 256 labelled rungs with
  the preprocessor and throws away the ones past the end of the machine with
  `if constexpr`. A rung is about a millisecond, so a machine of nine states
  pays for two hundred and fifty-six. Size the ladder from
  `states_in<automaton>` -- 32, 64, 128, 256 -- and pick it where the walk is
  instantiated. Five hundred lines of `SCAN_AFTER_0xNN` go with it if the
  stepping is done by a constant rather than by concatenation.
* **`static_assert(states_in<automaton> <= SCAN_LADDER)`** is the cap on a
  machine that can be written out at all; say it in the README's compile-time
  section once the number is chosen rather than fixed.
* **The chain budget** (`chain_budget`, `walk_shape`) decides how much of the
  walk is written out; it is a number in code with no test pinning it. Pin it.
* **One or two patterns to a test file** is the standing rule and it is right;
  the table tests are the exception and they are the ones to watch, because a
  table is a multiplication.

## 7. The threshold between the two walks

Two benchmarks say the same thing: the length at which the reading starts
taking words is worked out from the pattern, and on a short subject with small
fields it asks for vectors that cost more than they save (`address_benchmark`,
`fold_benchmark`'s last column -- four times slower than `.scalar()`).

Make the threshold one named thing with one source of truth, feed it both the
pattern's runs *and* what the caller knows (`sized`, `sentinel`, a length that
is a constant), and put a test on the decision rather than on the timing: for
this pattern and this length, which walk. Then the benchmarks measure and the
test pins.

## 8. The public surface, before 0.1.0

* `scan::reader<Type, Format>` (`scan_range.cc:694`) is exported and is the
  resumable reading -- the one thing in the library that is not finished. Move
  it behind `scan::experimental::` or stop exporting it; it is not in the
  README and it should not be in the headers either.
* `scan::pattern_buffer<N>` and `scan::fixed_string<N>` are both "text known
  while compiling"; one of them is enough in the public namespace.
* `scan::detail` is where everything else lives, and the generated headers
  publish it. Worth one pass to see what is in `scan::` that nobody outside
  calls.
* The failure kinds are templates on their base (`no_match<>`, and the
  `handed_back` default) so that the same kind can be thrown or handed back.
  The aliases people write are the ones in the README; make sure those are the
  short names and that the templates are the thing needing the angle brackets.

## 9. Smaller things, worth doing while passing

* `finish_scanners` (`scan_shape.cc:5648`) has no callers. Dead, and it fills an
  output by assigning to each field, which is the very thing that loses a
  resource -- so it would be wrong if it were called.
* Diagnostic texts are written where they are used, and two of them are
  duplicated word for word (`scan_range.cc:82` and `:127`). Collect the
  messages in one place, one constant a message.
* `run_continuation` takes six arguments through five call sites (`cursor`,
  `last`, `place`, `registers`, `into`, `best`). Bundle them; the bundle is the
  walk's state and naming it is half the documentation.
* `gathering_slot<Type, Format, Group>` is indexed out of a `std::tuple` per
  state. It is the hottest data structure in the format layer and the least
  described; one comment block on the layout would pay for itself.
* The built-in scanners (`scan_scanners.cc`) are five shapes of the same thing
  and read well; the integer one is the only one with two gathering paths
  (digits, and a run). Leave it alone.
* `every_subject_agrees_test.cc` is the pattern the other tests should follow
  where they are combinatorial; a handful of the eighty are the same reading
  said twice and can go.

## The order to do it in

1. Delete `scan::context`, collect the diagnostic texts (§2, §9) -- small, and
   they make the rest quieter.
2. PascalCase, file by file (§3) -- mechanical, and best done before the text
   moves between files.
3. Split `scan_shape.cc` (§1) -- moves only.
4. The entry-point mixins (§4) -- the first change with real risk; CI green
   before and after.
5. The unit tests under the walk (§5) -- write them against today's code, watch
   them pass, then let them guard §6 and §7.
6. The ladder and the threshold (§6, §7) -- both are measurable, so both end
   with a number in the README.
7. The public surface (§8) -- last, because it is the thing a release freezes.
