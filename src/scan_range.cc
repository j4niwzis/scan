export module scan.range;

import std;
import boost.pfr;
export import scan.runtime;

export namespace scan::detail {

template <class type>
[[nodiscard]] constexpr type parse_value(std::string_view text,
                                         std::string_view parameters) {
  using value_type = std::remove_cv_t<type>;
  static_assert(requires { scanner_parse<value_type>(text); },
                "scan::scanner<type> must provide parse(string_view)");
  return scanner_parse<value_type>(text, parameters);
}

// Where a branch's mark stands, counting from the start of the variant: each
// branch before it took a mark of its own and whatever its alternative reads.
template <class type, std::size_t branch>
[[nodiscard]] consteval std::size_t groups_before_branch() {
  return []<std::size_t... which>(std::index_sequence<which...>) {
    return (std::size_t{0} + ... +
            (1 + groups_of<std::variant_alternative_t<which, type>>()));
  }(std::make_index_sequence<branch>{});
}

// Whether a variant stands anywhere inside this output, at any depth. Where one
// does, a group that took no part is the ordinary state of affairs rather than
// a fault.
template <class type>
[[nodiscard]] consteval bool holds_a_variant() {
  if constexpr (scanned_as_variant<type>) {
    return true;
  } else if constexpr (scanned_as_leaf<type>) {
    return false;
  } else {
    return []<std::size_t... field>(std::index_sequence<field...>) {
      return (false || ... ||
              holds_a_variant<typename parts_of<type>::template at<field>>());
    }(std::make_index_sequence<parts_of<type>::count>{});
  }
}

// Whether a list stands anywhere inside this output.
//
// A list is read by gathering, and it has to be: the positions a match leaves
// behind hold the last turn round the loop and nothing else, so an output with
// a list in it cannot be put together by reading them afterwards, however well
// they can be pointed at. It goes to the machine that gathers as it goes, over
// the very same characters.
template <class type>
[[nodiscard]] consteval bool holds_a_range() {
  if constexpr (scanned_as_range<type>) {
    return true;
  } else if constexpr (scanned_as_leaf<type>) {
    return false;
  } else if constexpr (scanned_as_variant<type>) {
    return []<std::size_t... which>(std::index_sequence<which...>) {
      return (false || ... ||
              holds_a_range<std::variant_alternative_t<which, type>>());
    }(std::make_index_sequence<std::variant_size_v<type>>{});
  } else {
    return []<std::size_t... part>(std::index_sequence<part...>) {
      return (false || ... ||
              holds_a_range<typename parts_of<type>::template at<part>>());
    }(std::make_index_sequence<parts_of<type>::count>{});
  }
}

// A leaf is read from its one group; a product is built from its fields, each
// of which takes as many groups as it needs, in order. Nothing about the
// nesting is written in the format: a structure of structures is spelled out
// flat, because a product of products is flat.
// The parameters come from the same spread the automaton was built from, so a
// colon written at the place a value is read from reaches that value however
// deeply it sits, and a format declared by a type is read against that type
// here exactly as it was there.
template <class root, class type, fixed_string format, std::size_t offset,
          std::size_t extent>
[[nodiscard]] constexpr type build_value(
    const std::array<std::string_view, extent>& groups) {
  if constexpr (scanned_as_leaf<type>) {
    static constexpr auto spread = spread_of<root, format>();
    return parse_value<std::remove_cv_t<type>>(
        groups[offset], spread.parameters[offset].view());
  } else if constexpr (scanned_as_variant<type>) {
    // Exactly one branch ran, and its mark says so: a mark that took part
    // points into the subject, and the others point nowhere.
    return [&]<std::size_t... branch>(std::index_sequence<branch...>) -> type {
      std::optional<type> made;
      const auto take = [&]<std::size_t which>() {
        constexpr std::size_t mark = offset + groups_before_branch<type, which>();
        if (made || groups[mark].data() == nullptr) return;
        using alternative = std::variant_alternative_t<which, type>;
        made.emplace(std::in_place_index<which>,
                     build_value<root, alternative, format, mark + 1>(groups));
      };
      (take.template operator()<branch>(), ...);
      if (!made) throw scan_error("no branch of the format took the input");
      return std::move(*made);
    }(std::make_index_sequence<std::variant_size_v<type>>{});
  } else if constexpr (scanned_from_values<type>) {
    // Made by the call it named, out of the values its places stood for.
    return [&]<std::size_t... index>(std::index_sequence<index...>) {
      return scan::scanner<std::remove_cv_t<type>>::parse(
          build_value<root, typename parts_of<type>::template at<index>, format,
                      offset + groups_before_field<type, index>()>(groups)...);
    }(std::make_index_sequence<parts_of<type>::count>{});
  } else {
    return [&]<std::size_t... index>(std::index_sequence<index...>) {
      return type{build_value<root, typename parts_of<type>::template at<index>,
                              format,
                              offset + groups_before_field<type, index>()>(
          groups)...};
    }(std::make_index_sequence<parts_of<type>::count>{});
  }
}
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
  return build_value<type, type, format, 0>(fields);
}

