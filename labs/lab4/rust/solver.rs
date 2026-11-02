//! Lab 4 - candidate moves in local search.
//!
//! Method: steepest local search from a random solution with the neighborhood that was best in lab 3:
//!   inter-route  exchange a selected node with an unselected one
//!   intra-route  edges exchange (2-opt, reverses a segment)
//! but only candidate moves are evaluated: moves that add at least one candidate edge to the tour.
//! Edge (a, b) is a candidate edge if b is one of the CANDIDATE_COUNT nearest nodes of a,
//! "nearest" measured by d(a, b) + cost[b].
//!
//! The candidate moves are generated directly (never by filtering the full neighborhood):
//! for every selected node a and every candidate b of a, the two moves that add edge (a, b).
//! Every move is evaluated by its delta (change of the objective), never from scratch.
//!
//! Run i uses seed + i, so it starts from the same random solution as lab 3's steepest LS
//! (the baseline without candidate moves).
#[path = "../../../common/rust/tsp.rs"]
mod tsp;

use tsp::{emit_step, Instance, Rng};

/// Nearest nodes of every node that define the candidate edges.
const CANDIDATE_COUNT: usize = 10;

/// Move kinds. A move is (kind, i, j):
#[derive(Clone, Copy)]
enum MoveKind {
    /// inter-route: tour[i] is replaced by the unselected node j
    ExchangeSelected,
    /// intra-route: edges (tour[i], tour[i+1]) and (tour[j], tour[j+1]) -> reverse tour[i+1..j], i < j
    SwapEdges,
}

#[derive(Clone, Copy)]
struct Move {
    kind: MoveKind,
    i: usize,
    j: usize,
}

const METHODS: [&str; 1] = ["ls_candidates_steepest_edges_random"];

// ---------------------------------------------------------------------------
// Candidate edges and the starting solution
// ---------------------------------------------------------------------------

/// For every node a: the CANDIDATE_COUNT other nodes b with the smallest d(a, b) + cost[b].
fn nearest_candidates(instance: &Instance) -> Vec<Vec<usize>> {
    (0..instance.n)
        .map(|a| {
            let mut others: Vec<usize> = (0..instance.n).filter(|&b| b != a).collect();
            others.sort_by_key(|&b| instance.d(a, b) + instance.cost[b]); // stable sort: ties by node index
            others.truncate(CANDIDATE_COUNT);
            others
        })
        .collect()
}

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
// Moves: deltas and application
// ---------------------------------------------------------------------------

/// Replace tour[i] with new_node: its two edges and its cost change.
fn exchange_selected_delta(instance: &Instance, tour: &[usize], i: usize, new_node: usize) -> i64 {
    let k = tour.len();
    let (before, old_node, after) = (tour[(i + k - 1) % k], tour[i], tour[(i + 1) % k]);
    instance.cost[new_node] - instance.cost[old_node] + instance.d(before, new_node) + instance.d(new_node, after)
        - instance.d(before, old_node)
        - instance.d(old_node, after)
}

/// Remove edges (a, b) = (tour[i], tour[i+1]) and (c, d) = (tour[j], tour[j+1]),
/// add (a, c) and (b, d), i < j.
fn swap_edges_delta(instance: &Instance, tour: &[usize], i: usize, j: usize) -> i64 {
    let (a, b) = (tour[i], tour[i + 1]);
    let (c, d) = (tour[j], tour[(j + 1) % tour.len()]);
    instance.d(a, c) + instance.d(b, d) - instance.d(a, b) - instance.d(c, d)
}

/// Change of the objective if the move were applied (negative = better).
fn move_delta(instance: &Instance, tour: &[usize], mv: Move) -> i64 {
    match mv.kind {
        MoveKind::ExchangeSelected => exchange_selected_delta(instance, tour, mv.i, mv.j),
        MoveKind::SwapEdges => swap_edges_delta(instance, tour, mv.i, mv.j),
    }
}

