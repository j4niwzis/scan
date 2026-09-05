// The subjects the benchmarks match, as a module: a benchmark that includes
// nothing textually cannot take them from a header.
export module bench.inputs;

import std;

export namespace bench {

// Matching is anchored and consumes the whole input, so each of these is an
// exact subject for the pattern of the benchmark that uses it.
inline constexpr std::string_view word =
    "thequickbrownfoxjumpsoverthelazydogandkeepsrunningpastthefence";
inline constexpr std::string_view csv = "alpha,bravo,charlie,delta,echo";
inline constexpr std::string_view address = "first.last@subdomain.example.com";
inline constexpr std::string_view timestamp = "2026-09-05T04:20:59";

// A subject long enough that the work outweighs the loop around it. Sixty
// bytes are matched in about twenty nanoseconds, which is also what one
// iteration of the harness costs, so a short subject measures the harness.
inline const std::string& long_word(std::size_t length = 4096) {
  static std::string storage;
  if (storage.size() != length) {
    storage.clear();
    storage.reserve(length);
    while (storage.size() < length) {
      storage.push_back(static_cast<char>('a' + (storage.size() % 26)));
    }
  }
  return storage;
}

// The same five fields, long enough that what the loop does to a character is
// what is being measured. Thirty bytes are read in under ten nanoseconds, which
// is the same order as everything that surrounds a match -- entering it,
// cutting the fields out of it, handing them back -- so a short subject prices
// all of that together. A thousand bytes prices the loop.
inline const std::string& long_csv(std::size_t field_length = 200) {
  static std::string storage;
  const std::size_t wanted = field_length * 5 + 4;
  if (storage.size() != wanted) {
    storage.clear();
    for (std::size_t field = 0; field < 5; ++field) {
      if (field) storage.push_back(',');
      storage.append(field_length, static_cast<char>('a' + field));
    }
  }
  return storage;
}

// Copies of the same subject at different addresses, so that a batch of calls
// in one iteration cannot be answered once and reused.
//
// Kept per subject and not only per count: two rows in one file asking for the
// same number of copies of different text would otherwise be handed whichever
// text asked first, and would measure it instead of their own.
inline const std::vector<std::string>& copies_of(std::string_view text,
                                                 std::size_t count) {
  static std::map<std::pair<std::string, std::size_t>, std::vector<std::string>>
      storage;
  auto key = std::pair(std::string(text), count);
  const auto position = storage.find(key);
  if (position != storage.end()) return position->second;
  return storage.emplace(std::move(key),
                         std::vector<std::string>(count, std::string(text)))
      .first->second;
}

}  // namespace bench