template <fixed_string format, int sentinel = -1, bool terminated = false>
class borrowed_result {
 public:
  constexpr explicit borrowed_result(std::string_view input) : input_(input) {}

  template <class type>
    requires std::is_aggregate_v<type>
  constexpr operator type() const {
    static_assert(!scanned_by_format<type>,
                  "a type that declares its own format is read as a field, not "
                  "as the whole of what is scanned into: wrap it in a struct "
                  "with one member and scan into that");
    if constexpr (holds_a_range<type>()) {
      return detail::scan_stream<type, format>(input_);
    } else {
    // A group that took no part is an error, unless somewhere in this output
    // there is a variant, where exactly one branch takes part and the rest do
    // not. Which it is, is known while the pattern is compiled.
    const auto fields =
        [&] {
          if constexpr (holds_a_variant<type>()) {
            return scan_branch_fields<type, format, sentinel, terminated>(input_);
          } else {
            return scan_fields<type, format, sentinel, terminated>(input_);
          }
        }();
    return build_value<type, type, format, 0>(fields);
    }
  }

  // The same scan, for a format that says the input may be one of several
  // shapes. Which branch ran is read from the mark each branch was given.
  template <class type>
    requires scanned_as_variant<type>
  constexpr operator type() const {
    const auto groups =
        scan_branch_fields<type, format, sentinel, terminated>(input_);
    return build_value<type, type, format, 0>(groups);
  }

 private:
  std::string_view input_;
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
    requires std::is_aggregate_v<type>
  constexpr operator type() {
    return scan_stream<type, format>(input_);
  }

 private:
  range_type input_;
};

template <class range_type>
concept contiguous_char_range =
    std::ranges::contiguous_range<range_type> &&
    std::ranges::sized_range<range_type> &&
    std::same_as<std::ranges::range_value_t<range_type>, char>;


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
                                    std::allocator<char>>>);

// This overload borrows the original contiguous storage and allocates nothing.
//
// Where the input is of a type that carries a terminator, this says so, and the
// scan then runs the loop that tests only the character and not the end of the
// input as well -- provided the pattern is one the terminator cannot appear in,
// which is asked while the pattern is compiled and answered without the caller
// having to know it was asked.
template <fixed_string format, detail::contiguous_char_range range_type>
  requires(std::is_lvalue_reference_v<range_type&&> || std::ranges::borrowed_range<range_type>)
[[nodiscard]] constexpr auto scan(range_type&& input) {
  return detail::borrowed_result<format, -1, terminated_char_range<range_type>>(
      std::string_view(std::ranges::data(input), std::ranges::size(input)));
}

// The same, for input that carries a terminator the pattern never matches.
//
// Without one the loop tests the end of the input on every character as well
// as the character itself; with one the terminator fails the class test like
// any other symbol no transition takes. It is the caller's promise that the
// terminator is there -- a `std::string` always has it, a `string_view` into
// the middle of something does not.
template <fixed_string format, int sentinel = 0,
          detail::contiguous_char_range range_type>
  requires(std::is_lvalue_reference_v<range_type&&> || std::ranges::borrowed_range<range_type>)
