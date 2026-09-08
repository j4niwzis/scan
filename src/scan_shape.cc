// The shape layer: places, fields, gatherings, and the putting together of a
// value out of what a walk found.
//
// Above the walk and knowing nothing it does not ask for. What is below reads
// a pattern and says where its groups were; what is here decides what those
// groups mean to a type -- which of them is which place, what gathers each
// one, and how the value is made when the reading is over. The walk takes
// whoever is gathering as a parameter and never looks inside it, which is what
// lets this be a module of its own rather than a half of that one.
export module scan.shape;

import std;
import scan.tre;
export import scan.compiler;
export import scan.runtime;

export namespace scan::detail {

#if defined(_MSC_VER)
#define SCAN_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define SCAN_FORCE_INLINE [[gnu::always_inline]] inline
#else
#define SCAN_FORCE_INLINE inline
#endif

template <class type, std::size_t index>
using field_type = typename scan::fields<type>::template at<index>;

// Nothing is gathered at the place a leaf that reads its own groups stands on:
// what it is built from are its groups, and they are gathered each at its own.
struct no_gathering {};

// A fold, and what it has been told.
//
// The state is the type's own -- it says how it is made and what it holds. The
// two arrays beside it are the walk's bookkeeping: which turn of each group the
// type has been told about, and whether that turn is still open. They travel
// with the state, because a reading that divides carries its fold with it, and
// what one reading has been told the other has not.
//
// A turn is known by the position its group opened at. Positions only ever move
// forward, so a group whose opening has moved is a new turn and is announced;
// one whose opening stands still is the same turn going on.
template <class held>
struct fold_of {
  using held_type = std::remove_cv_t<held>;
  static constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
  using state_type = decltype(scan::scanner<held_type>{}.begin_groups());

  state_type state = scan::scanner<held_type>{}.begin_groups();
  std::array<std::ptrdiff_t, inside> told_at{};
  std::array<bool, inside> open{};
  // Whether it asked for something this subject cannot give: its groups whole,
  // off a reading with nothing to point at.
  bool wanted_a_subject = false;
  // The first character of the subject this walk started on, where there is
  // one to point at. Then a group that closes is handed whole, and the
  // characters are never handed over one at a time. Off a stream this stays
  // nothing, and the fold is told the characters instead.
  const char* text = nullptr;

  constexpr fold_of() { told_at.fill(-1); }
};

// One step of a fold: what happened to the groups inside a place, said to the
// type in the order it can make sense of.
//
// Opened first and in the order they are written, because a group that opens
// inside another opens after it. Then the character, to every group it is
// inside of. Then the closings, innermost first, because a group that opens
// inside another closes before it.
//
// Nothing here asks what step the walk is on. It asks the positions, which say
// everything: a group whose opening has moved has begun a turn, one whose
// closing has caught up with its opening has ended one. So the same step run
// twice tells nothing twice, and the same step run at the end of the input --
// where there is no character to hand over -- finishes what the characters
// left open.
template <std::size_t place, class held, class reading_type, class fold_type,
          std::size_t register_count>
constexpr void fold_one_step(
    fold_type& fold, const reading_type& reading,
    const std::array<std::ptrdiff_t, register_count>& registers, char symbol,
    bool hands_the_character) {
  using held_type = std::remove_cv_t<held>;
  constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
  const auto opening_of = [&](std::size_t which) {
    return registers[reading[(place + 1 + which) * 2]];
  };
  const auto closing_of = [&](std::size_t which) {
    return registers[reading[(place + 1 + which) * 2 + 1]];
  };
  [&]<std::size_t... which>(std::index_sequence<which...>) {
    ((void)[&] {
      const std::ptrdiff_t began = opening_of(which);
      if (began < 0 || fold.told_at[which] == began) return;
      open_one_group<held_type, which>(fold.state);
      fold.told_at[which] = began;
      fold.open[which] = true;
    }(), ...);
  }(std::make_index_sequence<inside>{});
  if (hands_the_character) {
    [&]<std::size_t... which>(std::index_sequence<which...>) {
      ((void)[&] {
        if constexpr (takes_group_characters<held_type, which,
                                             typename fold_type::state_type> &&
                      !takes_the_group_whole<
                          held_type, which,
                          typename fold_type::state_type>) {
          if (!fold.open[which]) return;
          if (closing_of(which) >= opening_of(which)) return;
          push_one_group<held_type, which>(fold.state, symbol);
        } else if constexpr (takes_group_characters<
                                 held_type, which,
                                 typename fold_type::state_type>) {
          // It would take the group whole, and will where there is something
          // to point at. Where there is not, the characters are all there is.
          if (fold.text != nullptr) return;
          if (!fold.open[which]) return;
          if (closing_of(which) >= opening_of(which)) return;
          push_one_group<held_type, which>(fold.state, symbol);
        }
      }(), ...);
    }(std::make_index_sequence<inside>{});
  }
  [&]<std::size_t... step>(std::index_sequence<step...>) {
    ((void)[&] {
      constexpr std::size_t which = inside - 1 - step;
      if (!fold.open[which]) return;
      const std::ptrdiff_t began = opening_of(which);
      const std::ptrdiff_t ended = closing_of(which);
      if (ended < began) return;
      if constexpr (takes_the_group_whole<held_type, which,
                                          typename fold_type::state_type>) {
        // The whole of what the group stood on, pointed at rather than copied.
        //
        // A position here is how many characters have been read and not the
        // index of one: the walk writes a tag with the count after the
        // character that wrote it. So what a group stood on begins one before
        // where its opening says.
        if (fold.text != nullptr) {
          close_one_group<held_type, which>(
              fold.state,
              std::string_view(fold.text + (began > 0 ? began - 1 : 0),
                               static_cast<std::size_t>(ended - began)));
          fold.open[which] = false;
          return;
        }
        if constexpr (!takes_group_characters<held_type, which,
                                              typename fold_type::state_type>) {
          // It takes its groups whole and nothing else, and there is nothing
          // here to point at. Holding the characters to hand them over at the
          // end would be a hold with no bound, so this reading cannot be had --
          // said here and handed back where the value would have been, because
          // a walk has nobody to say it to.
          fold.wanted_a_subject = true;
          fold.open[which] = false;
          return;
        }
      }
      close_one_group<held_type, which>(fold.state);
      fold.open[which] = false;
    }(), ...);
  }(std::make_index_sequence<inside>{});
}

// Whether every group of a fold would take its group whole. Then a run of
// characters the walk stepped over costs one step and not one a character:
// nothing is handed over, and what opened and what closed is the same at both
// ends of a run, because a run is where nothing is written.
template <class held, class state_type>
[[nodiscard]] consteval bool every_group_whole() {
  using held_type = std::remove_cv_t<held>;
  return []<std::size_t... which>(std::index_sequence<which...>) {
    return (true && ... &&
            takes_the_group_whole<held_type, which, state_type>);
  }(std::make_index_sequence<groups_a_leaf_opens<held_type>()>{});
}

// The fold of every reading that stands at this state, told the same step once.
//
// A fold lives at the register holding the place's opening, which is the
// discipline a list is gathered by: readings that share that register share
// what is gathered there, and where two readings would have to disagree the
// machine has already given them registers of their own.
template <std::size_t place, std::size_t slot, class held, auto& automaton,
          class states_type, std::size_t register_count>
constexpr void fold_the_readings(
    std::size_t state,
    const std::array<std::ptrdiff_t, register_count>& registers,
    states_type& states, char symbol, bool hands_the_character,
    const char* text) {
  const auto& entered = automaton.states[state];
  std::array<bool, register_count> told{};
  for (std::size_t reading = 0; reading < entered.reading_count; ++reading) {
    const std::uint32_t at = entered.readings[reading][place * 2];
    if (told[at] || registers[at] < 0) continue;
    told[at] = true;
    auto& folding = std::get<slot>(states[at]);
    // Said every step rather than once, because a fold is made where its place
    // opens and carried where a reading divides, and neither of those knows
    // what the walk is reading.
    folding.text = text;
    fold_one_step<place, held>(folding, entered.readings[reading], registers,
                               symbol, hands_the_character);
  }
}

// How one group is gathered, made once and asked at every place that gathers.
//
// A leaf that is built from the groups its own pattern opens is not handed the
// text it stands on, so its place gathers nothing and each of its groups
// gathers characters. Every other group is gathered by the reader of the type
// it holds, which is what it was before any of this.
template <class type, fixed_string format, std::size_t group>
struct gathering_of {
  using held_type = leaf_kind_of_output<type, group>;
  static constexpr bool by_groups = gathers_by_its_groups<held_type>;
  static constexpr bool folds = folds_by_turns<std::remove_cv_t<held_type>>;
  static constexpr bool the_place = by_groups && leaf_offset_of_output<type, group> == 0;
  static constexpr bool inside = by_groups && leaf_offset_of_output<type, group> != 0;

  [[nodiscard]] static constexpr auto begin(std::string_view parameters) {
    if constexpr (the_place && folds) {
      // The type's own state, and the walk's note of what it has been told.
      static_cast<void>(parameters);
      return fold_of<std::remove_cv_t<held_type>>{};
    } else if constexpr (inside && folds) {
      // Nothing: the characters and the edges of this group go to the fold,
      // which is kept at the place the group is inside of.
      static_cast<void>(parameters);
      return no_gathering{};
    } else if constexpr (the_place || inside) {
      // A leaf read from its groups after the match gathers nothing at all:
      // the positions say where each of its groups stood, and the subject is
      // still there to be pointed at -- which is why such a leaf is refused
      // where the subject is not.
      static_cast<void>(parameters);
      return no_gathering{};
    } else {
      static_assert(requires { scanner_begin<held_type>(parameters); },
                    "single-pass input requires incremental scan::scanner<T>");
      return scanner_begin<held_type>(parameters);
    }
  }

