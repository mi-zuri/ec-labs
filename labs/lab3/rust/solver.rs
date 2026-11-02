//! Lab 3 - local search.
//!
//! Methods: ls_<search>_<intra>_<start>
//!   search  steepest - apply the best move of the whole neighborhood
//!           greedy   - apply the first improving move, moves browsed in random order
//!   intra   nodes    - intra-route move exchanges two nodes of the tour
//!           edges    - intra-route move exchanges two edges (2-opt, reverses a segment)
//!   start   random   - random solution
//!           greedy   - greedy cycle, 2-regret + best change (best heuristic of lab 2)
//!
//! The neighborhood always contains both kinds of moves:
//!   inter-route  exchange a selected node with an unselected one
//!   intra-route  nodes or edges exchange (see above)
//! Every move is evaluated by its delta (change of the objective), never from scratch.
//!
//! Run i uses seed + i (random start and greedy browsing order),
//! and the greedy start of run i begins at node i.
#[path = "../../../common/rust/tsp.rs"]
mod tsp;

use tsp::{emit_step, Instance, Rng};

#[derive(Clone, Copy, PartialEq)]
enum Search {
    Steepest,
    Greedy,
}

#[derive(Clone, Copy, PartialEq)]
enum Start {
    Random,
    Greedy,
}

/// Move kinds. A move is (kind, i, j) of positions:
#[derive(Clone, Copy)]
enum MoveKind {
    /// inter-route: tour[i] <-> unselected[j]
    ExchangeSelected,
    /// intra-route: tour[i] <-> tour[j], i < j
    SwapNodes,
    /// intra-route: edges (tour[i], tour[i+1]) and (tour[j], tour[j+1]) -> reverse tour[i+1..j], i < j
    SwapEdges,
}

#[derive(Clone, Copy)]
struct Move {
    kind: MoveKind,
    i: usize,
    j: usize,
}

/// method id -> (search variant, intra-route move kind, starting solution)
fn find_method(method: &str) -> Option<(Search, MoveKind, Start)> {
    match method {
        "ls_steepest_nodes_random" => Some((Search::Steepest, MoveKind::SwapNodes, Start::Random)),
        "ls_steepest_edges_random" => Some((Search::Steepest, MoveKind::SwapEdges, Start::Random)),
        "ls_greedy_nodes_random" => Some((Search::Greedy, MoveKind::SwapNodes, Start::Random)),
        "ls_greedy_edges_random" => Some((Search::Greedy, MoveKind::SwapEdges, Start::Random)),
        "ls_steepest_nodes_greedy" => Some((Search::Steepest, MoveKind::SwapNodes, Start::Greedy)),
        "ls_steepest_edges_greedy" => Some((Search::Steepest, MoveKind::SwapEdges, Start::Greedy)),
        "ls_greedy_nodes_greedy" => Some((Search::Greedy, MoveKind::SwapNodes, Start::Greedy)),
        "ls_greedy_edges_greedy" => Some((Search::Greedy, MoveKind::SwapEdges, Start::Greedy)),
        _ => None,
    }
}

/// A complete solution: k nodes of the cycle and the n - k other nodes.
struct Solution {
    tour: Vec<usize>,
    unselected: Vec<usize>,
}

// ---------------------------------------------------------------------------
// Starting solutions
// ---------------------------------------------------------------------------

/// Random k nodes in random order.
fn random_start(instance: &Instance, rng: &mut Rng) -> Solution {
    let mut nodes: Vec<usize> = (0..instance.n).collect();
    rng.shuffle(&mut nodes);
    let unselected = nodes.split_off(instance.k);
    Solution { tour: nodes, unselected }
}

