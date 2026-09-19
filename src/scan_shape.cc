// The shape layer, as the rest of the library asks for it.
//
// The parts above are where the work is; this is the reading itself -- a whole
// subject, a subject in pieces, a subject read once -- and `aggregate_scanner`,
// which is how a type says its places are a format of its own.

export module scan.shape;

import std;
import scan.tre;
export import scan.compiler;
export import scan.runtime;
export import scan.shape.values;

#if defined(_MSC_VER)
#define SCAN_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define SCAN_FORCE_INLINE [[gnu::always_inline]] inline
#else
#define SCAN_FORCE_INLINE inline
#endif

export namespace scan::detail {
template <class Type, fixed_string Format, class SourceType>
struct taken_from_pieces {
  std::optional<Type> value;
  bool matched = false;
};

// How much a reading of this format has to carry between one match and the
// next, where the subject arrives in pieces. The same number the stream
// reading asks for: the longest walk out of a match that finds no other one.
template <class Type, fixed_string Format>
inline constexpr std::size_t pieces_hold = [] consteval {
  constexpr std::size_t window =
      walk_past_a_match<streaming_automaton<Type, Format>>();
  if constexpr (window == std::numeric_limits<std::size_t>::max()) {
    return std::size_t{0};
  } else {
    return window * 2 + 1;
  }
}();

template <class Type, fixed_string Format, class SourceType>
[[nodiscard]] constexpr auto take_from_pieces(SourceType& into,
                                              const char*& cursor,
                                              const char*& last,
                                              std::ptrdiff_t& place) {
  constexpr const auto& automaton = streaming_automaton<Type, Format>;
  taken_from_pieces<Type, Format, SourceType> said;
  register_file<std::ptrdiff_t, automaton.register_count> registers{};
  registers.fill(scan::tre::negative_tag);
  execute_initial<automaton>(registers, place);
  // The same note as everywhere else, and here it costs nothing to go back to:
  // the place is an address inside a piece the reading is still holding.
  constexpr bool walks_past = walk_past_a_match<automaton>() != 0;
  using kept_type =
      std::conditional_t<walks_past,
                         register_file<std::ptrdiff_t,
                                       automaton.register_count>,
                         nothing_kept>;
  walk_answer<const char*, kept_type> best;
  constexpr walk_shape shape{.in_words = true, .longest = true};
  into.watch(best.at, best.upto);
  if (!run_continuation<automaton, shape, automaton.initial, shape.budget, 0,
                        std::ptrdiff_t>(cursor, last, place, registers, into,
                                        best)) {
    return said;
  }
  // Where the match ended is where the next reading begins. Which characters
  // those are is the source's business: the piece they were in may have been
  // given up while the walk read past them, and then they were carried.
  into.go_back_to(cursor, last);
  auto got = into.taken();
  if (!got) return said;
  said.value = std::move(*got);
  said.matched = true;
  return said;
}

// A scan of input that arrives in pieces.
//
// The walk reads a piece the way it reads a string -- in words and vectors --
// and asks for the next one where it runs out. Nothing is buffered: what a
// field gathers, it gathers as the characters go by, and a piece is not looked
// at again once the walk has left it.
template <class Type, fixed_string Format,
          class CarrierType = scan::default_context_t,
          piecewise_char_range PiecesType>
[[nodiscard]] constexpr std::expected<Type, failure_for<Type>> scan_pieces(
    PiecesType&& pieces, const CarrierType& told = CarrierType{}) {
  constexpr const auto& automaton = streaming_automaton<Type, Format>;
  register_file<std::ptrdiff_t, automaton.register_count> registers{};
  registers.fill(scan::tre::negative_tag);
  execute_initial<automaton>(registers, std::ptrdiff_t{0});
  auto view = std::views::all(std::forward<PiecesType>(pieces));
  typename field_gatherer<Type, Format, automaton, false, std::ptrdiff_t,
                          CarrierType>::cold_type collected =
      made_cold_at_places<
          Type, Format, std::ptrdiff_t,
          typename field_gatherer<Type, Format, automaton, false,
                                  std::ptrdiff_t, CarrierType>::cold_type>(told);
  gathers_from_pieces<field_gatherer<Type, Format, automaton, false,
                                     std::ptrdiff_t, CarrierType>,
                      decltype(view),
                      pieces_hold<Type, Format>>
      into(field_gatherer<Type, Format, automaton, false, std::ptrdiff_t,
                          CarrierType>{collected, told},
           std::move(view));
  // Nothing in hand to begin with, so the first thing the walk does is ask.
  const char* cursor = nullptr;
  const char* last = nullptr;
  std::ptrdiff_t place = 0;
  walk_answer<const char*> best;
  constexpr walk_shape shape{.in_words = true,
                             .budget = bodies_worth_writing<automaton>()};
  if (!run_continuation<automaton, shape, automaton.initial, shape.budget, 0,
                        std::ptrdiff_t>(cursor, last, place, registers, into,
                                        best)) {
    return std::unexpected(scan::as_a_failure<failure_for<Type>>(
        no_match<>("input does not match scan expression")));
  }
  return into.taken();
}

template <class Type, fixed_string Format,
          how_to_walk Walk = how_to_walk::by_length,
          class CarrierType = scan::default_context_t,
          std::ranges::input_range RangeType>
[[nodiscard]] constexpr std::expected<Type, failure_for<Type>> scan_stream(
    RangeType&& input, const CarrierType& told = CarrierType{}) {
  // The whole of the reading, so the walks below a match are kept.
  constexpr const auto& automaton = streaming_automaton<Type, Format, false>;
  // Positions as addresses where the subject lies in a row.
  //
  // An address is the cursor, which the walk is holding anyway; a count is a
  // step of its own on every character, and a cursor that is a base and an
  // index rather than one pointer. Where the subject arrives a character at a
  // time there is nothing to point at and the count is what there is.
  constexpr bool in_a_row = std::ranges::contiguous_range<RangeType>;
  using mark_kind = std::conditional_t<in_a_row, const char*, std::ptrdiff_t>;
  // Written out, the same as every other walk. A subject handed over a
  // character at a time is read by the machine written as code -- what it
  // cannot have is the vectors, because there is nothing in a row to read.
  //
  // Where there is, it can. The characters of a run keep the machine where it
  // stands and write nothing, so the walk steps over the whole run at once and
  // hands it to whoever is gathering as a run -- which is one call for a
  // hundred characters where the type can take one. That wants addresses
  // rather than iterators, which is what a range in a row has.
  // A list is left out of it: its elements are handed over turn by turn and a
  // run stepped over in one go is one turn as far as the walk can tell.
  if constexpr (std::ranges::contiguous_range<RangeType>) {
    // Where the reading holds a list, its elements are turns: the runs cannot
    // be stepped over whole -- a run stepped over in one go is one turn as far
    // as the walk can tell -- and there is nothing to point at through them.
    // Everything else about the two walks is the same, and they were written
    // out twice until one of the two went without the contexts the caller
    // said, which is a thing a second copy of an argument list will do.
    constexpr bool points_at_it = !holds_a_range<Type>();
    const char* cursor = std::ranges::data(input);
    const char* const last = cursor + std::ranges::size(input);
    // Runs stepped over whole, unless the caller asked for a character at a
    // time.
    //
    // Only the one walk the caller will use is written. The other paths write
    // both and pick by the length of the subject, which they can afford
    // because a walk to a terminator is a state and a comparison; a walk that
    // gathers is a body a state, and two of them is twice the code for a
    // question that a subject of any length answers the same way.
    constexpr walk_shape shape =
        points_at_it
            ? walk_shape{
                  .in_words = Walk != how_to_walk::one_at_a_time,
                  .tags_read =
                      groups_whose_place_is_read<Type, Format, automaton>(),
                  .tags_written =
                      groups_whose_mark_is_read<Type, Format, automaton>(),
                  .budget = bodies_worth_writing<automaton>()}
            : walk_shape{.budget = bodies_worth_writing<automaton>()};
    // Nothing the reading fills in is made here.
    //
    // A walk written as labels is a walk no inliner will fold into this one,
    // so anything handed to it by reference is an address a call has seen and
    // must stay in memory until the call returns -- which is every character
    // of the subject. Told to make its own instead, the gatherer and the
    // registers are values of the walk and go wherever values go.
    return run_owning<automaton, shape, automaton.initial, shape.budget, 0,
                      const char*, points_at_it, const char*, const char*,
                      automaton.register_count,
                      field_gatherer<Type, Format, automaton, in_a_row,
                                     mark_kind, CarrierType>,
                      walk_answer<const char*>>(
        cursor, last, points_at_it ? cursor : nullptr,
        points_at_it ? cursor : mark_kind{}, {}, told);
  } else {
    register_file<mark_kind, automaton.register_count> registers{};
    if constexpr (in_a_row) {
      registers.fill(nullptr);
      execute_initial<automaton>(registers, static_cast<const char*>(nullptr));
    } else {
      registers.fill(scan::tre::negative_tag);
      execute_initial<automaton>(registers, std::ptrdiff_t{0});
    }
    using gatherer_type =
        field_gatherer<Type, Format, automaton, in_a_row, mark_kind, CarrierType>;
    typename gatherer_type::cold_type collected =
        made_cold_at_places<Type, Format, mark_kind,
                            typename gatherer_type::cold_type>(told);
    gatherer_type into{collected, told};
    auto cursor = std::ranges::begin(input);
    mark_kind position = 0;
    constexpr walk_shape shape{.budget = bodies_worth_writing<automaton>()};
    walk_answer<decltype(cursor)> best;
    if (!run_continuation<automaton, shape, automaton.initial, shape.budget, 0,
                          mark_kind>(cursor, std::ranges::end(input), position,
                                     registers, into, best)) {
      return std::unexpected(scan::as_a_failure<failure_for<Type>>(
          no_match<>("input does not match scan expression")));
    }
    return into.taken();
  }
}

// The head of a range that is read once, and the character that ended it.
//
// Nothing is buffered: the characters go through the machine as they come, the
// values are gathered by the scanners of the fields themselves, and the one
// character the machine could not take is handed back with them, because it has
// been read and cannot be put back where it came from.
template <class Type, std::size_t Hold = 0>
struct taken_ahead {
  Type value;
  // The character that ended the match: offered, looked at, and left where it
  // stood. One is enough, because only one is ever looked at.
  std::optional<char> stopped;
  // The characters read after the match on the chance of a longer one, where
  // the walk went past it and then died. Those were taken out of the reading
  // and have to come back somewhere: a reading that goes on holds them for the
  // next match, and a single head hands them here. Room for as many as the
  // machine can read past a match, which is a number it is asked for while
  // this is compiled.
  std::array<char, Hold == 0 ? 1 : Hold> given{};
  std::size_t count = 0;

