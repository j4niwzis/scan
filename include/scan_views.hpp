// Generated from the module interface unit of the same name. Do not edit.
#pragma once

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>
#include "scan_core.hpp"

namespace scan {

namespace views {

template <class Type, std::size_t Extent>
struct static_chunk_value {
  std::array<Type, Extent> values{};
  std::size_t size = 0;

  template <std::size_t... Index>
  [[nodiscard]] constexpr auto as_ptr_tuple(std::index_sequence<Index...>) const {
    return std::tuple{(Index < size ? std::addressof(values[Index]) : nullptr)...};
  }

  [[nodiscard]] constexpr auto as_ptr_tuple() const {
    return as_ptr_tuple(std::make_index_sequence<Extent>{});
  }
};

template <std::size_t Extent>
struct static_chunk_adaptor
    : std::ranges::range_adaptor_closure<static_chunk_adaptor<Extent>> {
  // Groups of `extent`, without std::views::chunk: a group is the source
  // dropped by as many elements as the groups before it and taken up to
  // `extent`, and the groups themselves are the iota of how many there are.
  // Nothing is materialised until the group is asked for, and the source is
  // walked once per group -- which is what a forward range is for, and what
  // the two-element splits this adapts are small enough not to care about.
  template <std::ranges::viewable_range RangeType>
    requires std::ranges::forward_range<RangeType>
  [[nodiscard]] constexpr auto operator()(RangeType&& range) const {
    using value_type = std::ranges::range_value_t<RangeType>;
    auto view = std::views::all(std::forward<RangeType>(range));
    const auto count = static_cast<std::size_t>(std::ranges::distance(view));
    return std::views::iota(std::size_t{0}, (count + Extent - 1) / Extent) |
           std::views::transform([view](std::size_t group) mutable {
             auto part = view |
                         std::views::drop(static_cast<std::ptrdiff_t>(
                             group * Extent)) |
                         std::views::take(static_cast<std::ptrdiff_t>(Extent));
             static_chunk_value<value_type, Extent> result;
             result.size =
                 static_cast<std::size_t>(std::ranges::distance(part));
             std::ranges::copy(part, result.values.begin());
             return result;
           });
  }

};

 template <std::size_t Extent>
inline constexpr static_chunk_adaptor<Extent> static_chunk;

template <std::size_t Extent>
struct to_array_adaptor
    : std::ranges::range_adaptor_closure<to_array_adaptor<Extent>> {
  template <std::ranges::viewable_range RangeType>
    requires std::ranges::forward_range<RangeType>
  [[nodiscard]] constexpr auto operator()(RangeType&& range) const {
    using value_type = std::ranges::range_value_t<RangeType>;
    if (std::ranges::distance(range) != static_cast<std::ptrdiff_t>(Extent)) {
      throw "range size does not match std::array extent";
    }
    std::array<value_type, Extent> result{};
    std::ranges::copy(range, result.begin());
    return result;
  }
};

 template <std::size_t Extent>
inline constexpr to_array_adaptor<Extent> to_array;

}  // namespace views

// Characters as they arrive, handed on in pieces that lie in a row.
//
// A subject that can only be read once is read a character at a time, which
// costs the vectors: a walk cannot step over a run of thirty letters in one
// instruction when it is handed them one at a time. This gathers them into
// room said in advance and hands over that room, so the reading is by pieces
// -- and a piece is characters in a row, which is what the fast walk wants.
//
// It is a separate thing rather than something `scan` does, because it is not
// always the right trade. It costs a copy of every character and a buffer that
// has to be somewhere; it wins where the fields are long enough for the walk
// to step over them, and loses where they are a few characters each and the
// copy is the whole of the work.
//
// Two rooms, used in turn. The walk keeps an address inside the piece it is
// holding -- where a record ended, so that the next one starts there -- and
// asks for the next piece before it is done with that. So the piece handed
// over before this one is still where it was, and only the one before that is
// written over.
template <std::size_t Room, class RangeType>
class piece_view : public std::ranges::view_interface<
                       piece_view<Room, RangeType>> {
 public:
  static_assert(Room != 0, "a piece has to have room for something");

  constexpr explicit piece_view(RangeType input) : input_(std::move(input)) {}

  piece_view(piece_view&&) = default;
  piece_view& operator=(piece_view&&) = default;
  piece_view(const piece_view&) = delete;
  piece_view& operator=(const piece_view&) = delete;

  class iterator {
   public:
    using value_type = std::string_view;
    using difference_type = std::ptrdiff_t;
    using iterator_concept = std::input_iterator_tag;

    constexpr iterator() = default;
    constexpr explicit iterator(piece_view& owner) : owner_(&owner) {}

    [[nodiscard]] constexpr std::string_view operator*() const {
      return owner_->piece_;
    }
    constexpr iterator& operator++() {
      owner_->fill();
      return *this;
    }
    constexpr void operator++(int) { ++*this; }
    [[nodiscard]] constexpr bool operator==(std::default_sentinel_t) const {
      return owner_ == nullptr || owner_->piece_.empty();
    }

   private:
    piece_view* owner_ = nullptr;
  };

  [[nodiscard]] constexpr iterator begin() {
    fill();
    return iterator(*this);
  }
  [[nodiscard]] constexpr std::default_sentinel_t end() const { return {}; }

 private:
  constexpr void fill() {
    if (!cursor_) cursor_.emplace(std::ranges::begin(input_));
    which_ = 1 - which_;
    auto& into = rooms_[which_];
    std::size_t taken = 0;
    while (taken < Room && *cursor_ != std::ranges::end(input_)) {
      into[taken++] = **cursor_;
      ++*cursor_;
    }
    piece_ = std::string_view(into.data(), taken);
  }

  RangeType input_;
  std::optional<std::ranges::iterator_t<RangeType>> cursor_;
  std::array<std::array<char, Room>, 2> rooms_{};
  std::size_t which_ = 0;
  std::string_view piece_;
};

template <std::size_t Room>
struct in_pieces_adaptor
    : std::ranges::range_adaptor_closure<in_pieces_adaptor<Room>> {
  template <std::ranges::input_range RangeType>
    requires std::same_as<std::ranges::range_value_t<RangeType>, char>
  [[nodiscard]] constexpr auto operator()(RangeType&& input) const {
    auto view = std::views::all(std::forward<RangeType>(input));
    return piece_view<Room, decltype(view)>(std::move(view));
  }
};

// `source | scan::in_pieces<512>` -- the same characters, handed over in
// pieces of that size.
 template <std::size_t Room = 512>
inline constexpr in_pieces_adaptor<Room> in_pieces{};

struct format_details {
  std::string_view name;
  std::optional<std::string_view> parameters;

  [[nodiscard]] static constexpr format_details parse(std::string_view text) {
    const auto to_string_view = [](auto&& part) {
      const auto size = static_cast<std::size_t>(std::ranges::distance(part));
      return std::string_view(size == 0 ? &text_sentinel_ :
                                         std::to_address(part.begin()),
                              size);
    };
    format_details result{};
    for (const auto& chunk : text | std::views::split(':') |
            std::views::transform(to_string_view) | views::static_chunk<2> |
            std::views::take(1)) {
          const auto [name, parameters] = chunk.as_ptr_tuple();
          result = {.name = *name,
                    .parameters =
                        parameters
                            ? std::optional<std::string_view>(*parameters)
                            : std::nullopt};
        }
    return result;
  }

 private:
  static inline constexpr char text_sentinel_ = '\0';
};

}  // namespace scan
