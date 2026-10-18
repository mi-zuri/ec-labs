"""Lab 2 - greedy regret heuristics.

Methods:
    nn_regret     - nearest neighbor (any position), 2-regret
    nn_weighted   - nearest neighbor (any position), 2-regret + best change
    gc_regret     - greedy cycle, 2-regret
    gc_weighted   - greedy cycle, 2-regret + best change

For every unused node:
    regret      = (second cheapest length increase) - (cheapest length increase)
    best change = (cheapest length increase) + (cost of the node)
Every step inserts the node with the highest
regret_weight * regret - change_weight * best change at its cheapest position.

All methods start from a given node, and run i starts from node i.
On ties the first candidate found wins (lowest node index, then lowest position).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "common", "python"))
import tsp  # noqa: E402

# (regret weight, best change weight)
PURE_REGRET = (1, 0)
EQUAL_WEIGHTS = (1, 1)


# ---------------------------------------------------------------------------
# Building blocks shared by the methods
# ---------------------------------------------------------------------------

def start_tour(start, used, trace):
    """Begin a tour containing only the start node."""
    used[start] = True
    tour = [start]
    if trace:
        tsp.emit_step(tour)
    return tour


def add_node(tour, used, position, node, trace):
    """Insert `node` into `tour` at index `position`."""
    tour.insert(position, node)
    used[node] = True
    if trace:
        tsp.emit_step(tour)


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


def path_insertion_increases(instance, tour, node):
    """Length increase for every insertion index p of the open path
    (p = 0 before the first node, p = len(tour) after the last one)."""
    dist = instance.dist
    increases = [dist[node][tour[0]]]
    for p in range(1, len(tour)):
        before, after = tour[p - 1], tour[p]
        increases.append(dist[before][node] + dist[node][after] - dist[before][after])
    increases.append(dist[tour[-1]][node])
    return increases


def cycle_insertion_increases(instance, tour, node):
    """Length increase for every insertion index p of the closed cycle,
    i.e. between tour[p-1] and tour[p] (p = 0 is the closing edge)."""
    dist = instance.dist
    increases = []
    for p in range(len(tour)):
        before, after = tour[p - 1], tour[p]
        increases.append(dist[before][node] + dist[node][after] - dist[before][after])
    return increases


def two_cheapest(increases):
    """Return (position of the cheapest, cheapest, second cheapest)."""
    best_position, best, second = -1, tsp.INFINITY, tsp.INFINITY
    for position, increase in enumerate(increases):
        if increase < best:
            best_position, best, second = position, increase, best
        elif increase < second:
            second = increase
    return best_position, best, second


def regret_insertion(instance, tour, used, insertion_increases, weights):
    """Find the unused node with the highest score and its cheapest position.

    Returns (node, position).
    """
    regret_weight, change_weight = weights
    best_node, best_position, best_score = -1, -1, -tsp.INFINITY
    for u in range(instance.n):
        if used[u]:
            continue
        position, cheapest, second_cheapest = two_cheapest(insertion_increases(instance, tour, u))
        regret = second_cheapest - cheapest
        best_change = cheapest + instance.cost[u]
        score = regret_weight * regret - change_weight * best_change
        if score > best_score:
            best_node, best_position, best_score = u, position, score
    return best_node, best_position


# ---------------------------------------------------------------------------
# Methods
# ---------------------------------------------------------------------------

def nearest_neighbor_regret(instance, start, weights, trace):
    """Grow a path from the start node, inserting nodes at any position."""
    used = [False] * instance.n
    tour = start_tour(start, used, trace)

    while len(tour) < instance.k:
        node, position = regret_insertion(instance, tour, used, path_insertion_increases, weights)
        add_node(tour, used, position, node, trace)
    return tour


def greedy_cycle_regret(instance, start, weights, trace):
    """Start a cycle with the start node and its best partner, then keep
    inserting nodes into the edges of the cycle."""
    used = [False] * instance.n
    tour = start_tour(start, used, trace)

    partner = nearest_unused_node(instance, used, from_node=start)
    add_node(tour, used, len(tour), partner, trace)

    while len(tour) < instance.k:
        node, position = regret_insertion(instance, tour, used, cycle_insertion_increases, weights)
        add_node(tour, used, position, node, trace)
    return tour


# ---------------------------------------------------------------------------
# Entry points required by the protocol
# ---------------------------------------------------------------------------

def solve(instance, method, run_index, seed, trace):
    """Run one run of a method and return the resulting tour."""
    if method == "nn_regret":
        return nearest_neighbor_regret(instance, run_index, PURE_REGRET, trace)
    if method == "nn_weighted":
        return nearest_neighbor_regret(instance, run_index, EQUAL_WEIGHTS, trace)
    if method == "gc_regret":
        return greedy_cycle_regret(instance, run_index, PURE_REGRET, trace)
    return greedy_cycle_regret(instance, run_index, EQUAL_WEIGHTS, trace)


def runs_for(method, n):
    """Number of runs of a method, or None if the method does not exist."""
    if method in ("nn_regret", "nn_weighted", "gc_regret", "gc_weighted"):
        return n  # one run per starting node
    return None


if __name__ == "__main__":
    tsp.run(solve, runs_for)
