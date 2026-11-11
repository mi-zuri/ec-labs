"""Lab 6 - multiple start local search (MSLS) and iterated local search (ILS).

Both use the local search of lab 5: steepest, a selected node <-> unselected node exchange
plus edges exchange (2-opt), with a list of improving moves (LM). In this file "local search"
always means one full run of it, from a solution to its local optimum.

MSLS: the best of 400 local searches, each from a new random solution.

    best = none
    repeat 400 times
        x = local_search(random solution)
        if x is better than best: best = x

ILS: one random solution, then repeatedly kick the current local optimum out of its basin
(perturbation) and descend again. Only a strictly better local optimum becomes the current one.
It stops after the average running time of MSLS (given by the server as --time-limit-ms),
so both methods get the same time.

    x = local_search(random solution)
    repeat until the time is up
        y = local_search(perturb(x))
        if y is better than x: x = y

Perturbation (designed for this problem: a cycle through half of the nodes):
    1. double bridge: cut the tour into 4 parts A B C D at 3 random places and reconnect
       them as A C B D. It changes 3 edges at once in a way a single 2-opt move cannot undo.
    2. exchange NODE_EXCHANGES random selected nodes with random unselected ones, which changes
       the set of selected nodes (the double bridge only changes the order).

Run i uses seed + i; its first random solution is the same as in lab 5's run i.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "common", "python"))
import tsp  # noqa: E402

MSLS = "msls"
ILS = "ils"
METHODS = [MSLS, ILS]

RUNS = 20                       # runs of each method
MSLS_LOCAL_SEARCHES = 400       # local searches in one run of MSLS
NODE_EXCHANGES = 3              # perturbation: selected nodes replaced with random unselected ones

NOT_SELECTED = -1  # position of a node that is not in the tour

# Move kinds. A move is a tuple (delta, kind, n1, n2, n3, n4) of nodes:
EXCHANGE_SELECTED = 0  # inter-route: (delta, kind, before, removed, after, added)
#                        removes edges (before, removed), (removed, after); added takes the place of removed
SWAP_EDGES = 1         # intra-route: (delta, kind, a, b, c, d)
#                        removes edges (a, b), (c, d) and adds (a, c), (b, d)
# Tuples compare by delta first (ties: by kind, then nodes).
# LM is sorted from the worst to the best: the best move is at the end, so removing the moves
# browsed first (the best ones) shifts only the few moves after them.

# Situations of a stored move in the current tour
OBSOLETE = 0            # a removed edge (or the added node) is gone: remove the move from LM
NOT_APPLICABLE_NOW = 1  # removed edges are there, but in a different relative direction: keep it
APPLICABLE = 2          # removed edges are there in the same relative direction: apply it

# ---------------------------------------------------------------------------
# Local search with a list of improving moves (lab 5, unchanged)
# ---------------------------------------------------------------------------

def random_start(instance, rng):
    """Random k nodes in random order."""
    nodes = list(range(instance.n))
    rng.shuffle(nodes)
    return nodes[:instance.k]


def tour_positions(instance, tour):
    """position[v] = index of v in the tour, or NOT_SELECTED."""
    position = [NOT_SELECTED] * instance.n
    for i, node in enumerate(tour):
        position[node] = i
    return position


# ---------------------------------------------------------------------------
# Evaluating all moves (fills LM)
# ---------------------------------------------------------------------------

def exchange_selected_delta(instance, before, removed, after, added):
    """Replace node removed (between before and after) with node added."""
    dist = instance.dist
    return (instance.cost[added] - instance.cost[removed]
            + dist[before][added] + dist[added][after]
            - dist[before][removed] - dist[removed][after])


def swap_edges_delta(dist, a, b, c, d):
    """Remove edges (a, b) and (c, d), add (a, c) and (b, d)."""
    return dist[a][c] + dist[b][d] - dist[a][b] - dist[c][d]


def evaluate_all_moves(instance, tour, position):
    """LM: every improving move of the tour, sorted from the worst (the best move is last).

    For every pair of tour edges a -> b, c -> d two moves are stored:
        same direction      remove (a, b), (c, d), add (a, c), (b, d)   applicable now
        inverted direction  remove (a, b), (d, c), add (a, d), (b, c)   applicable after one edge gets reversed
    """
    dist = instance.dist
    k = len(tour)
    moves = []

    for i in range(k):
        before, removed, after = tour[i - 1], tour[i], tour[(i + 1) % k]
        for added in range(instance.n):
            if position[added] != NOT_SELECTED:
                continue
            delta = exchange_selected_delta(instance, before, removed, after, added)
            if delta < 0:
                moves.append((delta, EXCHANGE_SELECTED, before, removed, after, added))

    for i in range(k):
        for j in range(i + 2, k):
            if i == 0 and j == k - 1:
                continue  # the edges share node tour[0]
            a, b = tour[i], tour[i + 1]
            c, d = tour[j], tour[(j + 1) % k]
            delta = swap_edges_delta(dist, a, b, c, d)
            if delta < 0:
                moves.append((delta, SWAP_EDGES, a, b, c, d))
            delta = swap_edges_delta(dist, a, b, d, c)
            if delta < 0:
                moves.append((delta, SWAP_EDGES, a, b, d, c))

    moves.sort(reverse=True)
    return moves


# ---------------------------------------------------------------------------
# Browsing LM: is a stored move still valid, applicable, and applying it
# ---------------------------------------------------------------------------

def edge_direction(tour, position, x, y):
    """+1 if the tour goes x -> y, -1 if it goes y -> x, 0 if (x, y) is not a tour edge."""
    if position[x] == NOT_SELECTED or position[y] == NOT_SELECTED:
        return 0
    k = len(tour)
    if tour[(position[x] + 1) % k] == y:
        return 1
    if tour[position[x] - 1] == y:
        return -1
    return 0


def move_status(tour, position, move):
    """OBSOLETE, NOT_APPLICABLE_NOW or APPLICABLE (see the module docstring)."""
    _, kind, n1, n2, n3, n4 = move
    first_direction = edge_direction(tour, position, n1, n2)
    if kind == EXCHANGE_SELECTED:
        before, removed, after, added = n1, n2, n3, n4
        second_direction = edge_direction(tour, position, removed, after)
        if first_direction == 0 or second_direction == 0 or position[added] != NOT_SELECTED:
            return OBSOLETE
        return APPLICABLE  # before-removed-after are consecutive, so both edges have the same direction

    second_direction = edge_direction(tour, position, n3, n4)
    if first_direction == 0 or second_direction == 0:
        return OBSOLETE
    if first_direction != second_direction:
        return NOT_APPLICABLE_NOW
    return APPLICABLE


def take_applicable_move(moves, tour, position):
    """Browse LM from the best move: remove obsolete moves, skip the not applicable ones,
    remove and return the first applicable move (or None)."""
    for index in range(len(moves) - 1, -1, -1):
        status = move_status(tour, position, moves[index])
        if status == OBSOLETE:
            del moves[index]
        elif status == APPLICABLE:
            return moves.pop(index)
    return None


def apply_move(tour, position, move):
    _, kind, n1, n2, n3, n4 = move
    if kind == EXCHANGE_SELECTED:
        removed, added = n2, n4
        i = position[removed]
        position[removed] = NOT_SELECTED
        tour[i] = added
        position[added] = i
        return

    a, b, c, d = n1, n2, n3, n4
    if edge_direction(tour, position, a, b) == 1:
        i, j = position[a], position[c]  # tour: a -> b ... c -> d
    else:
        i, j = position[b], position[d]  # tour: b -> a ... d -> c (both reversed)
    i, j = min(i, j), max(i, j)
    # remove edges (tour[i], tour[i+1]) and (tour[j], tour[j+1]): reverse tour[i+1..j]
    tour[i + 1:j + 1] = reversed(tour[i + 1:j + 1])
    for p in range(i + 1, j + 1):
        position[tour[p]] = p



def local_search(instance, tour):
    """Apply moves from LM; when LM has no applicable move, evaluate all moves again.
    Stop when a fresh LM has no applicable move (local optimum). Changes tour in place and returns it.

    LM is rebuilt from scratch instead of adding the new moves to it: the moves left in LM
    (not applicable now) are found again by the full evaluation, with the same delta,
    because a delta depends only on the nodes of the move.
    """
    position = tour_positions(instance, tour)

    while True:
        moves = evaluate_all_moves(instance, tour, position)
        move = take_applicable_move(moves, tour, position)
        if move is None:
            return tour

        while move is not None:
            apply_move(tour, position, move)
            move = take_applicable_move(moves, tour, position)


def objective(instance, tour):
    length, cost = instance.evaluate(tour)
    return length + cost


# ---------------------------------------------------------------------------
# Multiple start local search
# ---------------------------------------------------------------------------

def multiple_start_local_search(instance, rng, trace):
    """The best of MSLS_LOCAL_SEARCHES local searches from random solutions."""
    best_tour, best_objective = None, tsp.INFINITY
    for _ in range(MSLS_LOCAL_SEARCHES):
        tour = local_search(instance, random_start(instance, rng))
        if trace:
            tsp.emit_step(tour)
        tour_objective = objective(instance, tour)
        if tour_objective < best_objective:
            best_tour, best_objective = tour, tour_objective

    tsp.report_iterations(MSLS_LOCAL_SEARCHES)
    return best_tour


# ---------------------------------------------------------------------------
# Iterated local search
# ---------------------------------------------------------------------------

def double_bridge(tour, rng):
    """Cut the tour into 4 non-empty parts A B C D and reconnect them as A C B D."""
    k = len(tour)
    cuts = []  # 3 different cut positions in 1 .. k-1
    while len(cuts) < 3:
        cut = 1 + rng.below(k - 1)
        if cut not in cuts:
            cuts.append(cut)
    first, second, third = sorted(cuts)
    return tour[:first] + tour[second:third] + tour[first:second] + tour[third:]


def exchange_random_nodes(instance, tour, rng):
    """Replace NODE_EXCHANGES random selected nodes with random unselected ones (in place)."""
    selected = [False] * instance.n
    for node in tour:
        selected[node] = True

    for _ in range(NODE_EXCHANGES):
        i = rng.below(len(tour))
        added = rng.below(instance.n)
        while selected[added]:
            added = rng.below(instance.n)
        selected[tour[i]] = False
        tour[i] = added
        selected[added] = True


def perturb(instance, tour, rng):
    """A new tour: double bridge, then exchange of a few selected nodes with unselected ones."""
    perturbed = double_bridge(tour, rng)
    exchange_random_nodes(instance, perturbed, rng)
    return perturbed


def iterated_local_search(instance, rng, trace):
    """Perturb the current local optimum and run local search on it, until the time limit.
    Keep the result only if it is strictly better than the current local optimum."""
    deadline = time.perf_counter_ns() + tsp.time_limit()

    current = local_search(instance, random_start(instance, rng))
    current_objective = objective(instance, current)
    local_searches = 1
    if trace:
        tsp.emit_step(current)

    while time.perf_counter_ns() < deadline:
        candidate = perturb(instance, current, rng)
        if trace:
            tsp.emit_step(candidate)
        local_search(instance, candidate)
        local_searches += 1
        if trace:
            tsp.emit_step(candidate)

        candidate_objective = objective(instance, candidate)
        if candidate_objective < current_objective:
            current, current_objective = candidate, candidate_objective

    tsp.report_iterations(local_searches)
    return current


# ---------------------------------------------------------------------------
# Entry points required by the protocol
# ---------------------------------------------------------------------------

def solve(instance, method, run_index, seed, trace):
    """Run one run of a method and return the resulting tour."""
    rng = tsp.Rng(seed)
    if method == MSLS:
        return multiple_start_local_search(instance, rng, trace)
    return iterated_local_search(instance, rng, trace)


def runs_for(method, n):
    """Number of runs of a method, or None if the method does not exist."""
    if method in METHODS:
        return RUNS
    return None


if __name__ == "__main__":
    tsp.run(solve, runs_for)
