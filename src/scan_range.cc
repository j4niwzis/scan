export module scan.range;

import std;
export import scan.shape;

export namespace scan::detail {

template <class Type, fixed_string Format, std::size_t Extent, std::size_t... Index>
[[nodiscard]] constexpr Type convert(
    const std::array<std::string_view, Extent>& fields,
    std::index_sequence<Index...>) {
  static_assert(groups_of_output<Type>() == Extent,
                "placeholder count must equal the number of values the output "
                "type reads");
  // Built, not built empty and then written over. The aggregate used to be
  // default-constructed and each field assigned a temporary afterwards, which
  // for a field that owns storage is a construction, a move-assignment that
  // must first ask whether the destination is holding any, and a destruction --
  // three times what initialising it once costs. It also demanded that every
  // field be default-constructible and assignable, which is more than an
  // aggregate has to be.
  return or_thrown(build_value<failure_for<Type>,
                               format_parameters<Type, Format>, Type, 0>(
      fields));
}

// A reading that has been told its contexts and not yet its output type.
//
// The contexts cannot be told to the conversion itself -- a conversion takes no
// arguments -- so they are told to the reading, and the conversion is the one
// it always was. Everything here is the same reading under another spelling.
template <class Reading, class... Contexts>
class reading_with {
 public:
  constexpr reading_with(Reading what, Contexts&... given)
      : what_(std::move(what)), given_(given...) {}

  template <class Type>
    requires(std::is_aggregate_v<Type> || scanned_as_variant<Type>)
  constexpr operator Type() const {
    return what_.template read_or_throw<Type>(given_);
  }

  template <class Type>
  [[nodiscard]] constexpr Type of() const {
    return what_.template read_or_throw<Type>(given_);
  }

  template <class Type>
  [[nodiscard]] constexpr std::expected<Type, detail::failure_for<Type>> try_of()
      const {
    return what_.template read<Type>(given_);
  }

 private:
  Reading what_;
  scan::contexts_at_places<std::remove_reference_t<Contexts>...> given_;
};

// Naming what a reading is for, written once for every kind of subject.
//
// A reading is a description of one until its output type is named, and naming
// it is what runs it. What differs between a subject in a row, one in pieces and
// one read once is the call below this -- how that subject is walked. What does
// not differ is this: the type said after the subject, the contexts said as they
// stand, and the contexts said in braces. Written out three times, as it was,
// the three roads drifted apart from each other; written here, a place that can
// be told a context can be told one off any subject.
//
// A reading that can hand the value over without building an expected first is
// asked that way, and one that cannot is asked for what it has and told to
// throw at the asking.
struct names_its_output {
  template <class Type, class Self, class... Contexts>
  [[nodiscard]] constexpr Type of(this Self&& self, Contexts&&... given) {
    if constexpr (sizeof...(Contexts) == 0) {
      if constexpr (requires { self.template read_or_throw<Type>(); }) {
        return std::forward<Self>(self).template read_or_throw<Type>();
      } else {
        return or_thrown(std::forward<Self>(self).template read<Type>());
      }
    } else {
      return std::forward<Self>(self).template asked_for<Type>(
          scan::contexts_at_places<std::remove_reference_t<Contexts>...>(given...));
    }
  }

  // The same, where a place is a shape and its parts want their own contexts.
  // A braced list deduces nothing, so this is the whole list at once.
  template <class Type, class Self>
  [[nodiscard]] constexpr Type of(this Self&& self, carrier_for<Type> given) {
    return std::forward<Self>(self).template asked_for<Type>(given);
  }

  template <class Type, class Self, class... Contexts>
  [[nodiscard]] constexpr std::expected<Type, detail::failure_for<Type>> try_of(
      this Self&& self, Contexts&&... given) {
    if constexpr (sizeof...(Contexts) == 0) {
      return std::forward<Self>(self).template read<Type>();
    } else {
      return std::forward<Self>(self).template read<Type>(
          scan::contexts_at_places<std::remove_reference_t<Contexts>...>(given...));
    }
  }

  template <class Type, class Self>
  [[nodiscard]] constexpr std::expected<Type, detail::failure_for<Type>> try_of(
      this Self&& self, carrier_for<Type> given) {
    return std::forward<Self>(self).template read<Type>(given);
  }

  // Asked for the value rather than for what went wrong, which is the one place
  // a failure becomes a throw.
  template <class Type, class Self, class Carrier>
  [[nodiscard]] constexpr Type asked_for(this Self&& self,
                                         const Carrier& carrier) {
    if constexpr (requires { self.template read_or_throw<Type>(carrier); }) {
      return std::forward<Self>(self).template read_or_throw<Type>(carrier);
    } else {
      return or_thrown(std::forward<Self>(self).template read<Type>(carrier));
    }
  }
};

template <fixed_string Format, int Terminator = -1, bool Terminated = false,
          how_to_walk Walk = how_to_walk::by_length>
class borrowed_result : public names_its_output {
 public:
  constexpr explicit borrowed_result(std::string_view input) : input_(input) {}

