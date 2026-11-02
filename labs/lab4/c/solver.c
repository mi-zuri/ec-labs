/* Lab 4 - candidate moves in local search.
 *
 * Method: steepest local search from a random solution with the neighborhood that was best in lab 3:
 *   inter-route  exchange a selected node with an unselected one
 *   intra-route  edges exchange (2-opt, reverses a segment)
 * but only candidate moves are evaluated: moves that add at least one candidate edge to the tour.
 * Edge (a, b) is a candidate edge if b is one of the CANDIDATE_COUNT nearest nodes of a,
 * "nearest" measured by D(a, b) + cost[b].
 *
 * The candidate moves are generated directly (never by filtering the full neighborhood):
 * for every selected node a and every candidate b of a, the two moves that add edge (a, b).
 * Every move is evaluated by its delta (change of the objective), never from scratch.
 *
 * Run i uses seed + i, so it starts from the same random solution as lab 3's steepest LS
 * (the baseline without candidate moves).
 */
#include "../../../common/c/tsp.h"

#define CANDIDATE_COUNT 10 /* nearest nodes of every node that define the candidate edges */

#define NOT_SELECTED (-1) /* position of a node that is not in the tour */

/* Move kinds. A move is (kind, i, j): */
typedef enum {
    EXCHANGE_SELECTED, /* inter-route: tour[i] is replaced by the unselected node j */
    SWAP_EDGES         /* intra-route: edges (tour[i], tour[i+1]) and (tour[j], tour[j+1]) -> reverse tour[i+1..j], i < j */
} MoveKind;

typedef struct {
    MoveKind kind;
    int i, j;
} Move;

static const char *METHODS[] = {"ls_candidates_steepest_edges_random"};
static const int METHOD_COUNT = sizeof(METHODS) / sizeof(METHODS[0]);

/* ------------------------------------------------------------------------- */
/* Candidate edges and the starting solution                                 */
/* ------------------------------------------------------------------------- */

/* For every node a: the CANDIDATE_COUNT other nodes b with the smallest D(a, b) + cost[b].
 * candidates[a * CANDIDATE_COUNT + c] is the c-th nearest node of a. Ties: lower node index first. */
static int *nearest_candidates(const Instance *instance) {
    int n = instance->n;
    int *candidates = malloc((size_t)n * CANDIDATE_COUNT * sizeof(int));
    char *taken = malloc(n);

    for (int a = 0; a < n; a++) {
        memset(taken, 0, n);
        taken[a] = 1;
        /* selection: CANDIDATE_COUNT times pick the nearest node not taken yet */
        for (int c = 0; c < CANDIDATE_COUNT; c++) {
            int nearest = -1;
            long long nearest_value = LLONG_MAX;
            for (int b = 0; b < n; b++) {
                if (taken[b]) continue;
                long long value = D(instance, a, b) + instance->cost[b];
                if (value < nearest_value) {
                    nearest = b;
                    nearest_value = value;
                }
            }
            taken[nearest] = 1;
            candidates[a * CANDIDATE_COUNT + c] = nearest;
        }
    }
    free(taken);
    return candidates;
}

/* Random k nodes in random order (the same solution as lab 3 for the same seed). */
static void random_start(const Instance *instance, Rng *rng, int *tour) {
    int *nodes = malloc(instance->n * sizeof(int));
    for (int v = 0; v < instance->n; v++) nodes[v] = v;
    rng_shuffle(rng, nodes, instance->n);
    memcpy(tour, nodes, instance->k * sizeof(int));
    free(nodes);
}

/* position[v] = index of v in the tour, or NOT_SELECTED. */
static void tour_positions(const Instance *instance, const int *tour, int *position) {
    for (int v = 0; v < instance->n; v++) position[v] = NOT_SELECTED;
    for (int i = 0; i < instance->k; i++) position[tour[i]] = i;
}

/* ------------------------------------------------------------------------- */
/* Moves: deltas and application                                             */
/* ------------------------------------------------------------------------- */

/* Replace tour[i] with new_node: its two edges and its cost change. */
static long long exchange_selected_delta(const Instance *instance, const int *tour, int k, int i, int new_node) {
    int before = tour[(i - 1 + k) % k], old_node = tour[i], after = tour[(i + 1) % k];
    return (long long)instance->cost[new_node] - instance->cost[old_node] + D(instance, before, new_node) +
           D(instance, new_node, after) - D(instance, before, old_node) - D(instance, old_node, after);
}

/* Remove edges (a, b) = (tour[i], tour[i+1]) and (c, d) = (tour[j], tour[j+1]),
 * add (a, c) and (b, d), i < j. */
static long long swap_edges_delta(const Instance *instance, const int *tour, int k, int i, int j) {
    int a = tour[i], b = tour[i + 1];
    int c = tour[j], d = tour[(j + 1) % k];
    return D(instance, a, c) + D(instance, b, d) - D(instance, a, b) - D(instance, c, d);
}

