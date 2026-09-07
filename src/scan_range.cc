export module scan.range;

import std;
import boost.pfr;
export import scan.runtime;

export namespace scan::detail {

template <class type, fixed_string format, std::size_t extent, std::size_t... index>
[[nodiscard]] constexpr type convert(
    const std::array<std::string_view, extent>& fields,
    std::index_sequence<index...>) {
  static_assert(groups_of<type>() == extent,
                "placeholder count must equal the number of values the output "
                "type reads");
  // Built, not built empty and then written over. The aggregate used to be
  // default-constructed and each field assigned a temporary afterwards, which
  // for a field that owns storage is a construction, a move-assignment that
  // must first ask whether the destination is holding any, and a destruction --
  // three times what initialising it once costs. It also demanded that every
  // field be default-constructible and assignable, which is more than an
  // aggregate has to be.
  return or_thrown(build_value<failure_for<type>,
                               format_parameters<type, format>, type, 0>(
      fields));
}

template <fixed_string format, int terminator = -1, bool terminated = false,
          how_to_walk walk = how_to_walk::by_length>
class borrowed_result {
 public:
  constexpr explicit borrowed_result(std::string_view input) : input_(input) {}

  // The reading itself, which hands back what it read or what went wrong.
  // Everything below is this, asked for in one of the two ways.
  template <class type>
  [[nodiscard]] constexpr std::expected<type, failure_for<type>> read() const {
    static_assert(!scanned_by_format<type>,
                  "a type that declares its own format is read as a field, not "
                  "as the whole of what is scanned into: wrap it in a struct "
                  "with one member and scan into that");
    // A list or a fold is read by the machine that gathers as it goes, even
    // where the subject lies in a row and could be pointed at: what either of
    // them is made of are the turns, and the positions left behind hold the
    // last turn and nothing before it.
    if constexpr (holds_a_range<type>() || holds_a_fold<type>()) {
      return detail::scan_stream<type, format>(input_);
    } else {
      // A group that took no part is an error, unless somewhere in this output
      // there is a variant, where exactly one branch takes part and the rest do
      // not. Which it is, is known while the pattern is compiled.
      auto fields = [&] {
        if constexpr (holds_a_variant<type>() || scanned_as_variant<type>) {
          return scan_branch_fields<type, format, terminator, terminated, walk>(
              input_);
        } else {
          return scan_fields<type, format, terminator, terminated, walk>(
              input_);
        }
      }();
      if (!fields) {
        return std::unexpected(
            scan::as_a_failure<failure_for<type>>(std::move(fields).error()));
      }
      return build_value<failure_for<type>, format_parameters<type, format>,
                         type, 0>(*fields);
    }
  }

  template <class type>
    requires std::is_aggregate_v<type>
  constexpr operator type() const {
    return or_thrown(read<type>());
  }

  // The same scan, for a format that says the input may be one of several
  // shapes. Which branch ran is read from the mark each branch was given.
  template <class type>
    requires scanned_as_variant<type>
  constexpr operator type() const {
    return or_thrown(read<type>());
  }

  // The same scan, said rather than implied, and the same scan that does not
  // throw.
  //
  // Assigning the result of a scan to something converts it, and a conversion
  // has nowhere to put a failure but an exception. Named, it has: `of` is the
  // conversion under another spelling, and `try_of` hands back what went wrong
  // instead of throwing it.
  template <class type>
  [[nodiscard]] constexpr type of() const {
    return static_cast<type>(*this);
  }

  template <class type>
  [[nodiscard]] constexpr std::expected<type, detail::failure_for<type>>
  try_of() const {
    return read<type>();
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
  [[nodiscard]] constexpr borrowed_result<format, -1, terminated, walk> sized()
      const {
    return borrowed_result<format, -1, terminated, walk>(input_);
  }

  template <unsigned char byte = 0>
  [[nodiscard]] constexpr borrowed_result<format, byte, false, walk> sentinel()
      const {
    return borrowed_result<format, byte, false, walk>(input_);
  }

  [[nodiscard]] constexpr borrowed_result<format, terminator, terminated,
                                          how_to_walk::by_length>
  by_length() const {
    return borrowed_result<format, terminator, terminated,
                           how_to_walk::by_length>(input_);
  }

  [[nodiscard]] constexpr borrowed_result<format, terminator, terminated,
                                          how_to_walk::one_at_a_time>
  scalar() const {
    return borrowed_result<format, terminator, terminated,
                           how_to_walk::one_at_a_time>(input_);
  }

  [[nodiscard]] constexpr borrowed_result<format, terminator, terminated,
                                          how_to_walk::in_words>
  vec() const {
    return borrowed_result<format, terminator, terminated,
                           how_to_walk::in_words>(input_);
  }

 private:
  std::string_view input_;
};

// What a scan over pieces hands back until somebody says what it is scanning
// into.
template <fixed_string format, class pieces_type>
class pieces_result {
 public:
  constexpr explicit pieces_result(pieces_type input)
      : input_(std::move(input)) {}

