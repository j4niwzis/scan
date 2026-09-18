# scan

Reading text into values, with the pattern known while the program is
compiled.

A pattern or a format written here becomes a tagged deterministic automaton at
compile time, and the walk over that automaton is written out state by state.
There is no pattern object at run time, no interpreter, and nothing is
allocated to match. What comes out of a scan is a value of the type you asked
for -- an aggregate, a variant, a list, a view into the subject -- rather than
a match object you then take apart.

Two layers sit on one machine:

* the **pattern layer** answers questions about text: does this match, what is
  the head of it, where are the matches, what lies between them;
* the **format layer** reads text into a type: `{}` for each value, the type's
  own fields deciding what each place means.

Four kinds of subject are read by the same machine and answer the same way:
characters in a row, input that arrives in pieces, a forward range, and a range
that can only be read once.

The pattern is a template argument, so a pattern that is only known while the
program runs cannot be read here. Everything below follows from that: the walk
has no dispatch in it, the cost of a reading can be asked before the program
runs, and a subject that can never be looked at twice can still be read.
Compiling a pattern is a constant evaluation that builds a machine, and a
program with hundreds of them will feel it.

Today it wants clang and `import std`; Boost.PFR unless C++26 binding packs are
turned on, and a generated header form for projects without modules.

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

