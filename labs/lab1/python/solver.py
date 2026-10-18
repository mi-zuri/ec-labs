"""Lab 1 - greedy heuristics: random, nn_end, nn_any, greedy_cycle.

Greedy run i starts from node i. Each step adds the unused node with the smallest
(tour length increase + node cost); ties go to the lowest node, then lowest position.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "common", "python"))
import tsp  # noqa: E402

RANDOM_RUNS = 200


def start_tour(start, used, trace):
    used[start] = True
    tour = [start]
    if trace:
        tsp.emit_step(tour)
    return tour


def add_node(tour, used, position, node, trace):
    tour.insert(position, node)
    used[node] = True
    if trace:
        tsp.emit_step(tour)


def nearest_unused_node(instance, used, from_node):
    """Unused node u minimizing dist[from_node][u] + cost[u]."""
    best_node, best_value = -1, tsp.INFINITY
    for u in range(instance.n):
        if used[u]:
            continue
        value = instance.dist[from_node][u] + instance.cost[u]
        if value < best_value:
            best_node, best_value = u, value
    return best_node


def cheapest_position_in_path(instance, tour, node):
    """Return (length increase, index) of the cheapest insertion into the open path.

    Index 0 is before the first node, len(tour) is after the last one.
    """
    dist = instance.dist
    increases = [dist[node][tour[0]]]
    for p in range(1, len(tour)):
        before, after = tour[p - 1], tour[p]
        increases.append(dist[before][node] + dist[node][after] - dist[before][after])
    increases.append(dist[tour[-1]][node])

    smallest = min(increases)
    return smallest, increases.index(smallest)


def cheapest_position_in_cycle(instance, tour, node):
    """Return (length increase, index) of the cheapest insertion into an edge of the cycle.

    Edge i is tour[i] -> tour[i+1] (the last wraps to tour[0]); inserting there gives index i + 1.
    """
    dist = instance.dist
    increases = []
    for i in range(len(tour)):
        before, after = tour[i], tour[(i + 1) % len(tour)]
        increases.append(dist[before][node] + dist[node][after] - dist[before][after])

    smallest = min(increases)
    return smallest, increases.index(smallest) + 1


def cheapest_insertion(instance, tour, used, cheapest_position):
    """Return the (node, position) with the smallest total cost of adding."""
    best_node, best_position, best_value = -1, -1, tsp.INFINITY
    for u in range(instance.n):
        if used[u]:
            continue
        length_increase, position = cheapest_position(instance, tour, u)
        value = length_increase + instance.cost[u]
        if value < best_value:
            best_node, best_position, best_value = u, position, value
    return best_node, best_position


def random_solution(instance, seed, trace):
    rng = tsp.Rng(seed)
    nodes = list(range(instance.n))
    rng.shuffle(nodes)
    tour = nodes[:instance.k]

    if trace:
        for length in range(1, instance.k + 1):
            tsp.emit_step(tour[:length])
    return tour


def nn_end(instance, start, trace):
    """Append the nearest unused node to the end of the path."""
    used = [False] * instance.n
    tour = start_tour(start, used, trace)

    while len(tour) < instance.k:
        node = nearest_unused_node(instance, used, from_node=tour[-1])
        add_node(tour, used, len(tour), node, trace)
    return tour


def nn_any(instance, start, trace):
    """Insert the best node at the best position of the path."""
    used = [False] * instance.n
    tour = start_tour(start, used, trace)

    while len(tour) < instance.k:
        node, position = cheapest_insertion(instance, tour, used, cheapest_position_in_path)
        add_node(tour, used, position, node, trace)
    return tour


def greedy_cycle(instance, start, trace):
    """Start from the best pair, then insert the best node into the best edge of the cycle."""
    used = [False] * instance.n
    tour = start_tour(start, used, trace)

    partner = nearest_unused_node(instance, used, from_node=start)
    add_node(tour, used, len(tour), partner, trace)

    while len(tour) < instance.k:
        node, position = cheapest_insertion(instance, tour, used, cheapest_position_in_cycle)
        add_node(tour, used, position, node, trace)
    return tour


def solve(instance, method, run_index, seed, trace):
    if method == "random":
        return random_solution(instance, seed, trace)
    if method == "nn_end":
        return nn_end(instance, run_index, trace)
    if method == "nn_any":
        return nn_any(instance, run_index, trace)
    return greedy_cycle(instance, run_index, trace)


def runs_for(method, n):
    if method == "random":
        return RANDOM_RUNS
    if method in ("nn_end", "nn_any", "greedy_cycle"):
        return n
    return None


if __name__ == "__main__":
    tsp.run(solve, runs_for)
