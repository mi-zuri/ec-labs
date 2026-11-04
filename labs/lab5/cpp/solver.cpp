// Lab 5 - move evaluations from previous iterations (list of improving moves) in local search.
//
// Method: steepest local search from a random solution with the neighborhood that was best in lab 3:
//   inter-route  exchange a selected node with an unselected one
//   intra-route  edges exchange (2-opt, reverses a segment)
// but improving moves are kept in a list LM (best first) and reused in later iterations
// instead of evaluating the whole neighborhood in every iteration:
//
//   LM = all improving moves of the solution, sorted from the best
//   repeat
//       browse LM from the best until an applicable move is found (removing the obsolete ones)
//       found     -> apply it
//       not found -> evaluate all moves again into LM, stop if none is applicable
//
// A move is stored by the nodes of the edges it removes, not by positions (positions change).
// When browsing LM, a stored move is in one of 3 situations:
//   a removed edge is no longer in the tour                      -> obsolete, remove it from LM
//   the removed edges are in the tour, but one of them reversed  -> not applicable now, keep it
//   the removed edges are in the tour in the same relative
//   direction (both as stored or both reversed)                  -> apply it and remove it from LM
// That is why, when evaluating, both relative directions of every pair of edges are stored.
//
// Run i uses seed + i, so it starts from the same random solution as lab 3's steepest LS
// (the baseline without the list of moves).
#include "../../../common/cpp/tsp.hpp"

#include <algorithm>
#include <numeric>
#include <optional>
#include <set>
#include <tuple>

using tsp::Instance;

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

static const std::set<std::string> METHODS = {"ls_move_list_steepest_edges_random"};

// ---------------------------------------------------------------------------
// Starting solution
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Local search
// ---------------------------------------------------------------------------

// Apply moves from LM; when LM has no applicable move, evaluate all moves again.
// Stop when a fresh LM has no applicable move (local optimum).
//
// LM is rebuilt from scratch instead of adding the new moves to it: the moves left in LM
// (not applicable now) are found again by the full evaluation, with the same delta,
// because a delta depends only on the nodes of the move.
static std::vector<int> local_search(const Instance &instance, std::vector<int> tour, bool trace) {
    std::vector<int> position = tour_positions(instance, tour);
    if (trace) tsp::emit_step(tour);

    while (true) {
        std::vector<Move> moves = evaluate_all_moves(instance, tour, position);
        std::optional<Move> move = take_applicable_move(moves, tour, position);
        if (!move) return tour;

        while (move) {
            apply_move(tour, position, *move);
            if (trace) tsp::emit_step(tour);
            move = take_applicable_move(moves, tour, position);
        }
    }
}

// ---------------------------------------------------------------------------
// Entry points required by the protocol
// ---------------------------------------------------------------------------

// Run one run of a method and return the resulting tour.
static std::vector<int> solve(const Instance &instance, const std::string &, int, uint64_t seed, bool trace) {
    tsp::Rng rng(seed);
    std::vector<int> tour = random_start(instance, rng);
    return local_search(instance, tour, trace);
}

// Number of runs of a method, or -1 if the method does not exist.
static int runs_for(const std::string &method, int n) {
    if (METHODS.count(method)) return n;  // random starts, as many as in lab 3
    return -1;
}

int main(int argc, char **argv) { return tsp::run(argc, argv, solve, runs_for); }
