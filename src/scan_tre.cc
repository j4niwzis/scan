export module scan.tre;

import std;

export namespace scan::tre {

// A set of symbols as four words rather than two hundred and fifty-six bools.
//
// It is the same information, but the constant evaluator counts objects, not
// bytes: an array of two hundred and fifty-six bools is two hundred and fifty
// six tracked objects, copied one at a time whenever a transition is copied --
// and transitions are copied on every vector growth in the determinisation.
// Four words are four objects.
struct symbol_set {
  std::array<std::uint64_t, 4> words{};

  [[nodiscard]] constexpr bool test(std::size_t symbol) const {
    return (words[symbol >> 6] >> (symbol & 63)) & 1;
  }
  constexpr void set(std::size_t symbol) {
    words[symbol >> 6] |= std::uint64_t{1} << (symbol & 63);
  }
  constexpr void merge(const symbol_set& other) {
    for (std::size_t index = 0; index < words.size(); ++index) {
      words[index] |= other.words[index];
    }
  }
  [[nodiscard]] constexpr bool any() const {
    return words[0] != 0 || words[1] != 0 || words[2] != 0 || words[3] != 0;
  }
  [[nodiscard]] constexpr bool operator==(const symbol_set&) const = default;

  [[nodiscard]] static constexpr symbol_set from_bools(
      const std::array<bool, 256>& symbols) {
    symbol_set result;
    for (std::size_t symbol = 0; symbol < 256; ++symbol) {
      if (symbols[symbol]) result.set(symbol);
    }
    return result;
  }
};

using state_id = std::uint32_t;
using tag_id = std::uint32_t;
inline constexpr std::size_t unbounded =
    std::numeric_limits<std::size_t>::max();
inline constexpr std::ptrdiff_t negative_tag = -1;

class node;

namespace ast {

struct empty {};
struct epsilon {};
struct symbol {
  char value = 0;
};
struct tag {
  tag_id id = 0;
};
struct character_class {
  symbol_set symbols{};
};
struct alternative {
  std::vector<node> branches;
};
struct concatenation {
  std::vector<node> elements;
};
struct repetition {
  std::vector<node> element;
  std::size_t minimum = 0;
  std::size_t maximum = unbounded;
  // Whether another turn is preferred to stopping.
  //
  // Both are always possible; this says which is tried first, and the order of
  // the two is the whole of the difference between `a*` and `a*?`. It decides
  // which parse wins where several are possible, and never whether anything
  // matches at all.
  bool greedy = true;
};

using node_variant =
    std::variant<empty, epsilon, symbol, tag, character_class, alternative,
                 concatenation, repetition>;

}  // namespace ast

class node : public ast::node_variant {
 public:
  using ast::node_variant::node_variant;
  using ast::node_variant::operator=;
};

[[nodiscard]] constexpr node empty() { return ast::empty{}; }
[[nodiscard]] constexpr node epsilon() { return ast::epsilon{}; }
[[nodiscard]] constexpr node symbol(char value) { return ast::symbol{value}; }
[[nodiscard]] constexpr node tag(tag_id id) { return ast::tag{id}; }
[[nodiscard]] constexpr node character_class(symbol_set symbols) {
  return ast::character_class{symbols};
}

[[nodiscard]] constexpr node character_class(
    const std::array<bool, 256>& symbols) {
  return ast::character_class{symbol_set::from_bools(symbols)};
}
[[nodiscard]] constexpr node alt(std::vector<node> branches) {
  return ast::alternative{std::move(branches)};
}
[[nodiscard]] constexpr node cat(std::vector<node> elements) {
  return ast::concatenation{std::move(elements)};
}
[[nodiscard]] constexpr node repeat(node element, std::size_t minimum,
                                    std::size_t maximum = unbounded,
                                    bool greedy = true) {
  return ast::repetition{{std::move(element)}, minimum, maximum, greedy};
}
[[nodiscard]] constexpr node star(node element, bool greedy = true) {
  return repeat(std::move(element), 0, unbounded, greedy);
}
[[nodiscard]] constexpr node plus(node element, bool greedy = true) {
  return repeat(std::move(element), 1, unbounded, greedy);
}
[[nodiscard]] constexpr node optional(node element, bool greedy = true) {
  return repeat(std::move(element), 0, 1, greedy);
}

enum class transition_kind : std::uint8_t {
  epsilon,
  symbol,
  character_class,
  tag
};

struct transition {
  state_id target = 0;
  transition_kind kind = transition_kind::epsilon;
  char symbol = 0;
  symbol_set symbols{};
  tag_id tag = 0;
  bool negative = false;
  // Lower values have higher priority. Priority zero is the greedy branch.
  std::uint32_t priority = 0;
};

struct tnfa {
  state_id initial = 0;
  state_id final = 0;
  std::size_t tag_count = 0;
  std::vector<std::vector<transition>> transitions;
};

[[nodiscard]] constexpr tnfa compile_tnfa(const node& expression);

using tag_history = std::vector<std::ptrdiff_t>;

struct match {
  bool matched = false;
  std::vector<tag_history> tags;
};

// Runs an anchored, whole-input match with leftmost-greedy disambiguation.
template <std::ranges::input_range range_type>
  requires std::same_as<std::ranges::range_value_t<range_type>, char>
[[nodiscard]] constexpr match simulate(const tnfa& automaton,
                                       range_type&& input);

template <std::size_t extent>
[[nodiscard]] constexpr match simulate(const tnfa& automaton,
                                       const char (&input)[extent]) {
  return simulate(automaton, std::string_view(input, extent - 1));
}

// The values a command appends, and there are a handful of them at most: a bit
// each, in one word, so a command is a fixed-size object. As a vector it was
// an allocation per command -- and a command is made for every slot and every
// tag of every transition, nearly all of them empty.
struct tag_values {
  std::uint32_t bits = 0;
  std::uint8_t size = 0;

