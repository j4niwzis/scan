// An allocator said as a context, and nothing written for it.
//
// A place whose type keeps its own resource -- std::pmr::string here -- is
// begun with the allocator its place was told about, whether that was said as
// a pack or in braces. Nobody writes a scanner for this: the library asks what
// the context keeps, and a resource is a thing answered at runtime, so a
// context said in braces answers it through the same door everything else
// crosses. Told nothing, the place is on the default resource, which is where
// std::pmr::string would have been anyway.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

using namespace std::string_view_literals;

struct two {
  std::pmr::string name;
  std::pmr::string tail;
};

class a_context_that_is_an_allocator : public ::testing::Test {
 protected:
  std::pmr::monotonic_buffer_resource bytes;
  std::pmr::polymorphic_allocator<> mine{&bytes};
};

TEST_F(a_context_that_is_an_allocator, SaidAsAPackItReachesEveryPlace) {
  const auto got = scan::scan<"{[a-z]+} {[a-z]+}">("abc def"sv).of<two>(mine);
  EXPECT_EQ(got.name, "abc");
  EXPECT_EQ(got.tail, "def");
  EXPECT_EQ(got.name.get_allocator().resource(), &bytes);
  EXPECT_EQ(got.tail.get_allocator().resource(), &bytes);
}

TEST_F(a_context_that_is_an_allocator, SaidInBracesItReachesThePlaceItWasSaidAt) {
  const auto got =
      scan::scan<"{[a-z]+} {[a-z]+}">("abc def"sv).of<two>({mine, mine});
  EXPECT_EQ(got.name.get_allocator().resource(), &bytes);
  EXPECT_EQ(got.tail.get_allocator().resource(), &bytes);
}

TEST_F(a_context_that_is_an_allocator, ToldNothingItIsOnTheDefaultResource) {
  const auto got = scan::scan<"{[a-z]+} {[a-z]+}">("abc def"sv).of<two>();
  EXPECT_EQ(got.name, "abc");
  EXPECT_NE(got.name.get_allocator().resource(), &bytes);
}

}  // namespace
