// Where the gatherings live while the machine runs, and what a move does to
// them.
//
// The walk stands in several readings of the subject at once and keeps them
// apart by its registers, so a gathering goes where a register goes: begun
// where a group opens, carried where a reading divides, set aside where a turn
// ends. This is the half of the shape layer that has to know the automaton.

export module scan.shape.walk;

import std;
import scan.tre;
export import scan.compiler;
export import scan.runtime;
export import scan.shape.gatherings;

#if defined(_MSC_VER)
#define SCAN_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define SCAN_FORCE_INLINE [[gnu::always_inline]] inline
#else
#define SCAN_FORCE_INLINE inline
#endif

export namespace scan::detail {
template <class Type, fixed_string Format, std::size_t Group,
          class MarkType = std::ptrdiff_t>
[[nodiscard]] consteval bool alone_in_its_slot();

template <class Type, fixed_string Format, auto& Automaton, std::size_t Group>
[[nodiscard]] consteval bool gathers_in_the_walk() {
  using how = gathering_of<Type, Format, Group>;
  // A list is left out twice over: it grows turn by turn, and which turn a
  // gathering belongs to is what the registers keep straight -- so it stays
  // where they are, and so does anything standing at a place that repeats.
  return every_move_says_the_groups<Automaton>() && !how::place_repeats &&
         !scanned_as_range<typename how::held_type> &&
         alone_in_its_slot<Type, Format, Group>();
}


// Whether a list can be gathered by the walk rather than at the registers.
//
// A list is kept at a register because its turns are told apart by the
// registers: the place an element starts at is written afresh every turn, and
// which gathering that is, is what the register says. Where every move can say
// what its character lies inside, none of that is needed -- the move says when
// a turn begins, and the walk can keep the list and the turn being gathered
// itself, the way it keeps a fold.
//
// Asked of the list and of its element together, because they are kept
// together: the list is the slot the elements are appended to and the element
// is the slot being gathered, and one of them living at a register would put
// the other back there too.
template <class Type, fixed_string Format, auto& Automaton, std::size_t Group>
[[nodiscard]] consteval bool list_gathers_in_the_walk() {
  if constexpr (!scanned_as_range<leaf_kind_of_output<Type, Group>>) {
    return false;
  } else if constexpr (Group + 1 >= groups_of_output<Type>()) {
    return false;
  } else {
    constexpr std::size_t element = Group + 1;
    using inside = leaf_kind_of_output<Type, element>;
    if constexpr (scanned_as_range<inside>) {
      return false;
    } else if constexpr (gathering_of<Type, Format, element>::folds) {
      return false;
    } else {
      return every_move_says_the_groups<Automaton>() &&
             alone_in_its_slot<Type, Format, Group>() &&
             alone_in_its_slot<Type, Format, element>();
    }
  }
}

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
template <class Type, fixed_string Format, std::size_t... Group>
[[nodiscard]] constexpr auto make_scanner_state(
    std::index_sequence<Group...>) {
  static constexpr auto spread = spread_of<Type, Format>();
  // A group that stands for a list gathers the list itself, which needs no
  // reader: what goes into it are whole elements, put there as each one ends.
  const auto one = []<std::size_t which>() {
    using held_type = leaf_kind_of_output<Type, which>;
    if constexpr (scanned_as_range<held_type>) {
      return made_range<std::remove_cv_t<held_type>, spread.turns_most[which]>(
          scan::no_contexts{});
    } else {
      return gathering_of<Type, Format, which>::begin(
          spread.parameters[which].view());
    }
  };
  return std::tuple{one.template operator()<Group>()...};
}

template <class Type, fixed_string Format>
[[nodiscard]] constexpr auto make_scanner_state() {
  return make_scanner_state<Type, Format>(
      std::make_index_sequence<groups_of_output<Type>()>{});
}

// The same, told what the place this shape stands at was told. Every place
// inside begins with what it was told, which is what a place told a context
// means one level down as much as it does at the call.
template <class Type, fixed_string Format, class CarrierType, std::size_t... Group>
[[nodiscard]] constexpr auto make_scanner_state_told(
    const CarrierType& told, std::index_sequence<Group...>) {
  static constexpr auto spread = spread_of<Type, Format>();
  const auto one = [&]<std::size_t which>() {
    using held_type = leaf_kind_of_output<Type, which>;
    if constexpr (scanned_as_range<held_type>) {
      return made_range<std::remove_cv_t<held_type>, spread.turns_most[which]>(
          context_at_group<Type, which>(told));
    } else {
      return gathering_of<Type, Format, which>::begin(
          spread.parameters[which].view(), context_at_group<Type, which>(told));
    }
  };
  return std::tuple{one.template operator()<Group>()...};
}

template <class Type, fixed_string Format, class CarrierType>
[[nodiscard]] constexpr auto make_scanner_state_told(const CarrierType& told) {
  return make_scanner_state_told<Type, Format>(
      told, std::make_index_sequence<groups_of_output<Type>()>{});
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
template <class... Kinds>
struct gathering_kinds {
  using as_a_tuple = std::tuple<Kinds...>;
};

template <class List, class Kind>
struct with_kind;

template <class... Kinds, class Kind>
struct with_kind<gathering_kinds<Kinds...>, Kind> {
  using result = std::conditional_t<(std::is_same_v<Kind, Kinds> || ...),
                                    gathering_kinds<Kinds...>,
                                    gathering_kinds<Kinds..., Kind>>;
};

template <class List, class Kind>
struct where_kind;

template <class Kind>
struct where_kind<gathering_kinds<>, Kind> {
  static constexpr std::size_t at = 0;
};

template <class First, class... Rest, class Kind>
struct where_kind<gathering_kinds<First, Rest...>, Kind> {
  static constexpr std::size_t at =
      std::is_same_v<First, Kind>
          ? 0
          : 1 + where_kind<gathering_kinds<Rest...>, Kind>::at;
};

// What one group is gathered in, asked without asking for the others.
template <class Type, fixed_string Format, std::size_t Group,
          class MarkType = std::ptrdiff_t,
          class CarrierType = scan::default_context_t,
          bool AList = scanned_as_range<leaf_kind_of_output<Type, Group>>>
struct gathering_state {
  using result = decltype(gathering_of<Type, Format, Group, MarkType>::begin(
      std::string_view{},
      context_at_group<Type, Group>(std::declval<const CarrierType&>())));
};

template <class Type, fixed_string Format, std::size_t Group, class MarkType,
          class CarrierType>
struct gathering_state<Type, Format, Group, MarkType, CarrierType, true> {
  using result = std::remove_cv_t<leaf_kind_of_output<Type, Group>>;
};

template <class Type, fixed_string Format, class MarkType, class List,
          std::size_t Group, std::size_t Count,
          class CarrierType = scan::default_context_t>
struct kinds_from {
  using result = typename kinds_from<
      Type, Format, MarkType,
      typename with_kind<
          List, typename gathering_state<Type, Format, Group, MarkType,
                                         CarrierType>::result>::result,
      Group + 1, Count, CarrierType>::result;
};

template <class Type, fixed_string Format, class MarkType, class List,
          std::size_t Count, class CarrierType>
struct kinds_from<Type, Format, MarkType, List, Count, Count, CarrierType> {
  using result = List;
};

template <class Type, fixed_string Format, class MarkType = std::ptrdiff_t,
          class CarrierType = scan::default_context_t>
using gathering_kinds_of =
    typename kinds_from<Type, Format, MarkType, gathering_kinds<>, 0,
                        groups_of_output<Type>(), CarrierType>::result;

// What one register holds.
template <class Type, fixed_string Format, class MarkType = std::ptrdiff_t,
          class CarrierType = scan::default_context_t>
using register_state =
    typename gathering_kinds_of<Type, Format, MarkType, CarrierType>::as_a_tuple;

// Which slot of it a group is gathered in.
template <class Type, fixed_string Format, std::size_t Group,
          class MarkType = std::ptrdiff_t,
          class CarrierType = scan::default_context_t>
inline constexpr std::size_t gathering_slot =
    where_kind<gathering_kinds_of<Type, Format, MarkType, CarrierType>,
               typename gathering_state<Type, Format, Group, MarkType,
                                        CarrierType>::result>::at;

// The default is said once, where the name is first declared above; saying it
// again here is ill-formed and only a module unit lets it pass.
template <class Type, fixed_string Format, std::size_t Group, class MarkType>
[[nodiscard]] consteval bool alone_in_its_slot() {
  // Slots are handed out by kind, not by place: two places gathered the same
  // way share one, because at a register they are still two -- the register is
  // what tells them apart. A gathering the walk keeps has no register to be
  // told apart by, so where two places share a slot the walk can keep neither:
  // what one pushed the other would read.
  constexpr std::size_t mine = gathering_slot<Type, Format, Group, MarkType>;
  return [&]<std::size_t... other>(std::index_sequence<other...>) {
    return (true && ... &&
            (other == Group ||
             gathering_slot<Type, Format, other, MarkType> != mine));
  }(std::make_index_sequence<groups_of_output<Type>()>{});
}

// Слот, который принимает символы, держит внутри буфер с индексом времени
// выполнения -- и такой буфер не даёт поднять в регистры ничего, что лежит с
// ним в одном объекте. Поэтому слоты делятся надвое: те, что меняются на
// каждом символе, остаются значением сборщика, а собирающие обход держит
// своей переменной и даёт по ссылке.
template <class Kind>
concept keeps_characters = requires(Kind& one, char letter) {
  one.push_back(letter);
};

template <class TupleType, std::size_t... Which>
[[nodiscard]] constexpr auto pick_warm(std::index_sequence<Which...>) {
  return std::tuple_cat(
      std::conditional_t<
          keeps_characters<std::tuple_element_t<Which, TupleType>>,
          std::tuple<>,
          std::tuple<std::tuple_element_t<Which, TupleType>>>{}...);
}

template <class TupleType, std::size_t... Which>
[[nodiscard]] constexpr auto pick_cold(std::index_sequence<Which...>) {
  return std::tuple_cat(
      std::conditional_t<
          keeps_characters<std::tuple_element_t<Which, TupleType>>,
          std::tuple<std::tuple_element_t<Which, TupleType>>,
          std::tuple<>>{}...);
}

template <class TupleType>
using warm_slots_of = decltype(pick_warm<TupleType>(
    std::make_index_sequence<std::tuple_size_v<TupleType>>{}));
template <class TupleType>
using cold_slots_of = decltype(pick_cold<TupleType>(
    std::make_index_sequence<std::tuple_size_v<TupleType>>{}));

// A gathering begun again, keeping the resource it was begun with.
//
// Beginning one and assigning it into a slot loses what it was begun with: a
// container that keeps a resource keeps its own when it is assigned to, and the
// slot was made on the default resource before anybody said otherwise. So the
// slot is ended and begun where it stands, which is what "made with" means.
template <class Slot, class Made>
constexpr void begin_gathering_at(Slot& into, Made&& made) {
  if constexpr (requires { typename Slot::allocator_type; }) {
    std::destroy_at(&into);
    std::construct_at(&into, std::forward<Made>(made));
  } else {
    into = std::forward<Made>(made);
  }
}

// One gathering of every kind, each begun as the first group of that kind
// would begin it. Where two groups of a kind ask for different parameters, the
// one that is not first is begun again when its group opens, which is where
// every group but one begins in any case.
template <class Type, fixed_string Format, class MarkType = std::ptrdiff_t,
          class CarrierType = scan::default_context_t>
[[nodiscard]] constexpr auto make_slots(const CarrierType& told = CarrierType{}) {
  register_state<Type, Format, MarkType, CarrierType> made{};
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    // Backwards, so that the first group of a kind is the one that is left.
    const auto one = [&]<std::size_t which>() {
      using held_type = leaf_kind_of_output<Type, which>;
      if constexpr (scanned_as_range<held_type>) {
        static constexpr auto spread = spread_of<Type, Format>();
        std::get<gathering_slot<Type, Format, which, MarkType>>(made) =
            made_range<std::remove_cv_t<held_type>, spread.turns_most[which]>(
                context_at_group<Type, which>(told));
      } else {
        static constexpr auto spread = spread_of<Type, Format>();
        begin_gathering_at(
            std::get<gathering_slot<Type, Format, which, MarkType, CarrierType>>(
                made),
            gathering_of<Type, Format, which, MarkType>::begin(
                spread.parameters[which].view(),
                context_at_group<Type, which>(told)));
      }
    };
    (one.template operator()<groups_of_output<Type>() - 1 - group>(), ...);
  }(std::make_index_sequence<groups_of_output<Type>()>{});
  return made;
}

// Every register, begun.
//
// One gathering of each kind everywhere, and then the groups that are open
// from the very first character begun where their reading says they are kept:
// those never meet the command that begins a group, because they were opened
// before there was a character to move on.
template <class Type, fixed_string Format, auto& Automaton,
          class MarkType = std::ptrdiff_t,
          class CarrierType = scan::default_context_t>
[[nodiscard]] constexpr auto make_register_states(
    const CarrierType& told = CarrierType{}) {
  // Made one by one rather than made once and filled in. A container that
  // keeps a resource takes it when it is constructed and keeps its own when it
  // is assigned or copied, so filling an array of default-made states with a
  // well-made one leaves every register on the default resource.
  auto states = [&]<std::size_t... at>(std::index_sequence<at...>) {
    return std::array<register_state<Type, Format, MarkType, CarrierType>,
                      Automaton.register_count>{
        ((void)at, make_slots<Type, Format, MarkType>(told))...};
  }(std::make_index_sequence<Automaton.register_count>{});
  const auto& initial = Automaton.states[Automaton.initial];
  [&]<std::size_t... group>(std::index_sequence<group...>) {
    ([&] {
      using held_type = leaf_kind_of_output<Type, group>;
      for (std::size_t reading = 0; reading < initial.reading_count;
           ++reading) {
        const std::uint32_t at = initial.readings[reading][group * 2];
        if (at >= Automaton.register_count) continue;
        if constexpr (scanned_as_range<held_type>) {
          static constexpr auto spread = spread_of<Type, Format>();
          std::get<gathering_slot<Type, Format, group, MarkType>>(states[at]) =
              made_range<std::remove_cv_t<held_type>, spread.turns_most[group]>(
                  context_at_group<Type, group>(told));
        } else {
          static constexpr auto spread = spread_of<Type, Format>();
          begin_gathering_at(
              std::get<gathering_slot<Type, Format, group, MarkType, CarrierType>>(
                  states[at]),
              gathering_of<Type, Format, group, MarkType>::begin(
                  spread.parameters[group].view(),
                  context_at_group<Type, group>(told)));
        }
      }
    }(), ...);
  }(std::make_index_sequence<groups_of_output<Type>()>{});
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
template <class StatesType, std::size_t CommandCapacity>
struct kept_gatherings {
  using held_type = typename StatesType::value_type;
  std::array<std::size_t, CommandCapacity> which{};
  // Room for as many as a transition could ask for, and a gathering made in
  // none of them until one is asked for. A transition that copies two of them
  // used to make one for every command it could have had, and throw the rest
  // away unread -- a dozen strings a character where a field ends.
  std::array<std::optional<held_type>, CommandCapacity> held{};
  std::size_t count = 0;

  [[nodiscard]] constexpr const held_type& operator[](
      std::size_t source) const {
    for (std::size_t at = 0; at < count; ++at) {
      if (which[at] == source) return *held[at];
    }
    return *held[0];
  }
};

// A copy of one gathering that keeps the resource it was made with.
//
// A container that keeps its own resource does not hand it on when it is
// copied -- select_on_container_copy_construction says so -- so a walk that
// keeps a gathering for later would keep it on the default resource and hand
// that back at the end. Said once here, because every other copy of a
// gathering goes through an assignment, and an assignment keeps the resource
// the thing being assigned to was made with.
template <class Kind>
[[nodiscard]] constexpr Kind copied_gathering(const Kind& one) {
  if constexpr (requires {
                  typename Kind::allocator_type;
                  one.get_allocator();
                  Kind(one, one.get_allocator());
                }) {
    return Kind(one, one.get_allocator());
  } else {
    return one;
  }
}

template <class SlotsType, std::size_t... At>
[[nodiscard]] constexpr SlotsType copied_slots(const SlotsType& all,
                                                std::index_sequence<At...>) {
  return SlotsType{copied_gathering(std::get<At>(all))...};
}

template <class StatesType, std::size_t CommandCount>
[[nodiscard]] constexpr auto keep_gatherings(
    const StatesType& states,
    const std::array<packed_command, CommandCount>& commands,
    std::size_t count) {
  kept_gatherings<StatesType, CommandCount> kept;
  // Where nothing is gathered at a register there is no array to keep
  // anything out of: the walk holds every gathering itself, and asking for
  // `states[somewhere]` would be asking a row of no elements for one of them.
  if constexpr (std::tuple_size_v<StatesType> == 0) {
    return kept;
  }
  for (std::size_t index = 0; index < count; ++index) {
    if (commands[index].source == packed_command::no_source) continue;
    if (commands[index].value != -2) continue;
    bool already = false;
    for (std::size_t at = 0; at < kept.count; ++at) {
      if (kept.which[at] == commands[index].source) already = true;
    }
    if (already) continue;
    kept.which[kept.count] = commands[index].source;
    using slots_kind = std::remove_cvref_t<decltype(states[0])>;
    kept.held[kept.count].emplace(copied_slots<slots_kind>(
        states[commands[index].source],
        std::make_index_sequence<std::tuple_size_v<slots_kind>>{}));
    ++kept.count;
  }
  return kept;
}

template <std::size_t Group, class Type, fixed_string Format, auto& Automaton,
          bool HandsTheCharacter = true, bool KeptInTheWalk = false,
          class StatesType, class KeptType, class RegistersType,
          std::size_t CommandCount,
          class CarrierType = scan::no_contexts>
constexpr void advance_scanner(
    char symbol, std::size_t state, std::size_t left_state, auto position,
    const RegistersType& registers,
    const KeptType& old_states, StatesType& states,
    const std::array<packed_command, CommandCount>& commands,
    std::size_t count, const char* text,
    const CarrierType& told = CarrierType{}) {
  static constexpr auto spread = spread_of<Type, Format>();
  constexpr std::size_t opening = Group * 2;
  constexpr std::size_t closing = Group * 2 + 1;
  using held_type = leaf_kind_of_output<Type, Group>;
  constexpr bool gathers_a_list = scanned_as_range<held_type>;
  using how = gathering_of<Type, Format, Group>;
  // A gathering the walk keeps for itself is not at a register, so none of
  // what follows is about it: nothing to begin where a group opens, nothing to
  // copy where a reading divides, nothing to hand from one register to
  // another. That is most of what a move used to cost.
  //
  // Asked of whoever is calling, because only one of them keeps gatherings
  // that way: a walk over characters in a row does, and a reader taking a
  // stream a character at a time keeps everything at its registers.
  if constexpr (KeptInTheWalk &&
                (gathers_in_the_walk<Type, Format, Automaton, Group>() ||
                 list_gathers_in_the_walk<Type, Format, Automaton, Group>() ||
                 (Group > 0 &&
                  list_gathers_in_the_walk<Type, Format, Automaton,
                                           Group == 0 ? 0 : Group - 1>()) ||
                 (how::folds && how::the_place && !how::place_repeats &&
                  every_move_says_the_groups<Automaton>()))) {
    return;
  } else {
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
  if constexpr (!gathers_a_list) {
    // The groups that close on this step. What the field gathered is in the
    // opening it was being added to, whichever register that has become.
    command_index = 0;
    std::apply(
        [&](const auto&... command) {
          ([&] {
            if (command_index++ >= count) return;
            if (Automaton.register_tag[command.destination] != closing) return;
            if (slot_read(registers, command.destination) != position) return;
            // What the turn gathered is at the opening the state being left
            // named, not the one the state being entered names: this step is
            // where the group is renamed, and the register the characters went
            // to is the old one.
            const auto& left = Automaton.states[left_state];
            for (std::size_t reading = 0; reading < left.reading_count;
                 ++reading) {
              const std::uint32_t was = left.readings[reading][opening];
              if (stood_nowhere(slot_read(registers, was))) continue;
              std::get<gathering_slot<Type, Format, Group>>(
                  states[command.destination]) =
                  std::get<gathering_slot<Type, Format, Group>>(states[was]);
              break;
            }
          }(),
           ...);
        },
        commands);
  }
  // A turn that ended is set aside where it was gathered.
  //
  // The characters of a turn go to the register the readings of the state being
  // left name for this place's opening. The command that begins the next turn
  // writes a register of its own, and for the first turn of a list that is a
  // register with nothing in it yet -- so setting the turn aside by the command
  // lost the first turn of every list, and only the first: from the second turn
  // on, the register the command writes is the one the turn before it was
  // carried into. It is set aside by the readings now, which is how the turn
  // that ended is looked for everywhere else.
  if constexpr (how::folds && how::the_place && how::place_repeats) {
    bool beginning_another = false;
    std::uint32_t begins_at = 0;
    for (std::size_t index = 0; index < count; ++index) {
      const auto& command = commands[index];
      if (command.value == -2) continue;
      if (Automaton.register_tag[command.destination] != opening) continue;
      beginning_another = true;
      begins_at = command.destination;
      break;
    }
    if (beginning_another) {
      // Gathered at the register the readings of the state being left name for
      // this place. The move that begins the next turn renames the register --
      // turn one lives at one number and turn two at another -- so the turn
      // that ended is looked for where it was gathered, which is the old name.
      const auto& left = Automaton.states[left_state];
      std::array<bool, Automaton.register_count> aside{};
      for (std::size_t reading = 0; reading < left.reading_count; ++reading) {
        const std::uint32_t was = left.readings[reading][opening];
        if (aside[was]) continue;
        aside[was] = true;
        auto& fold =
            std::get<gathering_slot<Type, Format, Group>>(states[was]);
        if (!fold.here.started) continue;
        // Set aside where the next turn will be looked for. The move renames
        // the register -- the turn that ended was gathered under the old name
        // and the turn that follows it is written under the new one -- and
        // whoever makes the element of a turn that ended looks under the new
        // name, because that is where the walk is now.
        auto& into = std::get<gathering_slot<Type, Format, Group>>(
            states[begins_at]);
        into.going = std::move(fold.here);
        into.has_going = true;
        fold.here = std::remove_cvref_t<decltype(fold.here)>(
            context_at_group<Type, Group>(told));
      }
    }
  }
  command_index = 0;
  std::apply(
      [&](const auto&... command) {
        ([&] {
          if (command_index++ >= count) return;
          const std::uint32_t tag = Automaton.register_tag[command.destination];
          if (tag == opening) {
            if (command.source != packed_command::no_source &&
                command.value == -2) {
              // A reading that divides carries its gathering with it.
              std::get<gathering_slot<Type, Format, Group>>(
                  states[command.destination]) =
                  std::get<gathering_slot<Type, Format, Group>>(
                      old_states[command.source]);
            } else if constexpr (gathers_a_list) {
              // A list begins empty, and begins once. This command writes the
              // register that holds where the list opened -- which happens when
              // the list starts and again at a turn boundary, where the
              // boundary has just handed the turn that ended to this very list.
              // Emptying it there throws that turn away, and only that one: the
              // boundaries after it arrive as copies and go down the branch
              // above. So it is emptied where it is starting, and where it is
              // starting the register it is written into stood nowhere.
              if (stood_nowhere(slot_read(registers, command.destination))) {
                auto& stands_here =
                    std::get<gathering_slot<Type, Format, Group>>(
                        states[command.destination]);
                stands_here = made_like(stands_here);
              }
            } else if constexpr (how::folds && how::the_place &&
                                 how::place_repeats) {
              // The turn that ended was set aside above, where it was
              // gathered. What this register holds now is the turn that is
              // beginning, and a turn begins with what its place was told --
              // the same as the first turn did.
              auto& fold = std::get<gathering_slot<Type, Format, Group>>(
                  states[command.destination]);
              fold.here = std::remove_cvref_t<decltype(fold.here)>(
                  context_at_group<Type, Group>(told));
            } else {
              begin_gathering_at(
                  std::get<gathering_slot<Type, Format, Group>>(
                      states[command.destination]),
                  gathering_of<Type, Format, Group>::begin(
                      spread.parameters[Group].view(),
                      context_at_group<Type, Group>(told)));
            }
          } else if (tag == closing && slot_read(registers, command.destination) != position &&
                     command.source != packed_command::no_source &&
                     command.value == -2) {
            // A closing already written, only being carried along, keeps what
            // it holds.
            std::get<gathering_slot<Type, Format, Group>>(
                states[command.destination]) =
                std::get<gathering_slot<Type, Format, Group>>(
                    old_states[command.source]);
          }
        }(),
         ...);
      },
      commands);
  if constexpr (how::folds && how::the_place &&
                (how::place_repeats || !KeptInTheWalk ||
                 !every_move_says_the_groups<Automaton>())) {
    // Everything that happened inside this place on this character, told in
    // order -- and told now, before the copy below, or a fold that ends where
    // its place ends would be copied one closing short.
    //
    // Left out only where somebody else is doing the telling. The walk tells
    // the fold from the move, once, where the machine can say what a character
    // lies inside, and saying it again here would say every opening and every
    // character twice.
    //
    // Whether the machine could say it is not on its own the question, and
    // asking only that is what emptied a list read record after record: the
    // reading that steps a character at a time has no move to be told from and
    // never told the fold anything, while the machine, asked by itself,
    // answered that somebody else would.
    fold_the_readings<Group, gathering_slot<Type, Format, Group>,
                      std::remove_cv_t<held_type>, Automaton>(
        state, registers, states, symbol, HandsTheCharacter, text);
  }
  // A list gathers elements, not characters. Written as an early return this
  // would discard nothing: what follows an `if constexpr` is not the branch it
  // did not take.
  //
  // Handing the character over is skipped where the caller knows the state and
  // does it by the numbers: this walks every reading of the state and keeps a
  // flag per register to do it once, which on a subject read a character at a
  // time is most of what reading it costs.
  if constexpr (!gathers_a_list && HandsTheCharacter) {
    // Once each, however many readings share it: a register is one gathering.
    one_per_register<Automaton.register_count> filled;
    const auto& packed = Automaton.states[state];
    for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
      const std::uint32_t open = packed.readings[reading][opening];
      const std::uint32_t close = packed.readings[reading][closing];
      if (filled.test(open)) continue;
      if (stood_nowhere(slot_read(registers, open)) ||
          closed_since_turn(slot_read(registers, close), slot_read(registers, open),
                            how::place_repeats))
        continue;
      filled.set(open);
      gathering_of<Type, Format, Group>::push(
          std::get<gathering_slot<Type, Format, Group>>(states[open]), symbol);
    }
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
// Nothing was kept by the walk: every gathering is at a register.
struct nothing_kept_here {};
inline constexpr nothing_kept_here nothing_was_kept{};

// What a walk kept for itself, and which places those are.
//
// The mask is carried beside the gatherings because whoever reads the value
// has the places in hand and not the machine: a place that follows a reading
// is read from the register the reading names, and one the walk kept is read
// from here.
template <class SlotsType, std::uint64_t Places>
struct kept_by_the_walk {
  static constexpr std::uint64_t which_places = Places;
  const SlotsType& slots;
  // Which of them had a turn being gathered when the reading stopped. A list
  // the walk keeps has no register to say so.
  std::uint64_t turns = 0;
};

template <class Type, fixed_string Format, class ReadingType,
          class StatesType, class RegistersType,
          class KeptType = nothing_kept_here,
          class EndingType = ReadingType>
struct gathered_by_the_registers {
  const ReadingType& reading;
  const StatesType& states;
  const RegistersType& registers;
  // What the walk kept for itself, where it kept anything: a gathering that
  // does not follow a reading is not at a register, and this is where it is.
  const KeptType& kept;
  // Where the ending put each tag, for the places the ending is what closed.
  //
  // A tag that the walk closed stands where the reading says throughout, and
  // that is what everything here reads. A group still open where the match
  // ends is closed by the commands that end it and by nothing else, and those
  // write into registers of their own -- so its closing is only to be found
  // here. Two truths rather than one because they are about two different
  // moments, and reading either by the other's registers is reading a register
  // nothing filled.
  const EndingType& ending;

  // A field still being read when the input ended is where it was being
  // gathered; one that ended earlier is the copy taken when it closed, which
  // the readings that went on adding to the opening cannot have changed.
  template <std::size_t Place>
  [[nodiscard]] SCAN_FORCE_INLINE constexpr const auto& gathering() const {
    if constexpr (kept_here<Place>()) {
      return std::get<gathering_slot<Type, Format, Place>>(kept.slots);
    } else {
      const std::uint32_t open = reading[Place * 2];
      const std::uint32_t close = reading[Place * 2 + 1];
      // A place taken over and over is read where it is being gathered: the
      // end standing where the beginning stands is the end of the turn before,
      // and the copy taken then is a turn behind.
      const bool still_reading = !closed_since_turn(
          slot_read(registers, close), slot_read(registers, open),
          gathering_of<Type, Format, Place>::place_repeats);
      return std::get<gathering_slot<Type, Format, Place>>(
          states[still_reading ? open : close]);
    }
  }

  // Whether this place's gathering is one the walk kept. Asked of the slot
  // rather than of the machine, because this is read from where the value is
  // made and the machine is not in hand there.
  template <std::size_t Place>
  [[nodiscard]] static consteval bool kept_here() {
    if constexpr (std::same_as<KeptType, nothing_kept_here>) {
      return false;
    } else {
      return (KeptType::which_places & (std::uint64_t{1} << Place)) != 0;
    }
  }

  // A list is gathered and read at its opening throughout: its elements go on
  // being added to the same list however the readings divide.
  template <std::size_t Place>
  [[nodiscard]] SCAN_FORCE_INLINE constexpr const auto& list() const {
    if constexpr (kept_here<Place>()) {
      return std::get<gathering_slot<Type, Format, Place>>(kept.slots);
    } else {
      return std::get<gathering_slot<Type, Format, Place>>(
          states[reading[Place * 2]]);
    }
  }

  template <std::size_t Place>
  [[nodiscard]] SCAN_FORCE_INLINE constexpr bool took_part() const {
    // A turn the walk was gathering is said by the walk: there is no register
    // holding where it began, because it was never at a register.
    if constexpr (kept_here<Place>()) {
      if constexpr (std::same_as<KeptType, nothing_kept_here>) {
        return false;
      } else {
        return (kept.turns & (std::uint64_t{1} << Place)) != 0;
      }
    } else {
      return !stood_nowhere(slot_read(registers, reading[Place * 2]));
    }
  }

  // What a place stood on, where the subject can be pointed at. Nothing where
  // the place took no part.
  //
  // A position here is how many characters have been read and not the index of
  // one, so what a place stood on begins one before where its opening says.
  template <std::size_t Place>
  [[nodiscard]] SCAN_FORCE_INLINE constexpr std::string_view span(const char* text) const {
    const auto began = slot_read(registers, reading[Place * 2]);
    if (stood_nowhere(began)) return {};
    const auto walked = slot_read(registers, reading[Place * 2 + 1]);
    // Closed as the walk passed, or closed by the commands that end a match
    // because the match ended while it was still open. The second is not in
    // the reading at all -- the ending writes registers of its own -- and read
    // through the reading such a group looked like one that never closed.
    const auto ended =
        closed_since(walked, began) ? walked : slot_read(registers, ending[Place * 2 + 1]);
    if (!closed_since(ended, began)) return {};
    if constexpr (std::is_pointer_v<std::remove_cvref_t<decltype(began)>>) {
      // What a group stood on is what lies between its marks. Nothing is taken
      // off: a mark is written as the walk stands on the character, and the
      // step that used to be taken off here was a character of every group.
      static_cast<void>(text);
      return std::string_view(began, static_cast<std::size_t>(ended - began));
    } else {
      return stood_on(text, began, ended);
    }
  }

  // What was written after the colon at this place, where anything was. Asked
  // of the source because the format is known here and not where the value is
  // put together.
  template <std::size_t Place>
  [[nodiscard]] static constexpr std::string_view parameters_at() {
    return format_parameters<Type, Format>::at(Place);
  }

  // A fold at this place, with the last step run into the copy: the end of the
  // input is not a character, so what it left open is closed here.
  template <std::size_t Place, class Held>
  [[nodiscard]] SCAN_FORCE_INLINE constexpr auto fold_at() const {
    auto fold = gathering<Place>();
    fold_one_step<fold_phase::whole, Place, Held>(fold.here, reading, registers,
                                                  '\0', false);
    return fold;
  }
};

// Made rather than named: the reading, the states and the registers are all
// deduced, and the type and the format are what say where a group is gathered.
template <class Type, fixed_string Format, class ReadingType,
          class StatesType, class RegistersType,
          class KeptType = nothing_kept_here>
[[nodiscard]] constexpr auto by_the_registers(
    const ReadingType& reading, const StatesType& states,
    const RegistersType& registers,
    const KeptType& kept = nothing_was_kept) {
  return gathered_by_the_registers<Type, Format, ReadingType, StatesType,
                                   RegistersType, KeptType, ReadingType>{
      reading, states, registers, kept, reading};
}

// The same, told as well where the ending put the tags it closed.
template <class Type, fixed_string Format, class ReadingType,
          class StatesType, class RegistersType, class KeptType,
          class EndingType>
[[nodiscard]] constexpr auto by_the_registers(const ReadingType& reading,
                                              const StatesType& states,
                                              const RegistersType& registers,
                                              const KeptType& kept,
                                              const EndingType& ending) {
  return gathered_by_the_registers<Type, Format, ReadingType, StatesType,
                                   RegistersType, KeptType, EndingType>{
      reading, states, registers, kept, ending};
}


template <class Root, class Type, std::size_t Offset, bool AsOutput = false,
          class FailureType = failure_for<Root>, class SourceType,
          class CarrierType = scan::no_contexts>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::expected<Type, FailureType> finish_value(
    const SourceType& source, const char* text,
    const CarrierType& given = CarrierType{});

// The parts of a product, and the arguments of a call, as named functions
// rather than as lambdas called where they stand. A lambda holding references
// and called inside the argument of something that itself holds references is
// more than the constant evaluator will follow.

template <class Root, class Type, std::size_t Offset, class FailureType,
          class SourceType, class CarrierType, std::size_t... Part>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::expected<Type, FailureType> finish_parts(
    const SourceType& source, const char* text, const CarrierType& given,
    std::index_sequence<Part...>) {
  auto parts =
      std::tuple{finish_value<Root, typename parts_of<Type>::template at<Part>,
                              Offset + groups_before_field<Type, Part>(), false,
                              FailureType>(
          source, text, told_for_part<Part>(given))...};
  if (auto went_wrong = what_went_wrong<FailureType>(parts)) {
    return std::unexpected(std::move(*went_wrong));
  }
  return Type{std::move(*std::get<Part>(parts))...};
}

template <class Root, class Type, std::size_t Offset, class FailureType,
          class SourceType, class CarrierType, std::size_t... Part>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::expected<Type, FailureType> finish_by_call(
    const SourceType& source, const char* text, const CarrierType& given,
    std::index_sequence<Part...>) {
  auto parts =
      std::tuple{finish_value<Root, typename parts_of<Type>::template at<Part>,
                              Offset + groups_before_field<Type, Part>(), false,
                              FailureType>(
          source, text, told_for_part<Part>(given))...};
  if (auto went_wrong = what_went_wrong<FailureType>(parts)) {
    return std::unexpected(std::move(*went_wrong));
  }
  return scan::scanner<std::remove_cv_t<Type>>::parse(
      std::move(*std::get<Part>(parts))...);
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
template <class ListType, class ElementType>
constexpr void append_to(ListType& list, ElementType&& value) {
  // `insert` only where it has to be. It is here at all because libc++ writes
  // `vector::emplace_back` through a helper taking two capturing lambdas, and
  // clang's constant evaluator refuses those -- so a list of anything could
  // not be read while compiling. That is a reason to spell it this way while
  // compiling and no reason at all to spell it this way while running, where
  // `insert` costs the best part of a nanosecond an element more.
  if constexpr (requires { list.insert(list.end(), std::move(value)); }) {
    if consteval {
      list.insert(list.end(), std::move(value));
    } else {
      if constexpr (requires { list.push_back(std::move(value)); }) {
        list.push_back(std::move(value));
      } else {
        list.insert(list.end(), std::move(value));
      }
    }
  } else {
    list.push_back(std::move(value));
  }
}

// The registers a state names for a group, in pairs.
//
// A gathering lives at the register holding the group's opening, and whether
// it is still being added to is said by the closing register of the same
// reading. Both are facts about the state, so they are worked out once here
// rather than walked at every character -- and asked in pairs, because a
// reading exists precisely to disagree with the others about whether the
// group has ended.
template <std::size_t Capacity>
struct gathered_pairs_of {
  std::array<std::uint32_t, Capacity> open{};
  std::array<std::uint32_t, Capacity> shut{};
  std::size_t count = 0;
};

// Which tag of the machine a group of the output is written in.
//
// The two counts are the same only while every group gets a mark. A place whose
// turns are told by the moves gets none -- and then every group after it stands
// one tag earlier than its number says. The mask that decides who gets a mark is
// the one the automaton was built from, so the mapping is that mask, counted.
template <class Type, fixed_string Format, auto& Automaton, std::size_t Group>
[[nodiscard]] consteval bool a_group_with_a_tag() {
  return (groups_whose_mark_is_read<Type, Format, Automaton>() >> Group) & 1;
}

template <class Type, fixed_string Format, auto& Automaton, std::size_t Group>
[[nodiscard]] consteval std::size_t tag_of_group() {
  const std::uint64_t mask = groups_whose_mark_is_read<Type, Format, Automaton>();
  return static_cast<std::size_t>(
      std::popcount(mask & ((std::uint64_t{1} << Group) - 1)));
}

template <auto& Automaton, std::size_t State, std::size_t Group>
inline constexpr auto gathered_pairs = [] consteval {
  constexpr std::size_t capacity =
      Automaton.states[State].readings.size() == 0
          ? 1
          : Automaton.states[State].readings.size();
  gathered_pairs_of<capacity> said;
  const auto& packed = Automaton.states[State];
  // Asked about a tag this machine does not have, nobody stands at it. The
  // callers say which tag they mean rather than which group, so this is a
  // backstop and not the mapping itself.
  if (Group * 2 + 1 >= packed.readings[0].size()) return said;
  for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
    const std::uint32_t open = packed.readings[reading][Group * 2];
    const std::uint32_t shut = packed.readings[reading][Group * 2 + 1];
    bool already = false;
    for (std::size_t at = 0; at < said.count; ++at) {
      if (said.open[at] == open && said.shut[at] == shut) already = true;
    }
    if (already) continue;
    said.open[said.count] = open;
    said.shut[said.count] = shut;
    ++said.count;
  }
  return said;
}();

template <std::size_t Group, class Type, fixed_string Format, auto& Automaton,
          class FailureType, class StatesType, class RegistersType,
          std::size_t CommandCount,
          class CarrierType = scan::no_contexts>
constexpr void collect_element(
    std::size_t state, const RegistersType& registers, StatesType& states,
    const std::array<packed_command, CommandCount>& commands,
    std::size_t count, const char* text, std::optional<FailureType>& failed,
    const CarrierType& told = CarrierType{}) {
  if constexpr (Group == 0) {
    return;
  } else if constexpr (!scanned_as_range<leaf_kind_of_output<Type, Group - 1>>) {
    return;
  } else {
    using list_type = leaf_kind_of_output<Type, Group - 1>;
    using element = std::remove_cvref_t<std::ranges::range_value_t<list_type>>;
    constexpr std::size_t list_group = Group - 1;
    if constexpr (gathering_of<Type, Format, Group>::folds) {
      // Made out of the turn that was moved aside, once that turn has been
      // told what ended it. Nothing here can be: at this moment it has not.
      return;
    } else if constexpr (list_gathers_in_the_walk<Type, Format, Automaton,
                                                  list_group>()) {
      // The walk is keeping this one and closes its turns from the moves.
      return;
    } else {
    // Does this step begin another turn? It does if it writes a fresh position
    // into a register that holds the place an element starts at.
    bool going_round = false;
    for (std::size_t index = 0; index < count; ++index) {
      const auto& command = commands[index];
      if (command.value == -2) continue;
      if (Automaton.register_tag[command.destination] == Group * 2) {
        going_round = true;
        break;
      }
    }
    if (!going_round) return;
    // The turn that is ending belongs to the state being left, and so do the
    // positions and the gatherings. Where the list goes next is the business of
    // the commands, which carry the gathering with the register.
    const auto& packed = Automaton.states[state];
    std::array<bool, Automaton.register_count> done{};
    for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
      const std::uint32_t open = packed.readings[reading][Group * 2];
      const std::uint32_t into = packed.readings[reading][list_group * 2];
      if (done[into] || stood_nowhere(slot_read(registers, open))) continue;
      done[into] = true;
      // An element of a list is a reading of its own, and what the list's place
      // was told is what it was told: a whole shape standing here reads its
      // places with it, the way one standing anywhere else does.
      auto one = finish_value<Type, element, Group, false, FailureType>(
          by_the_registers<Type, Format>(packed.readings[reading], states,
                                         registers),
          text, context_at_group<Type, list_group>(told));
      if (!one) {
        if (!failed) failed = std::move(one).error();
        continue;
      }
      append_to(std::get<gathering_slot<Type, Format, list_group>>(states[into]),
                std::move(*one));
    }
    }
  }
}

// The turn that was moved aside, made into an element now that the step which
// ended it has been said.
//
// It is looked for at every register, not at the readings of one state: a
// gathering travels with its register, and the move that ended the turn may
// have put it anywhere. What says there is one is the fold itself.
template <std::size_t Group, class Type, fixed_string Format, auto& Automaton,
          class FailureType, class StatesType, class RegistersType>
constexpr void collect_turn_that_ended(
    std::size_t state, const RegistersType& registers,
    StatesType& states, std::optional<FailureType>& failed) {
  if constexpr (Group == 0) {
    return;
  } else if constexpr (!scanned_as_range<leaf_kind_of_output<Type, Group - 1>>) {
    return;
  } else if constexpr (!gathering_of<Type, Format, Group>::folds) {
    return;
  } else {
    using list_type = leaf_kind_of_output<Type, Group - 1>;
    using element = std::remove_cvref_t<std::ranges::range_value_t<list_type>>;
    using held = std::remove_cv_t<element>;
    constexpr std::size_t list_group = Group - 1;
    const auto& packed = Automaton.states[state];
    std::array<bool, Automaton.register_count> done{};
    for (std::size_t reading = 0; reading < packed.reading_count; ++reading) {
      const std::uint32_t open = packed.readings[reading][Group * 2];
      const std::uint32_t into = packed.readings[reading][list_group * 2];
      if (done[open]) continue;
      done[open] = true;
      auto& fold = std::get<gathering_slot<Type, Format, Group>>(states[open]);
      if (!fold.has_going) continue;
      fold.has_going = false;
      if (fold.going.wanted_a_subject) {
        if (!failed) {
          failed = scan::as_a_failure<FailureType>(wrong_subject<>(
              "a fold that only takes its groups whole needs a subject that "
              "can be pointed at: give it push_group to read a stream"));
        }
        continue;
      }
      if constexpr (scan::says_what_went_wrong_folding<held>) {
        auto got =
            scan::scanner_told_finish_groups<held>(std::move(fold.going.state));
        if (!got) {
          if (!failed) {
            failed = scan::as_a_failure<FailureType>(std::move(got).error());
          }
          continue;
        }
        append_to(
            std::get<gathering_slot<Type, Format, list_group>>(states[into]),
            std::move(*got));
      } else {
        append_to(
            std::get<gathering_slot<Type, Format, list_group>>(states[into]),
            scan::scanner<held>{}.finish_groups(std::move(fold.going.state)));
      }
    }
  }
}

template <class Type, fixed_string Format, auto& Automaton, class FailureType,
          class RegistersType, class StatesType, std::size_t... Group>
constexpr void collect_turns_that_ended(
    std::size_t state, const RegistersType& registers,
    StatesType& states, std::index_sequence<Group...>,
    std::optional<FailureType>& failed) {
  (collect_turn_that_ended<Group, Type, Format, Automaton, FailureType>(
       state, registers, states, failed),
   ...);
}

template <class Type, fixed_string Format, auto& Automaton, class FailureType,
          class RegistersType, class StatesType, std::size_t CommandCount,
          class CarrierType = scan::no_contexts, std::size_t... Group>
constexpr void collect_elements(
    std::size_t state, const RegistersType& registers, StatesType& states,
    const std::array<packed_command, CommandCount>& commands,
    std::size_t count, std::index_sequence<Group...>, const char* text,
    std::optional<FailureType>& failed,
    const CarrierType& told = CarrierType{}) {
  (collect_element<Group, Type, Format, Automaton, FailureType>(
       state, registers, states, commands, count, text, failed, told),
   ...);
}

template <class Type, fixed_string Format, auto& Automaton,
          bool HandsTheCharacter = true, bool KeptInTheWalk = false,
          class RegistersType, class StatesType, std::size_t CommandCount,
          class CarrierType = scan::no_contexts, std::size_t... Group>
constexpr void advance_scanners(
    char symbol, std::size_t state, std::size_t left_state, auto position,
    const RegistersType& registers,
    StatesType& states,
    const std::array<packed_command, CommandCount>& commands,
    std::size_t count, std::index_sequence<Group...>,
    const char* text = nullptr, const CarrierType& told = CarrierType{}) {
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
    (advance_scanner<Group, Type, Format, Automaton, HandsTheCharacter,
                     KeptInTheWalk>(
         symbol, state, left_state, position, registers, old_states, states,
         commands, count, text, told),
     ...);
  } else {
    (advance_scanner<Group, Type, Format, Automaton, HandsTheCharacter,
                     KeptInTheWalk>(
         symbol, state, left_state, position, registers, states, states,
         commands, count, text, told),
     ...);
  }
}

// The output put together from the gatherings, walked the same way it is walked
// when the values are pieces of a subject that can be pointed at: a value asks
// its own reader to finish, a product asks its parts, a type made by a call
// makes it. Each value is taken from the gathering of the register that holds
// its opening tag in the reading that accepted.
template <class Root, class Type, std::size_t Offset, bool AsOutput,
          class FailureType, class SourceType, class CarrierType>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::expected<Type, FailureType> finish_value(
    const SourceType& source, const char* text, const CarrierType& given) {
  // A shape that reads its own groups is a value where it stands in somebody
  // else's format and a product of places in its own. Where this is the whole
  // of what is being read, it is the second.
  constexpr bool a_value = scanned_as_leaf<Type> && !AsOutput;
  if constexpr (a_value && folds_by_turns<std::remove_cv_t<Type>>) {
    // A leaf that was told its groups as the walk passed them. What is left is
    // the end of the input, which is not a character and so was never handed
    // over: a group that opened where nothing followed it, and every group
    // still open when the reading stopped. The same step the walk runs says
    // both, asked once more with nothing to hand over.
    using held = std::remove_cv_t<Type>;
    static_assert(
        requires { source.template fold_at<Offset, held>(); },
        "a shape whose places include a type that folds its own groups is read "
        "by the machine that gathers, not by a fold of its own");
    auto fold = source.template fold_at<Offset, held>();
    if (fold.here.wanted_a_subject) {
      return std::unexpected(scan::as_a_failure<FailureType>(wrong_subject<>(
          "a fold that only takes its groups whole needs a subject that can be "
          "pointed at: give it push_group to read a stream")));
    }
    if constexpr (scan::says_what_went_wrong_folding<held>) {
      auto got =
          scan::scanner_told_finish_groups<held>(std::move(fold.here.state));
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<FailureType>(std::move(got).error()));
    } else {
      return scan::scanner_told_finish_groups<held>(std::move(fold.here.state));
    }
  } else if constexpr (a_value && gathers_by_its_groups<Type>) {
    // A leaf built from its own groups once the match is over. They are groups
    // of this match like any others and the positions say where each one
    // stood, so what it is handed are views of the subject: nothing was
    // gathered for it and nothing was copied. That it has a subject to point
    // at is settled where the walk is made.
    using held = std::remove_cv_t<Type>;
    constexpr std::size_t inside = groups_a_leaf_opens<held>();
    std::array<std::string_view, inside> theirs{};
    std::array<bool, inside> took{};
    static_assert(
        requires { source.template span<Offset>(text); },
        "a shape whose places include a type built from its own groups is read "
        "by the machine that gathers, not by a fold of its own");
    [&]<std::size_t... at>(std::index_sequence<at...>) {
      ((void)[&] {
        constexpr std::size_t which = Offset + 1 + at;
        const std::string_view stood_on = source.template span<which>(text);
        if (stood_on.data() == nullptr) return;
        took[at] = true;
        theirs[at] = stood_on;
      }(), ...);
    }(std::make_index_sequence<inside>{});
    const auto pieces = std::span<const std::string_view>(theirs);
    if constexpr (scan::says_what_went_wrong_from_groups<held>) {
      auto got = [&] {
        if constexpr (requires {
                        scan::scanner_told_from_groups<held>(pieces, given);
                      }) {
          return scan::scanner_told_from_groups<held>(pieces, given);
        } else {
          return scan::scanner_told_from_groups<held>(pieces);
        }
      }();
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<FailureType>(std::move(got).error()));
    } else if constexpr (requires {
                           scan::scanner<held>{}.from_groups(pieces, given);
                         }) {
      return scan::scanner<held>{}.from_groups(pieces, given);
    } else if constexpr (requires {
                           scan::scanner<held>{}.from_groups(pieces);
                         }) {
      return scan::scanner<held>{}.from_groups(pieces);
    } else {
      auto state = begun_groups<held>(given);
      [&]<std::size_t... at>(std::index_sequence<at...>) {
        ((void)[&] {
          if (!took[at]) return;
          open_one_group<held, at>(state);
          close_one_group<held, at>(state, theirs[at]);
        }(), ...);
      }(std::make_index_sequence<inside>{});
      if constexpr (scan::says_what_went_wrong_folding<held>) {
        auto got =
            scan::scanner_told_finish_groups<held>(std::move(state));
        if (got) return std::move(*got);
        return std::unexpected(
            scan::as_a_failure<FailureType>(std::move(got).error()));
      } else {
        return scan::scanner<held>{}.finish_groups(std::move(state));
      }
    }
  } else if constexpr (a_value &&
                       reads_a_whole_piece_only<std::remove_cv_t<Type>>) {
    // A leaf that only reads a piece handed to it whole. Nothing was gathered
    // for it: the marks say where its piece stood, and it is handed a view of
    // the subject, so nothing was copied to get here either. That there is a
    // subject to point at is settled where the walk is made, the same as for a
    // leaf built from its own groups.
    using held = std::remove_cv_t<Type>;
    static_assert(
        requires { source.template span<Offset>(text); },
        "a place whose type only reads a piece handed to it whole is read by "
        "the machine that keeps the subject, not by one that is fed");
    const std::string_view piece = source.template span<Offset>(text);
    constexpr std::string_view parameters =
        SourceType::template parameters_at<Offset>();
    if constexpr (scan::says_what_went_wrong<held>) {
      auto got =
          scan::scanner_told_parse<held, scan::hands_a_failure_back>(
              piece, parameters);
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<FailureType>(std::move(got).error()));
    } else {
      if constexpr (std::same_as<CarrierType, scan::no_contexts>) {
        return scan::scanner_parse<held>(piece, parameters);
      } else {
        auto got = parse_value_given<held, FailureType,
                                     CarrierType::told_apart>(piece, parameters,
                                                               given.leaf());
        if (got) return std::move(*got);
        return std::unexpected(std::move(got).error());
      }
    }
  } else if constexpr (a_value) {
    // Finished from a copy that keeps what the gathering was made with. A
    // scanner takes its state by value, and a container that keeps a resource
    // does not hand it on when it is copied -- so handing the gathering over as
    // it stands would finish a value on the default resource, however the place
    // was told to gather it.
    const auto& gathered = source.template gathering<Offset>();
    if constexpr (scan::says_what_went_wrong_finishing<Type>) {
      auto got = scan::scanner_told_finish<std::remove_cv_t<Type>>(
          copied_gathering(gathered));
      if (got) return std::move(*got);
      return std::unexpected(
          scan::as_a_failure<FailureType>(std::move(got).error()));
    } else {
      return scanner_finish<Type>(copied_gathering(gathered));
    }
  } else if constexpr (scanned_as_range<Type>) {
    // What has been put in as each element ended, and then the one that was
    // still being read when the whole thing ended.
    using element = std::remove_cvref_t<std::ranges::range_value_t<Type>>;
    // Taken with the allocator it was gathered with. A container that keeps a
    // resource does not hand it on when it is copied -- that is what
    // select_on_container_copy_construction says -- so a list gathered into
    // the caller's resource would arrive holding the default one.
    // Handed back on the resource the caller said, whatever the walk gathered
    // it on. What a walk allocates while it reads is its own business -- it may
    // keep a gathering, copy it between registers, begin it again on a turn --
    // and none of that should decide where the value the caller is handed
    // lives. So the list is taken onto the resource its place was told about,
    // and onto the one it was gathered with where its place was told nothing.
    Type made = [&] -> Type {
      const auto& gathered = source.template list<Offset>();
      // Asked of the very construction that would be used: a container with an
      // allocator of its own is not thereby a container that takes a resource,
      // and asking the wrong question here says yes for every one of them.
      if constexpr (requires {
                      Type(gathered,
                           std::pmr::polymorphic_allocator<
                               typename Type::value_type>{});
                    }) {
        if (std::pmr::memory_resource* where = resource_of(given)) {
          return Type(gathered,
                      std::pmr::polymorphic_allocator<typename Type::value_type>(
                          where));
        }
        return Type(gathered, gathered.get_allocator());
      } else if constexpr (requires {
                             Type(gathered, gathered.get_allocator());
                           }) {
        return Type(gathered, gathered.get_allocator());
      } else {
        return gathered;
      }
    }();
    // The turn that was still going when the whole thing ended. Where the list
    // is written to be allowed none at all, there may not have been one.
    if (source.template took_part<Offset + 1>()) {
      auto last = finish_value<Root, element, Offset + 1, false, FailureType>(
          source, text, given);
      if (!last) return std::unexpected(std::move(last).error());
      append_to(made, std::move(*last));
    }
    return made;
  } else if constexpr (scanned_as_variant<Type>) {
    // Exactly one branch ran, and its mark says so: the mark of the branch
    // that took part was opened, and the others never were. The same question
    // the subject that can be pointed at answers by whether the group points
    // anywhere.
    return [&]<std::size_t... branch>(std::index_sequence<branch...>)
               -> std::expected<Type, FailureType> {
      std::optional<std::expected<Type, FailureType>> made;
      const auto take = [&]<std::size_t which>() {
        constexpr std::size_t mark =
            Offset + groups_before_branch<Type, which>();
        if (made || !source.template took_part<mark>()) return;
        using alternative = branch_at<Type, which>;
        auto part = finish_value<Root, alternative, mark + 1, false,
                                 FailureType>(source, text,
                                               told_for_part<which>(given));
        if (!part) {
          made = std::unexpected(std::move(part).error());
          return;
        }
        made = scan::branches<std::remove_cv_t<Type>>::template make<which>(
            std::move(*part));
      };
      (take.template operator()<branch>(), ...);
      if (!made) {
        return std::unexpected(scan::as_a_failure<FailureType>(
            no_match<>("no branch of the format took the input")));
      }
      return std::move(*made);
    }(std::make_index_sequence<branch_count<Type>()>{});
  } else if constexpr (scanned_from_values<Type>) {
    return finish_by_call<Root, Type, Offset, FailureType>(
        source, text, given, std::make_index_sequence<parts_of<Type>::count>{});
  } else {
    return finish_parts<Root, Type, Offset, FailureType>(
        source, text, given, std::make_index_sequence<parts_of<Type>::count>{});
  }
}

// A shape's places, gathered together, and what the walk has told it.
//
// This is the other way of answering the builder's questions. Where the machine
// keeps a gathering per register and works out which register holds a place,
// this keeps them all in one object -- which is what a type is handed when it
// is told its own groups, and it is told them because its places take turns.

}  // namespace scan::detail

#undef SCAN_FORCE_INLINE
