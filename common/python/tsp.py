"""Shared helpers for Python solvers: instance loading, RNG and the output protocol.

See common/PROTOCOL.md. A lab solver provides solve() and runs_for() and calls run().
"""
import json
import math
import sys
import time

UINT64_MASK = (1 << 64) - 1
INFINITY = 1 << 62
DEFAULT_SEED = 42


class Instance:
    """Distance matrix and node costs (coordinates are dropped after building the matrix).

    n: number of nodes, k: number of nodes to select, cost[v]: cost of node v,
    dist[u][v]: rounded Euclidean distance.
    """

    def __init__(self, path):
        xs, ys, costs = read_instance_file(path)
        self.n = len(costs)
        self.k = (self.n + 1) // 2
        self.cost = costs
        self.dist = [
            [rounded_distance(xs[i], ys[i], xs[j], ys[j]) for j in range(self.n)]
            for i in range(self.n)
        ]

    def evaluate(self, tour):
        """Return (cycle length including the closing edge, total node cost)."""
        length = 0
        if len(tour) > 1:
            for i, node in enumerate(tour):
                next_node = tour[(i + 1) % len(tour)]
                length += self.dist[node][next_node]
        total_cost = sum(self.cost[node] for node in tour)
        return length, total_cost


def read_instance_file(path):
    """Read "x;y;cost" lines. Returns (xs, ys, costs)."""
    xs, ys, costs = [], [], []
    with open(path) as file:
        for line in file:
            line = line.strip()
            if not line:
                continue
            x, y, cost = (int(field) for field in line.split(";"))
            xs.append(x)
            ys.append(y)
            costs.append(cost)
    return xs, ys, costs


def rounded_distance(x1, y1, x2, y2):
    return int(math.floor(math.sqrt((x1 - x2) ** 2 + (y1 - y2) ** 2) + 0.5))


class Rng:
    """splitmix64, identical in every language so random runs match exactly."""

    def __init__(self, seed):
        self.state = seed & UINT64_MASK

    def next(self):
        self.state = (self.state + 0x9E3779B97F4A7C15) & UINT64_MASK
        z = self.state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & UINT64_MASK
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & UINT64_MASK
        return z ^ (z >> 31)

    def below(self, n):
        return self.next() % n

    def shuffle(self, items):
        """Fisher-Yates, in place."""
        for i in range(len(items) - 1, 0, -1):
            j = self.below(i + 1)
            items[i], items[j] = items[j], items[i]


def emit(message):
    sys.stdout.write(json.dumps(message, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def emit_step(tour):
    emit({"t": "step", "tour": tour})


def fail(message):
    emit({"t": "error", "msg": message})
    sys.exit(2)


def parse_args(argv):
    args = {"instance": None, "method": None, "mode": "bench", "start": 0, "seed": DEFAULT_SEED}
    integer_args = ("start", "seed")

    if len(argv) % 2 != 0:
        fail("arguments must come in --key value pairs")

    for flag, value in zip(argv[::2], argv[1::2]):
        name = flag.lstrip("-")
        if name not in args:
            fail("unknown argument " + flag)
        args[name] = int(value) if name in integer_args else value

    if not args["instance"] or not args["method"]:
        fail("--instance and --method are required")
    return args


def run(solve, runs_for):
    """Protocol driver. Bench mode runs every run; trace mode only the run given by --start."""
    args = parse_args(sys.argv[1:])
    instance = Instance(args["instance"])
    method = args["method"]
    is_trace = args["mode"] == "trace"

    total_runs = runs_for(method, instance.n)
    if total_runs is None:
        fail("unknown method " + method)
    if is_trace and not 0 <= args["start"] < total_runs:
        fail("start must be in [0, %d)" % total_runs)

    run_indices = [args["start"]] if is_trace else range(total_runs)

    emit({
        "t": "meta",
        "n": instance.n,
        "k": instance.k,
        "runs": len(run_indices),
        "mode": "trace" if is_trace else "bench",
    })

    total_ns = 0
    for run_index in run_indices:
        seed = args["seed"] + run_index

        started = time.perf_counter_ns()
        tour = solve(instance, method, run_index, seed, is_trace)
        elapsed_ns = time.perf_counter_ns() - started
        total_ns += elapsed_ns

        length, cost = instance.evaluate(tour)
        emit({
            "t": "run_end",
            "run": run_index,
            "obj": length + cost,
            "len": length,
            "cost": cost,
            "ns": elapsed_ns,
            "tour": tour,
        })

    emit({"t": "done", "total_ns": total_ns})
