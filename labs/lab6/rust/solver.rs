//! Lab 6 - multiple start local search (MSLS) and iterated local search (ILS).
//!
//! Both use the local search of lab 5: steepest, a selected node <-> unselected node exchange
//! plus edges exchange (2-opt), with a list of improving moves (LM). In this file "local search"
//! always means one full run of it, from a solution to its local optimum.
//!
//! MSLS: the best of 400 local searches, each from a new random solution.
//!
//!   best = none
//!   repeat 400 times
//!       x = local_search(random solution)
//!       if x is better than best: best = x
//!
//! ILS: one random solution, then repeatedly kick the current local optimum out of its basin
//! (perturbation) and descend again. Only a strictly better local optimum becomes the current one.
//! It stops after the average running time of MSLS (given by the server as --time-limit-ms),
//! so both methods get the same time.
//!
//!   x = local_search(random solution)
//!   repeat until the time is up
//!       y = local_search(perturb(x))
//!       if y is better than x: x = y
//!
//! Perturbation (designed for this problem: a cycle through half of the nodes):
//!   1. double bridge: cut the tour into 4 parts A B C D at 3 random places and reconnect
//!      them as A C B D. It changes 3 edges at once in a way a single 2-opt move cannot undo.
//!   2. exchange NODE_EXCHANGES random selected nodes with random unselected ones, which changes
//!      the set of selected nodes (the double bridge only changes the order).
//!
//! Run i uses seed + i; its first random solution is the same as in lab 5's run i.
#[path = "../../../common/rust/tsp.rs"]
mod tsp;

use std::time::{Duration, Instant};
use tsp::{emit_step, Instance, Rng};

const MSLS: &str = "msls";
const ILS: &str = "ils";

/// Runs of each method.
const RUNS: usize = 20;
/// Local searches in one run of MSLS.
const MSLS_LOCAL_SEARCHES: usize = 400;
/// Perturbation: selected nodes replaced with random unselected ones.
const NODE_EXCHANGES: usize = 3;

/// Move kinds. A move is (delta, kind, n1, n2, n3, n4) of nodes:
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
enum MoveKind {
    /// inter-route: (before, removed, after, added)
    /// removes edges (before, removed), (removed, after); added takes the place of removed
    ExchangeSelected,
    /// intra-route: (a, b, c, d) removes edges (a, b), (c, d) and adds (a, c), (b, d)
    SwapEdges,
}

/// Derived order of moves: by delta, ties by kind, then by nodes (the same order as Python's tuples).
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
struct Move {
    delta: i64,
    kind: MoveKind,
    n1: usize,
    n2: usize,
    n3: usize,
    n4: usize,
}

/// Situations of a stored move in the current tour
#[derive(PartialEq)]
enum MoveStatus {
    /// a removed edge (or the added node) is gone: remove the move from LM
    Obsolete,
    /// removed edges are there, but in a different relative direction: keep it
    NotApplicableNow,
    /// removed edges are there in the same relative direction: apply it
    Applicable,
}

// ---------------------------------------------------------------------------
// Local search with a list of improving moves (lab 5, unchanged)
// ---------------------------------------------------------------------------

/// Random k nodes in random order.
fn random_start(instance: &Instance, rng: &mut Rng) -> Vec<usize> {
    let mut nodes: Vec<usize> = (0..instance.n).collect();
    rng.shuffle(&mut nodes);
    nodes.truncate(instance.k);
    nodes
}

/// position[v] = Some(index of v in the tour), or None if v is not selected.
fn tour_positions(instance: &Instance, tour: &[usize]) -> Vec<Option<usize>> {
    let mut position = vec![None; instance.n];
    for (i, &node) in tour.iter().enumerate() {
        position[node] = Some(i);
    }
    position
}

// ---------------------------------------------------------------------------
// Evaluating all moves (fills LM)
// ---------------------------------------------------------------------------

/// Replace node removed (between before and after) with node added.
fn exchange_selected_delta(instance: &Instance, before: usize, removed: usize, after: usize, added: usize) -> i64 {
    instance.cost[added] - instance.cost[removed] + instance.d(before, added) + instance.d(added, after)
        - instance.d(before, removed)
        - instance.d(removed, after)
}

/// Remove edges (a, b) and (c, d), add (a, c) and (b, d).
fn swap_edges_delta(instance: &Instance, a: usize, b: usize, c: usize, d: usize) -> i64 {
    instance.d(a, c) + instance.d(b, d) - instance.d(a, b) - instance.d(c, d)
}