  // The reading itself, which hands back what it read or what went wrong.
  // Everything below is this, asked for in one of the two ways.
  template <class Type, class CarrierType = scan::no_contexts>
  [[nodiscard]] constexpr std::expected<Type, failure_for<Type>> read(
      const CarrierType& given = CarrierType{}) const {
    // A list or a fold is read by the machine that gathers as it goes, even
    // where the subject lies in a row and could be pointed at: what either of
    // them is made of are the turns, and the positions left behind hold the
    // last turn and nothing before it.
    if constexpr (holds_a_range<Type>() || holds_a_fold<Type>()) {
      // A reading that gathers as it goes keeps its fields' states for the
      // whole walk, so the type of a state has to be known -- and it is,
      // wherever the contexts were written as they stand. In braces the type of
      // a context is forgotten at the door, and a state whose type is forgotten
      // has nowhere to live.
      static_assert(!requires { given.leaf().told(); },
                    "a fold or a list is told its context without braces: "
                    "scan<f>(text).of<T>(context), not .of<T>({context})");
      return detail::scan_stream<Type, Format, Walk, CarrierType>(input_, given);
    } else {
      // A group that took no part is an error, unless somewhere in this output
      // there is a variant, where exactly one branch takes part and the rest do
      // not. Which it is, is known while the pattern is compiled.
      auto fields = [&] {
        if constexpr (holds_a_variant<Type>() || scanned_as_variant<Type>) {
          return scan_branch_fields<Type, Format, Terminator, Terminated, Walk>(
              input_);
        } else {
          return scan_fields<Type, Format, Terminator, Terminated, Walk>(
              input_);
        }
      }();
      if (!fields) {
        return std::unexpected(
            scan::as_a_failure<failure_for<Type>>(std::move(fields).error()));
      }
      // The whole of what is read, and not a value standing in a place.
      //
      // A type that declares a format is a field wherever one is wanted -- its
      // own format is spread where it stands and its own reader is handed what
      // that matched. Asked for as the whole output it is the other thing: the
      // format the caller wrote is what made these groups, and the type is put
      // together from them. Read as a value here, such a type was handed the
      // whole match and read it by its own format a second time, so a shape of
      // two numbers was given "(3,-4)" where it expected "3".
      // Built by the helper that knows what a shape is made of, and not
      // here. What this layer has is groups; what a type is made of is a
      // question it does not ask.
      return scan::aggregate_scanner<Format>::template read<Type>(*fields,
                                                                  given);
    }
  }

  // The same reading, asked for rather than tried for: nothing along the way
  // holds a failure, because there is nowhere to put one but a throw and the
  // throw happens where the failure is.
  template <class Type, class CarrierType = scan::no_contexts>
  [[nodiscard]] constexpr Type read_or_throw(
      const CarrierType& given = CarrierType{}) const {
    if constexpr (holds_a_range<Type>() || holds_a_fold<Type>()) {
      static_assert(!requires { given.leaf().told(); },
                    "a fold or a list is told its context without braces: "
                    "scan<f>(text).of<T>(context), not .of<T>({context})");
      return or_thrown(
          detail::scan_stream<Type, Format, Walk, CarrierType>(input_, given));
    } else {
      const auto fields = [&] {
        if constexpr (holds_a_variant<Type>() || scanned_as_variant<Type>) {
          return scan_branch_fields<Type, Format, Terminator, Terminated, Walk,
                                    throws_a_failure>(input_);
        } else {
          return scan_fields<Type, Format, Terminator, Terminated, Walk,
                             throws_a_failure>(input_);
        }
      }();
      return scan::aggregate_scanner<Format>::template read_or_throw<Type>(
          fields, given);
    }
  }

  template <class Type>
    requires std::is_aggregate_v<Type>
  constexpr operator Type() const {
    return read_or_throw<Type>();
  }

  // The same scan, for a format that says the input may be one of several
  // shapes. Which branch ran is read from the mark each branch was given.
  template <class Type>
    requires scanned_as_variant<Type>
  constexpr operator Type() const {
    return read_or_throw<Type>();
  }

  // What a reading is for is named by `names_its_output` above: `of`, `try_of`,
  // and the contexts said either way.

  // The same contexts, told before the output type is named -- which is what a
  // reading assigned to a variable needs, because the conversion that names the
  // type has nowhere to put them.
  template <class... Contexts>
  [[nodiscard]] constexpr auto with(Contexts&&... given) const {
    return reading_with<borrowed_result, std::remove_reference_t<Contexts>...>(
        *this, given...);
  }

  // The same things the reading could be told before it was handed a subject,
  // told after.
  //
  // Nothing has happened yet: a scan runs when the output type is named, so
  // between `scan<f>(text)` and `of<T>()` the reading is still only a
  // description of one, and saying more about it is free. So both of these are
  // the same reading, and neither is the one that is written first:
  //
  //   scan::scan<f>.sentinel().vec()(text).of<T>()
  //   scan::scan<f>(text).sentinel().vec().of<T>()
  //
  // The pattern layer's `match` has no such thing, and cannot: it walks where
  // it is called and hands back the answer, so by the time there is something
  // to say a method on, the walk it would have changed is over.
  [[nodiscard]] constexpr borrowed_result<Format, -1, Terminated, Walk> sized()
      const {
    return borrowed_result<Format, -1, Terminated, Walk>(input_);
  }

  template <unsigned char Byte = 0>
  [[nodiscard]] constexpr borrowed_result<Format, Byte, false, Walk> sentinel()
      const {
    return borrowed_result<Format, Byte, false, Walk>(input_);
  }

  [[nodiscard]] constexpr borrowed_result<Format, Terminator, Terminated,
                                          how_to_walk::by_length>
  by_length() const {
    return borrowed_result<Format, Terminator, Terminated,
                           how_to_walk::by_length>(input_);
  }

  [[nodiscard]] constexpr borrowed_result<Format, Terminator, Terminated,
                                          how_to_walk::one_at_a_time>
  scalar() const {
    return borrowed_result<Format, Terminator, Terminated,
                           how_to_walk::one_at_a_time>(input_);
  }

  [[nodiscard]] constexpr borrowed_result<Format, Terminator, Terminated,
                                          how_to_walk::in_words>
  vec() const {
    return borrowed_result<Format, Terminator, Terminated,
                           how_to_walk::in_words>(input_);
  }

 private:
  std::string_view input_;
};

// What a scan over pieces hands back until somebody says what it is scanning
// into.
template <fixed_string Format, class PiecesType>
class pieces_result : public names_its_output {
 public:
  constexpr explicit pieces_result(PiecesType input)
      : input_(std::move(input)) {}

