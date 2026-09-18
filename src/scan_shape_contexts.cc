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
export import scan.shape.places;

#if defined(_MSC_VER)
#define SCAN_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define SCAN_FORCE_INLINE [[gnu::always_inline]] inline
#else
#define SCAN_FORCE_INLINE inline
#endif

export namespace scan::detail {

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
template <std::size_t Part, class CarrierType>
[[nodiscard]] constexpr auto told_for_part(const CarrierType& given) {
  if constexpr (requires { given.template for_part<Part>(); }) {
    return given.template for_part<Part>();
  } else {
    return given;
  }
}

// The context said at the place a group belongs to.
//
// The walk knows groups; the caller said places. This walks down the shape the
// same way the value is built, so a group of a place inside a place inside the
// output ends at the context that place was given.
template <class Type, std::size_t Group, class Carrier>
[[nodiscard]] constexpr auto context_at_group(const Carrier& given) {
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
[[nodiscard]] constexpr std::expected<Held, failure_for<Held>> groups_value(
    std::span<const std::string_view> groups, ContextType&& given);

template <class Told>
[[nodiscard]] constexpr std::pmr::memory_resource* resource_of(const Told& given);

// The state a scanner that gathers begins with. A scanner told a context and a
// scanner told none begin the same kind of state -- that is what lets a place
// hand the context over without the type of it crossing the door.
// The state a scanner that is handed pieces begins with -- the other gathering
// protocol, and the same rule: told a context or told none, what it begins is
// the same kind of thing, so a place can hand the context over without the type
// of it crossing the door.
template <class Held>
using gather_state_for = decltype([] {
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
}());

template <class Held>
using fold_state_for = decltype([] {
  if constexpr (requires { scan::scanner<std::remove_cv_t<Held>>{}.begin_groups(); }) {
    return scan::scanner<std::remove_cv_t<Held>>{}.begin_groups();
  } else {
    return scan::no_contexts{};
  }
}());

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
  // Begun where the context still has its type, handed back as the state the
  // scanner would have begun anyway.
  [[nodiscard]] constexpr virtual fold_state_for<held> begin_fold() = 0;
  [[nodiscard]] constexpr virtual answer read(std::string_view text,
                                              std::string_view parameters) = 0;
  [[nodiscard]] constexpr virtual answer read_groups(
      std::span<const std::string_view> groups) = 0;
};

// The implementation is written where the context still has its type, so it
// holds the caller's own thing -- not a copy of it, and not a const picture of
// it either: a context is a place to keep things while a reading runs, and a
// scanner that is told one may write in it. It is made at the place the context
// is said, as a default argument, so it lives exactly as long as the call.
template <class FieldType, class ContextType, bool ToldApart>
struct reading_by final : reading_of<FieldType> {
  using held = std::remove_cv_t<FieldType>;
  using answer = std::expected<held, failure_for<held>>;

  ContextType* kept = nullptr;

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