/// LM: every improving move of the tour, sorted from the worst: the best move is at the end, so removing
/// the moves browsed first (the best ones) shifts only the few moves after them.
///
/// For every pair of tour edges a -> b, c -> d two moves are stored:
///   same direction      remove (a, b), (c, d), add (a, c), (b, d)   applicable now
///   inverted direction  remove (a, b), (d, c), add (a, d), (b, c)   applicable after one edge gets reversed
fn evaluate_all_moves(instance: &Instance, tour: &[usize], position: &[Option<usize>]) -> Vec<Move> {
    let k = tour.len();
    let mut moves = Vec::new();
    let mut add_if_improving = |delta: i64, kind: MoveKind, n1: usize, n2: usize, n3: usize, n4: usize| {
        if delta < 0 {
            moves.push(Move { delta, kind, n1, n2, n3, n4 });
        }
    };

    for i in 0..k {
        let (before, removed, after) = (tour[(i + k - 1) % k], tour[i], tour[(i + 1) % k]);
        for added in 0..instance.n {
            if position[added].is_some() {
                continue;
            }
            let delta = exchange_selected_delta(instance, before, removed, after, added);
            add_if_improving(delta, MoveKind::ExchangeSelected, before, removed, after, added);
        }
    }

    for i in 0..k {
        for j in i + 2..k {
            if i == 0 && j == k - 1 {
                continue; // the edges share node tour[0]
            }
            let (a, b) = (tour[i], tour[i + 1]);
            let (c, d) = (tour[j], tour[(j + 1) % k]);
            add_if_improving(swap_edges_delta(instance, a, b, c, d), MoveKind::SwapEdges, a, b, c, d);
            add_if_improving(swap_edges_delta(instance, a, b, d, c), MoveKind::SwapEdges, a, b, d, c);
        }
    }

    moves.sort_by(|x, y| y.cmp(x));
    moves
}

// ---------------------------------------------------------------------------
// Browsing LM: is a stored move still valid, applicable, and applying it
// ---------------------------------------------------------------------------

/// +1 if the tour goes x -> y, -1 if it goes y -> x, 0 if (x, y) is not a tour edge.
fn edge_direction(tour: &[usize], position: &[Option<usize>], x: usize, y: usize) -> i32 {
    let (Some(i), Some(_)) = (position[x], position[y]) else {
        return 0;
    };
    let k = tour.len();
    if tour[(i + 1) % k] == y {
        1
    } else if tour[(i + k - 1) % k] == y {
        -1
    } else {
        0
    }
}

/// Obsolete, NotApplicableNow or Applicable (see the comment at the top).
fn move_status(tour: &[usize], position: &[Option<usize>], mv: &Move) -> MoveStatus {
    let first_direction = edge_direction(tour, position, mv.n1, mv.n2);
    if mv.kind == MoveKind::ExchangeSelected {
        let (removed, after, added) = (mv.n2, mv.n3, mv.n4);
        let second_direction = edge_direction(tour, position, removed, after);
        if first_direction == 0 || second_direction == 0 || position[added].is_some() {
            return MoveStatus::Obsolete;
        }
        return MoveStatus::Applicable; // before-removed-after are consecutive, so both edges have the same direction
    }

    let second_direction = edge_direction(tour, position, mv.n3, mv.n4);
    if first_direction == 0 || second_direction == 0 {
        MoveStatus::Obsolete
    } else if first_direction != second_direction {
        MoveStatus::NotApplicableNow
    } else {
        MoveStatus::Applicable
    }
}

/// Browse LM from the best move: remove obsolete moves, skip the not applicable ones,
/// remove and return the first applicable move (or None).
fn take_applicable_move(moves: &mut Vec<Move>, tour: &[usize], position: &[Option<usize>]) -> Option<Move> {
    for index in (0..moves.len()).rev() {
        match move_status(tour, position, &moves[index]) {
            MoveStatus::Obsolete => {
                moves.remove(index);
            }
            MoveStatus::NotApplicableNow => {}
            MoveStatus::Applicable => return Some(moves.remove(index)),
        }
    }
    None
}

fn apply_move(tour: &mut [usize], position: &mut [Option<usize>], mv: &Move) {
    if mv.kind == MoveKind::ExchangeSelected {
        let (removed, added) = (mv.n2, mv.n4);
        let i = position[removed].unwrap();
        position[removed] = None;
        tour[i] = added;
        position[added] = Some(i);
        return;
    }

    let (a, b, c, d) = (mv.n1, mv.n2, mv.n3, mv.n4);
    let (i, j) = if edge_direction(tour, position, a, b) == 1 {
        (position[a].unwrap(), position[c].unwrap()) // tour: a -> b ... c -> d
    } else {
        (position[b].unwrap(), position[d].unwrap()) // tour: b -> a ... d -> c (both reversed)
    };
    let (i, j) = (i.min(j), i.max(j));
    // remove edges (tour[i], tour[i+1]) and (tour[j], tour[j+1]): reverse tour[i+1..j]
    tour[i + 1..=j].reverse();
    for p in i + 1..=j {
        position[tour[p]] = Some(p);
    }
}