  // What was given back, as text. A view of what this holds, so it lives as
  // long as this does.
  [[nodiscard]] constexpr std::string_view given_back() const {
    return std::string_view(given.data(), count);
  }
};

// What a reading holds between one match and the next.
//
// A walk can go past a match and die away from one, and then the answer is the
// place it passed -- which means the characters it read after that place have
// to be read again by whoever reads next. There is nowhere to put them back
// on a subject that is gone once it is read, so they are kept here, in front
// of the reading, and taken before anything else.
//
// How many there can be is what the automaton says: the longest walk out of a
// match that finds no other match. Where that is nothing, this is nothing.
template <std::size_t Hold>
struct stream_carry {
  std::array<char, Hold == 0 ? 1 : Hold> held{};
  std::size_t count = 0;
  std::size_t at = 0;

  [[nodiscard]] constexpr bool empty() const { return at == count; }
  [[nodiscard]] constexpr char front() const { return held[at]; }
  constexpr void pop() { ++at; }

  // In front of whatever is still unread here, because they were read first.
  constexpr void put_in_front(const char* from, std::size_t many) {
    if (many == 0) return;
    const std::size_t left = count - at;
    std::array<char, Hold == 0 ? 1 : Hold> made{};
    for (std::size_t index = 0; index < many; ++index) made[index] = from[index];
    for (std::size_t index = 0; index < left; ++index) {
      made[many + index] = held[at + index];
    }
    held = made;
    count = many + left;
    at = 0;
  }
};

// The head of a subject read as it arrives, and where it ended.
//
// The machine is offered the character before it is taken out of the reading,
// so the one that ends a match is never lost -- it is looked at, reported, and
// left where it is. What has to be given back is the other thing: the
// characters read after the last match on the chance of a longer one, where
// the walk went past a match and then died. Those were taken, and they go into
// the carry for the next reading.
//
// A reading that can be gone back over needs neither: the place is an iterator
// and going back is assigning it. That is why a forward range holds nothing at
// all here, and why the only reading that has to name a number is the one that
// cannot be gone back over.
template <class Type, fixed_string Format, class IteratorType,
          class SentinelType, std::size_t Hold>
[[nodiscard]] constexpr std::expected<taken_ahead<Type>, failure_for<Type>>
scan_stream_prefix(IteratorType& first, SentinelType last,
                   stream_carry<Hold>& carry, bool read_on = true) {
  constexpr const auto& automaton = streaming_automaton<Type, Format>;
  constexpr std::size_t window = walk_past_a_match<automaton>();
  constexpr bool can_go_back = std::forward_iterator<IteratorType>;
  static_assert(
      can_go_back || window != std::numeric_limits<std::size_t>::max(),
      "this pattern can read any number of characters past a match without "
      "finding another one, so the reading that has to give them back would "
      "have to hold any number of them: read it from something that can be "
      "gone back over -- a forward range, characters in a row, or input in "
      "pieces");
  stream_state<Type, Format> state;
  std::optional<char> stopped;
  std::optional<stream_state<Type, Format>> note;
  // The place the note was taken at, kept the way this reading can keep it: an
  // iterator where the reading can be gone back over, and nothing at all where
  // it cannot -- an iterator of such a range cannot even be copied.
  using place_type =
      std::conditional_t<can_go_back, IteratorType, nothing_kept>;
  place_type note_at{};
  constexpr std::size_t held_here =
      can_go_back || window == 0 ||
              window == std::numeric_limits<std::size_t>::max()
          ? 1
          : window;
  std::array<char, held_here> since{};
  std::size_t since_count = 0;
  if (state.accepting()) note = state;
  // A character that has been taken but not stepped over yet.
  //
  // Stepping over one reads the next: an iterator of a subject that arrives as
  // it is read does its reading in `++`. So the step is put off until another
  // character is actually wanted, and a match that settles where it stands
  // never causes the one after it to be read at all. What is read is what the
  // machine asked for, and nothing beyond it.
  bool taken_here = false;
  const auto step_over_it = [&] {
    if (!taken_here) return;
    ++first;
    taken_here = false;
  };
  while (true) {
    char symbol = 0;
    if (!carry.empty()) {
      symbol = carry.front();
    } else {
      step_over_it();
      if (first == last) break;
      symbol = static_cast<char>(*first);
    }
    if (!state.offer(symbol)) {
      // Looked at and not taken: it stays where it is, and is said here so
      // that whoever asked knows what ended the match.
      stopped = symbol;
      break;
    }
    if (!carry.empty()) {
      carry.pop();
    } else {
      taken_here = true;
    }
    if constexpr (!can_go_back && window != 0) since[since_count++] = symbol;
    if (state.accepting()) {
      note = state;
      since_count = 0;
      if constexpr (can_go_back) note_at = first;
      // Where the machine can go nowhere from where it stands, it is over, and
      // nothing needs to be read to find that out.
      if (state.settled()) break;
    }
  }
  const auto handed_back =
      [](std::expected<Type, failure_for<Type>> got,
         std::optional<char> ended_it)
      -> std::expected<taken_ahead<Type>, failure_for<Type>> {
    if (!got) return std::unexpected(std::move(got).error());
    return taken_ahead<Type>{std::move(*got), ended_it};
  };
  // Whoever reads on from here needs the reading to stand after what was
  // taken; whoever does not would only make it read one more character.
  if (read_on) step_over_it();
  if (state.accepting()) return handed_back(std::move(state).finish(), stopped);
  if (note) {
    // Past the match and dead. The answer is the place that was kept, and what
    // was read after it goes back in front of the reading.
    if constexpr (can_go_back) {
      first = note_at;
    } else {
      carry.put_in_front(since.data(), since_count);
    }
    // And the one that ended it, where one was looked at: going past a match
    // and dying does not make the character that stopped the walk any less
    // read. Thrown away here, it was the one thing the caller could not get
    // back by any other means.
    return handed_back(std::move(*note).finish(), stopped);
  }
  // Nothing matched; `finish` says so in the way the caller expects.
  return handed_back(std::move(state).finish(), stopped);
}

// How much a reading of this format has to be able to hold: nothing where it
// can be gone back over, and nothing where no walk out of a match ever fails
// to find another. Otherwise the characters read past a match, and the ones
// already held when that happened.
template <class Type, fixed_string Format, class IteratorType>
inline constexpr std::size_t stream_hold = [] consteval {
  if constexpr (std::forward_iterator<IteratorType>) {
    return std::size_t{0};
  } else {
    constexpr std::size_t window =
        walk_past_a_match<streaming_automaton_whole<Type, Format>>();
    if constexpr (window == std::numeric_limits<std::size_t>::max()) {
      // Refused where it is used; sized so that saying so is what the caller
      // sees, rather than an array of every address there is.
      return std::size_t{0};
    } else {
      return window * 2;
    }
  }
}();

template <class Type, fixed_string Format, class IteratorType>
using stream_carry_for = stream_carry<stream_hold<Type, Format, IteratorType>>;

template <class Type, fixed_string Format, std::ranges::input_range RangeType>
[[nodiscard]] constexpr auto scan_stream_prefix(RangeType&& input) {
  auto first = std::ranges::begin(input);
  // One head and no reading after it, so what the walk read past the match has
  // nowhere to go: the carry below goes out of scope with this call. A reading
  // that goes on -- one record after another -- holds it between matches and
  // reads them again. A single head has no next reading, so they are handed to
  // the caller instead, and nothing is eaten.
  constexpr std::size_t hold = stream_hold<Type, Format, decltype(first)>;
  stream_carry<hold> carry;
  auto got = scan_stream_prefix<Type, Format>(first, std::ranges::end(input),
                                              carry, false);
  using answer = taken_ahead<Type, hold>;
  if (!got) {
    return std::expected<answer, failure_for<Type>>(
        std::unexpected(std::move(got).error()));
  }
  answer made{std::move(got->value), got->stopped, {}, 0};
  while (!carry.empty()) {
    made.given[made.count++] = carry.front();
    carry.pop();
  }
  return std::expected<answer, failure_for<Type>>(std::move(made));
}



}  // namespace scan::detail