  constexpr void push_back(bool value) {
    if (size == 32) throw "a command appends more values than a word holds";
    if (value) bits |= std::uint32_t{1} << size;
    ++size;
  }
  [[nodiscard]] constexpr bool empty() const { return size == 0; }
  [[nodiscard]] constexpr bool back() const {
    return ((bits >> (size - 1)) & 1) != 0;
  }
  [[nodiscard]] constexpr bool operator==(const tag_values&) const = default;

  struct iterator {
    const tag_values* owner = nullptr;
    std::uint8_t index = 0;
    [[nodiscard]] constexpr bool operator*() const {
      return ((owner->bits >> index) & 1) != 0;
    }
    constexpr iterator& operator++() { ++index; return *this; }
    [[nodiscard]] constexpr bool operator==(const iterator&) const = default;
  };
  [[nodiscard]] constexpr iterator begin() const { return {this, 0}; }
  [[nodiscard]] constexpr iterator end() const { return {this, size}; }
};

struct register_command {
  std::size_t destination = 0;
  std::optional<std::size_t> source;
  // Values are appended in order; true means current input position.
  tag_values values;
};

struct tdfa_transition {
  symbol_set symbols{};
  std::size_t target = 0;
  std::vector<register_command> commands;
};

struct tdfa_state {
  std::vector<state_id> nfa_states;
  std::vector<tdfa_transition> transitions;
  std::optional<std::size_t> accepting_slot;
  std::vector<register_command> final_commands;
  // Which register holds which tag, for each of the readings this state is
  // standing in at once. A deterministic tagged machine is in one state and
  // several readings of the input, and the registers are how the readings are
  // kept apart. Anything that has to follow a reading -- rather than take the
  // positions out at the end -- needs to be told which registers make it up,
  // and this is where it is told. Indexed the way the accepting slot is.
  std::vector<std::vector<std::uint32_t>> readings;
};

struct tdfa {
  std::size_t initial = 0;
  std::size_t tag_count = 0;
  std::size_t register_count = 0;
  std::vector<register_command> initialize;
  std::vector<tdfa_state> states;
  // Which tag each register holds. Every register is made for one tag and never
  // holds another, and renaming keeps that true; guessing it from arithmetic on
  // the number was a thing that happened to work under one way of handing them
  // out.
  std::vector<std::uint32_t> register_tag;
};

// Builds a deterministic tagged transducer. epsilon actions after a symbol are
// delayed to that symbol's transition, which is the one-symbol lookahead form.
// How large a deterministic machine this will build before it gives up.
//
// Determinizing can cost exponentially more states than the expression has
// symbols, and the expressions that do it are ordinary: anything that reads
// freely and then counts, `.*a.{20}` and its like, doubles with every
// character of the tail. Every engine has to answer this somehow -- RE2
// answers it by simulating rather than determinizing and keeping a bounded
// cache of the states it has met.
//
// This library is compiled, so the failure is not a slow program but a
// compilation nobody waits for, ended by the machine running out of room. A
// number here turns that into a sentence saying which pattern did it.
inline constexpr std::size_t most_states = 20000;

[[nodiscard]] constexpr tdfa compile_tdfa(const tnfa& automaton,
                                          bool cut_at_match = true);
// Applies TDFA register liveness, dead-store elimination, copy cleanup, and
// local transition normalization.
[[nodiscard]] constexpr tdfa optimize_tdfa(tdfa automaton,
                                            bool allocate_registers = true);
[[nodiscard]] constexpr match simulate(const tdfa& automaton,
                                       std::string_view input);

}  // namespace scan::tre

namespace scan::tre {
// Not exported, and not in an unnamed namespace either: an entity there is
// local to the translation unit, and naming one in the body of an exported
// inline function exposes it -- which the standard forbids and clang warns
// about. Without `export` these have module linkage instead: an importer
// still cannot name them, and the interface may refer to them.

struct fragment {
  state_id start;
  state_id end;
};

class tnfa_builder {
 public:
  [[nodiscard]] constexpr tnfa build(const node& expression) {
    const fragment fragment = visit(expression);
    automaton_.initial = fragment.start;
    automaton_.final = fragment.end;
    return std::move(automaton_);
  }

 private:
  [[nodiscard]] constexpr state_id new_state() {
    automaton_.transitions.emplace_back();
    return static_cast<state_id>(automaton_.transitions.size() - 1);
  }

  constexpr void add(state_id from, transition transition) {
    automaton_.transitions[from].push_back(transition);
  }

  constexpr void add_negative_chain(state_id from, state_id to,
                                  const std::vector<tag_id>& tags,
                                  std::uint32_t priority = 0) {
    state_id tail = from;
    for (tag_id tag : tags) {
      const state_id next = new_state();
      add(tail, {.target = next,
                 .kind = transition_kind::tag,
                 .tag = tag,
                 .negative = true,
                 .priority = priority});
      tail = next;
      priority = 0;
    }
    add(tail, {.target = to,
               .kind = transition_kind::epsilon,
               .priority = priority});
  }

  [[nodiscard]] constexpr std::vector<tag_id> tags(const node& expression) const {
    std::vector<tag_id> result;
    std::visit(
        [&](const auto& value) {
          using value_type = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<value_type, ast::tag>) {
            result.push_back(value.id);
          } else if constexpr (std::is_same_v<value_type, ast::alternative>) {
            for (const node& child : value.branches) {
              const auto nested = tags(child);
              result.insert(result.end(), nested.begin(), nested.end());
            }
          } else if constexpr (std::is_same_v<value_type, ast::concatenation>) {
            for (const node& child : value.elements) {
              const auto nested = tags(child);
              result.insert(result.end(), nested.begin(), nested.end());
            }
          } else if constexpr (std::is_same_v<value_type, ast::repetition>) {
            if (!value.element.empty()) {
              result = tags(value.element.front());
            }
          }
        },
        static_cast<const ast::node_variant&>(expression));
    std::ranges::sort(result);
    result.erase(std::ranges::unique(result).begin(), result.end());
    return result;
  }