  pieces_result(pieces_result&&) = default;
  pieces_result& operator=(pieces_result&&) = default;
  pieces_result(const pieces_result&) = delete;
  pieces_result& operator=(const pieces_result&) = delete;

  template <class type>
  [[nodiscard]] constexpr std::expected<type, detail::failure_for<type>>
  read() {
    return scan_pieces<type, format>(std::move(input_));
  }

  template <class type>
    requires std::is_aggregate_v<type>
  constexpr operator type() {
    return or_thrown(read<type>());
  }

  template <class type>
  [[nodiscard]] constexpr type of() {
    return static_cast<type>(*this);
  }

  template <class type>
  [[nodiscard]] constexpr std::expected<type, detail::failure_for<type>>
  try_of() {
    return read<type>();
  }

 private:
  pieces_type input_;
};

template <fixed_string format, std::ranges::input_range range_type>
class streaming_result {
 public:
  constexpr explicit streaming_result(range_type input)
      : input_(std::move(input)) {}

  streaming_result(streaming_result&&) = default;
  streaming_result& operator=(streaming_result&&) = default;
  streaming_result(const streaming_result&) = delete;
  streaming_result& operator=(const streaming_result&) = delete;

  template <class type>
  [[nodiscard]] constexpr std::expected<type, detail::failure_for<type>>
  read() {
    return scan_stream<type, format>(input_);
  }

  template <class type>
    requires std::is_aggregate_v<type>
  constexpr operator type() {
    return or_thrown(read<type>());
  }

  template <class type>
  [[nodiscard]] constexpr type of() {
    return static_cast<type>(*this);
  }

  template <class type>
  [[nodiscard]] constexpr std::expected<type, detail::failure_for<type>>
  try_of() {
    return read<type>();
  }

 private:
  range_type input_;
};


}  // namespace scan::detail

export namespace scan {

// Whether the type of the input promises a terminator past its last character.
//
// A `std::string` does, and has since the language said `data()` and `c_str()`
// are the same thing: there is a null character at `data() + size()`. A string
// literal does. A `string_view` into the middle of something does not, and
// neither does a vector of characters.
template <class range_type>
concept terminated_char_range =
    detail::contiguous_char_range<range_type> &&
    (std::same_as<std::remove_cvref_t<range_type>, std::string> ||
     std::same_as<std::remove_cvref_t<range_type>,
                  std::basic_string<char, std::char_traits<char>,
                                    std::allocator<char>>> ||
     // A string literal is read to the nul the compiler put there rather than
     // by counting, which is the faster of the two walks and the one nobody
     // has to ask for.
     detail::literal_char_range<range_type>);

// What a scan of the head of an input hands back: the values, and what is left.
template <class type>
struct taken {
  type value;
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
template <fixed_string format>
class prefix_scan {
 public:
  constexpr explicit prefix_scan(std::string_view input) : input_(input) {}

  // The head and what follows it, or what went wrong instead. The reading
  // itself; `take` is this, asked for rather than tried for.
  template <class type>
  [[nodiscard]] constexpr std::expected<taken<type>, detail::failure_for<type>>
  try_take() const {
    // One walk: where the head ends and what is in it come back together, out
    // of the registers the walk was carrying anyway.
    const auto found =
        detail::taken_prefix_fields<type, format,
                                    detail::holds_a_variant<type>()>(input_);
    if (!found.matched) {
      return std::unexpected(
          scan::as_a_failure<detail::failure_for<type>>(
              no_match("input does not begin with the pattern")));
    }
    auto made = detail::build_value<detail::failure_for<type>,
                                    detail::format_parameters<type, format>,
                                    type, 0>(found.groups);
    if (!made) return std::unexpected(std::move(made).error());
    return taken<type>{std::move(*made), input_.substr(found.head.size())};
  }

  template <class type>
  [[nodiscard]] constexpr taken<type> take() const {
    return or_thrown(try_take<type>());
  }

