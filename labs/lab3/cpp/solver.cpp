// Lab 3 - local search.
//
// Methods: ls_<search>_<intra>_<start>
//   search  steepest - apply the best move of the whole neighborhood
//           greedy   - apply the first improving move, moves browsed in random order
//   intra   nodes    - intra-route move exchanges two nodes of the tour
//           edges    - intra-route move exchanges two edges (2-opt, reverses a segment)
//   start   random   - random solution
//           greedy   - greedy cycle, 2-regret + best change (best heuristic of lab 2)
//
// The neighborhood always contains both kinds of moves:
//   inter-route  exchange a selected node with an unselected one
//   intra-route  nodes or edges exchange (see above)
// Every move is evaluated by its delta (change of the objective), never from scratch.
//
// Run i uses seed + i (random start and greedy browsing order),
// and the greedy start of run i begins at node i.
#include "../../../common/cpp/tsp.hpp"

#include <algorithm>
#include <climits>
#include <map>
#include <numeric>
#include <optional>

using tsp::Instance;

enum class Search { Steepest, Greedy };
enum class Start { Random, Greedy };

// Move kinds. A move is (kind, i, j) of positions:
enum class MoveKind {
    ExchangeSelected,  // inter-route: tour[i] <-> unselected[j]
    SwapNodes,         // intra-route: tour[i] <-> tour[j], i < j
    SwapEdges,         // intra-route: edges (tour[i], tour[i+1]) and (tour[j], tour[j+1]) -> reverse tour[i+1..j], i < j
};

struct Move {
    MoveKind kind;
    int i, j;
};

struct Method {
    Search search;
    MoveKind intra_kind;
    Start start;
};

static const std::map<std::string, Method> METHODS = {
    {"ls_steepest_nodes_random", {Search::Steepest, MoveKind::SwapNodes, Start::Random}},
    {"ls_steepest_edges_random", {Search::Steepest, MoveKind::SwapEdges, Start::Random}},
    {"ls_greedy_nodes_random", {Search::Greedy, MoveKind::SwapNodes, Start::Random}},
    {"ls_greedy_edges_random", {Search::Greedy, MoveKind::SwapEdges, Start::Random}},
    {"ls_steepest_nodes_greedy", {Search::Steepest, MoveKind::SwapNodes, Start::Greedy}},
    {"ls_steepest_edges_greedy", {Search::Steepest, MoveKind::SwapEdges, Start::Greedy}},
    {"ls_greedy_nodes_greedy", {Search::Greedy, MoveKind::SwapNodes, Start::Greedy}},
    {"ls_greedy_edges_greedy", {Search::Greedy, MoveKind::SwapEdges, Start::Greedy}},
};

// A complete solution: k nodes of the cycle and the n - k other nodes.
struct Solution {
    std::vector<int> tour;
    std::vector<int> unselected;
};

// ---------------------------------------------------------------------------
// Starting solutions
// ---------------------------------------------------------------------------

// Random k nodes in random order.
static Solution random_start(const Instance &instance, tsp::Rng &rng) {
    std::vector<int> nodes(instance.n);
    std::iota(nodes.begin(), nodes.end(), 0);
    rng.shuffle(nodes);
    return {std::vector<int>(nodes.begin(), nodes.begin() + instance.k),
            std::vector<int>(nodes.begin() + instance.k, nodes.end())};
}

// Return the unused node u minimizing d(from_node, u) + cost[u].
static int nearest_unused_node(const Instance &instance, const std::vector<bool> &used, int from_node) {
    int best_node = -1;
    long long best_value = LLONG_MAX;
    for (int u = 0; u < instance.n; u++) {
        if (used[u]) continue;
        long long value = instance.d(from_node, u) + instance.cost[u];
        if (value < best_value) {
            best_node = u;
            best_value = value;
        }
    }
    return best_node;
}

// The node to add next and where to put it.
struct Insertion {
    int node;
    int position;
};

