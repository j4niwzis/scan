export module scan.views;

import std;
export import scan.core;

export namespace scan {

namespace views {

template <class type, std::size_t extent>
struct static_chunk_value {
  std::array<type, extent> values{};
  std::size_t size = 0;

  template <std::size_t... index>
  [[nodiscard]] constexpr auto as_ptr_tuple(std::index_sequence<index...>) const {
    return std::tuple{(index < size ? std::addressof(values[index]) : nullptr)...};
  }

  [[nodiscard]] constexpr auto as_ptr_tuple() const {
    return as_ptr_tuple(std::make_index_sequence<extent>{});
  }
};

template <std::size_t extent>
struct static_chunk_adaptor
    : std::ranges::range_adaptor_closure<static_chunk_adaptor<extent>> {
  template <std::ranges::viewable_range range_type>
  [[nodiscard]] constexpr auto operator()(range_type&& range) const {
    using value_type = std::ranges::range_value_t<range_type>;
    return std::forward<range_type>(range) | std::views::chunk(extent) |
           std::views::transform([](auto chunk) {
             static_chunk_value<value_type, extent> result;
             result.size = static_cast<std::size_t>(
                 std::ranges::distance(chunk));
             std::ranges::copy(chunk, result.values.begin());
             return result;
           });
  }

};

template <std::size_t extent>
inline constexpr static_chunk_adaptor<extent> static_chunk;

template <std::size_t extent>
struct to_array_adaptor
    : std::ranges::range_adaptor_closure<to_array_adaptor<extent>> {
  template <std::ranges::viewable_range range_type>
    requires std::ranges::forward_range<range_type>
  [[nodiscard]] constexpr auto operator()(range_type&& range) const {
    using value_type = std::ranges::range_value_t<range_type>;
    if (std::ranges::distance(range) != static_cast<std::ptrdiff_t>(extent)) {
      throw "range size does not match std::array extent";
    }
    std::array<value_type, extent> result{};
    std::ranges::copy(range, result.begin());
    return result;
  }
};

template <std::size_t extent>
inline constexpr to_array_adaptor<extent> to_array;

}  // namespace views

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
    std::ranges::for_each(
        text | std::views::split(':') |
            std::views::transform(to_string_view) | views::static_chunk<2> |
            std::views::take(1),
        [&](const auto& chunk) {
          const auto [name, parameters] = chunk.as_ptr_tuple();
          result = {.name = *name,
                    .parameters =
                        parameters
                            ? std::optional<std::string_view>(*parameters)
                            : std::nullopt};
        });
    return result;
  }

 private:
  static inline constexpr char text_sentinel_ = '\0';
};

}  // namespace scan
