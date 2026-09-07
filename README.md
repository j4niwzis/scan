# scan

Reading text into values, with the pattern known while the program is
compiled.

A pattern or a format written here is turned into a tagged deterministic
automaton at compile time, and the walk over that automaton is written out
state by state. There is no pattern object at run time, no interpreter, and
nothing is allocated to match. What comes out of a scan is a value of the type
you asked for -- an aggregate, a variant, a list, a view into the subject --
rather than a match object you then take apart.

Two layers sit on one machine:

* the **pattern layer** answers questions about text: does this match, what is
  the head of it, where are the matches, what are the pieces between them;
* the **format layer** reads text into a type: `{}` for each value, the
  type's own fields deciding what each place means.

Four kinds of subject are read by the same machine and answer the same way:
characters in a row, input that arrives in pieces, a forward range, and a
range that can only be read once.

The pattern is a template argument, so a pattern that is only known while the
program runs cannot be read here. Everything below follows from that: the walk
has no dispatch in it, the cost of a reading can be asked before the program
runs, and a subject that can never be looked at twice can still be read.
Compiling a pattern is a constant evaluation that builds a machine, and a
program with hundreds of them will feel it.

Today it wants clang, `import std`, and `boost::pfr`; a header form is
coming.

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

## A five-minute tour

Everything below is a callable object, so it can be used as a function or
piped into with `|`, which is what a range adaptor closure is for.

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

Both are the same three branches. In the first, the match of `for` is the
first walk in that state, so everything under it has lost and the record ends
there -- the machine has nowhere to go and reads no further. In the second,
the walk of `foreach` sits above that match and is still alive, so the machine
goes on and finds the longer branch.

This is not the longest match. `for|each|foreach` reading "foreach" stops at
`for`, where a lexer would take the whole word.

### Two readings, and why they differ

A pattern is read in one of two ways, and the difference is what ends the
reading:

* **anchored** -- the subject ends it. `match` and `scan` read this way: the
  whole of the subject must be the pattern.
* **a head** -- the match ends it. `starts_with`, `search`, `scan_prefix`,
  `each`, `split` and `search_all` read this way.

The two need different machines. For a head, a walk below a match has lost and
is cut where the automaton is built, which is what makes the machine stop at
`for` above. Anchored, that same walk may be the only one that reaches the end
of the subject, so it is kept:

```cpp
scan::match<"a|ab">("ab")          // matches: `ab`, because `a` cannot reach the end
scan::starts_with<"a|ab">("ab")    // takes `a`, because `a` matched first
```

Both are the same rule said at the two ends: the first walk that gets to the
end of what was asked for.

### Nothing is walked back, except by a note

A walk can go past a match on the chance of a longer one the order prefers,
and die without finding it. `foreach|for|each` reading "fore" does that: it
passes the match of `for` on the `e` and then the subject runs out. The answer
is the place it passed.

So the walk keeps a note -- the place, and the registers as they stood there
where the automaton can walk past a match at all. That is the fallback of the
TDFA papers, and whether this automaton has one is a question asked while it
is compiled: where every step out of a match lands in another match, the note
is a pointer and nothing is copied.

There is no backtracking beyond that note. Nothing is ever tried a second way.

### Groups

Groups are numbered by their opening parenthesis, from one; nought is the
whole match. A group that took no part in the parse that won says so rather
than coming back empty, and a group in a repetition holds the turn that won,
which for a greedy repetition is the last one.

## The subject, and what each kind costs

| subject | how it is read | what is held |
| --- | --- | --- |
| characters in a row (`string_view`, `string`, `vector<char>`) | in words and vectors past a threshold, a character at a time below it | nothing; groups are pointers into the subject |
| pieces (a range of contiguous ranges) | each piece in words and vectors, the reading held between them | nothing; the place a record ended is an address inside a piece |
| a forward range | a character at a time | nothing; the note is an iterator, and going back is assigning it |
| a range read once (`views::istream`, `istreambuf_iterator`) | a character at a time, once | the characters read past a match, and no more |
| a range read once, through `\| scan::in_pieces<N>` | as pieces: in words and vectors inside each | the room asked for, twice over |

