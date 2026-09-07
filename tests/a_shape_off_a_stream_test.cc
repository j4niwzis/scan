// A type with parts, read off a subject that can only be read once.
//
// There are no group texts on such a reading: the characters are gone as they
// arrive. What takes their place is the format -- a type that declares one has
// its places spread into the one automaton, so the machine knows which of that
// type's places is open at each character and hands the character to that
// place's own scanner. Nothing is put together as text and read again.
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

}  // namespace

template <>
struct scan::scanner<version> : scan::aggregate_scanner<"{}.{}.{}"> {};

namespace {

struct release {
  version number;
  scan::held<16> name;
};

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


TEST(AShapeOffAStream, TheFieldsGatherSeparately) {
  const std::string text = "1.22.333 stable";
  std::size_t at = 0;
  const release one =
      scan::scan<"{} {[a-z]+}">(read_once(text, &at)).of<release>();
  EXPECT_EQ(one.number.major, 1);
  EXPECT_EQ(one.number.minor, 22);
  EXPECT_EQ(one.number.patch, 333);
  EXPECT_EQ(one.name.view(), "stable");
}

TEST(AShapeOffAStream, OneRecordAfterAnother) {
  const std::string text = "1.2.3 alpha\n4.5.6 beta\n";
  std::size_t at = 0;
  std::vector<int> majors;
  std::vector<std::string> names;
  for (const release& one :
       scan::each<"{} {[a-z]+}\n">(read_once(text, &at)).of<release>()) {
    majors.push_back(one.number.major);
    names.push_back(std::string(one.name.view()));
  }
  EXPECT_EQ(majors, std::vector<int>({1, 4}));
  EXPECT_EQ(names, std::vector<std::string>({"alpha", "beta"}));
}

}  // namespace
