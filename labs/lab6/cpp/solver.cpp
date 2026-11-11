// Lab 6 - multiple start local search (MSLS) and iterated local search (ILS).
//
// Both use the local search of lab 5: steepest, a selected node <-> unselected node exchange
// plus edges exchange (2-opt), with a list of improving moves (LM). In this file "local search"
// always means one full run of it, from a solution to its local optimum.
//
// MSLS: the best of 400 local searches, each from a new random solution.
//
//   best = none
//   repeat 400 times
//       x = local_search(random solution)
//       if x is better than best: best = x
//
// ILS: one random solution, then repeatedly kick the current local optimum out of its basin
// (perturbation) and descend again. Only a strictly better local optimum becomes the current one.
// It stops after the average running time of MSLS (given by the server as --time-limit-ms),
// so both methods get the same time.
//
//   x = local_search(random solution)
//   repeat until the time is up
//       y = local_search(perturb(x))
//       if y is better than x: x = y
//
// Perturbation (designed for this problem: a cycle through half of the nodes):
//   1. double bridge: cut the tour into 4 parts A B C D at 3 random places and reconnect
//      them as A C B D. It changes 3 edges at once in a way a single 2-opt move cannot undo.
//   2. exchange NODE_EXCHANGES random selected nodes with random unselected ones, which changes
//      the set of selected nodes (the double bridge only changes the order).
//
// Run i uses seed + i; its first random solution is the same as in lab 5's run i.
#include "../../../common/cpp/tsp.hpp"

#include <algorithm>
#include <climits>
#include <numeric>
#include <optional>
#include <tuple>

using tsp::Instance;

static const std::string MSLS = "msls";
static const std::string ILS = "ils";

static const int RUNS = 20;                  // runs of each method
static const int MSLS_LOCAL_SEARCHES = 400;  // local searches in one run of MSLS
static const int NODE_EXCHANGES = 3;         // perturbation: selected nodes replaced with random unselected ones

static const int NOT_SELECTED = -1;  // position of a node that is not in the tour

// Move kinds. A move is (delta, kind, n1, n2, n3, n4) of nodes:
enum class MoveKind {
    ExchangeSelected,  // inter-route: (before, removed, after, added)
                       // removes edges (before, removed), (removed, after); added takes the place of removed
    SwapEdges,         // intra-route: (a, b, c, d) removes edges (a, b), (c, d) and adds (a, c), (b, d)
};

struct Move {
    long long delta;
    MoveKind kind;
    int n1, n2, n3, n4;

    // Order of moves: by delta, ties by kind, then by nodes (the same order as Python's tuples).
    bool operator<(const Move &other) const {
        return std::tie(delta, kind, n1, n2, n3, n4) < std::tie(other.delta, other.kind, other.n1, other.n2, other.n3, other.n4);
    }
};

// Situations of a stored move in the current tour
enum class MoveStatus {
    Obsolete,          // a removed edge (or the added node) is gone: remove the move from LM
    NotApplicableNow,  // removed edges are there, but in a different relative direction: keep it
    Applicable,        // removed edges are there in the same relative direction: apply it
};

// ---------------------------------------------------------------------------
// Local search with a list of improving moves (lab 5, unchanged)
// ---------------------------------------------------------------------------

// Random k nodes in random order.
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
// Evaluating all moves (fills LM)
// ---------------------------------------------------------------------------

// Replace node removed (between before and after) with node added.
static long long exchange_selected_delta(const Instance &instance, int before, int removed, int after, int added) {
    return static_cast<long long>(instance.cost[added]) - instance.cost[removed] + instance.d(before, added) +
           instance.d(added, after) - instance.d(before, removed) - instance.d(removed, after);
}

// Remove edges (a, b) and (c, d), add (a, c) and (b, d).
static long long swap_edges_delta(const Instance &instance, int a, int b, int c, int d) {
    return static_cast<long long>(instance.d(a, c)) + instance.d(b, d) - instance.d(a, b) - instance.d(c, d);
}