### A subject that can only be read once

This is the one worth explaining, because most engines cannot do it and the
ones that can give up submatches for it.

A deterministic machine never looks ahead: it always knows where to go from
the character in front of it. So it can read a range that has no way back --
`std::views::istream`, a socket, a pipe -- and gather the fields as they
arrive:

```cpp
std::istringstream source("set speed 42\nset gain 7\n");
struct command { scan::held<16> name; int value; };
for (const command& one :
     scan::each<"set {[a-z]+} {[0-9]+}\n">(std::views::istream<char>(source))
         .of<command>()) {
  …
}
```

Nothing is buffered. The characters go through the machine as they come, each
field gathers into whatever collects it, and the record is built where the
match ends.

Two things do have to be held, and both are numbers the pattern names while it
is compiled:

* **what was read past a match**, where the walk went on for a longer one and
  died -- those characters belong to the next record, and there is nowhere to
  put them back, so they are carried;
* **what a failed attempt swallowed**, where a search starts one character
  later and needs the characters again.

For most patterns both are zero. `\s+` holds nothing at all: every space is
already a whole match, so nothing is ever read past one, and the first
character that is not a space dies before it is taken. Where a number cannot
be named -- a cycle with no match anywhere along it, like `a+b`, which can eat
any number of characters and still not match -- the reading is refused where
it is compiled, and told why. It is not silently buffered.

### Handing it over in pieces instead

Reading one character at a time costs the walk its vectors: it cannot step
over a run of thirty letters in one instruction when it is handed them one at
a time. `in_pieces` gathers the characters into room said in advance and hands
over the room, so the reading becomes the pieces one -- and a piece is
characters in a row, which is what the fast walk wants:

```cpp
scan::each<f>(source | scan::in_pieces<512>).of<row>()
```

It is a separate thing rather than something a scan does for you, because it
is not always the right trade. It costs a copy of every character and a buffer
that has to live somewhere; it wins where the fields are long enough for the
walk to step over them, and loses where they are a few characters each and the
copying is the whole of the work. Which of the two you have is not something
the library can know.

The room is used two pieces at a time, in turn: the walk keeps an address
inside the piece it is holding -- where a record ended, so that the next one
starts there -- and asks for the next piece before it is done with that, so
the piece handed over before this one is still where it was.

**It changes what the program does, not only what it costs.** A range that
reads a character at a time is read a character at a time, and this library
reads exactly as far as the match needs and no further. That is what lets a
scan sit on a console, a socket or a pipe: the characters arrive as they are
typed or sent, a record is answered the moment it ends, and nothing further is
taken.

Put `in_pieces<512>` in front of that and the reading waits for five hundred
and twelve characters, or for the range to end, before the machine sees the
first one. On a terminal that means a program that answered every line stops
answering until the buffer fills. On a socket it means more of the stream is
consumed than the match needed -- and those characters are in the buffer, so
whoever reads the range afterwards will not find them.

So it is for a subject that is all there and merely arrives in a stream: a
file, a pipe already full, a decompressor. For anything that answers as it is
read, leave it out; reading one character at a time is what makes that work at
all.

The order of the alternatives decides this, which is the practical thing to
know:

```cpp
scan::search_all<"a|abcd">   // holds nothing: the match of `a` ends the walk
scan::search_all<"abcd|a">   // holds two characters: `abcd` outlives the match
```

## The pattern layer

Every entry point is a callable object taking a subject, and every one of them
is also a range adaptor closure, so `subject | scan::search<p>` is the same as
`scan::search<p>(subject)`.

