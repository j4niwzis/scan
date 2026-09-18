// What a place gathers while the walk is passing over it.
//
// A field that cannot be pointed at afterwards is gathered as it arrives: the
// reader of its own type takes the characters, a fold is told its groups, a
// list takes its turns. This says what one of those is and how it is begun,
// told and finished; where it is kept is the next module's business.

export module scan.shape.gatherings;

import std;
import scan.tre;
export import scan.compiler;
export import scan.runtime;
export import scan.shape.places;

#if defined(_MSC_VER)
#define SCAN_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define SCAN_FORCE_INLINE [[gnu::always_inline]] inline
#else
#define SCAN_FORCE_INLINE inline
#endif

export namespace scan::detail {
template <class Type, std::size_t Index>
using field_type = typename scan::fields<Type>::template at<Index>;

// Nothing is gathered at the place a leaf that reads its own groups stands on:
// what it is built from are its groups, and they are gathered each at its own.
struct no_gathering {};

// Whether any group of a type is handed over whole. Said here because a turn
// has to know it before the question below can be asked.
template <class Held, class StateType>
[[nodiscard]] consteval bool any_group_taken_whole();

// The address a mark stands on, or none where a mark is not an address.
//
// A subject that arrives a character at a time has counts for marks and
// nothing to point into, so there is no address and no group can be handed
// over whole. Asked through a function of its own so that the question is
// answered once, where the mark's type is known, instead of in every place
// that would rather not know.
template <class Mark>
[[nodiscard]] constexpr const char* pointed_at(Mark position) {
  if constexpr (std::is_pointer_v<Mark>) {
    return position;
  } else {
    return nullptr;
  }
}

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
template <class Held, class MarkType = std::ptrdiff_t,
          class CarrierType = scan::default_context_t>
struct fold_turn {
  using held_type = std::remove_cv_t<Held>;
  static constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
  using state_type =
      decltype(begun_groups<held_type>(std::declval<const CarrierType&>()));

  constexpr fold_turn()
    requires std::default_initializable<CarrierType>
      : state(begun_groups<held_type>(CarrierType{})) {}
  constexpr explicit fold_turn(const CarrierType& told)
      : state(begun_groups<held_type>(told)) {}

  state_type state;
  // Where the walk stood, said the way the walk says it: a count where the
  // subject arrives a character at a time, an address where it lies in a row.
  // There the address is the cursor, which the walk is holding anyway, and a
  // count would be a step of its own on every character.
  std::array<MarkType, inside> told_at{};
  // And which closing it has been told about, for the same reason: a group
  // that is taken over and over writes its closing into the same register
  // every turn, so what says a turn has ended is that the position moved --
  // not that it stands anywhere in particular.
  std::array<MarkType, inside> ended_at{};
  // Which groups are open, a bit each. A byte each was an array to index on
  // every character, and which groups those are is known while this is
  // compiled -- so the whole of it is one word and a mask.
  std::uint64_t open = 0;
  // Whether this turn has been told anything at all. A place that has not been
  // stood on yet is not a turn that ended, and a list does not begin with one.
  bool started = false;
  // Where each group asked for whole began, as the address of its first
  // character.
  //
  // Not a mark: a mark is written by the machine in the machine's own
  // reckoning, held back to the step after the character that earned it, and
  // read out again by whoever knows that. This is the character itself, and it
  // is here because the step that opens a group is standing on it. Nothing at
  // all where no group is asked for whole, which is nearly always.
  static constexpr bool asked_whole =
      any_group_taken_whole<held_type, state_type>();
  [[no_unique_address]]
  std::array<const char*, asked_whole ? inside : 0> began_at{};
  // Whether it asked for something this subject cannot give: its groups whole,
  // off a reading with nothing to point at.
  bool wanted_a_subject = false;
  // The first character of the subject this walk started on, where there is
  // one to point at. Then a group that closes is handed whole, and the
  // characters are never handed over one at a time. Off a stream this stays
  // nothing, and the fold is told the characters instead.
  const char* text = nullptr;

  constexpr fold_turn() {
    told_at.fill(nowhere());
    ended_at.fill(nowhere());
  }