  pieces_result(pieces_result&&) = default;
  pieces_result& operator=(pieces_result&&) = default;
  pieces_result(const pieces_result&) = delete;
  pieces_result& operator=(const pieces_result&) = delete;

  template <class Type>
  [[nodiscard]] constexpr std::expected<Type, detail::failure_for<Type>>
  read() {
    return scan_pieces<Type, Format>(std::move(input_));
  }

  template <class Type>
    requires std::is_aggregate_v<Type>
  constexpr operator Type() {
    return or_thrown(read<Type>());
  }

  // The same contexts a subject in a row may be told. What is below took them
  // all along -- the gatherer is told at the door and each place asks it -- so
  // saying them here is all that was missing.
  template <class Type, class CarrierType>
  [[nodiscard]] constexpr std::expected<Type, detail::failure_for<Type>> read(
      const CarrierType& told) {
    return scan_pieces<Type, Format, CarrierType>(std::move(input_), told);
  }

 private:
  PiecesType input_;
};

template <fixed_string Format, std::ranges::input_range RangeType>
class streaming_result : public names_its_output {
 public:
  constexpr explicit streaming_result(RangeType input)
      : input_(std::move(input)) {}

  streaming_result(streaming_result&&) = default;
  streaming_result& operator=(streaming_result&&) = default;
  streaming_result(const streaming_result&) = delete;
  streaming_result& operator=(const streaming_result&) = delete;

  template <class Type>
  [[nodiscard]] constexpr std::expected<Type, detail::failure_for<Type>>
  read() {
    return scan_stream<Type, Format>(input_);
  }

  template <class Type>
    requires std::is_aggregate_v<Type>
  constexpr operator Type() {
    return or_thrown(read<Type>());
  }

  // The same contexts a subject in a row may be told. What is below took them
  // all along -- the gatherer is told at the door and each place asks it -- so
  // saying them here is all that was missing.
  template <class Type, class CarrierType>
  [[nodiscard]] constexpr std::expected<Type, detail::failure_for<Type>> read(
      const CarrierType& told) {
    return scan_stream<Type, Format, how_to_walk::by_length, CarrierType>(
        input_, told);
  }

 private:
  RangeType input_;
};


}  // namespace scan::detail

export namespace scan {

// Whether the type of the input promises a terminator past its last character.
//
// A `std::string` does, and has since the language said `data()` and `c_str()`
// are the same thing: there is a null character at `data() + size()`. A string
// literal does. A `string_view` into the middle of something does not, and
// neither does a vector of characters.
template <class RangeType>
concept terminated_char_range =
    detail::contiguous_char_range<RangeType> &&
    (std::same_as<std::remove_cvref_t<RangeType>, std::string> ||
     std::same_as<std::remove_cvref_t<RangeType>,
                  std::basic_string<char, std::char_traits<char>,
                                    std::allocator<char>>> ||
     // A string literal is read to the nul the compiler put there rather than
     // by counting, which is the faster of the two walks and the one nobody
     // has to ask for.
     detail::literal_char_range<RangeType>);

// What a scan of the head of an input hands back: the values, and what is left.
template <class Type>
struct taken {
  Type value;
  std::string_view rest;
};

// The head of an input, and what follows it.
//
// The output type has to be named, and named here rather than deduced from
// somewhere: how much of the input the pattern takes depends on the automaton,
// the automaton depends on the format, and a place written `{}` takes its
// pattern from the type of the value it stands for. There is nothing to work
// out until the type is said.
//
// Assigning it to something is the ordinary scan of the head, with the rest
// thrown away; `take` hands back both.
template <fixed_string Format>
class prefix_scan {
 public:
  constexpr explicit prefix_scan(std::string_view input) : input_(input) {}

  // The head and what follows it, or what went wrong instead. The reading
  // itself; `take` is this, asked for rather than tried for.
  template <class Type>
  [[nodiscard]] constexpr std::expected<taken<Type>, detail::failure_for<Type>>
  try_take() const {
    // One walk: where the head ends and what is in it come back together, out
    // of the registers the walk was carrying anyway.
    const auto found =
        detail::taken_prefix_fields<Type, Format,
                                    detail::holds_a_variant<Type>()>(input_);
    if (!found.matched) {
      return std::unexpected(
          scan::as_a_failure<detail::failure_for<Type>>(
              no_match<>("input does not begin with the pattern")));
    }
    auto made = detail::build_value<detail::failure_for<Type>,
                                    detail::format_parameters<Type, Format>,
                                    Type, 0>(found.groups);
    if (!made) return std::unexpected(std::move(made).error());
    return taken<Type>{std::move(*made), input_.substr(found.head.size())};
  }

  template <class Type>
  [[nodiscard]] constexpr taken<Type> take() const {
    // One walk, and nothing along it holds a failure: asked for a value, a
    // failure is a throw where it happens.
    const auto found =
        detail::taken_prefix_fields<Type, Format,
                                    detail::holds_a_variant<Type>()>(input_);
    if (!found.matched) {
      throw no_match<std::exception>(
          "input does not begin with the pattern");
    }
    return taken<Type>{
        detail::build_value<detail::failure_for<Type>,
                            detail::format_parameters<Type, Format>, Type, 0,
                            false, scan::throws_a_failure>(found.groups),
        input_.substr(found.head.size())};
  }

