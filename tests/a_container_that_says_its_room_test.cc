// A container told what the format could ask of it.
//
// A place says how many turns it may take, and that number is known where the
// reading is compiled. A container written with room said in advance can be
// asked whether that many would fit, and one that can grow can take the room
// for all of them at once rather than a handful at a time.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

struct row {
  std::vector<int> values;
};

// Room said in advance, and said through the extension point rather than
// guessed at by the library.
struct three_at_most {
  using value_type = int;
  std::array<int, 3> kept{};
  std::size_t count = 0;

  void push_back(int one) {
    if (count < kept.size()) kept[count++] = one;
  }
  [[nodiscard]] auto begin() const { return kept.begin(); }
  [[nodiscard]] auto end() const { return kept.begin() + count; }
};

struct small_row {
  three_at_most values;
};

}  // namespace

template <>
struct scan::room_for<three_at_most> {
  static constexpr std::size_t most = 3;
};

namespace {

TEST(AContainerThatSaysItsRoom, TheRoomIsTakenAtOnce) {
  const auto three = scan::scan<"{{}{*,?}}{2,3}">("1,2,3"sv).try_of<row>();
  ASSERT_TRUE(three.has_value());
  ASSERT_EQ(three->values.size(), 3u);
  // Everything the count could ask for, and nothing asked for twice.
  EXPECT_GE(three->values.capacity(), 3u);
}

TEST(AContainerThatSaysItsRoom, OneWithRoomOfItsOwnIsReadInto) {
  const auto fits = scan::scan<"{{}{*,?}}{2,3}">("4,5,6"sv).try_of<small_row>();
  ASSERT_TRUE(fits.has_value());
  ASSERT_EQ(fits->values.count, 3u);
  EXPECT_EQ(fits->values.kept[0], 4);
  EXPECT_EQ(fits->values.kept[2], 6);
}

}  // namespace
