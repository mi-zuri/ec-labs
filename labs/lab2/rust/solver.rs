//! Lab 2 - greedy regret heuristics.
//!
//! Methods:
//!   nn_regret     - nearest neighbor (any position), 2-regret
//!   nn_weighted   - nearest neighbor (any position), 2-regret + best change
//!   gc_regret     - greedy cycle, 2-regret
//!   gc_weighted   - greedy cycle, 2-regret + best change
//!
//! For every unused node:
//!   regret      = (second cheapest length increase) - (cheapest length increase)
//!   best change = (cheapest length increase) + (cost of the node)
//! Every step inserts the node with the highest
//! regret weight * regret - change weight * best change at its cheapest position.
//!
//! All methods start from a given node, and run i starts from node i.
//! On ties the first candidate found wins (lowest node index, then lowest position).
#[path = "../../../common/rust/tsp.rs"]
mod tsp;

use tsp::{emit_step, Instance};

#[derive(Clone, Copy)]
struct Weights {
    regret: i64,
    change: i64,
}

const PURE_REGRET: Weights = Weights { regret: 1, change: 0 };
const EQUAL_WEIGHTS: Weights = Weights { regret: 1, change: 1 };

// ---------------------------------------------------------------------------
// Building blocks shared by the methods
// ---------------------------------------------------------------------------

/// A tour under construction, plus which nodes are already in it.
struct Builder<'a> {
    instance: &'a Instance,
    /// Print every intermediate tour?
    trace: bool,
    nodes: Vec<usize>,
    /// `used[v]` is true if node v is already in the tour.
    used: Vec<bool>,
}

struct TwoCheapest {
    position: usize,
    cheapest: i64,
    second_cheapest: i64,
}

/// The node to add next and where to put it.
struct Insertion {
    node: usize,
    position: usize,
}

impl<'a> Builder<'a> {
    /// Begin a tour containing only the start node.
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

    /// Insert `node` into the tour at index `position`.
    fn add_node(&mut self, position: usize, node: usize) {
        self.nodes.insert(position, node);
        self.used[node] = true;
        if self.trace {
            emit_step(&self.nodes);
        }
    }

    /// Return the unused node u minimizing d(from_node, u) + cost[u].
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

    /// Length increase for every insertion index p of the open path
    /// (p = 0 before the first node, p = length after the last one).
    fn path_insertion_increases(&self, node: usize) -> Vec<i64> {
        let instance = self.instance;
        let nodes = &self.nodes;
        let length = nodes.len();

        let mut increases = vec![instance.d(node, nodes[0])];
        for p in 1..length {
            let (before, after) = (nodes[p - 1], nodes[p]);
            increases.push(instance.d(before, node) + instance.d(node, after) - instance.d(before, after));
        }
        increases.push(instance.d(nodes[length - 1], node));
        increases
    }

    /// Length increase for every insertion index p of the closed cycle,
    /// i.e. between nodes[p-1] and nodes[p] (p = 0 is the closing edge).
    fn cycle_insertion_increases(&self, node: usize) -> Vec<i64> {
        let instance = self.instance;
        let nodes = &self.nodes;
        let length = nodes.len();

        (0..length)
            .map(|p| {
                let (before, after) = (nodes[(p + length - 1) % length], nodes[p]);
                instance.d(before, node) + instance.d(node, after) - instance.d(before, after)
            })
            .collect()
    }

    /// Find the unused node with the highest score and its cheapest position.
    fn regret_insertion(&self, insertion_increases: impl Fn(&Self, usize) -> Vec<i64>, weights: Weights) -> Insertion {
        let instance = self.instance;
        let mut best = Insertion { node: 0, position: 0 };
        let mut best_score = i64::MIN;

        for u in 0..instance.n {
            if self.used[u] {
                continue;
            }
            let options = two_cheapest(&insertion_increases(self, u));

            let regret = options.second_cheapest - options.cheapest;
            let best_change = options.cheapest + instance.cost[u];
            let score = weights.regret * regret - weights.change * best_change;
            if score > best_score {
                best = Insertion { node: u, position: options.position };
                best_score = score;
            }
        }
        best
    }
}

/// Find the cheapest and second cheapest increase, and where the cheapest one is.
fn two_cheapest(increases: &[i64]) -> TwoCheapest {
    let mut result = TwoCheapest { position: 0, cheapest: i64::MAX, second_cheapest: i64::MAX };
    for (p, &increase) in increases.iter().enumerate() {
        if increase < result.cheapest {
            result = TwoCheapest { position: p, cheapest: increase, second_cheapest: result.cheapest };
        } else if increase < result.second_cheapest {
            result.second_cheapest = increase;
        }
    }
    result
}

// ---------------------------------------------------------------------------
// Methods
// ---------------------------------------------------------------------------

/// Grow a path from the start node, inserting nodes at any position.
fn nearest_neighbor_regret(instance: &Instance, start: usize, weights: Weights, trace: bool) -> Vec<usize> {
    let mut path = Builder::new(instance, start, trace);

    while path.length() < instance.k {
        let next = path.regret_insertion(Builder::path_insertion_increases, weights);
        path.add_node(next.position, next.node);
    }
    path.nodes
}

/// Start a cycle with the start node and its best partner, then keep inserting
/// nodes into the edges of the cycle.
fn greedy_cycle_regret(instance: &Instance, start: usize, weights: Weights, trace: bool) -> Vec<usize> {
    let mut cycle = Builder::new(instance, start, trace);

    let partner = cycle.nearest_unused_node(start);
    cycle.add_node(cycle.length(), partner);

    while cycle.length() < instance.k {
        let next = cycle.regret_insertion(Builder::cycle_insertion_increases, weights);
        cycle.add_node(next.position, next.node);
    }
    cycle.nodes
}

// ---------------------------------------------------------------------------
// Entry points required by the protocol
// ---------------------------------------------------------------------------

/// Run one run of a method and return the resulting tour.
fn solve(instance: &Instance, method: &str, run: usize, _seed: u64, trace: bool) -> Vec<usize> {
    match method {
        "nn_regret" => nearest_neighbor_regret(instance, run, PURE_REGRET, trace),
        "nn_weighted" => nearest_neighbor_regret(instance, run, EQUAL_WEIGHTS, trace),
        "gc_regret" => greedy_cycle_regret(instance, run, PURE_REGRET, trace),
        _ => greedy_cycle_regret(instance, run, EQUAL_WEIGHTS, trace),
    }
}

/// Number of runs of a method, or None if the method does not exist.
fn runs_for(method: &str, n: usize) -> Option<usize> {
    match method {
        "nn_regret" | "nn_weighted" | "gc_regret" | "gc_weighted" => Some(n), // one run per starting node
        _ => None,
    }
}

fn main() {
    tsp::run(solve, runs_for);
}