  [[nodiscard]] constexpr fragment atomic(transition transition,
                                          bool connected = true) {
    const state_id start = new_state();
    const state_id end = new_state();
    transition.target = end;
    if (connected) add(start, transition);
    return {start, end};
  }

  [[nodiscard]] constexpr fragment visit(const node& expression) {
    return std::visit(
        [this](const auto& value) -> fragment { return build_node(value); },
        static_cast<const ast::node_variant&>(expression));
  }

  [[nodiscard]] constexpr fragment build_node(const ast::empty&) {
    return atomic({}, false);
  }
  [[nodiscard]] constexpr fragment build_node(const ast::epsilon&) {
    return atomic({.kind = transition_kind::epsilon});
  }
  [[nodiscard]] constexpr fragment build_node(const ast::symbol& node) {
    return atomic({.kind = transition_kind::symbol, .symbol = node.value});
  }
  [[nodiscard]] constexpr fragment build_node(const ast::tag& node) {
    automaton_.tag_count = std::max(automaton_.tag_count,
                                    static_cast<std::size_t>(node.id) + 1);
    return atomic({.kind = transition_kind::tag, .tag = node.id});
  }
  [[nodiscard]] constexpr fragment build_node(const ast::character_class& node) {
    return atomic({.kind = transition_kind::character_class,
                   .symbols = node.symbols});
  }
  [[nodiscard]] constexpr fragment build_node(const ast::alternative& alternative) {
    const state_id start = new_state();
    const state_id end = new_state();
    std::vector<tag_id> all_tags;
    for (const node& branch : alternative.branches) {
      const auto branch_tags = tags(branch);
      all_tags.insert(all_tags.end(), branch_tags.begin(), branch_tags.end());
    }
    std::sort(all_tags.begin(), all_tags.end());
    all_tags.erase(std::unique(all_tags.begin(), all_tags.end()), all_tags.end());
    for (std::size_t i = 0; i < alternative.branches.size(); ++i) {
      const fragment branch = visit(alternative.branches[i]);
      add(start, {.target = branch.start,
                  .kind = transition_kind::epsilon,
                  .priority = static_cast<std::uint32_t>(i)});
      const auto present = tags(alternative.branches[i]);
      std::vector<tag_id> missing;
      std::set_difference(all_tags.begin(), all_tags.end(), present.begin(),
                          present.end(), std::back_inserter(missing));
      add_negative_chain(branch.end, end, missing);
    }
    return {start, end};
  }
  [[nodiscard]] constexpr fragment build_node(
      const ast::concatenation& node) {
    if (node.elements.empty()) return build_node(ast::epsilon{});
    fragment result = visit(node.elements.front());
    for (std::size_t i = 1; i < node.elements.size(); ++i) {
      const fragment next = visit(node.elements[i]);
      add(result.end,
          {.target = next.start, .kind = transition_kind::epsilon});
      result.end = next.end;
    }
    return result;
  }
  [[nodiscard]] constexpr fragment build_node(const ast::repetition& node) {
    if (node.element.size() != 1 || node.minimum > node.maximum) {
      throw std::invalid_argument("invalid repetition");
    }
    const state_id start = new_state();
    state_id tail = start;
    for (std::size_t i = 0; i < node.minimum; ++i) {
      const fragment copy = visit(node.element.front());
      add(tail, {.target = copy.start, .kind = transition_kind::epsilon});
      tail = copy.end;
    }
    const state_id end = new_state();
    // Which of the two is tried first. Greedy takes another turn before it
    // stops; lazy stops before it takes another. Both edges are there either
    // way, so this decides which parse wins and never whether one exists.
    const std::uint32_t again = node.greedy ? 0 : 1;
    const std::uint32_t enough = node.greedy ? 1 : 0;
    if (node.maximum == unbounded) {
      const fragment copy = visit(node.element.front());
      add(tail, {.target = copy.start, .kind = transition_kind::epsilon,
                 .priority = again});
      if (node.minimum == 0) {
        add_negative_chain(tail, end, tags(node.element.front()), enough);
      } else {
        add(tail, {.target = end, .kind = transition_kind::epsilon,
                   .priority = enough});
      }
      add(copy.end, {.target = copy.start, .kind = transition_kind::epsilon,
                     .priority = again});
      add(copy.end, {.target = end, .kind = transition_kind::epsilon,
                     .priority = enough});
      return {start, end};
    }
    if (node.minimum == 0) {
      add_negative_chain(tail, end, tags(node.element.front()), enough);
    } else {
      add(tail, {.target = end, .kind = transition_kind::epsilon,
                 .priority = enough});
    }
    for (std::size_t i = node.minimum; i < node.maximum; ++i) {
      const fragment copy = visit(node.element.front());
      add(tail, {.target = copy.start, .kind = transition_kind::epsilon,
                 .priority = again});
      tail = copy.end;
      add(tail, {.target = end, .kind = transition_kind::epsilon,
                 .priority = enough});
    }
    return {start, end};
  }

  tnfa automaton_;
};

struct path {
  state_id state = 0;
  std::vector<std::pair<tag_id, bool>> actions;
  // Which seed this path grew from, so that the closure of a whole set can be
  // taken at once and still say where each path came from.
  std::size_t origin = 0;
};

[[nodiscard]] constexpr std::vector<path> closure(
    const tnfa& automaton, std::span<const path> seeds) {
  std::vector<path> result;
  std::vector<char> seen(automaton.transitions.size());
  std::vector<path> work(seeds.rbegin(), seeds.rend());
  while (!work.empty()) {
    path current = std::move(work.back());
    work.pop_back();
    if (seen[current.state]) continue;
    seen[current.state] = true;
    result.push_back(current);
    // The edges are ordered by priority without copying them: a transition
    // carries a symbol set and a copy of it is an object the constant
    // evaluator tracks, where an index is a number.
    const std::vector<transition>& outgoing = automaton.transitions[current.state];
    std::vector<std::uint32_t> order;
    for (std::size_t index = 0; index < outgoing.size(); ++index) {
      const transition& edge = outgoing[index];
      if (edge.kind != transition_kind::symbol &&
          edge.kind != transition_kind::character_class) {
        order.push_back(index);
      }
    }
    std::ranges::sort(order, [&](std::size_t lhs, std::size_t rhs) {
      return outgoing[lhs].priority < outgoing[rhs].priority;
    });
    // Lowest priority is pushed first, so the greedy branch is on top of the
    // stack. The last one to be pushed is the one that may take the path it
    // was built from rather than copy its actions.
    for (std::size_t position = order.size(); position-- > 0;) {
      const transition& edge = outgoing[order[position]];
      path next = position == 0 ? std::move(current) : current;
      next.state = edge.target;
      if (edge.kind == transition_kind::tag) {
        next.actions.emplace_back(edge.tag, edge.negative);
      }
      work.push_back(std::move(next));
    }
  }
  return result;
}

