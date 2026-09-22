// What a context said at a place reaches, and how it gets there.
//
// A context is a thing of the caller's that the library has never heard of, and
// the one place it is wanted is the call that makes a value. Its type is worked
// out where it was said and stops there: what crosses the door is a pointer to
// an interface written for the field, so nothing here ever names a context type.
// Everything a reading builds with a resource -- a list, a string that keeps its
// own -- is built here as well, because that is the same question asked of a
// container rather than of a scanner.

export module scan.shape.contexts;

import std;
import scan.tre;
export import scan.compiler;
export import scan.runtime;
export import scan.shape.format;

#if defined(_MSC_VER)
#define SCAN_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define SCAN_FORCE_INLINE [[gnu::always_inline]] inline
#else
#define SCAN_FORCE_INLINE inline
#endif

namespace scan::detail {

template <class Type, std::size_t Group>
[[nodiscard]] consteval std::size_t branch_holding_group() {
  std::size_t found = 0;
  [&]<std::size_t... which>(std::index_sequence<which...>) {
    ((groups_before_branch<Type, which>() <= Group ? (found = which) : found),
     ...);
  }(std::make_index_sequence<branch_count<Type>()>{});
  return found;
}

// What a part of a place was told. A carrier knows its parts; a place told
// nothing has no parts to ask about and says the same nothing to each of them,
// which is what the whole-place forms do one level up.
export template <std::size_t Part, class CarrierType>
[[nodiscard]] constexpr auto told_for_part(CarrierType&& given) {
  if constexpr (requires { given.template for_part<Part>(); }) {
    return given.template for_part<Part>();
  } else {
    return given;
  }
}

// The two questions a carrier answers, asked so that a context answers them
// too.
//
// A place that is a shape hands what it was told to each of its parts, and what
// it was told may be a carrier or may be the context itself -- a braced list is
// the first, a context said for the whole place is the second. Everything below
// asks through these, so neither has to be turned into the other.
template <class CarrierType>
[[nodiscard]] constexpr bool told_apart_of() {
  if constexpr (requires { CarrierType::told_apart; }) {
    return CarrierType::told_apart;
  } else {
    return false;
  }
}

export template <class CarrierType>
[[nodiscard]] constexpr decltype(auto) leaf_of(CarrierType&& given) {
  if constexpr (requires { given.leaf(); }) {
    return given.leaf();
  } else {
    return (given);
  }
}

// Said before it is asked for: how many places a context may be said at for one
// field, which is worked out further down with the carriers.
export template <class FieldType>
[[nodiscard]] consteval std::size_t carrier_places();

// The context said at the place a group belongs to.
//
// The walk knows groups; the caller said places. This walks down the shape the
// same way the value is built, so a group of a place inside a place inside the
// output ends at the context that place was given.
export template <class Type, std::size_t Group, class Carrier>
[[nodiscard]] constexpr auto context_at_group(Carrier&& given) {
  if constexpr (scanned_as_variant<std::remove_cv_t<Type>> &&
                requires { given.template for_part<0>(); }) {
    // One of several opens up into its branches: the group belongs to the one
    // that stands where it stands, and that branch was told its own. Asked of
    // a place that was handed a context rather than a list of them, this is
    // not the way down -- the same one goes to whichever branch runs.
    using kind = std::remove_cv_t<Type>;
    constexpr std::size_t which = branch_holding_group<kind, Group>();
    constexpr std::size_t inside = Group - groups_before_branch<kind, which>();
    return context_at_group<std::remove_cv_t<branch_at<kind, which>>,
                            (inside == 0 ? 0 : inside - 1)>(
        told_for_part<which>(given));
  } else if constexpr (!requires { given.leaf(); } &&
                       !requires { given.template for_part<0>(); }) {
    // Already a context and not a carrier: a place that was handed one
    // directly hands the same one down. A carrier says one of these two --
    // a leaf answers `leaf`, a shape answers `for_part` -- and a shape used
    // to be mistaken for a context here, which is how a context said in
    // braces stopped at the door of a place that gathers.
    return given;
  } else if constexpr (carrier_places<std::remove_cv_t<Type>>() > 0 &&
                       requires { given.template for_part<0>(); }) {
    // A shape opens up into its parts, and so does a type whose places are a
    // format of its own: the carrier for such a place has a part for each of
    // them, and the group belongs to one of those.
    constexpr std::size_t which = field_holding_group<Type, Group>();
    using part = std::remove_cv_t<typename parts_of<Type>::template at<which>>;
    if constexpr (reads_its_own_groups<part> && carrier_places<part>() > 0) {
      // And there it stops: a place whose type says a format of its own is told
      // the carrier for that place, and what reaches the places inside is that
      // type's own business -- it is told them through the state it keeps, not
      // through this walk down.
      return given.template for_part<which>();
    } else if constexpr (reads_its_own_groups<part>) {
      // A type that folds its own groups has no places for a context to be
      // said at: what is inside is its own, and it is told the context whole.
      return leaf_of(given.template for_part<which>());
    } else {
      return context_at_group<part, Group - groups_before_field<Type, which>()>(
          given.template for_part<which>());
    }
  } else if constexpr (parts_under<std::remove_cv_t<Type>>() == 0) {
    return given.leaf();
  } else {
    constexpr std::size_t which = field_holding_group<Type, Group>();
    using part = std::remove_cv_t<typename parts_of<Type>::template at<which>>;
    return context_at_group<part, Group - groups_before_field<Type, which>()>(
        given.template for_part<which>());
  }
}

// One leaf read with a context, said as an interface that knows the field.
//
// The field is what the answer is made of, so the interface can be written out
// here, where the place is; the context is what the answer is made with, and it
// is the one thing this cannot name. So the interface is declared knowing the
// field and implemented knowing the context, and the implementation is one
// object per pair of them -- constant, shared, and alive as long as the program
// is. What crosses the door is a pointer to the interface and a pointer to the
// context, and both are pointers to something whose type is known again on the
// other side.
template <class Held, bool ToldApart, class ContextType>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::expected<Held, failure_for<Held>>
groups_value(std::span<const std::string_view> groups, ContextType&& given);

// The memory a context keeps, whatever it keeps it as: the library's own
// question, asked where a place builds something that allocates.
export template <class Told>
[[nodiscard]] constexpr std::pmr::memory_resource* resource_of(
    const Told& given);

// Said before what is below names them: a leaf carrier keeps a pointer to a
// reading and makes one as a default argument, and both of those are written
// out further down, where the context still has its type.
template <class FieldType>
struct reading_of;

// A fold in a reading, said as a slot in it. Written out below, where the
// interface it speaks to has been said.
template <class FieldType>
struct folding_in;

// How many readings of one field the walk can stand in at once, said as a
// number by whoever knows the machine. The carrier is handed it rather than
// working it out: what a reading is told is written where the contexts are,
// and the contexts are written where the format is not.
template <class FieldType, class ContextType, bool ToldApart,
          std::size_t Copies = 1>
struct reading_by;
// One context said for a whole shape needs one reading per leaf under it, and
// those readings have to outlive the call that says it. They are made as a
// default argument -- in the caller's own full expression -- and wired to the
// places here.
template <class Carrier, class It>
struct readings_for;

// One leaf read with a context, said as an interface that knows the field.
//
// The field is what the answer is made of, so the interface can be written out
// here, where the place is; the context is what the answer is made with, and it
// is the one thing this cannot name. So the type of the context is worked out
// where it is said -- deduced in the constructor below, from the thing itself
// -- and the reading that knows it is made there too, as a default argument, so
// it is alive for as long as the expression that said it. What crosses the door
// is a pointer to the interface, and nothing here ever names a context type.
export template <class FieldType, std::size_t Copies = 1>
class context_leaf {
 public:
  using held = std::remove_cv_t<FieldType>;
  using answer = std::expected<held, failure_for<held>>;
  static constexpr bool told_apart = false;