// LM: every improving move of the tour, sorted from the worst: the best move is at the end, so removing
// the moves browsed first (the best ones) shifts only the few moves after them.
//
// For every pair of tour edges a -> b, c -> d two moves are stored:
//   same direction      remove (a, b), (c, d), add (a, c), (b, d)   applicable now
//   inverted direction  remove (a, b), (d, c), add (a, d), (b, c)   applicable after one edge gets reversed
static std::vector<Move> evaluate_all_moves(const Instance &instance, const std::vector<int> &tour,
                                            const std::vector<int> &position) {
    int k = static_cast<int>(tour.size());
    std::vector<Move> moves;
    auto add_if_improving = [&](long long delta, MoveKind kind, int n1, int n2, int n3, int n4) {
        if (delta < 0) moves.push_back({delta, kind, n1, n2, n3, n4});
    };

    for (int i = 0; i < k; i++) {
        int before = tour[(i - 1 + k) % k], removed = tour[i], after = tour[(i + 1) % k];
        for (int added = 0; added < instance.n; added++) {
            if (position[added] != NOT_SELECTED) continue;
            add_if_improving(exchange_selected_delta(instance, before, removed, after, added), MoveKind::ExchangeSelected,
                             before, removed, after, added);
        }
    }

    for (int i = 0; i < k; i++) {
        for (int j = i + 2; j < k; j++) {
            if (i == 0 && j == k - 1) continue;  // the edges share node tour[0]
            int a = tour[i], b = tour[i + 1];
            int c = tour[j], d = tour[(j + 1) % k];
            add_if_improving(swap_edges_delta(instance, a, b, c, d), MoveKind::SwapEdges, a, b, c, d);
            add_if_improving(swap_edges_delta(instance, a, b, d, c), MoveKind::SwapEdges, a, b, d, c);
        }
    }

    std::sort(moves.rbegin(), moves.rend());
    return moves;
}

// ---------------------------------------------------------------------------
// Browsing LM: is a stored move still valid, applicable, and applying it
// ---------------------------------------------------------------------------

// +1 if the tour goes x -> y, -1 if it goes y -> x, 0 if (x, y) is not a tour edge.
static int edge_direction(const std::vector<int> &tour, const std::vector<int> &position, int x, int y) {
    if (position[x] == NOT_SELECTED || position[y] == NOT_SELECTED) return 0;
    int k = static_cast<int>(tour.size());
    if (tour[(position[x] + 1) % k] == y) return 1;
    if (tour[(position[x] - 1 + k) % k] == y) return -1;
    return 0;
}

// Obsolete, NotApplicableNow or Applicable (see the comment at the top).
static MoveStatus move_status(const std::vector<int> &tour, const std::vector<int> &position, const Move &move) {
    int first_direction = edge_direction(tour, position, move.n1, move.n2);
    if (move.kind == MoveKind::ExchangeSelected) {
        int removed = move.n2, after = move.n3, added = move.n4;
        int second_direction = edge_direction(tour, position, removed, after);
        if (first_direction == 0 || second_direction == 0 || position[added] != NOT_SELECTED) return MoveStatus::Obsolete;
        return MoveStatus::Applicable;  // before-removed-after are consecutive, so both edges have the same direction
    }

    int second_direction = edge_direction(tour, position, move.n3, move.n4);
    if (first_direction == 0 || second_direction == 0) return MoveStatus::Obsolete;
    if (first_direction != second_direction) return MoveStatus::NotApplicableNow;
    return MoveStatus::Applicable;
}

// Browse LM from the best move: remove obsolete moves, skip the not applicable ones,
// remove and return the first applicable move (or nothing).
static std::optional<Move> take_applicable_move(std::vector<Move> &moves, const std::vector<int> &tour,
                                                const std::vector<int> &position) {
    for (int index = static_cast<int>(moves.size()) - 1; index >= 0; index--) {
        MoveStatus status = move_status(tour, position, moves[index]);
        if (status == MoveStatus::Obsolete) {
            moves.erase(moves.begin() + index);
        } else if (status == MoveStatus::Applicable) {
            Move move = moves[index];
            moves.erase(moves.begin() + index);
            return move;
        }
    }
    return std::nullopt;
}

static void apply_move(std::vector<int> &tour, std::vector<int> &position, const Move &move) {
    if (move.kind == MoveKind::ExchangeSelected) {
        int removed = move.n2, added = move.n4;
        int i = position[removed];
        position[removed] = NOT_SELECTED;
        tour[i] = added;
        position[added] = i;
        return;
    }

    int a = move.n1, b = move.n2, c = move.n3, d = move.n4;
    int i, j;
    if (edge_direction(tour, position, a, b) == 1) {
        i = position[a];  // tour: a -> b ... c -> d
        j = position[c];
    } else {
        i = position[b];  // tour: b -> a ... d -> c (both reversed)
        j = position[d];
    }
    if (i > j) std::swap(i, j);
    // remove edges (tour[i], tour[i+1]) and (tour[j], tour[j+1]): reverse tour[i+1..j]
    std::reverse(tour.begin() + i + 1, tour.begin() + j + 1);
    for (int p = i + 1; p <= j; p++) position[tour[p]] = p;
}

// Apply moves from LM; when LM has no applicable move, evaluate all moves again.
// Stop when a fresh LM has no applicable move (local optimum).
//
// LM is rebuilt from scratch instead of adding the new moves to it: the moves left in LM
// (not applicable now) are found again by the full evaluation, with the same delta,
// because a delta depends only on the nodes of the move.
static std::vector<int> local_search(const Instance &instance, std::vector<int> tour) {
    std::vector<int> position = tour_positions(instance, tour);

    while (true) {
        std::vector<Move> moves = evaluate_all_moves(instance, tour, position);
        std::optional<Move> move = take_applicable_move(moves, tour, position);
        if (!move) return tour;

        while (move) {
            apply_move(tour, position, *move);
            move = take_applicable_move(moves, tour, position);
        }
    }
}