fn apply_move(tour: &mut [usize], position: &mut [Option<usize>], mv: Move) {
    match mv.kind {
        MoveKind::ExchangeSelected => {
            position[tour[mv.i]] = None;
            tour[mv.i] = mv.j;
            position[mv.j] = Some(mv.i);
        }
        MoveKind::SwapEdges => {
            tour[mv.i + 1..=mv.j].reverse();
            for p in mv.i + 1..=mv.j {
                position[tour[p]] = Some(p);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Candidate moves
// ---------------------------------------------------------------------------

/// Edges exchange of the edges starting at positions i and j, in either order.
fn edges_exchange(i: usize, j: usize) -> Move {
    Move { kind: MoveKind::SwapEdges, i: i.min(j), j: i.max(j) }
}

/// The two moves that add edge (a, b) to the tour, a = tour[i]. None if (a, b) is already in it.
fn moves_adding_edge(tour: &[usize], position: &[Option<usize>], i: usize, b: usize) -> Option<[Move; 2]> {
    let k = tour.len();
    let (successor, predecessor) = ((i + 1) % k, (i + k - 1) % k);

    let Some(j) = position[b] else {
        // b replaces the successor of a (new edge a-b) or the predecessor of a (new edge b-a)
        return Some([
            Move { kind: MoveKind::ExchangeSelected, i: successor, j: b },
            Move { kind: MoveKind::ExchangeSelected, i: predecessor, j: b },
        ]);
    };

    if j == successor || j == predecessor {
        return None; // edge already in the tour
    }

    // remove the edges after a and after b: adds (a, b) and (succ a, succ b)
    // remove the edges before a and before b: adds (pred a, pred b) and (a, b)
    Some([edges_exchange(i, j), edges_exchange(predecessor, (j + k - 1) % k)])
}

/// Steepest over candidate moves only: return the best improving one (or None).
fn find_best_candidate_move(instance: &Instance, tour: &[usize], position: &[Option<usize>], candidates: &[Vec<usize>]) -> Option<Move> {
    let mut best_move = None;
    let mut best_delta = 0;
    for (i, &a) in tour.iter().enumerate() {
        for &b in &candidates[a] {
            let Some(moves) = moves_adding_edge(tour, position, i, b) else {
                continue;
            };
            for mv in moves {
                let delta = move_delta(instance, tour, mv);
                if delta < best_delta {
                    best_move = Some(mv);
                    best_delta = delta;
                }
            }
        }
    }
    best_move
}

// ---------------------------------------------------------------------------
// Local search
// ---------------------------------------------------------------------------

/// Apply the best candidate move until no candidate move improves (local optimum).
fn local_search(instance: &Instance, mut tour: Vec<usize>, candidates: &[Vec<usize>], trace: bool) -> Vec<usize> {
    let mut position = tour_positions(instance, &tour);
    if trace {
        emit_step(&tour);
    }

    loop {
        let Some(mv) = find_best_candidate_move(instance, &tour, &position, candidates) else {
            return tour;
        };

        apply_move(&mut tour, &mut position, mv);
        if trace {
            emit_step(&tour);
        }
    }
}

// ---------------------------------------------------------------------------
// Entry points required by the protocol
// ---------------------------------------------------------------------------

/// Run one run of a method and return the resulting tour.
fn solve(instance: &Instance, _method: &str, _run: usize, seed: u64, trace: bool) -> Vec<usize> {
    let mut rng = Rng::new(seed);
    let candidates = nearest_candidates(instance);
    let tour = random_start(instance, &mut rng);
    local_search(instance, tour, &candidates, trace)
}

/// Number of runs of a method, or None if the method does not exist.
fn runs_for(method: &str, n: usize) -> Option<usize> {
    METHODS.contains(&method).then_some(n) // random starts, as many as in lab 3
}

fn main() {
    tsp::run(solve, runs_for);
}