// Find the unused node with the highest 2-regret - best change, and its cheapest position.
static Insertion regret_insertion(const Instance &instance, const std::vector<int> &tour,
                                  const std::vector<bool> &used) {
    int length = static_cast<int>(tour.size());
    Insertion best = {-1, -1};
    long long best_score = LLONG_MIN;

    for (int u = 0; u < instance.n; u++) {
        if (used[u]) continue;
        // cheapest and second cheapest length increase over all edges of the cycle
        int position = -1;
        long long cheapest = LLONG_MAX, second_cheapest = LLONG_MAX;
        for (int p = 0; p < length; p++) {
            int before = tour[(p - 1 + length) % length], after = tour[p];
            long long increase = instance.d(before, u) + instance.d(u, after) - instance.d(before, after);
            if (increase < cheapest) {
                position = p;
                second_cheapest = cheapest;
                cheapest = increase;
            } else if (increase < second_cheapest) {
                second_cheapest = increase;
            }
        }

        long long regret = second_cheapest - cheapest;
        long long best_change = cheapest + instance.cost[u];
        long long score = regret - best_change;
        if (score > best_score) {
            best = {u, position};
            best_score = score;
        }
    }
    return best;
}

// Greedy cycle with 2-regret + best change (lab 2), from the start node.
static Solution greedy_start(const Instance &instance, int start) {
    std::vector<bool> used(instance.n, false);
    std::vector<int> tour = {start};
    used[start] = true;

    int partner = nearest_unused_node(instance, used, start);
    tour.push_back(partner);
    used[partner] = true;

    while (static_cast<int>(tour.size()) < instance.k) {
        Insertion next = regret_insertion(instance, tour, used);
        tour.insert(tour.begin() + next.position, next.node);
        used[next.node] = true;
    }

    std::vector<int> unselected;
    for (int v = 0; v < instance.n; v++) {
        if (!used[v]) unselected.push_back(v);
    }
    return {tour, unselected};
}

// ---------------------------------------------------------------------------
// Moves: deltas and application
// ---------------------------------------------------------------------------

// Every move of the neighborhood: all inter-route moves, then all intra-route moves.
static std::vector<Move> all_moves(int k, int unselected_count, MoveKind intra_kind) {
    std::vector<Move> moves;
    for (int i = 0; i < k; i++) {
        for (int j = 0; j < unselected_count; j++) moves.push_back({MoveKind::ExchangeSelected, i, j});
    }
    for (int i = 0; i < k; i++) {
        for (int j = i + 1; j < k; j++) moves.push_back({intra_kind, i, j});
    }
    return moves;
}

// Replace tour[i] with new_node: its two edges and its cost change.
static long long exchange_selected_delta(const Instance &instance, const std::vector<int> &tour, int i,
                                         int new_node) {
    int k = static_cast<int>(tour.size());
    int before = tour[(i - 1 + k) % k], old_node = tour[i], after = tour[(i + 1) % k];
    return static_cast<long long>(instance.cost[new_node]) - instance.cost[old_node] +
           instance.d(before, new_node) + instance.d(new_node, after) - instance.d(before, old_node) -
           instance.d(old_node, after);
}

// Swap the nodes at positions i < j.
static long long swap_nodes_delta(const Instance &instance, const std::vector<int> &tour, int i, int j) {
    int k = static_cast<int>(tour.size());
    int a = tour[i], b = tour[j];

    if (j == i + 1) {
        // neighbours: before a b after -> before b a after (edge a-b stays)
        int before = tour[(i - 1 + k) % k], after = tour[(j + 1) % k];
        return instance.d(before, b) + instance.d(a, after) - instance.d(before, a) - instance.d(b, after);
    }

    if (i == 0 && j == k - 1) {
        // neighbours over the closing edge: before b a after -> before a b after
        int before = tour[j - 1], after = tour[i + 1];
        return instance.d(before, a) + instance.d(b, after) - instance.d(before, b) - instance.d(a, after);
    }

    int before_a = tour[(i - 1 + k) % k], after_a = tour[i + 1];
    int before_b = tour[j - 1], after_b = tour[(j + 1) % k];
    return instance.d(before_a, b) + instance.d(b, after_a) + instance.d(before_b, a) + instance.d(a, after_b) -
           instance.d(before_a, a) - instance.d(a, after_a) - instance.d(before_b, b) - instance.d(b, after_b);
}