// The helper that reads a shape, which is ordinary code and says so.
//
// It is here rather than higher up because the reading it does is the reading
// everything else does -- one builder, one gathering, one fold -- and a
// consumer that writes its own says the same things through the same hooks.
// Nothing below this line knows that a type has fields; this is where that
// knowledge lives.
export namespace scan {




template <fixed_string Format>
struct aggregate_scanner {
  // Said outright: the places of this type are a format of its own, so a
  // context said at a place of this type can be said to its places one by one.
  // Nothing else in the library answers this, and a scanner that folds its own
  // groups does not -- its groups are its own business and a context said at it
  // is said to it whole.
  static constexpr bool says_a_format = true;

  // The shape read out of groups that this format made, for a type that was
  // named rather than inherited from.
  //
  // A caller writing `scan<"{},{}">.of<point>()` says the format at the call
  // and the type at the call, and `point` may have no scanner at all. What
  // reads it is this, asked for both: the library below hands over the groups
  // and asks nothing about what a point is made of.
  template <class Type, class CarrierType = scan::no_contexts>
  [[nodiscard]] static constexpr auto read(
      std::span<const std::string_view> groups,
      const CarrierType& given = CarrierType{})
      -> std::expected<Type, detail::failure_for<Type>> {
    return detail::build_value<detail::failure_for<Type>,
                               detail::format_parameters<Type, Format>, Type, 0,
                               true, scan::hands_a_failure_back>(groups, given);
  }