  // What "nowhere" is, said the same way.
  [[nodiscard]] static constexpr MarkType nowhere() {
    if constexpr (std::is_pointer_v<MarkType>) {
      return nullptr;
    } else {
      return MarkType{-1};
    }
  }
};

// What a group stood on, out of the subject and two positions.
//
// A position is how many characters have been read, or the address one past
// the character that wrote it -- the walk says it one way or the other. Either
// way what the group stood on begins one before where its opening says.
[[nodiscard]] constexpr std::string_view stood_on(const char* text, auto began,
                                                  auto ended) {
  if constexpr (std::is_pointer_v<decltype(began)>) {
    const char* from = began > text ? began - 1 : text;
    return std::string_view(from, static_cast<std::size_t>(ended - began));
  } else {
    const char* from = began > 0 ? text + began - 1 : text;
    return std::string_view(from, static_cast<std::size_t>(ended - began));
  }
}

// Whether a position says the walk never stood there.
//
// Said as a negative count where positions are counted, and as no address at
// all where they are addresses -- the walk says them one way or the other and
// everything that reads them asks here.
[[nodiscard]] constexpr bool stood_nowhere(auto where) {
  if constexpr (std::is_pointer_v<decltype(where)>) {
    return where == nullptr;
  } else {
    return where < 0;
  }
}

// Whether a closing lies at or after an opening -- that is, whether the group
// has closed since it opened.
//
// A position that stood nowhere is a null pointer where positions are
// addresses, and asking whether a null pointer is at or past a real address is
// a question a constant evaluation refuses to answer: the two point into
// unrelated objects. So the order is asked only of two positions that both
// stood somewhere, and a closing that never happened is simply not a closing.
// A flag per register, in as many words as that takes. One word covers the
// machines that have fewer than sixty-five registers, which is most of them;
// a reading that keeps everything at its registers has far more, and a mask of
// one word silently stopped saying anything about those above the sixty-fourth.
template <std::size_t Registers>
struct one_per_register {
  std::array<std::uint64_t, (Registers + 63) / 64 == 0 ? 1
                                                       : (Registers + 63) / 64>
      words{};
  [[nodiscard]] constexpr bool test(std::size_t at) const {
    return (words[at >> 6] & (std::uint64_t{1} << (at & 63))) != 0;
  }
  constexpr void set(std::size_t at) {
    words[at >> 6] |= std::uint64_t{1} << (at & 63);
  }
};

[[nodiscard]] constexpr bool closed_since(auto ended, auto began) {
  if (stood_nowhere(ended)) return false;
  return ended >= began;
}

// The same, for a place that is taken over and over.
//
// A tag is written a step after the character that wrote it, so the end of one
// turn is recorded at the position the next one begins at. Asked plainly,
// every turn after the first looks closed the moment it opens, and nothing is
// handed to it at all. An end standing exactly where the beginning stands
// therefore belongs to the turn before.
[[nodiscard]] constexpr bool closed_since_turn(auto ended, auto began,
                                               bool repeats) {
  if (stood_nowhere(ended)) return false;
  return repeats ? ended > began : ended >= began;
}

// A fold, which is one turn being gathered and at most one turn on its way out.
//
// A place that is taken over and over -- an element of a list -- is a turn at
// a time, and the walk learns that a turn ended a step after it did: a tag is
// written when the step after the character that wrote it is taken. So at the
// moment the next turn begins, the one before it has not been told what closed
// it, and a fold that was simply started afresh there lost the end of every
// turn it gathered.
//
// It is not started afresh. The turn that is ending is moved aside and goes on
// being told what closes it, for exactly as long as that takes -- one step --
// and the element is made from it when it has been. The turn that is beginning
// gathers meanwhile. Which is the same holding back the tags do, done for the
// gatherings.
// The turn on its way out is only kept where a place is taken over and over,
// which is a place standing for an element of a list. Everywhere else a place
// is stood on once, nothing is handed over in the middle of a walk, and room
// for a second turn would double what every walk carries for nothing at all.
struct no_turn {};

template <class Held, class MarkType = std::ptrdiff_t, bool Repeats = true,
          class CarrierType = scan::default_context_t>
struct fold_of {
  using held_type = std::remove_cv_t<Held>;
  static constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
  using state_type = typename fold_turn<Held, MarkType>::state_type;

  constexpr fold_of()
    requires std::default_initializable<CarrierType>
  = default;
  constexpr explicit fold_of(const CarrierType& told)
      : here(told), going(going_of(told)) {}

  [[nodiscard]] static constexpr auto going_of(const CarrierType& told) {
    if constexpr (Repeats) {
      return fold_turn<Held, MarkType, CarrierType>(told);
    } else {
      static_cast<void>(told);
      return no_turn{};
    }
  }