  template <class state_type>
  static constexpr void push(state_type& state, char letter) {
    if constexpr (the_place || inside) {
      // A fold is handed its characters by the group they fell in, which the
      // place does for all of its groups at once and in order; a leaf read
      // after the match is handed nothing at all.
      static_cast<void>(state);
      static_cast<void>(letter);
    } else {
      scanner_push<held_type>(state, letter);
    }
  }

  // The characters of a run the walk stepped over, together. What the step in
  // vectors saved was being given back a call at a time here.
  template <class state_type>
  static constexpr void push_run(state_type& state, const char* from,
                                 const char* to) {
    if constexpr (the_place || inside) {
      static_cast<void>(state);
      static_cast<void>(from);
      static_cast<void>(to);
    } else {
      scanner_push_run<held_type>(state, from, to);
    }
  }
};

// One gathering per value the pattern reads, not one per field of the output.
//
// They are the same thing only where every field is one place. A field that is
// itself a shape is several places, and its own reader would then have to be
// handed the text and made to find the same boundaries a second time -- with a
// pattern that is a copy of the one already running. The boundaries are known
// here: the machine has a group for each of them. So each value is gathered by
// the reader of the type that value is, and the output is put together from
// those afterwards, the same way it is put together from pieces of a subject
// that can be pointed at.
template <class type, fixed_string format, std::size_t... group>
[[nodiscard]] constexpr auto make_scanner_state(
    std::index_sequence<group...>) {
  static constexpr auto spread = spread_of<type, format>();
  // A group that stands for a list gathers the list itself, which needs no
  // reader: what goes into it are whole elements, put there as each one ends.
  const auto one = []<std::size_t which>() {
    using held_type = leaf_kind_of_output<type, which>;
    if constexpr (scanned_as_range<held_type>) {
      return held_type{};
    } else {
      return gathering_of<type, format, which>::begin(
          spread.parameters[which].view());
    }
  };
  return std::tuple{one.template operator()<group>()...};
}

template <class type, fixed_string format>
[[nodiscard]] constexpr auto make_scanner_state() {
  return make_scanner_state<type, format>(
      std::make_index_sequence<groups_of_output<type>()>{});
}

// One slot per kind of gathering, not one per group.
//
// The machine keeps a gathering for every register, and a register is made for
// one tag, which belongs to one group -- so all but one of the gatherings kept
// in a register are for groups that register can never hold. A set of every
// group in every register is what a row of five fields was paying a
// microsecond and a half a scan for: five strings a register, made at the head
// of the scan, copied wherever a reading divided, and thrown away at the end.
//
// So the set is by kind and not by group. Groups gathered the same way share a
// slot, because no two of them are ever gathered in one register at once, and
// each register is begun with the parameters of the group whose tag it holds.
template <class... kinds>
struct gathering_kinds {
  using as_a_tuple = std::tuple<kinds...>;
};

template <class list, class kind>
struct with_kind;

template <class... kinds, class kind>
struct with_kind<gathering_kinds<kinds...>, kind> {
  using result = std::conditional_t<(std::is_same_v<kind, kinds> || ...),
                                    gathering_kinds<kinds...>,
                                    gathering_kinds<kinds..., kind>>;
};

template <class list, class kind>
struct where_kind;

template <class kind>
struct where_kind<gathering_kinds<>, kind> {
  static constexpr std::size_t at = 0;
};

template <class first, class... rest, class kind>
struct where_kind<gathering_kinds<first, rest...>, kind> {
  static constexpr std::size_t at =
      std::is_same_v<first, kind>
          ? 0
          : 1 + where_kind<gathering_kinds<rest...>, kind>::at;
};

// What one group is gathered in, asked without asking for the others.
template <class type, fixed_string format, std::size_t group,
          bool a_list = scanned_as_range<leaf_kind_of_output<type, group>>>
struct gathering_state {
  using result = decltype(gathering_of<type, format, group>::begin(
      std::string_view{}));
};

template <class type, fixed_string format, std::size_t group>
struct gathering_state<type, format, group, true> {
  using result = std::remove_cv_t<leaf_kind_of_output<type, group>>;
};

template <class type, fixed_string format, class list, std::size_t group,
          std::size_t count>
struct kinds_from {
  using result = typename kinds_from<
      type, format,
      typename with_kind<list,
                         typename gathering_state<type, format, group>::result>::result,
      group + 1, count>::result;
};

template <class type, fixed_string format, class list, std::size_t count>
struct kinds_from<type, format, list, count, count> {
  using result = list;
};

template <class type, fixed_string format>
using gathering_kinds_of =
    typename kinds_from<type, format, gathering_kinds<>, 0,
                        groups_of_output<type>()>::result;

// What one register holds.
template <class type, fixed_string format>
using register_state = typename gathering_kinds_of<type, format>::as_a_tuple;

// Which slot of it a group is gathered in.
template <class type, fixed_string format, std::size_t group>
inline constexpr std::size_t gathering_slot =
    where_kind<gathering_kinds_of<type, format>,
               typename gathering_state<type, format, group>::result>::at;

// One gathering of every kind, each begun as the first group of that kind
// would begin it. Where two groups of a kind ask for different parameters, the
// one that is not first is begun again when its group opens, which is where
// every group but one begins in any case.
template <class type, fixed_string format>
[[nodiscard]] constexpr auto make_slots() {
  register_state<type, format> made{};
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    // Backwards, so that the first group of a kind is the one that is left.
    const auto one = [&]<std::size_t which>() {
      using held_type = leaf_kind_of_output<type, which>;
      if constexpr (scanned_as_range<held_type>) {
        std::get<gathering_slot<type, format, which>>(made) =
            std::remove_cv_t<held_type>{};
      } else {
        static constexpr auto spread = spread_of<type, format>();
        std::get<gathering_slot<type, format, which>>(made) =
            gathering_of<type, format, which>::begin(
                spread.parameters[which].view());
      }
    };
    (one.template operator()<groups_of_output<type>() - 1 - group>(), ...);
  }(std::make_index_sequence<groups_of_output<type>()>{});
  return made;
}

// Every register, begun.
//
// One gathering of each kind everywhere, and then the groups that are open
// from the very first character begun where their reading says they are kept:
// those never meet the command that begins a group, because they were opened
// before there was a character to move on.
template <class type, fixed_string format, auto& automaton>
[[nodiscard]] constexpr auto make_register_states() {
  std::array<register_state<type, format>, automaton.register_count> states{};
  std::ranges::fill(states, make_slots<type, format>());
  const auto& initial = automaton.states[automaton.initial];
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    ([&] {
      using held_type = leaf_kind_of_output<type, group>;
      for (std::size_t reading = 0; reading < initial.reading_count;
           ++reading) {
        const std::uint32_t at = initial.readings[reading][group * 2];
        if (at >= automaton.register_count) continue;
        if constexpr (scanned_as_range<held_type>) {
          std::get<gathering_slot<type, format, group>>(states[at]) =
              std::remove_cv_t<held_type>{};
        } else {
          static constexpr auto spread = spread_of<type, format>();
          std::get<gathering_slot<type, format, group>>(states[at]) =
              gathering_of<type, format, group>::begin(
                  spread.parameters[group].view());
        }
      }
    }(), ...);
  }(std::make_index_sequence<groups_of_output<type>()>{});
  return states;
}

// Following a reading instead of counting on the numbers.
//
// The machine stands in one state and in several readings of the input at once,
// and the registers are how the readings are kept apart. A field's text cannot
// be a piece of the subject here -- the subject is gone as it is read -- so it
// is gathered as it arrives, one gathering per register that holds the opening
// tag of that field. The gathering goes where the register goes: a command that
// copies a register copies it, a command that writes a fresh position starts it
// again.
//
// The order is the whole of the correctness. The positions are written first,
// then the gatherings are moved the way the positions moved, and only then does
// the character go in -- so the character that opens a field is inside it and
// the character that closes one is not, which is what the positions say once
// they have been written and cannot be asked before.
//
// Which register holds which tag, and which registers make up one reading, are
// both said by the automaton. They used to be worked out by dividing a register
// number by the number of tags, which was true of one way of handing registers
// out and of nothing else.
// The gatherings a transition's copies will read, and only those.
//
// A transition that copies a register needs that register's gathering as it
// was before the transition, so what was here copied the whole set on every
// character -- every register's gathering of every field, to be ready for a
// copy of one or two of them. On a subject read a character at a time that was
// most of what reading it cost.
template <class states_type, std::size_t command_capacity>
struct kept_gatherings {
  using held_type = typename states_type::value_type;
  std::array<std::size_t, command_capacity> which{};
  // Room for as many as a transition could ask for, and a gathering made in
  // none of them until one is asked for. A transition that copies two of them
  // used to make one for every command it could have had, and throw the rest
  // away unread -- a dozen strings a character where a field ends.
  std::array<std::optional<held_type>, command_capacity> held{};
  std::size_t count = 0;

  [[nodiscard]] constexpr const held_type& operator[](
      std::size_t source) const {
    for (std::size_t at = 0; at < count; ++at) {
      if (which[at] == source) return *held[at];
    }
    return *held[0];
  }
};