  // The same, where the caller asked for the value itself: what went wrong is
  // thrown at the asking, which is the only place anything is thrown.
  template <class Type, class CarrierType = scan::no_contexts>
  [[nodiscard]] static constexpr Type read_or_throw(
      std::span<const std::string_view> groups,
      const CarrierType& given = CarrierType{}) {
    return detail::build_value<detail::failure_for<Type>,
                               detail::format_parameters<Type, Format>, Type, 0,
                               true, scan::throws_a_failure>(groups, given);
  }

  // Nothing here is a member the library reads and reacts to. What this class
  // does, it does through the hooks any scanner may write: the pattern its
  // places make, the building from the groups that pattern opens, and the
  // telling of those groups as they arrive. The format is a parameter of this
  // class and is spoken by nobody else -- the library below knows patterns,
  // groups and hooks, and has never heard of a format.

  [[nodiscard]] constexpr auto pattern(this const auto& self) {
    using type = scanner_target_t<decltype(self)>;
    // What this shape matches, with its places as groups and in the order the
    // format has them -- the very order the reading below counts on, because
    // both come out of the one walk over the format.
    return detail::places_pattern<type, Format>();
  }

  // Whether this shape is read from its own groups, said outright.
  //
  // Asked as a question and not found out by whether the hook below is there:
  // what that hook hands back is the list of everything reading this shape can
  // fail with, and working that list out means knowing how the shape is read,
  // which is what the question decides.
  //
  // A shape made only of places can be handed its groups when the match is
  // over. One with a list in it is made of turns, and the positions a match
  // leaves behind hold the last turn and nothing before it -- so it has to be
  // told its groups as they happen, which every place of it has to be able to
  // take. Where neither is true, the shape keeps the road that spreads its
  // places into the automaton around it.
  [[nodiscard]] constexpr bool reads_its_groups(this const auto& self) {
    using type = scanner_target_t<decltype(self)>;
    static_cast<void>(self);
    return true;
  }

