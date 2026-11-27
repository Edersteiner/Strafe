#include "strafe/core/log.h"
#include "strafe/core/reflect.h"
#include "strafe/core/utils.h"
#include "strafe/ecs/system.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace strafe::ecs {

namespace {

// As [[system(stage = "...")]] spells it.
std::string stageName(Stage stage) { return utils::toLower(reflect::enumName(stage).value_or("?")); }

// The systems of one stage as a graph, by their index in the stage's default order.
struct Graph {
  std::vector<SystemDesc *> nodes;
  std::vector<std::vector<size_t>> successors;
  std::vector<std::vector<size_t>> predecessors;

  void addEdge(size_t first, size_t then) {
    successors[first].push_back(then);
    predecessors[then].push_back(first);
  }
};

// Every system left over by the sort is behind or on a cycle, so each has a
// left over predecessor. Walking predecessors from any of them must repeat.
std::string describeCycle(const Graph &graph, const std::vector<bool> &placed) {
  size_t current = static_cast<size_t>(std::ranges::find(placed, false) - placed.begin());
  std::vector<size_t> path;
  std::vector<bool> on_path(graph.nodes.size(), false);
  while (!on_path[current]) {
    on_path[current] = true;
    path.push_back(current);
    current = *std::ranges::find_if(graph.predecessors[current], [&](size_t node) { return !placed[node]; });
  }
  // The walk went backwards, from each system to one that must run before it.
  std::vector<size_t> cycle(std::ranges::find(path, current), path.end());
  std::ranges::reverse(cycle);
  std::string text;
  for (const size_t node : cycle) {
    text += std::format("'{}' runs before ", graph.nodes[node]->name);
  }
  return text + std::format("'{}'", graph.nodes[cycle.front()]->name);
}

} // namespace

void Schedule::add(SystemDesc system) { systems_.push_back(std::move(system)); }

bool Schedule::finalize() {
  bool ok = true;
  std::unordered_map<std::string_view, SystemDesc *> by_name;
  std::vector<std::string_view> names;
  for (SystemDesc &system : systems_) {
    names.push_back(system.name);
    const auto [existing, added] = by_name.emplace(system.name, &system);
    if (!added) {
      log::error(
          "{}: there is already a system named '{}', at {}", system.source, system.name, existing->second->source);
      ok = false;
    }
  }

  for (size_t stage = 0; stage < order_.size(); ++stage) {
    Graph graph;
    std::unordered_map<const SystemDesc *, size_t> index;
    for (SystemDesc &system : systems_) {
      if (static_cast<size_t>(system.stage) == stage) {
        index.emplace(&system, graph.nodes.size());
        graph.nodes.push_back(&system);
      }
    }
    graph.successors.resize(graph.nodes.size());
    graph.predecessors.resize(graph.nodes.size());

    // Resolves one name from before or after into a node of this stage.
    const auto resolve = [&](const SystemDesc &system, std::string_view relation,
                             std::string_view name) -> std::optional<size_t> {
      const auto found = by_name.find(name);
      if (found == by_name.end()) {
        const std::optional<std::string_view> closest = utils::closestMatch(name, names);
        log::error(
            "{}: '{}' runs {} '{}', but there is no such system{}", system.source, system.name, relation, name,
            closest ? std::format("; did you mean '{}'?", *closest) : "");
        return std::nullopt;
      }
      const SystemDesc &other = *found->second;
      if (other.stage != system.stage) {
        log::error(
            "{}: '{}' runs {} '{}', but '{}' is in the {} stage and '{}' in the {} stage", system.source, system.name,
            relation, name, system.name, stageName(system.stage), name, stageName(other.stage));
        return std::nullopt;
      }
      return index.at(&other);
    };

    for (size_t node = 0; node < graph.nodes.size(); ++node) {
      const SystemDesc &system = *graph.nodes[node];
      for (const std::string_view name : system.before) {
        if (const std::optional<size_t> other = resolve(system, "before", name)) {
          graph.addEdge(node, *other);
        } else {
          ok = false;
        }
      }
      for (const std::string_view name : system.after) {
        if (const std::optional<size_t> other = resolve(system, "after", name)) {
          graph.addEdge(*other, node);
        } else {
          ok = false;
        }
      }
    }

    // Kahn's algorithm. Of the systems that are free to run, the earliest in
    // the default order goes next, so constraints only move what they must.
    std::vector<size_t> waiting_on(graph.nodes.size());
    std::set<size_t> ready;
    for (size_t node = 0; node < graph.nodes.size(); ++node) {
      waiting_on[node] = graph.predecessors[node].size();
      if (waiting_on[node] == 0) {
        ready.insert(node);
      }
    }
    std::vector<SystemDesc *> &order = order_[stage];
    order.clear();
    std::vector<bool> placed(graph.nodes.size(), false);
    while (!ready.empty()) {
      const size_t node = *ready.begin();
      ready.erase(ready.begin());
      placed[node] = true;
      order.push_back(graph.nodes[node]);
      for (const size_t next : graph.successors[node]) {
        if (--waiting_on[next] == 0) {
          ready.insert(next);
        }
      }
    }
    if (order.size() != graph.nodes.size()) {
      log::error("Systems can't be ordered because {}", describeCycle(graph, placed));
      ok = false;
    }
  }
  return ok;
}

void Schedule::run(Stage stage, Registry &registry, float delta_seconds) const {
  SystemContext context{.registry = registry, .delta_seconds = delta_seconds};
  for (const SystemDesc *system : order_[static_cast<size_t>(stage)]) {
    if (system->enabled) {
      system->run(context);
    }
  }
}

SystemDesc *Schedule::find(std::string_view name) {
  const auto found = std::ranges::find(systems_, name, &SystemDesc::name);
  return found == systems_.end() ? nullptr : &*found;
}

} // namespace strafe::ecs