template <class states_type, std::size_t command_count>
[[nodiscard]] constexpr auto keep_gatherings(
    const states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count) {
  kept_gatherings<states_type, command_count> kept;
  for (std::size_t index = 0; index < count; ++index) {
    if (commands[index].source == packed_command::no_source) continue;
    if (commands[index].value != -2) continue;
    bool already = false;
    for (std::size_t at = 0; at < kept.count; ++at) {
      if (kept.which[at] == commands[index].source) already = true;
    }
    if (already) continue;
    kept.which[kept.count] = commands[index].source;
    kept.held[kept.count].emplace(states[commands[index].source]);
    ++kept.count;
  }
  return kept;
}

template <std::size_t group, class type, fixed_string format, auto& automaton,
          bool hands_the_character = true, class states_type, class kept_type,
          std::size_t register_count, std::size_t command_count>
constexpr void advance_scanner(
    char symbol, std::size_t state, std::ptrdiff_t position,
    const std::array<std::ptrdiff_t, register_count>& registers,
    const kept_type& old_states, states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count, const char* text) {
  static constexpr auto spread = spread_of<type, format>();
  constexpr std::size_t opening = group * 2;
  constexpr std::size_t closing = group * 2 + 1;
  using held_type = leaf_kind_of_output<type, group>;
  constexpr bool gathers_a_list = scanned_as_range<held_type>;
  using how = gathering_of<type, format, group>;
  // A group inside a folding place is not gathered at all: its place tells the
  // fold what happened to it, and does that for all of its groups in one go,
  // where the order can be got right.
  if constexpr (how::folds && how::inside) return;
  // A field is gathered at the register holding its opening: nothing is written
  // inside a field, so that register stands still while the characters arrive.
  //
  // It is read at the register holding its closing, and the two are joined by a
  // copy made at the moment the group closes. Readings divide: two can hold the
  // same opening and disagree about whether the field is still being read, one
  // having met what follows it and one not, and one gathering cannot be both
  // "bob" and "bob id=7". The one that goes on goes on adding to the opening;
  // the one that has closed keeps the copy taken when it closed, and that is
  // what it is read from.
  //
  // A list is gathered and read at its opening throughout. Its elements go on
  // being added to the same list however the readings divide, and where it
  // began is what says which list that is.
  // Two passes, because a transition can rename a group's opening and write
  // its closing at once: `r86(t3) <- position` beside `r77(t2) <- r80` is a
  // field ending and its opening moving house in the same breath. The copy has
  // to happen first, or the copy taken for the closing is of a register that
  // has not been given what it holds yet.
  std::size_t command_index = 0;
  std::apply(
      [&](const auto&... command) {
        ([&] {
          if (command_index++ >= count) return;
          const std::uint32_t tag = automaton.register_tag[command.destination];
          if (tag == opening) {
            if (command.source != packed_command::no_source &&
                command.value == -2) {
              // A reading that divides carries its gathering with it.
              std::get<gathering_slot<type, format, group>>(
                  states[command.destination]) =
                  std::get<gathering_slot<type, format, group>>(
                      old_states[command.source]);
            } else if constexpr (gathers_a_list) {
              std::get<gathering_slot<type, format, group>>(
                  states[command.destination]) = held_type{};
            } else {
              std::get<gathering_slot<type, format, group>>(
                  states[command.destination]) =
                  gathering_of<type, format, group>::begin(
                      spread.parameters[group].view());
            }
          } else if (tag == closing && registers[command.destination] != position &&
                     command.source != packed_command::no_source &&
                     command.value == -2) {
            // A closing already written, only being carried along, keeps what
            // it holds.
            std::get<gathering_slot<type, format, group>>(
                states[command.destination]) =
                std::get<gathering_slot<type, format, group>>(
                    old_states[command.source]);
          }
        }(),
         ...);
      },
      commands);
  if constexpr (how::folds && how::the_place) {
    // Everything that happened inside this place on this character, told in
    // order -- and told now, before the copy below, or a fold that ends where
    // its place ends would be copied one closing short.
    fold_the_readings<group, gathering_slot<type, format, group>,
                      std::remove_cv_t<held_type>, automaton>(
        state, registers, states, symbol, hands_the_character, text);
  }
  if constexpr (!gathers_a_list) {
    // The groups that close on this step. What the field gathered is in the
    // opening it was being added to, whichever register that has become.
    command_index = 0;
    std::apply(
        [&](const auto&... command) {
          ([&] {
            if (command_index++ >= count) return;
            if (automaton.register_tag[command.destination] != closing) return;
            if (registers[command.destination] != position) return;
            const auto& entered = automaton.states[state];
            for (std::size_t reading = 0; reading < entered.reading_count;
                 ++reading) {
              if (entered.readings[reading][closing] != command.destination) {
                continue;
              }
              std::get<gathering_slot<type, format, group>>(
                  states[command.destination]) =
                  std::get<gathering_slot<type, format, group>>(
                      states[entered.readings[reading][opening]]);
              break;
            }
          }(),
           ...);
        },
        commands);
  }
  // A list gathers elements, not characters. Written as an early return this
  // would discard nothing: what follows an `if constexpr` is not the branch it
  // did not take.
  //
  // Handing the character over is skipped where the caller knows the state and
  // does it by the numbers: this walks every reading of the state and keeps a
  // flag per register to do it once, which on a subject read a character at a
  // time is most of what reading it costs.
  if constexpr (!gathers_a_list && hands_the_character) {
    // Once each, however many readings share it: a register is one gathering.
    std::array<bool, register_count> filled{};
    const auto& packed = automaton.states[state];
    for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
      const std::uint32_t open = packed.readings[reading][opening];
      const std::uint32_t close = packed.readings[reading][closing];
      if (filled[open]) continue;
      if (registers[open] < 0 || registers[close] >= registers[open]) continue;
      filled[open] = true;
      gathering_of<type, format, group>::push(
          std::get<gathering_slot<type, format, group>>(states[open]), symbol);
    }
  }
}

// Where a value's gathering is, asked without saying how it is kept.
//
// The machine that gathers keeps one per register, and which register holds a
// place depends on the reading the walk is in. A type that folds its own groups
// keeps them all together in one state of its own. The putting together of a
// value is the same work either way, so it is written once and asks for what it
// needs through one of these.
template <class type, fixed_string format, class reading_type,
          class states_type, std::size_t register_count>
struct gathered_by_the_registers {
  const reading_type& reading;
  const states_type& states;
  const std::array<std::ptrdiff_t, register_count>& registers;

  // A field still being read when the input ended is where it was being
  // gathered; one that ended earlier is the copy taken when it closed, which
  // the readings that went on adding to the opening cannot have changed.
  template <std::size_t place>
  [[nodiscard]] constexpr const auto& gathering() const {
    const std::uint32_t open = reading[place * 2];
    const std::uint32_t close = reading[place * 2 + 1];
    const bool still_reading = registers[close] < registers[open];
    return std::get<gathering_slot<type, format, place>>(
        states[still_reading ? open : close]);
  }

  // A list is gathered and read at its opening throughout: its elements go on
  // being added to the same list however the readings divide.
  template <std::size_t place>
  [[nodiscard]] constexpr const auto& list() const {
    return std::get<gathering_slot<type, format, place>>(
        states[reading[place * 2]]);
  }

  template <std::size_t place>
  [[nodiscard]] constexpr bool took_part() const {
    return registers[reading[place * 2]] >= 0;
  }

  // What a place stood on, where the subject can be pointed at. Nothing where
  // the place took no part.
  //
  // A position here is how many characters have been read and not the index of
  // one, so what a place stood on begins one before where its opening says.
  template <std::size_t place>
  [[nodiscard]] constexpr std::string_view span(const char* text) const {
    const std::ptrdiff_t began = registers[reading[place * 2]];
    const std::ptrdiff_t ended = registers[reading[place * 2 + 1]];
    if (began < 0 || ended < began) return {};
    return std::string_view(text + (began > 0 ? began - 1 : 0),
                            static_cast<std::size_t>(ended - began));
  }

  // A fold at this place, with the last step run into the copy: the end of the
  // input is not a character, so what it left open is closed here.
  template <std::size_t place, class held>
  [[nodiscard]] constexpr auto fold_at() const {
    auto fold = gathering<place>();
    fold_one_step<place, held>(fold, reading, registers, '\0', false);
    return fold;
  }
};

// Made rather than named: the reading, the states and the registers are all
// deduced, and the type and the format are what say where a group is gathered.
template <class type, fixed_string format, class reading_type,
          class states_type, std::size_t register_count>
[[nodiscard]] constexpr auto by_the_registers(
    const reading_type& reading, const states_type& states,
    const std::array<std::ptrdiff_t, register_count>& registers) {
  return gathered_by_the_registers<type, format, reading_type, states_type,
                                   register_count>{reading, states, registers};
}


template <class root, class type, std::size_t offset, bool as_output = false,
          class failure_type = failure_for<root>, class source_type>
[[nodiscard]] constexpr std::expected<type, failure_type> finish_value(
    const source_type& source, const char* text);

// The parts of a product, and the arguments of a call, as named functions
// rather than as lambdas called where they stand. A lambda holding references
// and called inside the argument of something that itself holds references is
// more than the constant evaluator will follow.
template <class root, class type, std::size_t offset, class failure_type,
          class source_type, std::size_t... part>
[[nodiscard]] constexpr std::expected<type, failure_type> finish_parts(
    const source_type& source, const char* text,
    std::index_sequence<part...>) {
  auto parts =
      std::tuple{finish_value<root, typename parts_of<type>::template at<part>,
                              offset + groups_before_field<type, part>(), false,
                              failure_type>(source, text)...};
  if (auto went_wrong = what_went_wrong<failure_type>(parts)) {
    return std::unexpected(std::move(*went_wrong));
  }
  return type{std::move(*std::get<part>(parts))...};
}