// Remove edges (a, b) = (tour[i], tour[i+1]) and (c, d) = (tour[j], tour[j+1]),
// add (a, c) and (b, d), i < j.
static long long swap_edges_delta(const Instance &instance, const std::vector<int> &tour, int i, int j) {
    int k = static_cast<int>(tour.size());
    int a = tour[i], b = tour[i + 1];
    int c = tour[j], d = tour[(j + 1) % k];
    return instance.d(a, c) + instance.d(b, d) - instance.d(a, b) - instance.d(c, d);
}

// Change of the objective if the move were applied (negative = better).
static long long move_delta(const Instance &instance, const Solution &solution, const Move &move) {
    switch (move.kind) {
        case MoveKind::ExchangeSelected:
            return exchange_selected_delta(instance, solution.tour, move.i, solution.unselected[move.j]);
        case MoveKind::SwapNodes:
            return swap_nodes_delta(instance, solution.tour, move.i, move.j);
        default:
            return swap_edges_delta(instance, solution.tour, move.i, move.j);
    }
}

static void apply_move(Solution &solution, const Move &move) {
    std::vector<int> &tour = solution.tour;
    switch (move.kind) {
        case MoveKind::ExchangeSelected:
            std::swap(tour[move.i], solution.unselected[move.j]);
            break;
        case MoveKind::SwapNodes:
            std::swap(tour[move.i], tour[move.j]);
            break;
        default:
            std::reverse(tour.begin() + move.i + 1, tour.begin() + move.j + 1);
    }
}

// ---------------------------------------------------------------------------
// Local search
// ---------------------------------------------------------------------------

// Steepest: evaluate every move, return the best improving one (or nothing).
static std::optional<Move> find_best_move(const Instance &instance, const Solution &solution,
                                          const std::vector<Move> &moves) {
    std::optional<Move> best_move;
    long long best_delta = 0;
    for (const Move &move : moves) {
        long long delta = move_delta(instance, solution, move);
        if (delta < best_delta) {
            best_move = move;
            best_delta = delta;
        }
    }
    return best_move;
}

// Greedy: browse the moves in random order, return the first improving one (or nothing).
//
// The order is a lazy Fisher-Yates shuffle: each step draws the next move at random from
// the ones not yet tried, so only the moves actually evaluated cost any work.
static std::optional<Move> find_first_improving_move(const Instance &instance, const Solution &solution,
                                                     std::vector<Move> &moves, tsp::Rng &rng) {
    int count = static_cast<int>(moves.size());
    for (int tried = 0; tried < count; tried++) {
        int pick = tried + rng.below(count - tried);
        std::swap(moves[tried], moves[pick]);
        if (move_delta(instance, solution, moves[tried]) < 0) return moves[tried];
    }
    return std::nullopt;
}

// Apply improving moves until none is left (local optimum).
static std::vector<int> local_search(const Instance &instance, Solution solution, Search search,
                                     MoveKind intra_kind, tsp::Rng &rng, bool trace) {
    std::vector<Move> moves = all_moves(static_cast<int>(solution.tour.size()),
                                        static_cast<int>(solution.unselected.size()), intra_kind);
    if (trace) tsp::emit_step(solution.tour);

    while (true) {
        std::optional<Move> move = search == Search::Steepest
                                       ? find_best_move(instance, solution, moves)
                                       : find_first_improving_move(instance, solution, moves, rng);
        if (!move) return solution.tour;

        apply_move(solution, *move);
        if (trace) tsp::emit_step(solution.tour);
    }
}

// ---------------------------------------------------------------------------
// Entry points required by the protocol
// ---------------------------------------------------------------------------

// Run one run of a method and return the resulting tour.
static std::vector<int> solve(const Instance &instance, const std::string &method, int run, uint64_t seed,
                              bool trace) {
    const Method &chosen = METHODS.at(method);
    tsp::Rng rng(seed);
    Solution start = chosen.start == Start::Random ? random_start(instance, rng) : greedy_start(instance, run);
    return local_search(instance, start, chosen.search, chosen.intra_kind, rng, trace);
}

// Number of runs of a method, or -1 if the method does not exist.
static int runs_for(const std::string &method, int n) {
    if (METHODS.count(method)) return n;  // 200 random starts / one greedy start per node
    return -1;
}

int main(int argc, char **argv) { return tsp::run(argc, argv, solve, runs_for); }