  fold_turn<Held, MarkType, CarrierType> here{};
  [[no_unique_address]]
  std::conditional_t<Repeats, fold_turn<Held, MarkType, CarrierType>, no_turn>
      going{};
  [[no_unique_address]] std::conditional_t<Repeats, bool, no_turn> has_going{};
};

// One step of a fold: what happened to the groups inside a place, said to the
// type in the order it can make sense of.
//
// Closed first, innermost outwards, because a group that opens inside another
// closes before it. Then the openings, outermost inwards, for the same reason
// read the other way. Then the character, to every group that is open once
// those two have settled it.
//
// Closings come first because of the delay. A tag is written when the walk
// takes the step after the character that wrote it, so the end of one turn of
// a repeated group and the start of the next arrive together -- and a step
// that opened before it closed would announce a turn that had not ended,
// hand the next turn's characters to nobody, and lose the closing entirely.
// `(X)*` over "XXX" was three openings, two closings and one character.
//
// Nothing here asks what step the walk is on. It asks the positions, which say
// everything: a group whose opening has moved has begun a turn, and one whose
// closing has moved has ended the turn this fold was told about. Both are
// compared against what was announced rather than against each other, because
// a group taken over and over writes into the same two registers every turn:
// what says a turn ended is that the position moved, not where it stands. So
// the same step run twice tells nothing twice, and the same step run at the end
// of the input -- where there is no character to hand over -- finishes what the
// characters left open.
// Which parts of a step a turn is to be told.
//
// A turn on its way out hears only what closes it: what a move opens belongs
// to the turn that has begun, and so does the character.
enum class fold_phase { whole, closings_only };

template <fold_phase Phase = fold_phase::whole, std::size_t Place, class Held,
          class ReadingType, class FoldType, class RegistersType>
SCAN_FORCE_INLINE constexpr void fold_one_step(
    FoldType& fold, const ReadingType& reading,
    const RegistersType& registers, char symbol, bool hands_the_character) {
  using held_type = std::remove_cv_t<Held>;
  constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
  const auto opening_of = [&](std::size_t which) {
    return slot_read(registers, reading[(Place + 1 + which) * 2]);
  };
  const auto closing_of = [&](std::size_t which) {
    return slot_read(registers, reading[(Place + 1 + which) * 2 + 1]);
  };
  [[clang::always_inline]] [&]<std::size_t... step>(std::index_sequence<step...>) {
    ((void)[&] {
      constexpr std::size_t which = inside - 1 - step;
      if ((fold.open & (std::uint64_t{1} << which)) == 0) return;
      const auto began = fold.told_at[which];
      const auto ended = closing_of(which);
      if (!closed_since(ended, began)) return;
      if (ended == fold.ended_at[which]) return;
      if constexpr (takes_the_group_whole<held_type, which,
                                          typename FoldType::state_type>) {
        // The whole of what the group stood on, pointed at rather than copied.
        //
        // A position here is how many characters have been read and not the
        // index of one: the walk writes a tag with the count after the
        // character that wrote it. So what a group stood on begins one before
        // where its opening says.
        if (fold.text != nullptr) {
          close_one_group<held_type, which>(
              fold.state,
              stood_on(fold.text, began, ended));
          fold.open &= ~(std::uint64_t{1} << which);
          fold.ended_at[which] = ended;
          return;
        }
        if constexpr (!takes_group_characters<held_type, which,
                                              typename FoldType::state_type>) {
          // It takes its groups whole and nothing else, and there is nothing
          // here to point at. Holding the characters to hand them over at the
          // end would be a hold with no bound, so this reading cannot be had --
          // said here and handed back where the value would have been, because
          // a walk has nobody to say it to.
          fold.wanted_a_subject = true;
          fold.open &= ~(std::uint64_t{1} << which);
          fold.ended_at[which] = ended;
          return;
        }
      }
      close_one_group<held_type, which>(fold.state);
      fold.open &= ~(std::uint64_t{1} << which);
      fold.ended_at[which] = ended;
    }(), ...);
  }(std::make_index_sequence<inside>{});
  if constexpr (Phase == fold_phase::closings_only) return;
  [[clang::always_inline]] [&]<std::size_t... which>(std::index_sequence<which...>) {
    ((void)[&] {
      const auto began = opening_of(which);
      if (stood_nowhere(began) || fold.told_at[which] == began) return;
      open_one_group<held_type, which>(fold.state);
      fold.told_at[which] = began;
      fold.open |= std::uint64_t{1} << which;
      fold.started = true;
    }(), ...);
  }(std::make_index_sequence<inside>{});
  if (hands_the_character) {
    [[clang::always_inline]] [&]<std::size_t... which>(std::index_sequence<which...>) {
      ((void)[&] {
        if constexpr (takes_group_characters<held_type, which,
                                             typename FoldType::state_type> &&
                      !takes_the_group_whole<
                          held_type, which,
                          typename FoldType::state_type>) {
          if ((fold.open & (std::uint64_t{1} << which)) == 0) return;
          push_one_group<held_type, which>(fold.state, symbol);
        } else if constexpr (takes_group_characters<
                                 held_type, which,
                                 typename FoldType::state_type>) {
          // It would take the group whole, and will where there is something
          // to point at. Where there is not, the characters are all there is.
          if (fold.text != nullptr) return;
          if ((fold.open & (std::uint64_t{1} << which)) == 0) return;
          push_one_group<held_type, which>(fold.state, symbol);
        }
      }(), ...);
    }(std::make_index_sequence<inside>{});
  }
}

// Whether every group of a fold would take its group whole. Then a run of
// characters the walk stepped over costs one step and not one a character:
// nothing is handed over, and what opened and what closed is the same at both
// ends of a run, because a run is where nothing is written.
template <class Held, class StateType>
[[nodiscard]] consteval bool every_group_whole() {
  using held_type = std::remove_cv_t<Held>;
  return []<std::size_t... which>(std::index_sequence<which...>) {
    return (true && ... &&
            takes_the_group_whole<held_type, which, StateType>);
  }(std::make_index_sequence<groups_a_leaf_opens<held_type>()>{});
}

// Whether every move of a machine says what the character it reads lies
// inside.
//
// Then no gathering has to follow a reading. Two readings differ in where the
// tags will land, and a fold is never told where a tag landed -- it is told
// that a group opened, that a character fell in it, that it closed. If every
// move agrees about which groups a character is inside, every reading would be
// told the very same things in the very same order, so one fold answers for
// all of them and lives in the walk itself rather than in a register.
template <auto& Automaton>
[[nodiscard]] consteval bool every_move_says_the_groups() {
  for (std::size_t state = 0; state < Automaton.states.size(); ++state) {
    const auto& here = Automaton.states[state];
    for (std::size_t move = 0; move < here.range_count; ++move) {
      if (!here.ranges[move].groups_known) return false;
    }
  }
  return true;
}

// The move a state takes to stay where it is, where it has one. A run is
// stepped over by taking that move again and again, so what it says about the
// character is what the whole run is inside of.
inline constexpr std::size_t no_move = std::numeric_limits<std::size_t>::max();

template <auto& Automaton, std::size_t State>
[[nodiscard]] consteval std::size_t staying_move() {
  const auto& here = Automaton.states[State];
  for (std::size_t move = 0; move < here.range_count; ++move) {
    if (here.ranges[move].target == State) return move;
  }
  return no_move;
}

// Whether a step can say what happened to a fold's groups without looking at
// anything.
//
// It can when both ends of the step know what they stand inside and each holds
// a single reading. Then the groups the character fell in are the ones the step
// arrived inside; what closed is what the step left and did not arrive in, and
// what opened is the other way round. All of it is a fact about two states,
// which is to say a fact about a place in the written-out code -- so the step
// costs the user's own arithmetic and nothing else.
template <auto& Automaton, std::size_t From, std::size_t Move>
[[nodiscard]] consteval bool step_says_the_groups() {
  // Asked of the whole machine and not of this move alone: a fold that is kept
  // in the walk has to be kept there for the whole of it, and what puts it
  // there is that no move anywhere needs a reading followed.
  return every_move_says_the_groups<Automaton>();
}

// The one register a fold stands at, where the state holds one reading.
template <auto& Automaton, std::size_t State, std::size_t Place>
inline constexpr std::uint32_t only_fold_register =
    Automaton.states[State].readings[0][Place * 2];

// Whether a type wants to hear where a group of its own begins and ends.
//
// A type that only takes the characters does not: it is told which group each
// character fell in and that is the whole of what it asked for. Keeping track
// of what is open for such a group is bookkeeping nobody reads -- and on a
// group that begins again on every character, as `(X|Y)*` does, it is that
// bookkeeping on every character.
template <class Held, std::size_t Which, class StateType>
[[nodiscard]] consteval bool takes_the_group_edges() {
  using scanner_type = scan::scanner<std::remove_cv_t<Held>>;
  return requires(StateType& state) {
    scanner_type{}.opened_group(state, scan::group_at<Which>{});
  } || requires(StateType& state) {
    scanner_type{}.opened_group(state, Which);
  } || requires(StateType& state) {
    scanner_type{}.closed_group(state, scan::group_at<Which>{});
  } || requires(StateType& state) {
    scanner_type{}.closed_group(state, Which);
  } || takes_the_group_whole<std::remove_cv_t<Held>, Which, StateType>;
}

// One step of a fold, told by the shape of the machine rather than by the
// positions it wrote.
// Что открыто на входе в состояние.
//
// Где каждый ход умеет сказать о своих группах, все входящие ходы согласны --
// значит набор открытых групп есть величина времени компиляции, и слово,
// которое несло его между символами, нужно было лишь чтобы прочитать уже
// известное.
template <auto& Automaton, std::size_t State>
[[nodiscard]] consteval std::uint64_t open_on_entry() {
  for (std::size_t f = 0; f < Automaton.states.size(); ++f)
    for (std::size_t m = 0; m < Automaton.states[f].range_count; ++m)
      if (Automaton.states[f].ranges[m].target == State)
        return Automaton.states[f].ranges[m].groups_open;
  return 0;
}

// Ключ -- пара масок, а не ребро.
//
// Внутри от ребра нужны только они: что открыто и что начато заново. Рёбер в
// машине много, а разных пар мало, и пока ключом стояло ребро, компилятор
// порождал тело на каждое ребро там, где по существу их несколько -- а каждое
// порождение он потом отдельно прогоняет через оптимизатор и отдельно решает
// про встраивание, по несвёрнутому размеру.
template <std::size_t Place, class Held, std::uint64_t NowMask,
          std::uint64_t AgainMask, std::uint64_t WasMask,
          bool EdgesCanMove = true, class FoldType>
constexpr void fold_by_the_step(FoldType& fold, char symbol,
                                bool hands_the_character, auto position) {
  using held_type = std::remove_cv_t<Held>;
  // Where this step stands, where that is a place in a subject at all.
  const char* const spot = pointed_at(position);
  constexpr std::size_t inside = groups_a_leaf_opens<held_type>();
  constexpr std::uint64_t now = NowMask;
  // A group this move begins again is one whose turn has ended, however the
  // masks stand: a place taken over and over is open on both sides of it.
  constexpr std::uint64_t again = AgainMask;
  // The groups of this place are the ones just past it: place 0 is the whole
  // and its groups follow it, which is how the readings are laid out too.
  constexpr auto holds = [](std::uint64_t mask, std::size_t which) {
    return (mask & (std::uint64_t{1} << (Place + 1 + which))) != 0;
  };
  // Closed innermost outwards, then opened outermost inwards, then the
  // character to whatever the step arrived inside -- and where a move stays
  // where it is and begins nothing again, none of that can have changed since
  // the move that arrived here, so the character is all there is to do.
  if constexpr (EdgesCanMove) {
  [&]<std::size_t... step>(std::index_sequence<step...>) {
    ((void)[&] {
      constexpr std::size_t which = inside - 1 - step;
      // What this move arrives inside is a constant; what the move before it
      // arrived inside is a word the walk carries. A group closes where the
      // second says yes and the first says no.
      // And only where somebody is listening. A group whose type takes the
      // characters and asks for nothing else has no edges to be told about,
      // so what is open is bookkeeping nobody reads -- and on a group that
      // begins again on every character it is that bookkeeping on every
      // character.
      if constexpr (takes_the_group_edges<
                        held_type, which, typename FoldType::state_type>() &&
                    (!holds(now, which) || holds(again, which))) {
        if constexpr (holds(WasMask, which)) {
          // What the group stood on, where there is a subject to point into
          // and the type asked for it. The step that closes a group is
          // standing on the character just past it, so what it stood on ends
          // where this step begins.
          if constexpr (takes_the_group_whole<
                            held_type, which,
                            typename FoldType::state_type>) {
            const char* const began = fold.here.began_at[which];
            if (began != nullptr && spot >= began) {
              close_one_group<held_type, which>(
                  fold.here.state,
                  std::string_view(began,
                                   static_cast<std::size_t>(spot - began)));
            } else {
              close_one_group<held_type, which>(fold.here.state);
            }
            fold.here.began_at[which] = nullptr;
          } else {
            close_one_group<held_type, which>(fold.here.state);
          }
          // Слово, которое несло это между символами, больше не читается.
        }
      }
    }(), ...);
  }(std::make_index_sequence<inside>{});
  [&]<std::size_t... which>(std::index_sequence<which...>) {
    ((void)[&] {
      if constexpr (takes_the_group_edges<
                        held_type, which, typename FoldType::state_type>() &&
                    holds(now, which)) {
        // Открывается там, где не было открыто -- или было, но этот ход
        // начал группу заново: проход закрытия выше уже погасил её, и слово,
        // которое раньше несло это между двумя проходами, здесь не нужно.
        if constexpr (!holds(WasMask, which) || holds(AgainMask, which)) {
          open_one_group<held_type, which>(fold.here.state);
          if constexpr (takes_the_group_whole<
                            held_type, which,
                            typename FoldType::state_type>) {
            fold.here.began_at[which] = spot;
          }
          fold.here.started = true;
        }
      }
    }(), ...);
  }(std::make_index_sequence<inside>{});
  }
  if (!hands_the_character) return;
  [&]<std::size_t... which>(std::index_sequence<which...>) {
    ((void)[&] {
      if constexpr (holds(now, which)) {
        if constexpr (takes_group_characters<held_type, which,
                                             typename FoldType::state_type>) {
          // A group that goes over whole is not also told its characters --
          // that is the whole point of asking for it whole. Off a stream there
          // is nothing to point at and the characters are all there is.
          if constexpr (takes_the_group_whole<
                            held_type, which,
                            typename FoldType::state_type>) {
            if (spot == nullptr) {
              push_one_group<held_type, which>(fold.here.state, symbol);
            }
          } else {
            push_one_group<held_type, which>(fold.here.state, symbol);
          }
        }
      }
    }(), ...);
  }(std::make_index_sequence<inside>{});
}

// The fold of every reading that stands at this state, told the same step once.
//
// A fold lives at the register holding the place's opening, which is the
// discipline a list is gathered by: readings that share that register share
// what is gathered there, and where two readings would have to disagree the
// machine has already given them registers of their own.
template <std::size_t Place, std::size_t Slot, class Held, auto& Automaton,
          class StatesType, class RegistersType>
constexpr void fold_the_readings(
    std::size_t state, const RegistersType& registers, StatesType& states,
    char symbol, bool hands_the_character, const char* text) {
  const auto& entered = Automaton.states[state];
  std::array<bool, Automaton.register_count> told{};
  for (std::size_t reading = 0; reading < entered.reading_count; ++reading) {
    const std::uint32_t at = entered.readings[reading][Place * 2];
    // Nowhere is said as a negative count or as no address at all, and the
    // walk says it whichever way it says positions.
    if (told[at] || stood_nowhere(slot_read(registers, at))) continue;
    told[at] = true;
    auto& folding = std::get<Slot>(states[at]);
    // Said every step rather than once, because a fold is made where its place
    // opens and carried where a reading divides, and neither of those knows
    // what the walk is reading.
    folding.here.text = text;
    fold_one_step<fold_phase::whole, Place, Held>(
        folding.here, entered.readings[reading], registers, symbol,
        hands_the_character);
    if constexpr (requires { folding.has_going = true; }) {
      if (folding.has_going) {
        folding.going.text = text;
        fold_one_step<fold_phase::closings_only, Place, Held>(
            folding.going, entered.readings[reading], registers, symbol, false);
      }
    }
  }
}

// Whether the place a group stands for is taken over and over, which is what
// an element of a list is and what nothing else is.
template <class Type, std::size_t Group>
[[nodiscard]] consteval bool a_place_that_repeats() {
  if constexpr (Group == 0) {
    return false;
  } else {
    return scanned_as_range<leaf_kind_of_output<Type, Group - 1>>;
  }
}

// How one group is gathered, made once and asked at every place that gathers.
//
// A leaf that is built from the groups its own pattern opens is not handed the
// text it stands on, so its place gathers nothing and each of its groups
// gathers characters. Every other group is gathered by the reader of the type
// it holds, which is what it was before any of this.
template <class Type, fixed_string Format, std::size_t Group,
          class MarkType = std::ptrdiff_t>
struct gathering_of {
  using held_type = leaf_kind_of_output<Type, Group>;
  static constexpr bool by_groups = gathers_by_its_groups<held_type>;
  // Whether this place is stood on over and over, which an element of a list
  // is and nothing else is. Asked through a function rather than written as an
  // expression: `group > 0 && …<group - 1>` still names the type at group - 1,
  // and at group zero that is an index of every bit set.
  static constexpr bool place_repeats = a_place_that_repeats<Type, Group>();
  static constexpr bool folds = folds_by_turns<std::remove_cv_t<held_type>>;
  static constexpr bool the_place = by_groups && leaf_offset_of_output<Type, Group> == 0;
  static constexpr bool inside = by_groups && leaf_offset_of_output<Type, Group> != 0;
  // Whether this leaf is only a piece of the subject: its scanner says how to
  // read a piece handed to it whole, and says nothing about being told one
  // character at a time. The marks say where the piece stood and the subject
  // is still there to be pointed at, so there is nothing to gather as the walk
  // goes -- it is read where the value is put together. Such a leaf is refused
  // where the subject is not kept, the same as one read from its groups.
  static constexpr bool only_a_piece =
      reads_a_whole_piece_only<std::remove_cv_t<held_type>>;