template <class root, class type, std::size_t offset, class failure_type,
          class source_type, std::size_t... part>
[[nodiscard]] constexpr std::expected<type, failure_type> finish_by_call(
    const source_type& source, const char* text,
    std::index_sequence<part...>) {
  auto parts =
      std::tuple{finish_value<root, typename parts_of<type>::template at<part>,
                              offset + groups_before_field<type, part>(), false,
                              failure_type>(source, text)...};
  if (auto went_wrong = what_went_wrong<failure_type>(parts)) {
    return std::unexpected(std::move(*went_wrong));
  }
  return scan::scanner<std::remove_cv_t<type>>::parse(
      std::move(*std::get<part>(parts))...);
}

// An element ends where the next one begins, and where that is, is said by a
// command writing a fresh position into the group the element starts at. The
// positions still hold the turn that is ending when this runs, which is why it
// runs before they move.
// A list is written to with push_back, which is what a range is asked for. It
// is reached here through insert where the type has it, because libc++ writes
// vector::emplace_back through a helper taking two capturing lambdas, and
// clang's constant evaluator refuses those ("captures not currently allowed"):
// a list of anything but a leaf could not be read while compiling. Appending
// at the end is the same thing either way.
template <class list_type, class element_type>
constexpr void append_to(list_type& list, element_type&& value) {
  if constexpr (requires { list.insert(list.end(), std::move(value)); }) {
    list.insert(list.end(), std::move(value));
  } else {
    list.push_back(std::move(value));
  }
}

template <std::size_t group, class type, fixed_string format, auto& automaton,
          class failure_type, class states_type, std::size_t register_count,
          std::size_t command_count>
constexpr void collect_element(
    std::size_t state,
    const std::array<std::ptrdiff_t, register_count>& registers,
    states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count, const char* text,
    std::optional<failure_type>& failed) {
  if constexpr (group == 0) {
    return;
  } else if constexpr (!scanned_as_range<leaf_kind_of_output<type, group - 1>>) {
    return;
  } else {
    using list_type = leaf_kind_of_output<type, group - 1>;
    using element = std::remove_cvref_t<std::ranges::range_value_t<list_type>>;
    constexpr std::size_t list_group = group - 1;
    // Does this step begin another turn? It does if it writes a fresh position
    // into a register that holds the place an element starts at.
    bool going_round = false;
    for (std::size_t index = 0; index < count; ++index) {
      const auto& command = commands[index];
      if (command.value == -2) continue;
      if (automaton.register_tag[command.destination] == group * 2) {
        going_round = true;
        break;
      }
    }
    if (!going_round) return;
    // The turn that is ending belongs to the state being left, and so do the
    // positions and the gatherings. Where the list goes next is the business of
    // the commands, which carry the gathering with the register.
    const auto& packed = automaton.states[state];
    std::array<bool, register_count> done{};
    for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
      const std::uint32_t open = packed.readings[reading][group * 2];
      const std::uint32_t into = packed.readings[reading][list_group * 2];
      if (done[into] || registers[open] < 0) continue;
      done[into] = true;
      auto one = finish_value<type, element, group, false, failure_type>(
          by_the_registers<type, format>(packed.readings[reading], states,
                                         registers),
          text);
      if (!one) {
        if (!failed) failed = std::move(one).error();
        continue;
      }
      append_to(std::get<gathering_slot<type, format, list_group>>(states[into]),
                std::move(*one));
    }
  }
}

template <class type, fixed_string format, auto& automaton, class failure_type,
          std::size_t register_count, class states_type,
          std::size_t command_count, std::size_t... group>
constexpr void collect_elements(
    std::size_t state,
    const std::array<std::ptrdiff_t, register_count>& registers,
    states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count, std::index_sequence<group...>, const char* text,
    std::optional<failure_type>& failed) {
  (collect_element<group, type, format, automaton, failure_type>(
       state, registers, states, commands, count, text, failed),
   ...);
}

template <class type, fixed_string format, auto& automaton,
          bool hands_the_character = true, std::size_t register_count,
          class states_type, std::size_t command_count, std::size_t... group>
constexpr void advance_scanners(
    char symbol, std::size_t state, std::ptrdiff_t position,
    const std::array<std::ptrdiff_t, register_count>& registers,
    states_type& states,
    const std::array<packed_command, command_count>& commands,
    std::size_t count, std::index_sequence<group...>,
    const char* text = nullptr) {
  // The old gatherings are only needed where a command copies one, and inside a
  // field nothing is copied and nothing is written -- that is what holding the
  // tags back bought. Copying the whole set on every character to be ready for
  // a copy that is not coming is the difference between reading a long command
  // and reading it twice for nothing.
  bool copies = false;
  for (std::size_t index = 0; index < count; ++index) {
    if (commands[index].source != packed_command::no_source &&
        commands[index].value == -2) {
      copies = true;
      break;
    }
  }
  if (copies) {
    const auto old_states = keep_gatherings(states, commands, count);
    (advance_scanner<group, type, format, automaton, hands_the_character>(
         symbol, state, position, registers, old_states, states, commands,
         count, text),
     ...);
  } else {
    (advance_scanner<group, type, format, automaton, hands_the_character>(
         symbol, state, position, registers, states, states, commands,
         count, text),
     ...);
  }
}

template <class type, class state_type, std::size_t... index>
[[nodiscard]] constexpr type finish_scanners(
    state_type state, std::index_sequence<index...>) {
  type result{};
  ((scan::fields<type>::template of<index>(result) =
        scanner_finish<field_type<type, index>>(
            std::move(std::get<index>(state)))),
   ...);
  return result;
}

// The output put together from the gatherings, walked the same way it is walked
// when the values are pieces of a subject that can be pointed at: a value asks
// its own reader to finish, a product asks its parts, a type made by a call
// makes it. Each value is taken from the gathering of the register that holds
// its opening tag in the reading that accepted.
template <class root, class type, std::size_t offset, bool as_output,
          class failure_type, class source_type>
[[nodiscard]] constexpr std::expected<type, failure_type> finish_value(
    const source_type& source, const char* text) {
  // A shape that reads its own groups is a value where it stands in somebody
  // else's format and a product of places in its own. Where this is the whole
  // of what is being read, it is the second.
  constexpr bool a_value = scanned_as_leaf<type> && !as_output;
  if constexpr (a_value && folds_by_turns<std::remove_cv_t<type>>) {
    // A leaf that was told its groups as the walk passed them. What is left is
    // the end of the input, which is not a character and so was never handed
    // over: a group that opened where nothing followed it, and every group
    // still open when the reading stopped. The same step the walk runs says
    // both, asked once more with nothing to hand over.
    using held = std::remove_cv_t<type>;
    static_assert(
        requires { source.template fold_at<offset, held>(); },
        "a shape whose places include a type that folds its own groups is read "
        "by the machine that gathers, not by a fold of its own");
    auto fold = source.template fold_at<offset, held>();
    if (fold.wanted_a_subject) {
      return std::unexpected(scan::as_a_failure<failure_type>(wrong_subject(
          "a fold that only takes its groups whole needs a subject that can be "
          "pointed at: give it push_group to read a stream")));
    }
    if constexpr (scan::says_what_went_wrong_folding<held>) {
      auto got = scan::scanner<held>{}.try_finish_groups(std::move(fold.state));
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<failure_type>(std::move(got).error()));
    } else {
      return scan::scanner<held>{}.finish_groups(std::move(fold.state));
    }
  } else if constexpr (a_value && gathers_by_its_groups<type>) {
    // A leaf built from its own groups once the match is over. They are groups
    // of this match like any others and the positions say where each one
    // stood, so what it is handed are views of the subject: nothing was
    // gathered for it and nothing was copied. That it has a subject to point
    // at is settled where the walk is made.
    using held = std::remove_cv_t<type>;
    constexpr std::size_t inside = groups_a_leaf_opens<held>();
    std::array<std::string_view, inside> theirs{};
    std::array<bool, inside> took{};
    static_assert(
        requires { source.template span<offset>(text); },
        "a shape whose places include a type built from its own groups is read "
        "by the machine that gathers, not by a fold of its own");
    [&]<std::size_t... at>(std::index_sequence<at...>) {
      ((void)[&] {
        constexpr std::size_t which = offset + 1 + at;
        const std::string_view stood_on = source.template span<which>(text);
        if (stood_on.data() == nullptr) return;
        took[at] = true;
        theirs[at] = stood_on;
      }(), ...);
    }(std::make_index_sequence<inside>{});
    const auto given = std::span<const std::string_view>(theirs);
    if constexpr (scan::says_what_went_wrong_from_groups<held>) {
      auto got = scan::scanner<held>{}.try_from_groups(given);
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<failure_type>(std::move(got).error()));
    } else if constexpr (requires {
                           scan::scanner<held>{}.from_groups(given);
                         }) {
      return scan::scanner<held>{}.from_groups(given);
    } else {
      auto state = scan::scanner<held>{}.begin_groups();
      [&]<std::size_t... at>(std::index_sequence<at...>) {
        ((void)[&] {
          if (!took[at]) return;
          open_one_group<held, at>(state);
          close_one_group<held, at>(state, theirs[at]);
        }(), ...);
      }(std::make_index_sequence<inside>{});
      if constexpr (scan::says_what_went_wrong_folding<held>) {
        auto got = scan::scanner<held>{}.try_finish_groups(std::move(state));
        if (got) return std::move(*got);
        return std::unexpected(
            scan::as_a_failure<failure_type>(std::move(got).error()));
      } else {
        return scan::scanner<held>{}.finish_groups(std::move(state));
      }
    }
  } else if constexpr (a_value) {
    const auto& gathered = source.template gathering<offset>();
    if constexpr (scan::says_what_went_wrong_finishing<type>) {
      auto got = scan::scanner<std::remove_cv_t<type>>{}.try_finish(gathered);
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<failure_type>(std::move(got).error()));
    } else {
      return scanner_finish<type>(gathered);
    }
  } else if constexpr (scanned_as_range<type>) {
    // What has been put in as each element ended, and then the one that was
    // still being read when the whole thing ended.
    using element = std::remove_cvref_t<std::ranges::range_value_t<type>>;
    type made = source.template list<offset>();
    // The turn that was still going when the whole thing ended. Where the list
    // is written to be allowed none at all, there may not have been one.
    if (source.template took_part<offset + 1>()) {
      auto last = finish_value<root, element, offset + 1, false, failure_type>(
          source, text);
      if (!last) return std::unexpected(std::move(last).error());
      append_to(made, std::move(*last));
    }
    return made;
  } else if constexpr (scanned_as_variant<type>) {
    // Exactly one branch ran, and its mark says so: the mark of the branch
    // that took part was opened, and the others never were. The same question
    // the subject that can be pointed at answers by whether the group points
    // anywhere.
    return [&]<std::size_t... branch>(std::index_sequence<branch...>)
               -> std::expected<type, failure_type> {
      std::optional<std::expected<type, failure_type>> made;
      const auto take = [&]<std::size_t which>() {
        constexpr std::size_t mark =
            offset + groups_before_branch<type, which>();
        if (made || !source.template took_part<mark>()) return;
        using alternative = branch_at<type, which>;
        auto part = finish_value<root, alternative, mark + 1, false,
                                 failure_type>(source, text);
        if (!part) {
          made = std::unexpected(std::move(part).error());
          return;
        }
        made = scan::branches<std::remove_cv_t<type>>::template make<which>(
            std::move(*part));
      };
      (take.template operator()<branch>(), ...);
      if (!made) {
        return std::unexpected(scan::as_a_failure<failure_type>(
            no_match("no branch of the format took the input")));
      }
      return std::move(*made);
    }(std::make_index_sequence<branch_count<type>()>{});
  } else if constexpr (scanned_from_values<type>) {
    return finish_by_call<root, type, offset, failure_type>(
        source, text, std::make_index_sequence<parts_of<type>::count>{});
  } else {
    return finish_parts<root, type, offset, failure_type>(
        source, text, std::make_index_sequence<parts_of<type>::count>{});
  }
}

