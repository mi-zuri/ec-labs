// Lab 4 - candidate moves in local search.
//
// Method: steepest local search from a random solution with the neighborhood that was best in lab 3:
//   inter-route  exchange a selected node with an unselected one
//   intra-route  edges exchange (2-opt, reverses a segment)
// but only candidate moves are evaluated: moves that add at least one candidate edge to the tour.
// Edge (a, b) is a candidate edge if b is one of the CANDIDATE_COUNT nearest nodes of a,
// "nearest" measured by d(a, b) + cost[b].
//
// The candidate moves are generated directly (never by filtering the full neighborhood):
// for every selected node a and every candidate b of a, the two moves that add edge (a, b).
// Every move is evaluated by its delta (change of the objective), never from scratch.
//
// Run i uses seed + i, so it starts from the same random solution as lab 3's steepest LS
// (the baseline without candidate moves).
#include "../../../common/cpp/tsp.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <optional>
#include <set>

using tsp::Instance;

static const int CANDIDATE_COUNT = 10;  // nearest nodes of every node that define the candidate edges

static const int NOT_SELECTED = -1;  // position of a node that is not in the tour

// Move kinds. A move is (kind, i, j):
enum class MoveKind {
    ExchangeSelected,  // inter-route: tour[i] is replaced by the unselected node j
    SwapEdges,         // intra-route: edges (tour[i], tour[i+1]) and (tour[j], tour[j+1]) -> reverse tour[i+1..j], i < j
};

struct Move {
    MoveKind kind;
    int i, j;
};

static const std::set<std::string> METHODS = {"ls_candidates_steepest_edges_random"};

// ---------------------------------------------------------------------------
// Candidate edges and the starting solution
// ---------------------------------------------------------------------------

// For every node a: the CANDIDATE_COUNT other nodes b with the smallest d(a, b) + cost[b].
static std::vector<std::vector<int>> nearest_candidates(const Instance &instance) {
    std::vector<std::vector<int>> candidates(instance.n);
    for (int a = 0; a < instance.n; a++) {
        std::vector<int> others;
        for (int b = 0; b < instance.n; b++) {
            if (b != a) others.push_back(b);
        }
        // stable sort: ties by node index
        std::stable_sort(others.begin(), others.end(), [&](int b1, int b2) {
            return instance.d(a, b1) + instance.cost[b1] < instance.d(a, b2) + instance.cost[b2];
        });
        candidates[a].assign(others.begin(), others.begin() + CANDIDATE_COUNT);
    }
    return candidates;
}

// Random k nodes in random order (the same solution as lab 3 for the same seed).
static std::vector<int> random_start(const Instance &instance, tsp::Rng &rng) {
    std::vector<int> nodes(instance.n);
    std::iota(nodes.begin(), nodes.end(), 0);
    rng.shuffle(nodes);
    return std::vector<int>(nodes.begin(), nodes.begin() + instance.k);
}

// position[v] = index of v in the tour, or NOT_SELECTED.
static std::vector<int> tour_positions(const Instance &instance, const std::vector<int> &tour) {
    std::vector<int> position(instance.n, NOT_SELECTED);
    for (int i = 0; i < static_cast<int>(tour.size()); i++) position[tour[i]] = i;
    return position;
}

// ---------------------------------------------------------------------------
// Moves: deltas and application
// ---------------------------------------------------------------------------

// Replace tour[i] with new_node: its two edges and its cost change.
static long long exchange_selected_delta(const Instance &instance, const std::vector<int> &tour, int i,
                                         int new_node) {
    int k = static_cast<int>(tour.size());
    int before = tour[(i - 1 + k) % k], old_node = tour[i], after = tour[(i + 1) % k];
    return static_cast<long long>(instance.cost[new_node]) - instance.cost[old_node] + instance.d(before, new_node) +
           instance.d(new_node, after) - instance.d(before, old_node) - instance.d(old_node, after);
}

// Remove edges (a, b) = (tour[i], tour[i+1]) and (c, d) = (tour[j], tour[j+1]),
// add (a, c) and (b, d), i < j.
static long long swap_edges_delta(const Instance &instance, const std::vector<int> &tour, int i, int j) {
    int k = static_cast<int>(tour.size());
    int a = tour[i], b = tour[i + 1];
    int c = tour[j], d = tour[(j + 1) % k];
    return static_cast<long long>(instance.d(a, c)) + instance.d(b, d) - instance.d(a, b) - instance.d(c, d);
}

