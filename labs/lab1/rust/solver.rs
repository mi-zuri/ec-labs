//! Lab 1 - greedy heuristics: random, nn_end, nn_any, greedy_cycle.
//!
//! Greedy run i starts from node i. Each step adds the unused node with the smallest
//! (tour length increase + node cost); ties go to the lowest node, then lowest position.
#[path = "../../../common/rust/tsp.rs"]
mod tsp;

use tsp::{emit_step, Instance, Rng};

const RANDOM_RUNS: usize = 200;

/// A tour under construction, plus which nodes are already in it.
struct Builder<'a> {
    instance: &'a Instance,
    trace: bool,
    nodes: Vec<usize>,
    /// `used[v]` is true if node v is in the tour.
    used: Vec<bool>,
}

/// Where a node would be inserted and how much that lengthens the tour.
struct Position {
    index: usize,
    length_increase: i64,
}

/// The node to add next and where to put it.
struct Insertion {
    node: usize,
    position: usize,
}

impl<'a> Builder<'a> {
    fn new(instance: &'a Instance, start: usize, trace: bool) -> Builder<'a> {
        let mut used = vec![false; instance.n];
        used[start] = true;
        let builder = Builder { instance, trace, nodes: vec![start], used };
        if trace {
            emit_step(&builder.nodes);
        }
        builder
    }

    fn length(&self) -> usize {
        self.nodes.len()
    }

    fn add_node(&mut self, position: usize, node: usize) {
        self.nodes.insert(position, node);
        self.used[node] = true;
        if self.trace {
            emit_step(&self.nodes);
        }
    }

    /// Unused node u minimizing d(from_node, u) + cost[u].
    fn nearest_unused_node(&self, from_node: usize) -> usize {
        let instance = self.instance;
        let mut best_node = 0;
        let mut best_value = i64::MAX;
        for u in 0..instance.n {
            if self.used[u] {
                continue;
            }
            let value = instance.d(from_node, u) + instance.cost[u];
            if value < best_value {
                best_node = u;
                best_value = value;
            }
        }
        best_node
    }

    /// Index 0 is before the first node, `length` is after the last one.
    fn cheapest_position_in_path(&self, node: usize) -> Position {
        let instance = self.instance;
        let nodes = &self.nodes;
        let length = nodes.len();
        let mut best = Position { index: 0, length_increase: i64::MAX };

        for p in 0..=length {
            let increase = if p == 0 {
                instance.d(node, nodes[0])
            } else if p == length {
                instance.d(nodes[length - 1], node)
            } else {
                let (before, after) = (nodes[p - 1], nodes[p]);
                instance.d(before, node) + instance.d(node, after) - instance.d(before, after)
            };
            if increase < best.length_increase {
                best = Position { index: p, length_increase: increase };
            }
        }
        best
    }

    /// Edge i is nodes[i] -> nodes[i+1] (the last wraps to nodes[0]); inserting there gives index i + 1.
    fn cheapest_position_in_cycle(&self, node: usize) -> Position {
        let instance = self.instance;
        let nodes = &self.nodes;
        let length = nodes.len();
        let mut best = Position { index: 0, length_increase: i64::MAX };

        for i in 0..length {
            let (before, after) = (nodes[i], nodes[(i + 1) % length]);
            let increase = instance.d(before, node) + instance.d(node, after) - instance.d(before, after);
            if increase < best.length_increase {
                best = Position { index: i + 1, length_increase: increase };
            }
        }
        best
    }

    /// Unused node and position with the smallest total cost of adding.
    fn cheapest_insertion(&self, cheapest_position: impl Fn(&Self, usize) -> Position) -> Insertion {
        let instance = self.instance;
        let mut best = Insertion { node: 0, position: 0 };
        let mut best_value = i64::MAX;

        for u in 0..instance.n {
            if self.used[u] {
                continue;
            }
            let position = cheapest_position(self, u);
            let value = position.length_increase + instance.cost[u];
            if value < best_value {
                best = Insertion { node: u, position: position.index };
                best_value = value;
            }
        }
        best
    }
}

fn random_solution(instance: &Instance, seed: u64, trace: bool) -> Vec<usize> {
    let mut rng = Rng::new(seed);
    let mut nodes: Vec<usize> = (0..instance.n).collect();
    rng.shuffle(&mut nodes);
    nodes.truncate(instance.k);

    if trace {
        for length in 1..=instance.k {
            emit_step(&nodes[..length]);
        }
    }
    nodes
}

/// Append the nearest unused node to the end of the path.
fn nn_end(instance: &Instance, start: usize, trace: bool) -> Vec<usize> {
    let mut path = Builder::new(instance, start, trace);

    while path.length() < instance.k {
        let last = path.nodes[path.length() - 1];
        let node = path.nearest_unused_node(last);
        path.add_node(path.length(), node);
    }
    path.nodes
}

/// Insert the best node at the best position of the path.
fn nn_any(instance: &Instance, start: usize, trace: bool) -> Vec<usize> {
    let mut path = Builder::new(instance, start, trace);

    while path.length() < instance.k {
        let next = path.cheapest_insertion(Builder::cheapest_position_in_path);
        path.add_node(next.position, next.node);
    }
    path.nodes
}

/// Start from the best pair, then insert the best node into the best edge of the cycle.
fn greedy_cycle(instance: &Instance, start: usize, trace: bool) -> Vec<usize> {
    let mut cycle = Builder::new(instance, start, trace);

    let partner = cycle.nearest_unused_node(start);
    cycle.add_node(cycle.length(), partner);

    while cycle.length() < instance.k {
        let next = cycle.cheapest_insertion(Builder::cheapest_position_in_cycle);
        cycle.add_node(next.position, next.node);
    }
    cycle.nodes
}

fn solve(instance: &Instance, method: &str, run: usize, seed: u64, trace: bool) -> Vec<usize> {
    match method {
        "random" => random_solution(instance, seed, trace),
        "nn_end" => nn_end(instance, run, trace),
        "nn_any" => nn_any(instance, run, trace),
        _ => greedy_cycle(instance, run, trace),
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