// A shape's places, gathered together, and what the walk has told it.
//
// This is the other way of answering the builder's questions. Where the machine
// keeps a gathering per register and works out which register holds a place,
// this keeps them all in one object -- which is what a type is handed when it
// is told its own groups, and it is told them because its places take turns.
template <class type, fixed_string format>
struct shape_turns {
  using held = std::remove_cv_t<type>;
  static constexpr std::size_t places = groups_of_output<held>();
  using gatherings_type = decltype(make_scanner_state<held, format>());

  gatherings_type gatherings = make_scanner_state<held, format>();
  // An element that did not read, kept until there is somebody to hand it to:
  // a turn ends in the middle of a walk, where there is nowhere to say so.
  std::optional<shape_failure<held>> went_wrong{};
  // Which places have been opened since they were last read out. A choice says
  // which branch ran by which mark opened; a list says whether a turn is going.
  std::array<bool, places == 0 ? 1 : places> took{};
};

template <class shape_type>
struct gathered_by_a_fold {
  shape_type& state;

  template <std::size_t place>
  [[nodiscard]] constexpr const auto& gathering() const {
    return std::get<place>(state.gatherings);
  }

  template <std::size_t place>
  [[nodiscard]] constexpr const auto& list() const {
    return std::get<place>(state.gatherings);
  }

  template <std::size_t place>
  [[nodiscard]] constexpr bool took_part() const {
    return state.took[place];
  }

  // A fold at this place has been told everything as it happened -- the walk
  // hands the edges on and this hands them further -- so there is nothing left
  // to run into it here.
  template <std::size_t place, class held>
  [[nodiscard]] constexpr auto fold_at() const {
    return std::get<place>(state.gatherings);
  }
};

// Where a group of a shape belongs: the place it is, or the place it is inside
// of and which of that type's own groups it is.
template <class type, std::size_t group>
inline constexpr std::size_t shape_place_of =
    group - leaf_offset_of_output<std::remove_cv_t<type>, group>;

template <class type, std::size_t group>
inline constexpr std::size_t shape_place_inside =
    leaf_offset_of_output<std::remove_cv_t<type>, group>;

// One character, to the place it fell in -- or to the type standing at that
// place, where the group is one of that type's own. A list's own group holds no
// characters: what is inside it are the places of one turn, and they take them.
template <class type, fixed_string format, std::size_t group, class shape_type>
constexpr void push_shape_place(shape_type& state, char letter) {
  using held = std::remove_cv_t<type>;
  constexpr std::size_t place = shape_place_of<held, group>;
  constexpr std::size_t inside = shape_place_inside<held, group>;
  using stands_for = std::remove_cv_t<leaf_kind_of_output<held, group>>;
  if constexpr (inside != 0) {
    push_one_group<stands_for, inside - 1>(
        std::get<place>(state.gatherings).state, letter);
  } else if constexpr (gathers_by_its_groups<stands_for> ||
                       scanned_as_range<stands_for>) {
    // Neither takes characters of its own: a list holds turns and the places
    // inside it take them, and a type that reads its own groups is told them
    // by the groups, which are the places after this one.
    static_cast<void>(state);
    static_cast<void>(letter);
  } else {
    scanner_push<stands_for>(std::get<place>(state.gatherings), letter);
  }
}

// A place opened. Said so that a choice can be asked which branch ran and a
// list whether a turn is going -- and handed on where the group belongs to the
// type standing at that place.
template <class type, fixed_string format, std::size_t group, class shape_type>
constexpr void open_shape_place(shape_type& state) {
  using held = std::remove_cv_t<type>;
  constexpr std::size_t place = shape_place_of<held, group>;
  constexpr std::size_t inside = shape_place_inside<held, group>;
  if constexpr (inside != 0) {
    using stands_for = std::remove_cv_t<leaf_kind_of_output<held, group>>;
    open_one_group<stands_for, inside - 1>(
        std::get<place>(state.gatherings).state);
  } else {
    state.took[place] = true;
  }
}

// A place closed. Where it is a list, that is one turn: the element is put
// together out of the places inside it, added to the list, and those places
// begin again for the turn that may follow.
template <class type, fixed_string format, std::size_t group,
          class failure_type, class shape_type>
constexpr void close_shape_place(shape_type& state,
                                 std::optional<failure_type>& failed) {
  using held = std::remove_cv_t<type>;
  constexpr std::size_t place = shape_place_of<held, group>;
  constexpr std::size_t inside = shape_place_inside<held, group>;
  using stands_for = std::remove_cv_t<leaf_kind_of_output<held, group>>;
  if constexpr (inside != 0) {
    close_one_group<stands_for, inside - 1>(
        std::get<place>(state.gatherings).state);
  } else if constexpr (scanned_as_range<stands_for>) {
    using element = std::remove_cvref_t<std::ranges::range_value_t<stands_for>>;
    if (!state.took[place + 1]) return;
    auto one = finish_value<held, element, place + 1, false, failure_type>(
        gathered_by_a_fold<shape_type>{state}, nullptr);
    if (!one) {
      if (!failed) failed = std::move(one).error();
      return;
    }
    append_to(std::get<place>(state.gatherings), std::move(*one));
    // The turn is over: what its places gathered belongs to the element that
    // has just been taken, and the next turn starts from nothing.
    static constexpr auto spread = spread_of<held, format>();
    [&]<std::size_t... inside>(std::index_sequence<inside...>) {
      ((void)[&] {
        constexpr std::size_t which = place + 1 + inside;
        std::get<which>(state.gatherings) =
            gathering_of<held, format, which>::begin(
                spread.parameters[which].view());
        state.took[which] = false;
      }(), ...);
    }(std::make_index_sequence<groups_of<element>()>{});
  }
}

// Fed a character at a time. Whoever holds it says where the input ends, so
// the reading is anchored by default -- the walks below a match are kept, and
// the answer is the first still accepting when the feeding stops. A prefix
// read off a stream asks for the other policy, and stops where the match ends.
template <class type, fixed_string format, bool cut = true,
          class failure_type = failure_for<type>>
class stream_state {
 private:
  static_assert(
      !a_flat_reader_inside<type>(),
      "a type built from its groups after the match cannot read a stream: "
      "there is nothing left to point at by the time it would be handed them "
      "-- give it begin_groups and push_group to be told its groups as they "
      "arrive");
  inline static constexpr const auto& automaton =
      streaming_automaton<type, format, cut>;
  inline static constexpr std::size_t field_count = groups_of_output<type>();
  // One gathering per register, because a gathering follows the register it
  // belongs to and there is no arithmetic that says which registers go
  // together.
  inline static constexpr std::size_t slot_count = automaton.register_count;
  using field_states = register_state<type, format>;

 public:
  constexpr stream_state() {
    scanner_states_ = make_register_states<type, format, automaton>();
    std::ranges::fill(registers_, scan::tre::negative_tag);
    execute_commands(automaton.initialize, automaton.initialize.size(),
                     registers_, std::ptrdiff_t{0});
  }