  template <class CarrierType>
  [[nodiscard]] static constexpr auto begin(std::string_view parameters,
                                            const CarrierType& told) {
    if constexpr (the_place && folds) {
      static_cast<void>(parameters);
      return fold_of<std::remove_cv_t<held_type>, MarkType, place_repeats,
                     CarrierType>(told);
    } else if constexpr (inside && folds) {
      static_cast<void>(parameters);
      static_cast<void>(told);
      return no_gathering{};
    } else if constexpr (the_place || inside || only_a_piece) {
      static_cast<void>(parameters);
      static_cast<void>(told);
      return no_gathering{};
    } else {
      return scanner_begin_given<held_type>(parameters, told);
    }
  }

  [[nodiscard]] static constexpr auto begin(std::string_view parameters) {
    if constexpr (the_place && folds) {
      // The type's own state, and the walk's note of what it has been told.
      static_cast<void>(parameters);
      return fold_of<std::remove_cv_t<held_type>, MarkType,
                     place_repeats>{};
    } else if constexpr (inside && folds) {
      // Nothing: the characters and the edges of this group go to the fold,
      // which is kept at the place the group is inside of.
      static_cast<void>(parameters);
      return no_gathering{};
    } else if constexpr (the_place || inside || only_a_piece) {
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

  template <class StateType>
  static constexpr void push(StateType& state, char letter) {
    if constexpr (the_place || inside || only_a_piece) {
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
  template <class StateType>
  static constexpr void push_run(StateType& state, const char* from,
                                 const char* to) {
    if constexpr (the_place || inside || only_a_piece) {
      static_cast<void>(state);
      static_cast<void>(from);
      static_cast<void>(to);
    } else {
      scanner_push_run<held_type>(state, from, to);
    }
  }
};

// Whether any group of a type is handed over whole -- cut out of the subject
// rather than told character by character. Such a group is read from the
// positions however its edges were announced.
template <class Held, class StateType>
[[nodiscard]] consteval bool any_group_taken_whole() {
  return []<std::size_t... which>(std::index_sequence<which...>) {
    return (false || ... || takes_the_group_whole<Held, which, StateType>);
  }(std::make_index_sequence<groups_a_leaf_opens<std::remove_cv_t<Held>>()>{});
}

// Which groups' marks anybody will read.
//
// The same question as below, asked of the marks rather than of the places. A
// group inside a fold that hears what happened from the moves has no place
// anybody asks about -- but it is still a group, and below it says so, because
// what says "this group took no part" is that it stood nowhere.
//
// Nothing says that of a group whose every edge is told. That is the whole
// difference between the two, and it is worth the two names: what a walk
// writes on every character is decided here, and what it may step over in one
// go is decided below.
template <class Type, fixed_string Format, auto& Automaton>
[[nodiscard]] consteval std::uint64_t groups_whose_mark_is_read() {
  std::uint64_t made = 0;
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    ([&] {
      using how = gathering_of<Type, Format, group>;
      using held = std::remove_cv_t<leaf_kind_of_output<Type, group>>;
      constexpr std::size_t inside = groups_a_leaf_opens<held>();
      constexpr bool a_fold_of_its_own =
          how::folds && how::the_place && !how::place_repeats &&
          every_move_says_the_groups<Automaton>();
      constexpr bool told_by_the_moves = [] {
        if constexpr (a_fold_of_its_own) {
          using state_type = decltype(scan::scanner<held>{}.begin_groups());
          return !any_group_taken_whole<held, state_type>();
        } else {
          return false;
        }
      }();
      // A fold that hears everything from the moves does not need its own
      // pair either. What its place stands for is never cut out of the
      // subject: the value is what finish_groups makes of the state it was
      // handed, and where the place began and ended is read by nobody. A place
      // taken over and over is the exception, and says so by itself --
      // told_by_the_moves is false for it, because its turns are told by its
      // marks, the same as any list's.
      if constexpr (!(how::folds && (how::inside || told_by_the_moves))) {
        made |= std::uint64_t{1} << group;
      }
      // Asked of the place, not of what stands inside it. `inside` counts the
      // groups of the whole leaf, which is an answer about a place -- and a
      // group standing inside a fold is not a place, so what it counted was
      // the groups that follow it. Every group inside a fold claimed the
      // marks of the groups after it, the claims overlapped, and a fold
      // whose groups nobody asks about kept every mark it has -- a machine
      // writing tags on every character for nobody to read.
      if constexpr (how::the_place && !told_by_the_moves) {
        for (std::size_t which = 0; which < inside; ++which) {
          made |= std::uint64_t{1} << (group + 1 + which);
        }
      }
    }(), ...);
  }(std::make_index_sequence<groups_of_output<Type>()>{});
  return made;
}

// The machine everything walks: the one above, with the marks nobody will
// read left out.
//
// Which those are is a question about the type and not about the expression,
// so it cannot be asked until both are known -- and asking it wants a machine
// to ask of, which is why the whole one is built first. What it buys is not
// only the stores: two readings of the input that differ in nothing but a mark
// nobody reads are one reading here, so the machine carries fewer of them,
// hands out fewer registers, and a run that wrote such a mark on every
// character becomes a run that writes nothing -- which is a run the walk can
// step over whole.
// Which groups' tags the machine is worth writing at all.
//
// Wider than the marks anybody reads, and for one reason: a tag is also how
// the machine knows a group began again. A place taken over and over is told
// its turns by the moves, and the moves can only say so because the tags are
// there to be written -- so a group whose edges somebody listens for keeps
// them even where nothing will ever ask where it stood.
//
// Which groups a character lies inside is not a reason: that is read off the
// whole machine before anything is taken out of it.
// The groups inside one place whose edges its type listens for.
//
// Kept apart from the walk over places below so that the two lists of groups
// -- the places and what is inside each of them -- are never expanded
// together.
template <class Type, fixed_string Format, std::size_t Group>
[[nodiscard]] consteval std::uint64_t edges_listened_for() {
  using how = gathering_of<Type, Format, Group>;
  using held = std::remove_cv_t<leaf_kind_of_output<Type, Group>>;
  std::uint64_t made = 0;
  if constexpr (how::folds && how::the_place) {
    using state_type = decltype(scan::scanner<held>{}.begin_groups());
    [&]<std::size_t... which>(std::index_sequence<which...>) {
      ([&] {
        if constexpr (takes_the_group_edges<held, which, state_type>()) {
          made |= std::uint64_t{1} << (Group + 1 + which);
        }
      }(), ...);
    }(std::make_index_sequence<groups_a_leaf_opens<held>()>{});
  }
  return made;
}

// Which tags the machine is worth writing at all.
//
// Said by the tag and not by the group, because a group can be worth half of
// itself. A mark somebody reads is a pair -- where it began and where it ended
// -- and both halves have to be written for the answer to be cut out of the
// subject. A group whose edges are only listened for is not read anywhere: all
// it owes is the opening, which is what tells a turn from the turn before it,
// and its closing is told by the moves, which say which groups a character
// lies inside. Kept by the group, such a group carried a closing mark written
// on the last character of every turn for nobody to read.
// The groups of a fold whose characters it is told about.
//
// Their marks are read by nobody, and for a while that was taken to mean the
// machine need not write them. It does: a tag is what holds two readings
// apart, and two readings that differ only in which group a character fell in
// are exactly the two a fold is told apart by. Merge them and the move still
// says which groups the character lies inside -- it says the wrong ones, the
// ones of whichever reading survived. `\(((_+)((A)|(B)))*\))` then answers 11
// where it means 21, because the B is announced as an A.
//
// So a group whose characters go to a fold keeps its marks, unread as they
// are. What is left to save is the groups a fold hears nothing about.
template <class Type, fixed_string Format, std::size_t Group>
[[nodiscard]] consteval std::uint64_t characters_told_of() {
  using how = gathering_of<Type, Format, Group>;
  using held = std::remove_cv_t<leaf_kind_of_output<Type, Group>>;
  std::uint64_t made = 0;
  if constexpr (how::folds && how::the_place) {
    using state_type = decltype(scan::scanner<held>{}.begin_groups());
    [&]<std::size_t... which>(std::index_sequence<which...>) {
      ([&] {
        if constexpr (takes_group_characters<held, which, state_type>) {
          made |= std::uint64_t{1} << (Group + 1 + which);
        }
      }(), ...);
    }(std::make_index_sequence<groups_a_leaf_opens<held>()>{});
  }
  return made;
}

// Every group a fold is told the characters of, in one mask.
template <class Type, fixed_string Format>
[[nodiscard]] consteval std::uint64_t groups_told_of() {
  std::uint64_t made = 0;
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    ((made |= characters_told_of<Type, Format, group>()), ...);
  }(std::make_index_sequence<groups_of_output<Type>()>{});
  return made;
}

// Whether a machine still shows every group a fold is told about.
//
// This is what taking a tag away can cost, and the cost does not look like an
// error. Two groups that are alternatives of each other -- `(A)|(B)` -- are
// told apart by nothing but their tags, so with the tags gone their positions
// fold together and one of them wins: the machine still says which groups a
// character lies inside, and for a B it says the A. A group that has lost that
// argument does not appear in any move at all, which is a thing that can be
// looked for.
//
// A fold that tells its groups apart by the character rather than by the group
// -- `(X|Y)` read as one group -- loses nothing, and that is the common shape.
// It keeps the trimming; the other one does not.
template <class Type, fixed_string Format, auto& Automaton>
[[nodiscard]] consteval bool every_told_group_is_seen() {
  const std::uint64_t told = groups_told_of<Type, Format>();
  for (std::size_t group = 0; group < 64; ++group) {
    if (((told >> group) & 1) == 0) continue;
    bool seen = false;
    for (std::size_t state = 0; state < Automaton.states.size(); ++state) {
      const auto& here = Automaton.states[state];
      for (std::size_t move = 0; move < here.range_count; ++move) {
        if ((here.ranges[move].groups_open & (std::uint64_t{1} << group)) != 0) {
          seen = true;
        }
      }
    }
    if (!seen) return false;
  }
  return true;
}

template <class Type, fixed_string Format, auto& Automaton>
[[nodiscard]] consteval std::uint64_t tags_that_matter() {
  const std::uint64_t read = groups_whose_mark_is_read<Type, Format, Automaton>();
  std::uint64_t edges = 0;
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    ((edges |= edges_listened_for<Type, Format, group>()), ...);
  }(std::make_index_sequence<groups_of_output<Type>()>{});
  std::uint64_t made = 0;
  for (std::size_t group = 0; group < 32; ++group) {
    const std::uint64_t opening = std::uint64_t{1} << (2 * group);
    const std::uint64_t closing = std::uint64_t{1} << (2 * group + 1);
    if (((read >> group) & 1) != 0) {
      made |= opening | closing;
    } else if (((edges >> group) & 1) != 0) {
      made |= opening;
    }
  }
  return made;
}

// The tags the machine is built with, and a check that keeping so few has not
// cost it something else.
//
// A tag is not only a mark somebody reads: it is what holds two readings
// apart. Take away the tags of a group nobody asks the position of, and two
// readings that differed in nothing else become one -- and then a move can no
// longer say which groups the character it reads lies inside. That is exactly
// what a fold is told, so a fold beside such a reading stops being told which
// of its groups a character fell in, and answers with the wrong number rather
// than with an error.
//
// The question was asked of the whole machine and the answer used for the
// trimmed one, which is the mistake. It is asked of the machine that will be
// walked now, and where trimming would cost it that, nothing is trimmed.
template <class Type, fixed_string Format, bool Cut>
inline constexpr std::uint64_t tags_worth_keeping = [] {
  constexpr std::uint64_t slim =
      tags_that_matter<Type, Format,
                       streaming_automaton_whole<Type, Format, Cut>>();
  // The groups a fold is told the characters of, both halves of each.
  constexpr std::uint64_t told = [] {
    const std::uint64_t groups = groups_told_of<Type, Format>();
    std::uint64_t made = 0;
    for (std::size_t group = 0; group < 32; ++group) {
      if (((groups >> group) & 1) != 0) {
        made |= (std::uint64_t{1} << (2 * group)) |
                (std::uint64_t{1} << (2 * group + 1));
      }
    }
    return made;
  }();
  constexpr std::uint64_t wanted =
      every_told_group_is_seen<Type, Format,
                               packed_text_automaton<spread_text<Type, Format>,
                                                     false, Cut, slim>>()
          ? slim
          : (slim | told);
  if constexpr (every_move_says_the_groups<
                    packed_text_automaton<spread_text<Type, Format>, false, Cut,
                                          wanted>>()) {
    return wanted;
  } else {
    return ~std::uint64_t{0};
  }
}();

template <class Type, fixed_string Format, bool Cut = true>
inline constexpr auto& streaming_automaton =
    packed_text_automaton<spread_text<Type, Format>, false, Cut,
                          tags_worth_keeping<Type, Format, Cut>>;

// Which groups' positions anybody will read.
//
// A place is always one: where it stood is how a field is cut out of the
// subject, and whether it stood anywhere at all is how a group that took no
// part is told from one that did. The groups inside a place are another
// matter. Where the machine says what each character lies inside, a fold hears
// what opened and what closed from the moves themselves and never asks where.
// Nothing reads those positions then -- and writing them is not merely a store
// on nearly every character: it makes every run a run that writes, which is a
// run the vectors cannot step over.
template <class Type, fixed_string Format, auto& Automaton>
[[nodiscard]] consteval std::uint64_t groups_whose_place_is_read() {
  std::uint64_t made = 0;
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    ([&] {
      made |= std::uint64_t{1} << group;
      using how = gathering_of<Type, Format, group>;
      using held = std::remove_cv_t<leaf_kind_of_output<Type, group>>;
      constexpr std::size_t inside = groups_a_leaf_opens<held>();
      // Asked in steps, because only a fold has groups to be told about and
      // only a fold has a state to be told into.
      constexpr bool a_fold_of_its_own =
          how::folds && how::the_place && !how::place_repeats &&
          every_move_says_the_groups<Automaton>();
      constexpr bool told_by_the_moves = [] {
        if constexpr (a_fold_of_its_own) {
          using state_type = decltype(scan::scanner<held>{}.begin_groups());
          return !any_group_taken_whole<held, state_type>();
        } else {
          return false;
        }
      }();
      // Asked of the place, not of what stands inside it. `inside` counts the
      // groups of the whole leaf, which is an answer about a place -- and a
      // group standing inside a fold is not a place, so what it counted was
      // the groups that follow it. Every group inside a fold claimed the
      // marks of the groups after it, the claims overlapped, and a fold
      // whose groups nobody asks about kept every mark it has -- a machine
      // writing tags on every character for nobody to read.
      if constexpr (how::the_place && !told_by_the_moves) {
        for (std::size_t which = 0; which < inside; ++which) {
          made |= std::uint64_t{1} << (group + 1 + which);
        }
      }
    }(), ...);
  }(std::make_index_sequence<groups_of_output<Type>()>{});
  return made;
}

// Whether a fold in this shape can be gathered by the walk over characters in
// a row.
//
// It can where every move of the machine says what its character lies inside:
// then the fold does not follow a reading, does not live at a register, and
// nothing about it needs the positions -- so the walk that steps over runs in
// vectors can gather it, which is the walk everything else is read by.
//
// A list still cannot. Its elements are handed over turn by turn, and which
// turn a gathering belongs to is exactly what the registers are keeping
// straight.
template <class Type, fixed_string Format>
[[nodiscard]] consteval bool a_fold_the_walk_can_keep() {
  if constexpr (holds_a_range<Type>()) {
    return false;
  } else if constexpr (!holds_a_fold<Type>()) {
    return false;
  } else {
    return every_move_says_the_groups<packed_automaton<Type, Format>>();
  }
}

// Whether the gathering for a group can live in the walk rather than at a
// register.
//
// The same question as for a fold, asked of a place that gathers characters:
// where every move says what its character lies inside, whether this place is
// open is a fact about the move, so nothing has to be read to find out and
// nothing has to follow a reading. A place taken over and over is left out --
// there the gathering is handed away turn by turn, and which turn it belongs
// to is what the registers are keeping straight.

}  // namespace scan::detail

#undef SCAN_FORCE_INLINE
