export module tre;

import std;

export namespace tre {

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
                                    std::size_t maximum = unbounded) {
  return ast::repetition{{std::move(element)}, minimum, maximum};
}
[[nodiscard]] constexpr node star(node element) {
  return repeat(std::move(element), 0);
}
[[nodiscard]] constexpr node plus(node element) {
  return repeat(std::move(element), 1);
}
[[nodiscard]] constexpr node optional(node element) {
  return repeat(std::move(element), 0, 1);
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
};

struct tdfa {
  std::size_t initial = 0;
  std::size_t tag_count = 0;
  std::size_t register_count = 0;
  std::vector<register_command> initialize;
  std::vector<tdfa_state> states;
};

// Builds a deterministic tagged transducer. epsilon actions after a symbol are
// delayed to that symbol's transition, which is the one-symbol lookahead form.
[[nodiscard]] constexpr tdfa compile_tdfa(const tnfa& automaton);
// Applies TDFA register liveness, dead-store elimination, copy cleanup, and
// local transition normalization.
[[nodiscard]] constexpr tdfa optimize_tdfa(tdfa automaton,
                                            bool allocate_registers = true);
[[nodiscard]] constexpr match simulate(const tdfa& automaton,
                                       std::string_view input);

}  // namespace tre

namespace tre {
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
    if (node.maximum == unbounded) {
      const fragment copy = visit(node.element.front());
      add(tail, {.target = copy.start, .kind = transition_kind::epsilon,
                 .priority = 0});
      if (node.minimum == 0) {
        add_negative_chain(tail, end, tags(node.element.front()), 1);
      } else {
        add(tail, {.target = end, .kind = transition_kind::epsilon,
                   .priority = 1});
      }
      add(copy.end, {.target = copy.start, .kind = transition_kind::epsilon,
                     .priority = 0});
      add(copy.end, {.target = end, .kind = transition_kind::epsilon,
                     .priority = 1});
      return {start, end};
    }
    if (node.minimum == 0) {
      add_negative_chain(tail, end, tags(node.element.front()), 1);
    } else {
      add(tail, {.target = end, .kind = transition_kind::epsilon,
                 .priority = 1});
    }
    for (std::size_t i = node.minimum; i < node.maximum; ++i) {
      const fragment copy = visit(node.element.front());
      add(tail, {.target = copy.start, .kind = transition_kind::epsilon,
                 .priority = 0});
      tail = copy.end;
      add(tail, {.target = end, .kind = transition_kind::epsilon,
                 .priority = 1});
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

constexpr tdfa compile_tdfa(const tnfa& automaton) {
  tdfa result;
  result.tag_count = automaton.tag_count;
  const std::vector<path> initial = closure(
      automaton,
      std::array{path{.state = automaton.initial, .actions = {}}});

  std::vector<std::vector<state_id>> keys;
  std::vector<std::uint32_t> pending;
  std::size_t pending_index = 0;
  const auto add_state = [&](const std::vector<path>& closure) -> std::size_t {
    std::vector<state_id> key;
    for (const auto& path : closure) key.push_back(path.state);
    const auto found = std::ranges::find(keys, key);
    if (found != keys.end()) {
      return static_cast<std::size_t>(std::ranges::distance(keys.begin(), found));
    }
    const std::size_t id = result.states.size();
    keys.push_back(key);
    tdfa_state state{.nfa_states = std::move(key),
                    .transitions = {},
                    .accepting_slot = std::nullopt,
                    .final_commands = {}};
    for (std::size_t i = 0; i < closure.size(); ++i) {
      if (closure[i].state == automaton.final &&
          !state.accepting_slot.has_value()) {
        state.accepting_slot = i;
      }
    }
    if (state.accepting_slot) {
      for (std::size_t tag : std::views::iota(std::size_t{0}, automaton.tag_count)) {
            state.final_commands.push_back(register_command{
                .destination = tag,
                .source = automaton.tag_count +
                          register_index(*state.accepting_slot, tag,
                                         automaton.tag_count),
                .values = {}});
          }
    }
    result.states.push_back(std::move(state));
    pending.push_back(id);
    return id;
  };
  result.initial = add_state(initial);
  result.register_count =
      automaton.tag_count + initial.size() * automaton.tag_count;
  for (std::size_t slot = 0; slot < initial.size(); ++slot) {
    for (tag_id tag = 0; tag < automaton.tag_count; ++tag) {
      register_command command{
          .destination = automaton.tag_count +
                         register_index(slot, tag, automaton.tag_count),
          .source = std::nullopt,
          .values = {}};
      for (const auto& [action_tag, negative] : initial[slot].actions) {
        if (action_tag == tag) command.values.push_back(!negative);
      }
      result.initialize.push_back(std::move(command));
    }
  }

  while (pending_index < pending.size()) {
    const std::size_t current_state = pending[pending_index++];
    const std::vector<state_id> source_states =
        result.states[current_state].nfa_states;
    const auto edge_matches = [](const transition& edge, char symbol) {
      return (edge.kind == transition_kind::symbol && edge.symbol == symbol) ||
             (edge.kind == transition_kind::character_class &&
              edge.symbols.test(static_cast<unsigned char>(symbol)));
    };
    const auto equivalent = [&](char lhs, char rhs) {
      return std::ranges::all_of(source_states, [&](state_id state) {
        return std::ranges::all_of(
            automaton.transitions[state], [&](const transition& edge) {
              return edge_matches(edge, lhs) == edge_matches(edge, rhs);
            });
      });
    };
    std::vector<char> representatives;
    std::vector<symbol_set> symbol_classes;
    for (std::size_t value : std::views::iota(std::size_t{0}, std::size_t{256})) {
          const char symbol = static_cast<char>(value);
          const bool active = std::ranges::any_of(
              source_states, [&](state_id state) {
                return std::ranges::any_of(
                    automaton.transitions[state], [&](const transition& edge) {
                      return edge_matches(edge, symbol);
                    });
              });
          if (!active) continue;
          const auto found = std::ranges::find_if(
              representatives,
              [&](char representative) { return equivalent(symbol, representative); });
          if (found == representatives.end()) {
            representatives.push_back(symbol);
            symbol_classes.emplace_back();
            symbol_classes.back().set(value);
          } else {
            const auto index = static_cast<std::size_t>(
                std::ranges::distance(representatives.begin(), found));
            symbol_classes[index].set(value);
          }
        }
    for (std::size_t class_index : std::views::iota(std::size_t{0}, representatives.size())) {
      const char symbol = representatives[class_index];
      struct seed {
        path path;
        std::size_t source_slot;
      };
      std::vector<seed> seeds;
      for (std::size_t slot = 0; slot < source_states.size(); ++slot) {
        for (const auto& edge : automaton.transitions[source_states[slot]]) {
          const bool matches =
              (edge.kind == transition_kind::symbol &&
               edge.symbol == symbol) ||
              (edge.kind == transition_kind::character_class &&
               edge.symbols.test(static_cast<unsigned char>(symbol)));
          if (matches) {
            seeds.push_back(
                {path{.state = edge.target, .actions = {}}, slot});
          }
        }
      }
      // One closure over every seed, in seed order. Taking it per seed and
      // merging afterwards visits the same states in the same order -- the
      // stack is shared either way -- but allocates the whole apparatus once
      // per seed instead of once.
      std::vector<path> seed_paths;
      seed_paths.reserve(seeds.size());
      for (std::size_t index = 0; index < seeds.size(); ++index) {
        path entry = seeds[index].path;
        entry.origin = index;
        seed_paths.push_back(std::move(entry));
      }
      std::vector<path> target_paths = closure(automaton, seed_paths);
      std::vector<std::uint32_t> source_slots;
      source_slots.reserve(target_paths.size());
      for (const path& path : target_paths) {
        source_slots.push_back(seeds[path.origin].source_slot);
      }
      const std::size_t target = add_state(target_paths);
      result.register_count = std::max(
          result.register_count,
          automaton.tag_count +
              result.states[target].nfa_states.size() * automaton.tag_count);
      tdfa_transition transition{.symbols = symbol_classes[class_index],
                                .target = target,
                                .commands = {}};
      for (std::size_t slot = 0; slot < target_paths.size(); ++slot) {
        for (tag_id tag = 0; tag < automaton.tag_count; ++tag) {
          register_command command{
              .destination = automaton.tag_count +
                             register_index(slot, tag, automaton.tag_count),
              .source = automaton.tag_count +
                        register_index(source_slots[slot], tag,
                                       automaton.tag_count),
              .values = {}};
          for (const auto& [action_tag, negative] :
               target_paths[slot].actions) {
            if (action_tag == tag) command.values.push_back(!negative);
          }
          transition.commands.push_back(std::move(command));
        }
      }
      result.states[current_state].transitions.push_back(std::move(transition));
    }
  }
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

    std::vector<std::vector<char>> interference(
        register_count, std::vector<char>(register_count));
    // Plain loops with a test in the body, and no filter view: the predicate
    // of one is a temporary of the range expression, and reading it back
    // during constant evaluation is where this stopped being a constant.
    for (const std::vector<char>& state_live : live) {
      for (std::size_t lhs = 0; lhs < register_count; ++lhs) {
        if (!state_live[lhs]) continue;
        for (std::size_t rhs = lhs + 1; rhs < register_count; ++rhs) {
          if (!state_live[rhs]) continue;
          interference[lhs][rhs] = true;
          interference[rhs][lhs] = true;
        }
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
    }
  };

  optimize_registers();
  optimize_registers();

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
    execute(found->commands, i + 1);
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

}  // namespace tre
