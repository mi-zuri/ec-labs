"""Lab 5 - move evaluations from previous iterations (list of improving moves) in local search.

Method: steepest local search from a random solution with the neighborhood that was best in lab 3:
    inter-route  exchange a selected node with an unselected one
    intra-route  edges exchange (2-opt, reverses a segment)
but improving moves are kept in a list LM (best first) and reused in later iterations
instead of evaluating the whole neighborhood in every iteration:

    LM = all improving moves of the solution, sorted from the best
    repeat
        browse LM from the best until an applicable move is found (removing the obsolete ones)
        found     -> apply it
        not found -> evaluate all moves again into LM, stop if none is applicable

A move is stored by the nodes of the edges it removes, not by positions (positions change).
When browsing LM, a stored move is in one of 3 situations:
    a removed edge is no longer in the tour                      -> obsolete, remove it from LM
    the removed edges are in the tour, but one of them reversed  -> not applicable now, keep it
    the removed edges are in the tour in the same relative
    direction (both as stored or both reversed)                  -> apply it and remove it from LM
That is why, when evaluating, both relative directions of every pair of edges are stored.

Run i uses seed + i, so it starts from the same random solution as lab 3's steepest LS
(the baseline without the list of moves).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "common", "python"))
import tsp  # noqa: E402

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

METHODS = ["ls_move_list_steepest_edges_random"]


# ---------------------------------------------------------------------------
# Starting solution
# ---------------------------------------------------------------------------

def random_start(instance, rng):
    """Random k nodes in random order (the same solution as lab 3 for the same seed)."""
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


# ---------------------------------------------------------------------------
# Local search
# ---------------------------------------------------------------------------

def local_search(instance, tour, trace):
    """Apply moves from LM; when LM has no applicable move, evaluate all moves again.
    Stop when a fresh LM has no applicable move (local optimum).

    LM is rebuilt from scratch instead of adding the new moves to it: the moves left in LM
    (not applicable now) are found again by the full evaluation, with the same delta,
    because a delta depends only on the nodes of the move.
    """
    position = tour_positions(instance, tour)
    if trace:
        tsp.emit_step(tour)

    while True:
        moves = evaluate_all_moves(instance, tour, position)
        move = take_applicable_move(moves, tour, position)
        if move is None:
            return tour

        while move is not None:
            apply_move(tour, position, move)
            if trace:
                tsp.emit_step(tour)
            move = take_applicable_move(moves, tour, position)


# ---------------------------------------------------------------------------
# Entry points required by the protocol
# ---------------------------------------------------------------------------

def solve(instance, method, run_index, seed, trace):
    """Run one run of a method and return the resulting tour."""
    rng = tsp.Rng(seed)
    tour = random_start(instance, rng)
    return local_search(instance, tour, trace)


def runs_for(method, n):
    """Number of runs of a method, or None if the method does not exist."""
    if method in METHODS:
        return n  # random starts, as many as in lab 3
    return None


if __name__ == "__main__":
    tsp.run(solve, runs_for)
