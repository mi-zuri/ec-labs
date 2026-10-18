// Lab 2 - greedy regret heuristics.
//
// Methods:
//   nn_regret     - nearest neighbor (any position), 2-regret
//   nn_weighted   - nearest neighbor (any position), 2-regret + best change
//   gc_regret     - greedy cycle, 2-regret
//   gc_weighted   - greedy cycle, 2-regret + best change
//
// For every unused node:
//   regret      = (second cheapest length increase) - (cheapest length increase)
//   best change = (cheapest length increase) + (cost of the node)
// Every step inserts the node with the highest
// regret weight * regret - change weight * best change at its cheapest position.
//
// All methods start from a given node, and run i starts from node i.
// On ties the first candidate found wins (lowest node index, then lowest position).
#include "../../../common/cpp/tsp.hpp"

#include <climits>

using tsp::Instance;

struct Weights {
    long long regret;
    long long change;
};

static const Weights PURE_REGRET = {1, 0};
static const Weights EQUAL_WEIGHTS = {1, 1};

// ---------------------------------------------------------------------------
// Building blocks shared by the methods
// ---------------------------------------------------------------------------

// A tour under construction, plus which nodes are already in it.
struct Builder {
    const Instance &instance;
    bool trace;                // print every intermediate tour?
    std::vector<int> nodes;
    std::vector<bool> used;    // used[v] is true if node v is already in the tour

    // Begin a tour containing only the start node.
    Builder(const Instance &instance, int start, bool trace)
        : instance(instance), trace(trace), nodes{start}, used(instance.n, false) {
        used[start] = true;
        if (trace) tsp::emit_step(nodes);
    }

    int length() const { return static_cast<int>(nodes.size()); }

    // Insert `node` into the tour at index `position`.
    void add_node(int position, int node) {
        nodes.insert(nodes.begin() + position, node);
        used[node] = true;
        if (trace) tsp::emit_step(nodes);
    }
};

// Return the unused node u minimizing d(from_node, u) + cost[u].
static int nearest_unused_node(const Builder &tour, int from_node) {
    const Instance &instance = tour.instance;
    int best_node = -1;
    long long best_value = LLONG_MAX;
    for (int u = 0; u < instance.n; u++) {
        if (tour.used[u]) continue;
        long long value = instance.d(from_node, u) + instance.cost[u];
        if (value < best_value) {
            best_node = u;
            best_value = value;
        }
    }
    return best_node;
}

// Length increase for every insertion index p of the open path
// (p = 0 before the first node, p = length after the last one).
static std::vector<long long> path_insertion_increases(const Builder &tour, int node) {
    const Instance &instance = tour.instance;
    const std::vector<int> &nodes = tour.nodes;
    int length = tour.length();

    std::vector<long long> increases = {instance.d(node, nodes[0])};
    for (int p = 1; p < length; p++) {
        int before = nodes[p - 1], after = nodes[p];
        increases.push_back(instance.d(before, node) + instance.d(node, after) - instance.d(before, after));
    }
    increases.push_back(instance.d(nodes[length - 1], node));
    return increases;
}

// Length increase for every insertion index p of the closed cycle,
// i.e. between nodes[p-1] and nodes[p] (p = 0 is the closing edge).
static std::vector<long long> cycle_insertion_increases(const Builder &tour, int node) {
    const Instance &instance = tour.instance;
    const std::vector<int> &nodes = tour.nodes;
    int length = tour.length();

    std::vector<long long> increases;
    for (int p = 0; p < length; p++) {
        int before = nodes[(p - 1 + length) % length], after = nodes[p];
        increases.push_back(instance.d(before, node) + instance.d(node, after) - instance.d(before, after));
    }
    return increases;
}

struct TwoCheapest {
    int position;  // of the cheapest
    long long cheapest;
    long long second_cheapest;
};

static TwoCheapest two_cheapest(const std::vector<long long> &increases) {
    TwoCheapest result = {-1, LLONG_MAX, LLONG_MAX};
    for (int p = 0; p < static_cast<int>(increases.size()); p++) {
        if (increases[p] < result.cheapest) {
            result = {p, increases[p], result.cheapest};
        } else if (increases[p] < result.second_cheapest) {
            result.second_cheapest = increases[p];
        }
    }
    return result;
}

// The node to add next and where to put it.
struct Insertion {
    int node;
    int position;
};

// Find the unused node with the highest score and its cheapest position.
template <typename IncreasesFn>
static Insertion regret_insertion(const Builder &tour, IncreasesFn insertion_increases, Weights weights) {
    const Instance &instance = tour.instance;
    Insertion best = {-1, -1};
    long long best_score = LLONG_MIN;

    for (int u = 0; u < instance.n; u++) {
        if (tour.used[u]) continue;
        TwoCheapest options = two_cheapest(insertion_increases(tour, u));

        long long regret = options.second_cheapest - options.cheapest;
        long long best_change = options.cheapest + instance.cost[u];
        long long score = weights.regret * regret - weights.change * best_change;
        if (score > best_score) {
            best = {u, options.position};
            best_score = score;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Methods
// ---------------------------------------------------------------------------

// Grow a path from the start node, inserting nodes at any position.
static std::vector<int> nearest_neighbor_regret(const Instance &instance, int start, Weights weights, bool trace) {
    Builder path(instance, start, trace);

    while (path.length() < instance.k) {
        Insertion next = regret_insertion(path, path_insertion_increases, weights);
        path.add_node(next.position, next.node);
    }
    return path.nodes;
}

// Start a cycle with the start node and its best partner, then keep inserting
// nodes into the edges of the cycle.
static std::vector<int> greedy_cycle_regret(const Instance &instance, int start, Weights weights, bool trace) {
    Builder cycle(instance, start, trace);

    int partner = nearest_unused_node(cycle, start);
    cycle.add_node(cycle.length(), partner);

    while (cycle.length() < instance.k) {
        Insertion next = regret_insertion(cycle, cycle_insertion_increases, weights);
        cycle.add_node(next.position, next.node);
    }
    return cycle.nodes;
}

// ---------------------------------------------------------------------------
// Entry points required by the protocol
// ---------------------------------------------------------------------------

// Run one run of a method and return the resulting tour.
static std::vector<int> solve(const Instance &instance, const std::string &method, int run, uint64_t /*seed*/,
                              bool trace) {
    if (method == "nn_regret") return nearest_neighbor_regret(instance, run, PURE_REGRET, trace);
    if (method == "nn_weighted") return nearest_neighbor_regret(instance, run, EQUAL_WEIGHTS, trace);
    if (method == "gc_regret") return greedy_cycle_regret(instance, run, PURE_REGRET, trace);
    return greedy_cycle_regret(instance, run, EQUAL_WEIGHTS, trace);
}

// Number of runs of a method, or -1 if the method does not exist.
static int runs_for(const std::string &method, int n) {
    if (method == "nn_regret" || method == "nn_weighted" || method == "gc_regret" || method == "gc_weighted") {
        return n;  // one run per starting node
    }
    return -1;
}

int main(int argc, char **argv) { return tsp::run(argc, argv, solve, runs_for); }