  constexpr void push(char symbol) {
    if (!offer(symbol)) state_ = packed_range<0>::reject;
  }

  // The same step, told rather than assumed: false means the machine could not
  // take this character and has not moved. Whoever is feeding it then knows
  // that what came before is as far as the pattern goes, and holds a character
  // that belongs to whatever comes next.
  [[nodiscard]] constexpr bool offer(char symbol) {
    if (state_ == packed_range<0>::reject) return false;
    const std::size_t run =
        run_taken<automaton>(state_, static_cast<unsigned char>(symbol));
    if (run == no_run) return false;
    const auto* transition = &automaton.states[state_].ranges[run];
    collect_elements<type, format, automaton, failure_type>(
        state_, registers_, scanner_states_, transition->commands,
        transition->command_count, std::make_index_sequence<field_count>{},
        nullptr, failed_);
    execute_commands(transition->commands, transition->command_count, registers_,
                     ++position_);
    advance_scanners<type, format, automaton>(
        symbol, transition->target, position_, registers_, scanner_states_,
        transition->commands, transition->command_count,
        std::make_index_sequence<field_count>{});
    state_ = transition->target;
    return true;
  }

  // Where the machine stands now: would what it has read so far be a whole
  // match? Asked between characters, this is how something that reads a command
  // as it is typed knows the command has arrived.
  [[nodiscard]] constexpr bool accepting() const {
    return state_ != packed_range<0>::reject &&
           automaton.states[state_].accepting_slot !=
               packed_state<0, 0, 0>::not_accepting;
  }

  // What has been gathered for a field so far, in the reading the machine would
  // prefer.
  //
  // Standing in several readings at once, the machine has several answers for a
  // field until the input decides between them; the readings are held in the
  // order the pattern gives them, so the first is the one that would win if the
  // match ended here. That is the one worth showing while a command is still
  // being typed.
  //
  // What comes back is the state of that field's own scanner, as far as it has
  // been fed -- for a field of fixed room, the characters themselves.
  template <std::size_t field>
  [[nodiscard]] constexpr const auto& gathering() const {
    const std::size_t here =
        state_ == packed_range<0>::reject ? automaton.initial : state_;
    // The same question the reading asks: a field still being typed is where
    // it is being gathered, and one that has closed is the copy taken then.
    const auto& reading = automaton.states[here].readings[0];
    const std::uint32_t open = reading[field * 2];
    const std::uint32_t close = reading[field * 2 + 1];
    const bool still_reading = registers_[close] < registers_[open];
    return std::get<field>(scanner_states_[still_reading ? open : close]);
  }

  // Whether that field is being read right now: begun and not yet ended.
  template <std::size_t field>
  [[nodiscard]] constexpr bool reading() const {
    if (state_ == packed_range<0>::reject) return false;
    const auto& packed = automaton.states[state_];
    if (packed.reading_count == 0) return false;
    const std::uint32_t open = packed.readings[0][field * 2];
    const std::uint32_t close = packed.readings[0][field * 2 + 1];
    return registers_[open] >= 0 && registers_[close] < registers_[open];
  }

  [[nodiscard]] constexpr bool rejected() const {
    return state_ == packed_range<0>::reject;
  }

  // Accepting, with nowhere to go from here: the match is over and saying so
  // costs nothing, where finding out by offering the next character costs that
  // character. A pattern that ends in the thing that ends it -- a newline at
  // the end of a command -- settles like this, and then reading one after
  // another loses nothing at all.
  [[nodiscard]] constexpr bool settled() const {
    if (!accepting()) return false;
    const auto& packed = automaton.states[state_];
    for (std::size_t index = 0; index < packed.range_count; ++index) {
      if (packed.ranges[index].target != packed.ranges[index].reject) {
        return false;
      }
    }
    return true;
  }

  constexpr void restart() { *this = stream_state{}; }

  [[nodiscard]] constexpr std::expected<type, failure_type> finish() const& {
    stream_state copy = *this;
    return std::move(copy).finish();
  }

  [[nodiscard]] constexpr std::expected<type, failure_type> finish() && {
    if (failed_) return std::unexpected(std::move(*failed_));
    if (state_ == packed_range<0>::reject) {
      return std::unexpected(scan::as_a_failure<failure_type>(
          no_match("input does not match scan expression")));
    }
    const auto slot = automaton.states[state_].accepting_slot;
    if (slot == packed_state<0, 0, 0>::not_accepting) {
      return std::unexpected(scan::as_a_failure<failure_type>(
          no_match("input does not match scan expression")));
    }
    // The reading that accepted says which register holds each value. Nothing
    // is written here: the commands that end a match are not run by this
    // machine, so a group that never closed is read from where it was being
    // gathered, which is what the registers say.
    const auto& reached = automaton.states[state_];
    // Nothing to point at: this machine is fed and never holds the subject.
    return finish_value<type, type, 0, true, failure_type>(
        by_the_registers<type, format>(reached.readings[slot], scanner_states_,
                                       registers_),
        nullptr);
  }

 private:
  std::array<field_states, slot_count> scanner_states_{};
  std::array<std::ptrdiff_t, automaton.register_count> registers_{};
  std::size_t state_ = automaton.initial;
  std::ptrdiff_t position_ = 0;
  // An element of a list that did not read: met in the middle of the walk,
  // where there is nothing to hand it back to yet, so it waits here.
  std::optional<failure_type> failed_;
};

// Which gatherings a group is added to where the machine stands.
//
// A state stands in several readings at once and they can share a register, so
// what a character is added to is the set of the registers holding the group's
// opening across those readings, each of them once. Walking the readings to
// find that out on every character is what a machine that does not know where
// it stands has to do; where the state is known, the set is known, and this is
// it.
template <auto& automaton, std::size_t state, std::size_t group>
inline constexpr auto gathered_at = [] consteval {
  constexpr const auto& packed = automaton.states[state];
  struct answer {
    std::array<std::uint32_t, automaton.register_count> at{};
    std::size_t count = 0;
  };
  answer said;
  for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
    const std::uint32_t opening = packed.readings[reading][group * 2];
    bool already = false;
    for (std::size_t at = 0; at < said.count; ++at) {
      if (said.at[at] == opening) already = true;
    }
    if (!already) said.at[said.count++] = opening;
  }
  return said;
}();

// The gatherer the format reader hands to the walk: the fields' own scanners,
// following the moves.
//
// What the machine that can be stopped and started looks up on every character
// -- the state, its runs, the commands of the run it took, the readings that
// say which register holds which group -- is a constant here, because the walk
// knows where it stands. The work each character does is what it was: apply
// what the move writes to the gatherings, and give the character to the fields
// that are open.
template <class type, fixed_string format, auto& automaton,
          bool pointable = false>
class field_gatherer {
 public:
  // A type built from its groups once the match is over is handed views of the
  // subject. Where the subject is gone as it is read there is nothing to give
  // it, and holding the characters until the end to give it something would be
  // a hold with no bound.
  static_assert(
      pointable || !a_flat_reader_inside<type>(),
      "a type built from its groups after the match needs a subject that can "
      "be pointed at: give it begin_groups and push_group to be told its "
      "groups as they are read, or scan it from something contiguous");
  static constexpr std::size_t field_count = groups_of_output<type>();
  using states_type =
      std::array<register_state<type, format>, automaton.register_count>;

  constexpr field_gatherer() {
    states_ = make_register_states<type, format, automaton>();
  }

  // A list takes in the turn that has just ended, and what says it ended is
  // the registers as they stood before this move wrote anything.
  template <std::size_t state, std::size_t move, class registers_type>
  constexpr void moving(const registers_type& registers, std::ptrdiff_t) {
    constexpr const auto& taken = automaton.states[state].ranges[move];
    collect_elements<type, format, automaton, failure_for<type>>(
        state, registers, states_, taken.commands, taken.command_count,
        std::make_index_sequence<field_count>{}, text_, failed_);
  }

  // A run the walk stepped over in vectors: the fields that are open take all
  // of it, which is one pass over the piece rather than one call a character.
  template <std::size_t state, class registers_type>
  constexpr void took_run(const char* from, const char* to,
                          const registers_type& registers, std::ptrdiff_t) {
    hand_run<state>(from, to, registers,
                    std::make_index_sequence<field_count>{});
  }

  template <std::size_t state, std::size_t landed, class registers_type>
  constexpr void moved(std::size_t move, char letter,
                       const registers_type& registers,
                       std::ptrdiff_t position) {
    // A move that writes nothing leaves the gatherings where they are, and
    // most of the characters of a subject are read by one: inside a field
    // nothing is written, which is what holding the tags back bought. So the
    // whole of what a move does to the gatherings is skipped for it, and what
    // is left is handing the character to the fields that are open.
    if constexpr (state != landed || staying_writes<automaton, state>()) {
      const auto& taken = automaton.states[state].ranges[move];
      advance_scanners<type, format, automaton, false>(
          letter, landed, position, registers, states_, taken.commands,
          taken.command_count, std::make_index_sequence<field_count>{}, text_);
    }
    hand_over<landed>(letter, registers,
                      std::make_index_sequence<field_count>{});
  }

  // Where the walk ended is a constant, so the reading that accepted is one
  // too, and the value is put together here rather than looked for afterwards.
  // A walk keeps every place it passes, and the last one it keeps is the
  // answer -- so this runs more than once and the last of them wins, which is
  // what it has always done with the value. It has to do the same with a
  // failure: a place passed early where a field was not read yet is not this
  // reading's answer, and holding on to that would lose every match after it.
  template <std::size_t state, class registers_type>
  constexpr void ended(const registers_type& registers) {
    constexpr const auto& packed = automaton.states[state];
    auto got = finish_value<type, type, 0, true>(
        by_the_registers<type, format>(packed.readings[packed.accepting_slot],
                                       states_, registers),
        text_);
    if (!got) {
      failed_ = std::move(got).error();
      made_.reset();
      return;
    }
    failed_.reset();
    made_.emplace(std::move(*got));
  }