  // The same head, told what the places were told.
  //
  // A head is where the fallback lives: the walk goes past a match on the
  // chance of a longer one the order prefers, and where it dies the answer is
  // the place it passed. What a scanner wrote into a context along the way it
  // wrote for a reading that did not happen -- the state is copied where a
  // reading divides, a context is not -- so a context is the one place where
  // the walk can be watched, and the one place where writing is the caller's
  // own business.
  template <class Type, class... Contexts>
    requires(sizeof...(Contexts) > 0)
  [[nodiscard]] constexpr std::expected<taken<Type>, detail::failure_for<Type>>
  try_take(Contexts&&... given) const {
    return taken_with<Type>(scan::contexts_at_places<std::remove_reference_t<Contexts>...>(given...));
  }

  template <class Type>
  [[nodiscard]] constexpr std::expected<taken<Type>, detail::failure_for<Type>>
  try_take(detail::carrier_for<Type> given) const {
    return taken_with<Type>(given);
  }

  template <class Type, class... Contexts>
    requires(sizeof...(Contexts) > 0)
  [[nodiscard]] constexpr taken<Type> take(Contexts&&... given) const {
    return or_thrown(
        taken_with<Type>(scan::contexts_at_places<std::remove_reference_t<Contexts>...>(given...)));
  }

  template <class Type>
  [[nodiscard]] constexpr taken<Type> take(detail::carrier_for<Type> given) const {
    return or_thrown(taken_with<Type>(given));
  }

  template <class Type>
    requires std::is_aggregate_v<Type> || detail::scanned_as_variant<Type>
  constexpr operator Type() const {
    return take<Type>().value;
  }

  // The head and what follows it, told a carrier: one walk, the same as above,
  // and the value built with what each place was given.
  template <class Type, class CarrierType>
  [[nodiscard]] constexpr std::expected<taken<Type>, detail::failure_for<Type>>
  taken_with(const CarrierType& given) const {
    const auto found =
        detail::taken_prefix_fields<Type, Format,
                                    detail::holds_a_variant<Type>()>(input_);
    if (!found.matched) {
      return std::unexpected(scan::as_a_failure<detail::failure_for<Type>>(
          no_match<>("input does not begin with the pattern")));
    }
    auto made =
        detail::build_value<detail::failure_for<Type>,
                            detail::format_parameters<Type, Format>, Type, 0,
                            false, scan::hands_a_failure_back, CarrierType>(
            found.groups, given);
    if (!made) return std::unexpected(std::move(made).error());
    return taken<Type>{std::move(*made), input_.substr(found.head.size())};
  }

  template <class Type>
  [[nodiscard]] constexpr Type of() const {
    return take<Type>().value;
  }

 private:

  template <class Type>
  [[nodiscard]] constexpr std::expected<Type, detail::failure_for<Type>>
  try_of() const {
    auto got = try_take<Type>();
    if (!got) return std::unexpected(got.error());
    return std::move(got->value);
  }

 private:
  std::string_view input_;
};

// One match after another, off the front of what is left.
//
// The reading stops where the pattern stops taking, and what it did not take is
// still there to be looked at -- so a loop that ends early can say why. The
// values are read as the loop asks for them and never all at once.
template <class Type, fixed_string Format>
class each_view {
 public:
  constexpr explicit each_view(std::string_view input) : rest_(input) {}

  class iterator {
   public:
    using value_type = Type;
    using difference_type = std::ptrdiff_t;

    constexpr iterator() = default;
    constexpr explicit iterator(each_view& owner) : owner_(&owner) {}

    [[nodiscard]] constexpr const Type& operator*() const {
      return *owner_->value_;
    }
    constexpr iterator& operator++() {
      owner_->advance();
      return *this;
    }
    constexpr void operator++(int) { ++*this; }
    [[nodiscard]] constexpr bool operator==(std::default_sentinel_t) const {
      return owner_ == nullptr || !owner_->value_.has_value();
    }

   private:
    each_view* owner_ = nullptr;
  };

  [[nodiscard]] constexpr iterator begin() {
    advance();
    return iterator(*this);
  }
  [[nodiscard]] constexpr std::default_sentinel_t end() const { return {}; }

  // What the pattern did not take. Empty when everything was read.
  [[nodiscard]] constexpr std::string_view rest() const { return rest_; }

 private:
  constexpr void advance() {
    value_.reset();
    if (rest_.empty()) return;
    // One walk a match, not two: the walk that finds where this one ends is
    // carrying what is in it.
    const auto found =
        detail::taken_prefix_fields<Type, Format,
                                    detail::holds_a_variant<Type>()>(rest_);
    if (!found.matched) return;
    auto made = detail::build_value<detail::failure_for<Type>,
                                    detail::format_parameters<Type, Format>,
                                    Type, 0>(found.groups);
    // A match whose values did not read ends the reading, the same way a
    // subject that stopped matching does: the loop asked for values and there
    // are none, and there is nowhere in a loop to hand a failure to.
    if (!made) return;
    value_ = std::move(*made);
    rest_ = rest_.substr(found.head.size());
  }

  std::string_view rest_;
  std::optional<Type> value_;
};

// The same off a range that is read as it comes.
template <class Type, fixed_string Format, class RangeType,
          class CarrierType = scan::default_context_t>
class each_stream_view {
 public:
  constexpr explicit each_stream_view(RangeType input,
                                      CarrierType told = CarrierType{})
      : input_(std::move(input)),
        first_(std::ranges::begin(input_)),
        told_(std::move(told)) {}

  each_stream_view(each_stream_view&&) = default;
  each_stream_view& operator=(each_stream_view&&) = default;
  each_stream_view(const each_stream_view&) = delete;
  each_stream_view& operator=(const each_stream_view&) = delete;

  class iterator {
   public:
    using value_type = Type;
    using difference_type = std::ptrdiff_t;

    constexpr iterator() = default;
    constexpr explicit iterator(each_stream_view& owner) : owner_(&owner) {}