  template <class type>
    requires std::is_aggregate_v<type> || detail::scanned_as_variant<type>
  constexpr operator type() const {
    return take<type>().value;
  }

  template <class type>
  [[nodiscard]] constexpr type of() const {
    return take<type>().value;
  }

  template <class type>
  [[nodiscard]] constexpr std::expected<type, detail::failure_for<type>>
  try_of() const {
    auto got = try_take<type>();
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
template <class type, fixed_string format>
class each_view {
 public:
  constexpr explicit each_view(std::string_view input) : rest_(input) {}

  class iterator {
   public:
    using value_type = type;
    using difference_type = std::ptrdiff_t;

    constexpr iterator() = default;
    constexpr explicit iterator(each_view& owner) : owner_(&owner) {}

    [[nodiscard]] constexpr const type& operator*() const {
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
        detail::taken_prefix_fields<type, format,
                                    detail::holds_a_variant<type>()>(rest_);
    if (!found.matched) return;
    auto made = detail::build_value<detail::failure_for<type>,
                                    detail::format_parameters<type, format>,
                                    type, 0>(found.groups);
    // A match whose values did not read ends the reading, the same way a
    // subject that stopped matching does: the loop asked for values and there
    // are none, and there is nowhere in a loop to hand a failure to.
    if (!made) return;
    value_ = std::move(*made);
    rest_ = rest_.substr(found.head.size());
  }

  std::string_view rest_;
  std::optional<type> value_;
};

// The same off a range that is read as it comes.
template <class type, fixed_string format, class range_type>
class each_stream_view {
 public:
  constexpr explicit each_stream_view(range_type input)
      : input_(std::move(input)),
        first_(std::ranges::begin(input_)) {}

  each_stream_view(each_stream_view&&) = default;
  each_stream_view& operator=(each_stream_view&&) = default;
  each_stream_view(const each_stream_view&) = delete;
  each_stream_view& operator=(const each_stream_view&) = delete;

  class iterator {
   public:
    using value_type = type;
    using difference_type = std::ptrdiff_t;

    constexpr iterator() = default;
    constexpr explicit iterator(each_stream_view& owner) : owner_(&owner) {}

    [[nodiscard]] constexpr const type& operator*() const {
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
    auto got = detail::scan_stream_prefix<type, format>(
        first_, std::ranges::end(input_), carry_);
    if (!got) {
      value_.reset();
      return;
    }
    value_ = std::move(got->value);
    stopped_ = got->stopped;
  }

  range_type input_;
  std::ranges::iterator_t<range_type> first_;
  // What the last match read and did not keep. A reading that can be gone back
  // over holds nothing here and the iterator goes back instead.
  detail::stream_carry_for<type, format,
                           std::ranges::iterator_t<range_type>> carry_;
  std::optional<type> value_;
  std::optional<char> stopped_;
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
//     scan::reader<command, "set {[a-z]+} {[0-9]+}"> reading;
//     for (;;) {
//       const char symbol = next();
//       if (reading.offer(symbol)) continue;
//       if (reading.accepting()) act(reading.take());
//       reading.restart();
//       if (!reading.offer(symbol)) report(symbol);
//     }
template <class type, fixed_string format>
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
  [[nodiscard]] constexpr std::expected<type, detail::failure_for<type>>
  try_take() const {
    return state_.finish();
  }

  [[nodiscard]] constexpr type take() const {
    return or_thrown(try_take());
  }

  // What has been gathered for a field so far, while the match is still going
  // on -- for showing a command back as it is typed, for finishing it for
  // whoever is typing, for refusing it before they are done.
  template <std::size_t field>
  [[nodiscard]] constexpr const auto& gathering() const {
    return state_.template gathering<field>();
  }

  // Whether that field is being read right now.
  template <std::size_t field>
  [[nodiscard]] constexpr bool reading() const {
    return state_.template reading<field>();
  }

  // Whether nothing can follow what has been offered: the machine is not in a
  // match and cannot get into one from here.
  [[nodiscard]] constexpr bool rejected() const { return state_.rejected(); }

  constexpr void restart() { state_.restart(); }

 private:
  detail::stream_state<type, format, false> state_;
};

// The head of a range that is read once. The range is left standing after the
// character that ended the match, which is handed back with the values because
// it has been read and there is nowhere to put it back.
template <fixed_string format, std::ranges::input_range range_type>
class prefix_stream_scan {
 public:
  constexpr explicit prefix_stream_scan(range_type input)
      : input_(std::move(input)) {}

  prefix_stream_scan(prefix_stream_scan&&) = default;
  prefix_stream_scan& operator=(prefix_stream_scan&&) = default;
  prefix_stream_scan(const prefix_stream_scan&) = delete;
  prefix_stream_scan& operator=(const prefix_stream_scan&) = delete;

  template <class type>
  [[nodiscard]] constexpr std::expected<detail::taken_ahead<type>,
                                        detail::failure_for<type>>
  try_take() {
    return detail::scan_stream_prefix<type, format>(input_);
  }

  template <class type>
  [[nodiscard]] constexpr detail::taken_ahead<type> take() {
    return or_thrown(try_take<type>());
  }

  template <class type>
    requires std::is_aggregate_v<type>
  constexpr operator type() {
    return take<type>().value;
  }

  template <class type>
  [[nodiscard]] constexpr type of() {
    return take<type>().value;
  }

  template <class type>
  [[nodiscard]] constexpr std::expected<type, detail::failure_for<type>>
  try_of() {
    auto got = try_take<type>();
    if (!got) return std::unexpected(got.error());
    return std::move(got->value);
  }

 private:
  range_type input_;
};

// One match after another. The output type is named where the reading starts,
// for the same reason the head of an input names it: until it is said there is
// nothing to work out.
template <fixed_string format>
class each_scan {
 public:
  constexpr explicit each_scan(std::string_view input) : input_(input) {}

  template <class type>
  [[nodiscard]] constexpr each_view<type, format> of() const {
    // Asked of the compiled automaton, which is not built at all where they
    // are built while the program runs. There the same pattern is refused by
    // the reading itself, which cannot move and says so.
    if constexpr (!detail::automata_at_runtime) {
      static_assert(!detail::matches_nothing<
                        detail::packed_automaton<type, format>>(),
                    "this pattern is happy with nothing at all, so reading one "
                    "match after another would never move");
    }
    return each_view<type, format>(input_);
  }

 private:
  std::string_view input_;
};

template <fixed_string format, class range_type>
class each_stream_scan {
 public:
  constexpr explicit each_stream_scan(range_type input)
      : input_(std::move(input)) {}

  each_stream_scan(each_stream_scan&&) = default;
  each_stream_scan& operator=(each_stream_scan&&) = default;
  each_stream_scan(const each_stream_scan&) = delete;
  each_stream_scan& operator=(const each_stream_scan&) = delete;

  template <class type>
  [[nodiscard]] constexpr each_stream_view<type, format, range_type> of() && {
    static_assert(!detail::matches_nothing<
                      detail::streaming_automaton<type, format>>(),
                  "this pattern is happy with nothing at all, so reading one "
                  "match after another would never move");
    return each_stream_view<type, format, range_type>(std::move(input_));
  }

 private:
  range_type input_;
};

// One match after another, off a contiguous input or off one that is read as
// it comes.
//
// Said as a name rather than called, so that it can be either: `each<f>(text)`
// is the call, `text | each<f>` is the same thing said the other way round,
// and the type each of them hands back is asked for the same way --
// `.of<type>()`.
template <class type, fixed_string format, class pieces_type>
class each_pieces_view;

template <fixed_string format, class pieces_type>
class each_pieces_scan;

template <fixed_string format>
struct each_closure : std::ranges::range_adaptor_closure<each_closure<format>> {
  // Off input that arrives in pieces: each piece read in words and vectors,
  // and the reading held between matches.
  template <detail::piecewise_char_range pieces_type>
    requires(!detail::contiguous_char_range<pieces_type> &&
             !std::same_as<std::ranges::range_value_t<pieces_type>, char>)
  [[nodiscard]] constexpr auto operator()(pieces_type&& input) const {
    auto view = std::views::all(std::forward<pieces_type>(input));
    return each_pieces_scan<format, decltype(view)>(std::move(view));
  }

  template <detail::contiguous_char_range range_type>
    requires(std::is_lvalue_reference_v<range_type&&> ||
             std::ranges::borrowed_range<range_type>)
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    return each_scan<format>(detail::characters_of(input));
  }

  template <std::ranges::input_range range_type>
    requires std::same_as<std::ranges::range_value_t<range_type>, char> &&
             (!detail::contiguous_char_range<range_type> ||
              (!std::is_lvalue_reference_v<range_type&&> &&
               !std::ranges::borrowed_range<range_type>))
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    auto view = std::views::all(std::forward<range_type>(input));
    return each_stream_scan<format, decltype(view)>(std::move(view));
  }
};

template <fixed_string format>
inline constexpr each_closure<format> each{};

// What `each` over pieces hands back until somebody says what it reads into.
template <fixed_string format, class pieces_type>
class each_pieces_scan {
 public:
  constexpr explicit each_pieces_scan(pieces_type input)
      : input_(std::move(input)) {}

  each_pieces_scan(each_pieces_scan&&) = default;
  each_pieces_scan& operator=(each_pieces_scan&&) = default;
  each_pieces_scan(const each_pieces_scan&) = delete;
  each_pieces_scan& operator=(const each_pieces_scan&) = delete;

  template <class type>
  [[nodiscard]] constexpr each_pieces_view<type, format, pieces_type> of() && {
    return each_pieces_view<type, format, pieces_type>(std::move(input_));
  }

 private:
  pieces_type input_;
};

// One match after another off input that arrives in pieces.
//
// The reading is held between matches: where one stopped is where the next
// begins, in the piece the walk is holding, so nothing is put back and nothing
// is read twice. The gathering starts again for each match; the pieces do not.
template <class type, fixed_string format, class pieces_type>
class each_pieces_view {
 public:
  using automaton_type =
      std::remove_cvref_t<decltype(detail::streaming_automaton<type, format>)>;
  using gatherer_type =
      detail::field_gatherer<type, format,
                             detail::streaming_automaton<type, format>>;
  using source_type = detail::gathers_from_pieces<gatherer_type, pieces_type>;

  constexpr explicit each_pieces_view(pieces_type input)
      : source_(gatherer_type{}, std::move(input)) {}

  each_pieces_view(each_pieces_view&&) = default;
  each_pieces_view& operator=(each_pieces_view&&) = default;
  each_pieces_view(const each_pieces_view&) = delete;
  each_pieces_view& operator=(const each_pieces_view&) = delete;

  class iterator {
   public:
    using value_type = type;
    using difference_type = std::ptrdiff_t;

    constexpr iterator() = default;
    constexpr explicit iterator(each_pieces_view& owner) : owner_(&owner) {
      owner_->advance();
    }

    [[nodiscard]] constexpr const type& operator*() const {
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
    // The gathering begins again; the reading does not.
    static_cast<gatherer_type&>(source_) = gatherer_type{};
    auto taken = detail::take_from_pieces<type, format>(source_, cursor_, last_,
                                                       place_);
    if (!taken.matched) return;
    value_ = std::move(taken.value);
  }

  source_type source_;
  const char* cursor_ = nullptr;
  const char* last_ = nullptr;
  std::ptrdiff_t place_ = 0;
  std::optional<type> value_;
};

// The head of the input that the pattern takes, and what follows it.
template <fixed_string format, detail::contiguous_char_range range_type>
  requires(std::is_lvalue_reference_v<range_type&&> || std::ranges::borrowed_range<range_type>)
[[nodiscard]] constexpr auto scan_prefix(range_type&& input) {
  return prefix_scan<format>(detail::characters_of(input));
}

// The same, for input that has to be read as it comes. Nothing is buffered and
// nothing is looked at twice.
template <fixed_string format, std::ranges::input_range range_type>
  requires std::same_as<std::ranges::range_value_t<range_type>, char> &&
           (!detail::contiguous_char_range<range_type> ||
            (!std::is_lvalue_reference_v<range_type&&> &&
             !std::ranges::borrowed_range<range_type>))
[[nodiscard]] constexpr auto scan_prefix(range_type&& input) {
  auto view = std::views::all(std::forward<range_type>(input));
  return prefix_stream_scan<format, decltype(view)>(std::move(view));
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
template <fixed_string format, class type = void, bool no_throw = false,
          int terminator = -1, how_to_walk walk = how_to_walk::by_length>
struct scan_closure {
  // The output type, named before the subject rather than after it.
  //
  // A scan runs when the type is known, and it can be known from either end:
  // `scan<f>(text).of<T>()` says it after, `scan<f>.of<T>()(text)` says it
  // before. The second one is a whole reading with nothing left to say -- it
  // can be handed round, stored, or piped into.
  template <class other>
  [[nodiscard]] constexpr scan_closure<format, other, false, terminator, walk>
  of() const {
    return {};
  }

  // The same, handing back what went wrong instead of throwing it.
  template <class other>
  [[nodiscard]] constexpr scan_closure<format, other, true, terminator, walk>
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
  [[nodiscard]] constexpr scan_closure<format.past_space(), type, no_throw,
                                       terminator, walk>
  past_space() const {
    return {};
  }

  // The subject ends where it ends, and the walk tests that as well as the
  // character -- except where the type of the subject carries a terminator of
  // its own, which is noticed without the caller having to say so.
  [[nodiscard]] constexpr scan_closure<format, type, no_throw, -1, walk> sized() const {
    return {};
  }

  // The subject carries a character the pattern can never match. Whether it is
  // really there is the caller's promise; whether the pattern can match it is
  // asked while the pattern is compiled.
  template <unsigned char byte = 0>
  [[nodiscard]] constexpr scan_closure<format, type, no_throw, byte, walk> sentinel() const {
    return {};
  }

  [[nodiscard]] constexpr scan_closure<format, type, no_throw, terminator,
                                       how_to_walk::by_length>
  by_length() const {
    return {};
  }

  [[nodiscard]] constexpr scan_closure<format, type, no_throw, terminator,
                                       how_to_walk::one_at_a_time>
  scalar() const {
    return {};
  }

  [[nodiscard]] constexpr scan_closure<format, type, no_throw, terminator,
                                       how_to_walk::in_words>
  vec() const {
    return {};
  }

  // Characters in a row: the answers borrow the storage they were read from
  // and nothing is allocated.
  template <detail::contiguous_char_range range_type>
    requires(std::is_lvalue_reference_v<range_type&&> ||
             std::ranges::borrowed_range<range_type>)
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    const std::string_view text = detail::characters_of(input);
    auto reading = [&] {
      if constexpr (terminator < 0) {
        return detail::borrowed_result<format, -1,
                                       terminated_char_range<range_type>,
                                       walk>(text);
      } else {
        return detail::borrowed_result<format, terminator, false, walk>(text);
      }
    }();
    if constexpr (std::is_void_v<type>) {
      return reading;
    } else if constexpr (no_throw) {
      return reading.template try_of<type>();
    } else {
      return reading.template of<type>();
    }
  }

  // Input that arrives in pieces. Each piece is characters in a row, so the
  // walk reads it in words and vectors; where a piece runs out it asks for the
  // next one and goes on where it stood. Nothing is buffered and no piece is
  // looked at twice, so what comes back owns whatever it holds.
  template <detail::piecewise_char_range pieces_type>
    requires(!detail::contiguous_char_range<pieces_type>)
  [[nodiscard]] constexpr auto operator()(pieces_type&& input) const {
    auto reading = detail::pieces_result<format, pieces_type>(
        std::forward<pieces_type>(input));
    if constexpr (std::is_void_v<type>) {
      return reading;
    } else if constexpr (no_throw) {
      return reading.template try_of<type>();
    } else {
      return reading.template of<type>();
    }
  }

  // Input that has to be read as it comes. The proxy owns the view and
  // consumes it once, after the output type is known.
  template <std::ranges::input_range range_type>
    requires std::same_as<std::ranges::range_value_t<range_type>, char> &&
             (!detail::contiguous_char_range<range_type> ||
              (!std::is_lvalue_reference_v<range_type&&> &&
               !std::ranges::borrowed_range<range_type>))
  [[nodiscard]] constexpr auto operator()(range_type&& input) const {
    auto view = std::views::all(std::forward<range_type>(input));
    auto reading = detail::streaming_result<format, decltype(view)>(std::move(view));
    if constexpr (std::is_void_v<type>) {
      return reading;
    } else if constexpr (no_throw) {
      return reading.template try_of<type>();
    } else {
      return reading.template of<type>();
    }
  }

  // Stream-buffer iteration is unformatted and therefore keeps whitespace
  // whatever the stream's skipws flag says.
  [[nodiscard]] constexpr auto operator()(std::istream& input) const {
    auto range = std::ranges::subrange(std::istreambuf_iterator<char>(input),
                                       std::istreambuf_iterator<char>());
    auto reading = detail::streaming_result<format, decltype(range)>(std::move(range));
    if constexpr (std::is_void_v<type>) {
      return reading;
    } else if constexpr (no_throw) {
      return reading.template try_of<type>();
    } else {
      return reading.template of<type>();
    }
  }
};

template <fixed_string format>
inline constexpr scan_closure<format> scan{};

template <class type, fixed_string format, std::ranges::input_range range_type>
[[nodiscard]] constexpr type scan_as(range_type&& input) {
  return static_cast<type>(scan<format>(std::forward<range_type>(input)));
}

template <class type, fixed_string format>
[[nodiscard]] constexpr type scan_as(std::istream& input) {
  return static_cast<type>(scan<format>(input));
}

}  // namespace scan
