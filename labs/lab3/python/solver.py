"""Lab 3 - local search.

Methods: ls_<search>_<intra>_<start>
    search  steepest - apply the best move of the whole neighborhood
            greedy   - apply the first improving move, moves browsed in random order
    intra   nodes    - intra-route move exchanges two nodes of the tour
            edges    - intra-route move exchanges two edges (2-opt, reverses a segment)
    start   random   - random solution
            greedy   - greedy cycle, 2-regret + best change (best heuristic of lab 2)

The neighborhood always contains both kinds of moves:
    inter-route  exchange a selected node with an unselected one
    intra-route  nodes or edges exchange (see above)
Every move is evaluated by its delta (change of the objective), never from scratch.

Run i uses seed + i (random start and greedy browsing order),
and the greedy start of run i begins at node i.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "common", "python"))
import tsp  # noqa: E402

# Search variants
STEEPEST = "steepest"
GREEDY = "greedy"

# Starting solutions
RANDOM_START = "random"
GREEDY_START = "greedy"

# Move kinds. A move is a tuple (kind, i, j) of positions:
EXCHANGE_SELECTED = 0  # inter-route: tour[i] <-> unselected[j]
SWAP_NODES = 1         # intra-route: tour[i] <-> tour[j], i < j
SWAP_EDGES = 2         # intra-route: edges (tour[i], tour[i+1]) and (tour[j], tour[j+1]) -> reverse tour[i+1..j], i < j

# method id -> (search variant, intra-route move kind, starting solution)
METHODS = {
    "ls_steepest_nodes_random": (STEEPEST, SWAP_NODES, RANDOM_START),
    "ls_steepest_edges_random": (STEEPEST, SWAP_EDGES, RANDOM_START),
    "ls_greedy_nodes_random": (GREEDY, SWAP_NODES, RANDOM_START),
    "ls_greedy_edges_random": (GREEDY, SWAP_EDGES, RANDOM_START),
    "ls_steepest_nodes_greedy": (STEEPEST, SWAP_NODES, GREEDY_START),
    "ls_steepest_edges_greedy": (STEEPEST, SWAP_EDGES, GREEDY_START),
    "ls_greedy_nodes_greedy": (GREEDY, SWAP_NODES, GREEDY_START),
    "ls_greedy_edges_greedy": (GREEDY, SWAP_EDGES, GREEDY_START),
}


# ---------------------------------------------------------------------------
# Starting solutions
# ---------------------------------------------------------------------------

def random_start(instance, rng):
    """Random k nodes in random order. Returns (tour, unselected)."""
    nodes = list(range(instance.n))
    rng.shuffle(nodes)
    return nodes[:instance.k], nodes[instance.k:]


def greedy_start(instance, start):
    """Greedy cycle with 2-regret + best change (lab 2), from the start node.
    Returns (tour, unselected)."""
    used = [False] * instance.n
    tour = [start]
    used[start] = True

    partner = nearest_unused_node(instance, used, from_node=start)
    tour.append(partner)
    used[partner] = True

    while len(tour) < instance.k:
        node, position = regret_insertion(instance, tour, used)
        tour.insert(position, node)
        used[node] = True

    unselected = [v for v in range(instance.n) if not used[v]]
    return tour, unselected


def nearest_unused_node(instance, used, from_node):
    """Return the unused node u minimizing dist[from_node][u] + cost[u]."""
    best_node, best_value = -1, tsp.INFINITY
    for u in range(instance.n):
        if used[u]:
            continue
        value = instance.dist[from_node][u] + instance.cost[u]
        if value < best_value:
            best_node, best_value = u, value
    return best_node


def regret_insertion(instance, tour, used):
    """Find the unused node with the highest 2-regret - best change, and its cheapest position.

    Returns (node, position).
    """
    dist = instance.dist
    best_node, best_position, best_score = -1, -1, -tsp.INFINITY
    for u in range(instance.n):
        if used[u]:
            continue
        # cheapest and second cheapest length increase over all edges of the cycle
        position, cheapest, second_cheapest = -1, tsp.INFINITY, tsp.INFINITY
        for p in range(len(tour)):
            before, after = tour[p - 1], tour[p]
            increase = dist[before][u] + dist[u][after] - dist[before][after]
            if increase < cheapest:
                position, cheapest, second_cheapest = p, increase, cheapest
            elif increase < second_cheapest:
                second_cheapest = increase

        regret = second_cheapest - cheapest
        best_change = cheapest + instance.cost[u]
        score = regret - best_change
        if score > best_score:
            best_node, best_position, best_score = u, position, score
    return best_node, best_position


# ---------------------------------------------------------------------------
# Moves: deltas and application
# ---------------------------------------------------------------------------

def all_moves(k, unselected_count, intra_kind):
    """Every move of the neighborhood: all inter-route moves, then all intra-route moves."""
    moves = []
    for i in range(k):
        for j in range(unselected_count):
            moves.append((EXCHANGE_SELECTED, i, j))
    for i in range(k):
        for j in range(i + 1, k):
            moves.append((intra_kind, i, j))
    return moves


def exchange_selected_delta(instance, tour, i, new_node):
    """Replace tour[i] with new_node: its two edges and its cost change."""
    dist = instance.dist
    before, old_node, after = tour[i - 1], tour[i], tour[(i + 1) % len(tour)]
    return (instance.cost[new_node] - instance.cost[old_node]
            + dist[before][new_node] + dist[new_node][after]
            - dist[before][old_node] - dist[old_node][after])


def swap_nodes_delta(dist, tour, i, j):
    """Swap the nodes at positions i < j."""
    k = len(tour)
    a, b = tour[i], tour[j]

    if j == i + 1:
        # neighbours: before a b after -> before b a after (edge a-b stays)
        before, after = tour[i - 1], tour[(j + 1) % k]
        return dist[before][b] + dist[a][after] - dist[before][a] - dist[b][after]

    if i == 0 and j == k - 1:
        # neighbours over the closing edge: before b a after -> before a b after
        before, after = tour[j - 1], tour[i + 1]
        return dist[before][a] + dist[b][after] - dist[before][b] - dist[a][after]

    before_a, after_a = tour[i - 1], tour[i + 1]
    before_b, after_b = tour[j - 1], tour[(j + 1) % k]
    return (dist[before_a][b] + dist[b][after_a] + dist[before_b][a] + dist[a][after_b]
            - dist[before_a][a] - dist[a][after_a] - dist[before_b][b] - dist[b][after_b])


def swap_edges_delta(dist, tour, i, j):
    """Remove edges (a, b) = (tour[i], tour[i+1]) and (c, d) = (tour[j], tour[j+1]),
    add (a, c) and (b, d), i < j."""
    a, b = tour[i], tour[i + 1]
    c, d = tour[j], tour[(j + 1) % len(tour)]
    return dist[a][c] + dist[b][d] - dist[a][b] - dist[c][d]


def move_delta(instance, tour, unselected, move):
    """Change of the objective if the move were applied (negative = better)."""
    kind, i, j = move
    if kind == EXCHANGE_SELECTED:
        return exchange_selected_delta(instance, tour, i, unselected[j])
    if kind == SWAP_NODES:
        return swap_nodes_delta(instance.dist, tour, i, j)
    return swap_edges_delta(instance.dist, tour, i, j)


def apply_move(tour, unselected, move):
    kind, i, j = move
    if kind == EXCHANGE_SELECTED:
        tour[i], unselected[j] = unselected[j], tour[i]
    elif kind == SWAP_NODES:
        tour[i], tour[j] = tour[j], tour[i]
    else:
        tour[i + 1:j + 1] = reversed(tour[i + 1:j + 1])


# ---------------------------------------------------------------------------
# Local search
# ---------------------------------------------------------------------------

def find_best_move(instance, tour, unselected, moves):
    """Steepest: evaluate every move, return the best improving one (or None)."""
    best_move, best_delta = None, 0
    for move in moves:
        delta = move_delta(instance, tour, unselected, move)
        if delta < best_delta:
            best_move, best_delta = move, delta
    return best_move


def find_first_improving_move(instance, tour, unselected, moves, rng):
    """Greedy: browse the moves in random order, return the first improving one (or None).

    The order is a lazy Fisher-Yates shuffle: each step draws the next move at random from
    the ones not yet tried, so only the moves actually evaluated cost any work.
    """
    for tried in range(len(moves)):
        pick = tried + rng.below(len(moves) - tried)
        moves[tried], moves[pick] = moves[pick], moves[tried]
        if move_delta(instance, tour, unselected, moves[tried]) < 0:
            return moves[tried]
    return None


def local_search(instance, tour, unselected, search, intra_kind, rng, trace):
    """Apply improving moves until none is left (local optimum)."""
    moves = all_moves(len(tour), len(unselected), intra_kind)
    if trace:
        tsp.emit_step(tour)

    while True:
        if search == STEEPEST:
            move = find_best_move(instance, tour, unselected, moves)
        else:
            move = find_first_improving_move(instance, tour, unselected, moves, rng)
        if move is None:
            return tour

        apply_move(tour, unselected, move)
        if trace:
            tsp.emit_step(tour)


# ---------------------------------------------------------------------------
# Entry points required by the protocol
# ---------------------------------------------------------------------------

def solve(instance, method, run_index, seed, trace):
    """Run one run of a method and return the resulting tour."""
    search, intra_kind, start = METHODS[method]
    rng = tsp.Rng(seed)
    if start == RANDOM_START:
        tour, unselected = random_start(instance, rng)
    else:
        tour, unselected = greedy_start(instance, run_index)
    return local_search(instance, tour, unselected, search, intra_kind, rng, trace)


def runs_for(method, n):
    """Number of runs of a method, or None if the method does not exist."""
    if method in METHODS:
        return n  # 200 random starts / one greedy start per node
    return None


if __name__ == "__main__":
    tsp.run(solve, runs_for)