/// Apply moves from LM; when LM has no applicable move, evaluate all moves again.
/// Stop when a fresh LM has no applicable move (local optimum).
///
/// LM is rebuilt from scratch instead of adding the new moves to it: the moves left in LM
/// (not applicable now) are found again by the full evaluation, with the same delta,
/// because a delta depends only on the nodes of the move.
fn local_search(instance: &Instance, mut tour: Vec<usize>) -> Vec<usize> {
    let mut position = tour_positions(instance, &tour);

    loop {
        let mut moves = evaluate_all_moves(instance, &tour, &position);
        let Some(mut mv) = take_applicable_move(&mut moves, &tour, &position) else {
            return tour;
        };

        loop {
            apply_move(&mut tour, &mut position, &mv);
            match take_applicable_move(&mut moves, &tour, &position) {
                Some(next) => mv = next,
                None => break,
            }
        }
    }
}

fn objective(instance: &Instance, tour: &[usize]) -> i64 {
    let (cycle_length, total_cost) = instance.evaluate(tour);
    cycle_length + total_cost
}

// ---------------------------------------------------------------------------
// Multiple start local search
// ---------------------------------------------------------------------------

/// The best of MSLS_LOCAL_SEARCHES local searches from random solutions.
fn multiple_start_local_search(instance: &Instance, rng: &mut Rng, trace: bool) -> Vec<usize> {
    let mut best_tour = Vec::new();
    let mut best_objective = i64::MAX;
    for _ in 0..MSLS_LOCAL_SEARCHES {
        let tour = local_search(instance, random_start(instance, rng));
        if trace {
            emit_step(&tour);
        }
        let tour_objective = objective(instance, &tour);
        if tour_objective < best_objective {
            best_tour = tour;
            best_objective = tour_objective;
        }
    }

    tsp::report_iterations(MSLS_LOCAL_SEARCHES as i64);
    best_tour
}

// ---------------------------------------------------------------------------
// Iterated local search
// ---------------------------------------------------------------------------

/// Cut the tour into 4 non-empty parts A B C D and reconnect them as A C B D.
fn double_bridge(tour: &[usize], rng: &mut Rng) -> Vec<usize> {
    let k = tour.len();
    let mut cuts: Vec<usize> = Vec::new(); // 3 different cut positions in 1 .. k-1
    while cuts.len() < 3 {
        let cut = 1 + rng.below(k - 1);
        if !cuts.contains(&cut) {
            cuts.push(cut);
        }
    }
    cuts.sort();
    let (first, second, third) = (cuts[0], cuts[1], cuts[2]);
    [&tour[..first], &tour[second..third], &tour[first..second], &tour[third..]].concat() // A C B D
}

/// Replace NODE_EXCHANGES random selected nodes with random unselected ones (in place).
fn exchange_random_nodes(instance: &Instance, tour: &mut [usize], rng: &mut Rng) {
    let mut selected = vec![false; instance.n];
    for &node in tour.iter() {
        selected[node] = true;
    }

    for _ in 0..NODE_EXCHANGES {
        let i = rng.below(tour.len());
        let mut added = rng.below(instance.n);
        while selected[added] {
            added = rng.below(instance.n);
        }
        selected[tour[i]] = false;
        tour[i] = added;
        selected[added] = true;
    }
}

/// A new tour: double bridge, then exchange of a few selected nodes with unselected ones.
fn perturb(instance: &Instance, tour: &[usize], rng: &mut Rng) -> Vec<usize> {
    let mut perturbed = double_bridge(tour, rng);
    exchange_random_nodes(instance, &mut perturbed, rng);
    perturbed
}

/// Perturb the current local optimum and run local search on it, until the time limit.
/// Keep the result only if it is strictly better than the current local optimum.
fn iterated_local_search(instance: &Instance, rng: &mut Rng, trace: bool) -> Vec<usize> {
    let deadline = Instant::now() + Duration::from_nanos(tsp::time_limit() as u64);

    let mut current = local_search(instance, random_start(instance, rng));
    let mut current_objective = objective(instance, &current);
    let mut local_searches: i64 = 1;
    if trace {
        emit_step(&current);
    }

    while Instant::now() < deadline {
        let candidate = perturb(instance, &current, rng);
        if trace {
            emit_step(&candidate);
        }
        let candidate = local_search(instance, candidate);
        local_searches += 1;
        if trace {
            emit_step(&candidate);
        }

        let candidate_objective = objective(instance, &candidate);
        if candidate_objective < current_objective {
            current = candidate;
            current_objective = candidate_objective;
        }
    }

    tsp::report_iterations(local_searches);
    current
}

// ---------------------------------------------------------------------------
// Entry points required by the protocol
// ---------------------------------------------------------------------------

/// Run one run of a method and return the resulting tour.
fn solve(instance: &Instance, method: &str, _run: usize, seed: u64, trace: bool) -> Vec<usize> {
    let mut rng = Rng::new(seed);
    if method == MSLS {
        multiple_start_local_search(instance, &mut rng, trace)
    } else {
        iterated_local_search(instance, &mut rng, trace)
    }
}

/// Number of runs of a method, or None if the method does not exist.
fn runs_for(method: &str, _n: usize) -> Option<usize> {
    [MSLS, ILS].contains(&method).then_some(RUNS)
}

fn main() {
    tsp::run(solve, runs_for);
}
