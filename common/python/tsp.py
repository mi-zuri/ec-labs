"""Shared helpers for Python solvers: instance loading, RNG, timing and the output protocol.

See common/PROTOCOL.md for the contract every solver follows.
"""
import json
import math
import sys
import time

MASK64 = (1 << 64) - 1
INF = 1 << 62


class Instance:
    """Distance matrix + node costs. Coordinates are dropped right after building the matrix."""

    def __init__(self, path):
        xs, ys, cost = [], [], []
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                x, y, c = (int(v) for v in line.split(";"))
                xs.append(x)
                ys.append(y)
                cost.append(c)
        n = len(cost)
        self.n = n
        self.k = (n + 1) // 2
        self.cost = cost
        self.dist = [
            [int(math.floor(math.sqrt((xs[i] - xs[j]) ** 2 + (ys[i] - ys[j]) ** 2) + 0.5)) for j in range(n)]
            for i in range(n)
        ]

    def evaluate(self, tour):
        """Return (cycle length, sum of node costs) of a tour."""
        d = self.dist
        m = len(tour)
        length = sum(d[tour[i]][tour[(i + 1) % m]] for i in range(m)) if m > 1 else 0
        return length, sum(self.cost[v] for v in tour)


class Rng:
    """splitmix64, identical in every language so random runs match exactly."""

    def __init__(self, seed):
        self.state = seed & MASK64

    def next(self):
        self.state = (self.state + 0x9E3779B97F4A7C15) & MASK64
        z = self.state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
        return z ^ (z >> 31)

    def below(self, n):
        return self.next() % n

    def shuffle(self, a):
        for i in range(len(a) - 1, 0, -1):
            j = self.below(i + 1)
            a[i], a[j] = a[j], a[i]


def emit(obj):
    sys.stdout.write(json.dumps(obj, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def emit_step(tour):
    emit({"t": "step", "tour": tour})


def fail(msg):
    emit({"t": "error", "msg": msg})
    sys.exit(2)


def parse_args(argv):
    args = {"instance": None, "method": None, "mode": "bench", "start": 0, "seed": 42}
    if len(argv) % 2:
        fail("arguments must come in --key value pairs")
    for key, val in zip(argv[::2], argv[1::2]):
        name = key.lstrip("-")
        if name not in args:
            fail("unknown argument " + key)
        args[name] = int(val) if name in ("start", "seed") else val
    if not args["instance"] or not args["method"]:
        fail("--instance and --method are required")
    return args


def run(solve, runs_for):
    """Protocol driver. solve(inst, method, run, seed, trace) -> tour; runs_for(method, n) -> int | None."""
    args = parse_args(sys.argv[1:])
    inst = Instance(args["instance"])
    method = args["method"]
    trace = args["mode"] == "trace"
    total = runs_for(method, inst.n)
    if total is None:
        fail("unknown method " + method)
    if trace and not 0 <= args["start"] < total:
        fail("start must be in [0, %d)" % total)
    ids = [args["start"]] if trace else range(total)

    emit({"t": "meta", "n": inst.n, "k": inst.k, "runs": len(ids), "mode": "trace" if trace else "bench"})
    total_ns = 0
    for r in ids:
        t0 = time.perf_counter_ns()
        tour = solve(inst, method, r, args["seed"] + r, trace)
        ns = time.perf_counter_ns() - t0
        total_ns += ns
        length, cost = inst.evaluate(tour)
        emit({"t": "run_end", "run": r, "obj": length + cost, "len": length, "cost": cost, "ns": ns, "tour": tour})
    emit({"t": "done", "total_ns": total_ns})