[[nodiscard]] constexpr std::size_t register_index(std::size_t slot, tag_id tag,
                                             std::size_t tag_count) {
  return slot * tag_count + tag;
}



constexpr tnfa compile_tnfa(const node& expression) {
  return tnfa_builder{}.build(expression);
}

template <std::ranges::input_range range_type>
  requires std::same_as<std::ranges::range_value_t<range_type>, char>
constexpr match simulate(const tnfa& automaton, range_type&& input) {
  struct configuration {
    state_id state = 0;
    std::vector<tag_history> tags;
  };
  const auto close = [&](std::vector<configuration> seeds,
                         std::size_t position) {
    std::vector<configuration> output;
    std::vector<char> seen(automaton.transitions.size());
    std::vector<configuration> work(seeds.rbegin(), seeds.rend());
    while (!work.empty()) {
      configuration item = std::move(work.back());
      work.pop_back();
      if (seen[item.state]) continue;
      seen[item.state] = true;
      output.push_back(item);
      // Indices, not copies of the transitions: sorting the transitions
      // themselves moves their symbol sets about, and a sort assigns an
      // element to itself -- which the bytecode interpreter reads as a copy
      // between overlapping regions and refuses.
      const std::vector<transition>& outgoing = automaton.transitions[item.state];
      std::vector<std::uint32_t> order;
      for (std::uint32_t index = 0; index < outgoing.size(); ++index) {
        const transition& edge = outgoing[index];
        if (edge.kind != transition_kind::symbol &&
            edge.kind != transition_kind::character_class) {
          order.push_back(index);
        }
      }
      std::ranges::sort(order, [&](std::uint32_t lhs, std::uint32_t rhs) {
        return outgoing[lhs].priority < outgoing[rhs].priority;
      });
      for (std::size_t position_in_order = order.size();
           position_in_order-- > 0;) {
        const transition& edge = outgoing[order[position_in_order]];
        configuration next = item;
        next.state = edge.target;
        if (edge.kind == transition_kind::tag) {
          next.tags[edge.tag].push_back(edge.negative ? negative_tag
                                                      : position);
        }
        work.push_back(std::move(next));
      }
    }
    return output;
  };

  std::vector<configuration> active = close(
      {{automaton.initial, std::vector<tag_history>(automaton.tag_count)}}, 0);
  std::size_t position = 0;
  for (char symbol : input) {
    std::vector<configuration> next;
    for (const auto& item : active) {
      for (const auto& edge : automaton.transitions[item.state]) {
        const bool matches =
            (edge.kind == transition_kind::symbol &&
             edge.symbol == symbol) ||
            (edge.kind == transition_kind::character_class &&
             edge.symbols.test(static_cast<unsigned char>(symbol)));
        if (matches) {
          configuration moved = item;
          moved.state = edge.target;
          next.push_back(std::move(moved));
        }
      }
    }
    active = close(std::move(next), ++position);
  }
  for (auto& item : active) {
    if (item.state == automaton.final) {
      return {.matched = true, .tags = std::move(item.tags)};
    }
  }
  return {.matched = false,
          .tags = std::vector<tag_history>(automaton.tag_count)};
}