| | what it answers |
| --- | --- |
| `scan::match<p>` | whether the whole subject is `p`, with the groups |
| `scan::starts_with<p>` | the head of the subject that `p` takes |
| `scan::search<p>` | the leftmost match anywhere |
| `scan::search_all<p>` | every match in turn, as a lazy view |
| `scan::split<p>` | the pieces between the matches, as a lazy view |
| `scan::tokenize<p>`, `scan::iterator<p>`, `scan::range<p>` | other spellings of `search_all` |

### Saying what the reading should be

The three things that are the caller's business are said as methods, so they
compose in any order and nothing is named twice:

```cpp
scan::match<p>(text)                          // the length says which walk
scan::match<p>.sentinel()(text)               // there is a terminator: '\0'
scan::match<p>.sentinel<'\n'>()(text)         // or this one
scan::match<p>.scalar()(text)                 // a character at a time, always
scan::match<p>.vec()(text)                    // in words and vectors, always
scan::match<p>.sentinel().scalar()(text)      // both
scan::match<p>.into<std::pmr::string>()(text) // where the answers are kept
```

`sized()` and `by_length()` are the opposites of `sentinel()` and the two walk
choices.

**A terminator** is a character the pattern can never match, sitting after the
subject -- which a `std::string` always has and a `string_view` into the
middle of something does not. Where there is one, the walk tests the character
and not the end of the input as well. That the pattern cannot match it is
checked while it is compiled; that it is really there is your promise.

**The walk** is chosen by how long the subject is, unless you say. A subject
of a few dozen characters is read faster one at a time, a long one is read
faster in words -- and where you know which, saying so means the length is
never looked at and the walk you did not name is not written at all.

### Collectors: what a group comes back as

By default a group is the characters themselves, held in whatever way the
subject affords:

| subject | what a group is |
| --- | --- |
| characters in a row | `std::string_view` into the subject; nothing is copied |
| a forward range | `std::ranges::subrange<It, It>` -- the two iterators, and nothing is copied either |
| pieces, or a range read once | owned, because what it was read from is gone; `std::string` unless you say otherwise |

Which of the three you get is decided by the subject and not by the pattern,
so the same reading written once gives views where it is handed a string and
owns what it kept where it is handed a socket. A field that was a
`std::string_view` becomes a `std::string` by moving the reading from one to
the other, and nothing else in the code changes.

`into<T>()` says what "owned" means, and it is worth knowing that it says
nothing else: where the characters can be pointed at they are pointed at, and
`into<std::pmr::string>()` on a `string_view` subject changes nothing at all.
It takes effect on the subjects where owning is what has to happen -- pieces
and a range read once:

```cpp
text | scan::match<"([a-z]+)">.into<std::pmr::string>()
```

`into(collectors…)` says it for each group separately, in the order the groups
were written, and then each group can be a different thing entirely:

```cpp
scan::match<"([0-9]+)-([a-z]+)-([a-z]+)">.into(
    scan::as<int>(),                       // parsed into a value
    scan::as<std::pmr::string>(&pool),     // built with the arguments given
    scan::skip())                          // nothing kept, and no room taken
```

| | |
| --- | --- |
| `scan::text()` | the characters, held as the subject affords -- the default |
| `scan::as<T>(args…)` | a `T`: parsed by `scan::scanner<T>` if it has one, otherwise built as `T(first, last, args…)` |
| `scan::skip()` | nothing at all; the group takes no room in the answer |
| `scan::collecting(push, args…)` | a value of any type, made from `args…`, with every character handed to `push` |

`collecting` is the one to reach for when the answer is not a string and not a
parsed value -- a count, a hash, a checksum:

```cpp
scan::match<"([a-z]+)">.into(scan::collecting(
    [](std::size_t& sum, char letter) { sum += static_cast<unsigned char>(letter); },
    std::size_t{0}));
```

#### Writing one of your own

A collector is any type with the parts below, and none of them is virtual or
inherited from anything. Write the ones your collector needs and leave the
rest out.

