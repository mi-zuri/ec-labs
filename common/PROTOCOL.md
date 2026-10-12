# Solver protocol

Every solver (any lab, any language) is a command-line program that follows this contract.
The server (`server.py`) relies on it to run solvers and stream their progress to the viewer.

## Command line

```
solver --instance <path.csv> --method <id> --mode trace|bench [--start S] [--seed 42]
```

- `--mode bench` runs every run of the method (e.g. 200) and prints only one line per run.
- `--mode trace` runs a single run (`--start S` = run index) and prints every intermediate step.
- For greedy methods run `i` starts from node `i`. Random run `i` uses seed `seed + i`.

## Output: one JSON object per line on stdout, flushed after each line

| `t`       | fields                                                     | when                          |
|-----------|------------------------------------------------------------|-------------------------------|
| `meta`    | `n`, `k`, `runs`, `mode`                                   | once, first                   |
| `step`    | `tour` (node indices, current partial solution)            | trace only, after each change |
| `run_end` | `run`, `obj`, `len`, `cost`, `ns`, `tour`                  | after every run               |
| `done`    | `total_ns`                                                 | once, last                    |
| `error`   | `msg`                                                      | on failure (then exit != 0)   |

`ns` is the time of the algorithm only (instance loading and evaluation are excluded).
In trace mode it also includes the time spent printing steps, so use bench mode for timing.

## Shared rules (identical results across languages)

- Distances: `floor(sqrt(dx*dx + dy*dy) + 0.5)` as integers, computed once into a matrix.
  Algorithms only use the distance matrix and node costs, never coordinates.
- `k = ceil(n / 2)` nodes are selected. Objective = cycle length (incl. closing edge) + sum of costs.
- RNG: splitmix64. `below(n) = next() % n`.
  Shuffle: Fisher-Yates, `for i = n-1 .. 1: j = below(i+1); swap(a[i], a[j])`.
- Ties: candidates are scanned in increasing node index, then increasing position,
  and only a strictly better value replaces the current best.

## Adding a lab

1. Create `labs/labN/lab.json` with `title` and `methods` (`id`, `name`, `shape`: `path` or `cycle`,
   the shape of partial solutions during construction).
2. Add `labs/labN/python/solver.py`, `c/solver.c`, `cpp/solver.cpp`, `rust/solver.rs`
   (copy them from `labs/lab1/`), each using the shared helper from `common/<lang>/`
   (included via `../../../common/...`) and providing `solve` + `runs_for`.
3. Restart nothing: the viewer picks it up on reload.