static long long objective(const Instance &instance, const std::vector<int> &tour) {
    auto [cycle_length, total_cost] = instance.evaluate(tour);
    return cycle_length + total_cost;
}

// ---------------------------------------------------------------------------
// Multiple start local search
// ---------------------------------------------------------------------------

// The best of MSLS_LOCAL_SEARCHES local searches from random solutions.
static std::vector<int> multiple_start_local_search(const Instance &instance, tsp::Rng &rng, bool trace) {
    std::vector<int> best_tour;
    long long best_objective = LLONG_MAX;
    for (int i = 0; i < MSLS_LOCAL_SEARCHES; i++) {
        std::vector<int> tour = local_search(instance, random_start(instance, rng));
        if (trace) tsp::emit_step(tour);
        long long tour_objective = objective(instance, tour);
        if (tour_objective < best_objective) {
            best_tour = tour;
            best_objective = tour_objective;
        }
    }

    tsp::report_iterations(MSLS_LOCAL_SEARCHES);
    return best_tour;
}

// ---------------------------------------------------------------------------
// Iterated local search
// ---------------------------------------------------------------------------

// Cut the tour into 4 non-empty parts A B C D and reconnect them as A C B D.
static std::vector<int> double_bridge(const std::vector<int> &tour, tsp::Rng &rng) {
    int k = static_cast<int>(tour.size());
    std::vector<int> cuts;  // 3 different cut positions in 1 .. k-1
    while (cuts.size() < 3) {
        int cut = 1 + rng.below(k - 1);
        if (std::find(cuts.begin(), cuts.end(), cut) == cuts.end()) cuts.push_back(cut);
    }
    std::sort(cuts.begin(), cuts.end());
    auto first = tour.begin() + cuts[0], second = tour.begin() + cuts[1], third = tour.begin() + cuts[2];

    std::vector<int> result(tour.begin(), first);    // A
    result.insert(result.end(), second, third);      // C
    result.insert(result.end(), first, second);      // B
    result.insert(result.end(), third, tour.end());  // D
    return result;
}

// Replace NODE_EXCHANGES random selected nodes with random unselected ones (in place).
static void exchange_random_nodes(const Instance &instance, std::vector<int> &tour, tsp::Rng &rng) {
    std::vector<bool> selected(instance.n, false);
    for (int node : tour) selected[node] = true;

    for (int e = 0; e < NODE_EXCHANGES; e++) {
        int i = rng.below(static_cast<int>(tour.size()));
        int added = rng.below(instance.n);
        while (selected[added]) added = rng.below(instance.n);
        selected[tour[i]] = false;
        tour[i] = added;
        selected[added] = true;
    }
}

// A new tour: double bridge, then exchange of a few selected nodes with unselected ones.
static std::vector<int> perturb(const Instance &instance, const std::vector<int> &tour, tsp::Rng &rng) {
    std::vector<int> perturbed = double_bridge(tour, rng);
    exchange_random_nodes(instance, perturbed, rng);
    return perturbed;
}

// Perturb the current local optimum and run local search on it, until the time limit.
// Keep the result only if it is strictly better than the current local optimum.
static std::vector<int> iterated_local_search(const Instance &instance, tsp::Rng &rng, bool trace) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(tsp::time_limit());

    std::vector<int> current = local_search(instance, random_start(instance, rng));
    long long current_objective = objective(instance, current);
    long long local_searches = 1;
    if (trace) tsp::emit_step(current);

    while (std::chrono::steady_clock::now() < deadline) {
        std::vector<int> candidate = perturb(instance, current, rng);
        if (trace) tsp::emit_step(candidate);
        candidate = local_search(instance, candidate);
        local_searches++;
        if (trace) tsp::emit_step(candidate);

        long long candidate_objective = objective(instance, candidate);
        if (candidate_objective < current_objective) {
            current = candidate;
            current_objective = candidate_objective;
        }
    }

    tsp::report_iterations(local_searches);
    return current;
}

// ---------------------------------------------------------------------------
// Entry points required by the protocol
// ---------------------------------------------------------------------------

// Run one run of a method and return the resulting tour.
static std::vector<int> solve(const Instance &instance, const std::string &method, int, uint64_t seed, bool trace) {
    tsp::Rng rng(seed);
    if (method == MSLS) return multiple_start_local_search(instance, rng, trace);
    return iterated_local_search(instance, rng, trace);
}

// Number of runs of a method, or -1 if the method does not exist.
static int runs_for(const std::string &method, int) {
    if (method == MSLS || method == ILS) return RUNS;
    return -1;
}

int main(int argc, char **argv) { return tsp::run(argc, argv, solve, runs_for); }