  // The shape, out of the groups its pattern opened.
  //
  // Its groups are its places, so what builds it from them is what builds it
  // from the places of the reading around it: one builder, and this is a call
  // to it.
  template <class SelfType>
    requires(!detail::says_a_list_inside<scanner_target_t<SelfType>>())
  [[nodiscard]] constexpr auto from_groups(
      this const SelfType& self, std::span<const std::string_view> groups)
      -> std::expected<scanner_target_t<SelfType>,
                       detail::shape_failure<scanner_target_t<SelfType>>> {
    using type = scanner_target_t<SelfType>;
    static_cast<void>(self);
    return detail::build_value<detail::shape_failure<type>,
                               detail::format_parameters<type, Format>, type, 0,
                               true>(groups);
  }

  // The same, told what the place this shape stands at was told. A shape
  // standing inside another shape is read by the same builder, so what reaches
  // it here reaches its places the way it reaches everything else.
  template <class SelfType, class CarrierType>
    requires(!detail::says_a_list_inside<scanner_target_t<SelfType>>())
  [[nodiscard]] SCAN_FORCE_INLINE constexpr auto from_groups(
      this const SelfType& self, std::span<const std::string_view> groups,
      const CarrierType& told)
      -> std::expected<scanner_target_t<SelfType>,
                       detail::shape_failure<scanner_target_t<SelfType>>> {
    using type = scanner_target_t<SelfType>;
    static_cast<void>(self);
    return detail::build_value<detail::shape_failure<type>,
                               detail::format_parameters<type, Format>, type, 0,
                               true>(groups, told);
  }