// Change of the objective if the move were applied (negative = better).
static long long move_delta(const Instance &instance, const std::vector<int> &tour, const Move &move) {
    if (move.kind == MoveKind::ExchangeSelected) return exchange_selected_delta(instance, tour, move.i, move.j);
    return swap_edges_delta(instance, tour, move.i, move.j);
}

static void apply_move(std::vector<int> &tour, std::vector<int> &position, const Move &move) {
    if (move.kind == MoveKind::ExchangeSelected) {
        position[tour[move.i]] = NOT_SELECTED;
        tour[move.i] = move.j;
        position[move.j] = move.i;
    } else {
        std::reverse(tour.begin() + move.i + 1, tour.begin() + move.j + 1);
        for (int p = move.i + 1; p <= move.j; p++) position[tour[p]] = p;
    }
}

// ---------------------------------------------------------------------------
// Candidate moves
// ---------------------------------------------------------------------------

// Edges exchange of the edges starting at positions i and j, in either order.
static Move edges_exchange(int i, int j) { return {MoveKind::SwapEdges, std::min(i, j), std::max(i, j)}; }

// The two moves that add edge (a, b) to the tour, a = tour[i]. Nothing if (a, b) is already in it.
static std::optional<std::array<Move, 2>> moves_adding_edge(const std::vector<int> &tour, const std::vector<int> &position, int i,
                                           int b) {
    int k = static_cast<int>(tour.size());
    int successor = (i + 1) % k, predecessor = (i - 1 + k) % k;
    int j = position[b];

    if (j == NOT_SELECTED) {
        // b replaces the successor of a (new edge a-b) or the predecessor of a (new edge b-a)
        return std::array<Move, 2>{{{MoveKind::ExchangeSelected, successor, b}, {MoveKind::ExchangeSelected, predecessor, b}}};
    }

    if (j == successor || j == predecessor) return std::nullopt;  // edge already in the tour

    // remove the edges after a and after b: adds (a, b) and (succ a, succ b)
    // remove the edges before a and before b: adds (pred a, pred b) and (a, b)
    return std::array<Move, 2>{edges_exchange(i, j), edges_exchange(predecessor, (j - 1 + k) % k)};
}

// Steepest over candidate moves only: return the best improving one (or nothing).
static std::optional<Move> find_best_candidate_move(const Instance &instance, const std::vector<int> &tour,
                                                    const std::vector<int> &position,
                                                    const std::vector<std::vector<int>> &candidates) {
    std::optional<Move> best_move;
    long long best_delta = 0;
    for (int i = 0; i < static_cast<int>(tour.size()); i++) {
        for (int b : candidates[tour[i]]) {
            std::optional<std::array<Move, 2>> moves = moves_adding_edge(tour, position, i, b);
            if (!moves) continue;
            for (const Move &move : *moves) {
                long long delta = move_delta(instance, tour, move);
                if (delta < best_delta) {
                    best_move = move;
                    best_delta = delta;
                }
            }
        }
    }
    return best_move;
}

// ---------------------------------------------------------------------------
// Local search
// ---------------------------------------------------------------------------

// Apply the best candidate move until no candidate move improves (local optimum).
static std::vector<int> local_search(const Instance &instance, std::vector<int> tour,
                                     const std::vector<std::vector<int>> &candidates, bool trace) {
    std::vector<int> position = tour_positions(instance, tour);
    if (trace) tsp::emit_step(tour);

    while (true) {
        std::optional<Move> move = find_best_candidate_move(instance, tour, position, candidates);
        if (!move) return tour;

        apply_move(tour, position, *move);
        if (trace) tsp::emit_step(tour);
    }
}

// ---------------------------------------------------------------------------
// Entry points required by the protocol
// ---------------------------------------------------------------------------

// Run one run of a method and return the resulting tour.
static std::vector<int> solve(const Instance &instance, const std::string &, int, uint64_t seed, bool trace) {
    tsp::Rng rng(seed);
    std::vector<std::vector<int>> candidates = nearest_candidates(instance);
    std::vector<int> tour = random_start(instance, rng);
    return local_search(instance, tour, candidates, trace);
}

// Number of runs of a method, or -1 if the method does not exist.
static int runs_for(const std::string &method, int n) {
    if (METHODS.count(method)) return n;  // random starts, as many as in lab 3
    return -1;
}

int main(int argc, char **argv) { return tsp::run(argc, argv, solve, runs_for); }
