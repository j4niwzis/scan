// What a value is made with belongs to that value, so each group says it for
// itself.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

TEST(CollectorArguments, AnAllocatorOfItsOwn) {
  std::array<std::byte, 512> room{};
  std::pmr::monotonic_buffer_resource pool(room.data(), room.size());
  const std::string text = "42-abc";
  const auto found = scan::match<"([0-9]+)-([a-z]+)">.into(
      scan::skip(), scan::as<std::pmr::string>(&pool))(text);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.get<2>(), "abc");
  EXPECT_EQ(found.get<2>().get_allocator().resource(), &pool);
}

TEST(CollectorArguments, ASkippedGroupIsNothing) {
  const std::string text = "42-abc";
  const auto found = scan::match<"([0-9]+)-([a-z]+)">.into(
      scan::skip(), scan::as<std::string>())(text);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_TRUE((std::is_same_v<std::remove_cvref_t<decltype(found.get<1>())>,
                              scan::skipped>));
  EXPECT_EQ(found.get<2>(), "abc");
}

}  // namespace
