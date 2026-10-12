"""Lab 1 - greedy heuristics: random, nearest neighbor (end / any position), greedy cycle."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "common", "python"))
import tsp  # noqa: E402

RANDOM_RUNS = 200


def random_solution(inst, seed, trace):
    rng = tsp.Rng(seed)
    perm = list(range(inst.n))
    rng.shuffle(perm)
    tour = perm[: inst.k]
    if trace:
        for m in range(1, inst.k + 1):
            tsp.emit_step(tour[:m])
    return tour


def nn_end(inst, start, trace):
    """Append the node with the best (distance from last node + node cost)."""
    d, c = inst.dist, inst.cost
    used = [False] * inst.n
    used[start] = True
    tour = [start]
    if trace:
        tsp.emit_step(tour)
    while len(tour) < inst.k:
        row = d[tour[-1]]
        best, best_u = tsp.INF, -1
        for u in range(inst.n):
            if not used[u] and row[u] + c[u] < best:
                best, best_u = row[u] + c[u], u
        tour.append(best_u)
        used[best_u] = True
        if trace:
            tsp.emit_step(tour)
    return tour


def nn_any(inst, start, trace):
    """Insert the best node at the best position of the path: front, between two nodes, or end."""
    d, c = inst.dist, inst.cost
    used = [False] * inst.n
    used[start] = True
    tour = [start]
    if trace:
        tsp.emit_step(tour)
    while len(tour) < inst.k:
        first, last = tour[0], tour[-1]
        edges = [(tour[i], tour[i + 1], d[tour[i]][tour[i + 1]]) for i in range(len(tour) - 1)]
        best, best_u, best_pos = tsp.INF, -1, -1
        for u in range(inst.n):
            if used[u]:
                continue
            du = d[u]
            # deltas[p] = length increase when u is inserted at index p
            deltas = [du[first]] + [du[a] + du[b] - e for a, b, e in edges] + [du[last]]
            low = min(deltas)
            if low + c[u] < best:
                best, best_u, best_pos = low + c[u], u, deltas.index(low)
        tour.insert(best_pos, best_u)
        used[best_u] = True
        if trace:
            tsp.emit_step(tour)
    return tour


def greedy_cycle(inst, start, trace):
    """Start from the best pair, then repeatedly insert the best node into the best edge of the cycle."""
    d, c = inst.dist, inst.cost
    used = [False] * inst.n
    used[start] = True
    tour = [start]
    if trace:
        tsp.emit_step(tour)

    row = d[start]
    best, best_u = tsp.INF, -1
    for u in range(inst.n):
        if not used[u] and row[u] + c[u] < best:
            best, best_u = row[u] + c[u], u
    tour.append(best_u)
    used[best_u] = True
    if trace:
        tsp.emit_step(tour)

    while len(tour) < inst.k:
        m = len(tour)
        edges = [(tour[i], tour[(i + 1) % m], d[tour[i]][tour[(i + 1) % m]]) for i in range(m)]
        best, best_u, best_pos = tsp.INF, -1, -1
        for u in range(inst.n):
            if used[u]:
                continue
            du = d[u]
            deltas = [du[a] + du[b] - e for a, b, e in edges]
            low = min(deltas)
            if low + c[u] < best:
                best, best_u, best_pos = low + c[u], u, deltas.index(low) + 1
        tour.insert(best_pos, best_u)
        used[best_u] = True
        if trace:
            tsp.emit_step(tour)
    return tour


def solve(inst, method, run, seed, trace):
    if method == "random":
        return random_solution(inst, seed, trace)
    if method == "nn_end":
        return nn_end(inst, run, trace)
    if method == "nn_any":
        return nn_any(inst, run, trace)
    return greedy_cycle(inst, run, trace)


def runs_for(method, n):
    if method == "random":
        return RANDOM_RUNS
    if method in ("nn_end", "nn_any", "greedy_cycle"):
        return n
    return None


if __name__ == "__main__":
    tsp.run(solve, runs_for)