  constexpr context_leaf() = default;
  constexpr context_leaf(scan::default_context_t) {}

  // The context keeps whatever it was said as: a named thing stays that named
  // thing, a temporary lives to the end of the expression, and a const one
  // stays const because the caller wrote it that way -- nothing here adds a
  // const of its own.
  template <class It>
    requires(!std::same_as<std::remove_cvref_t<It>, context_leaf> &&
             !std::same_as<std::remove_cvref_t<It>, scan::default_context_t> &&
             !std::same_as<std::remove_cvref_t<It>, no_place>)
  constexpr context_leaf(
      It&& given,
      reading_by<held, std::remove_reference_t<It>, true, Copies>&& made = {})
      : how_(&made) {
    made.kept = &given;
  }

  // The same leaf, told a context that was said for the shape above it.
  template <class Store, class It>
  [[nodiscard]] static constexpr context_leaf wire(Store& made, It&& given) {
    made.kept = &given;
    context_leaf done;
    done.how_ = &made;
    return done;
  }

  // A leaf's inside is the leaf's own business: a context said at this place was
  // said about the value here, not about what this value is built from.
  template <std::size_t>
  [[nodiscard]] constexpr scan::no_contexts for_part() const {
    return {};
  }
  [[nodiscard]] constexpr const context_leaf& leaf() const { return *this; }

  [[nodiscard]] constexpr bool told() const { return how_ != nullptr; }
  // Whatever the interface hands back, which is said where the interface is --
  // further down, after what a carrier is has been worked out.
  // Said below, where the handle it hands back is written out.
  [[nodiscard]] constexpr folding_in<held> begin_fold() const;
  [[nodiscard]] constexpr auto begin_gather(std::string_view parameters) const {
    return how_->begin_gather(parameters);
  }
  [[nodiscard]] constexpr std::pmr::memory_resource* told_resource() const {
    return how_ == nullptr ? nullptr : how_->told_resource();
  }
  [[nodiscard]] constexpr answer read(std::string_view text,
                                      std::string_view parameters) const {
    return how_->read(text, parameters);
  }
  [[nodiscard]] constexpr answer read_groups(
      std::span<const std::string_view> groups) const {
    return how_->read_groups(groups);
  }

 private:
  reading_of<held>* how_ = nullptr;
};

export template <class... Parts>
class context_shape {
 public:
  static constexpr bool told_apart = true;

  constexpr context_shape() = default;
  constexpr context_shape(scan::default_context_t) {}

  // One value at a place is that place's and every part of it. The readings for
  // everything under it are made here, as a default argument, and wired below.
  template <class It>
    requires(!std::same_as<std::remove_cvref_t<It>, context_shape> &&
             !std::same_as<std::remove_cvref_t<It>, scan::default_context_t> &&
             !std::same_as<std::remove_cvref_t<It>, no_place>)
  constexpr context_shape(It&& given,
                          typename readings_for<context_shape, It>::type&& made =
                              {})
      : context_shape(wire(made, given)) {}

  // Or its parts, one by one, in braces -- all of them, because a shape whose
  // parts are being told apart is being told apart. Each part is a carrier, so
  // a part in braces goes as deep as the shape does, and a part said as a plain
  // context works out its own type where it stands.
  constexpr context_shape(Parts... given) : parts_(given...) {}

  template <class Store, class It>
  [[nodiscard]] static constexpr context_shape wire(Store& made, It&& given) {
    return wire_parts(made, given, std::index_sequence_for<Parts...>{});
  }

  template <std::size_t K>
  [[nodiscard]] constexpr auto for_part() const {
    if constexpr (K < sizeof...(Parts)) {
      return std::get<K>(parts_);
    } else {
      return scan::no_contexts{};
    }
  }

 private:
  template <class Store, class It, std::size_t... K>
  [[nodiscard]] static constexpr context_shape wire_parts(
      Store& made, It&& given, std::index_sequence<K...>) {
    return context_shape(Parts::wire(std::get<K>(made), given)...);
  }