    [[nodiscard]] constexpr const Type& operator*() const {
      return *owner_->value_;
    }
    constexpr iterator& operator++() {
      owner_->advance();
      return *this;
    }
    constexpr void operator++(int) { ++*this; }
    [[nodiscard]] constexpr bool operator==(std::default_sentinel_t) const {
      return owner_ == nullptr || !owner_->value_.has_value();
    }

   private:
    each_stream_view* owner_ = nullptr;
  };

  [[nodiscard]] constexpr iterator begin() {
    advance();
    return iterator(*this);
  }
  [[nodiscard]] constexpr std::default_sentinel_t end() const { return {}; }

  // The character that ended the last match, where one was read and could not
  // be put back. Empty where the pattern ended by itself.
  [[nodiscard]] constexpr std::optional<char> stopped() const {
    return stopped_;
  }

 private:
  constexpr void advance() {
    value_.reset();
    if (carry_.empty() && first_ == std::ranges::end(input_)) return;
    auto got = detail::scan_stream_prefix<Type, Format, CarrierType>(
        first_, std::ranges::end(input_), carry_, true, told_);
    if (!got) {
      value_.reset();
      return;
    }
    value_ = std::move(got->value);
    stopped_ = got->stopped;
  }

  RangeType input_;
  std::ranges::iterator_t<RangeType> first_;
  // What the last match read and did not keep. A reading that can be gone back
  // over holds nothing here and the iterator goes back instead.
  detail::stream_carry_for<Type, Format,
                           std::ranges::iterator_t<RangeType>> carry_;
  std::optional<Type> value_;
  std::optional<char> stopped_;
  // Told once and told to every record: what a reading is told is a fact about
  // the reading and not about the record it is on.
  CarrierType told_;
};

// A machine fed one character at a time, for input that arrives rather than
// waiting to be read.
//
// Nothing pulls: whoever has the characters offers them, one at a time, and is
// told whether each was taken. Nothing is buffered and nothing is allocated
// beyond what the fields themselves ask for, so a field of fixed room --
// `scan::held<n>` -- makes the whole of it fit in a place with no allocator at
// all. And because nothing waits for the end of the input, whatever else the
// arriving characters are supposed to cause can happen as they arrive.
//
//     scan::experimental::reader<command, "set {[a-z]+} {[0-9]+}"> reading;
//     for (;;) {
//       const char symbol = next();
//       if (reading.offer(symbol)) continue;
//       if (reading.accepting()) act(reading.take());
//       reading.restart();
//       if (!reading.offer(symbol)) report(symbol);
//     }
//
// Under `experimental` because it is the one reading here whose shape is not
// settled: everything else in this library is a call that reads a subject, and
// this is a machine the caller drives. It is not in the README for the same
// reason.
namespace experimental {

template <class Type, fixed_string Format>
class reader {
 public:
  // False means the character was not taken and the machine has not moved:
  // either what came before is a whole command and this character belongs to
  // what comes next, or nothing here matches at all, which `accepting` tells
  // apart.
  [[nodiscard]] constexpr bool offer(char symbol) {
    return state_.offer(symbol);
  }

  // Would what has been read so far be a whole match?
  [[nodiscard]] constexpr bool accepting() const { return state_.accepting(); }

  // A whole match that nothing can extend. Where a pattern ends in the thing
  // that ends it, this is true the moment the last character goes in, and the
  // next character need never be offered to find out.
  [[nodiscard]] constexpr bool settled() const { return state_.settled(); }

  // The values, or what went wrong instead. Reading further after this is
  // reading further into the same match, so whoever wants the next one says so.
  [[nodiscard]] constexpr std::expected<Type, detail::failure_for<Type>>
  try_take() const {
    return state_.finish();
  }

  [[nodiscard]] constexpr Type take() const {
    return or_thrown(try_take());
  }

  // What has been gathered for a field so far, while the match is still going
  // on -- for showing a command back as it is typed, for finishing it for
  // whoever is typing, for refusing it before they are done.
  template <std::size_t Field>
  [[nodiscard]] constexpr const auto& gathering() const {
    return state_.template gathering<Field>();
  }

  // Whether that field is being read right now.
  template <std::size_t Field>
  [[nodiscard]] constexpr bool reading() const {
    return state_.template reading<Field>();
  }

  // Whether nothing can follow what has been offered: the machine is not in a
  // match and cannot get into one from here.
  [[nodiscard]] constexpr bool rejected() const { return state_.rejected(); }

  constexpr void restart() { state_.restart(); }

 private:
  detail::stream_state<Type, Format, false> state_;
};

}  // namespace experimental

// The head of a range that is read once. The range is left standing after the
// character that ended the match, which is handed back with the values because
// it has been read and there is nowhere to put it back.
template <fixed_string Format, std::ranges::input_range RangeType>
class prefix_stream_scan : public detail::names_its_output {
 public:
  constexpr explicit prefix_stream_scan(RangeType input)
      : input_(std::move(input)) {}

  prefix_stream_scan(prefix_stream_scan&&) = default;
  prefix_stream_scan& operator=(prefix_stream_scan&&) = default;
  prefix_stream_scan(const prefix_stream_scan&) = delete;
  prefix_stream_scan& operator=(const prefix_stream_scan&) = delete;

  // The answer says how much room it needs for what it hands back, which is a
  // question about the pattern and the reading both, so it is deduced rather
  // than named here.
  template <class Type, class CarrierType = scan::default_context_t>
  [[nodiscard]] constexpr auto try_take(const CarrierType& told = CarrierType{}) {
    return detail::scan_stream_prefix<Type, Format, CarrierType>(input_, told);
  }

  template <class Type, class CarrierType = scan::default_context_t>
  [[nodiscard]] constexpr auto take(const CarrierType& told = CarrierType{}) {
    return or_thrown(try_take<Type, CarrierType>(told));
  }

