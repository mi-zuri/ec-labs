//! Lab 1 - greedy heuristics: random, nearest neighbor (end / any position), greedy cycle.
#[path = "../../../common/rust/tsp.rs"]
mod tsp;

use tsp::{emit_step, Instance, Rng};

const RANDOM_RUNS: usize = 200;

fn random_solution(inst: &Instance, seed: u64, trace: bool) -> Vec<usize> {
    let mut rng = Rng::new(seed);
    let mut perm: Vec<usize> = (0..inst.n).collect();
    rng.shuffle(&mut perm);
    perm.truncate(inst.k);
    if trace {
        for m in 1..=inst.k {
            emit_step(&perm[..m]);
        }
    }
    perm
}

/// Append the node with the best (distance from last node + node cost).
fn nn_end(inst: &Instance, start: usize, trace: bool) -> Vec<usize> {
    let mut used = vec![false; inst.n];
    let mut tour = vec![start];
    used[start] = true;
    if trace {
        emit_step(&tour);
    }
    while tour.len() < inst.k {
        let last = tour[tour.len() - 1];
        let mut best = i64::MAX;
        let mut best_u = 0;
        for u in 0..inst.n {
            if used[u] {
                continue;
            }
            let delta = inst.d(last, u) + inst.cost[u];
            if delta < best {
                best = delta;
                best_u = u;
            }
        }
        tour.push(best_u);
        used[best_u] = true;
        if trace {
            emit_step(&tour);
        }
    }
    tour
}

/// Insert the best node at the best position of the path: front, between two nodes, or end.
fn nn_any(inst: &Instance, start: usize, trace: bool) -> Vec<usize> {
    let mut used = vec![false; inst.n];
    let mut tour = vec![start];
    used[start] = true;
    if trace {
        emit_step(&tour);
    }
    while tour.len() < inst.k {
        let m = tour.len();
        let mut best = i64::MAX;
        let mut best_u = 0;
        let mut best_pos = 0;
        for u in 0..inst.n {
            if used[u] {
                continue;
            }
            for p in 0..=m {
                // p = index u is inserted at
                let length_delta = if p == 0 {
                    inst.d(u, tour[0])
                } else if p == m {
                    inst.d(tour[m - 1], u)
                } else {
                    inst.d(tour[p - 1], u) + inst.d(u, tour[p]) - inst.d(tour[p - 1], tour[p])
                };
                let delta = length_delta + inst.cost[u];
                if delta < best {
                    best = delta;
                    best_u = u;
                    best_pos = p;
                }
            }
        }
        tour.insert(best_pos, best_u);
        used[best_u] = true;
        if trace {
            emit_step(&tour);
        }
    }
    tour
}

/// Start from the best pair, then repeatedly insert the best node into the best edge of the cycle.
fn greedy_cycle(inst: &Instance, start: usize, trace: bool) -> Vec<usize> {
    let mut used = vec![false; inst.n];
    let mut tour = vec![start];
    used[start] = true;
    if trace {
        emit_step(&tour);
    }

    let mut best = i64::MAX;
    let mut best_u = 0;
    for u in 0..inst.n {
        if used[u] {
            continue;
        }
        let delta = inst.d(start, u) + inst.cost[u];
        if delta < best {
            best = delta;
            best_u = u;
        }
    }
    tour.push(best_u);
    used[best_u] = true;
    if trace {
        emit_step(&tour);
    }

    while tour.len() < inst.k {
        let m = tour.len();
        let mut best = i64::MAX;
        let mut best_u = 0;
        let mut best_pos = 0;
        for u in 0..inst.n {
            if used[u] {
                continue;
            }
            for i in 0..m {
                // edge tour[i] -> tour[i+1], incl. the closing edge
                let a = tour[i];
                let b = tour[(i + 1) % m];
                let delta = inst.d(a, u) + inst.d(u, b) - inst.d(a, b) + inst.cost[u];
                if delta < best {
                    best = delta;
                    best_u = u;
                    best_pos = i + 1;
                }
            }
        }
        tour.insert(best_pos, best_u);
        used[best_u] = true;
        if trace {
            emit_step(&tour);
        }
    }
    tour
}

fn solve(inst: &Instance, method: &str, run: usize, seed: u64, trace: bool) -> Vec<usize> {
    match method {
        "random" => random_solution(inst, seed, trace),
        "nn_end" => nn_end(inst, run, trace),
        "nn_any" => nn_any(inst, run, trace),
        _ => greedy_cycle(inst, run, trace),
    }
}

fn runs_for(method: &str, n: usize) -> Option<usize> {
    match method {
        "random" => Some(RANDOM_RUNS),
        "nn_end" | "nn_any" | "greedy_cycle" => Some(n),
        _ => None,
    }
}

fn main() {
    tsp::run(solve, runs_for);
}