/* Change of the objective if the move were applied (negative = better). */
static long long move_delta(const Instance *instance, const int *tour, int k, Move move) {
    if (move.kind == EXCHANGE_SELECTED) return exchange_selected_delta(instance, tour, k, move.i, move.j);
    return swap_edges_delta(instance, tour, k, move.i, move.j);
}

static void apply_move(int *tour, int *position, Move move) {
    if (move.kind == EXCHANGE_SELECTED) {
        position[tour[move.i]] = NOT_SELECTED;
        tour[move.i] = move.j;
        position[move.j] = move.i;
    } else { /* reverse tour[i+1..j] */
        for (int left = move.i + 1, right = move.j; left < right; left++, right--) {
            int swapped = tour[left];
            tour[left] = tour[right];
            tour[right] = swapped;
        }
        for (int p = move.i + 1; p <= move.j; p++) position[tour[p]] = p;
    }
}

/* ------------------------------------------------------------------------- */
/* Candidate moves                                                           */
/* ------------------------------------------------------------------------- */

/* Edges exchange of the edges starting at positions i and j, in either order. */
static Move edges_exchange(int i, int j) { return (Move){SWAP_EDGES, i < j ? i : j, i < j ? j : i}; }

/* The two moves that add edge (a, b) to the tour, a = tour[i], stored in moves.
 * Returns how many (0 if (a, b) is already in the tour). */
static int moves_adding_edge(const int *position, int k, int i, int b, Move moves[2]) {
    int successor = (i + 1) % k, predecessor = (i - 1 + k) % k;
    int j = position[b];

    if (j == NOT_SELECTED) {
        /* b replaces the successor of a (new edge a-b) or the predecessor of a (new edge b-a) */
        moves[0] = (Move){EXCHANGE_SELECTED, successor, b};
        moves[1] = (Move){EXCHANGE_SELECTED, predecessor, b};
        return 2;
    }

    if (j == successor || j == predecessor) return 0; /* edge already in the tour */

    /* remove the edges after a and after b: adds (a, b) and (succ a, succ b)
     * remove the edges before a and before b: adds (pred a, pred b) and (a, b) */
    moves[0] = edges_exchange(i, j);
    moves[1] = edges_exchange(predecessor, (j - 1 + k) % k);
    return 2;
}

/* Steepest over candidate moves only: store the best improving one. Returns 0 if none improves. */
static int find_best_candidate_move(const Instance *instance, const int *tour, const int *position,
                                    const int *candidates, Move *best_move) {
    int k = instance->k;
    long long best_delta = 0;
    for (int i = 0; i < k; i++) {
        int a = tour[i];
        for (int c = 0; c < CANDIDATE_COUNT; c++) {
            Move moves[2];
            int count = moves_adding_edge(position, k, i, candidates[a * CANDIDATE_COUNT + c], moves);
            for (int m = 0; m < count; m++) {
                long long delta = move_delta(instance, tour, k, moves[m]);
                if (delta < best_delta) {
                    *best_move = moves[m];
                    best_delta = delta;
                }
            }
        }
    }
    return best_delta < 0;
}

/* ------------------------------------------------------------------------- */
/* Local search                                                              */
/* ------------------------------------------------------------------------- */

/* Apply the best candidate move until no candidate move improves (local optimum). */
static void local_search(const Instance *instance, int *tour, const int *candidates, int trace) {
    int *position = malloc(instance->n * sizeof(int));
    tour_positions(instance, tour, position);
    if (trace) emit_step(tour, instance->k);

    Move move;
    while (find_best_candidate_move(instance, tour, position, candidates, &move)) {
        apply_move(tour, position, move);
        if (trace) emit_step(tour, instance->k);
    }
    free(position);
}

/* ------------------------------------------------------------------------- */
/* Entry points required by the protocol                                     */
/* ------------------------------------------------------------------------- */

/* Run one run of a method and return the number of nodes in the resulting tour. */
static int solve(const Instance *instance, const char *method, int run, uint64_t seed, int trace, int *tour) {
    (void)method;
    (void)run;
    Rng rng = {seed};
    int *candidates = nearest_candidates(instance);
    random_start(instance, &rng, tour);
    local_search(instance, tour, candidates, trace);
    free(candidates);
    return instance->k;
}

/* Number of runs of a method, or -1 if the method does not exist. */
static int runs_for(const char *method, int n) {
    for (int m = 0; m < METHOD_COUNT; m++) {
        if (!strcmp(METHODS[m], method)) return n; /* random starts, as many as in lab 3 */
    }
    return -1;
}

int main(int argc, char **argv) { return run_solver(argc, argv, solve, runs_for); }
