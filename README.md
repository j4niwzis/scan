# scan

Reading text into values, with the pattern known while the program is compiled.

```cpp
import scan;

// A pattern, and whether the whole of the subject is it.
if (scan::match<"[0-9]{4}-[0-9]{2}-[0-9]{2}">(text)) { … }

// A format, and the values out of it.
struct point { int x; int y; };
const point where = scan::scan<"{},{}">("12,34");

// One record after another, off a stream, holding nothing.
for (const point& one : scan::each<"{},{}\n">(std::cin).of<point>()) { … }
```

The pattern is a template argument, so it becomes a tagged deterministic
automaton while the program is compiled and the walk over it is written out
state by state: no pattern object, no interpreter, no dispatch, nothing
allocated to match. A pattern known only at run time cannot be read here.

Two layers on one machine: the **pattern layer** asks about text -- match,
head, where the matches are, what lies between -- and the **format layer**
reads it into a type, the type's own fields deciding what each `{}` means.

Four kinds of subject answer the same way: characters in a row, input in
pieces, a forward range, and a range that can be read only once.

Compiling a pattern is a constant evaluation that builds a machine, and a
program with hundreds of them will feel it. clang or GCC 15, as the modules it
is written as or as the headers generated from them in `include/`; Boost.PFR
unless C++26 binding packs are on.

