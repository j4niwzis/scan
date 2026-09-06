// A range of the user's own making, with no allocator anywhere, read at
// compile time as well as at run time.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct point { int x; int y; };

template <class item, std::size_t room>
struct small_list {
  using value_type = item;
  std::array<item, room> items{};
  std::size_t count = 0;
  constexpr void push_back(item value) {
    if (count < room) items[count++] = value;
  }
  [[nodiscard]] constexpr auto begin() const { return items.begin(); }
  [[nodiscard]] constexpr auto end() const { return items.begin() + count; }
  [[nodiscard]] constexpr std::size_t size() const { return count; }
};

struct path { small_list<point, 4> points; };

constexpr std::size_t how_many(std::string_view text) {
  const path value = scan::scan<"{{}:{}{* ?}}+">(text);
  return value.points.size();
}

static_assert(how_many("1:2") == 1);
static_assert(how_many("1:2 3:4") == 2);

TEST(ListOwnTest, AListOfTheUsersOwn) {
  const std::string text = "1:2 3:4";
  const path value = scan::scan<"{{}:{}{* ?}}+">(text);
  ASSERT_EQ(value.points.size(), 2u);
  EXPECT_EQ(value.points.items[1].x, 3);
  EXPECT_EQ(value.points.items[1].y, 4);
}

}  // namespace
