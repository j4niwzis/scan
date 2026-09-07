// A type gathered by its own groups, a character at a time.
//
// `from_groups` hands the groups over when the match is done, which wants a
// subject that can still be pointed at. This is the other way round: as each
// character arrives, the type is told which of its own groups it belongs to.
// So a type with parts can be read off a subject that will never be seen
// again, and no text is put together anywhere -- and the same type reads the
// same way where the subject is in memory.
//
// Which of the three ways the type is told is its own business: the name of
// the group, the group as a variant, or its number.
import std;
import scan;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct version {
  int major = 0;
  int minor = 0;
  int patch = 0;
};

struct span_of_two {
  int from = 0;
  int to = 0;
};

struct pair_of_words {
  std::string first;
  std::string second;
};

}  // namespace

// Told by the name of the group: an overload each, and no switch anywhere.
template <>
struct scan::scanner<version> {
  static constexpr std::string_view pattern() {
    return "([0-9]+)\\.([0-9]+)\\.([0-9]+)";
  }

  struct major_part {};
  struct minor_part {};
  struct patch_part {};
  using group = std::variant<major_part, minor_part, patch_part>;

  struct state {
    version made;
  };

  static constexpr state begin_groups() { return {}; }
  static constexpr void push_group(state& into, major_part, char letter) {
    into.made.major = into.made.major * 10 + (letter - '0');
  }
  static constexpr void push_group(state& into, minor_part, char letter) {
    into.made.minor = into.made.minor * 10 + (letter - '0');
  }
  static constexpr void push_group(state& into, patch_part, char letter) {
    into.made.patch = into.made.patch * 10 + (letter - '0');
  }
  static constexpr version finish_groups(state from) { return from.made; }
};

// Told by the number.
template <>
struct scan::scanner<span_of_two> {
  static constexpr std::string_view pattern() { return "([0-9]+)-([0-9]+)"; }

  struct state {
    int part[2]{};
  };

  static constexpr state begin_groups() { return {}; }
  static constexpr void push_group(state& into, std::size_t which,
                                   char letter) {
    into.part[which] = into.part[which] * 10 + (letter - '0');
  }
  static constexpr span_of_two finish_groups(state from) {
    return span_of_two{from.part[0], from.part[1]};
  }
};

// Told with the variant itself, and answering with a visit.
template <>
struct scan::scanner<pair_of_words> {
  static constexpr std::string_view pattern() {
    return "([a-z]+),([a-z]+)";
  }

  struct left {};
  struct right {};
  using group = std::variant<left, right>;

  struct state {
    pair_of_words made;
  };

  static constexpr state begin_groups() { return {}; }
  static constexpr void push_group(state& into, group which, char letter) {
    std::visit(
        [&](auto one) {
          if constexpr (std::same_as<decltype(one), left>) {
            into.made.first.push_back(letter);
          } else {
            into.made.second.push_back(letter);
          }
        },
        which);
  }
  static constexpr pair_of_words finish_groups(state from) {
    return std::move(from.made);
  }
};

namespace {

// Characters one at a time and never again.
class read_once {
 public:
  class cursor {
   public:
    using iterator_concept = std::input_iterator_tag;
    using value_type = char;
    using difference_type = std::ptrdiff_t;

    cursor() = default;
    cursor(std::string_view text, std::size_t* at) : text_(text), at_(at) {}

    [[nodiscard]] char operator*() const { return text_[*at_]; }
    cursor& operator++() {
      ++*at_;
      return *this;
    }
    void operator++(int) { ++*this; }
    [[nodiscard]] bool operator==(std::default_sentinel_t) const {
      return *at_ == text_.size();
    }

   private:
    std::string_view text_;
    std::size_t* at_ = nullptr;
  };

  read_once(std::string_view text, std::size_t* at) : text_(text), at_(at) {}
  [[nodiscard]] cursor begin() const { return cursor(text_, at_); }
  [[nodiscard]] std::default_sentinel_t end() const { return {}; }

 private:
  std::string_view text_;
  std::size_t* at_ = nullptr;
};


TEST(ScannerByGroup, AndTheGroupAfterItIsTheNextCollectors) {
  // The type took the three groups it wrote, so the group after them is the
  // second collector's -- nobody has to count them out by hand.
  const std::string text = "v=1.22.333-stable!";
  const auto found =
      scan::match<"v=(([0-9]+)\\.([0-9]+)\\.([0-9]+))-([a-z]+)!">.into(
          scan::as<version>(), scan::text())(text);
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.get<1>().minor, 22);
  EXPECT_EQ(found.get<2>(), "stable"sv);
}

TEST(ScannerByGroup, ToldByTheNameOfTheGroup) {
  const std::string text = "v=1.22.333!";
  const auto here =
      scan::match<"v=(([0-9]+)\\.([0-9]+)\\.([0-9]+))!">.into(
          scan::as<version>())(text);
  ASSERT_TRUE(static_cast<bool>(here));
  EXPECT_EQ(here.get<1>().major, 1);
  EXPECT_EQ(here.get<1>().minor, 22);
  EXPECT_EQ(here.get<1>().patch, 333);
}

TEST(ScannerByGroup, AndTheSameOffASubjectReadOnce) {
  const std::string text = "v=1.22.333!";
  std::size_t at = 0;
  const auto arriving =
      scan::match<"v=(([0-9]+)\\.([0-9]+)\\.([0-9]+))!">.into(
          scan::as<version>())(read_once(text, &at));
  ASSERT_TRUE(static_cast<bool>(arriving));
  EXPECT_EQ(arriving.get<1>().major, 1);
  EXPECT_EQ(arriving.get<1>().minor, 22);
  EXPECT_EQ(arriving.get<1>().patch, 333);
}

TEST(ScannerByGroup, ToldByTheNumber) {
  const std::string text = "[12-34]";
  std::size_t at = 0;
  const auto found = scan::match<"\\[(([0-9]+)-([0-9]+))\\]">.into(
      scan::as<span_of_two>())(read_once(text, &at));
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.get<1>().from, 12);
  EXPECT_EQ(found.get<1>().to, 34);
}

TEST(ScannerByGroup, ToldWithTheVariantItself) {
  const std::string text = "<alpha,bravo>";
  std::size_t at = 0;
  const auto found = scan::match<"<(([a-z]+),([a-z]+))>">.into(
      scan::as<pair_of_words>())(read_once(text, &at));
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ(found.get<1>().first, "alpha");
  EXPECT_EQ(found.get<1>().second, "bravo");
}

}  // namespace