  std::tuple<Parts...> parts_{};
};

template <class FieldType, std::size_t Copies, class It>
struct readings_for<context_leaf<FieldType, Copies>, It> {
  using type = reading_by<std::remove_cv_t<FieldType>,
                          std::remove_reference_t<It>, false, Copies>;
};

template <class It, class... Parts>
struct readings_for<context_shape<Parts...>, It> {
  using type = std::tuple<typename readings_for<Parts, It>::type...>;
};

template <class It>
struct readings_for<no_place, It> {
  using type = scan::no_contexts;
};

// How many places a context may be said at, for one field. A shape opens up
// into its parts; one of several opens up into its branches, because exactly
// one of them runs and the caller may want to say something to each; anything
// read whole is one place and takes one context.
export template <class FieldType>
[[nodiscard]] consteval std::size_t carrier_places() {
  if constexpr (scanned_as_variant<FieldType>) {
    return branch_count<FieldType>();
  } else if constexpr (requires { scan::scanner<FieldType>::says_a_format; }) {
    // A type whose places are a format of its own opens up the same way a
    // record does: the places inside it are where a context can be said.
    if constexpr (std::is_aggregate_v<FieldType> &&
                  requires { parts_of<FieldType>::count; }) {
      return parts_of<FieldType>::count;
    } else {
      return parts_under<FieldType>();
    }
  } else {
    return parts_under<FieldType>();
  }
}

template <class FieldType, std::size_t K>
[[nodiscard]] consteval auto carrier_place_kind() {
  if constexpr (scanned_as_variant<FieldType>) {
    return std::type_identity<std::remove_cv_t<branch_at<FieldType, K>>>{};
  } else {
    return std::type_identity<
        std::remove_cv_t<typename parts_of<FieldType>::template at<K>>>{};
  }
}

template <class FieldType, std::size_t K>
using carrier_place_for = typename decltype(carrier_place_kind<FieldType, K>())::type;

// Which carrier a field wants: read whole, and it is a leaf; opening up into
// places, and it is a shape over their carriers, worked out the same way.
template <class FieldType, std::size_t Copies = 1,
          bool Whole = (carrier_places<FieldType>() == 0)>
struct carrier_of;

template <class FieldType, std::size_t Copies>
struct carrier_of<FieldType, Copies, true> {
  using type = context_leaf<FieldType, Copies>;
};

template <class FieldType, std::size_t Copies>
struct carrier_of<FieldType, Copies, false> {
  template <std::size_t... K>
  static auto made(std::index_sequence<K...>)
      -> context_shape<typename carrier_of<carrier_place_for<FieldType, K>,
                                           Copies>::type...>;
  using type =
      decltype(made(std::make_index_sequence<carrier_places<FieldType>()>{}));
};

export template <class FieldType, std::size_t Copies = 1>
using carrier_for =
    typename carrier_of<std::remove_cv_t<FieldType>, Copies>::type;

template <class Type, std::size_t Place, std::size_t Copies = 1>
[[nodiscard]] consteval auto context_place_kind() {
  if constexpr (carrier_places<Type>() == 0) {
    if constexpr (Place == 0) {
      return std::type_identity<context_leaf<std::remove_cv_t<Type>, Copies>>{};
    } else {
      return std::type_identity<no_place>{};
    }
  } else if constexpr (Place < carrier_places<Type>()) {
    return std::type_identity<
        carrier_for<carrier_place_for<Type, Place>, Copies>>{};
  } else {
    return std::type_identity<no_place>{};
  }
}

template <class Type, std::size_t Place, std::size_t Copies = 1>
using context_place_of =
    typename decltype(context_place_kind<Type, Place, Copies>())::type;


// The state a scanner that gathers begins with. A scanner told a context and a
// scanner told none begin the same kind of state -- that is what lets a place
// hand the context over without the type of it crossing the door.
// The state a scanner that is handed pieces begins with -- the other gathering
// protocol, and the same rule: told a context or told none, what it begins is
// the same kind of thing, so a place can hand the context over without the type
// of it crossing the door.
// Written as a named function and not as a lambda called where it stands: a
// closure type belongs to the translation unit it was written in, and a name
// exported from a module may not be one -- which is a thing a module compiler
// is right to refuse.
template <class Held>
[[nodiscard]] constexpr auto gather_state_of() {
  if constexpr (requires {
                  scan::scanner<std::remove_cv_t<Held>>{}.begin(
                      std::string_view{});
                }) {
    return scan::scanner<std::remove_cv_t<Held>>{}.begin(std::string_view{});
  } else if constexpr (requires {
                         scan::scanner<std::remove_cv_t<Held>>{}.begin();
                       }) {
    return scan::scanner<std::remove_cv_t<Held>>{}.begin();
  } else {
    return scan::no_contexts{};
  }
}

template <class Held>
using gather_state_for = decltype(gather_state_of<Held>());

// What a fold begins with, said once for told and untold alike.
//
// A type that says a format of its own keeps what its places were told inside
// its state, so a state told something and a state told nothing would be two
// types -- and an interface hands back one. So the one it hands back is the
// state told the carrier written for that shape: a carrier knows nothing of the
// caller's types, and a shape told nothing is that same carrier with nothing in
// it.
export 
// What a reading has to keep for the places under it: readings for them, where
// the field is a shape, and nothing at all where it is read whole. Said in two
// pieces rather than one conditional, because a leaf's own carrier is a reading
// of that leaf -- naming it inside itself is a circle.
template <class Held, class ContextType, std::size_t Copies = 1,
          bool AShape = (carrier_places<std::remove_cv_t<Held>>() > 0)>
struct readings_under {
  using type = scan::no_contexts;
};

template <class Held, class ContextType, std::size_t Copies>
struct readings_under<Held, ContextType, Copies, true> {
  using type =
      typename readings_for<carrier_for<std::remove_cv_t<Held>, Copies>,
                            ContextType>::type;
};

template <class FieldType>
struct reading_of {
  using held = std::remove_cv_t<FieldType>;
  using answer = std::expected<held, failure_for<held>>;

  constexpr virtual ~reading_of() = default;
  // The resource the context keeps, where it keeps one. Answered rather than
  // named: a resource is already a thing asked at runtime.
  [[nodiscard]] constexpr virtual std::pmr::memory_resource* told_resource()
      const = 0;
  // The same, for a scanner handed its pieces rather than its groups.
  [[nodiscard]] constexpr virtual gather_state_for<held> begin_gather(
      std::string_view parameters) = 0;

  // A fold, kept where the context still has its type.
  //
  // Every one of these takes the slot the state lives in, because the walk
  // stands in several readings at once and each carries a fold of its own. How
  // many is the number the carrier was given, which is worked out from the
  // shape before any of this is built.
  [[nodiscard]] constexpr virtual std::size_t fold_begin() = 0;
  constexpr virtual void fold_opened(std::size_t slot, std::size_t which) = 0;
  constexpr virtual void fold_pushed(std::size_t slot, std::size_t which,
                                     char letter) = 0;
  constexpr virtual void fold_pushed_run(std::size_t slot, std::size_t which,
                                         std::string_view run) = 0;
  constexpr virtual void fold_closed(std::size_t slot, std::size_t which) = 0;
  constexpr virtual void fold_closed_on(std::size_t slot, std::size_t which,
                                        std::string_view text) = 0;
  [[nodiscard]] constexpr virtual answer fold_finish(std::size_t slot) = 0;
  // A reading divides: the state is kept as the scanner says it is kept, in a
  // slot of its own, and the walk that took the other road holds that one.
  [[nodiscard]] constexpr virtual std::size_t fold_kept(std::size_t slot) = 0;
  constexpr virtual void fold_back_to(std::size_t live, std::size_t kept) = 0;
  constexpr virtual void fold_drop(std::size_t slot) = 0;
  [[nodiscard]] constexpr virtual answer read(std::string_view text,
                                              std::string_view parameters) = 0;
  [[nodiscard]] constexpr virtual answer read_groups(
      std::span<const std::string_view> groups) = 0;
};

// A fold in a reading, said as a slot in it.
//
// This is what a place told in braces carries instead of a state: the same
// type whatever the context is, and every turn of the fold a call through the
// interface. Copying one is a reading dividing, which is what the scanner's
// own `keep_groups` is for; assigning one is a reading going back.
template <class Held>
[[nodiscard]] constexpr auto begun_alone() {
  if constexpr (requires { scan::scanner<std::remove_cv_t<Held>>{}.begin_groups(); }) {
    return scan::scanner<std::remove_cv_t<Held>>{}.begin_groups();
  } else {
    return scan::no_contexts{};
  }
}

template <class FieldType>
struct folding_in {
  using held = std::remove_cv_t<FieldType>;
  using answer = std::expected<held, failure_for<held>>;
  // A place in a braced list that was written `scan::default_context` carries
  // a leaf with nothing in it. The fold still runs, so the state is here
  // instead -- named, because a scanner told nothing has one type for it.
  using alone_type = decltype(begun_alone<held>());