  // What was read, or what went wrong instead. An element of a list that did
  // not read is kept here too: the walk that met it is not over, and there is
  // nowhere to say so until it is.
  [[nodiscard]] constexpr std::expected<type, failure_for<type>> taken() {
    if (failed_) return std::unexpected(std::move(*failed_));
    if (!made_) {
      return std::unexpected(scan::as_a_failure<failure_for<type>>(
          no_match("input does not match scan expression")));
    }
    return std::move(*made_);
  }

  [[nodiscard]] constexpr std::optional<failure_for<type>>& went_wrong() {
    return failed_;
  }

  [[nodiscard]] constexpr std::optional<type>& made() { return made_; }

  // Where the subject begins, for a walk that has all of it in front of it. A
  // fold reading such a subject is handed each of its groups whole.
  constexpr void points_at(const char* text) { text_ = text; }

 private:
  template <std::size_t state, class registers_type, std::size_t... group>
  constexpr void hand_run(const char* from, const char* to,
                          const registers_type& registers,
                          std::index_sequence<group...>) {
    (hand_run_group<state, group>(from, to, registers), ...);
  }

  template <std::size_t state, std::size_t group, class registers_type>
  constexpr void hand_run_group(const char* from, const char* to,
                                const registers_type& registers) {
    using held_type = leaf_kind_of_output<type, group>;
    using how = gathering_of<type, format, group>;
    if constexpr (scanned_as_range<held_type>) {
      return;
    } else if constexpr (how::folds && how::inside) {
      return;
    } else if constexpr (how::folds && how::the_place) {
      // The characters of a run, each to the group it fell in. The positions
      // stand still across a run, so what opened and what closed is said once,
      // and where the subject can be pointed at nothing is handed over at all.
      using folded = fold_of<std::remove_cv_t<held_type>>;
      if constexpr (every_group_whole<held_type, typename folded::state_type>()) {
        if (from != to) {
          fold_the_readings<group, gathering_slot<type, format, group>,
                            std::remove_cv_t<held_type>, automaton>(
              state, registers, states_, *from, true, text_);
        }
      } else {
        for (const char* letter = from; letter != to; ++letter) {
          fold_the_readings<group, gathering_slot<type, format, group>,
                            std::remove_cv_t<held_type>, automaton>(
              state, registers, states_, *letter, true, text_);
        }
      }
    } else {
      constexpr auto at = gathered_at<automaton, state, group>;
      constexpr std::uint32_t closing =
          automaton.states[state].reading_count == 0
              ? 0
              : automaton.states[state].readings[0][group * 2 + 1];
      for (std::size_t which = 0; which < at.count; ++which) {
        const std::uint32_t opening = at.at[which];
        if (registers[opening] < 0) continue;
        if (registers[closing] >= registers[opening]) continue;
        gathering_of<type, format, group>::push_run(
            std::get<gathering_slot<type, format, group>>(states_[opening]),
            from, to);
      }
    }
  }

  template <std::size_t landed, class registers_type, std::size_t... group>
  constexpr void hand_over(char letter, const registers_type& registers,
                           std::index_sequence<group...>) {
    (hand_group<landed, group>(letter, registers), ...);
  }

  template <std::size_t landed, std::size_t group, class registers_type>
  constexpr void hand_group(char letter, const registers_type& registers) {
    using held_type = leaf_kind_of_output<type, group>;
    using how = gathering_of<type, format, group>;
    if constexpr (scanned_as_range<held_type>) {
      return;
    } else if constexpr (how::folds && how::inside) {
      return;
    } else if constexpr (how::folds && how::the_place) {
      fold_the_readings<group, gathering_slot<type, format, group>,
                        std::remove_cv_t<held_type>, automaton>(
          landed, registers, states_, letter, true, text_);
    } else {
      static constexpr auto spread = spread_of<type, format>();
      constexpr auto at = gathered_at<automaton, landed, group>;
      constexpr std::uint32_t closing =
          automaton.states[landed].reading_count == 0
              ? 0
              : automaton.states[landed].readings[0][group * 2 + 1];
      for (std::size_t which = 0; which < at.count; ++which) {
        const std::uint32_t opening = at.at[which];
        if (registers[opening] < 0) continue;
        if (registers[closing] >= registers[opening]) continue;
        gathering_of<type, format, group>::push(
            std::get<gathering_slot<type, format, group>>(states_[opening]),
            letter);
      }
    }
  }


  states_type states_;
  std::optional<type> made_;
  std::optional<failure_for<type>> failed_;
  const char* text_ = nullptr;
};

// One match off input that arrives in pieces, and where the next one starts.
//
// The head of the reading, in the sense the format reader means: the machine
// takes what it takes and stops, and where it stopped is where the reading
// goes on from -- which for pieces is a place in the piece it is holding, so
// nothing has to be put back.
template <class type, fixed_string format, class source_type>
struct taken_from_pieces {
  std::optional<type> value;
  bool matched = false;
};

// How much a reading of this format has to carry between one match and the
// next, where the subject arrives in pieces. The same number the stream
// reading asks for: the longest walk out of a match that finds no other one.
template <class type, fixed_string format>
inline constexpr std::size_t pieces_hold = [] consteval {
  constexpr std::size_t window =
      walk_past_a_match<streaming_automaton<type, format>>();
  if constexpr (window == std::numeric_limits<std::size_t>::max()) {
    return std::size_t{0};
  } else {
    return window * 2 + 1;
  }
}();

template <class type, fixed_string format, class source_type>
[[nodiscard]] constexpr auto take_from_pieces(source_type& into,
                                              const char*& cursor,
                                              const char*& last,
                                              std::ptrdiff_t& place) {
  constexpr const auto& automaton = streaming_automaton<type, format>;
  taken_from_pieces<type, format, source_type> said;
  std::array<std::ptrdiff_t, automaton.register_count> registers{};
  std::ranges::fill(registers, scan::tre::negative_tag);
  execute_commands(automaton.initialize, automaton.initialize.size(), registers,
                   place);
  // The same note as everywhere else, and here it costs nothing to go back to:
  // the place is an address inside a piece the reading is still holding.
  constexpr bool walks_past = walk_past_a_match<automaton>() != 0;
  using kept_type =
      std::conditional_t<walks_past,
                         std::array<std::ptrdiff_t, automaton.register_count>,
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
template <class type, fixed_string format, piecewise_char_range pieces_type>
[[nodiscard]] constexpr std::expected<type, failure_for<type>> scan_pieces(
    pieces_type&& pieces) {
  constexpr const auto& automaton = streaming_automaton<type, format>;
  std::array<std::ptrdiff_t, automaton.register_count> registers{};
  std::ranges::fill(registers, scan::tre::negative_tag);
  execute_commands(automaton.initialize, automaton.initialize.size(), registers,
                   std::ptrdiff_t{0});
  auto view = std::views::all(std::forward<pieces_type>(pieces));
  gathers_from_pieces<field_gatherer<type, format, automaton>, decltype(view),
                      pieces_hold<type, format>>
      into(field_gatherer<type, format, automaton>{}, std::move(view));
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
    return std::unexpected(scan::as_a_failure<failure_for<type>>(
        no_match("input does not match scan expression")));
  }
  return into.taken();
}

template <class type, fixed_string format, std::ranges::input_range range_type>
[[nodiscard]] constexpr std::expected<type, failure_for<type>> scan_stream(
    range_type&& input) {
  // The whole of the reading, so the walks below a match are kept.
  constexpr const auto& automaton = streaming_automaton<type, format, false>;
  std::array<std::ptrdiff_t, automaton.register_count> registers{};
  std::ranges::fill(registers, scan::tre::negative_tag);
  execute_commands(automaton.initialize, automaton.initialize.size(), registers,
                   std::ptrdiff_t{0});
  field_gatherer<type, format, automaton,
                 std::ranges::contiguous_range<range_type>>
      into;
  if constexpr (std::ranges::contiguous_range<range_type>) {
    into.points_at(std::ranges::data(input));
  }
  auto cursor = std::ranges::begin(input);
  std::ptrdiff_t position = 0;
  // Written out, the same as every other walk. A subject handed over a
  // character at a time is read by the machine written as code -- what it
  // cannot have is the vectors, because there is nothing in a row to read.
  constexpr walk_shape shape{.budget = bodies_worth_writing<automaton>()};
  walk_answer<decltype(cursor)> best;
  if (!run_continuation<automaton, shape, automaton.initial, shape.budget, 0,
                        std::ptrdiff_t>(cursor, std::ranges::end(input),
                                        position, registers, into, best)) {
    return std::unexpected(scan::as_a_failure<failure_for<type>>(
        no_match("input does not match scan expression")));
  }
  return into.taken();
}

// The head of a range that is read once, and the character that ended it.
//
// Nothing is buffered: the characters go through the machine as they come, the
// values are gathered by the scanners of the fields themselves, and the one
// character the machine could not take is handed back with them, because it has
// been read and cannot be put back where it came from.
template <class type>
struct taken_ahead {
  type value;
  std::optional<char> stopped;
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
template <std::size_t hold>
struct stream_carry {
  std::array<char, hold == 0 ? 1 : hold> held{};
  std::size_t count = 0;
  std::size_t at = 0;