[[nodiscard]] constexpr auto scan_sentinel(range_type&& input) {
  return detail::borrowed_result<format, sentinel>(std::string_view(
      std::ranges::data(input), std::ranges::size(input)));
}

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

  template <class type>
  [[nodiscard]] constexpr taken<type> take() const {
    const std::string_view head = detail::taken_prefix<type, format>(input_);
    // By length, always: the head is a piece of the middle of something, and
    // whatever terminator the whole input carried is not behind it.
    return {detail::borrowed_result<format>(head), input_.substr(head.size())};
  }

  template <class type>
    requires std::is_aggregate_v<type> || detail::scanned_as_variant<type>
  constexpr operator type() const {
    return take<type>().value;
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
    const std::string_view head =
        detail::taken_prefix_or_none<type, format>(rest_);
    if (head.data() == nullptr) return;
    value_ = detail::borrowed_result<format>(head);
    rest_ = rest_.substr(head.size());
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
    if (first_ == std::ranges::end(input_)) return;
    try {
      auto got = detail::scan_stream_prefix<type, format>(
          first_, std::ranges::end(input_));
      value_ = std::move(got.value);
      stopped_ = got.stopped;
    } catch (const scan_error&) {
      value_.reset();
    }
  }

  range_type input_;
  std::ranges::iterator_t<range_type> first_;
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

  // The values. Reading further after this is reading further into the same
  // match, so whoever wants the next one says so.
  [[nodiscard]] constexpr type take() const { return state_.finish(); }

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
  detail::stream_state<type, format> state_;
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
  [[nodiscard]] constexpr detail::taken_ahead<type> take() {
    return detail::scan_stream_prefix<type, format>(input_);
  }

  template <class type>
    requires std::is_aggregate_v<type>
  constexpr operator type() {
    return take<type>().value;
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

// One match after another, off a contiguous input.
template <fixed_string format, detail::contiguous_char_range range_type>
  requires(std::is_lvalue_reference_v<range_type&&> || std::ranges::borrowed_range<range_type>)
[[nodiscard]] constexpr auto each(range_type&& input) {
  return each_scan<format>(std::string_view(std::ranges::data(input),
                                            std::ranges::size(input)));
}

// And off one that is read as it comes.
template <fixed_string format, std::ranges::input_range range_type>
  requires std::same_as<std::ranges::range_value_t<range_type>, char> &&
           (!detail::contiguous_char_range<range_type> ||
            (!std::is_lvalue_reference_v<range_type&&> &&
             !std::ranges::borrowed_range<range_type>))
[[nodiscard]] constexpr auto each(range_type&& input) {
  auto view = std::views::all(std::forward<range_type>(input));
  return each_stream_scan<format, decltype(view)>(std::move(view));
}

// The head of the input that the pattern takes, and what follows it.
template <fixed_string format, detail::contiguous_char_range range_type>
  requires(std::is_lvalue_reference_v<range_type&&> || std::ranges::borrowed_range<range_type>)
[[nodiscard]] constexpr auto scan_prefix(range_type&& input) {
  return prefix_scan<format>(std::string_view(std::ranges::data(input),
                                              std::ranges::size(input)));
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

// The proxy owns the view and consumes it once after the output type is known.
template <fixed_string format, std::ranges::input_range range_type>
  requires std::same_as<std::ranges::range_value_t<range_type>, char> &&
           (!detail::contiguous_char_range<range_type> ||
            (!std::is_lvalue_reference_v<range_type&&> &&
             !std::ranges::borrowed_range<range_type>))
[[nodiscard]] constexpr auto scan(range_type&& input) {
  auto view = std::views::all(std::forward<range_type>(input));
  return detail::streaming_result<format, decltype(view)>(std::move(view));
}

// Stream-buffer iteration is unformatted and therefore preserves whitespace
// regardless of the stream's skipws formatting flag.
template <fixed_string format>
[[nodiscard]] constexpr auto scan(std::istream& input) {
  auto range = std::ranges::subrange(std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>());
  return detail::streaming_result<format, decltype(range)>(std::move(range));
}

template <class type, fixed_string format, std::ranges::input_range range_type>
[[nodiscard]] constexpr type scan_as(range_type&& input) {
  return static_cast<type>(scan<format>(std::forward<range_type>(input)));
}

template <class type, fixed_string format>
[[nodiscard]] constexpr type scan_as(std::istream& input) {
  return static_cast<type>(scan<format>(input));
}

}  // namespace scan