  [[nodiscard]] constexpr fold_state_for<held> begin_fold() override {
    if constexpr (requires { scan::scanner<held>{}.begin_groups(*kept); }) {
      return scan::scanner<held>{}.begin_groups(*kept);
    } else if constexpr (requires { scan::scanner<held>{}.begin_groups(); }) {
      return scan::scanner<held>{}.begin_groups();
    } else {
      return scan::no_contexts{};
    }
  }

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
template <class FieldType>
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
      reading_by<held, std::remove_reference_t<It>, true>&& made = {})
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
  [[nodiscard]] constexpr fold_state_for<held> begin_fold() const {
    return how_->begin_fold();
  }
  [[nodiscard]] constexpr gather_state_for<held> begin_gather(
      std::string_view parameters) const {
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

template <class... Parts>
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

template <class FieldType, class It>
struct readings_for<context_leaf<FieldType>, It> {
  using type = reading_by<std::remove_cv_t<FieldType>,
                          std::remove_reference_t<It>, false>;
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
template <class FieldType>
[[nodiscard]] consteval std::size_t carrier_places() {
  if constexpr (scanned_as_variant<FieldType>) {
    return branch_count<FieldType>();
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
template <class FieldType, bool Whole = (carrier_places<FieldType>() == 0)>
struct carrier_of;

template <class FieldType>
struct carrier_of<FieldType, true> {
  using type = context_leaf<FieldType>;
};

template <class FieldType>
struct carrier_of<FieldType, false> {
  template <std::size_t... K>
  static auto made(std::index_sequence<K...>)
      -> context_shape<
          typename carrier_of<carrier_place_for<FieldType, K>>::type...>;
  using type =
      decltype(made(std::make_index_sequence<carrier_places<FieldType>()>{}));
};

template <class FieldType>
using carrier_for = typename carrier_of<std::remove_cv_t<FieldType>>::type;

template <class Type, std::size_t Place>
[[nodiscard]] consteval auto context_place_kind() {
  if constexpr (carrier_places<Type>() == 0) {
    if constexpr (Place == 0) {
      return std::type_identity<context_leaf<std::remove_cv_t<Type>>>{};
    } else {
      return std::type_identity<no_place>{};
    }
  } else if constexpr (Place < carrier_places<Type>()) {
    return std::type_identity<carrier_for<carrier_place_for<Type, Place>>>{};
  } else {
    return std::type_identity<no_place>{};
  }
}

template <class Type, std::size_t Place>
using context_place_of = typename decltype(context_place_kind<Type, Place>())::type;

// The memory resource a context keeps, where it keeps one.
//
// An allocator says it, a resource is one, a thing that hands either back says
// it too, and a place told in braces is asked through the interface that
// carries it -- a resource is already a thing answered at runtime, so nothing
// of the context's type has to cross the door for this.
template <class Told>
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
template <class Held, std::size_t Most = turns_unbounded, class Told>
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
template <class Held>
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
template <class Held, class CarrierType>
[[nodiscard]] constexpr auto scanner_begin_given(std::string_view parameters,
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
template <class Held, class Context>
[[nodiscard]] constexpr auto begun_groups(Context&& given) {
  // A place told in braces carries its context behind an interface that knows
  // the field. The type of the context does not cross the door, but the
  // beginning of a fold does: the carrier begins it where the type is still
  // known and hands back the state the scanner would have begun anyway.
  if constexpr (requires {
                  given.told();
                  given.begin_fold();
                }) {
    if (given.told()) return given.begin_fold();
    return scan::scanner<std::remove_cv_t<Held>>{}.begin_groups();
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
[[nodiscard]] constexpr std::expected<Held, failure_for<Held>> groups_value(
    std::span<const std::string_view> groups, ContextType&& given) {
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
    return scan::scanner<Held>{}.finish_groups(std::move(state));
  }
}

// The value itself, for a reading that cannot go wrong.
template <class Parameters, class Type, std::size_t Offset,
          bool AsOutput = false, class CarrierType = scan::no_contexts>
[[nodiscard]] constexpr Type built_value(
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
                            CarrierType::told_apart, scan::throws_a_failure>(
              groups[Offset], Parameters::at(Offset), given.leaf()));
    }
  } else if constexpr (scanned_from_values<Type>) {
    return [&]<std::size_t... index>(std::index_sequence<index...>) {
      return scan::scanner<std::remove_cv_t<Type>>{}.parse(
          built_value<Parameters, typename parts_of<Type>::template at<index>,
                      Offset + groups_before_field<Type, index>()>(
              groups, given.template for_part<index>())...);
    }(std::make_index_sequence<parts_of<Type>::count>{});
  } else {
    return [&]<std::size_t... index>(std::index_sequence<index...>) {
      return Type{
          built_value<Parameters, typename parts_of<Type>::template at<index>,
                      Offset + groups_before_field<Type, index>()>(
              groups, given.template for_part<index>())...};
    }(std::make_index_sequence<parts_of<Type>::count>{});
  }
}

template <class FailureType, class Parameters, class Type,
          std::size_t Offset, bool AsOutput = false,
          class Ending = hands_a_failure_back,
          class CarrierType = scan::no_contexts>
[[nodiscard]] constexpr typename Ending::template result<Type, FailureType>
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
                    given.leaf().told();
                    given.leaf().read_groups(
                        std::span<const std::string_view>{});
                  }) {
      const auto told_here = given.leaf();
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
        auto got = [&] {
          if constexpr (requires {
                          scan::scanner_told_from_groups<held, Ending>(pieces,
                                                                       given);
                        }) {
            return scan::scanner_told_from_groups<held, Ending>(pieces, given);
          } else {
            return scan::scanner_told_from_groups<held, Ending>(pieces);
          }
        }();
        if (got) return std::move(*got);
        return Ending::template went_wrong<Type, FailureType>(
            std::move(got).error());
      } else if constexpr (requires {
                             scan::scanner<held>{}.from_groups(pieces, given);
                           }) {
        return scan::scanner<held>{}.from_groups(pieces, given);
      } else {
        return scan::scanner<held>{}.from_groups(pieces);
      }
    } else {
      auto state = begun_groups<held>(given.leaf());
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
        return scan::scanner<held>{}.finish_groups(std::move(state));
      }
    }
  } else if constexpr (a_value) {
    auto got = [&] {
      if constexpr (std::same_as<CarrierType, scan::no_contexts>) {
        return parse_value<std::remove_cv_t<Type>, FailureType>(
            groups[Offset], Parameters::at(Offset));
      } else {
        return parse_value_given<std::remove_cv_t<Type>, FailureType,
                                 CarrierType::told_apart>(
            groups[Offset], Parameters::at(Offset), given.leaf());
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
                        Ending>(groups, given.template for_part<index>())...);
      } else {
        auto parts = std::tuple{build_value<
            FailureType, Parameters, typename parts_of<Type>::template at<index>,
            Offset + groups_before_field<Type, index>(), false, Ending>(
            groups, given.template for_part<index>())...};
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
            groups, given.template for_part<index>())...};
      } else {
        auto parts = std::tuple{build_value<
            FailureType, Parameters, typename parts_of<Type>::template at<index>,
            Offset + groups_before_field<Type, index>(), false, Ending>(
            groups, given.template for_part<index>())...};
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
