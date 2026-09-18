// Generated from the module interface unit of the same name. Do not edit.
#pragma once
// The value, put together out of what the walk found.
//
// One road for a subject that can be pointed at and one for a subject that
// cannot, and the same questions asked of the output either way: which place is
// which, what finishes it, and what it hands back when something in it would
// not read.


#include <algorithm>
#include <array>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include "scan_tre.hpp"
#include "scan_compiler.hpp"
#include "scan_runtime.hpp"
#include "scan_shape_walk.hpp"

#if defined(_MSC_VER)
#define SCAN_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define SCAN_FORCE_INLINE [[gnu::always_inline]] inline
#else
#define SCAN_FORCE_INLINE inline
#endif

 namespace scan::detail {
template <class Type, fixed_string Format,
          class ToldType = scan::default_context_t>
struct shape_turns {
  using held = std::remove_cv_t<Type>;
  static constexpr std::size_t places = groups_of_output<held>();
  using gatherings_type =
      decltype(make_scanner_state_told<held, Format>(
          std::declval<const ToldType&>()));

  constexpr shape_turns() = default;
  constexpr explicit shape_turns(const ToldType& given)
      : gatherings(make_scanner_state_told<held, Format>(given)),
        told(given) {}

  gatherings_type gatherings =
      make_scanner_state_told<held, Format>(ToldType{});
  // An element that did not read, kept until there is somebody to hand it to:
  // a turn ends in the middle of a walk, where there is nowhere to say so.
  std::optional<shape_failure<held>> went_wrong{};
  // Which places have been opened since they were last read out. A choice says
  // which branch ran by which mark opened; a list says whether a turn is going.
  std::array<bool, places == 0 ? 1 : places> took{};
  // What this shape was told. A turn ends inside the walk, where the caller is
  // long out of reach, so what was said at the door is kept here until the
  // element a turn makes asks for it.
  [[no_unique_address]] ToldType told{};
};

template <class ShapeType>
struct gathered_by_a_fold {
  ShapeType& state;

  template <std::size_t Place>
  [[nodiscard]] constexpr const auto& gathering() const {
    return std::get<Place>(state.gatherings);
  }

  template <std::size_t Place>
  [[nodiscard]] constexpr const auto& list() const {
    return std::get<Place>(state.gatherings);
  }

  template <std::size_t Place>
  [[nodiscard]] constexpr bool took_part() const {
    return state.took[Place];
  }

  // A fold at this place has been told everything as it happened -- the walk
  // hands the edges on and this hands them further -- so there is nothing left
  // to run into it here.
  template <std::size_t Place, class Held>
  [[nodiscard]] constexpr auto fold_at() const {
    return std::get<Place>(state.gatherings);
  }
};

// Where a group of a shape belongs: the place it is, or the place it is inside
// of and which of that type's own groups it is.
template <class Type, std::size_t Group>
inline constexpr std::size_t shape_place_of =
    Group - leaf_offset_of_output<std::remove_cv_t<Type>, Group>;

template <class Type, std::size_t Group>
inline constexpr std::size_t shape_place_inside =
    leaf_offset_of_output<std::remove_cv_t<Type>, Group>;

// One character, to the place it fell in -- or to the type standing at that
// place, where the group is one of that type's own. A list's own group holds no
// characters: what is inside it are the places of one turn, and they take them.
template <class Type, fixed_string Format, std::size_t Group, class ShapeType>
constexpr void push_shape_place(ShapeType& state, char letter) {
  using held = std::remove_cv_t<Type>;
  constexpr std::size_t place = shape_place_of<held, Group>;
  constexpr std::size_t inside = shape_place_inside<held, Group>;
  using stands_for = std::remove_cv_t<leaf_kind_of_output<held, Group>>;
  if constexpr (inside != 0) {
    push_one_group<stands_for, inside - 1>(
        std::get<place>(state.gatherings).here.state, letter);
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
template <class Type, fixed_string Format, std::size_t Group, class ShapeType>
constexpr void open_shape_place(ShapeType& state) {
  using held = std::remove_cv_t<Type>;
  constexpr std::size_t place = shape_place_of<held, Group>;
  constexpr std::size_t inside = shape_place_inside<held, Group>;
  if constexpr (inside != 0) {
    using stands_for = std::remove_cv_t<leaf_kind_of_output<held, Group>>;
    open_one_group<stands_for, inside - 1>(
        std::get<place>(state.gatherings).here.state);
  } else {
    state.took[place] = true;
  }
}

// A place closed. Where it is a list, that is one turn: the element is put
// together out of the places inside it, added to the list, and those places
// begin again for the turn that may follow.
template <class Type, fixed_string Format, std::size_t Group,
          class FailureType, class ShapeType>
constexpr void close_shape_place(ShapeType& state,
                                 std::optional<FailureType>& failed) {
  using held = std::remove_cv_t<Type>;
  constexpr std::size_t place = shape_place_of<held, Group>;
  constexpr std::size_t inside = shape_place_inside<held, Group>;
  using stands_for = std::remove_cv_t<leaf_kind_of_output<held, Group>>;
  if constexpr (inside != 0) {
    close_one_group<stands_for, inside - 1>(
        std::get<place>(state.gatherings).here.state);
  } else if constexpr (scanned_as_range<stands_for>) {
    using element = std::remove_cvref_t<std::ranges::range_value_t<stands_for>>;
    if (!state.took[place + 1]) return;
    // Told nothing, this is the walk it always was: the same call, with
    // nothing in the place of a context. Told something, the element is made
    // with what its place was given.
    auto one = [&] {
      if constexpr (std::same_as<std::remove_cvref_t<decltype(state.told)>,
                                 scan::default_context_t>) {
        return finish_value<held, element, place + 1, false, FailureType>(
            gathered_by_a_fold<ShapeType>{state}, nullptr);
      } else {
        return finish_value<held, element, place + 1, false, FailureType>(
            gathered_by_a_fold<ShapeType>{state}, nullptr,
            context_at_group<held, Group>(state.told));
      }
    }();
    if (!one) {
      if (!failed) failed = std::move(one).error();
      return;
    }
    append_to(std::get<place>(state.gatherings), std::move(*one));
    // The turn is over: what its places gathered belongs to the element that
    // has just been taken, and the next turn starts from nothing.
    static constexpr auto spread = spread_of<held, Format>();
    [&]<std::size_t... inside>(std::index_sequence<inside...>) {
      ((void)[&] {
        constexpr std::size_t which = place + 1 + inside;
        std::get<which>(state.gatherings) =
            gathering_of<held, Format, which>::begin(
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
template <class Type, fixed_string Format, bool Cut = true,
          class FailureType = failure_for<Type>>
class stream_state {
 private:
  static_assert(
      !a_flat_reader_inside<Type>(),
      "a type built from its groups after the match cannot read a stream: "
      "there is nothing left to point at by the time it would be handed them "
      "-- give it begin_groups and push_group to be told its groups as they "
      "arrive");
  // The whole machine, marks and all.
  //
  // Everything else walks the machine with the marks nobody reads taken out --
  // it hears what happened to a place from the moves. This reading has no
  // moves to hear it from: it steps the states one character at a time and
  // asks the positions, so it needs every mark the expression writes.
  inline static constexpr const auto& automaton =
      streaming_automaton_whole<Type, Format, Cut>;
  inline static constexpr std::size_t field_count = groups_of_output<Type>();
  // One gathering per register, because a gathering follows the register it
  // belongs to and there is no arithmetic that says which registers go
  // together.
  inline static constexpr std::size_t slot_count = automaton.register_count;
  using field_states = register_state<Type, Format>;

 public:
  constexpr stream_state() {
    scanner_states_ = make_register_states<Type, Format, automaton>();
    std::ranges::fill(registers_, scan::tre::negative_tag);
    execute_initial<automaton>(registers_, std::ptrdiff_t{0});
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
    collect_elements<Type, Format, automaton, FailureType>(
        state_, registers_, scanner_states_, transition->commands,
        transition->command_count, std::make_index_sequence<field_count>{},
        nullptr, failed_);
    execute_commands(transition->commands, transition->command_count, registers_,
                     ++position_);
    advance_scanners<Type, Format, automaton>(
        symbol, transition->target, state_, position_, registers_,
        scanner_states_,
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
  template <std::size_t Field>
  [[nodiscard]] constexpr const auto& gathering() const {
    const std::size_t here =
        state_ == packed_range<0>::reject ? automaton.initial : state_;
    // The same question the reading asks: a field still being typed is where
    // it is being gathered, and one that has closed is the copy taken then.
    const auto& reading = automaton.states[here].readings[0];
    const std::uint32_t open = reading[Field * 2];
    const std::uint32_t close = reading[Field * 2 + 1];
    const bool still_reading = registers_[close] < registers_[open];
    return std::get<Field>(scanner_states_[still_reading ? open : close]);
  }

  // Whether that field is being read right now: begun and not yet ended.
  template <std::size_t Field>
  [[nodiscard]] constexpr bool reading() const {
    if (state_ == packed_range<0>::reject) return false;
    const auto& packed = automaton.states[state_];
    if (packed.reading_count == 0) return false;
    const std::uint32_t open = packed.readings[0][Field * 2];
    const std::uint32_t close = packed.readings[0][Field * 2 + 1];
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

  [[nodiscard]] constexpr std::expected<Type, FailureType> finish() const& {
    stream_state copy = *this;
    return std::move(copy).finish();
  }

  [[nodiscard]] constexpr std::expected<Type, FailureType> finish() && {
    if (failed_) return std::unexpected(std::move(*failed_));
    if (state_ == packed_range<0>::reject) {
      return std::unexpected(scan::as_a_failure<FailureType>(
          no_match<>("input does not match scan expression")));
    }
    const auto slot = automaton.states[state_].accepting_slot;
    if (slot == packed_state<0, 0, 0>::not_accepting) {
      return std::unexpected(scan::as_a_failure<FailureType>(
          no_match<>("input does not match scan expression")));
    }
    // The reading that accepted says which register holds each value. Nothing
    // is written here: the commands that end a match are not run by this
    // machine, so a group that never closed is read from where it was being
    // gathered, which is what the registers say.
    const auto& reached = automaton.states[state_];
    // Nothing to point at: this machine is fed and never holds the subject.
    return finish_value<Type, Type, 0, true, FailureType>(
        by_the_registers<Type, Format>(reached.readings[slot], scanner_states_,
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
  std::optional<FailureType> failed_;
};

// Which gatherings a group is added to where the machine stands.
//
// A state stands in several readings at once and they can share a register, so
// what a character is added to is the set of the registers holding the group's
// opening across those readings, each of them once. Walking the readings to
// find that out on every character is what a machine that does not know where
// it stands has to do; where the state is known, the set is known, and this is
// it.
template <auto& Automaton, std::size_t State, std::size_t Group>
inline constexpr auto gathered_at = [] consteval {
  constexpr const auto& packed = Automaton.states[State];
  struct answer {
    std::array<std::uint32_t, Automaton.register_count> at{};
    std::size_t count = 0;
  };
  answer said;
  for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
    const std::uint32_t opening = packed.readings[reading][Group * 2];
    bool already = false;
    for (std::size_t at = 0; at < said.count; ++at) {
      if (said.at[at] == opening) already = true;
    }
    if (!already) said.at[said.count++] = opening;
  }
  return said;
}();

// The cold slots, each begun with what its place was told. A slot that has
// nothing to be told is begun the way it always was.
template <class SlotType, class ToldType>
[[nodiscard]] constexpr SlotType made_slot(const ToldType& told) {
  if constexpr (requires { SlotType(told); }) {
    return SlotType(told);
  } else {
    static_cast<void>(told);
    return SlotType{};
  }
}

// The cold slots, taken out of the slots as they are begun for their places.
// Taken rather than made: make_slots asks each place what it was told, and a
// slot built from the whole carrier instead finds no constructor that takes it
// and quietly begins the untold way -- which is where a context said at a
// place used to stop on its way to a scanner that gathers.
template <class ColdType, class AllType, std::size_t... Which>
[[nodiscard]] constexpr ColdType cold_out_of(AllType& all,
                                              std::index_sequence<Which...>) {
  return std::tuple_cat([&] {
    if constexpr (keeps_characters<std::tuple_element_t<Which, AllType>>) {
      return std::tuple<std::tuple_element_t<Which, AllType>>(
          std::move(std::get<Which>(all)));
    } else {
      return std::tuple<>{};
    }
  }()...);
}

template <class Type, fixed_string Format, class MarkType, class ColdType,
          class ToldType>
[[nodiscard]] constexpr ColdType made_cold_at_places(const ToldType& told) {
  auto all = make_slots<Type, Format, MarkType, ToldType>(told);
  return cold_out_of<ColdType>(
      all, std::make_index_sequence<std::tuple_size_v<decltype(all)>>{});
}

template <class ColdType, class ToldType>
[[nodiscard]] constexpr ColdType made_cold(const ToldType& told) {
  if constexpr (requires { std::tuple_size<ColdType>::value; }) {
    return [&]<std::size_t... k>(std::index_sequence<k...>) {
      return ColdType{made_slot<std::tuple_element_t<k, ColdType>>(told)...};
    }(std::make_index_sequence<std::tuple_size_v<ColdType>>{});
  } else {
    static_cast<void>(told);
    return ColdType{};
  }
}

// The gatherer the format reader hands to the walk: the fields' own scanners,
// following the moves.
//
// What the machine that can be stopped and started looks up on every character
// -- the state, its runs, the commands of the run it took, the readings that
// say which register holds which group -- is a constant here, because the walk
// knows where it stands. The work each character does is what it was: apply
// what the move writes to the gatherings, and give the character to the fields
// that are open.
template <class Type, fixed_string Format, auto& Automaton,
          bool Pointable = false, class MarkKind = std::ptrdiff_t,
          class ToldType = scan::default_context_t>
class field_gatherer {
 public:
  using plain_folds_type = decltype(make_slots<Type, Format, MarkKind,
                                             ToldType>());
  // Where the gathering slots lie. They are not part of the gatherer: the
  // walk carries the gatherer from character to character, and a slot that
  // gathers characters would hold the whole of it down in memory.
  using cold_type = cold_slots_of<plain_folds_type>;

 private:
  using warm_type = warm_slots_of<plain_folds_type>;

 public:
  // A type built from its groups once the match is over is handed views of the
  // subject. Where the subject is gone as it is read there is nothing to give
  // it, and holding the characters until the end to give it something would be
  // a hold with no bound.
  static_assert(
      Pointable || !a_flat_reader_inside<Type>(),
      "a type built from its groups after the match needs a subject that can "
      "be pointed at: give it begin_groups and push_group to be told its "
      "groups as they are read, or scan it from something contiguous");
  static constexpr std::size_t field_count = groups_of_output<Type>();
  // Whether anything at all is gathered at a register.
  //
  // Where nothing is -- every place kept by the walk, every group inside one
  // of those told through it -- there is no reason to carry a gathering per
  // register, and carrying one is not free: it is what the walk begins by
  // clearing, and it is large enough that the optimiser will not put any of it
  // in a register of the processor.
  static constexpr bool nothing_at_a_register = [] {
    return [&]<std::size_t... group>(std::index_sequence<group...>) {
      return (true && ... && [] {
        using how = gathering_of<Type, Format, group>;
        if constexpr (how::folds && how::inside) return true;
        if constexpr (gathers_in_the_walk<Type, Format, Automaton, group>()) {
          return true;
        }
        // A list the walk gathers, and the turn of it being gathered, are both
        // held by the walk: neither is at a register, and where nothing else
        // is either there is no array of them to carry, to clear, or to take
        // a copy of at every move.
        if constexpr (list_gathers_in_the_walk<Type, Format, Automaton,
                                               group>()) {
          return true;
        }
        if constexpr (group > 0 &&
                      list_gathers_in_the_walk<Type, Format, Automaton,
                                               group == 0 ? 0 : group - 1>()) {
          return true;
        }
        if constexpr (how::folds && how::the_place && !how::place_repeats &&
                      every_move_says_the_groups<Automaton>()) {
          return true;
        }
        return false;
      }());
    }(std::make_index_sequence<groups_of_output<Type>()>{});
  }();

  using states_type =
      std::array<register_state<Type, Format, MarkKind, ToldType>,
                 nothing_at_a_register ? 0 : Automaton.register_count>;

  // A slot by its number: a warm one out of this gatherer's own tuple, a
  // gathering one out of the tuple the walk owns.
  template <std::size_t Which>
  [[nodiscard]] constexpr auto& slot() {
    using kind = std::tuple_element_t<Which, plain_folds_type>;
    if constexpr (keeps_characters<kind>) return std::get<kind>(*cold_);
    else return std::get<kind>(warm_);
  }
  template <std::size_t Which>
  [[nodiscard]] constexpr const auto& slot() const {
    using kind = std::tuple_element_t<Which, plain_folds_type>;
    if constexpr (keeps_characters<kind>) return std::get<kind>(*cold_);
    else return std::get<kind>(warm_);
  }
  // Все слоты вместе -- для того одного места в конце, где нужен целый набор.
  [[nodiscard]] constexpr plain_folds_type all_slots() const {
    plain_folds_type made{};
    [&]<std::size_t... which>(std::index_sequence<which...>) {
      ((std::get<which>(made) = slot<which>()), ...);
    }(std::make_index_sequence<std::tuple_size_v<plain_folds_type>>{});
    return made;
  }

  // The gathering slots lie outside, so there is no gatherer without them:
  // there is no default constructor, and every place that makes one has to
  // say where they lie. Making that the type's rule rather than a thing to
  // remember is what keeps a gatherer from ever pointing at nothing.
  // Told at the door, because the slots and the register states are made in
  // this body: a context that arrives after the gatherer is built arrives after
  // every state it was meant for has already begun.
  constexpr explicit field_gatherer(cold_type& cold,
                                    const ToldType& told = ToldType{})
      : cold_(&cold), told_(told) {
    begin_again();
  }

  // The slots, made where the places were told rather than made empty and
  // assigned afterwards. A container that keeps a resource does not take the
  // other one's resource when it is assigned, so a slot that begins empty
  // stays on the default resource whatever is put in it later.
  template <class Other>
  [[nodiscard]] static constexpr cold_type cold_for(const Other& told) {
    return made_cold_at_places<Type, Format, MarkKind, cold_type>(told);
  }

  // Where the gathering slots lie, said again.
  //
  // The gatherer holds where they are, not the slots themselves, so a gatherer
  // that has been carried to a new home has to be told the new one.
  constexpr void lives_in(cold_type& cold) { cold_ = &cold; }

  // What the places were told, for the slots this makes as the walk goes.
  constexpr void was_told(const ToldType& told) { told_ = told; }

  // Gathering from the beginning, leaving the slots where they lie.
  //
  // Between one match and the next the gathering starts over while the room
  // the slots lie in stays put. Assigning a fresh gatherer over this one would
  // start the gathering over too, but it would also carry over where that
  // fresh one thinks its slots are, which is nowhere.
  constexpr void begin_again() {
    turns_open_ = 0;
    made_.reset();
    failed_.reset();
    text_ = nullptr;
    auto all = make_slots<Type, Format, MarkKind, ToldType>(told_);
    [&]<std::size_t... which>(std::index_sequence<which...>) {
      // Begun where they stand rather than assigned over: a slot that keeps a
      // resource keeps its own when it is assigned to, and these were made on
      // whatever their places were told about.
      ((begin_gathering_at(slot<which>(), std::move(std::get<which>(all)))),
       ...);
    }(std::make_index_sequence<std::tuple_size_v<plain_folds_type>>{});
    if constexpr (!nothing_at_a_register) {
      // Built where it stands, for the same reason: assigning these would
      // leave every container on whatever resource it was made with.
      std::destroy_at(&states_);
      std::construct_at(
          &states_,
          make_register_states<Type, Format, Automaton, MarkKind, ToldType>(
              told_));
    }
  }

 private:
  // The folds that answer for every reading at once, kept here rather than at
  // a register. Where the machine says what each character lies inside, every
  // reading would be told the same things, so there is one fold to tell and it
  // can live in the walk -- which is what lets it stay in a register of the
  // processor rather than in an array indexed by a number read from memory.

 public:

  // A list takes in the turn that has just ended, and what says it ended is
  // the registers as they stood before this move wrote anything.
  template <std::size_t State, std::size_t Move, class RegistersType>
  constexpr void moving(const RegistersType& registers, auto) {
    constexpr const auto& taken = Automaton.states[State].ranges[Move];
    collect_elements<Type, Format, Automaton, failure_for<Type>>(
        State, registers, states_, taken.commands, taken.command_count,
        std::make_index_sequence<field_count>{}, text_, failed_, told_);
  }

  // Whether anything open on this state's run would rather have it whole.
  //
  // A gathering that appends by the length of what it is given, or a group
  // whose type takes a run in one call, is better handed the run: the walk
  // finds where it ends and hands it over once. Where nothing would -- every
  // open group is told a character at a time whatever happens -- finding the
  // end first and walking the run again to say what is in it is one pass more
  // than the reading needs, and for a run of two or three characters that pass
  // is most of what the run costs.
  // Whether anything at all is open to be handed this state's run.
  //
  // A state can keep a run with nothing gathering inside it: a head walked
  // over, a field whose characters nobody reads. Handing that run over is a
  // call that does nothing, and finding where it ends first is a pass over
  // characters the walk was going to pass over anyway. In words that pass is
  // thirty-two characters to a step and pays for itself; read one character at
  // a time it is the same walk done twice.
  template <std::size_t State>
  [[nodiscard]] static consteval bool anything_takes_the_run() {
    constexpr std::size_t staying = staying_move<Automaton, State>();
    if constexpr (staying == no_move) {
      return false;
    } else {
      return [&]<std::size_t... group>(std::index_sequence<group...>) {
        return (false || ... || in_one_call<State, staying, group>());
      }(std::make_index_sequence<field_count>{});
    }
  }

  // Whether this place, open here, takes a run in one call rather than a
  // character at a time. Room written into with one append does; a fold told
  // what each character was does not, and handing it a run only moves the
  // walking of that run from one place to another.
  template <std::size_t State, std::size_t Move, std::size_t Group>
  [[nodiscard]] static consteval bool in_one_call() {
    constexpr const auto& taken = Automaton.states[State].ranges[Move];
    if constexpr ((taken.groups_open & (std::uint64_t{1} << Group)) == 0) {
      return false;
    } else {
      using held_type = std::remove_cv_t<leaf_kind_of_output<Type, Group>>;
      return scanned_as_range<held_type> || keeps_characters<held_type>;
    }
  }

  template <std::size_t State>
  [[nodiscard]] static consteval bool wants_a_run_whole() {
    constexpr std::size_t staying = staying_move<Automaton, State>();
    if constexpr (staying == no_move) {
      return true;
    } else {
      return [&]<std::size_t... group>(std::index_sequence<group...>) {
        return (false || ... || takes_it_whole<State, staying, group>());
      }(std::make_index_sequence<field_count>{});
    }
  }

  template <std::size_t State, std::size_t Move, std::size_t Group>
  [[nodiscard]] static consteval bool takes_it_whole() {
    using held_type = leaf_kind_of_output<Type, Group>;
    using how = gathering_of<Type, Format, Group>;
    constexpr const auto& taken = Automaton.states[State].ranges[Move];
    if constexpr (scanned_as_range<held_type>) {
      return true;
    } else if constexpr (gathers_in_the_walk<Type, Format, Automaton, Group>()) {
      return (taken.groups_open & (std::uint64_t{1} << Group)) != 0;
    } else if constexpr (how::folds && how::the_place && !how::place_repeats &&
                         every_move_says_the_groups<Automaton>()) {
      using held = std::remove_cv_t<held_type>;
      using state_type = decltype(scan::scanner<held>{}.begin_groups());
      return [&]<std::size_t... which>(std::index_sequence<which...>) {
        return (false || ... || [] {
          constexpr std::uint64_t bit = std::uint64_t{1} << (Group + 1 + which);
          if constexpr ((taken.groups_open & bit) == 0) {
            return false;
          } else {
            return takes_group_runs<held, which, state_type> ||
                   takes_the_group_whole<held, which, state_type>;
          }
        }());
      }(std::make_index_sequence<groups_a_leaf_opens<held>()>{});
    } else {
      return true;
    }
  }

  // Whether exactly one place does the work of this state's run, and does it a
  // character at a time.
  template <std::size_t State, std::size_t Group>
  [[nodiscard]] static consteval bool this_place_reads_the_run() {
    constexpr std::size_t staying = staying_move<Automaton, State>();
    if constexpr (staying == no_move) {
      return false;
    } else if constexpr (takes_it_whole<State, staying, Group>()) {
      return false;
    } else {
      using held_type = leaf_kind_of_output<Type, Group>;
      using how = gathering_of<Type, Format, Group>;
      if constexpr (!(how::folds && how::the_place && !how::place_repeats &&
                      every_move_says_the_groups<Automaton>()) ||
                    scanned_as_range<held_type>) {
        return false;
      } else {
        constexpr const auto& taken = Automaton.states[State].ranges[staying];
        using held = std::remove_cv_t<held_type>;
        using state_type = decltype(scan::scanner<held>{}.begin_groups());
        return [&]<std::size_t... which>(std::index_sequence<which...>) {
          return (false || ... || [] {
            constexpr std::uint64_t bit = std::uint64_t{1} << (Group + 1 + which);
            return (taken.groups_open & bit) != 0 &&
                   takes_group_characters<held, which, state_type>;
          }());
        }(std::make_index_sequence<groups_a_leaf_opens<held>()>{});
      }
    }
  }

  // And whether every other place has nothing to do with it.
  template <std::size_t State, std::size_t Group>
  [[nodiscard]] static consteval bool this_place_is_idle() {
    constexpr std::size_t staying = staying_move<Automaton, State>();
    using how = gathering_of<Type, Format, Group>;
    if constexpr (how::folds && how::inside) {
      // A group inside a fold is its place's business and never its own: it is
      // open whenever the place is standing in it, and counting it as work of
      // its own is counting the same work twice.
      return true;
    } else if constexpr (staying == no_move) {
      return false;
    } else {
      constexpr const auto& taken = Automaton.states[State].ranges[staying];
      using held_type = leaf_kind_of_output<Type, Group>;
      constexpr std::size_t inside =
          groups_a_leaf_opens<std::remove_cv_t<held_type>>();
      constexpr std::uint64_t mine = [] {
        std::uint64_t made = std::uint64_t{1} << Group;
        for (std::size_t which = 0; which < inside; ++which) {
          made |= std::uint64_t{1} << (Group + 1 + which);
        }
        return made;
      }();
      return (taken.groups_open & mine) == 0 &&
             (taken.groups_reopened & mine) == 0;
    }
  }

  // Which place that is, where there is exactly one.
  template <std::size_t State>
  [[nodiscard]] static consteval std::size_t the_place_that_reads_the_run() {
    std::size_t found = no_move;
    std::size_t count = 0;
    [&]<std::size_t... group>(std::index_sequence<group...>) {
      ([&] {
        if constexpr (this_place_reads_the_run<State, group>()) {
          found = group;
          ++count;
        } else if constexpr (!this_place_is_idle<State, group>()) {
          ++count;
          ++count;
        }
      }(), ...);
    }(std::make_index_sequence<field_count>{});
    return count == 1 ? found : no_move;
  }

  // The run this state keeps, read and handed over in one loop -- and the loop
  // is here rather than in the walk.
  //
  // Where the walk owns the loop, what the type gathers into is an object the
  // loop writes through, and a store to it is a store on every character: the
  // optimiser cannot keep it in a register of the processor because it cannot
  // see where the loop will stop. Here it is a local. Taken out once, kept in a
  // register for the whole run, put back when the run ends -- which is what
  // somebody writing this reading by hand would have done without thinking
  // about it.
  template <std::size_t State, staying_class Klass>
    requires(the_place_that_reads_the_run<State>() != no_move)
  [[nodiscard]] SCAN_FORCE_INLINE constexpr const char* took_class(
      const char* from, const char* limit) {
    constexpr std::size_t group = the_place_that_reads_the_run<State>();
    constexpr std::size_t staying = staying_move<Automaton, State>();
    constexpr std::uint64_t now =
        Automaton.states[State].ranges[staying].groups_open;
    using held = std::remove_cv_t<leaf_kind_of_output<Type, group>>;
    auto& fold =
        slot<gathering_slot<Type, Format, group, MarkKind, ToldType>>();
    auto gathered = fold.here.state;
    const char* cursor = from;
    while (cursor != limit &&
           inside_of<Klass>(static_cast<unsigned char>(*cursor))) {
      const char letter = *cursor;
      [&]<std::size_t... which>(std::index_sequence<which...>) {
        ((void)[&] {
          constexpr std::uint64_t bit = std::uint64_t{1} << (group + 1 + which);
          if constexpr ((now & bit) != 0) {
            if constexpr (takes_group_characters<held, which,
                                                 decltype(gathered)>) {
              push_one_group<held, which>(gathered, letter);
            }
          }
        }(), ...);
      }(std::make_index_sequence<groups_a_leaf_opens<held>()>{});
      ++cursor;
    }
    fold.here.state = gathered;
    return cursor;
  }

  // A run the walk stepped over in vectors: the fields that are open take all
  // of it, which is one pass over the piece rather than one call a character.
  template <std::size_t State, class RegistersType>
  constexpr void took_run(const char* from, const char* to,
                          const RegistersType& registers, auto) {
    hand_run<State>(from, to, registers,
                    std::make_index_sequence<field_count>{});
  }

  template <std::size_t State, std::size_t Landed, std::size_t Move,
            class RegistersType>
  SCAN_FORCE_INLINE constexpr void moved(char letter,
                                         const RegistersType& registers,
                                         auto position) {
    // A move that writes nothing leaves the gatherings where they are, and
    // most of the characters of a subject are read by one: inside a field
    // nothing is written, which is what holding the tags back bought. So the
    // whole of what a move does to the gatherings is skipped for it, and what
    // is left is handing the character to the fields that are open.
    constexpr const auto& taken = Automaton.states[State].ranges[Move];
    if constexpr (State != Landed || staying_writes<Automaton, State>()) {
      advance_scanners<Type, Format, Automaton, false, true>(
          letter, Landed, State, position, registers, states_, taken.commands,
          taken.command_count, std::make_index_sequence<field_count>{}, text_,
          told_);
    }
    hand_over<taken.groups_open, taken.groups_reopened,
              open_on_entry<Automaton, State>(),
              taken.target == State && taken.groups_reopened == 0, Landed>(
        letter, registers, position,
        std::make_index_sequence<field_count>{});
    collect_turns_that_ended<Type, Format, Automaton, failure_for<Type>>(
        Landed, registers, states_, std::make_index_sequence<field_count>{},
        failed_);
  }

  // Where the walk ended is a constant, so the reading that accepted is one
  // too, and the value is put together here rather than looked for afterwards.
  // A walk keeps every place it passes, and the last one it keeps is the
  // answer -- so this runs more than once and the last of them wins, which is
  // what it has always done with the value. It has to do the same with a
  // failure: a place passed early where a field was not read yet is not this
  // reading's answer, and holding on to that would lose every match after it.
  template <std::size_t State, class RegistersType>
  constexpr void ended(const RegistersType& registers) {
    constexpr const auto& packed = Automaton.states[State];
    // What the walk kept is told where the walk stands, and then read from
    // where it is.
    //
    // Reading it runs one more step, by the positions, because the end of the
    // input is not a character and whatever is still open has to be closed.
    // Everything else was said by the moves as they were taken, so the fold is
    // set to the positions as they stand: nothing has moved since, and that
    // last step announces nothing twice.
    auto kept = all_slots();
    [&]<std::size_t... group>(std::index_sequence<group...>) {
      ([&] {
        using how = gathering_of<Type, Format, group>;
        if constexpr (how::folds && how::the_place && !how::place_repeats &&
                      every_move_says_the_groups<Automaton>()) {
          auto& one = std::get<gathering_slot<Type, Format, group, MarkKind, ToldType>>(kept);
          using held = std::remove_cv_t<leaf_kind_of_output<Type, group>>;
          constexpr std::size_t inside = groups_a_leaf_opens<held>();
          const auto& reading = packed.readings[packed.accepting_slot];
          for (std::size_t which = 0; which < inside; ++which) {
            one.here.told_at[which] =
                slot_read(registers, reading[(group + 1 + which) * 2]);
            one.here.ended_at[which] =
                slot_read(registers, reading[(group + 1 + which) * 2 + 1]);
          }
        }
      }(), ...);
    }(std::make_index_sequence<field_count>{});
    // Which places the walk kept: a fact about the shape and the machine,
    // worked out once here and carried with the gatherings.
    constexpr std::uint64_t mine = [] {
      std::uint64_t made = 0;
      [&]<std::size_t... group>(std::index_sequence<group...>) {
        ([&] {
          using how = gathering_of<Type, Format, group>;
          if constexpr (gathers_in_the_walk<Type, Format, Automaton, group>() ||
                        list_gathers_in_the_walk<Type, Format, Automaton,
                                                 group>() ||
                        (group > 0 &&
                         list_gathers_in_the_walk<Type, Format, Automaton,
                                                  group == 0 ? 0
                                                             : group - 1>()) ||
                        (how::folds && how::the_place && !how::place_repeats &&
                         every_move_says_the_groups<Automaton>())) {
            made |= std::uint64_t{1} << group;
          }
        }(), ...);
      }(std::make_index_sequence<field_count>{});
      return made;
    }();
    const kept_by_the_walk<decltype(kept), mine> mine_kept{kept, turns_open_};
    // What this costs, so that the next reader does not have to find it
    // again: the bundle handed over holds the walk's register file, and a
    // register file whose address any call has seen is one no compiler will
    // take apart. So the whole of it stays on the stack and is cleared before
    // a character is read -- 632 bytes for this pattern.
    //
    // Folding this one call in does not help: it makes another to finish the
    // parts, and that one is handed the same bundle. Nothing short of the
    // whole chain being written out lets the registers go, and the whole chain
    // is the reading written out again for every place it has.
    // Handed both readings: where the walk held each tag, and where the
    // ending put the ones it closed.
    //
    // A tag the walk closed stands where the reading says, all the way to the
    // end, and everything is read from there. A group still open where the
    // match ends is closed by the commands that end it and by nothing else,
    // and those write registers of their own -- so its closing is not in the
    // reading at all, and was read as a group that never closed. The two are
    // about two different moments, so both are carried and each is asked where
    // it is the one that knows.
    auto got = finish_value<Type, Type, 0, true>(
        by_the_registers<Type, Format>(packed.readings[packed.accepting_slot],
                                       states_, registers, mine_kept,
                                       packed.ending_reading),
        text_, told_);
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
  [[nodiscard]] constexpr std::expected<Type, failure_for<Type>> taken() {
    if (failed_) return std::unexpected(std::move(*failed_));
    if (!made_) {
      return std::unexpected(scan::as_a_failure<failure_for<Type>>(
          no_match<>("input does not match scan expression")));
    }
    return std::move(*made_);
  }

  [[nodiscard]] constexpr std::optional<failure_for<Type>>& went_wrong() {
    return failed_;
  }

  [[nodiscard]] constexpr std::optional<Type>& made() { return made_; }

  // Where the subject begins, for a walk that has all of it in front of it. A
  // fold reading such a subject is handed each of its groups whole.
  // Said once, where the subject is handed over, rather than on every
  // character: what a fold points at is the subject, and the subject does not
  // move.
  constexpr void points_at(const char* text) {
    text_ = text;
    [&]<std::size_t... group>(std::index_sequence<group...>) {
      ([&] {
        using how = gathering_of<Type, Format, group>;
        if constexpr (how::folds && how::the_place) {
          slot<gathering_slot<Type, Format, group, MarkKind, ToldType>>().here.text =
              text;
        }
      }(), ...);
    }(std::make_index_sequence<field_count>{});
  }

 private:
  template <std::size_t State, class RegistersType, std::size_t... Group>
  constexpr void hand_run(const char* from, const char* to,
                          const RegistersType& registers,
                          std::index_sequence<Group...>) {
    (hand_run_group<State, Group>(from, to, registers), ...);
  }

  template <std::size_t State, std::size_t Group, class RegistersType>
  constexpr void hand_run_group(const char* from, const char* to,
                                const RegistersType& registers) {
    using held_type = leaf_kind_of_output<Type, Group>;
    using how = gathering_of<Type, Format, Group>;
    if constexpr (scanned_as_range<held_type>) {
      return;
    } else if constexpr (how::folds && how::inside) {
      return;
    } else if constexpr (how::folds && how::the_place && !how::place_repeats &&
                         every_move_says_the_groups<Automaton>()) {
      // A run, handed to the groups it fell in, whole.
      //
      // Nothing is written across a run -- that is what makes it a run -- so
      // what is open at its first character is open at its last, and the move
      // that takes it says which groups those are. There is one question for
      // the whole run and, for a type that takes a run, one call.
      constexpr std::size_t staying = staying_move<Automaton, State>();
      if constexpr (staying != no_move) {
        constexpr std::uint64_t inside_now =
            Automaton.states[State].ranges[staying].groups_open;
        // A run that begins a group again on every character is a run of
        // turns, and a turn is not something to hand over in bulk: what the
        // type is told has to be what happened.
        constexpr std::uint64_t begins_again =
            Automaton.states[State].ranges[staying].groups_reopened;
        using held = std::remove_cv_t<held_type>;
        constexpr std::size_t inside = groups_a_leaf_opens<held>();
        auto& fold = slot<gathering_slot<Type, Format, Group, MarkKind, ToldType>>();
        const std::string_view run(from, static_cast<std::size_t>(to - from));
        [&]<std::size_t... which>(std::index_sequence<which...>) {
          ((void)[&] {
            if constexpr ((inside_now &
                           (std::uint64_t{1} << (Group + 1 + which))) != 0 &&
                          (begins_again &
                           (std::uint64_t{1} << (Group + 1 + which))) == 0) {
              // A group that goes over whole is not also told its
              // characters, and a run is nothing but characters.
              if constexpr (takes_group_characters<
                                held, which,
                                typename std::remove_cvref_t<
                                    decltype(fold.here)>::state_type> &&
                            !takes_the_group_whole<
                                held, which,
                                typename std::remove_cvref_t<
                                    decltype(fold.here)>::state_type>) {
                push_one_group<held, which>(fold.here.state, run);
              }
            }
          }(), ...);
        }(std::make_index_sequence<inside>{});
      }
    } else if constexpr (how::folds && how::the_place) {
      // The characters of a run, each to the group it fell in. The positions
      // stand still across a run, so what opened and what closed is said once,
      // and where the subject can be pointed at nothing is handed over at all.
      using folded =
          fold_of<std::remove_cv_t<held_type>, MarkKind,
                  gathering_of<Type, Format, Group>::place_repeats>;
      if constexpr (every_group_whole<held_type, typename folded::state_type>()) {
        if (from != to) {
          fold_the_readings<Group, gathering_slot<Type, Format, Group, MarkKind, ToldType>,
                            std::remove_cv_t<held_type>, Automaton>(
              State, registers, states_, *from, true, text_);
        }
      } else {
        for (const char* letter = from; letter != to; ++letter) {
          fold_the_readings<Group, gathering_slot<Type, Format, Group, MarkKind, ToldType>,
                            std::remove_cv_t<held_type>, Automaton>(
              State, registers, states_, *letter, true, text_);
        }
      }
    } else if constexpr (gathers_in_the_walk<Type, Format, Automaton, Group>()) {
      // A run is where nothing is written, so what was open at its first
      // character is open at its last: one question for the whole of it.
      constexpr std::uint64_t staying =
          staying_move<Automaton, State>() == no_move
              ? 0
              : Automaton.states[State]
                    .ranges[staying_move<Automaton, State>()]
                    .groups_open;
      if constexpr ((staying & (std::uint64_t{1} << Group)) != 0) {
        gathering_of<Type, Format, Group>::push_run(
            slot<gathering_slot<Type, Format, Group, MarkKind, ToldType>>(), from,
            to);
      }
    } else {
      // The same pairs, for a run handed over whole.
      constexpr auto& pairs =
          gathered_pairs<Automaton, State,
                         tag_of_group<Type, Format, Automaton, Group>()>;
      one_per_register<Automaton.register_count> given;
      for (std::size_t which = 0; which < pairs.count; ++which) {
        const std::uint32_t opening = pairs.open[which];
        const std::uint32_t closing = pairs.shut[which];
        if (given.test(opening) || stood_nowhere(slot_read(registers, opening))) continue;
        if (closed_since_turn(slot_read(registers, closing), slot_read(registers, opening),
                              how::place_repeats)) continue;
        given.set(opening);
        gathering_of<Type, Format, Group>::push_run(
            std::get<gathering_slot<Type, Format, Group, MarkKind, ToldType>>(states_[opening]),
            from, to);
      }
    }
  }

  template <std::uint64_t NowMask, std::uint64_t AgainMask,
            std::uint64_t WasMask, bool StaysPutFlag, std::size_t Landed,
            std::size_t... Group, class RegistersType>
  constexpr void hand_over(char letter, const RegistersType& registers,
                           auto position, std::index_sequence<Group...>) {
    (hand_group<NowMask, AgainMask, WasMask, StaysPutFlag, Landed,
                Group>(letter, registers, position),
     ...);
  }

  template <std::uint64_t NowMask, std::uint64_t AgainMask,
            std::uint64_t WasMask, bool StaysPutFlag, std::size_t Landed,
            std::size_t Group, class RegistersType>
  constexpr void hand_group(char letter, const RegistersType& registers,
                            auto position) {
    using held_type = leaf_kind_of_output<Type, Group>;
    using how = gathering_of<Type, Format, Group>;
    if constexpr (scanned_as_range<held_type>) {
      return;
    } else if constexpr (how::folds && how::inside) {
      return;
    } else if constexpr (how::folds && how::the_place && !how::place_repeats &&
                         every_move_says_the_groups<Automaton>()) {
      // The machine says what happened; nothing is read to find out, and the
      // fold is where the walk keeps it rather than where a register points.
      //
      // Asked of every move and not of this one. Where the fold is written is
      // a fact about the step, but where it is read out again is a fact about
      // the whole machine -- so a machine with one move that cannot say what
      // its character lies inside keeps the whole fold at its registers, and
      // asking step by step here put half of it in the other place, where
      // nobody looked for it.
      auto& fold = slot<gathering_slot<Type, Format, Group, MarkKind, ToldType>>();
      constexpr bool stays_put = StaysPutFlag;
      fold_by_the_step<Group, std::remove_cv_t<held_type>, NowMask,
                       AgainMask, WasMask, !stays_put>(fold, letter, true,
                                                         position);
    } else if constexpr (how::folds && how::the_place) {
      fold_the_readings<Group, gathering_slot<Type, Format, Group, MarkKind, ToldType>,
                        std::remove_cv_t<held_type>, Automaton>(
          Landed, registers, states_, letter, true, text_);
    } else if constexpr (Group > 0 &&
                         list_gathers_in_the_walk<
                             Type, Format, Automaton,
                             Group == 0 ? 0 : Group - 1>()) {
      // A turn of a list, told by the move rather than by the registers.
      //
      // The move says when a turn begins, so the one before it ends there: it
      // is finished and put in the list, and a fresh one is begun. Nothing is
      // read out of a register and nothing is copied between them, which is
      // what a turn boundary used to be made of.
      constexpr std::size_t list_group = Group == 0 ? 0 : Group - 1;
      constexpr std::uint64_t now =
          NowMask;
      constexpr std::uint64_t again =
          AgainMask;
      constexpr std::uint64_t mine = std::uint64_t{1} << Group;
      static constexpr auto spread = spread_of<Type, Format>();
      using element = std::remove_cvref_t<std::ranges::range_value_t<
          std::remove_cv_t<leaf_kind_of_output<Type, list_group>>>>;
      if constexpr ((again & mine) != 0) {
        auto& made =
            slot<gathering_slot<Type, Format, Group, MarkKind, ToldType>>();
        if ((turns_open_ & mine) != 0) {
          auto& list = slot<gathering_slot<Type, Format, list_group, MarkKind, ToldType>>();
          append_to(list, scanner_finish<element>(std::move(made)));
        }
        made = gathering_of<Type, Format, Group>::begin(
            spread.parameters[Group].view(),
            context_at_group<Type, Group>(told_));
        turns_open_ |= mine;
      }
      if constexpr ((now & mine) != 0) {
        gathering_of<Type, Format, Group>::push(
            slot<gathering_slot<Type, Format, Group, MarkKind, ToldType>>(),
            letter);
        turns_open_ |= mine;
      }
    } else if constexpr (gathers_in_the_walk<Type, Format, Automaton, Group>()) {
      // Open where the move says so, and gathered where the walk keeps it.
      constexpr std::uint64_t now =
          NowMask;
      constexpr std::uint64_t again =
          AgainMask;
      if constexpr ((now & (std::uint64_t{1} << Group)) != 0) {
        static constexpr auto spread = spread_of<Type, Format>();
        auto& made = slot<gathering_slot<Type, Format, Group, MarkKind, ToldType>>();
        if constexpr ((again & (std::uint64_t{1} << Group)) != 0) {
          made = gathering_of<Type, Format, Group>::begin(
              spread.parameters[Group].view(),
              context_at_group<Type, Group>(told_));
        }
        gathering_of<Type, Format, Group>::push(made, letter);
      }
    } else {
      static constexpr auto spread = spread_of<Type, Format>();
      // The pairs this state names, worked out while compiling; a register is
      // one gathering, so one of them going on is enough.
      constexpr auto& pairs =
          gathered_pairs<Automaton, Landed,
                         tag_of_group<Type, Format, Automaton, Group>()>;
      one_per_register<Automaton.register_count> given;
      for (std::size_t which = 0; which < pairs.count; ++which) {
        const std::uint32_t opening = pairs.open[which];
        const std::uint32_t closing = pairs.shut[which];
        if (given.test(opening) || stood_nowhere(slot_read(registers, opening))) continue;
        if (closed_since_turn(slot_read(registers, closing), slot_read(registers, opening),
                              how::place_repeats)) continue;
        given.set(opening);
        gathering_of<Type, Format, Group>::push(
            std::get<gathering_slot<Type, Format, Group, MarkKind, ToldType>>(states_[opening]),
            letter);
      }
    }
  }


  // Kept first, and by itself.
  //
  // What the walk touches on every character is here; everything else it holds
  // -- the answer, the failure, where the subject begins, the gatherings that
  // do follow a register -- is touched once a match or once a field. Put first
  // in the object, the hot part shares no cache line with the cold, and an
  // optimiser that will not promote a whole gatherer to registers can still
  // keep this much of it in one.
  // Which walk-kept lists have a turn open. A turn is closed by the move that
  // begins the next one, and the first turn of all has nothing before it to
  // close -- so being open is remembered rather than guessed at.
  std::uint64_t turns_open_ = 0;
  [[no_unique_address]] warm_type warm_{};
  cold_type* cold_ = nullptr;
  // What the places were told, kept for the slots that are made as the
  // walk goes. Empty where nothing was told, so it costs nothing there.
  [[no_unique_address]] ToldType told_{};
  states_type states_;
  std::optional<Type> made_;
  std::optional<failure_for<Type>> failed_;
  const char* text_ = nullptr;
};

// One match off input that arrives in pieces, and where the next one starts.
//
// The head of the reading, in the sense the format reader means: the machine
// takes what it takes and stops, and where it stopped is where the reading
// goes on from -- which for pieces is a place in the piece it is holding, so
// nothing has to be put back.

}  // namespace scan::detail

#undef SCAN_FORCE_INLINE

// A module keeps its macros to itself and a header does not, so they are
// taken back here rather than handed to whoever includes this.
#undef SCAN_FORCE_INLINE