**Contents.** [A tour](#a-tour) · [What the answers mean](#what-the-answers-mean)
· [The subject](#the-subject) · [The pattern layer](#the-pattern-layer) ·
[The format layer](#the-format-layer) · [Contexts](#contexts) ·
[Extension points](#extension-points) · [The pattern syntax](#the-pattern-syntax)
· [Speed](#speed) · [Building](#building)

## A tour

Every entry point is a callable object and a range adaptor closure, so
`subject | scan::search<p>` is `scan::search<p>(subject)`.

```cpp
// Does the whole subject match?
scan::match<"[a-z]+@[a-z.]+">(address);
address | scan::match<"[a-z]+@[a-z.]+">;

// With the groups.
const auto found = scan::match<"([0-9]+)-([a-z]+)">("42-abc");
found.get<1>().to_view();            // "42"
found.get<2>().to_view();            // "abc"

// The head of the subject that the pattern takes.
scan::starts_with<"[a-z]+">("abc123").whole().to_view();   // "abc"

// The leftmost match anywhere in it.
scan::search<"[0-9]+">("id=4210x").to_view();              // "4210"

// Every match, and the pieces between them. Both are lazy views.
for (const auto& one : text | scan::search_all<"[a-z]+">) { … }
const auto fields = "a,bb,,ccc" | scan::split<","> | std::ranges::to<std::vector>();

// Values, not text.
struct row { int id; std::string_view name; };
const row one = scan::scan<"{},{[a-z]+}">(line);

// A list, a variant, a nested shape.
struct all { std::vector<int> values; std::variant<int, std::string_view> tail; };
const all got = scan::scan<"{{}{*,?}} {{[0-9]+}|{[a-z]+}}">("1,2,3 abc");

// The head of the input, and what is left of it.
const auto [value, rest] = scan::scan_prefix<"{},{}">(line).take<point>();

// A record at a time, out of anything.
for (const row& one : scan::each<"{},{[a-z]+}\n">(text).of<row>()) { … }
```

## What the answers mean

### Leftmost-first

Perl's rule, which is RE2's and CTRE's: **the first alternative under which the
whole expression matches wins, however short it is.** Alternation is ordered,
repetition greedy unless written lazy.

```cpp
scan::each<"{{for}|{each}|{foreach}}">("foreach")   // for, then each
scan::each<"{{foreach}|{for}|{each}}">("foreach")   // foreach
```

In the first, the match of `for` is the first walk in that state and everything
below it has lost. Not longest-match: a lexer would take the whole word.

### Anchored, or a head

* **anchored** -- the subject ends the reading. `match`, `scan`.
* **a head** -- the match ends it. `starts_with`, `search`, `scan_prefix`,
  `each`, `split`, `search_all`.

Different machines: for a head a walk below a match has lost and is cut when
the automaton is built; anchored, that walk may be the only one that reaches
the end.

```cpp
scan::match<"a|ab">("ab")          // `ab`: `a` cannot reach the end
scan::starts_with<"a|ab">("ab")    // `a`: it matched first
```

### Nothing is walked back, except by a note

A walk can pass a match hunting a longer one the order prefers and die --
`foreach|for|each` reading "fore" -- so it keeps a note: the place, the
registers there, and what a fold gathered. Whether a machine can pass a match
at all is a compile-time question; where every step out of a match lands in
another, the note is a pointer. No backtracking beyond it, and nothing is ever
tried a second way.

### Groups

Numbered by opening parenthesis from one; nought is the whole match. A group
that took no part says so rather than coming back empty, and a group in a
repetition holds the turn that won.

## The subject

| subject | how it is read | what is held |
| --- | --- | --- |
| characters in a row (`string_view`, `string`, `vector<char>`) | in words and vectors past a threshold, a character at a time below it | nothing; groups are pointers into the subject |
| pieces (a range of contiguous ranges, or `subject \| scan::in_pieces<N>`) | each piece in words and vectors, the reading held between them | nothing; the place a record ended is an address inside a piece |
| a forward range | a character at a time | nothing; the note is an iterator, and going back is assigning it |
| a range read once (`views::istream`, `istreambuf_iterator`) | a character at a time, once | the characters read past a match, and no more |

A string literal's NUL is not part of it: `scan<"{}">("450")` reads three
characters, read *to* the NUL rather than by counting. For a buffer you filled,
hand over a `string_view` of the filled part.

### A subject that can only be read once

A deterministic machine never looks ahead, so it reads a range with no way back
-- `std::views::istream`, a socket, a pipe -- gathering fields as they arrive:

```cpp
std::istringstream source("set speed 42\nset gain 7\n");
source >> std::noskipws;
struct command { scan::held<16> name; int value; };
for (const command& one :
     scan::each<"set {[a-z]+} {[0-9]+}\n">(std::views::istream<char>(source))
         .of<command>()) { … }
```

Nothing is buffered. Two things are held, and both are numbers the pattern
names while it is compiled:

* **what was read past a match**, which belongs to the next record;
* **what a failed attempt swallowed**, which a search starting one character
  later needs again.

For most patterns both are zero. Where no number can be named -- a cycle with
no match along it, `a+b` -- the reading is refused where it is compiled rather
than silently buffered. The order of the alternatives decides:

```cpp
scan::search_all<"a|abcd">   // holds nothing: the match of `a` ends the walk
scan::search_all<"abcd|a">   // holds two characters: `abcd` outlives the match
```

## The pattern layer

| | what it answers |
| --- | --- |
| `scan::match<p>` | whether the whole subject is `p`, with the groups |
| `scan::starts_with<p>` | the head of the subject that `p` takes |
| `scan::search<p>` | the leftmost match anywhere |
| `scan::search_all<p>` | every match in turn, as a lazy view |
| `scan::split<p>` | the pieces between the matches, as a lazy view |
| `scan::tokenize<p>`, `scan::iterator<p>`, `scan::range<p>` | other spellings of `search_all` |

`search` and `split` want characters that are all there; the rest read a
subject that arrives once as well.

### Saying what the reading should be

Policies are methods and compose in any order:

```cpp
scan::match<p>(text)                          // the length says which walk
scan::match<p>.sentinel()(text)               // there is a terminator: '\0'
scan::match<p>.sentinel<'\n'>()(text)         // or this one
scan::match<p>.scalar()(text)                 // a character at a time, always
scan::match<p>.vec()(text)                    // in words and vectors, always
scan::match<p>.into<std::pmr::string>()(text) // where the answers are kept
```

`sized()` and `by_length()` are the opposites of `sentinel()` and of the two
walk choices.

**`sentinel()`** promises a terminator past the end -- NUL unless you name
another. The walk then tests the character instead of also testing the end. A
`static_assert` checks the pattern cannot match it; that it is there is your
promise, as with any `c_str()`.

**The walk** is chosen by length unless you say: a few dozen characters go
faster one at a time, a long subject in words. Naming it means the length is
never read and the other walk is never written.

### What a match hands back

```cpp
const auto found = scan::match<"([0-9]+)-([a-z]+)">(text);
if (found) { … }                     // explicit operator bool
found.whole();                       // the whole match, as a submatch
found.get<0>(), found.get<1>();      // nought is the whole; then the groups
found.to_view(), found.size();       // where the characters can be pointed at
for (char letter : found) { … }      // begin/end/size on the whole match
std::string_view text = found;       // and the conversion
```

### Collectors: what a group comes back as

| subject | what a group is |
| --- | --- |
| characters in a row | `std::string_view` into the subject; nothing is copied |
| a forward range | `std::ranges::subrange<It, It>`; nothing is copied either |
| pieces, or a range read once | owned, because what it was read from is gone; `std::string` unless you say otherwise |

The subject decides which, so one reading gives views off a string and owns
what it kept off a socket. `into<T>()` says what "owned" means and nothing
else: `into<std::pmr::string>()` on a `string_view` subject changes nothing.

`into(collectors…)` says it per group, in the order the groups were written:

```cpp
scan::match<"([0-9]+)-([a-z]+)-([a-z]+)">.into(
    scan::as<int>(),                       // parsed into a value
    scan::as<std::pmr::string>(&pool),     // built with the arguments given
    scan::skip())                          // nothing kept, and no room taken
```

| | |
| --- | --- |
| `scan::text()` | the characters, held as the subject affords -- the default |
| `scan::as<T>(args…)` | a `T`: built as `T(first, last, args…)` where that works, otherwise parsed by `scan::scanner<T>` |
| `scan::skip()` | nothing; `scan::skipped` stands in the answer and the group takes no room |
| `scan::collecting<T>(push, args…)` | a `T`, made from `args…`, with every character handed to `push` |

One collector to a group, except where one reads a type with groups of its
own: those are that type's, and the next collector starts past them.

```cpp
// `version` wrote three groups, so this is one collector over four groups.
scan::match<"v=(([0-9]+)\\.([0-9]+)\\.([0-9]+))-([a-z]+)!">.into(
    scan::as<version>(), scan::text())(text);

// For an answer that is neither text nor a parsed value.
scan::match<"([a-z]+)">.into(scan::collecting<std::size_t>(
    [](std::size_t& sum, char letter) { sum += static_cast<unsigned char>(letter); }));
```

## The format layer

A format is a pattern with places in it, each place a value of the output type:

```cpp
struct row { int id; std::string_view name; double weight; };
const row one = scan::scan<"{},{[a-z]+},{}">(line);
```

| written | means |
| --- | --- |
| `{}` | this field, read by whatever its type says |
| `{[a-z]+}` | this field, read by this pattern |
| `{:x}` | this field, with parameters for its scanner |
| `{[0-9]+:x}` | both |
| `{*…}` | matched and kept by nobody |
| `{{a}\|{b}}` | a sum: whichever branch took the input |
| `{…}`, `{…}*`, `{…}+`, `{…}?`, `{…}{2,5}` | a list field, bounded by what is written after it |
| `{{…},{…}}` | a place that is a shape: the places inside are its fields |
| `{{`, `}}` | a brace that is text |

Everything else in the format is a pattern and matches itself.

| | |
| --- | --- |
| `scan::scan<f>(subject)` | the whole subject, as the type asked for |
| `scan::scan_prefix<f>(subject)` | the head of it, and what is left |
| `scan::each<f>(subject)` | one record after another, lazily |

All three take `match`'s policy methods, and the output type may be named at
either end:

```cpp
scan::scan<f>(text).of<row>()                     // said after
scan::scan<f>.of<row>()(text)                     // said before: a whole reading
scan::scan<f>(text).sentinel().of<row>()

constexpr auto read_row = scan::scan<"{},{},{}">.sentinel().of<row>();
for (const std::string& line : lines) rows.push_back(read_row(line));
```

A conversion has nowhere to put a failure but an exception, so `of<T>()` throws
and `try_of<T>()` hands back `std::expected<T, …>`. `scan_prefix` says
`take<T>()` and `try_take<T>()`, which also give what is left.

**`past_space`** is `{*\s*}` before every place, said once -- what `%d` does in
a `scanf` format and `{}` does not.

```cpp
scan::scan<f>.past_space()(text)
constexpr auto stamp = scan::fixed_string("{}-{}-{}T{}:{}:{}").past_space();
```

### The types a place can be

* anything with a `scan::scanner<T>`: the integers, the floating-point types,
  `bool`, `char`, `std::string`, `std::pmr::string`, `std::string_view`, and
  `scan::held<N>` -- room said in advance, for a reading with no allocator,
  keeping what fits and saying `overflowed` for what did not;
* an aggregate, whose fields are the places inside a nested `{…}`;
* a sum type -- `std::variant`, or anything with a `scan::branches<T>`;
* a range, which takes as many turns as the place allows.

A type that is both a range and has a scanner -- `std::string` -- is read as
one value unless its scanner says `as_a_list`.

### Reading into a list

```cpp
struct row { std::vector<int> values; };
const row one = scan::scan<"{{}{*,?}}">("1,2,3,4");

scan::scan<"{{}{*,?}}{2,3}">("1,2,3")   // at least two turns, at most three
scan::scan<"{{}{* ?}}+">(pairs)         // one or more
scan::scan<"{{}{*,?}}?">(text)          // none or one -- an optional
```

The place is a shape of two: the element, and a separator kept by nobody. The
bounds are known where the reading is compiled. Filling is `push_back`, and
with an upper bound the room is taken once. `scan::room_for` says a container
holds only so many, and then a place that could ask for more is refused at
compile time.

A list is read by the machine that gathers as it goes even off a subject in a
row, because a repeated group keeps only the turn that won.

### Reading into a sum

```cpp
struct row { std::variant<int, std::string_view> value; };
const row one = scan::scan<"{{[0-9]+}|{[a-z]+}}">(text);   // the format says the branches
const row two = scan::scan<"{}">(text);                    // each type's own pattern
```

Branches are tried in the order written, and the first under which the whole
reading succeeds wins. Which branch ran is read from the mark it left, not by
trying the alternatives again.

### Failures

| thrown | what it means |
| --- | --- |
| `scan::no_match` | the subject is not what the pattern says |
| `scan::no_group` | a value was asked for out of a group that took no part |
| `scan::bad_field` | a place matched and what stood there is not that type |
| `scan::out_of_range` | it is that type and it does not fit |
| `scan::wrong_subject` | the reading asked for cannot be had off this kind of subject |

All are `scan::scan_error`, an `std::exception` rather than an
`std::runtime_error` -- that one keeps a `std::string`, this holds a pointer to
a literal and allocates nothing. `scan::field_error` is never thrown; it is the
name for catching either field kind.

**Nothing here catches anything, and the reading never throws.** A failure
becomes a throw only where the value is asked for rather than tried for --
`of<T>()`, the conversion, `take<T>()` -- at the asking, not inside the walk.
The whole reading works with exceptions turned off.

A scanner of your own that throws throws past all of it: a reading asked to
*try* does not turn that into a failure, because that would mean catching it.

Handed back rather than thrown, the kind survives: the error type of `try_of`
and `try_take` is a `std::variant` of exactly the kinds that reading can
produce. `scan::what(…)` gives the message whichever it holds.

```cpp
const auto got = scan::scan<"{},{}">(line).try_of<row>();
if (!got) {
  if (std::holds_alternative<scan::no_match>(got.error())) continue;  // next line
  std::println("{}", scan::what(got.error()));
}
```

## Contexts

A context is a thing of the caller's that the library has never heard of -- an
allocator, a pool, a piece of the program's world -- handed to the one call
where the value of a place is made. It inherits nothing, it is not wrapped, and
its type is never forgotten.

```cpp
scan::scan<"{} {}">(text).of<pair>(fast)                         // one is everybody's
scan::scan<"{} {}">(text).of<pair>(fast, slow)                   // one per place, in order
scan::scan<"{} {}">(text).of<pair>(fast, scan::default_context)  // this place wants none
scan::scan<"{} {} {}">(text).of<nest>({{fast, scan::default_context}, slow})
scan::scan<"{} {} {}">(text).of<nest>(scan::parts{fast, scan::default_context}, slow)
scan::scan<"{} {} {}">(text).with(scan::parts{fast, slow}, slow).of<nest>()
const pair got = scan::scan<"{} {}">(text).with(fast);           // before the type is named
```

* **One** is everybody's; a scanner that takes none is read as it always was.
* **More than one** is one per place, in the order the places are read. Giving
  one to a place whose scanner takes none is said while it is compiled.
* **In braces** and **`scan::parts{...}`** both say the parts of a place, as
  deep as the shape goes -- the branches of a sum, the places of a type that
  says its own format. `parts` is deduced where braces are erased; see
  [what braces cost](#what-braces-cost).
* A fold or a list is told **without** braces: one value of many turns, not a
  shape of parts.
* `with(…)` says it before the output type is named.

A context reaches exactly the call that makes a value -- `parse`,
`from_groups`, `begin`, `begin_groups` -- as an overload taking one more
argument, in a constant expression and on every kind of subject.

```cpp
template <> struct scan::scanner<tagged> {
  static constexpr std::string_view pattern() { return "[a-z]+"; }
  static tagged parse(std::string_view text);
  static tagged parse(std::string_view text, const room& where);   // told one
};
```

**A context that keeps memory is used for what the reading builds.** A
`std::pmr::memory_resource*`, an allocator, or anything answering `resource()`,
`get_allocator()` or `told_resource()` is asked for it:

```cpp
std::pmr::monotonic_buffer_resource bytes;
struct two { std::pmr::string name; std::pmr::string tail; };
const two got = scan::scan<"{[a-z]+} {[a-z]+}">(text).of<two>(
    std::pmr::polymorphic_allocator<>(&bytes));
```

### What braces cost

```cpp
of<nest>({{fast, slow}, fast})            // erased: one type for every field
of<nest>(scan::parts{fast, slow}, fast)   // deduced: nothing erased
```

A braced list deduces nothing, so the parameter's type is fixed before the call
-- one type per field, whatever was written there. That is the erasure, and it
costs two things:

* every leaf of the list gets a reading made as a **default argument of the
  call**, which is what makes it live to the end of the full expression;
* calls into your scanner from behind it are **virtual** -- `parse`,
  `from_groups`, `begin_groups`. Clang devirtualises them; whether it inlines
  them is its own decision.

So `each` takes no braces -- it reads a record after that expression has ended
-- and takes `scan::parts`, which costs neither:

```cpp
for (const row& one : scan::each<f>(source).of<row>(scan::parts{fast, fast}, fast))
```

A named context is held as its address, so a scanner writes into the caller's
own. One made at the call is moved in and held. Nothing has to outlive
anything.

## Extension points

Everything a type says about how it is read is a specialisation or a member.
Nothing **you write** is virtual or inherited, apart from `aggregate_scanner`.
The one place a call arrives through an interface is a context said in braces,
and that interface is the library's own -- see
[what braces cost](#what-braces-cost).

| point | what it says |
| --- | --- |
| `scan::scanner<T>` | how a value of `T` is read: a pattern, and one of the shapes below |
| `scan::aggregate_scanner<f>` | a base for a scanner whose value is a whole format |
| `scan::branches<T>` | that `T` is a sum, and what its alternatives are |
| `scan::room_for<T>` | how many elements a container of yours holds |
| `scan::fields<T>` | how a type that is not an aggregate is taken apart |
| a collector | what a group of a *pattern* comes back as |
| a failure of your own | any `scan::scan_error<…>`, handed back by a `try_` shape |

### A leaf: a value read out of one place

```cpp
template <>
struct scan::scanner<weight> {
  // What the place matches when the format does not say.
  static constexpr std::string_view pattern();
  static constexpr auto pattern(std::string_view parameters);

  // From the text of the place, with the parameters if it wants them.
  static constexpr weight parse(std::string_view text);
  static constexpr weight parse(std::string_view text, std::string_view parameters);

  // For a subject that arrives a character at a time.
  static constexpr state begin();
  static constexpr state begin(std::string_view parameters);
  static constexpr void push(state&, char);
  static constexpr weight finish(state);
};
```

`parse` is enough for a subject in memory; `begin`/`push`/`finish` is what a
one-pass reading needs, and a type with them can be a field of a record read
off a stream. The built-in scanners are written that way -- the integers take
`{:x}`, `{:o}`, `{:b}`, `{:i}` and a width through `parameters`.

Every function that makes a value has a second shape that hands the failure
back instead, used wherever the throwing one would be:

| asked for | handed back |
| --- | --- |
| `parse(text[, parameters])` | `try_parse(text[, parameters])` |
| `finish(state)` | `try_finish(state)` |
| `from_groups(groups)` | `try_from_groups(groups)` |
| `finish_groups(state)` | `try_finish_groups(state)` |

```cpp
static std::expected<weight, std::variant<too_heavy, not_a_weight>>
try_parse(std::string_view text);
```

A push needs none: the walk is not over when a character arrives, so a push
that finds something wrong records it and hands it back at the end -- what
`scan::held<N>` does with a field too long. The kinds must be `scan_error`s,
since asking rather than trying throws what was handed back.

### A shape: a type that says a whole format

```cpp
template <>
struct scan::scanner<point> : scan::aggregate_scanner<"({}, {})"> {};

struct line { point from; point to; };
const line one = scan::scan<"{} -> {}">("(1, 2) -> (3, 4)");
```

The places inside `point`'s format mean `point`'s fields wherever it is used,
and the outer pattern and the inner one are one automaton.

With a `parse` taking the places as arguments the type need not be an aggregate
-- it may have invariants, private members, or an order of its own:

```cpp
template <>
struct scan::scanner<angle> : scan::aggregate_scanner<"{}deg{}min"> {
  static constexpr angle parse(int degrees, int minutes) {
    return angle(degrees * 60 + minutes);
  }
};
```

### A type that reads its own groups

A leaf may say a pattern with groups and be built from those rather than from
the text it stood on -- groups of the same match, found on the way past.

```cpp
template <>
struct scan::scanner<version> {
  static constexpr std::string_view pattern() {
    return "([0-9]+)\\.([0-9]+)\\.([0-9]+)";
  }
  static constexpr bool reads_its_groups() { return true; }
  // Handed exactly its own groups, in the order it wrote them.
  static constexpr version from_groups(std::span<const std::string_view> groups);
};
```

Works in a format and a pattern alike. Such a place takes parameters but not a
pattern of its own: the groups are counted off the pattern the type declares.

`from_groups` is handed views, so it wants a subject there is something left to
point at -- reading a once-only subject into a type that says only
`from_groups` does not compile. Such a type reads a stream by saying the fold
instead.

### A fold: a type told its groups as they happen

`from_groups` hands over what is there when the match is over, which for a
repeated group is the last turn and nothing before it. A type whose groups
repeat is told the turns as they go and folds them itself:

```cpp
template <>
struct scan::scanner<numbers> {
  struct state { std::vector<int> values; int running = 0; };

  static constexpr std::string_view pattern() { return "([0-9]+)(?:,([0-9]+))*"; }

  static constexpr state begin_groups();
  // One overload a group; `std::size_t` does as well as `group_at<k>`.
  static constexpr void opened_group(state&, scan::group_at<0>);
  static constexpr void push_group(state&, scan::group_at<0>, char);
  static constexpr void closed_group(state&, scan::group_at<0>);
  // … and the same three for group 1, which repeats
  static constexpr numbers finish_groups(state);
};
```

Openings arrive in the order the groups are written, closings innermost first,
once a turn. Where the subject can be pointed at a closing may be handed the
whole of what the group stood on --
`closed_group(state&, scan::group_at<k>, std::string_view)` -- and then no
characters are handed over, so a run stepped over in vectors costs one call
rather than one a character. Saying only that form says the fold wants a
subject it can point at. Every hook is optional but `begin_groups` and
`finish_groups`.

**The walk stands in several readings of the subject at once** and carries a
fold with each: the state is copied where a reading divides and dropped where
one dies, so it must be copyable and must be the only thing the fold touches.
This is the incremental fold contract: **hooks may run for a reading that is
later abandoned** -- a walk that can pass a match has to read past it to find
out whether a longer one is there. **The completed fold state is exact**; the
number of hook invocations is not an observable matching guarantee.

```cpp
static state keep_groups(const state& made);                    // kept at a match
static void groups_go_back_to(state& live, const state& kept);  // and back to it
```

Said neither, the state is copied and assigned as it always was.

### A list, a sum, a container of your own

```cpp
// Read as many as there are, though the type has a scanner of its own.
template <> struct scan::scanner<packet_bytes> {
  static constexpr bool as_a_list = true;
  …
};

// A sum: three questions, and `std::variant` answers them like anything else.
template <>
struct scan::branches<either_word_or_number> {
  static constexpr std::size_t count = 2;               // how many alternatives
  template <std::size_t Which>                          // which type the k-th is
  using at = std::conditional_t<Which == 0, word, number>;
  template <std::size_t Which, class Value>             // and how to make it
  static constexpr either_word_or_number make(Value&& one);
};

// A container with room said in advance.
template <> struct scan::room_for<three_at_most> {
  static constexpr std::size_t most = 3;
};
```

A container is read into through `value_type` and `push_back`. `scan::fields<T>`
answers the three questions a shape asks of its fields -- how many, the type at
an index, the reference in a value -- for a type that is not an aggregate.

### A collector of your own

The same four hooks a scanner has, and one thing more: what it makes.

```cpp
struct hex_bytes {
  // What it makes. Either one type…
  using value_type = std::vector<std::byte>;
  // …or a type per holder, where what it makes depends on what the subject
  // affords: a view where the characters can be pointed at, something owning
  // where they cannot.
  template <class Holder> using value_for = Holder;

  // From the whole group at once, where the subject can be pointed at.
  value_type parse(std::string_view text, std::string_view parameters) const;
  template <class Holder>
  Holder parse(std::string_view text, std::string_view parameters) const;

  // A character at a time where it cannot, a run at a time where it can.
  value_type begin(std::string_view parameters) const;
  void push(value_type& into, char letter) const;
  void push(value_type& into, std::string_view run) const;
  value_type finish(value_type state) const;

  // Optional: nothing at all from this group. `scan::skipped` stands in the
  // answer, and this is what `scan::skip()` is.
  static constexpr bool takes_nothing = true;
};
```

Write both halves and it works everywhere; write only `parse` and it works
wherever the characters can be pointed at.

**A `scan::scanner<T>` is therefore a collector of `T`** and can be handed
straight to `into`; `scan::as<T>(args…)` is that with arguments, for a value
built from something the scanner has never heard of.

```cpp
text | scan::match<"([0-9]+)g-([a-z]+)">.into(scan::scanner<weight>{}, scan::text())
```

## The pattern syntax

Literals; `.`; classes `[a-z]`, `[^a-z]` with ranges and escapes; `\d \D \s \S
\w \W` and the usual `\n \t \\`; groups `(…)` and `(?:…)`; alternation `|`; the
repetitions `* + ? {n} {n,} {n,m}`, each lazy with a `?` after it.

No lookaround, no backreferences, no Unicode properties. Patterns are bytes: a
UTF-8 literal matches itself and `.` is one byte, not one code point.

## Where this differs from other engines

**From `sscanf`.** Whitespace is not skipped unless the format says so
(`past_space`); the whole subject must match unless you read a head with
`scan_prefix`; a scan is all or nothing where `sscanf` hands back how many
fields it filled; an integer too big for its type is an error rather than
undefined behaviour; nothing is locale-dependent.

**From Perl and RE2, in one corner.** A quantifier around something that can
match nothing. `([ab]*?)*` against "ba", anchored:

| | group 1 |
| --- | --- |
| Perl | `[2,2)` |
| Python | `[2,2)` |
| `scan::scan` | `[1,2)` |
| RE2 | `[0,2)` |

The order a backtracking engine tries things in gives this one's answer: the
loop takes `b`, then `a`, and a third turn would match nothing. Perl divides it
the same way and differs only by taking that last empty turn.

**Anchored and head readings differ where the order decides**, as `a|ab` above
-- the same in Perl, where `^(?:a|ab)$` matches "ab" and `(?:a|ab)` matches
"a".

## What is known while it is compiled

* **the shortest match** -- a shorter subject is answered without reading a
  character;
* **the walk past a match** (`fallback_window`) -- how far the machine can read
  past a match before it dies, which is what a once-only reading has to hold;
* **the walk from the start** -- how much a failed attempt can swallow, which
  is what a search over such a subject has to give back;
* **whether a terminator is safe** -- that the pattern cannot match it;
* **which walk reads it** -- labels and a jump through a table of their
  addresses at run time, the same states numbered under constant evaluation,
  because a label is not a thing a constant evaluation has. One rung, read two
  ways.

Determinization stops at twenty thousand states and says so. It costs
exponentially more states than an expression has symbols for perfectly ordinary
ones -- anything that reads freely and then counts, `.*a.{20}` and its like --
and there it is not a slow program but a compilation nobody waits for.

## Speed

One machine -- Ryzen 9 9950X, clang 22.1.8 with libc++, `-O3 -march=native`,
LTO -- against CTRE, RE2 and re2c. Shapes, not decimals. Each engine gets a
backend run of its own, because an `-mllvm` option handed to a link under LTO
reaches every engine at once; this one is built with
`-jump-threading-across-loop-headers`, worth two to four per cent here and
nineteen to re2c.

**Five fields of letters out of one record** (`benchmarks/captures_benchmark.cc`),
thirty characters, thirty-two records a pass, median of seven:

| | a pass |
| --- | --- |
| the floor, nothing scanned | 268 ns |
| `scan::scan<f>.sentinel()` | 477 ns |
| re2c | 554 ns |
| `scan::scan<f>` | 659 ns |
| CTRE | 717 ns |
| RE2 | 15986 ns |

The same five out of a thousand characters, a field being two hundred letters:
57.8 ns against re2c's 637, CTRE's 692, RE2's 11751. The crossover is the
length of a *field* and not of the subject -- what wins is a run worth stepping
over in vectors.

**Recognition** (`benchmarks/address_benchmark.cc`), thirty-two characters,
thirty-two subjects a pass: 436 ns for `scan::match<p>.sentinel().scalar()` and
545 without the terminator, against re2c's 1186, RE2's 2616, CTRE's 14339.

**Against `sscanf`**, same characters in, same values out, thirty-two records:

| | `sscanf` | here |
| --- | --- | --- |
| two numbers | 2831 ns | 470 ns |
| a timestamp of six | 6827 ns | 1551 ns |
| five words into views | 10155 ns | 934 ns |
| five words into room said in advance | 10155 ns | 4265 ns |

**A fold** (`benchmarks/fold_benchmark.cc`) is the one the others cannot run: a
type told which of its groups each character belongs to, doing its arithmetic
there -- no turn kept, no substring made, the number finished when the match
is. The subject is `value=(`, heaps of marks, `)` and a tail. A heap is a
number written in weighed marks -- the underscores give the decimal place, `X`
counts one and `Y` two, so twelve is `__X_XX` -- and the type that adds them up
never sees a character of it:

```cpp
template <>
struct scan::scanner<tally> {
  static constexpr std::string_view pattern() { return R"(\(((_+)(X|Y)*)*\))"; }
  struct state_type { unsigned long total = 0; unsigned place = 0, marks = 0; };
  static constexpr state_type begin_groups() { return {}; }
  static constexpr void opened_group(state_type& one, scan::group_at<0>) {
    one.place = 0; one.marks = 0;                        // a heap begins
  }
  static constexpr void closed_group(state_type& one, scan::group_at<0>) {
    unsigned long weight = 1;                            // it ends: weigh it
    for (unsigned step = 1; step < one.place; ++step) weight *= 10;
    one.total += weight * one.marks;
  }
  static constexpr void push_group(state_type& one, scan::group_at<1>, char) {
    ++one.place;                                         // an underscore
  }
  static constexpr void push_group(state_type& one, scan::group_at<2>, char letter) {
    one.marks += letter == 'Y' ? 2u : 1u;                // a mark
  }
  static constexpr tally finish_groups(state_type one) { return {one.total}; }
};
```

Three columns beside it: the reading written out as labels and direct jumps,
in the shape a scanner generator emits and calling these same hooks; the same
reading written by hand as loops and a pointer; and the library left to choose
its own walk.

| heaps | `scan::scan<f>.scalar()` | written out | by hand | `scan::scan<f>` |
| --- | --- | --- | --- | --- |
| 10 | 141 ns | 41.4 ns | 42.5 ns | 170 ns |
| 100 | 439 ns | 341 ns | 392 ns | 1176 ns |
| 1000 | 3314 ns | 3170 ns | 3797 ns | 11887 ns |

Straight lines with different intercepts. Fitted: a heap costs 3.19 ns here,
3.14 ns written out and 3.78 ns by hand; entering costs about 120 ns against 27
and 14. Per heap the walk is within two per cent of the written-out scanner and
sixteen per cent cheaper than the reading written by hand. Entering is what it
is not cheap at: three times behind on forty-six characters, level on three
thousand, ahead of the hand at both ends. Those hundred and twenty nanoseconds are the gathering the fold is
kept in, the marks the machine writes, and the commands run before the first
character -- paid once a reading, which is once a line for a reading handed one
line at a time and unnoticed by one handed a file.

The last column is the library left to itself, at 11.9 ns a heap against 3.19.
The length at which the reading starts taking words instead of characters is
worked out from the pattern -- sixteen characters for every run in it worth
stepping over -- and not from the subject, and a heap of one to four characters
is far shorter than one vector step. Both `.scalar()` rows, here and in the
address benchmark, say the same thing: the threshold is measured from the
pattern, and where the two walks disagree `.scalar()` is what to say.

## What this is built on

A tagged deterministic finite automaton:

* Ville Laurikari, *NFAs with Tagged Transitions, their Conversion to
  Deterministic Automata and Application to Regular Expressions* (2000);
* Ulya Trofimovich, *[Tagged Deterministic Finite Automata with
  Lookahead](https://arxiv.org/abs/1907.08837)* (2019) -- TDFA(1), which makes
  a field cost one write instead of one per character;
* Angelo Borsotti and Ulya Trofimovich, *[A closer look at
  TDFA](https://arxiv.org/abs/2206.01398)* (2022) -- the algorithm in full.

Two things here are not from those papers. The **disambiguation policy** is
leftmost-first -- Perl's rule, RE2's, CTRE's -- where the papers implement
POSIX and leftmost-greedy; the mechanism is the cut, which is what a Pike VM
does by killing lower-priority threads at a Match instruction. The **format
layer** has no paper behind it.

## Tests and fuzzing

Around eighty test files, one or two patterns each: compiling a pattern is a
constant evaluation, and a translation unit holding ten of them costs ten times
as much whenever one is touched.

* **A differential fuzzer against RE2.** Everything that happens while a
  pattern is compiled is ordinary code that also runs, so the fuzzer builds
  machines from patterns made up at run time and compares matched-or-not, where
  a head ended, and where every group began and ended.

  ```sh
  cmake -B build -DSCAN_BUILD_FUZZER=ON && cmake --build build --target differential_fuzz
  ./build/differential_fuzz --seed 1 --rounds 200000
  ```

* **The walk against the interpreter.** The fuzzer cannot reach the walk, which
  exists only where something is compiled, so a test compares it against the
  interpreter over an automaton built from the same pattern by the same code,
  on every subject up to four characters.

## Building

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

| option | |
| --- | --- |
| `SCAN_BUILD_BENCHMARKS` | the benchmarks: against CTRE, RE2, re2c and `sscanf`. Brings all four in |
| `SCAN_BUILD_FUZZER` | the differential fuzzer; brings RE2 and abseil with it |
| `SCAN_FUZZER_LIBFUZZER` | the same fuzzer under libFuzzer with the sanitizers |
| `SCAN_AUTOMATA_AT_RUNTIME` | build the machine on first use rather than writing it out while compiling. On for a top-level build; the suite is run both ways |
| `SCAN_FIELDS_BY_BINDING_PACK` | the fields of an aggregate from a structured binding pack rather than from Boost.PFR |
| `SCAN_MODULES` | build and install the module interface units, beside the headers; on by default |

The library is a module graph -- `scan.core`, `scan.tre`, `scan.views`,
`scan.compiler`, `scan.runtime`, `scan.shape`, `scan.range`, `scan.regex`,
`scan.scanners` -- with `scan` as an umbrella that re-exports it.

### With modules

```cmake
cmake_minimum_required(VERSION 4.4 FATAL_ERROR)

# This library is written with `import std`, so a consumer asks for it too.
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
set(CMAKE_CXX_MODULE_STD 1)

project(mine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)

find_package(scan REQUIRED)
add_executable(mine main.cc)
target_link_libraries(mine PRIVATE scan::scan)
```

The prefix holds the archive, the interface units (`lib/scan/modules/src/`),
the CMake package and a port declaration. Your build compiles the interface
units, so it wants the compiler the library was built with -- a BMI is not a
portable artefact.

### As somebody else's subproject

```cmake
include(FetchContent)
FetchContent_Declare(scan
  GIT_REPOSITORY https://github.com/j4niwzis/scan.git
  GIT_TAG        v0.1.0)   # a release tag; `main` to follow it
FetchContent_MakeAvailable(scan)

target_link_libraries(mine PRIVATE scan::scan)
```

Nothing has to be installed in your build first. This library wants Boost.PFR
and resolves it itself: a provider is installed by the top-level `project()`
and by no other, so this one includes
[cmake-everywhere](https://github.com/j4niwzis/cmake-everywhere) without the
hook and asks by name. Your build is left as you configured it, and where
something already answers `find_package(boost_pfr)` that is what answers.

Or depend on nothing at all -- C++26, no library, nothing fetched:

```cmake
set(SCAN_FIELDS_BY_BINDING_PACK ON)
FetchContent_MakeAvailable(scan)
```

### Without modules

The same library as headers, in `include/`, generated from these interface
units by [demodulizer](https://github.com/j4niwzis/demodulizer). Every push to
`main` regenerates, builds and links them and commits them back, so a checkout
has them already.

One set answers both switches -- a generated header keeps the condition around
the import it came from. Through `cmake-everywhere`, two features:

```cmake
find_package(scan REQUIRED COMPONENTS headers)                # the first
find_package(scan REQUIRED COMPONENTS headers binding-pack)   # the second
```

Boost.PFR is the only dependency and the switch is whether to have it. An
aggregate's fields are wanted three ways -- how many, the type at an index, the
reference in a value. Boost.PFR probes for them; `auto&& [...parts] = value;`
names them, which is C++26 and so off by default. On, nothing is fetched and
nothing is linked.

## Licence

GNU General Public License, version 3 -- the text is in `LICENSE`. A program
that links this library is a work based on it, and the licence is what asks
that whoever receives that program can have its source as well.