/// Return the unused node u minimizing d(from_node, u) + cost[u].
fn nearest_unused_node(instance: &Instance, used: &[bool], from_node: usize) -> usize {
    let mut best_node = 0;
    let mut best_value = i64::MAX;
    for u in 0..instance.n {
        if used[u] {
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

/// Find the unused node with the highest 2-regret - best change, and its cheapest position.
/// Returns (node, position).
fn regret_insertion(instance: &Instance, tour: &[usize], used: &[bool]) -> (usize, usize) {
    let length = tour.len();
    let mut best = (0, 0);
    let mut best_score = i64::MIN;

    for u in 0..instance.n {
        if used[u] {
            continue;
        }
        // cheapest and second cheapest length increase over all edges of the cycle
        let mut position = 0;
        let (mut cheapest, mut second_cheapest) = (i64::MAX, i64::MAX);
        for p in 0..length {
            let (before, after) = (tour[(p + length - 1) % length], tour[p]);
            let increase = instance.d(before, u) + instance.d(u, after) - instance.d(before, after);
            if increase < cheapest {
                position = p;
                second_cheapest = cheapest;
                cheapest = increase;
            } else if increase < second_cheapest {
                second_cheapest = increase;
            }
        }

        let regret = second_cheapest - cheapest;
        let best_change = cheapest + instance.cost[u];
        let score = regret - best_change;
        if score > best_score {
            best = (u, position);
            best_score = score;
        }
    }
    best
}

/// Greedy cycle with 2-regret + best change (lab 2), from the start node.
fn greedy_start(instance: &Instance, start: usize) -> Solution {
    let mut used = vec![false; instance.n];
    let mut tour = vec![start];
    used[start] = true;

    let partner = nearest_unused_node(instance, &used, start);
    tour.push(partner);
    used[partner] = true;

    while tour.len() < instance.k {
        let (node, position) = regret_insertion(instance, &tour, &used);
        tour.insert(position, node);
        used[node] = true;
    }

    let unselected = (0..instance.n).filter(|&v| !used[v]).collect();
    Solution { tour, unselected }
}

// ---------------------------------------------------------------------------
// Moves: deltas and application
// ---------------------------------------------------------------------------

/// Every move of the neighborhood: all inter-route moves, then all intra-route moves.
fn all_moves(k: usize, unselected_count: usize, intra_kind: MoveKind) -> Vec<Move> {
    let mut moves = Vec::new();
    for i in 0..k {
        for j in 0..unselected_count {
            moves.push(Move { kind: MoveKind::ExchangeSelected, i, j });
        }
    }
    for i in 0..k {
        for j in i + 1..k {
            moves.push(Move { kind: intra_kind, i, j });
        }
    }
    moves
}

/// Replace tour[i] with new_node: its two edges and its cost change.
fn exchange_selected_delta(instance: &Instance, tour: &[usize], i: usize, new_node: usize) -> i64 {
    let k = tour.len();
    let (before, old_node, after) = (tour[(i + k - 1) % k], tour[i], tour[(i + 1) % k]);
    instance.cost[new_node] - instance.cost[old_node] + instance.d(before, new_node) + instance.d(new_node, after)
        - instance.d(before, old_node)
        - instance.d(old_node, after)
}

/// Swap the nodes at positions i < j.
fn swap_nodes_delta(instance: &Instance, tour: &[usize], i: usize, j: usize) -> i64 {
    let k = tour.len();
    let (a, b) = (tour[i], tour[j]);

    if j == i + 1 {
        // neighbours: before a b after -> before b a after (edge a-b stays)
        let (before, after) = (tour[(i + k - 1) % k], tour[(j + 1) % k]);
        return instance.d(before, b) + instance.d(a, after) - instance.d(before, a) - instance.d(b, after);
    }

    if i == 0 && j == k - 1 {
        // neighbours over the closing edge: before b a after -> before a b after
        let (before, after) = (tour[j - 1], tour[i + 1]);
        return instance.d(before, a) + instance.d(b, after) - instance.d(before, b) - instance.d(a, after);
    }

    let (before_a, after_a) = (tour[(i + k - 1) % k], tour[i + 1]);
    let (before_b, after_b) = (tour[j - 1], tour[(j + 1) % k]);
    instance.d(before_a, b) + instance.d(b, after_a) + instance.d(before_b, a) + instance.d(a, after_b)
        - instance.d(before_a, a)
        - instance.d(a, after_a)
        - instance.d(before_b, b)
        - instance.d(b, after_b)
}

/// Remove edges (a, b) = (tour[i], tour[i+1]) and (c, d) = (tour[j], tour[j+1]),
/// add (a, c) and (b, d), i < j.
fn swap_edges_delta(instance: &Instance, tour: &[usize], i: usize, j: usize) -> i64 {
    let (a, b) = (tour[i], tour[i + 1]);
    let (c, d) = (tour[j], tour[(j + 1) % tour.len()]);
    instance.d(a, c) + instance.d(b, d) - instance.d(a, b) - instance.d(c, d)
}

/// Change of the objective if the move were applied (negative = better).
fn move_delta(instance: &Instance, solution: &Solution, mv: Move) -> i64 {
    match mv.kind {
        MoveKind::ExchangeSelected => exchange_selected_delta(instance, &solution.tour, mv.i, solution.unselected[mv.j]),
        MoveKind::SwapNodes => swap_nodes_delta(instance, &solution.tour, mv.i, mv.j),
        MoveKind::SwapEdges => swap_edges_delta(instance, &solution.tour, mv.i, mv.j),
    }
}

fn apply_move(solution: &mut Solution, mv: Move) {
    match mv.kind {
        MoveKind::ExchangeSelected => std::mem::swap(&mut solution.tour[mv.i], &mut solution.unselected[mv.j]),
        MoveKind::SwapNodes => solution.tour.swap(mv.i, mv.j),
        MoveKind::SwapEdges => solution.tour[mv.i + 1..=mv.j].reverse(),
    }
}

// ---------------------------------------------------------------------------
// Local search
// ---------------------------------------------------------------------------

/// Steepest: evaluate every move, return the best improving one (or None).
fn find_best_move(instance: &Instance, solution: &Solution, moves: &[Move]) -> Option<Move> {
    let mut best_move = None;
    let mut best_delta = 0;
    for &mv in moves {
        let delta = move_delta(instance, solution, mv);
        if delta < best_delta {
            best_move = Some(mv);
            best_delta = delta;
        }
    }
    best_move
}

/// Greedy: browse the moves in random order, return the first improving one (or None).
///
/// The order is a lazy Fisher-Yates shuffle: each step draws the next move at random from
/// the ones not yet tried, so only the moves actually evaluated cost any work.
fn find_first_improving_move(instance: &Instance, solution: &Solution, moves: &mut [Move], rng: &mut Rng) -> Option<Move> {
    let count = moves.len();
    for tried in 0..count {
        let pick = tried + rng.below(count - tried);
        moves.swap(tried, pick);
        if move_delta(instance, solution, moves[tried]) < 0 {
            return Some(moves[tried]);
        }
    }
    None
}

/// Apply improving moves until none is left (local optimum).
fn local_search(instance: &Instance, mut solution: Solution, search: Search, intra_kind: MoveKind, rng: &mut Rng, trace: bool) -> Vec<usize> {
    let mut moves = all_moves(solution.tour.len(), solution.unselected.len(), intra_kind);
    if trace {
        emit_step(&solution.tour);
    }

    loop {
        let found = match search {
            Search::Steepest => find_best_move(instance, &solution, &moves),
            Search::Greedy => find_first_improving_move(instance, &solution, &mut moves, rng),
        };
        let Some(mv) = found else {
            return solution.tour;
        };

        apply_move(&mut solution, mv);
        if trace {
            emit_step(&solution.tour);
        }
    }
}

// ---------------------------------------------------------------------------
// Entry points required by the protocol
// ---------------------------------------------------------------------------

/// Run one run of a method and return the resulting tour.
fn solve(instance: &Instance, method: &str, run: usize, seed: u64, trace: bool) -> Vec<usize> {
    let (search, intra_kind, start) = find_method(method).unwrap();
    let mut rng = Rng::new(seed);
    let solution = match start {
        Start::Random => random_start(instance, &mut rng),
        Start::Greedy => greedy_start(instance, run),
    };
    local_search(instance, solution, search, intra_kind, &mut rng, trace)
}

/// Number of runs of a method, or None if the method does not exist.
fn runs_for(method: &str, n: usize) -> Option<usize> {
    find_method(method).map(|_| n) // 200 random starts / one greedy start per node
}

fn main() {
    tsp::run(solve, runs_for);
}
