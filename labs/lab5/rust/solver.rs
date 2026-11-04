//! Lab 5 - move evaluations from previous iterations (list of improving moves) in local search.
//!
//! Method: steepest local search from a random solution with the neighborhood that was best in lab 3:
//!   inter-route  exchange a selected node with an unselected one
//!   intra-route  edges exchange (2-opt, reverses a segment)
//! but improving moves are kept in a list LM (best first) and reused in later iterations
//! instead of evaluating the whole neighborhood in every iteration:
//!
//!   LM = all improving moves of the solution, sorted from the best
//!   repeat
//!       browse LM from the best until an applicable move is found (removing the obsolete ones)
//!       found     -> apply it
//!       not found -> evaluate all moves again into LM, stop if none is applicable
//!
//! A move is stored by the nodes of the edges it removes, not by positions (positions change).
//! When browsing LM, a stored move is in one of 3 situations:
//!   a removed edge is no longer in the tour                      -> obsolete, remove it from LM
//!   the removed edges are in the tour, but one of them reversed  -> not applicable now, keep it
//!   the removed edges are in the tour in the same relative
//!   direction (both as stored or both reversed)                  -> apply it and remove it from LM
//! That is why, when evaluating, both relative directions of every pair of edges are stored.
//!
//! Run i uses seed + i, so it starts from the same random solution as lab 3's steepest LS
//! (the baseline without the list of moves).
#[path = "../../../common/rust/tsp.rs"]
mod tsp;

use tsp::{emit_step, Instance, Rng};

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

const METHODS: [&str; 1] = ["ls_move_list_steepest_edges_random"];

// ---------------------------------------------------------------------------
// Starting solution
// ---------------------------------------------------------------------------

/// Random k nodes in random order (the same solution as lab 3 for the same seed).
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

// ---------------------------------------------------------------------------
// Local search
// ---------------------------------------------------------------------------

/// Apply moves from LM; when LM has no applicable move, evaluate all moves again.
/// Stop when a fresh LM has no applicable move (local optimum).
///
/// LM is rebuilt from scratch instead of adding the new moves to it: the moves left in LM
/// (not applicable now) are found again by the full evaluation, with the same delta,
/// because a delta depends only on the nodes of the move.
fn local_search(instance: &Instance, mut tour: Vec<usize>, trace: bool) -> Vec<usize> {
    let mut position = tour_positions(instance, &tour);
    if trace {
        emit_step(&tour);
    }

    loop {
        let mut moves = evaluate_all_moves(instance, &tour, &position);
        let Some(mut mv) = take_applicable_move(&mut moves, &tour, &position) else {
            return tour;
        };

        loop {
            apply_move(&mut tour, &mut position, &mv);
            if trace {
                emit_step(&tour);
            }
            match take_applicable_move(&mut moves, &tour, &position) {
                Some(next) => mv = next,
                None => break,
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Entry points required by the protocol
// ---------------------------------------------------------------------------

/// Run one run of a method and return the resulting tour.
fn solve(instance: &Instance, _method: &str, _run: usize, seed: u64, trace: bool) -> Vec<usize> {
    let mut rng = Rng::new(seed);
    let tour = random_start(instance, &mut rng);
    local_search(instance, tour, trace)
}

/// Number of runs of a method, or None if the method does not exist.
fn runs_for(method: &str, n: usize) -> Option<usize> {
    METHODS.contains(&method).then_some(n) // random starts, as many as in lab 3
}

fn main() {
    tsp::run(solve, runs_for);
}
