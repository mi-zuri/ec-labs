// Lab 1 - greedy heuristics: random, nearest neighbor (end / any position), greedy cycle.
#include "../../../common/cpp/tsp.hpp"

#include <climits>
#include <numeric>

using tsp::Instance;

static const int RANDOM_RUNS = 200;

static std::vector<int> random_solution(const Instance &in, uint64_t seed, bool trace) {
    tsp::Rng rng(seed);
    std::vector<int> perm(in.n);
    std::iota(perm.begin(), perm.end(), 0);
    rng.shuffle(perm);
    perm.resize(in.k);
    if (trace)
        for (int m = 1; m <= in.k; m++) tsp::emit_step(perm, m);
    return perm;
}

// Append the node with the best (distance from last node + node cost).
static std::vector<int> nn_end(const Instance &in, int start, bool trace) {
    std::vector<bool> used(in.n, false);
    std::vector<int> tour{start};
    used[start] = true;
    if (trace) tsp::emit_step(tour);
    while (static_cast<int>(tour.size()) < in.k) {
        int last = tour.back(), best_u = -1;
        long long best = LLONG_MAX;
        for (int u = 0; u < in.n; u++) {
            if (used[u]) continue;
            long long delta = in.d(last, u) + in.cost[u];
            if (delta < best) best = delta, best_u = u;
        }
        tour.push_back(best_u);
        used[best_u] = true;
        if (trace) tsp::emit_step(tour);
    }
    return tour;
}

// Insert the best node at the best position of the path: front, between two nodes, or end.
static std::vector<int> nn_any(const Instance &in, int start, bool trace) {
    std::vector<bool> used(in.n, false);
    std::vector<int> tour{start};
    used[start] = true;
    if (trace) tsp::emit_step(tour);
    while (static_cast<int>(tour.size()) < in.k) {
        int m = static_cast<int>(tour.size()), best_u = -1, best_pos = -1;
        long long best = LLONG_MAX;
        for (int u = 0; u < in.n; u++) {
            if (used[u]) continue;
            for (int p = 0; p <= m; p++) {  // p = index u is inserted at
                long long delta;
                if (p == 0) delta = in.d(u, tour[0]);
                else if (p == m) delta = in.d(tour[m - 1], u);
                else delta = in.d(tour[p - 1], u) + in.d(u, tour[p]) - in.d(tour[p - 1], tour[p]);
                delta += in.cost[u];
                if (delta < best) best = delta, best_u = u, best_pos = p;
            }
        }
        tour.insert(tour.begin() + best_pos, best_u);
        used[best_u] = true;
        if (trace) tsp::emit_step(tour);
    }
    return tour;
}

// Start from the best pair, then repeatedly insert the best node into the best edge of the cycle.
static std::vector<int> greedy_cycle(const Instance &in, int start, bool trace) {
    std::vector<bool> used(in.n, false);
    std::vector<int> tour{start};
    used[start] = true;
    if (trace) tsp::emit_step(tour);

    int best_u = -1;
    long long best = LLONG_MAX;
    for (int u = 0; u < in.n; u++) {
        if (used[u]) continue;
        long long delta = in.d(start, u) + in.cost[u];
        if (delta < best) best = delta, best_u = u;
    }
    tour.push_back(best_u);
    used[best_u] = true;
    if (trace) tsp::emit_step(tour);

    while (static_cast<int>(tour.size()) < in.k) {
        int m = static_cast<int>(tour.size()), best_pos = -1;
        best_u = -1;
        best = LLONG_MAX;
        for (int u = 0; u < in.n; u++) {
            if (used[u]) continue;
            for (int i = 0; i < m; i++) {  // edge tour[i] -> tour[i+1], incl. the closing edge
                int a = tour[i], b = tour[(i + 1) % m];
                long long delta = in.d(a, u) + in.d(u, b) - in.d(a, b) + in.cost[u];
                if (delta < best) best = delta, best_u = u, best_pos = i + 1;
            }
        }
        tour.insert(tour.begin() + best_pos, best_u);
        used[best_u] = true;
        if (trace) tsp::emit_step(tour);
    }
    return tour;
}

int main(int argc, char **argv) {
    auto solve = [](const Instance &in, const std::string &method, int run, uint64_t seed, bool trace) {
        if (method == "random") return random_solution(in, seed, trace);
        if (method == "nn_end") return nn_end(in, run, trace);
        if (method == "nn_any") return nn_any(in, run, trace);
        return greedy_cycle(in, run, trace);
    };
    auto runs_for = [](const std::string &method, int n) {
        if (method == "random") return RANDOM_RUNS;
        if (method == "nn_end" || method == "nn_any" || method == "greedy_cycle") return n;
        return -1;
    };
    return tsp::run(argc, argv, solve, runs_for);
}