  reading_of<held>* how = nullptr;
  std::size_t slot = 0;
  alone_type alone{};

  [[deprecated("untold")]] constexpr folding_in() : alone(begun_alone<held>()) {}
  constexpr explicit folding_in(reading_of<held>* from) : how(from) {
    if (how != nullptr) {
      slot = how->fold_begin();
    } else {
      alone = begun_alone<held>();
    }
  }
  constexpr folding_in(const folding_in& other)
      : how(other.how),
        slot(other.how == nullptr ? 0 : other.how->fold_kept(other.slot)),
        alone(other.how == nullptr ? kept_groups<held>(other.alone)
                                   : alone_type{}) {}
  constexpr folding_in& operator=(const folding_in& other) {
    if (this == &other) return *this;
    if (how != nullptr && other.how != nullptr) {
      how->fold_back_to(slot, other.slot);
      return *this;
    }
    if (how != nullptr) how->fold_drop(slot);
    how = other.how;
    if (how != nullptr) {
      slot = how->fold_kept(other.slot);
    } else {
      slot = 0;
      groups_go_back_to<held>(alone, other.alone);
    }
    return *this;
  }
  constexpr folding_in(folding_in&& other) noexcept
      : how(other.how), slot(other.slot) {
    other.how = nullptr;
  }
  constexpr folding_in& operator=(folding_in&& other) noexcept {
    if (this != &other) {
      if (how != nullptr) how->fold_drop(slot);
      how = other.how;
      slot = other.slot;
      other.how = nullptr;
    }
    return *this;
  }
  constexpr ~folding_in() {
    if (how != nullptr) how->fold_drop(slot);
  }

  template <std::size_t Which>
  constexpr void opened() {
    if (how != nullptr) how->fold_opened(slot, Which);
    else open_one_group<held, Which>(alone);
  }
  template <std::size_t Which>
  constexpr void pushed(char letter) {
    if (how != nullptr) how->fold_pushed(slot, Which, letter);
    else push_one_group<held, Which>(alone, letter);
  }
  template <std::size_t Which>
  constexpr void pushed(std::string_view run) {
    if (how != nullptr) how->fold_pushed_run(slot, Which, run);
    else push_one_group<held, Which>(alone, run);
  }
  template <std::size_t Which>
  constexpr void closed() {
    if (how != nullptr) how->fold_closed(slot, Which);
    else close_one_group<held, Which>(alone);
  }
  template <std::size_t Which>
  constexpr void closed(std::string_view text) {
    if (how != nullptr) how->fold_closed_on(slot, Which, text);
    else close_one_group<held, Which>(alone, text);
  }
  [[nodiscard]] constexpr answer finish() {
    if (how != nullptr) return how->fold_finish(slot);
    if constexpr (scan::says_what_went_wrong_folding<held>) {
      return scan::scanner_told_finish_groups<held, scan::hands_a_failure_back>(
          std::move(alone));
    } else {
      return answer(scan::finished_groups<held>(std::move(alone)));
    }
  }
};

template <class FieldType, std::size_t Copies>
[[nodiscard]] constexpr folding_in<std::remove_cv_t<FieldType>>
context_leaf<FieldType, Copies>::begin_fold() const {
  return folding_in<held>(how_);
}

// The implementation is written where the context still has its type, so it
// holds the caller's own thing -- not a copy of it, and not a const picture of
// it either: a context is a place to keep things while a reading runs, and a
// scanner that is told one may write in it. It is made at the place the context
// is said, as a default argument, so it lives exactly as long as the call.
template <class FieldType, class ContextType, bool ToldApart,
          std::size_t Copies>
struct reading_by final : reading_of<FieldType> {
  using held = std::remove_cv_t<FieldType>;
  static_assert(
      !scan::takes_its_context_deduced<held>,
      "this scanner says takes_its_context_deduced: a braced list of contexts "
      "erases their types before it is asked, so say them without braces -- "
      "one a place, or scan::parts{...} for the parts of one place");
  using answer = std::expected<held, failure_for<held>>;

  ContextType* kept = nullptr;
  // The readings for the places under this one, where this is a shape. They
  // live here because they have to outlive what they are wired into, and what
  // they are wired into is a state the walk carries about.
  [[no_unique_address]] typename readings_under<held, ContextType,
                                                Copies>::type under_{};

  static constexpr bool by_groups =
      requires { scan::scanner<held>{}.begin_groups(); } ||
      requires(std::span<const std::string_view> some) {
        scan::scanner<held>{}.from_groups(some);
      };

  // Both of these are written out with the table whether anybody calls them or
  // not, so each has to say what it does for a leaf that is read the other way
  // -- and saying it is an answer, not a failure to compile.
  static constexpr bool takes_a_context =
      requires(std::string_view text, std::string_view parameters,
               ContextType& told) {
        scan::scanner<held>{}.parse(text, told);
      } || requires(std::string_view text, std::string_view parameters,
                    ContextType& told) {
        scan::scanner<held>{}.parse(text, parameters, told);
      } || requires(std::string_view text, ContextType& told) {
        scan::scanner<held>::try_parse(text, told);
      } || requires(std::string_view text, std::string_view parameters,
                    ContextType& told) {
        scan::scanner<held>::try_parse(text, parameters, told);
      };

  static constexpr bool reads_a_piece =
      requires(std::string_view text) { scan::scanner<held>{}.parse(text); } ||
      requires(std::string_view text) { scan::scanner<held>::try_parse(text); };