  template <class Type>
    requires std::is_aggregate_v<Type>
  constexpr operator Type() {
    return take<Type>().value;
  }

  // What a head hands back is the value and what stopped it; what
  // `names_its_output` asks for is the value alone, so `of` and `try_of` are
  // the ones written there -- told as the contexts stand, or in braces, the
  // same as off any other subject.
  template <class Type, class CarrierType = scan::default_context_t>
  [[nodiscard]] constexpr std::expected<Type, detail::failure_for<Type>> read(
      const CarrierType& told = CarrierType{}) {
    auto got = try_take<Type, CarrierType>(told);
    if (!got) return std::unexpected(got.error());
    return std::move(got->value);
  }

 private:
  RangeType input_;
};

// One match after another. The output type is named where the reading starts,
// for the same reason the head of an input names it: until it is said there is
// nothing to work out.
template <fixed_string Format>
class each_scan {
 public:
  constexpr explicit each_scan(std::string_view input) : input_(input) {}

  template <class Type>
  [[nodiscard]] constexpr each_view<Type, Format> of() const {
    // Asked of the compiled automaton, which is not built at all where they
    // are built while the program runs. There the same pattern is refused by
    // the reading itself, which cannot move and says so.
    if constexpr (!detail::automata_at_runtime) {
      static_assert(!detail::matches_nothing<
                        detail::packed_automaton<Type, Format>>(),
                    "this pattern is happy with nothing at all, so reading one "
                    "match after another would never move");
    }
    return each_view<Type, Format>(input_);
  }

 private:
  std::string_view input_;
};

template <fixed_string Format, class RangeType>
class each_stream_scan {
 public:
  constexpr explicit each_stream_scan(RangeType input)
      : input_(std::move(input)) {}

  each_stream_scan(each_stream_scan&&) = default;
  each_stream_scan& operator=(each_stream_scan&&) = default;
  each_stream_scan(const each_stream_scan&) = delete;
  each_stream_scan& operator=(const each_stream_scan&) = delete;

  // Told as the contexts stand, or told nothing.
  //
  // Said here rather than taken from `names_its_output`, because what naming
  // the output of this reading makes is not a value but a view: one record
  // after another, each of them told the same thing.
  template <class Type, class... Contexts>
  [[nodiscard]] constexpr auto of(this each_stream_scan&& self,
                                  Contexts&&... given) {
    static_assert(!detail::matches_nothing<
                      detail::streaming_automaton<Type, Format>>(),
                  "this pattern is happy with nothing at all, so reading one "
                  "match after another would never move");
    if constexpr (sizeof...(Contexts) == 0) {
      return each_stream_view<Type, Format, RangeType>(std::move(self.input_));
    } else {
      using carrier =
          scan::contexts_at_places<std::remove_reference_t<Contexts>...>;
      return each_stream_view<Type, Format, RangeType, carrier>(
          std::move(self.input_), carrier(given...));
    }
  }

  // And no braced list here, where every other reading takes one.
  //
  // What a reading is told is held as the address of it: a context is a handle
  // -- an allocator, a pool, a pointer to the caller's world -- and the copy
  // that costs a word is the copy of that address. Everywhere else the reading
  // is run by the call that named its output, so a context written in braces
  // at that call lives until the full expression ends, which is after the
  // reading is over.
  //
  // This one is a view. It reads a record when it is asked for one, which is
  // after the call that made it has ended -- and a braced list makes its
  // contexts at that call and nothing else holds them. So the form that can
  // only be given temporaries is the form this reading cannot take, and what
  // is left takes lvalues: `contexts_at_places` binds `Contexts&`, so handing
  // it a temporary is a thing the compiler refuses rather than a thing that
  // reads freed memory on the second record.

 private:
  RangeType input_;
};

// One match after another, off a contiguous input or off one that is read as
// it comes.
//
// Said as a name rather than called, so that it can be either: `each<f>(text)`
// is the call, `text | each<f>` is the same thing said the other way round,
// and the type each of them hands back is asked for the same way --
// `.of<type>()`.
template <class Type, fixed_string Format, class PiecesType>
class each_pieces_view;

template <fixed_string Format, class PiecesType>
class each_pieces_scan;

template <fixed_string Format>
struct each_closure : std::ranges::range_adaptor_closure<each_closure<Format>> {
  // Off input that arrives in pieces: each piece read in words and vectors,
  // and the reading held between matches.
  template <detail::piecewise_char_range PiecesType>
    requires(!detail::contiguous_char_range<PiecesType> &&
             !std::same_as<std::ranges::range_value_t<PiecesType>, char>)
  [[nodiscard]] constexpr auto operator()(PiecesType&& input) const {
    auto view = std::views::all(std::forward<PiecesType>(input));
    return each_pieces_scan<Format, decltype(view)>(std::move(view));
  }

  template <detail::contiguous_char_range RangeType>
    requires(std::is_lvalue_reference_v<RangeType&&> ||
             std::ranges::borrowed_range<RangeType>)
  [[nodiscard]] constexpr auto operator()(RangeType&& input) const {
    return each_scan<Format>(detail::characters_of(input));
  }

  template <std::ranges::input_range RangeType>
    requires std::same_as<std::ranges::range_value_t<RangeType>, char> &&
             (!detail::contiguous_char_range<RangeType> ||
              (!std::is_lvalue_reference_v<RangeType&&> &&
               !std::ranges::borrowed_range<RangeType>))
  [[nodiscard]] constexpr auto operator()(RangeType&& input) const {
    auto view = std::views::all(std::forward<RangeType>(input));
    return each_stream_scan<Format, decltype(view)>(std::move(view));
  }
};

template <fixed_string Format>
inline constexpr each_closure<Format> each{};

// What `each` over pieces hands back until somebody says what it reads into.
template <fixed_string Format, class PiecesType>
class each_pieces_scan {
 public:
  constexpr explicit each_pieces_scan(PiecesType input)
      : input_(std::move(input)) {}