```cpp
struct hex_bytes {
  // What it makes. Either one type…
  using value_type = std::vector<std::byte>;
  // …or a type per holder, where what it makes depends on what the subject
  // affords -- a view where the characters can be pointed at, something
  // owning where they cannot.
  template <class holder> using value_for = holder;

  // From the whole group at once, where the subject can be pointed at. The
  // second argument is whatever was written after the colon in the format.
  value_type from_text(std::string_view text, std::string_view parameters) const;
  // Or the same, told what to make it as.
  template <class holder>
  holder from_text(std::string_view text, std::string_view parameters) const;

  // A character at a time, where the subject cannot be pointed at: this makes
  // the value…
  value_type begin_pushing(std::string_view parameters) const;
  template <class holder> holder begin_pushing(std::string_view parameters) const;
  // …this is handed every character of the group as it arrives…
  void push_one(value_type& into, char letter) const;
  // …and this turns it into the answer.
  value_type finish_pushed(value_type state) const;
};
```

`from_text` is what a subject held in memory uses; `begin_pushing`,
`push_one` and `finish_pushed` are what a subject that arrives once uses.
Write both halves and the collector works everywhere; write only the first and
it works wherever the characters can be pointed at.

Two more, both optional:

```cpp
  // A run the walk stepped over in vectors, handed over in one go rather than
  // as one call a character. Leave it out and the characters arrive one by
  // one, which is what happens anyway where the walk did not step over a run.
  void push_run(value_type& into, std::string_view run) const;

  // Nothing at all from this group: the characters need not be handed over,
  // and `scan::skipped` stands in the answer. This is `scan::skip()`.
  static constexpr bool takes_nothing = true;
```

The collector that keeps the characters -- the default one -- is written in
exactly these terms and has no privileges of its own:

```cpp
struct text_collector {
  template <class holder> using value_for = holder;

  template <class holder>
  constexpr holder from_text(std::string_view text, std::string_view) const {
    return holder(text.begin(), text.end());
  }
  template <class holder>
  constexpr holder begin_pushing(std::string_view) const { return holder{}; }
  constexpr void push_one(auto& into, char letter) const { into.push_back(letter); }
  constexpr auto finish_pushed(auto state) const { return state; }
};
```

### The groups are read once, not twice

Where a group is written with the very expression a type declares for its own
values, the value is built out of the groups the machine has already found:
nothing is matched twice and no substring is handed anywhere.

```cpp
struct point { int x; int y; };
template <> struct scan::scanner<point> : scan::aggregate_scanner<"({},{})"> {};

// `point` spells itself out as `\(([+-]?[0-9]+),([+-]?[0-9]+)\)`, and this
// group is written with exactly that. The two numbers are already groups of
// the big match, so `point` is built from them.
scan::match<"at=(\(([+-]?[0-9]+),([+-]?[0-9]+)\))">.into(
    scan::as<point>(), scan::skip(), scan::skip())(text);
```

Three things have to hold, and all three are decided while the program is
compiled: the type declares a **format** of its own rather than a pattern for
one value, that format has more than one place in it, and the group is written
with the same expression the format spells out.

**The same expression, not the same characters.** Both are read, both are put
into the plainest shape they can be, and the two trees are compared. So `+` and
`{1,}` agree, an extra `(?:…)` disagrees with nothing, and `a{2}` and `aa` are
the same two symbols.

That last one only where it is safe: a count is written out by hand exactly
when nothing inside it marks a place. `(a){2}` and `(a)(a)` stay apart, because
they really are different -- one group that took two turns against two groups
-- and what is being decided here is whether the groups already found are that
type's values, so anything that could move a group has to count as different.

What would need to know what a machine does with it is not done at all: `[a]`
stays a class and `a` stays a symbol.

Where the two really are the same and this says they are not, the text is read
the ordinary way and the cost is one reading. Where it said yes wrongly, the
answer would be wrong. So it says no unless the trees are the same tree.