  [[nodiscard]] constexpr bool empty() const { return at == count; }
  [[nodiscard]] constexpr char front() const { return held[at]; }
  constexpr void pop() { ++at; }

  // In front of whatever is still unread here, because they were read first.
  constexpr void put_in_front(const char* from, std::size_t many) {
    if (many == 0) return;
    const std::size_t left = count - at;
    std::array<char, hold == 0 ? 1 : hold> made{};
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
template <class type, fixed_string format, class iterator_type,
          class sentinel_type, std::size_t hold>
[[nodiscard]] constexpr std::expected<taken_ahead<type>, failure_for<type>>
scan_stream_prefix(iterator_type& first, sentinel_type last,
                   stream_carry<hold>& carry) {
  constexpr const auto& automaton = streaming_automaton<type, format>;
  constexpr std::size_t window = walk_past_a_match<automaton>();
  constexpr bool can_go_back = std::forward_iterator<iterator_type>;
  static_assert(
      can_go_back || window != std::numeric_limits<std::size_t>::max(),
      "this pattern can read any number of characters past a match without "
      "finding another one, so the reading that has to give them back would "
      "have to hold any number of them: read it from something that can be "
      "gone back over -- a forward range, characters in a row, or input in "
      "pieces");
  stream_state<type, format> state;
  std::optional<char> stopped;
  std::optional<stream_state<type, format>> note;
  // The place the note was taken at, kept the way this reading can keep it: an
  // iterator where the reading can be gone back over, and nothing at all where
  // it cannot -- an iterator of such a range cannot even be copied.
  using place_type =
      std::conditional_t<can_go_back, iterator_type, nothing_kept>;
  place_type note_at{};
  constexpr std::size_t held_here =
      can_go_back || window == 0 ||
              window == std::numeric_limits<std::size_t>::max()
          ? 1
          : window;
  std::array<char, held_here> since{};
  std::size_t since_count = 0;
  if (state.accepting()) note = state;
  while (true) {
    char symbol = 0;
    if (!carry.empty()) {
      symbol = carry.front();
    } else if (first != last) {
      symbol = static_cast<char>(*first);
    } else {
      break;
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
      ++first;
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
      [](std::expected<type, failure_for<type>> got,
         std::optional<char> ended_it)
      -> std::expected<taken_ahead<type>, failure_for<type>> {
    if (!got) return std::unexpected(std::move(got).error());
    return taken_ahead<type>{std::move(*got), ended_it};
  };
  if (state.accepting()) return handed_back(std::move(state).finish(), stopped);
  if (note) {
    // Past the match and dead. The answer is the place that was kept, and what
    // was read after it goes back in front of the reading.
    if constexpr (can_go_back) {
      first = note_at;
    } else {
      carry.put_in_front(since.data(), since_count);
    }
    return handed_back(std::move(*note).finish(), std::optional<char>{});
  }
  // Nothing matched; `finish` says so in the way the caller expects.
  return handed_back(std::move(state).finish(), stopped);
}

// How much a reading of this format has to be able to hold: nothing where it
// can be gone back over, and nothing where no walk out of a match ever fails
// to find another. Otherwise the characters read past a match, and the ones
// already held when that happened.
template <class type, fixed_string format, class iterator_type>
inline constexpr std::size_t stream_hold = [] consteval {
  if constexpr (std::forward_iterator<iterator_type>) {
    return std::size_t{0};
  } else {
    constexpr std::size_t window =
        walk_past_a_match<streaming_automaton<type, format>>();
    if constexpr (window == std::numeric_limits<std::size_t>::max()) {
      // Refused where it is used; sized so that saying so is what the caller
      // sees, rather than an array of every address there is.
      return std::size_t{0};
    } else {
      return window * 2;
    }
  }
}();

template <class type, fixed_string format, class iterator_type>
using stream_carry_for = stream_carry<stream_hold<type, format, iterator_type>>;

template <class type, fixed_string format, std::ranges::input_range range_type>
[[nodiscard]] constexpr std::expected<taken_ahead<type>, failure_for<type>>
scan_stream_prefix(range_type&& input) {
  auto first = std::ranges::begin(input);
  stream_carry<stream_hold<type, format, decltype(first)>> carry;
  return scan_stream_prefix<type, format>(first, std::ranges::end(input),
                                          carry);
}



}  // namespace scan::detail

}  // namespace scan::detail

// The helper that reads a shape, which is ordinary code and says so.
//
// It is here rather than higher up because the reading it does is the reading
// everything else does -- one builder, one gathering, one fold -- and a
// consumer that writes its own says the same things through the same hooks.
// Nothing below this line knows that a type has fields; this is where that
// knowledge lives.
export namespace scan {

template <fixed_string format>
struct aggregate_scanner {
  // The shape read out of groups that this format made, for a type that was
  // named rather than inherited from.
  //
  // A caller writing `scan<"{},{}">.of<point>()` says the format at the call
  // and the type at the call, and `point` may have no scanner at all. What
  // reads it is this, asked for both: the library below hands over the groups
  // and asks nothing about what a point is made of.
  template <class type>
  [[nodiscard]] static constexpr auto read(
      std::span<const std::string_view> groups)
      -> std::expected<type, detail::failure_for<type>> {
    return detail::build_value<detail::failure_for<type>,
                               detail::format_parameters<type, format>, type, 0,
                               true>(groups);
  }

  // The same, where the caller asked for the value itself: what went wrong is
  // thrown at the asking, which is the only place anything is thrown.
  template <class type>
  [[nodiscard]] static constexpr type read_or_throw(
      std::span<const std::string_view> groups) {
    return detail::build_value<detail::failure_for<type>,
                               detail::format_parameters<type, format>, type, 0,
                               true, detail::throws_a_failure>(groups);
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
    return detail::places_pattern<type, format>();
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
  template <class self_type>
    requires(!detail::says_a_list_inside<scanner_target_t<self_type>>())
  [[nodiscard]] constexpr auto try_from_groups(
      this const self_type& self, std::span<const std::string_view> groups)
      -> std::expected<scanner_target_t<self_type>,
                       detail::shape_failure<scanner_target_t<self_type>>> {
    using type = scanner_target_t<self_type>;
    static_cast<void>(self);
    return detail::build_value<detail::shape_failure<type>,
                               detail::format_parameters<type, format>, type, 0,
                               true>(groups);
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
  template <class self_type>
    requires(detail::turns_can_be_folded<scanner_target_t<self_type>, format>())
  [[nodiscard]] constexpr auto begin_groups(this const self_type& self) {
    static_cast<void>(self);
    return detail::shape_turns<scanner_target_t<self_type>, format>{};
  }

  template <class self_type, std::size_t place, class state_type>
    requires(detail::turns_can_be_folded<scanner_target_t<self_type>, format>())
  constexpr void push_group(this const self_type& self, state_type& state,
                            scan::group_at<place>, char letter) {
    static_cast<void>(self);
    detail::push_shape_place<scanner_target_t<self_type>, format, place>(
        state, letter);
  }

  template <class self_type, std::size_t place, class state_type>
    requires(detail::turns_can_be_folded<scanner_target_t<self_type>, format>())
  constexpr void opened_group(this const self_type& self, state_type& state,
                              scan::group_at<place>) {
    static_cast<void>(self);
    detail::open_shape_place<scanner_target_t<self_type>, format, place>(state);
  }

  template <class self_type, std::size_t place, class state_type>
    requires(detail::turns_can_be_folded<scanner_target_t<self_type>, format>())
  constexpr void closed_group(this const self_type& self, state_type& state,
                              scan::group_at<place>) {
    using type = scanner_target_t<self_type>;
    static_cast<void>(self);
    detail::close_shape_place<type, format, place,
                              detail::shape_failure<type>>(state,
                                                           state.went_wrong);
  }

  template <class self_type, class state_type>
    requires(detail::turns_can_be_folded<scanner_target_t<self_type>, format>())
  [[nodiscard]] constexpr auto try_finish_groups(this const self_type& self,
                                                 state_type state) {
    using type = scanner_target_t<self_type>;
    static_cast<void>(self);
    using failure_type = detail::shape_failure<type>;
    if (state.went_wrong) {
      return std::expected<type, failure_type>(
          std::unexpected(std::move(*state.went_wrong)));
    }
    return detail::finish_value<type, type, 0, true, failure_type>(
        detail::gathered_by_a_fold<state_type>{state}, nullptr);
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
  template <class self_type>
    requires(!requires { &scanner<scanner_target_t<self_type>>::parse; })
  [[nodiscard]] constexpr auto begin(this const self_type& self) {
    using type = scanner_target_t<self_type>;
    static_cast<void>(self);
    return detail::stream_state<type, format, false,
                                detail::shape_failure<type>>{};
  }

  constexpr void push(this const auto&, auto& state, char value) {
    state.push(value);
  }

  // Handed back rather than thrown, both of them: this type is read by the
  // same machine everything else is, and that machine says what went wrong
  // instead of throwing it. Which means a shape used as a field of another
  // shape carries its kinds up into what that reading can fail with.
  [[nodiscard]] constexpr auto try_finish(this const auto& self, auto state) {
    static_cast<void>(self);
    return std::move(state).finish();
  }

  [[nodiscard]] constexpr auto try_parse(this const auto& self,
                                         std::string_view input) {
    auto state = self.begin();
    for (char value : input) { self.push(state, value); }
    return self.try_finish(std::move(state));
  }
};

}  // namespace scan

export namespace scan::detail {

#undef SCAN_FORCE_INLINE

}  // namespace scan::detail
