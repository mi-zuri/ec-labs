"""Lab 4 - candidate moves in local search.

Method: steepest local search from a random solution with the neighborhood that was best in lab 3:
    inter-route  exchange a selected node with an unselected one
    intra-route  edges exchange (2-opt, reverses a segment)
but only candidate moves are evaluated: moves that add at least one candidate edge to the tour.
Edge (a, b) is a candidate edge if b is one of the CANDIDATE_COUNT nearest nodes of a,
"nearest" measured by dist[a][b] + cost[b].

The candidate moves are generated directly (never by filtering the full neighborhood):
for every selected node a and every candidate b of a, the two moves that add edge (a, b).
Every move is evaluated by its delta (change of the objective), never from scratch.

Run i uses seed + i, so it starts from the same random solution as lab 3's steepest LS
(the baseline without candidate moves).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "common", "python"))
import tsp  # noqa: E402

CANDIDATE_COUNT = 10  # nearest nodes of every node that define the candidate edges

NOT_SELECTED = -1  # position of a node that is not in the tour

# Move kinds. A move is a tuple (kind, i, j):
EXCHANGE_SELECTED = 0  # inter-route: tour[i] is replaced by the unselected node j
SWAP_EDGES = 1         # intra-route: edges (tour[i], tour[i+1]) and (tour[j], tour[j+1]) -> reverse tour[i+1..j], i < j

METHODS = ["ls_candidates_steepest_edges_random"]


# ---------------------------------------------------------------------------
# Candidate edges and the starting solution
# ---------------------------------------------------------------------------

def nearest_candidates(instance):
    """For every node a: the CANDIDATE_COUNT other nodes b with the smallest dist[a][b] + cost[b]."""
    candidates = []
    for a in range(instance.n):
        others = [b for b in range(instance.n) if b != a]
        others.sort(key=lambda b: instance.dist[a][b] + instance.cost[b])  # stable sort: ties by node index
        candidates.append(others[:CANDIDATE_COUNT])
    return candidates


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
# Moves: deltas and application
# ---------------------------------------------------------------------------

def exchange_selected_delta(instance, tour, i, new_node):
    """Replace tour[i] with new_node: its two edges and its cost change."""
    dist = instance.dist
    before, old_node, after = tour[i - 1], tour[i], tour[(i + 1) % len(tour)]
    return (instance.cost[new_node] - instance.cost[old_node]
            + dist[before][new_node] + dist[new_node][after]
            - dist[before][old_node] - dist[old_node][after])


def swap_edges_delta(dist, tour, i, j):
    """Remove edges (a, b) = (tour[i], tour[i+1]) and (c, d) = (tour[j], tour[j+1]),
    add (a, c) and (b, d), i < j."""
    a, b = tour[i], tour[i + 1]
    c, d = tour[j], tour[(j + 1) % len(tour)]
    return dist[a][c] + dist[b][d] - dist[a][b] - dist[c][d]


def move_delta(instance, tour, move):
    """Change of the objective if the move were applied (negative = better)."""
    kind, i, j = move
    if kind == EXCHANGE_SELECTED:
        return exchange_selected_delta(instance, tour, i, j)
    return swap_edges_delta(instance.dist, tour, i, j)


def apply_move(tour, position, move):
    kind, i, j = move
    if kind == EXCHANGE_SELECTED:
        position[tour[i]] = NOT_SELECTED
        tour[i] = j
        position[j] = i
    else:
        tour[i + 1:j + 1] = reversed(tour[i + 1:j + 1])
        for p in range(i + 1, j + 1):
            position[tour[p]] = p


# ---------------------------------------------------------------------------
# Candidate moves
# ---------------------------------------------------------------------------

def edges_exchange(i, j):
    """Edges exchange of the edges starting at positions i and j, in either order."""
    return (SWAP_EDGES, min(i, j), max(i, j))


def moves_adding_edge(tour, position, i, b):
    """The two moves that add edge (a, b) to the tour, a = tour[i]. Empty if (a, b) is already in it."""
    k = len(tour)
    successor, predecessor = (i + 1) % k, (i - 1) % k
    j = position[b]

    if j == NOT_SELECTED:
        # b replaces the successor of a (new edge a-b) or the predecessor of a (new edge b-a)
        return [(EXCHANGE_SELECTED, successor, b), (EXCHANGE_SELECTED, predecessor, b)]

    if j == successor or j == predecessor:
        return []  # edge already in the tour

    # remove the edges after a and after b: adds (a, b) and (succ a, succ b)
    # remove the edges before a and before b: adds (pred a, pred b) and (a, b)
    return [edges_exchange(i, j), edges_exchange(predecessor, (j - 1) % k)]


def find_best_candidate_move(instance, tour, position, candidates):
    """Steepest over candidate moves only: return the best improving one (or None)."""
    best_move, best_delta = None, 0
    for i, a in enumerate(tour):
        for b in candidates[a]:
            for move in moves_adding_edge(tour, position, i, b):
                delta = move_delta(instance, tour, move)
                if delta < best_delta:
                    best_move, best_delta = move, delta
    return best_move


# ---------------------------------------------------------------------------
# Local search
# ---------------------------------------------------------------------------

def local_search(instance, tour, candidates, trace):
    """Apply the best candidate move until no candidate move improves (local optimum)."""
    position = tour_positions(instance, tour)
    if trace:
        tsp.emit_step(tour)

    while True:
        move = find_best_candidate_move(instance, tour, position, candidates)
        if move is None:
            return tour

        apply_move(tour, position, move)
        if trace:
            tsp.emit_step(tour)


# ---------------------------------------------------------------------------
# Entry points required by the protocol
# ---------------------------------------------------------------------------

def solve(instance, method, run_index, seed, trace):
    """Run one run of a method and return the resulting tour."""
    rng = tsp.Rng(seed)
    candidates = nearest_candidates(instance)
    tour = random_start(instance, rng)
    return local_search(instance, tour, candidates, trace)


def runs_for(method, n):
    """Number of runs of a method, or None if the method does not exist."""
    if method in METHODS:
        return n  # random starts, as many as in lab 3
    return None


if __name__ == "__main__":
    tsp.run(solve, runs_for)