**Contents.** [A tour](#a-tour) · [What the answers mean](#what-the-answers-mean)
· [The subject](#the-subject) · [The pattern layer](#the-pattern-layer) ·
[The format layer](#the-format-layer) · [Contexts](#contexts) ·
[Extension points](#extension-points) · [The pattern syntax](#the-pattern-syntax)
· [Speed](#speed) · [Building](#building)

## A tour

Everything below is a callable object, so it can be used as a function or piped
into with `|`, which is what a range adaptor closure is for.

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

The rule is Perl's, which is also RE2's and CTRE's: **the first alternative
under which the whole expression matches wins, however short it is.**
Alternation is ordered, repetition is greedy unless it is written lazy, and
where several parses are possible the one the order prefers is the one whose
groups you get.

```cpp
scan::each<"{{for}|{each}|{foreach}}">("foreach")   // for, then each
scan::each<"{{foreach}|{for}|{each}}">("foreach")   // foreach
```

In the first, the match of `for` is the first walk in that state, so everything
below it has lost and the record ends there. In the second, the walk of
`foreach` sits above that match and is still alive, so the machine goes on.
This is not the longest match: `for|each|foreach` reading "foreach" stops at
`for`, where a lexer would take the whole word.

### Anchored, or a head

A pattern is read in one of two ways, and the difference is what ends the
reading:

* **anchored** -- the subject ends it. `match` and `scan` read this way.
* **a head** -- the match ends it. `starts_with`, `search`, `scan_prefix`,
  `each`, `split` and `search_all` read this way.

The two need different machines. For a head, a walk below a match has lost and
is cut where the automaton is built. Anchored, that same walk may be the only
one that reaches the end of the subject, so it is kept:

```cpp
scan::match<"a|ab">("ab")          // matches `ab`: `a` cannot reach the end
scan::starts_with<"a|ab">("ab")    // takes `a`: `a` matched first
```

### Nothing is walked back, except by a note

A walk can go past a match on the chance of a longer one the order prefers, and
die without finding it -- `foreach|for|each` reading "fore". So the walk keeps
a note: the place, and the registers as they stood there. Whether the automaton
can walk past a match at all is asked while it is compiled; where every step
out of a match lands in another match, the note is a pointer and nothing is
copied. There is no backtracking beyond that note, and nothing is ever tried a
second way.

### Groups

Groups are numbered by their opening parenthesis, from one; nought is the whole
match. A group that took no part in the winning parse says so rather than
coming back empty, and a group in a repetition holds the turn that won, which
for a greedy repetition is the last one.

## The subject

| subject | how it is read | what is held |
| --- | --- | --- |
| characters in a row (`string_view`, `string`, `vector<char>`) | in words and vectors past a threshold, a character at a time below it | nothing; groups are pointers into the subject |
| pieces (a range of contiguous ranges, or `subject \| scan::in_pieces<N>`) | each piece in words and vectors, the reading held between them | nothing; the place a record ended is an address inside a piece |
| a forward range | a character at a time | nothing; the note is an iterator, and going back is assigning it |
| a range read once (`views::istream`, `istreambuf_iterator`) | a character at a time, once | the characters read past a match, and no more |

A string literal is a subject like any other, and the nul the compiler put at
the end of it is not part of it: `scan<"{}">("450")` reads three characters. It
is read *to* that nul rather than by counting, which is the faster of the two
walks. A buffer you filled yourself is as long as it says it is: hand over a
`string_view` of the part that was filled.

### A subject that can only be read once

A deterministic machine never looks ahead, so it can read a range that has no
way back -- `std::views::istream`, a socket, a pipe -- and gather the fields as
they arrive:

```cpp
std::istringstream source("set speed 42\nset gain 7\n");
source >> std::noskipws;  // or the spaces never reach the machine
struct command { scan::held<16> name; int value; };
for (const command& one :
     scan::each<"set {[a-z]+} {[0-9]+}\n">(std::views::istream<char>(source))
         .of<command>()) { … }
```

`std::noskipws` is not this library's idea: `>>` steps over whitespace by
default, so the range would hand over `setspeed42setgain7`.

Nothing is buffered. The characters go through the machine as they come, each
field gathers into whatever collects it, and the record is built where the
match ends. Two things are held, and both are numbers the pattern names while
it is compiled:

* **what was read past a match**, where the walk went on for a longer one and
  died -- those characters belong to the next record;
* **what a failed attempt swallowed**, where a search starts one character
  later and needs the characters again.

For most patterns both are zero. Where a number cannot be named -- a cycle with
no match along it, like `a+b`, which can eat any number of characters and still
not match -- the reading is refused where it is compiled, and told why. It is
not silently buffered. The order of the alternatives is what decides this:

```cpp
scan::search_all<"a|abcd">   // holds nothing: the match of `a` ends the walk
scan::search_all<"abcd|a">   // holds two characters: `abcd` outlives the match
```

## The pattern layer

Every entry point is a callable object taking a subject, and every one is also
a range adaptor closure, so `subject | scan::search<p>` is `scan::search<p>(subject)`.

| | what it answers |
| --- | --- |
| `scan::match<p>` | whether the whole subject is `p`, with the groups |
| `scan::starts_with<p>` | the head of the subject that `p` takes |
| `scan::search<p>` | the leftmost match anywhere |
| `scan::search_all<p>` | every match in turn, as a lazy view |
| `scan::split<p>` | the pieces between the matches, as a lazy view |
| `scan::tokenize<p>`, `scan::iterator<p>`, `scan::range<p>` | other spellings of `search_all` |

`search` and `split` want characters that are all there; `match`,
`starts_with`, `search_all` and `split` read a subject that arrives once as
well, under the rules above.

### Saying what the reading should be

The caller's three choices are methods, so they compose in any order:

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

**A terminator** is a character the pattern can never match, sitting after the
subject -- which a `std::string` always has and a `string_view` into the middle
of something does not. Where there is one, the walk tests the character and not
the end of the input as well. That the pattern cannot match it is checked while
it is compiled; that it is really there is your promise.

**The walk** is chosen by how long the subject is, unless you say. A subject of
a few dozen characters is read faster one at a time, a long one in words --
and saying so means the length is never looked at and the walk you did not name
is not written at all.

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

A submatch answers the same way -- `operator bool`, `begin`/`end`/`size`,
`held()`, and `data()`/`to_view()`/the conversion where the characters lie in a
row. `get<k>` past the last group is an empty submatch rather than an error.

### Collectors: what a group comes back as

| subject | what a group is |
| --- | --- |
| characters in a row | `std::string_view` into the subject; nothing is copied |
| a forward range | `std::ranges::subrange<It, It>`; nothing is copied either |
| pieces, or a range read once | owned, because what it was read from is gone; `std::string` unless you say otherwise |

Which of the three you get is decided by the subject, so the same reading gives
views where it is handed a string and owns what it kept where it is handed a
socket. `into<T>()` says what "owned" means and says nothing else: where the
characters can be pointed at they are pointed at, and `into<std::pmr::string>()`
on a `string_view` subject changes nothing.

`into(collectors…)` says it for each group separately, in the order the groups
were written:

```cpp
scan::match<"([0-9]+)-([a-z]+)-([a-z]+)">.into(
    scan::as<int>(),                       // parsed into a value
    scan::as<std::pmr::string>(&pool),     // built with the arguments given
    scan::skip())                          // nothing kept, and no room taken
```

| | |
| --- | --- |
| `scan::text()` | the characters, held as the subject affords -- the default |
| `scan::as<T>(args…)` | a `T`: built as `T(first, last, args…)` where the arguments allow it, otherwise parsed by `scan::scanner<T>` |
| `scan::skip()` | nothing at all; `scan::skipped` stands in the answer and the group takes no room |
| `scan::collecting<T>(push, args…)` | a `T`, made from `args…`, with every character handed to `push` |

One collector to a group -- until one of them reads a type that has groups of
its own. Those groups are that type's, so the collector after it starts past
them:

```cpp
// `version` wrote three groups, so this is one collector over four groups.
scan::match<"v=(([0-9]+)\\.([0-9]+)\\.([0-9]+))-([a-z]+)!">.into(
    scan::as<version>(), scan::text())(text);
```

`collecting` is for an answer that is neither a string nor a parsed value -- a
count, a hash, a checksum:

```cpp
scan::match<"([a-z]+)">.into(scan::collecting<std::size_t>(
    [](std::size_t& sum, char letter) { sum += static_cast<unsigned char>(letter); }));
```

## The format layer

A format is a pattern with places in it, and each place is a value of the
output type:

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

All three take the same policy methods as `match` -- `sentinel`, `scalar`,
`vec`, `sized`, `by_length` -- and the output type can be named at either end,
because a scan runs when its type is known and until then it is only a
description of one:

```cpp
scan::scan<f>(text).of<row>()                     // said after
scan::scan<f>.of<row>()(text)                     // said before: a whole reading
scan::scan<f>(text).sentinel().of<row>()

constexpr auto read_row = scan::scan<"{},{},{}">.sentinel().of<row>();
for (const std::string& line : lines) rows.push_back(read_row(line));
```

Assigning the result of a scan converts it, and a conversion has nowhere to put
a failure but an exception. `of<T>()` is that conversion under another name;
`try_of<T>()` hands back `std::expected<T, …>` instead. `scan_prefix` has
`take<T>()` and `try_take<T>()`, which give the value and the rest of the
subject.

**`past_space`** says once what `{*\s*}` before every place says over and over:
every place begins past whatever whitespace is in front of it, which is what
`%d` does in a `scanf` format and `{}` does not.

```cpp
scan::scan<f>.past_space()(text)
constexpr auto stamp = scan::fixed_string("{}-{}-{}T{}:{}:{}").past_space();
```

### The types a place can be

* anything with a `scan::scanner<T>`: the integers, the floating-point types,
  `bool`, `char`, `std::string`, `std::pmr::string`, `std::string_view`, and
  `scan::held<N>` -- characters in room said in advance, for a reading with no
  allocator, which keeps what fits and says `overflowed` for what did not;
* an aggregate, whose fields are the places inside a nested `{…}`;
* a sum type -- `std::variant`, or anything with a `scan::branches<T>`;
* a range, which takes as many turns as the place allows.

A type that is both a range and has a scanner -- `std::string` -- is read as
one value, unless its scanner says `as_a_list`.

### Reading into a list

```cpp
struct row { std::vector<int> values; };
const row one = scan::scan<"{{}{*,?}}">("1,2,3,4");
```

The place is a shape of two: the element, and a separator kept by nobody. Left
alone it takes as many turns as the subject affords; a repetition after it
bounds them, and the bounds are known where the reading is compiled:

```cpp
scan::scan<"{{}{*,?}}{2,3}">("1,2,3")   // at least two turns, at most three
scan::scan<"{{}{* ?}}+">(pairs)         // one or more
scan::scan<"{{}{*,?}}?">(text)          // none or one -- which an optional is
```

The container is filled through `push_back`; where the count has an upper bound
and the container can grow, the room is taken once rather than a handful at a
time. A container with room of its own says so with `scan::room_for`, and then
a place that could ask for more than it holds is refused where it is compiled.

A list is read by the machine that gathers as it goes even where the subject
lies in a row, because a repeated group keeps only the turn that won.

### Reading into a sum

```cpp
struct row { std::variant<int, std::string_view> value; };
const row one = scan::scan<"{{[0-9]+}|{[a-z]+}}">(text);   // the format says the branches
const row two = scan::scan<"{}">(text);                    // each type's own pattern
```

The branches are tried in the order they are written, and the first under which
the whole reading succeeds is the one you get. Which branch ran is read from
the mark that branch left, not by trying the alternatives again. Anything with
a `scan::branches<T>` is read the same way.

### Failures

What is thrown says what went wrong by its type, and holds its message as a
pointer to a literal -- so a failure allocates nothing either:

| thrown | what it means |
| --- | --- |
| `scan::no_match` | the subject is not what the pattern says: nothing matched, nothing matched at the head, no branch took it |
| `scan::no_group` | a value was asked for out of a group that took no part |
| `scan::bad_field` | a place matched and what stood there is not that type |
| `scan::out_of_range` | it is that type and it does not fit |
| `scan::wrong_subject` | the reading asked for cannot be had off this kind of subject |

All of them are `scan::scan_error`, which is an `std::exception` and not an
`std::runtime_error` -- the latter keeps its message in a `std::string`, and
this one has nothing to keep. `scan::field_error` sits between `scan_error` and
the two field kinds and is never thrown itself: it is the name for catching
either.

**Nothing in this library catches anything, and the reading itself never
throws.** A reading hands back what it read or what went wrong, all the way
down. Asking for the value rather than trying for it is the one place where a
failure becomes a throw -- `of<T>()`, the conversion, `take<T>()` -- and that
throw happens at the asking, not inside the walk. So the whole reading is
usable where exceptions are turned off, which is what an embedded target is.

A scanner of your own that throws still throws, and it throws past everything:
a reading that was asked to *try* does not turn it into a failure, because
turning it into one would mean catching it. To have your failure handed back,
hand it back.

Handed back rather than thrown, the kind survives: the error type of `try_of`
and `try_take` is a `std::variant` of exactly the kinds that reading *that
output* can produce -- this library's, and the ones its own scanners say.
`scan::what(…)` gives the message whichever kind it holds.

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
where the value of a place is made, and going no further. It inherits nothing,
it is not wrapped, and its type is never forgotten.

```cpp
scan::scan<"{} {}">(text).of<pair>(fast)                // one is everybody's
scan::scan<"{} {}">(text).of<pair>(fast, slow)          // one per place, in order
scan::scan<"{} {}">(text).of<pair>(fast, scan::default_context)   // this place wants none
scan::scan<"{} {} {}">(text).of<nest>({{fast, scan::default_context}, slow})
const pair got = scan::scan<"{} {}">(text).with(fast);  // before the type is named
```

* **One** context is everybody's: whoever takes one takes it, and a scanner
  that takes none is read as it always was.
* **More than one** is one per place, in the order the places are read. Handing
  a context to a place whose scanner takes none is said while it is compiled
  rather than quietly dropped.
* **In braces**, the list goes as deep as the shape does, so the parts of a
  place can each have their own -- including the branches of a sum. A value at
  a place is that place's and all of its parts'.
* A fold or a list is told its context **without** braces: it is one value made
  of many turns, not a shape of parts.
* `with(…)` says the same thing before the output type is named, which is what
  a reading assigned to a variable needs.

A context reaches exactly the call that makes a value, which is `parse`,
`from_groups`, `begin` or `begin_groups` -- as an overload taking one more
argument. All of it works in a constant expression, and on every kind of
subject.

```cpp
template <> struct scan::scanner<tagged> {
  static constexpr std::string_view pattern() { return "[a-z]+"; }
  static tagged parse(std::string_view text);
  static tagged parse(std::string_view text, const room& where);   // told one
};
```

**A context that keeps memory is used for what the reading builds.** A
`std::pmr::memory_resource*`, an allocator, or anything answering `resource()`,
`get_allocator()` or `told_resource()` is asked for it, and then the containers
a place makes -- a `std::pmr::string` field, a list that grows -- are built
with it:

```cpp
std::pmr::monotonic_buffer_resource bytes;
struct two { std::pmr::string name; std::pmr::string tail; };
const two got = scan::scan<"{[a-z]+} {[a-z]+}">(text).of<two>(
    std::pmr::polymorphic_allocator<>(&bytes));
```

## Extension points

Everything a type can say about how it is read is a specialisation or a member.
Nothing is virtual and nothing is inherited, apart from `aggregate_scanner`,
which is a convenience.

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
  // What the place matches when the format does not say. A member or a
  // function, and taking the parameters written after the colon if it wants.
  static constexpr std::string_view pattern() { return "[0-9]+(?:\\.[0-9]+)?"; }
  static constexpr auto pattern(std::string_view parameters);

  // From the text of the place, with the parameters if it wants them.
  static constexpr weight parse(std::string_view text);
  static constexpr weight parse(std::string_view text, std::string_view parameters);

  // For a subject that arrives a character at a time: make a state, take the
  // characters, then make the value.
  static constexpr state begin();
  static constexpr state begin(std::string_view parameters);
  static constexpr void push(state&, char);
  static constexpr weight finish(state);
};
```

`parse` alone is enough for a subject held in memory; `begin`/`push`/`finish`
are what a one-pass reading uses, and a type that has them can be a field of a
record read off a stream. This is what the library's own scanners look like:
the integers take `{:x}`, `{:o}`, `{:b}`, `{:i}` and a width through
`parameters`, and gather digits through `push` so that a number can be read off
a socket.

Every function of yours that makes a value has a second shape, used wherever
the throwing one would be, that hands the failure back instead:

| asked for | handed back |
| --- | --- |
| `parse(text[, parameters])` | `try_parse(text[, parameters])` |
| `finish(state)` | `try_finish(state)` |
| `from_groups(groups)` | `try_from_groups(groups)` |
| `finish_groups(state)` | `try_finish_groups(state)` |

A push has no such shape and needs none: the walk is not over when a character
arrives, so a push that finds something wrong says so in its own state and
hands it back at the end -- which is what `scan::held<N>` does with a field too
long to fit. The kinds have to be `scan_error`s, because asking for the value
rather than trying for it throws what was handed back, and a scanner says them
in the best place there is, the type it returns:

```cpp
static std::expected<weight, std::variant<too_heavy, not_a_weight>>
try_parse(std::string_view text);
```

### A shape: a type that says a whole format

```cpp
template <>
struct scan::scanner<point> : scan::aggregate_scanner<"({}, {})"> {};

struct line { point from; point to; };
const line one = scan::scan<"{} -> {}">("(1, 2) -> (3, 4)");
```

The places inside `point`'s format mean `point`'s fields wherever it is used.
Nothing is read twice: the outer pattern and the inner one are one automaton.

With a `parse` taking the places as arguments, the type need not be an
aggregate at all -- it may have invariants, private members, or an order of its
own:

```cpp
template <>
struct scan::scanner<angle> : scan::aggregate_scanner<"{}deg{}min"> {
  static constexpr angle parse(int degrees, int minutes) {
    return angle(degrees * 60 + minutes);
  }
};
```

### A type that reads its own groups

A leaf may say a pattern with groups in it and be built from those groups
rather than from the text it stood on. They are groups of the same match, found
on the way past.

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

This works in a format and in a pattern alike, and a place standing for such a
type takes parameters but not a pattern of its own: the groups are counted off
the pattern the type declares.

`from_groups` is handed views of the subject, so it wants a subject there is
something left to point at. Reading a subject that arrives once into a type
that says only `from_groups` does not compile -- by then the characters are
gone. Such a type reads a stream by saying the fold instead.

### A fold: a type told its groups as they happen

`from_groups` hands over what is there when the match is over, which for a
repeated group is the last turn and nothing before it. A type whose groups
repeat is therefore told the turns as they go and folds them itself:

```cpp
template <>
struct scan::scanner<numbers> {
  struct state { std::vector<int> values; int running = 0; };

  static constexpr std::string_view pattern() { return "([0-9]+)(?:,([0-9]+))*"; }

  static constexpr state begin_groups();
  // One overload a group: the group's number said as a type you can overload
  // on. `std::size_t` does as well.
  static constexpr void opened_group(state&, scan::group_at<0>);
  static constexpr void push_group(state&, scan::group_at<0>, char);
  static constexpr void closed_group(state&, scan::group_at<0>);
  // … and the same three for group 1, which repeats
  static constexpr numbers finish_groups(state);
};
```

An opening and a closing arrive once a turn, openings in the order the groups
are written and closings innermost first. Where the subject can be pointed at,
a closing can be handed the whole of what the group stood on --
`closed_group(state&, scan::group_at<k>, std::string_view)` -- and then the
characters are not handed over at all, so a run the walk stepped over in
vectors costs one call rather than one a character. A fold that says only that
form says by it that it wants a subject it can point at, and asking it to read
a stream throws rather than quietly holding the characters. Every hook is
optional but `begin_groups` and `finish_groups`.

One thing is the price of a fold and not a detail. **The walk stands in several
readings of the subject at once**, and it carries a fold with each of them: the
state is copied where a reading divides and dropped where a reading dies. So
the state must be copyable, and it must be the only thing the fold touches --
anything written outside it would be written for a reading that never happened.

### A list, a sum, a container of your own

```cpp
// Read as many as there are, even though the type has a scanner of its own.
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

A container is read into through `value_type` and `push_back`; `room_for` is
what lets a place that could take more turns than it holds be refused where it
is compiled, and what lets one that can grow take all the room at once.

`scan::fields<T>` answers the three questions a shape is asked of its fields --
how many, what the one at an index is, how to reach it in a value -- and is
specialised for a type that is not an aggregate.

### A collector of your own

A collector is any type with the parts below, and none of them is virtual or
inherited. Write the ones it needs and leave the rest out.

```cpp
struct hex_bytes {
  // What it makes. Either one type…
  using value_type = std::vector<std::byte>;
  // …or a type per holder, where what it makes depends on what the subject
  // affords -- a view where the characters can be pointed at, something owning
  // where they cannot.
  template <class Holder> using value_for = Holder;

  // From the whole group at once, where the subject can be pointed at. The
  // second argument is whatever was written after the colon in the format.
  value_type from_text(std::string_view text, std::string_view parameters) const;
  template <class Holder>
  Holder from_text(std::string_view text, std::string_view parameters) const;

  // A character at a time, where it cannot: this makes the value…
  value_type begin_pushing(std::string_view parameters) const;
  // …this is handed every character of the group as it arrives…
  void push_one(value_type& into, char letter) const;
  // …and this turns it into the answer.
  value_type finish_pushed(value_type state) const;

  // Optional: a run the walk stepped over in vectors, in one go rather than a
  // call a character.
  void push_run(value_type& into, std::string_view run) const;
  // Optional: nothing at all from this group. `scan::skipped` stands in the
  // answer, and this is what `scan::skip()` is.
  static constexpr bool takes_nothing = true;
};
```

Write both halves and the collector works everywhere; write only `from_text`
and it works wherever the characters can be pointed at. The collector that
keeps the characters -- the default one -- is written in exactly these terms
and has no privileges of its own.

## The pattern syntax

Literals; `.`; classes `[a-z]`, `[^a-z]`, with ranges and escapes; the escapes
`\d \D \s \S \w \W` and the usual `\n \t \\` and friends; groups `(…)` and
`(?:…)`; alternation `|`; the repetitions `* + ? {n} {n,} {n,m}`, each of them
lazy with a `?` after it (`*?`, `+?`, `??`, `{n,m}?`).

There is no lookaround, there are no backreferences, and there are no Unicode
properties. Patterns are bytes: a UTF-8 literal matches itself, and `.` is one
byte rather than one code point.

## Where this differs from other engines

**From `sscanf`.** Whitespace is not skipped unless the format says so
(`past_space`); the whole subject must match unless you read a head with
`scan_prefix`; a scan is all or nothing where `sscanf` hands back how many
fields it filled; an integer too big for its type is an error rather than
undefined behaviour; and nothing is locale-dependent.

**From Perl and RE2, in one corner.** A quantifier around something that can
match nothing is where every engine answers differently. `([ab]*?)*` against
"ba", anchored:

| | group 1 |
| --- | --- |
| Perl | `[2,2)` |
| Python | `[2,2)` |
| `scan::scan` | `[1,2)` |
| RE2 | `[0,2)` |

Following the order a backtracking engine tries things in gives this library's
answer: the loop takes `b`, then takes `a`, and a third turn would match
nothing. Perl divides it the same way and differs only by taking that last
empty turn.

**Anchored and head readings differ where the order is what decides**, as
`a|ab` above -- which is the same in Perl, where `^(?:a|ab)$` matches "ab" and
`(?:a|ab)` matches "a".

## What is known while it is compiled

The automaton is asked these before your program runs, and they are what the
refusals and the costs are made of:

* **the shortest match** -- a subject shorter than it is answered without
  reading a character;
* **the walk past a match** (`fallback_window`) -- how far the machine can read
  past a match before it dies, which is what a reading that cannot go back has
  to hold, and whether it has to hold anything at all;
* **the walk from the start** -- how much a failed attempt can swallow, which
  is what a search over a subject read once has to give back;
* **whether a terminator is safe** -- that the pattern cannot match it;
* **how much of the walk to write out** -- the chain is written state by state
  up to a budget, and only where the state has one way out.

Determinization stops at twenty thousand states and says so. It costs
exponentially more states than an expression has symbols for expressions that
are perfectly ordinary -- anything that reads freely and then counts, `.*a.{20}`
and its like -- and where that happens here it is not a slow program but a
compilation nobody waits for.

## Speed

Measured on one machine -- AMD Ryzen 9 9950X, clang 22.1.8 with libc++, `-O3
-march=native`, whole-program optimisation -- against CTRE, RE2, and `re2c`,
which generates a scanner from a pattern in a separate build step. Take the
numbers as shapes rather than as decimals. Each engine's code is generated by a
backend run of its own, because an `-mllvm` option handed to a link under LTO
is handed to every engine at once; this library is built with
`-jump-threading-across-loop-headers`, which is worth two to four per cent to a
walk written out with a label for every state, and nineteen to re2c's scanner.

**Five fields of letters out of one record** (`benchmarks/captures_benchmark.cc`),
thirty characters, thirty-two records to a pass, median of seven:

| | a pass |
| --- | --- |
| the floor, nothing scanned | 268 ns |
| `scan::scan<f>.sentinel()` | 477 ns |
| re2c | 554 ns |
| `scan::scan<f>` | 659 ns |
| CTRE | 717 ns |
| RE2 | 15986 ns |

The same five fields out of a thousand characters, where a field is two hundred
letters and the run is worth stepping over: 57.8 ns against re2c's 637, CTRE's
692 and RE2's 11751. The crossover is the length of a *field*, not of the
subject: the reading wins where there is a run to step over in vectors.

**Recognition** (`benchmarks/address_benchmark.cc`), an address of thirty-two
characters, thirty-two subjects to a pass: 436 ns for
`scan::match<p>.sentinel().scalar()`, 545 without the terminator, against
re2c's 1186, RE2's 2616 and CTRE's 14339. Both of this library's rows say
`.scalar()`: the length at which the reading starts taking words is worked out
from the pattern, and for this one it lands below thirty-two, so left to itself
it asks for words on a subject too short to pay for them.

**Against `sscanf`**, same characters in, same values out, thirty-two records:

| | `sscanf` | here |
| --- | --- | --- |
| two numbers | 2831 ns | 470 ns |
| a timestamp of six | 6827 ns | 1551 ns |
| five words into views | 10155 ns | 934 ns |
| five words into room said in advance | 10155 ns | 4265 ns |

**A fold** (`benchmarks/fold_benchmark.cc`) measures the thing the others
cannot do: a type told which of its own groups each character belongs to while
the walk passes over it, doing its arithmetic there -- no turn kept, no
substring made, the number finished when the match is. Against this library's
own automaton written out by hand as labels and direct jumps, and against the
same reading written by hand as loops and a pointer: per element the walk and
the machine written out are within two per cent of each other, and the walk is
sixteen per cent cheaper than the hand-written loops. What it is not is cheap
to enter -- about 120 ns against 27 and 14 -- which is the gathering and the
register commands arranged before the first character, paid once a reading.

Two benchmarks say the same thing about the same fault: the threshold between
the two walks is measured from the pattern and not from the subject, and where
they disagree `.scalar()` is what to say.

## What this is built on

The machine is a tagged deterministic finite automaton:

* Ville Laurikari, *NFAs with Tagged Transitions, their Conversion to
  Deterministic Automata and Application to Regular Expressions* (2000);
* Ulya Trofimovich, *[Tagged Deterministic Finite Automata with
  Lookahead](https://arxiv.org/abs/1907.08837)* (2019) -- TDFA(1), which is
  what makes a field cost one write instead of one per character;
* Angelo Borsotti and Ulya Trofimovich, *[A closer look at
  TDFA](https://arxiv.org/abs/2206.01398)* (2022) -- the algorithm in full.

Two things here are not from those papers. The **disambiguation policy** is
leftmost-first, which is Perl's rule, RE2's and CTRE's; the papers implement
POSIX and leftmost-greedy. The mechanism is the cut: where a walk in a state
has matched, every walk below it in precedence has lost and is removed, which
is what a Pike VM does by killing lower-priority threads at a Match
instruction. The **format layer** -- reading a format against the type it scans
into, building the value where the match ends, and reading records one after
another off a subject that arrives as it is read -- has no paper behind it.

## Tests and fuzzing

Around eighty test files, each holding one or two patterns, because compiling a
pattern is a constant evaluation and a translation unit holding ten of them
costs ten times as much whenever one is touched.

* **A differential fuzzer against RE2.** Everything that happens while a
  pattern is compiled is ordinary code that also runs, so the fuzzer builds
  machines from patterns made up at run time and compares the answers --
  matched or not, where a head ended, where every group began and ended --
  against RE2, whose default rule is the same leftmost-first.

  ```sh
  cmake -B build -DSCAN_BUILD_FUZZER=ON && cmake --build build --target differential_fuzz
  ./build/differential_fuzz --seed 1 --rounds 200000
  ```

* **The walk against the interpreter.** The fuzzer cannot reach the walk
  itself, which exists only where something is compiled. So a test compares it
  against the interpreter over an automaton built from the same pattern by the
  same code, on every subject up to four characters.

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
| `SCAN_FIELDS_BY_BINDING_PACK` | the fields of an aggregate from a structured binding pack rather than from Boost.PFR |
| `SCAN_MODULES` | build and install the module interface units, beside the headers; on by default |

The library is a module graph -- `scan.core`, `scan.tre`, `scan.views`,
`scan.compiler`, `scan.runtime`, `scan.shape`, `scan.range`, `scan.regex`,
`scan.scanners` -- with `scan` as an umbrella that re-exports it. Importing
`scan` is all that is wanted.

### Without modules

A project that cannot take modules reads the same library as headers. They are
generated from these very interface units by
[demodulizer](https://github.com/j4niwzis/demodulizer), checked in the same run
that builds the library, and carried in the tree of a release; a checkout of
`main` does not have them and says so rather than failing later.

There is one set of them and it answers to both switches: a generated header
keeps the condition around the import it came from. Through `cmake-everywhere`
it is two features:

```cmake
find_package(scan REQUIRED COMPONENTS headers)                # the first
find_package(scan REQUIRED COMPONENTS headers binding-pack)   # the second
```

Boost.PFR is the only dependency, and the switch is whether to have it. A
shape's fields are asked for in one place and three ways: how many there are,
what the one at an index is, and how to reach it in a value. Boost.PFR answers
by probing what an aggregate can be built from; a structured binding pack
answers by naming them -- `auto&& [...parts] = value;` -- which is C++26, so it
is off by default. Turned on, nothing is fetched and nothing is linked.

## Licence

GNU General Public License, version 3 -- the text is in `LICENSE`. A program
that links this library is a work based on it, and the licence is what asks
that whoever receives that program can have its source as well.
