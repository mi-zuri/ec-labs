// Lab 1 - greedy heuristics: random, nn_end, nn_any, greedy_cycle.
//
// Greedy run i starts from node i. Each step adds the unused node with the smallest
// (tour length increase + node cost); ties go to the lowest node, then lowest position.
#include "../../../common/cpp/tsp.hpp"

#include <climits>
#include <numeric>

using tsp::Instance;

static const int RANDOM_RUNS = 200;

// A tour under construction, plus which nodes are already in it.
struct Builder {
    const Instance &instance;
    bool trace;
    std::vector<int> nodes;
    std::vector<bool> used;  // used[v] is true if node v is in the tour

    Builder(const Instance &instance, int start, bool trace)
        : instance(instance), trace(trace), nodes{start}, used(instance.n, false) {
        used[start] = true;
        if (trace) tsp::emit_step(nodes);
    }

    int length() const { return static_cast<int>(nodes.size()); }

    void add_node(int position, int node) {
        nodes.insert(nodes.begin() + position, node);
        used[node] = true;
        if (trace) tsp::emit_step(nodes);
    }
};

// Where a node would be inserted and how much that lengthens the tour.
struct Position {
    int index;
    long long length_increase;
};

// The node to add next and where to put it.
struct Insertion {
    int node;
    int position;
};

// Unused node u minimizing d(from_node, u) + cost[u].
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

// Index 0 is before the first node, `length` is after the last one.
static Position cheapest_position_in_path(const Builder &tour, int node) {
    const Instance &instance = tour.instance;
    const std::vector<int> &nodes = tour.nodes;
    int length = tour.length();
    Position best = {-1, LLONG_MAX};

    for (int p = 0; p <= length; p++) {
        long long increase;
        if (p == 0) {
            increase = instance.d(node, nodes[0]);
        } else if (p == length) {
            increase = instance.d(nodes[length - 1], node);
        } else {
            int before = nodes[p - 1], after = nodes[p];
            increase = instance.d(before, node) + instance.d(node, after) - instance.d(before, after);
        }
        if (increase < best.length_increase) best = {p, increase};
    }
    return best;
}

// Edge i is nodes[i] -> nodes[i+1] (the last wraps to nodes[0]); inserting there gives index i + 1.
static Position cheapest_position_in_cycle(const Builder &tour, int node) {
    const Instance &instance = tour.instance;
    int length = tour.length();
    Position best = {-1, LLONG_MAX};

    for (int i = 0; i < length; i++) {
        int before = tour.nodes[i];
        int after = tour.nodes[(i + 1) % length];
        long long increase = instance.d(before, node) + instance.d(node, after) - instance.d(before, after);
        if (increase < best.length_increase) best = {i + 1, increase};
    }
    return best;
}

// Unused node and position with the smallest total cost of adding.
template <typename PositionFn>
static Insertion cheapest_insertion(const Builder &tour, PositionFn cheapest_position) {
    const Instance &instance = tour.instance;
    Insertion best = {-1, -1};
    long long best_value = LLONG_MAX;

    for (int u = 0; u < instance.n; u++) {
        if (tour.used[u]) continue;
        Position position = cheapest_position(tour, u);
        long long value = position.length_increase + instance.cost[u];
        if (value < best_value) {
            best = {u, position.index};
            best_value = value;
        }
    }
    return best;
}

static std::vector<int> random_solution(const Instance &instance, uint64_t seed, bool trace) {
    tsp::Rng rng(seed);
    std::vector<int> nodes(instance.n);
    std::iota(nodes.begin(), nodes.end(), 0);
    rng.shuffle(nodes);
    nodes.resize(instance.k);

    if (trace) {
        for (int length = 1; length <= instance.k; length++) tsp::emit_step(nodes, length);
    }
    return nodes;
}

// Append the nearest unused node to the end of the path.
static std::vector<int> nn_end(const Instance &instance, int start, bool trace) {
    Builder path(instance, start, trace);

    while (path.length() < instance.k) {
        int node = nearest_unused_node(path, path.nodes.back());
        path.add_node(path.length(), node);
    }
    return path.nodes;
}

// Insert the best node at the best position of the path.
static std::vector<int> nn_any(const Instance &instance, int start, bool trace) {
    Builder path(instance, start, trace);

    while (path.length() < instance.k) {
        Insertion next = cheapest_insertion(path, cheapest_position_in_path);
        path.add_node(next.position, next.node);
    }
    return path.nodes;
}

// Start from the best pair, then insert the best node into the best edge of the cycle.
static std::vector<int> greedy_cycle(const Instance &instance, int start, bool trace) {
    Builder cycle(instance, start, trace);

    int partner = nearest_unused_node(cycle, start);
    cycle.add_node(cycle.length(), partner);

    while (cycle.length() < instance.k) {
        Insertion next = cheapest_insertion(cycle, cheapest_position_in_cycle);
        cycle.add_node(next.position, next.node);
    }
    return cycle.nodes;
}

static std::vector<int> solve(const Instance &instance, const std::string &method, int run, uint64_t seed,
                              bool trace) {
    if (method == "random") return random_solution(instance, seed, trace);
    if (method == "nn_end") return nn_end(instance, run, trace);
    if (method == "nn_any") return nn_any(instance, run, trace);
    return greedy_cycle(instance, run, trace);
}

static int runs_for(const std::string &method, int n) {
    if (method == "random") return RANDOM_RUNS;
    if (method == "nn_end" || method == "nn_any" || method == "greedy_cycle") return n;
    return -1;
}

int main(int argc, char **argv) { return tsp::run(argc, argv, solve, runs_for); }