  each_pieces_scan(each_pieces_scan&&) = default;
  each_pieces_scan& operator=(each_pieces_scan&&) = default;
  each_pieces_scan(const each_pieces_scan&) = delete;
  each_pieces_scan& operator=(const each_pieces_scan&) = delete;

  template <class Type>
  [[nodiscard]] constexpr each_pieces_view<Type, Format, PiecesType> of() && {
    return each_pieces_view<Type, Format, PiecesType>(std::move(input_));
  }

 private:
  PiecesType input_;
};

// One match after another off input that arrives in pieces.
//
// The reading is held between matches: where one stopped is where the next
// begins, in the piece the walk is holding, so nothing is put back and nothing
// is read twice. The gathering starts again for each match; the pieces do not.
template <class Type, fixed_string Format, class PiecesType>
class each_pieces_view {
 public:
  using automaton_type =
      std::remove_cvref_t<decltype(detail::streaming_automaton<Type, Format>)>;
  using gatherer_type =
      detail::field_gatherer<Type, Format,
                             detail::streaming_automaton<Type, Format>>;
  using source_type =
      detail::gathers_from_pieces<gatherer_type, PiecesType,
                                  detail::pieces_hold<Type, Format>>;

  constexpr explicit each_pieces_view(PiecesType input)
      : source_(gatherer_type{collected_}, std::move(input)) {}

  each_pieces_view(each_pieces_view&&) = default;
  each_pieces_view& operator=(each_pieces_view&&) = default;
  each_pieces_view(const each_pieces_view&) = delete;
  each_pieces_view& operator=(const each_pieces_view&) = delete;

  class iterator {
   public:
    using value_type = Type;
    using difference_type = std::ptrdiff_t;

    constexpr iterator() = default;
    constexpr explicit iterator(each_pieces_view& owner) : owner_(&owner) {
      owner_->advance();
    }

    [[nodiscard]] constexpr const Type& operator*() const {
      return *owner_->value_;
    }
    constexpr iterator& operator++() {
      owner_->advance();
      return *this;
    }
    constexpr void operator++(int) { ++*this; }
    [[nodiscard]] constexpr bool operator==(std::default_sentinel_t) const {
      return owner_ == nullptr || !owner_->value_.has_value();
    }

   private:
    each_pieces_view* owner_ = nullptr;
  };

  [[nodiscard]] constexpr iterator begin() { return iterator(*this); }
  [[nodiscard]] constexpr std::default_sentinel_t end() const { return {}; }

 private:
  friend class iterator;

  constexpr void advance() {
    value_.reset();
    // The gathering begins again; the reading does not. Where the gathering
    // slots lie is said again first, because a view that was carried here from
    // somewhere else brought its gatherer with it, and that gatherer still
    // holds where they lay in the view it came from.
    static_cast<gatherer_type&>(source_).lives_in(collected_);
    static_cast<gatherer_type&>(source_).begin_again();
    auto taken = detail::take_from_pieces<Type, Format>(source_, cursor_, last_,
                                                       place_);
    if (!taken.matched) return;
    value_ = std::move(taken.value);
  }

  // Named before the walk that points at it, so it is standing by the time
  // the walk is built.
  typename gatherer_type::cold_type collected_{};
  source_type source_;
  const char* cursor_ = nullptr;
  const char* last_ = nullptr;
  std::ptrdiff_t place_ = 0;
  std::optional<Type> value_;
};

// The head of the input that the pattern takes, and what follows it.
template <fixed_string Format, detail::contiguous_char_range RangeType>
  requires(std::is_lvalue_reference_v<RangeType&&> || std::ranges::borrowed_range<RangeType>)
[[nodiscard]] constexpr auto scan_prefix(RangeType&& input) {
  return prefix_scan<Format>(detail::characters_of(input));
}

// The same, for input that has to be read as it comes. Nothing is buffered and
// nothing is looked at twice.
template <fixed_string Format, std::ranges::input_range RangeType>
  requires std::same_as<std::ranges::range_value_t<RangeType>, char> &&
           (!detail::contiguous_char_range<RangeType> ||
            (!std::is_lvalue_reference_v<RangeType&&> &&
             !std::ranges::borrowed_range<RangeType>))
[[nodiscard]] constexpr auto scan_prefix(RangeType&& input) {
  auto view = std::views::all(std::forward<RangeType>(input));
  return prefix_stream_scan<Format, decltype(view)>(std::move(view));
}

// Everything a scan is asked to be, said in one place.
//
// The same three questions the pattern layer's `match` answers, and the same
// answer to the way they used to be written: as names they multiply --
// `scan`, `scan_sentinel`, `scan_scalar`, `scan_sentinel_vec` and so on for
// every combination anybody will ever want. As parameters with a method each
// they compose, and the order they are written in does not matter:
//
//   scan::scan<f>(text)
//   scan::scan<f>.sentinel()(text)
//   scan::scan<f>.scalar()(text)
//   scan::scan<f>.sentinel<'\n'>().vec()(text)
//
// What the reading is handed decides the rest: characters in a row are read
// where they lie, pieces are read piece by piece, and anything else is read as
// it arrives. Saying the walk means nothing for those last two -- there is no
// length to ask about -- so they take what they are given and ignore it.
template <fixed_string Format, class Type = void, bool NoThrow = false,
          int Terminator = -1, how_to_walk Walk = how_to_walk::by_length>
struct scan_closure {
  // The output type, named before the subject rather than after it.
  //
  // A scan runs when the type is known, and it can be known from either end:
  // `scan<f>(text).of<T>()` says it after, `scan<f>.of<T>()(text)` says it
  // before. The second one is a whole reading with nothing left to say -- it
  // can be handed round, stored, or piped into.
  template <class Other>
  [[nodiscard]] constexpr scan_closure<Format, Other, false, Terminator, Walk>
  of() const {
    return {};
  }