  [[nodiscard]] constexpr std::pmr::memory_resource* told_resource()
      const override {
    return resource_of(*kept);
  }

  [[nodiscard]] constexpr gather_state_for<held> begin_gather(
      std::string_view parameters) override {
    if constexpr (requires {
                    scan::scanner<held>{}.begin(parameters, *kept);
                  }) {
      return scan::scanner<held>{}.begin(parameters, *kept);
    } else if constexpr (requires { scan::scanner<held>{}.begin(*kept); }) {
      return scan::scanner<held>{}.begin(*kept);
    } else if constexpr (requires { scan::scanner<held>{}.begin(parameters); }) {
      return scan::scanner<held>{}.begin(parameters);
    } else if constexpr (requires { scan::scanner<held>{}.begin(); }) {
      return scan::scanner<held>{}.begin();
    } else {
      return scan::no_contexts{};
    }
  }

  // The fold itself, kept here.
  //
  // The scanner is asked with the caller's own thing, so its hooks may be
  // templates on that type and its state may be of a piece with it. None of
  // that crosses the door: what the walk carries is a slot number.
  [[nodiscard]] static constexpr auto begun_here(ContextType* told) {
    if constexpr (requires { scan::scanner<held>{}.begin_groups(*told); }) {
      return scan::scanner<held>{}.begin_groups(*told);
    } else if constexpr (requires { scan::scanner<held>{}.begin_groups(); }) {
      return scan::scanner<held>{}.begin_groups();
    } else {
      return scan::no_contexts{};
    }
  }
  // Every leaf a braced list can reach has these, because they are virtual;
  // only a leaf that folds its groups has anything to do in them.
  static constexpr bool folds =
      requires { scan::scanner<held>{}.begin_groups(); } ||
      requires(ContextType* told) {
        scan::scanner<held>{}.begin_groups(*told);
      };
  using fold_state =
      std::conditional_t<folds, decltype(begun_here(std::declval<ContextType*>())),
                         scan::no_contexts>;
  static constexpr std::size_t inside = groups_a_leaf_opens<held>();

  // Grown rather than counted out.
  //
  // How many readings of one place the walk stands in at once is not the
  // number this carrier was given: the walk keeps a state for every register
  // as well, and copies one for every road it tries. Counting that out here
  // would be counting the machine's shape from the wrong side, and coming up
  // short means two readings sharing a slot -- which is not an error anybody
  // sees, it is an answer built out of what another reading gathered.
  std::vector<std::optional<fold_state>> slots_{};

  [[nodiscard]] constexpr std::size_t fold_begin() override {
    if constexpr (folds) {
      const std::size_t slot = a_free_slot();
      slots_[slot] = begun_here(kept);
      return slot;
    } else {
      return 0;
    }
  }

  [[nodiscard]] constexpr std::size_t a_free_slot() {
    for (std::size_t at = 0; at < slots_.size(); ++at) {
      if (!slots_[at].has_value()) return at;
    }
    slots_.emplace_back();
    return slots_.size() - 1;
  }
  constexpr void fold_opened(std::size_t slot, std::size_t which) override {
    if constexpr (folds) {
      at_group(which, [&]<std::size_t k>() {
        open_one_group<held, k>(*slots_[slot]);
      });
    }
  }
  constexpr void fold_pushed(std::size_t slot, std::size_t which,
                             char letter) override {
    if constexpr (folds) {
      at_group(which, [&]<std::size_t k>() {
        push_one_group<held, k>(*slots_[slot], letter);
      });
    }
  }
  constexpr void fold_pushed_run(std::size_t slot, std::size_t which,
                                 std::string_view run) override {
    if constexpr (folds) {
      at_group(which, [&]<std::size_t k>() {
        push_one_group<held, k>(*slots_[slot], run);
      });
    }
  }
  constexpr void fold_closed(std::size_t slot, std::size_t which) override {
    if constexpr (folds) {
      at_group(which, [&]<std::size_t k>() {
        close_one_group<held, k>(*slots_[slot]);
      });
    }
  }
  constexpr void fold_closed_on(std::size_t slot, std::size_t which,
                                std::string_view text) override {
    if constexpr (folds) {
      at_group(which, [&]<std::size_t k>() {
        close_one_group<held, k>(*slots_[slot], text);
      });
    }
  }
  [[nodiscard]] constexpr answer fold_finish(std::size_t slot) override {
    if constexpr (!folds) {
      return answer(std::unexpected(scan::as_a_failure<failure_for<held>>(
          scan::no_match<>("this place does not fold its groups"))));
    } else if constexpr (scan::says_what_went_wrong_folding<held>) {
      return scan::scanner_told_finish_groups<held, scan::hands_a_failure_back>(
          std::move(*slots_[slot]));
    } else {
      return scan::finished_groups<held>(std::move(*slots_[slot]));
    }
  }
  [[nodiscard]] constexpr std::size_t fold_kept(std::size_t slot) override {
    if constexpr (!folds) {
      return slot;
    } else {
      const std::size_t free = a_free_slot();
      slots_[free] = kept_groups<held>(*slots_[slot]);
      return free;
    }
  }
  constexpr void fold_back_to(std::size_t live, std::size_t kept_at) override {
    if constexpr (folds) {
      groups_go_back_to<held>(*slots_[live], *slots_[kept_at]);
    }
  }
  constexpr void fold_drop(std::size_t slot) override {
    slots_[slot].reset();
  }

 private:
  // The walk knows a group by a number it has while it is compiled; the
  // interface says one at run time, so it is turned back into the other here.
  template <class Body>
  constexpr void at_group(std::size_t which, Body&& body) {
    [&]<std::size_t... k>(std::index_sequence<k...>) {
      ((k == which ? body.template operator()<k>() : void()), ...);
    }(std::make_index_sequence<inside>{});
  }

 public:

  [[nodiscard]] constexpr answer read(std::string_view text,
                                      std::string_view parameters) override {
    if constexpr (takes_a_context) {
      return parse_value_given<held, failure_for<held>, ToldApart>(
          text, parameters, *kept);
    } else if constexpr (reads_a_piece) {
      static_assert(!ToldApart || by_groups,
                    "this place was given a context of its own and its scanner "
                    "takes none: write parse(string_view, context) on "
                    "scan::scanner<T>, or write scan::default_context in its "
                    "place");
      return parse_value<held, failure_for<held>>(text, parameters);
    } else {
      static_cast<void>(text);
      static_cast<void>(parameters);
      return std::unexpected(scan::as_a_failure<failure_for<held>>(
          scan::no_group<>("this place is not read from a piece")));
    }
  }