#### Asking for the groups yourself

The above happens for a type that declares a format, and the value is put
together by the library. A type that declares a **pattern** of its own can ask
for the same thing and do the putting together itself, by saying
`from_groups`:

```cpp
struct version { int major, minor, patch; };

template <>
struct scan::scanner<version> {
  static constexpr std::string_view pattern() {
    return "([0-9]+)\\.([0-9]+)\\.([0-9]+)";
  }

  // Handed exactly the groups this pattern opens, in the order it opened
  // them, out of the match that has already happened.
  static constexpr version from_groups(std::span<const std::string_view> groups);

  // And how it reads itself where there are no groups to be had.
  static constexpr version parse(std::string_view text);
};

// The three numbers are groups of this match. `version` is handed them.
scan::match<"v=(([0-9]+)\\.([0-9]+)\\.([0-9]+))!">.into(
    scan::as<version>(), scan::skip(), scan::skip(), scan::skip())(text);

// Written without them, there is nothing to hand over, and `parse` reads the
// text as usual.
scan::match<"v=([0-9]+\\.[0-9]+\\.[0-9]+)!">.into(scan::as<version>())(text);
```

Which of the two happens is decided while the program is compiled, by the same
comparison of expressions.

#### Gathered by its own groups

`from_groups` hands the groups over when the match is done, which wants a
subject that can still be pointed at. The other way round is to be told, as
each character arrives, which of the type's own groups it belongs to -- and
then a type with parts can be read off a subject that will never be seen
again, with no text put together anywhere:

```cpp
template <>
struct scan::scanner<version> {
  static constexpr std::string_view pattern() {
    return "([0-9]+)\\.([0-9]+)\\.([0-9]+)";
  }

  // The groups, named. The k-th alternative is the k-th group.
  struct major_part {}; struct minor_part {}; struct patch_part {};
  using group = std::variant<major_part, minor_part, patch_part>;

  struct state { version made; };
  static constexpr state begin_groups() { return {}; }
  static constexpr void push_group(state& into, major_part, char letter) {
    into.made.major = into.made.major * 10 + (letter - '0');
  }
  static constexpr void push_group(state& into, minor_part, char letter) { … }
  static constexpr void push_group(state& into, patch_part, char letter) { … }
  static constexpr version finish_groups(state from) { return from.made; }
};
```

The names are optional. A type may be told which group a character belongs to
in whichever of these three ways it writes, and the choice is made where the
number is a constant, so none of them costs a branch:

```cpp
void push_group(state&, major_part, char);   // the name of the group
void push_group(state&, group, char);        // the group as a variant
void push_group(state&, std::size_t, char);  // its number, from nought
```

This is the same reading wherever it is used. Where the subject is in memory
the characters of each group are handed over the same way, so a type written
like this reads identically off a string and off a socket -- and where the
group is written with some other expression, `parse` or `from_groups` takes
over as before.

#### On a subject that can only be read once

There are no group texts there at all: the characters are gone as they are
read, and nothing can be handed a view into them afterwards. `from_groups` is
for a subject that can be pointed at, and on a one-pass reading it simply does
not apply.

The same reuse is there, and it is better -- it is what a **format** buys. A
type that declares one has its places spread into the one automaton, so the
machine knows which of that type's places is open at each character and hands
the character straight to that place's scanner. The three numbers of a
`version` are gathered as three numbers, and no text is ever put together:

```cpp
template <>
struct scan::scanner<version> : scan::aggregate_scanner<"{}.{}.{}"> {};

// Off a stream, holding nothing: each field of each record gathers into its
// own scanner as the characters arrive.
for (const row& one : scan::each<"v={}\n">(std::cin).of<row>()) { … }
```

So what a type says decides where it can be read:

| what the type says | pointed at | read once |
| --- | --- | --- |
| `parse(text)` | yes | no -- there is no text to give it |
| `begin` / `push` / `finish` | yes | yes: it gathers its own characters |
| `from_groups` | yes, and nothing is read twice | no -- the groups are not there to hand over |
| `begin_groups` / `push_group` / `finish_groups` | yes, and nothing is read twice | yes: told which of its own groups each character belongs to |
| a format (`aggregate_scanner`) | yes, and nothing is read twice | yes, and each place gathers on its own |

A type that will be read off a stream wants the last two rows: a format if it
has parts, and `begin`/`push`/`finish` if it is a value of its own. Writing
`parse` and `from_groups` as well costs nothing and is what makes it fast where
the subject is in memory.

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
| `{*…}` | matched and kept by nobody |
| `{{a}\|{b}}` | a `std::variant`: whichever branch took the input |
| `{…}*`, `{…}+`, `{…}{2,5}` | a list field: as many turns as it takes |

Everything else in the format is a pattern and matches itself.

| | |
| --- | --- |
| `scan::scan<f>(subject)` | the whole subject, as the type asked for |
| `scan::scan_prefix<f>(subject)` | the head of it, and what is left |
| `scan::each<f>(subject)` | one record after another, lazily |

`scan` and `each` take the same policy methods as `match`, and the type can be
said at either end, because a scan runs when its type is known and until then
it is only a description of one:

```cpp
scan::scan<f>(text).of<row>()                     // said after
scan::scan<f>.of<row>()(text)                     // said before: a whole reading
scan::scan<f>(text).sentinel().vec().of<row>()
scan::scan<f>.sentinel().vec().of<row>()(text)

constexpr auto read_row = scan::scan<"{},{},{}">.sentinel().of<row>();
for (const std::string& line : lines) rows.push_back(read_row(line));
```

Assigning the result of a scan converts it, and a conversion has nowhere to
put a failure but an exception. `of<T>()` is that conversion under another
name; `try_of<T>()` hands back `std::expected<T, scan_error>` instead.
`scan_prefix` has `take<T>()` and `try_take<T>()`, which give the value and
the rest of the subject.

**`past_space`** says once what `{*\s*}` before every place says over and over:
every place begins past whatever whitespace is in front of it, which is what
`%d` does in a `scanf` format and `{}` does not.

```cpp
scan::scan<f>.past_space()(text)
constexpr auto stamp = scan::fixed_string("{}-{}-{}T{}:{}:{}").past_space();
```

### The types a place can be

* anything with a `scan::scanner<T>`: the integers, the floating-point types,
  `bool`, `char`, `std::string`, `std::string_view`, and `scan::held<N>` --
  characters in room said in advance, for a reading with no allocator;
* an aggregate, whose fields are the places inside a nested `{…}`;
* a sum type, written as branches -- `std::variant`, or anything that answers
  the three questions a sum is asked (see below);
* a range, written with a repetition, which takes as many turns as the subject
  affords.

A type that is both -- a range with a scanner of its own, `std::string` being
the one everybody means -- is read as one value, because that is what a
`std::string` field is for. Where it should be the other way round, the type
says so:

```cpp
template <> struct scan::scanner<packet_bytes> {
  static constexpr bool as_a_list = true;   // read as many as there are
  …
};
```

### How a type says it can be read

`scan::scanner<T>` is the whole of it, and there are three shapes it can take.

**A leaf** -- a value read out of the text of one place:

```cpp
template <>
struct scan::scanner<weight> {
  // What the place matches when the format does not say. Either a member or a
  // function; and taking the parameters written after the colon, if it wants
  // them.
  static constexpr std::string_view pattern() { return "[0-9]+(?:\\.[0-9]+)?"; }
  static constexpr auto pattern(std::string_view parameters);

  // From the text of the place. Again, with the parameters if it wants them.
  static constexpr weight parse(std::string_view text);
  static constexpr weight parse(std::string_view text, std::string_view parameters);

  // And for a subject that arrives a character at a time and cannot be gone
  // back over: make a state, take the characters, then make the value.
  static constexpr state begin();
  static constexpr state begin(std::string_view parameters);
  static constexpr void push(state&, char);
  static constexpr weight finish(state);
};
```