  // The same, handing back what went wrong instead of throwing it.
  template <class Other>
  [[nodiscard]] constexpr scan_closure<Format, Other, true, Terminator, Walk>
  try_of() const {
    return {};
  }

  // Every place begins past whatever whitespace is in front of it, which is
  // what `%d` does and `{}` does not. Written out it is `{*\s*}` before every
  // place, and in a format with six fields that is six times the same words.
  //
  // It is not quite what `sscanf` does: there the skipping belongs to the
  // conversion, so `%d` and `%s` skip and `%c` and `%[a-z]` do not. Here it
  // belongs to the format, and every place in it skips.
  [[nodiscard]] constexpr scan_closure<Format.past_space(), Type, NoThrow,
                                       Terminator, Walk>
  past_space() const {
    return {};
  }

  // The subject ends where it ends, and the walk tests that as well as the
  // character -- except where the type of the subject carries a terminator of
  // its own, which is noticed without the caller having to say so.
  [[nodiscard]] constexpr scan_closure<Format, Type, NoThrow, -1, Walk> sized() const {
    return {};
  }

  // The subject carries a character the pattern can never match. Whether it is
  // really there is the caller's promise; whether the pattern can match it is
  // asked while the pattern is compiled.
  template <unsigned char Byte = 0>
  [[nodiscard]] constexpr scan_closure<Format, Type, NoThrow, Byte, Walk> sentinel() const {
    return {};
  }

  [[nodiscard]] constexpr scan_closure<Format, Type, NoThrow, Terminator,
                                       how_to_walk::by_length>
  by_length() const {
    return {};
  }

  [[nodiscard]] constexpr scan_closure<Format, Type, NoThrow, Terminator,
                                       how_to_walk::one_at_a_time>
  scalar() const {
    return {};
  }

  [[nodiscard]] constexpr scan_closure<Format, Type, NoThrow, Terminator,
                                       how_to_walk::in_words>
  vec() const {
    return {};
  }

  // Characters in a row: the answers borrow the storage they were read from
  // and nothing is allocated.
  template <detail::contiguous_char_range RangeType>
    requires(std::is_lvalue_reference_v<RangeType&&> ||
             std::ranges::borrowed_range<RangeType>)
  [[nodiscard]] constexpr auto operator()(RangeType&& input) const {
    const std::string_view text = detail::characters_of(input);
    auto reading = [&] {
      if constexpr (Terminator < 0) {
        return detail::borrowed_result<Format, -1,
                                       terminated_char_range<RangeType>,
                                       Walk>(text);
      } else {
        return detail::borrowed_result<Format, Terminator, false, Walk>(text);
      }
    }();
    if constexpr (std::is_void_v<Type>) {
      return reading;
    } else if constexpr (NoThrow) {
      return reading.template try_of<Type>();
    } else {
      return reading.template of<Type>();
    }
  }

  // Input that arrives in pieces. Each piece is characters in a row, so the
  // walk reads it in words and vectors; where a piece runs out it asks for the
  // next one and goes on where it stood. Nothing is buffered and no piece is
  // looked at twice, so what comes back owns whatever it holds.
  template <detail::piecewise_char_range PiecesType>
    requires(!detail::contiguous_char_range<PiecesType>)
  [[nodiscard]] constexpr auto operator()(PiecesType&& input) const {
    auto reading = detail::pieces_result<Format, PiecesType>(
        std::forward<PiecesType>(input));
    if constexpr (std::is_void_v<Type>) {
      return reading;
    } else if constexpr (NoThrow) {
      return reading.template try_of<Type>();
    } else {
      return reading.template of<Type>();
    }
  }

  // Input that has to be read as it comes. The proxy owns the view and
  // consumes it once, after the output type is known.
  template <std::ranges::input_range RangeType>
    requires std::same_as<std::ranges::range_value_t<RangeType>, char> &&
             (!detail::contiguous_char_range<RangeType> ||
              (!std::is_lvalue_reference_v<RangeType&&> &&
               !std::ranges::borrowed_range<RangeType>))
  [[nodiscard]] constexpr auto operator()(RangeType&& input) const {
    auto view = std::views::all(std::forward<RangeType>(input));
    auto reading = detail::streaming_result<Format, decltype(view)>(std::move(view));
    if constexpr (std::is_void_v<Type>) {
      return reading;
    } else if constexpr (NoThrow) {
      return reading.template try_of<Type>();
    } else {
      return reading.template of<Type>();
    }
  }

  // Stream-buffer iteration is unformatted and therefore keeps whitespace
  // whatever the stream's skipws flag says.
  [[nodiscard]] constexpr auto operator()(std::istream& input) const {
    auto range = std::ranges::subrange(std::istreambuf_iterator<char>(input),
                                       std::istreambuf_iterator<char>());
    auto reading = detail::streaming_result<Format, decltype(range)>(std::move(range));
    if constexpr (std::is_void_v<Type>) {
      return reading;
    } else if constexpr (NoThrow) {
      return reading.template try_of<Type>();
    } else {
      return reading.template of<Type>();
    }
  }
};

template <fixed_string Format>
inline constexpr scan_closure<Format> scan{};

template <class Type, fixed_string Format, std::ranges::input_range RangeType>
[[nodiscard]] constexpr Type scan_as(RangeType&& input) {
  return static_cast<Type>(scan<Format>(std::forward<RangeType>(input)));
}

template <class Type, fixed_string Format>
[[nodiscard]] constexpr Type scan_as(std::istream& input) {
  return static_cast<Type>(scan<Format>(input));
}

}  // namespace scan