  // Told its groups as they happen.
  //
  // Every shape that can be says this, not only one made of turns: a subject
  // that is read once has nothing to point at, and a shape standing inside a
  // shape that is being told its groups has to be told its own. Where the
  // groups can be handed over instead, they are -- that is the faster of the
  // two and the one that copies nothing.
  //
  // Every place gathers into the
  // reader of the value it stands for, a turn ends where the list's own group
  // closes, and the element is put together there and added -- by the same
  // builder that puts together everything else, asked for the gatherings a
  // different way.
  template <class SelfType, class Told>
    requires(detail::turns_can_be_folded<scanner_target_t<SelfType>, Format>())
  [[nodiscard]] constexpr auto begin_groups(this const SelfType& self,
                                            const Told& given) {
    static_cast<void>(self);
    return detail::shape_turns<scanner_target_t<SelfType>, Format, Told>(
        given);
  }

  template <class SelfType>
    requires(detail::turns_can_be_folded<scanner_target_t<SelfType>, Format>())
  [[nodiscard]] constexpr auto begin_groups(this const SelfType& self) {
    static_cast<void>(self);
    return detail::shape_turns<scanner_target_t<SelfType>, Format>{};
  }

  template <class SelfType, std::size_t Place, class StateType>
    requires(detail::turns_can_be_folded<scanner_target_t<SelfType>, Format>())
  constexpr void push_group(this const SelfType& self, StateType& state,
                            scan::group_at<Place>, char letter) {
    static_cast<void>(self);
    detail::push_shape_place<scanner_target_t<SelfType>, Format, Place>(
        state, letter);
  }