`parse` alone is enough for a subject held in memory. `begin`/`push`/`finish`
are what a one-pass reading uses, and a type that has them can be a field of a
record read off a stream. This is what the library's own scanners look like:
the integers take `{:x}`, `{:#}` and a width through `parameters`, and gather
digits through `push` so that a number can be read off a socket.

**A shape** -- a type that says a whole format rather than a pattern, whose
places are its own fields:

```cpp
template <>
struct scan::scanner<point> : scan::aggregate_scanner<"({}, {})"> {};

struct line { point from; point to; };
const line one = scan::scan<"{} -> {}">("(1, 2) -> (3, 4)");
```

The places inside `point`'s format mean `point`'s fields, wherever it is used.
Nothing is read twice: the outer pattern and the inner one are one automaton,
and `point` is built from the groups it already found.

**A shape that is built rather than filled** -- the same, with a `parse` taking
the places as arguments:

```cpp
template <>
struct scan::scanner<angle> : scan::aggregate_scanner<"{}deg{}min"> {
  static constexpr angle parse(int degrees, int minutes) {
    return angle(degrees * 60 + minutes);   // invariants kept, members private
  }
};
```

The places then stand for the arguments of that call rather than for the
fields of the type, so the type need not be an aggregate at all: it may have
invariants, private members, or an order of its own that has nothing to do
with the order the format is written in.

**A sum of your own** -- `std::variant` is the one everybody has, and nothing
about a sum is peculiar to it. Three questions are asked of one, and a type
that answers them is read as a sum:

```cpp
template <>
struct scan::branches<either_word_or_number> {
  static constexpr std::size_t count = 2;              // how many alternatives
  template <std::size_t which>                          // which type the k-th is
  using at = std::conditional_t<which == 0, word, number>;
  template <std::size_t which, class value>             // and how to make it
  static constexpr either_word_or_number make(value&& one) { … }
};
```

**A list** -- a field that is a range takes as many turns as the subject
affords, and the place says what one turn looks like:

```cpp
struct row { std::vector<int> values; };
const row one = scan::scan<"{{}{*,?}}">("1,2,3,4");
```

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
fields it filled and leaves the rest as they were; an integer too big for its
type is an error rather than undefined behaviour; and nothing is
locale-dependent.

**From Perl and RE2, in one corner.** A quantifier around something that can
match nothing is where every engine answers differently, including from each
other. `([ab]*?)*` against "ba", anchored:

| | group 1 |
| --- | --- |
| Perl | `[2,2)` |
| Python | `[2,2)` |
| this library | `[1,2)` |
| RE2 | `[0,2)` |

Following the order a backtracking engine tries things in gives this
library's answer: the loop takes `b`, then takes `a`, and a third turn would
match nothing. Perl divides it the same way and differs only by taking that
last empty turn and leaving the group there. RE2 divides it differently from
both. This is written down rather than chased.

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

Determinization stops at twenty thousand states and says so. Determinizing
costs exponentially more states than an expression has symbols for expressions
that are perfectly ordinary -- anything that reads freely and then counts,
`.*a.{20}` and its like -- and where that happens here it is not a slow
program but a compilation nobody waits for.

## Speed

Measured against `re2c`, which generates a scanner from a pattern in a
separate build step, and against `sscanf`. Take the numbers as shapes rather
than as decimals; the methodology matters more.

For a fixed-length pattern with a terminator and no length to check
(`scan::match<p>.sentinel().scalar()`), the code generated for
`[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}` is **71 instructions
with no calls, which is what re2c generates for the same pattern, instruction
for instruction**. The difference in the ordinary form is three instructions:
the length checks re2c does not have, because it is given a pointer and a
terminator rather than a range.