constexpr tdfa compile_tdfa(const tnfa& automaton, bool cut_at_match) {
  // Determinisation as Algorithm 3 of "A closer look at TDFA".
  //
  // A register belongs to a configuration, not to a slot. When a transition
  // gives a tag a new value, a register is allocated for that value and
  // remembered, so that the same value asked for again is the same register;
  // when a tag is untouched, the configuration keeps the register it already
  // had and nothing is copied. The previous scheme numbered registers by
  // position -- slot times tags plus tag -- which made every transition copy
  // the whole bank, including, for a state that loops on itself, on every
  // character of the input.
  const std::size_t tags = automaton.tag_count;
  tdfa result;
  result.tag_count = tags;

  // Registers 0..tags-1 are where a match leaves its answer. Everything the
  // automaton works with is allocated after them.
  std::size_t next_register = tags;
  // Every register is made for one tag, and is told so here rather than left to
  // be worked out from its number afterwards.
  std::vector<std::uint32_t> register_tag(tags);
  for (tag_id tag = 0; tag < tags; ++tag) {
    register_tag[tag] = static_cast<std::uint32_t>(tag);
  }
  const auto fresh_register = [&](tag_id tag) {
    register_tag.push_back(static_cast<std::uint32_t>(tag));
    return next_register++;
  };

  // A value asked for twice is one register: the tag, the register the value
  // is built from, and what is appended to it.
  struct interned {
    tag_id tag;
    std::size_t source;
    tag_values values;
    std::size_t reg;
  };

  struct configuration {
    path walk;
    std::vector<std::uint32_t> regs;
  };

  const auto initial_paths = closure(
      automaton, std::array{path{.state = automaton.initial, .actions = {}}});

  // The initial configurations: a register per tag, set from nothing.
  std::vector<configuration> initial;
  initial.reserve(initial_paths.size());
  for (const path& walk : initial_paths) {
    configuration entry{.walk = walk, .regs = std::vector<std::uint32_t>(tags)};
    for (tag_id tag = 0; tag < tags; ++tag) {
      // A register, and no operation to go with it.
      //
      // What the initial closure found is held back like anything else, and
      // written by the first transition -- or, if the input ends here, by the
      // final operations. What is left to say is that the register holds
      // nothing, and everything that runs one of these automata starts every
      // register holding nothing already: ten operations at the head of every
      // match said what was true before they ran.
      entry.regs[tag] = static_cast<std::uint32_t>(fresh_register(tag));
    }
    initial.push_back(std::move(entry));
  }

  std::vector<std::vector<configuration>> configurations;
  std::vector<std::uint32_t> pending;
  std::size_t pending_index = 0;

  // Two states are the same when they hold the same TNFA states in the same
  // order -- the order is the precedence, and states that disagree on it
  // disagree about which parse wins. Registers may differ: `mapping` says
  // whether they can be reconciled, and with which copies.
  const auto mapping = [&](const std::vector<configuration>& existing,
                           const std::vector<configuration>& fresh)
      -> std::optional<std::vector<register_command>> {
    if (existing.size() != fresh.size()) return std::nullopt;
    for (std::size_t index = 0; index < existing.size(); ++index) {
      if (existing[index].walk.state != fresh[index].walk.state) {
        return std::nullopt;
      }
      if (existing[index].walk.actions != fresh[index].walk.actions) {
        return std::nullopt;
      }
    }
    // A bijection, checked both ways: one register may not stand for two.
    std::vector<register_command> copies;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> bijection;
    for (std::size_t index = 0; index < existing.size(); ++index) {
      for (tag_id tag = 0; tag < tags; ++tag) {
        const std::uint32_t from = fresh[index].regs[tag];
        const std::uint32_t to = existing[index].regs[tag];
        bool known = false;
        for (const auto& [left, right] : bijection) {
          if (left == from && right == to) { known = true; break; }
          if (left == from || right == to) return std::nullopt;
        }
        if (!known) bijection.emplace_back(from, to);
      }
    }
    for (const auto& [from, to] : bijection) {
      if (from == to) continue;
      copies.push_back(register_command{
          .destination = to, .source = from, .values = {}});
    }
    return copies;
  };

  const auto add_state = [&](std::vector<configuration> entries,
                             std::vector<register_command>& operations)
      -> std::size_t {
    // Leftmost-first, which is Perl's rule and not the one the TDFA papers
    // call leftmost greedy.
    //
    // Those two are different rules under names close enough to be walked
    // into. RE2C's leftmost greedy is "match the longest possible prefix of
    // the input and take the leftmost path through the expression that
    // corresponds to this prefix": length first, precedence only to settle
    // paths of the same length -- which is why the disambiguation theory in
    // those papers compares paths that begin and end in the same place, and
    // says nothing about where a match ends. A lexer wants that rule.
    //
    // Perl's rule, which is also RE2's and CTRE's, is that the first
    // alternative under which the whole expression matches wins, however
    // short it is. `for|each|foreach` reads "foreach" as `for` and then
    // `each`, where the longest-prefix rule reads it as `foreach`.
    //
    // The whole of that rule is here. The walks in a state are held in the
    // order of precedence, so where one of them has matched, every walk below
    // it has lost -- no parse they could still find would be taken over this
    // one, whatever they go on to read. Keeping them is what lets the machine
    // walk on and answer with a parse the order says lost.
    //
    // Cut here rather than in the walk, because it is a fact about the state:
    // a state whose match is first has nowhere to go, and says so by having no
    // transitions at all. Nothing the papers do is touched -- the order of
    // configurations, their bitcodes, and the tags of the walk that wins are
    // all what they were, with the losers gone.
    //
    // Only where the match is what ends the reading. A match anchored to the
    // end of the input is not: there the whole of it has to be taken, and a
    // walk below the match may be the only one that can take it. `a|ab`
    // reading "ab" is the match of `a` losing to the walk under it, which is
    // what Perl's backtracking does when the anchor fails -- and what this
    // does by keeping them and answering with the first walk still accepting
    // once the input runs out. The same rule, said at the other end, and no
    // walking back either way.
    if (cut_at_match) {
      const auto matched =
          std::ranges::find_if(entries, [&](const configuration& one) {
            return one.walk.state == automaton.final;
          });
      if (matched != entries.end()) entries.erase(matched + 1, entries.end());
    }
    for (std::size_t id = 0; id < configurations.size(); ++id) {
      if (auto copies = mapping(configurations[id], entries)) {
        // The copies go on the transition that leads here -- but a transition
        // executes as one step: every source is read before any destination is
        // written, which is what makes a permutation of registers work at all.
        // So a copy may not be chained after an operation of the same
        // transition; it would read the value from before it. Where the copy
        // would take from a register this transition writes, the operation
        // that writes it is repeated into the destination instead, and where
        // the destination is already written there is nothing to do.
        for (register_command& copy : *copies) {
          const auto written = std::ranges::find_if(
              operations, [&](const register_command& command) {
                return command.destination == copy.destination;
              });
          if (written != operations.end()) continue;
          const auto produces = std::ranges::find_if(
              operations, [&](const register_command& command) {
                return copy.source && command.destination == *copy.source;
              });
          if (produces != operations.end()) {
            register_command repeated = *produces;
            repeated.destination = copy.destination;
            operations.push_back(std::move(repeated));
          } else {
            operations.push_back(std::move(copy));
          }
        }
        return id;
      }
    }
    const std::size_t id = result.states.size();
    std::vector<state_id> key;
    key.reserve(entries.size());
    for (const configuration& entry : entries) key.push_back(entry.walk.state);
    std::vector<std::vector<std::uint32_t>> readings;
    readings.reserve(entries.size());
    for (const configuration& entry : entries) readings.push_back(entry.regs);
    tdfa_state state{.nfa_states = std::move(key),
                     .transitions = {},
                     .accepting_slot = std::nullopt,
                     .final_commands = {},
                     .readings = std::move(readings)};
    for (std::size_t index = 0; index < entries.size(); ++index) {
      if (entries[index].walk.state == automaton.final &&
          !state.accepting_slot.has_value()) {
        state.accepting_slot = index;
      }
    }
    if (state.accepting_slot) {
      const configuration& accepting = entries[*state.accepting_slot];
      for (tag_id tag = 0; tag < tags; ++tag) {
        // Held back until the end, and applied here with the position the
        // input ended at.
        tag_values values;
        for (const auto& [action_tag, negative] : accepting.walk.actions) {
          if (action_tag == tag) values.push_back(!negative);
        }
        state.final_commands.push_back(register_command{
            .destination = tag,
            .source = accepting.regs[tag],
            .values = values});
      }
    }
    result.states.push_back(std::move(state));
    if (result.states.size() > most_states) {
      throw "the deterministic machine for this pattern is larger than this "
            "library will build: a pattern that counts and then goes on to "
            "read anything -- `.*a.{20}` and its like -- has a deterministic "
            "form that doubles with every character of the tail";
    }
    configurations.push_back(std::move(entries));
    pending.push_back(static_cast<std::uint32_t>(id));
    return id;
  };

  {
    std::vector<register_command> nothing;
    result.initial = add_state(initial, nothing);
  }

  while (pending_index < pending.size()) {
    const std::size_t current = pending[pending_index++];
    const std::vector<configuration> source = configurations[current];
    const std::vector<state_id> source_states = result.states[current].nfa_states;

    const auto edge_matches = [](const transition& edge, char symbol) {
      return (edge.kind == transition_kind::symbol && edge.symbol == symbol) ||
             (edge.kind == transition_kind::character_class &&
              edge.symbols.test(static_cast<unsigned char>(symbol)));
    };
    const auto equivalent = [&](char lhs, char rhs) {
      for (state_id state : source_states) {
        for (const transition& edge : automaton.transitions[state]) {
          if (edge_matches(edge, lhs) != edge_matches(edge, rhs)) return false;
        }
      }
      return true;
    };
    std::vector<char> representatives;
    std::vector<symbol_set> symbol_classes;
    for (std::size_t value = 0; value < 256; ++value) {
      const char symbol = static_cast<char>(value);
      bool active = false;
      for (state_id state : source_states) {
        for (const transition& edge : automaton.transitions[state]) {
          if (edge_matches(edge, symbol)) { active = true; break; }
        }
        if (active) break;
      }
      if (!active) continue;
      std::size_t found = representatives.size();
      for (std::size_t index = 0; index < representatives.size(); ++index) {
        if (equivalent(symbol, representatives[index])) { found = index; break; }
      }
      if (found == representatives.size()) {
        representatives.push_back(symbol);
        symbol_classes.emplace_back();
        symbol_classes.back().set(value);
      } else {
        symbol_classes[found].set(value);
      }
    }

    for (std::size_t class_index = 0; class_index < representatives.size();
         ++class_index) {
      const char symbol = representatives[class_index];
      // Step on the symbol: the configurations that have a transition on it,
      // each keeping the registers it arrived with.
      std::vector<path> seeds;
      std::vector<std::vector<std::uint32_t>> seed_regs;
      std::vector<std::vector<std::pair<tag_id, bool>>> seed_actions;
      for (std::size_t slot = 0; slot < source.size(); ++slot) {
        for (const transition& edge : automaton.transitions[source_states[slot]]) {
          if (!edge_matches(edge, symbol)) continue;
          path seed{.state = edge.target, .actions = {}};
          seed.origin = seeds.size();
          seeds.push_back(std::move(seed));
          seed_regs.push_back(source[slot].regs);
          seed_actions.push_back(source[slot].walk.actions);
        }
      }
      const std::vector<path> reached = closure(automaton, seeds);

      // The operations of this transition, and the registers they leave the
      // configurations holding.
      std::vector<interned> allocated;
      std::vector<register_command> operations;
      std::vector<configuration> entries;
      entries.reserve(reached.size());
      for (const path& walk : reached) {
        configuration entry{.walk = walk, .regs = seed_regs[walk.origin]};
        // What this transition writes is what the source configuration was
        // holding back -- the tags its closure found and did not apply -- and
        // it writes them with the position from before this symbol. The tags
        // this closure finds are held back in turn, and written by whichever
        // transition is taken next.
        //
        // That is what a lookahead of one symbol buys. Inside a field every
        // character reaches a configuration where the field could end, so
        // applying tags as they are found writes the end of the field on every
        // character; held back, the write happens once, on the transition that
        // actually leaves.
        for (tag_id tag = 0; tag < tags; ++tag) {
          tag_values values;
          for (const auto& [action_tag, negative] : seed_actions[walk.origin]) {
            if (action_tag == tag) values.push_back(!negative);
          }
          if (values.empty()) continue;  // untouched: the register stands
          const std::size_t from = entry.regs[tag];
          std::size_t reg = 0;
          bool known = false;
          for (const interned& one : allocated) {
            if (one.tag == tag && one.source == from && one.values == values) {
              reg = one.reg;
              known = true;
              break;
            }
          }
          if (!known) {
            reg = fresh_register(tag);
            allocated.push_back(interned{
                .tag = tag, .source = from, .values = values, .reg = reg});
            operations.push_back(register_command{
                .destination = reg, .source = from, .values = values});
          }
          entry.regs[tag] = static_cast<std::uint32_t>(reg);
        }
        entries.push_back(std::move(entry));
      }

      const std::size_t target = add_state(std::move(entries), operations);
      result.states[current].transitions.push_back(
          tdfa_transition{.symbols = symbol_classes[class_index],
                          .target = target,
                          .commands = std::move(operations)});
    }
  }
  result.register_count = next_register;
  result.register_tag = std::move(register_tag);
  return result;
}