  template <class SelfType, std::size_t Place, class StateType>
    requires(detail::turns_can_be_folded<scanner_target_t<SelfType>, Format>())
  constexpr void opened_group(this const SelfType& self, StateType& state,
                              scan::group_at<Place>) {
    static_cast<void>(self);
    detail::open_shape_place<scanner_target_t<SelfType>, Format, Place>(state);
  }

  template <class SelfType, std::size_t Place, class StateType>
    requires(detail::turns_can_be_folded<scanner_target_t<SelfType>, Format>())
  constexpr void closed_group(this const SelfType& self, StateType& state,
                              scan::group_at<Place>) {
    using type = scanner_target_t<SelfType>;
    static_cast<void>(self);
    detail::close_shape_place<type, Format, Place,
                              detail::shape_failure<type>>(state,
                                                           state.went_wrong);
  }

  template <class SelfType, class StateType>
    requires(detail::turns_can_be_folded<scanner_target_t<SelfType>, Format>())
  [[nodiscard]] constexpr auto finish_groups(this const SelfType& self,
                                                 StateType state) {
    using type = scanner_target_t<SelfType>;
    static_cast<void>(self);
    using failure_type = detail::shape_failure<type>;
    if (state.went_wrong) {
      return std::expected<type, failure_type>(
          std::unexpected(std::move(*state.went_wrong)));
    }
    // Told what the place this shape stands at was told. The state kept it
    // from the moment it was begun -- a shape standing inside another shape is
    // told at the door, the same as one standing on its own -- and its places
    // read with it, which is the whole of what a context said at a place
    // means.
    if constexpr (requires { state.told; }) {
      return detail::finish_value<type, type, 0, true, failure_type>(
          detail::gathered_by_a_fold<StateType>{state}, nullptr, state.told);
    } else {
      return detail::finish_value<type, type, 0, true, failure_type>(
          detail::gathered_by_a_fold<StateType>{state}, nullptr);
    }
  }

  // Gathered a character at a time, for whoever holds the characters and not
  // the subject.
  //
  // Said as a question rather than as a refusal inside it: whether a type
  // gathers this way is asked by things that are deciding how to read it, and
  // an answer that stops the compiler is not an answer. A shape made by the
  // call it named says no -- its places stand for that call's arguments, and
  // there is nothing to hand a half-read one to.
  //
  // The list of what can go wrong is built from this shape's parts and never
  // from the shape: what it says it hands back is that very list, and a list
  // that asked the shape would be asking its own answer.
  template <class SelfType>
    requires(!requires { &scanner<scanner_target_t<SelfType>>::parse; })
  [[nodiscard]] constexpr auto begin(this const SelfType& self) {
    using type = scanner_target_t<SelfType>;
    static_cast<void>(self);
    return detail::stream_state<type, Format, false,
                                detail::shape_failure<type>>{};
  }

  constexpr void push(this const auto&, auto& state, char value) {
    state.push(value);
  }

  // Handed back rather than thrown, both of them: this type is read by the
  // same machine everything else is, and that machine says what went wrong
  // instead of throwing it. Which means a shape used as a field of another
  // shape carries its kinds up into what that reading can fail with.
  [[nodiscard]] constexpr auto finish(this const auto& self, auto state) {
    static_cast<void>(self);
    return std::move(state).finish();
  }

  [[nodiscard]] constexpr auto try_parse(this const auto& self,
                                         std::string_view input) {
    auto state = self.begin();
    for (char value : input) { self.push(state, value); }
    return self.finish(std::move(state));
  }
};

}  // namespace scan

export namespace scan::detail {

#undef SCAN_FORCE_INLINE

}  // namespace scan::detail


export namespace scan {

// How many groups a type's own pattern opens.
//
// Said out loud because a type built from its groups may want to hand some of
// them on: a shape whose field is another shape has that field's groups inside
// its own, and it can only pass them along if it knows how many there are.
// What is counted is the pattern the type declares, which is the pattern the
// match was made with.
template <class Type>
[[nodiscard]] consteval std::size_t groups_in() {
  return detail::groups_a_leaf_opens<std::remove_cv_t<Type>>();
}

}  // namespace scan