  // A virtual is written out with the table, whether anybody calls it or not,
  // so a leaf that is not read from its groups must still have something here
  // -- and what it has says so rather than failing to compile.
  [[nodiscard]] constexpr answer read_groups(
      std::span<const std::string_view> groups) override {
    if constexpr (by_groups) {
      return groups_value<held, ToldApart>(groups, *kept);
    } else {
      static_cast<void>(groups);
      return std::unexpected(scan::as_a_failure<failure_for<held>>(
          scan::no_group<>("this place is not read from its groups")));
    }
  }
};

// The memory resource a context keeps, where it keeps one.
//
export template <class Told>
[[nodiscard]] constexpr std::pmr::memory_resource* resource_of(
    const Told& given) {
  using kind = std::remove_cvref_t<Told>;
  if constexpr (std::same_as<kind, std::pmr::memory_resource*>) {
    return given;
  } else if constexpr (requires { given.resource(); }) {
    return given.resource();
  } else if constexpr (requires { given.get_allocator().resource(); }) {
    return given.get_allocator().resource();
  } else if constexpr (requires { given.told_resource(); }) {
    return given.told_resource();
  } else if constexpr (requires { given.leaf(); }) {
    // A context said at a call is carried in a wrapper -- one for everybody,
    // one per place -- and what it keeps is inside. Asked after the carrier,
    // because a carrier answers `leaf` with itself.
    return resource_of(given.leaf());
  } else {
    static_cast<void>(given);
    return nullptr;
  }
}

// A list built where its place said to build it. A container that takes an
// allocator is given the one its place was told about; one that takes none is
// made the way it always was.
export template <class Held, std::size_t Most = turns_unbounded, class Told>
[[nodiscard]] constexpr Held made_range(const Told& given) {
  // What the format could ask for, against what this container holds. A
  // container that says nothing says nothing here either.
  if constexpr (requires { scan::room_for<Held>::most; }) {
    static_assert(Most <= scan::room_for<Held>::most,
                  "this place may take more turns than the container it is "
                  "read into has room for: say a count in braces after the "
                  "place, or read it into something with more room");
  }
  const auto begun = [&] {
    if constexpr (requires {
                    typename Held::value_type;
                    Held(std::pmr::polymorphic_allocator<
                         typename Held::value_type>{});
                  }) {
      if (std::pmr::memory_resource* where = resource_of(given)) {
        return Held(
            std::pmr::polymorphic_allocator<typename Held::value_type>(where));
      }
      return Held{};
    } else {
      static_cast<void>(given);
      return Held{};
    }
  };
  Held made = begun();
  // Room for everything the format could ask for, taken once. A list whose
  // count has no end asks for nothing here: what it will be is not known, and
  // guessing it is the container's business and not this one's.
  if constexpr (Most != turns_unbounded) {
    if constexpr (requires { made.reserve(Most); }) made.reserve(Most);
  }
  return made;
}

// An empty list of the same kind as one that stands here already, keeping the
// resource that one was made with. A turn ending and the next one beginning is
// not a reason to go back to the default resource.
export template <class Held>
[[nodiscard]] constexpr Held made_like(const Held& other) {
  if constexpr (requires { typename Held::allocator_type; }) {
    return Held(other.get_allocator());
  } else {
    static_cast<void>(other);
    return Held{};
  }
}

// A scanner begun with the context its place was given, asked for in the shapes
// it may have been written in, and begun the way it always was where it takes
// none.
export template <class Held, class CarrierType>
[[nodiscard]] SCAN_FORCE_INLINE constexpr auto scanner_begin_given(
    std::string_view parameters,
                                                 const CarrierType& told) {
  // Only where what the carrier begins is the very thing this call hands back:
  // a scanner with no gathering of its own begins nothing, and the branches
  // below must all agree on one return type.
  if constexpr (requires {
                  told.told();
                  told.begin_gather(parameters);
                  requires std::same_as<decltype(told.begin_gather(parameters)),
                                        decltype(scanner_begin<Held>(parameters))>;
                }) {
    // A place told in braces: the context is behind an interface that knows the
    // field, and beginning is part of that interface.
    if (told.told()) return told.begin_gather(parameters);
    return scanner_begin<Held>(parameters);
  } else if constexpr (!std::same_as<CarrierType, scan::default_context_t> &&
                requires { scan::scanner<Held>{}.begin(parameters, told); }) {
    return scan::scanner<Held>{}.begin(parameters, told);
  } else if constexpr (!std::same_as<CarrierType, scan::default_context_t> &&
                       requires { scan::scanner<Held>{}.begin(told); }) {
    return scan::scanner<Held>{}.begin(told);
  } else if constexpr (requires {
                         scan::scanner<Held>{}.begin(
                             parameters, std::pmr::polymorphic_allocator<>{});
                       }) {
    // The place was told something that keeps a resource, and this scanner
    // knows what to do with one. Nobody wrote anything for this to happen.
    if (std::pmr::memory_resource* where = resource_of(told)) {
      return scan::scanner<Held>{}.begin(
          parameters, std::pmr::polymorphic_allocator<>(where));
    }
    return scanner_begin<Held>(parameters);
  } else {
    static_cast<void>(told);
    return scanner_begin<Held>(parameters);
  }
}

// The state of a leaf that is built from its own groups, told the context its
// place was given. The same rule as everywhere: asked for with the context
// first, and a scanner that takes none is begun the way it always was.
export template <class Held, class Context>
[[nodiscard]] SCAN_FORCE_INLINE constexpr auto begun_groups(Context&& given) {
  // A place told in braces carries its context behind an interface that knows
  // the field. The type of the context does not cross the door, but the
  // beginning of a fold does: the carrier begins it where the type is still
  // known and hands back the state the scanner would have begun anyway.
  if constexpr (requires {
                  given.told();
                  given.begin_fold();
                }) {
    // One type either way: the handle knows whether there is a reading behind
    // it, and keeps the state itself where there is not.
    return given.begin_fold();
  } else if constexpr (!std::same_as<std::remove_cvref_t<Context>,
                              scan::default_context_t> &&
                !requires { given.told(); } &&
                requires { scan::scanner<Held>{}.begin_groups(given); }) {
    return scan::scanner<Held>{}.begin_groups(given);
  } else {
    static_cast<void>(given);
    return scan::scanner<Held>{}.begin_groups();
  }
}

// A leaf read from its own groups, told the context its place was given.
//
// Everything this leaf needs is here at once -- the groups are already found --
// so nothing of it crosses the door: the state is made, told and finished
// inside this one call, where the context still has its type.
template <class Held, bool ToldApart, class ContextType>
[[nodiscard]] SCAN_FORCE_INLINE constexpr std::expected<Held, failure_for<Held>>
groups_value(std::span<const std::string_view> groups, ContextType&& given) {
  constexpr std::size_t inside = groups_a_leaf_opens<Held>();
  if constexpr (requires {
                  scan::scanner<Held>{}.from_groups(groups, given);
                }) {
    return scan::scanner<Held>{}.from_groups(groups, given);
  } else if constexpr (requires { scan::scanner<Held>{}.from_groups(groups); }) {
    return scan::scanner<Held>{}.from_groups(groups);
  } else {
    auto state = begun_groups<Held>(given);
    [&]<std::size_t... at>(std::index_sequence<at...>) {
      ((void)[&] {
        if (groups[at].data() == nullptr) return;
        open_one_group<Held, at>(state);
        close_one_group<Held, at>(state, groups[at]);
      }(), ...);
    }(std::make_index_sequence<inside>{});
    return scan::finished_groups<Held>(std::move(state));
  }
}

// The parts of a value, each built and handed over where the value is made.
//
// Named rather than written as a lambda where they are used, and written into
// whoever asks: a context said in braces crosses the door as a reading, and a
// body left out of line cannot see which reading it holds -- so every place
// would cost a call through the interface. Inlined, the caller still holds the
// readings and every one of those calls is direct. Nothing is asked of the
// caller for that; it is the library's own road that is written out.
template <class Parameters, class Type, std::size_t Offset, class CarrierType,
          std::size_t... Index>
[[nodiscard]] SCAN_FORCE_INLINE constexpr Type built_of_parts(
    std::span<const std::string_view> groups, const CarrierType& given,
    std::index_sequence<Index...>);

template <class Parameters, class Type, std::size_t Offset, class CarrierType,
          std::size_t... Index>
[[nodiscard]] SCAN_FORCE_INLINE constexpr Type made_of_parts(
    std::span<const std::string_view> groups, const CarrierType& given,
    std::index_sequence<Index...>);

// The value itself, for a reading that cannot go wrong.
template <class Parameters, class Type, std::size_t Offset,
          bool AsOutput = false, class CarrierType = scan::no_contexts>
[[nodiscard]] SCAN_FORCE_INLINE constexpr Type built_value(
    std::span<const std::string_view> groups,
    const CarrierType& given = CarrierType{}) {
  constexpr bool a_value = scanned_as_leaf<Type> && !AsOutput;
  if constexpr (a_value) {
    if constexpr (std::same_as<CarrierType, scan::no_contexts>) {
      return scanner_parse<std::remove_cv_t<Type>>(groups[Offset],
                                                   Parameters::at(Offset));
    } else {
      return or_thrown(
          parse_value_given<std::remove_cv_t<Type>,
                            failure_for<std::remove_cv_t<Type>>,
                            told_apart_of<CarrierType>(),
                            scan::throws_a_failure>(
              groups[Offset], Parameters::at(Offset), leaf_of(given)));
    }
  } else if constexpr (scanned_from_values<Type>) {
    return made_of_parts<Parameters, Type, Offset>(
        groups, given, std::make_index_sequence<parts_of<Type>::count>{});
  } else {
    return built_of_parts<Parameters, Type, Offset>(
        groups, given, std::make_index_sequence<parts_of<Type>::count>{});
  }
}

template <class Parameters, class Type, std::size_t Offset, class CarrierType,
          std::size_t... Index>
[[nodiscard]] SCAN_FORCE_INLINE constexpr Type built_of_parts(
    std::span<const std::string_view> groups, const CarrierType& given,
    std::index_sequence<Index...>) {
  return Type{built_value<Parameters, typename parts_of<Type>::template at<Index>,
                          Offset + groups_before_field<Type, Index>()>(
      groups, told_for_part<Index>(given))...};
}

template <class Parameters, class Type, std::size_t Offset, class CarrierType,
          std::size_t... Index>
[[nodiscard]] SCAN_FORCE_INLINE constexpr Type made_of_parts(
    std::span<const std::string_view> groups, const CarrierType& given,
    std::index_sequence<Index...>) {
  return scan::scanner<std::remove_cv_t<Type>>{}.parse(
      built_value<Parameters, typename parts_of<Type>::template at<Index>,
                  Offset + groups_before_field<Type, Index>()>(
          groups, told_for_part<Index>(given))...);
}

export template <class FailureType, class Parameters, class Type,
          std::size_t Offset, bool AsOutput = false,
          class Ending = hands_a_failure_back,
          class CarrierType = scan::no_contexts>
[[nodiscard]] SCAN_FORCE_INLINE constexpr
    typename Ending::template result<Type, FailureType>
    build_value(std::span<const std::string_view> groups,
            const CarrierType& given = CarrierType{}) {
  // Where this is the whole of what is being read, a shape that reads its own
  // groups is a product of places rather than a value in a place.
  constexpr bool a_value = scanned_as_leaf<Type> && !AsOutput;
  if constexpr (never_fails<Type, AsOutput>() &&
                !std::same_as<Ending, throws_a_failure>) {
    // Nothing here can hand a failure back, so nothing here holds one -- even
    // where the caller asked to be handed one.
    return built_value<Parameters, Type, Offset, AsOutput>(groups, given);
  } else if constexpr (a_value && reads_its_own_groups<Type>) {
    // The type's own groups are groups of this match, already found. It is
    // handed them, or told which of them each character belongs to -- the same
    // reading it gets where a subject arrives as it is read, so it reads the
    // same way in both places.
    using held = std::remove_cv_t<Type>;
    constexpr std::size_t inside = groups_a_leaf_opens<held>();
    // Where this place was told a context, the whole of this reading is that
    // context's: the groups are cut out here and handed over in one call.
    if constexpr (requires {
                    leaf_of(given).told();
                    leaf_of(given).read_groups(
                        std::span<const std::string_view>{});
                  }) {
      const auto& told_here = leaf_of(given);
      if (told_here.told()) {
        std::array<std::string_view, inside> mine{};
        [&]<std::size_t... at>(std::index_sequence<at...>) {
          ((mine[at] = groups[Offset + 1 + at]), ...);
        }(std::make_index_sequence<inside>{});
        auto got = told_here.read_groups(std::span<const std::string_view>(mine));
        if (got) return std::move(*got);
        return Ending::template went_wrong<Type, FailureType>(
            scan::as_a_failure<FailureType>(std::move(got).error()));
      }
    }
    if constexpr (scan::says_what_went_wrong_from_groups<held> ||
                  requires(std::span<const std::string_view> given) {
                    scan::scanner<held>{}.from_groups(given);
                  }) {
      std::array<std::string_view, inside> theirs{};
      [&]<std::size_t... at>(std::index_sequence<at...>) {
        ((theirs[at] = groups[Offset + 1 + at]), ...);
      }(std::make_index_sequence<inside>{});
      const auto pieces = std::span<const std::string_view>(theirs);
      if constexpr (scan::says_what_went_wrong_from_groups<held>) {
        // Told where the type can take it, and asked the old way where it
        // cannot: a shape that reads its own groups need not take a context.
        auto got = scan::told_from_groups<held, Ending>(pieces, given);
        if (got) return std::move(*got);
        return Ending::template went_wrong<Type, FailureType>(
            std::move(got).error());
      } else if constexpr (requires {
                             scan::scanner<held>{}.from_groups(pieces,
                                                               leaf_of(given));
                           }) {
        // The place's own context and not the carrier holding it: a carrier
        // said in braces is the leaf already, one deduced at the call is a
        // wrapper around the caller's thing, and a scanner was written to take
        // the caller's thing. Asked with the wrapper it does not match, and the
        // road below -- the one for a scanner that takes no context at all --
        // was taken instead, quietly.
        return scan::scanner<held>{}.from_groups(pieces, leaf_of(given));
      } else {
        return scan::told_from_groups_plain<held>(pieces, given);
      }
    } else {
      auto state = begun_groups<held>(leaf_of(given));
      [&]<std::size_t... at>(std::index_sequence<at...>) {
        ((void)[&] {
          // A group that took no part in the match is not opened at all, which
          // is how the type is told it was not there.
          if (groups[Offset + 1 + at].data() == nullptr) return;
          open_one_group<held, at>(state);
          close_one_group<held, at>(state, groups[Offset + 1 + at]);
        }(), ...);
      }(std::make_index_sequence<inside>{});
      if constexpr (scan::says_what_went_wrong_folding<held>) {
        auto got =
            scan::scanner_told_finish_groups<held, Ending>(std::move(state));
        if (got) return std::move(*got);
        return Ending::template went_wrong<Type, FailureType>(
            std::move(got).error());
      } else {
        return scan::finished_groups<held>(std::move(state));
      }
    }
  } else if constexpr (a_value) {
    auto got = [&] {
      if constexpr (std::same_as<CarrierType, scan::no_contexts>) {
        return parse_value<std::remove_cv_t<Type>, FailureType>(
            groups[Offset], Parameters::at(Offset));
      } else {
        return parse_value_given<std::remove_cv_t<Type>, FailureType,
                                 told_apart_of<CarrierType>()>(
            groups[Offset], Parameters::at(Offset), leaf_of(given));
      }
    }();
    if (got) return std::move(*got);
    return Ending::template went_wrong<Type, FailureType>(
        std::move(got).error());
  } else if constexpr (scanned_as_variant<Type>) {
    // Exactly one branch ran, and its mark says so: a mark that took part
    // points into the subject, and the others point nowhere.
    using answer = typename Ending::template result<Type, FailureType>;
    return [&]<std::size_t... branch>(std::index_sequence<branch...>) -> answer {
      std::optional<answer> made;
      const auto take = [&]<std::size_t which>() {
        constexpr std::size_t mark = Offset + groups_before_branch<Type, which>();
        if (made || groups[mark].data() == nullptr) return;
        using alternative = branch_at<Type, which>;
        auto part = build_value<FailureType, Parameters, alternative, mark + 1,
                                false, Ending>(groups,
                                               told_for_part<which>(given));
        if (!Ending::read(part)) {
          made = Ending::template went_wrong<Type, FailureType>(
              Ending::failure(std::move(part)));
          return;
        }
        made = scan::branches<std::remove_cv_t<Type>>::template make<which>(
            Ending::value(std::move(part)));
      };
      (take.template operator()<branch>(), ...);
      if (!made) {
        return Ending::template went_wrong<Type, FailureType>(
            no_match<>("no branch of the format took the input"));
      }
      return std::move(*made);
    }(std::make_index_sequence<branch_count<Type>()>{});
  } else if constexpr (scanned_from_values<Type>) {
    // Made by the call it named, out of the values its places stood for. Each
    // of them is read first and the call is made after, because a value that
    // did not read is not an argument.
    using answer = typename Ending::template result<Type, FailureType>;
    return [&]<std::size_t... index>(std::index_sequence<index...>) -> answer {
      // Asked for a value, every step is the value it read and the call is
      // written out of them where they stand. Asked to try, each is held until
      // they are all in hand, because a call cannot be half made.
      if constexpr (std::same_as<Ending, throws_a_failure>) {
        return scan::scanner<std::remove_cv_t<Type>>{}.parse(
            build_value<FailureType, Parameters,
                        typename parts_of<Type>::template at<index>,
                        Offset + groups_before_field<Type, index>(), false,
                        Ending>(groups, told_for_part<index>(given))...);
      } else {
        auto parts = std::tuple{build_value<
            FailureType, Parameters, typename parts_of<Type>::template at<index>,
            Offset + groups_before_field<Type, index>(), false, Ending>(
            groups, told_for_part<index>(given))...};
        if (auto went_wrong = what_went_wrong<FailureType>(parts)) {
          return std::unexpected(std::move(*went_wrong));
        }
        return scan::scanner<std::remove_cv_t<Type>>{}.parse(
            std::move(*std::get<index>(parts))...);
      }
    }(std::make_index_sequence<parts_of<Type>::count>{});
  } else {
    using answer = typename Ending::template result<Type, FailureType>;
    return [&]<std::size_t... index>(std::index_sequence<index...>) -> answer {
      // The same two ways: written straight into the value, or held until they
      // are all in hand.
      if constexpr (std::same_as<Ending, throws_a_failure>) {
        return Type{build_value<FailureType, Parameters,
                                typename parts_of<Type>::template at<index>,
                                Offset + groups_before_field<Type, index>(),
                                false, Ending>(
            groups, told_for_part<index>(given))...};
      } else {
        auto parts = std::tuple{build_value<
            FailureType, Parameters, typename parts_of<Type>::template at<index>,
            Offset + groups_before_field<Type, index>(), false, Ending>(
            groups, told_for_part<index>(given))...};
        if (auto went_wrong = what_went_wrong<FailureType>(parts)) {
          return std::unexpected(std::move(*went_wrong));
        }
        return Type{std::move(*std::get<index>(parts))...};
      }
    }(std::make_index_sequence<parts_of<Type>::count>{});
  }
}

}  // namespace scan::detail

#undef SCAN_FORCE_INLINE