constexpr tdfa optimize_tdfa(tdfa automaton, bool allocate_registers) {
  const auto optimize_registers = [&] {
    const std::size_t register_count = automaton.register_count;
    std::vector<std::vector<char>> live(
        automaton.states.size(), std::vector<char>(register_count));

    const auto transfer = [&](const std::vector<char>& output,
                              const std::vector<register_command>& commands) {
      std::vector<char> input = output;
      for (const register_command& command : commands) {
        if (output[command.destination]) input[command.destination] = false;
      }
      for (const register_command& command : commands) {
        if (output[command.destination] && command.source)
          input[*command.source] = true;
      }
      return input;
    };

    std::vector<char> final_live(register_count);
    std::ranges::fill(final_live | std::views::take(automaton.tag_count), true);
    for (std::size_t state :
         std::views::iota(std::size_t{0}, automaton.states.size())) {
      if (automaton.states[state].accepting_slot) {
        live[state] =
            transfer(final_live, automaton.states[state].final_commands);
      }
    }

    bool changed = true;
    while (changed) {
      changed = false;
      for (std::size_t state :
           std::views::iota(std::size_t{0}, automaton.states.size()) |
               std::views::reverse) {
        for (const tdfa_transition& transition :
             automaton.states[state].transitions) {
          const auto input =
              transfer(live[transition.target], transition.commands);
          for (std::size_t reg :
               std::views::iota(std::size_t{0}, register_count)) {
            if (input[reg] && !live[state][reg]) {
              live[state][reg] = true;
              changed = true;
            }
          }
        }
      }
    }

    for (std::size_t state :
         std::views::iota(std::size_t{0}, automaton.states.size())) {
      for (tdfa_transition& transition : automaton.states[state].transitions) {
        const auto& output = live[transition.target];
        std::erase_if(transition.commands,
                      [&](const register_command& command) {
          return !output[command.destination];
        });
      }
    }
    const auto& initial_output = live[automaton.initial];
    std::erase_if(automaton.initialize, [&](const register_command& command) {
      return !initial_output[command.destination];
    });
    for (tdfa_state& state : automaton.states) {
      std::erase_if(state.final_commands,
                    [&](const register_command& command) {
        return !final_live[command.destination];
      });
    }

    // Two registers interfere when one is written while the other is alive --
    // not merely when both are alive somewhere.
    //
    // The coarser rule makes every pair that is ever alive together
    // inseparable, and the whole point of the analysis is to separate them: a
    // TDFA copies a bank of registers across a transition, and those copies
    // can only be removed if the source and the destination may become one
    // register. Which is the second half of the rule: a copy `x <- y` does not
    // make x and y interfere, because after it they hold the same value. That
    // exception is what a coalescing pass is for.
    std::vector<std::vector<char>> interference(
        register_count, std::vector<char>(register_count));
    const auto note_interference = [&](std::size_t lhs, std::size_t rhs) {
      if (lhs == rhs) return;
      interference[lhs][rhs] = true;
      interference[rhs][lhs] = true;
    };
    const auto interfere_over = [&](const std::vector<register_command>& commands,
                                    const std::vector<char>& output) {
      for (const register_command& command : commands) {
        for (std::size_t reg = 0; reg < register_count; ++reg) {
          if (!output[reg]) continue;
          if (command.source && *command.source == reg &&
              command.values.empty()) {
            continue;  // a copy: the two hold one value from here on
          }
          note_interference(command.destination, reg);
        }
      }
    };
    interfere_over(automaton.initialize, live[automaton.initial]);
    for (std::size_t state = 0; state < automaton.states.size(); ++state) {
      for (const tdfa_transition& transition : automaton.states[state].transitions) {
        interfere_over(transition.commands, live[transition.target]);
      }
      if (automaton.states[state].accepting_slot) {
        interfere_over(automaton.states[state].final_commands, final_live);
      }
    }

    std::vector<std::uint32_t> parent(register_count);
    std::ranges::copy(std::views::iota(std::size_t{0}, register_count),
                      parent.begin());
    std::vector<std::vector<std::uint32_t>> members(register_count);
    for (std::size_t reg :
         std::views::iota(std::size_t{0}, register_count)) {
      members[reg].push_back(reg);
    }
    const auto root = [&](std::size_t reg) {
      while (parent[reg] != reg) reg = parent[reg];
      return reg;
    };
    std::vector<char> pinned(register_count);
    std::ranges::fill(pinned | std::views::take(automaton.tag_count), true);
    const auto can_merge = [&](std::size_t lhs, std::size_t rhs) {
      lhs = root(lhs);
      rhs = root(rhs);
      if (lhs == rhs) return true;
      if (pinned[lhs] && pinned[rhs]) return false;
      return std::ranges::none_of(members[lhs], [&](std::size_t left) {
        return std::ranges::any_of(members[rhs], [&](std::size_t right) {
          return interference[left][right];
        });
      });
    };
    const auto merge = [&](std::size_t lhs, std::size_t rhs) {
      lhs = root(lhs);
      rhs = root(rhs);
      if (lhs == rhs || !can_merge(lhs, rhs)) return;
      if (pinned[rhs]) std::swap(lhs, rhs);
      parent[rhs] = lhs;
      pinned[lhs] = pinned[lhs] || pinned[rhs];
      members[lhs].append_range(members[rhs]);
      members[rhs].clear();
    };
    const auto coalesce = [&](const std::vector<register_command>& commands) {
      for (const register_command& command : commands) {
        if (command.source && command.values.empty())
          merge(command.destination, *command.source);
      }
    };
    if (allocate_registers) {
      coalesce(automaton.initialize);
      for (const tdfa_state& state : automaton.states) {
        coalesce(state.final_commands);
        for (const tdfa_transition& transition : state.transitions) {
          coalesce(transition.commands);
        }
      }
      for (std::size_t lhs :
           std::views::iota(std::size_t{0}, register_count)) {
        if (root(lhs) != lhs) continue;
        for (std::size_t rhs : std::views::iota(lhs + 1, register_count)) {
          if (root(rhs) == rhs && can_merge(lhs, rhs)) merge(lhs, rhs);
        }
      }
    }

    std::vector<std::uint32_t> renaming(register_count);
    if (allocate_registers) {
      std::vector<std::uint32_t> compact(
          register_count, std::numeric_limits<std::uint32_t>::max());
      std::ranges::copy(std::views::iota(std::size_t{0}, automaton.tag_count),
                        compact.begin());
      std::size_t next = automaton.tag_count;
      for (std::size_t reg :
           std::views::iota(std::size_t{0}, register_count)) {
        const std::size_t representative = root(reg);
        if (compact[representative] ==
            std::numeric_limits<std::uint32_t>::max()) {
          compact[representative] = next++;
        }
        renaming[reg] = compact[representative];
      }
      automaton.register_count = next;
      // Renaming moves a register's number and never what it holds, so the
      // table that says which tag it holds moves with it.
      std::vector<std::uint32_t> moved(next);
      for (std::size_t reg : std::views::iota(std::size_t{0}, register_count)) {
        moved[renaming[reg]] = automaton.register_tag[reg];
      }
      automaton.register_tag = std::move(moved);
    } else {
      std::ranges::copy(std::views::iota(std::size_t{0}, register_count),
                        renaming.begin());
    }
    const auto rename_commands = [&](std::vector<register_command>& commands) {
      for (register_command& command : commands) {
        command.destination = renaming[command.destination];
        if (command.source) command.source = renaming[*command.source];
      }
      std::erase_if(commands, [](const register_command& command) {
        return command.source == command.destination && command.values.empty();
      });
      std::vector<register_command> unique;
      for (register_command command : commands) {
        const bool duplicate = std::ranges::any_of(
            unique, [&](const register_command& candidate) {
              return candidate.destination == command.destination &&
                     candidate.source == command.source &&
                     candidate.values == command.values;
            });
        if (!duplicate) unique.push_back(std::move(command));
      }
      std::ranges::sort(unique, {}, [](const register_command& command) {
        return std::tuple(!command.source.has_value(), command.values.empty(),
                          command.destination,
                          command.source.value_or(
                              std::numeric_limits<std::size_t>::max()));
      });
      commands = std::move(unique);
    };
    rename_commands(automaton.initialize);
    for (tdfa_state& state : automaton.states) {
      rename_commands(state.final_commands);
      for (tdfa_transition& transition : state.transitions) {
        rename_commands(transition.commands);
      }
      for (std::vector<std::uint32_t>& reading : state.readings) {
        for (std::uint32_t& reg : reading) reg = renaming[reg];
      }
    }
  };

  // Until it stops changing, rather than a fixed number of passes.
  //
  // Each pass removes operations that the previous one made dead and merges
  // registers the previous one made mergeable, so the passes feed each other;
  // two of them was a guess at where that stops. The bound is there because a
  // guess about termination is not a proof.
  if (allocate_registers) {
    std::size_t previous = 0;
    for (std::size_t pass = 0; pass < 8; ++pass) {
      optimize_registers();
      std::size_t commands = automaton.initialize.size();
      for (const tdfa_state& state : automaton.states) {
        commands += state.final_commands.size();
        for (const tdfa_transition& transition : state.transitions) {
          commands += transition.commands.size();
        }
      }
      if (pass != 0 && commands == previous) break;
      previous = commands;
    }
  } else {
    optimize_registers();
    optimize_registers();
  }

  const auto same_commands = [](const auto& lhs, const auto& rhs) {
    return lhs.size() == rhs.size() &&
           std::ranges::equal(lhs, rhs, {}, [](const register_command& command) {
             return std::tuple(command.destination, command.source,
                               command.values);
           }, [](const register_command& command) {
             return std::tuple(command.destination, command.source,
                               command.values);
           });
  };
  for (tdfa_state& state : automaton.states) {
    // Moved from where they are rather than copied into the loop: a
    // transition carries a command list, and taking a copy of every one of
    // them to decide whether it is a duplicate is the copy this loop exists
    // to avoid making twice.
    std::vector<tdfa_transition> normalized;
    normalized.reserve(state.transitions.size());
    for (tdfa_transition& transition : state.transitions) {
      const auto equivalent = std::ranges::find_if(
          normalized, [&](const tdfa_transition& candidate) {
            return candidate.target == transition.target &&
                   same_commands(candidate.commands, transition.commands);
          });
      if (equivalent == normalized.end()) {
        normalized.push_back(std::move(transition));
        continue;
      }
      equivalent->symbols.merge(transition.symbols);
    }
    state.transitions = std::move(normalized);
  }
  return automaton;
}