Every benchmark asks its question of four engines -- this library, CTRE, RE2
and re2c -- on the same subjects. What each of them is given differs, and the
differences are the point:

| | pattern known | given | hands back |
| --- | --- | --- | --- |
| this library | while compiling | a range, or a pointer and a terminator | views into the subject |
| CTRE | while compiling | a range | views |
| RE2 | while running | a range | views, after a dispatch inside its walk |
| re2c | ahead of time, by a generator | a pointer and a terminator, no length | pointers |

Against `sscanf`, on the same work -- the same characters in, the same
integers out:

| | `sscanf` | here |
| --- | --- | --- |
| two numbers | 109 ns | 20 ns |
| a timestamp of six | 207 ns | 64 ns |
| five words into buffers | 299 ns | 75 ns |
| five words into views | 299 ns | 39 ns |

The third row is the honest pair: both copy each field into room said in
advance. The fourth is a thing `sscanf` cannot do at all. What the numbers do
not show is where its time goes: the format is a string it parses again on
every call.

## What this is built on

The machine is a tagged deterministic finite automaton, and the construction
is the one described in these papers:

* Ville Laurikari, *NFAs with Tagged Transitions, their Conversion to
  Deterministic Automata and Application to Regular Expressions* (2000) -- tags
  on transitions, and the idea of determinizing them.
* Ulya Trofimovich, *[Tagged Deterministic Finite Automata with
  Lookahead](https://arxiv.org/abs/1907.08837)* (2019) -- TDFA(1): holding tags
  back to the next symbol, which is what makes a field cost one write instead
  of one per character.
* Angelo Borsotti and Ulya Trofimovich, *[A closer look at
  TDFA](https://arxiv.org/abs/2206.01398)* (2022) -- the algorithm in full,
  with the register operations, the fallback registers, and the
  optimizations.

Two things here are not from those papers.

The **disambiguation policy** is leftmost-first, which is Perl's rule, RE2's
and CTRE's. The papers implement POSIX and what they call leftmost greedy --
which is longest-prefix-then-leftmost-path, a lexer's rule, and a different
thing from what is called leftmost-first here. The mechanism that makes the
difference is the cut: where a walk in a state has matched, every walk below
it in precedence has lost and is removed, so a state whose match is first has
no transitions at all. That is the rule a Pike VM applies by killing
lower-priority threads at a Match instruction, and it is described in Russ
Cox's writing on RE2.

The **format layer** -- reading a format against the type it scans into,
building the value where the match ends, and reading records one after another
off a subject that arrives as it is read -- has no paper behind it.

## Tests and fuzzing

Around eighty test files, each holding one or two patterns, because compiling
a pattern is a constant evaluation and a translation unit holding ten of them
costs ten times as much whenever one is touched.

Two things are compared against something outside this library:

* **A differential fuzzer against RE2.** Everything that happens while a
  pattern is compiled is ordinary code that also runs, so the fuzzer builds
  machines from patterns made up at run time and compares the answers --
  matched or not, where a head ended, and where every group began and ended --
  against RE2, whose default rule is the same leftmost-first. It found a real
  fault within a minute of its first run: `a*?` was being read as `a*` followed
  by a literal question mark.

  ```sh
  cmake -B build -DSCAN_BUILD_FUZZER=ON && cmake --build build --target differential_fuzz
  ./build/differential_fuzz --seed 1 --rounds 200000
  ```

* **The walk against the interpreter.** The fuzzer cannot reach the walk
  itself, which is written out state by state and exists only where something
  is compiled. So a test compares it against the interpreter over an automaton
  built from the same pattern by the same code, on every subject up to four
  characters.

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

The library is a module graph -- `scan.core`, `scan.tre`, `scan.views`,
`scan.compiler`, `scan.runtime`, `scan.range`, `scan.scanners` -- with `scan`
as an umbrella that re-exports it. Importing `scan` is all that is wanted.