constexpr match simulate(const tdfa& automaton, std::string_view input) {
  std::vector<tag_history> registers(automaton.register_count);
  const auto execute = [&](const std::vector<register_command>& commands,
                           std::size_t position) {
    const auto old = registers;
    for (const auto& command : commands) {
      tag_history value;
      if (command.source.has_value()) value = old[*command.source];
      for (bool current : command.values) {
        value.push_back(current ? static_cast<std::ptrdiff_t>(position)
                                : negative_tag);
      }
      registers[command.destination] = std::move(value);
    }
  };
  execute(automaton.initialize, 0);
  std::size_t state = automaton.initial;
  for (std::size_t i = 0; i < input.size(); ++i) {
    const auto& transitions = automaton.states[state].transitions;
    const auto found = std::find_if(transitions.begin(), transitions.end(),
                                    [&](const auto& transition) {
      return transition.symbols.test(static_cast<unsigned char>(input[i]));
    });
    if (found == transitions.end()) {
      return {.matched = false,
              .tags = std::vector<tag_history>(automaton.tag_count)};
    }
    execute(found->commands, i);
    state = found->target;
  }
  const auto slot = automaton.states[state].accepting_slot;
  if (!slot.has_value()) {
    return {.matched = false,
            .tags = std::vector<tag_history>(automaton.tag_count)};
  }
  execute(automaton.states[state].final_commands, input.size());
  match match{.matched = true,
              .tags = std::vector<tag_history>(automaton.tag_count)};
  for (tag_id tag = 0; tag < automaton.tag_count; ++tag) {
    match.tags[tag] = registers[tag];
  }
  return match;
}

}  // namespace scan::tre
