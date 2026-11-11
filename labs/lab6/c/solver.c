/* Lab 6 - multiple start local search (MSLS) and iterated local search (ILS).
 *
 * Both use the local search of lab 5: steepest, a selected node <-> unselected node exchange
 * plus edges exchange (2-opt), with a list of improving moves (LM). In this file "local search"
 * always means one full run of it, from a solution to its local optimum.
 *
 * MSLS: the best of 400 local searches, each from a new random solution.
 *
 *   best = none
 *   repeat 400 times
 *       x = local_search(random solution)
 *       if x is better than best: best = x
 *
 * ILS: one random solution, then repeatedly kick the current local optimum out of its basin
 * (perturbation) and descend again. Only a strictly better local optimum becomes the current one.
 * It stops after the average running time of MSLS (given by the server as --time-limit-ms),
 * so both methods get the same time.
 *
 *   x = local_search(random solution)
 *   repeat until the time is up
 *       y = local_search(perturb(x))
 *       if y is better than x: x = y
 *
 * Perturbation (designed for this problem: a cycle through half of the nodes):
 *   1. double bridge: cut the tour into 4 parts A B C D at 3 random places and reconnect
 *      them as A C B D. It changes 3 edges at once in a way a single 2-opt move cannot undo.
 *   2. exchange NODE_EXCHANGES random selected nodes with random unselected ones, which changes
 *      the set of selected nodes (the double bridge only changes the order).
 *
 * Run i uses seed + i; its first random solution is the same as in lab 5's run i.
 */
#include "../../../common/c/tsp.h"

#define MSLS "msls"
#define ILS "ils"

#define RUNS 20                  /* runs of each method */
#define MSLS_LOCAL_SEARCHES 400  /* local searches in one run of MSLS */
#define NODE_EXCHANGES 3         /* perturbation: selected nodes replaced with random unselected ones */
#define NOT_SELECTED (-1) /* position of a node that is not in the tour */

/* Move kinds. A move is (delta, kind, n1, n2, n3, n4) of nodes: */
typedef enum {
    EXCHANGE_SELECTED, /* inter-route: (before, removed, after, added)
                        * removes edges (before, removed), (removed, after); added takes the place of removed */
    SWAP_EDGES         /* intra-route: (a, b, c, d) removes edges (a, b), (c, d) and adds (a, c), (b, d) */
} MoveKind;

typedef struct {
    long long delta;
    MoveKind kind;
    int n1, n2, n3, n4;
} Move;

/* Situations of a stored move in the current tour */
typedef enum {
    OBSOLETE,           /* a removed edge (or the added node) is gone: remove the move from LM */
    NOT_APPLICABLE_NOW, /* removed edges are there, but in a different relative direction: keep it */
    APPLICABLE          /* removed edges are there in the same relative direction: apply it */
} MoveStatus;

/* LM: improving moves sorted from the worst: the best move is at the end, so removing the moves
 * browsed first (the best ones) shifts only the few moves after them */
typedef struct {
    Move *moves;
    int count;
} MoveList;

/* ------------------------------------------------------------------------- */
/* Local search with a list of improving moves (lab 5, unchanged)            */
/* ------------------------------------------------------------------------- */

/* Random k nodes in random order. */
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
/* Evaluating all moves (fills LM)                                           */
/* ------------------------------------------------------------------------- */

/* Replace node removed (between before and after) with node added. */
static long long exchange_selected_delta(const Instance *instance, int before, int removed, int after, int added) {
    return (long long)instance->cost[added] - instance->cost[removed] + D(instance, before, added) +
           D(instance, added, after) - D(instance, before, removed) - D(instance, removed, after);
}

/* Remove edges (a, b) and (c, d), add (a, c) and (b, d). */
static long long swap_edges_delta(const Instance *instance, int a, int b, int c, int d) {
    return (long long)D(instance, a, c) + D(instance, b, d) - D(instance, a, b) - D(instance, c, d);
}

/* Order of moves: by delta, ties by kind, then by nodes (the same order as Python's tuples). */
static int compare_moves(const void *left, const void *right) {
    const Move *x = left, *y = right;
    long long xs[6] = {x->delta, x->kind, x->n1, x->n2, x->n3, x->n4};
    long long ys[6] = {y->delta, y->kind, y->n1, y->n2, y->n3, y->n4};
    for (int f = 0; f < 6; f++) {
        if (xs[f] != ys[f]) return xs[f] < ys[f] ? -1 : 1;
    }
    return 0;
}

static int compare_moves_worst_first(const void *left, const void *right) { return compare_moves(right, left); }

static void add_if_improving(MoveList *list, long long delta, MoveKind kind, int n1, int n2, int n3, int n4) {
    if (delta < 0) list->moves[list->count++] = (Move){delta, kind, n1, n2, n3, n4};
}

/* LM: every improving move of the tour, sorted from the worst (the best move is last).
 *
 * For every pair of tour edges a -> b, c -> d two moves are stored:
 *   same direction      remove (a, b), (c, d), add (a, c), (b, d)   applicable now
 *   inverted direction  remove (a, b), (d, c), add (a, d), (b, c)   applicable after one edge gets reversed */
static void evaluate_all_moves(const Instance *instance, const int *tour, const int *position, MoveList *list) {
    int k = instance->k;
    list->count = 0;

    for (int i = 0; i < k; i++) {
        int before = tour[(i - 1 + k) % k], removed = tour[i], after = tour[(i + 1) % k];
        for (int added = 0; added < instance->n; added++) {
            if (position[added] != NOT_SELECTED) continue;
            add_if_improving(list, exchange_selected_delta(instance, before, removed, after, added), EXCHANGE_SELECTED,
                             before, removed, after, added);
        }
    }

    for (int i = 0; i < k; i++) {
        for (int j = i + 2; j < k; j++) {
            if (i == 0 && j == k - 1) continue; /* the edges share node tour[0] */
            int a = tour[i], b = tour[i + 1];
            int c = tour[j], d = tour[(j + 1) % k];
            add_if_improving(list, swap_edges_delta(instance, a, b, c, d), SWAP_EDGES, a, b, c, d);
            add_if_improving(list, swap_edges_delta(instance, a, b, d, c), SWAP_EDGES, a, b, d, c);
        }
    }

    qsort(list->moves, list->count, sizeof(Move), compare_moves_worst_first);
}

/* ------------------------------------------------------------------------- */
/* Browsing LM: is a stored move still valid, applicable, and applying it    */
/* ------------------------------------------------------------------------- */

/* +1 if the tour goes x -> y, -1 if it goes y -> x, 0 if (x, y) is not a tour edge. */
static int edge_direction(const int *tour, const int *position, int k, int x, int y) {
    if (position[x] == NOT_SELECTED || position[y] == NOT_SELECTED) return 0;
    if (tour[(position[x] + 1) % k] == y) return 1;
    if (tour[(position[x] - 1 + k) % k] == y) return -1;
    return 0;
}

/* OBSOLETE, NOT_APPLICABLE_NOW or APPLICABLE (see the comment at the top). */
static MoveStatus move_status(const int *tour, const int *position, int k, const Move *move) {
    int first_direction = edge_direction(tour, position, k, move->n1, move->n2);
    if (move->kind == EXCHANGE_SELECTED) {
        int removed = move->n2, after = move->n3, added = move->n4;
        int second_direction = edge_direction(tour, position, k, removed, after);
        if (first_direction == 0 || second_direction == 0 || position[added] != NOT_SELECTED) return OBSOLETE;
        return APPLICABLE; /* before-removed-after are consecutive, so both edges have the same direction */
    }

    int second_direction = edge_direction(tour, position, k, move->n3, move->n4);
    if (first_direction == 0 || second_direction == 0) return OBSOLETE;
    if (first_direction != second_direction) return NOT_APPLICABLE_NOW;
    return APPLICABLE;
}

static void remove_move(MoveList *list, int index) {
    memmove(&list->moves[index], &list->moves[index + 1], (size_t)(list->count - index - 1) * sizeof(Move));
    list->count--;
}

/* Browse LM from the best move: remove obsolete moves, skip the not applicable ones,
 * remove the first applicable move and store it in move. Returns 0 if there is none. */
static int take_applicable_move(MoveList *list, const int *tour, const int *position, int k, Move *move) {
    for (int index = list->count - 1; index >= 0; index--) {
        MoveStatus status = move_status(tour, position, k, &list->moves[index]);
        if (status == OBSOLETE) {
            remove_move(list, index);
        } else if (status == APPLICABLE) {
            *move = list->moves[index];
            remove_move(list, index);
            return 1;
        }
    }
    return 0;
}

static void apply_move(int *tour, int *position, int k, const Move *move) {
    if (move->kind == EXCHANGE_SELECTED) {
        int removed = move->n2, added = move->n4;
        int i = position[removed];
        position[removed] = NOT_SELECTED;
        tour[i] = added;
        position[added] = i;
        return;
    }

    int a = move->n1, b = move->n2, c = move->n3, d = move->n4;
    int i, j;
    if (edge_direction(tour, position, k, a, b) == 1) {
        i = position[a]; /* tour: a -> b ... c -> d */
        j = position[c];
    } else {
        i = position[b]; /* tour: b -> a ... d -> c (both reversed) */
        j = position[d];
    }
    if (i > j) {
        int swapped = i;
        i = j;
        j = swapped;
    }
    /* remove edges (tour[i], tour[i+1]) and (tour[j], tour[j+1]): reverse tour[i+1..j] */
    for (int left = i + 1, right = j; left < right; left++, right--) {
        int swapped = tour[left];
        tour[left] = tour[right];
        tour[right] = swapped;
    }
    for (int p = i + 1; p <= j; p++) position[tour[p]] = p;
}

/* Apply moves from LM; when LM has no applicable move, evaluate all moves again.
 * Stop when a fresh LM has no applicable move (local optimum). Changes tour in place.
 *
 * LM is rebuilt from scratch instead of adding the new moves to it: the moves left in LM
 * (not applicable now) are found again by the full evaluation, with the same delta,
 * because a delta depends only on the nodes of the move. */
static void local_search(const Instance *instance, int *tour) {
    int n = instance->n, k = instance->k;
    int *position = malloc(n * sizeof(int));
    tour_positions(instance, tour, position);

    /* at most k * (n - k) exchanges + 2 directions * k * k / 2 edge pairs */
    MoveList list = {malloc(((size_t)k * (n - k) + (size_t)k * k) * sizeof(Move)), 0};
    Move move;
    while (1) {
        evaluate_all_moves(instance, tour, position, &list);
        if (!take_applicable_move(&list, tour, position, k, &move)) break;

        do {
            apply_move(tour, position, k, &move);
        } while (take_applicable_move(&list, tour, position, k, &move));
    }
    free(list.moves);
    free(position);
}

static long long objective(const Instance *instance, const int *tour) {
    long long cycle_length, total_cost;
    evaluate(instance, tour, instance->k, &cycle_length, &total_cost);
    return cycle_length + total_cost;
}

/* ------------------------------------------------------------------------- */
/* Multiple start local search                                               */
/* ------------------------------------------------------------------------- */

/* The best of MSLS_LOCAL_SEARCHES local searches from random solutions, stored in best_tour. */
static void multiple_start_local_search(const Instance *instance, Rng *rng, int trace, int *best_tour) {
    int k = instance->k;
    int *tour = malloc(k * sizeof(int));
    long long best_objective = LLONG_MAX;

    for (int i = 0; i < MSLS_LOCAL_SEARCHES; i++) {
        random_start(instance, rng, tour);
        local_search(instance, tour);
        if (trace) emit_step(tour, k);
        long long tour_objective = objective(instance, tour);
        if (tour_objective < best_objective) {
            best_objective = tour_objective;
            memcpy(best_tour, tour, k * sizeof(int));
        }
    }

    report_iterations(MSLS_LOCAL_SEARCHES);
    free(tour);
}

/* ------------------------------------------------------------------------- */
/* Iterated local search                                                     */
/* ------------------------------------------------------------------------- */

/* Cut the tour into 4 non-empty parts A B C D and reconnect them as A C B D (into result). */
static void double_bridge(const int *tour, int k, Rng *rng, int *result) {
    int cuts[3]; /* 3 different cut positions in 1 .. k-1 */
    int count = 0;
    while (count < 3) {
        int cut = 1 + rng_below(rng, k - 1);
        int is_new = 1;
        for (int c = 0; c < count; c++) {
            if (cuts[c] == cut) is_new = 0;
        }
        if (is_new) cuts[count++] = cut;
    }
    /* sort the 3 cuts */
    for (int i = 1; i < 3; i++) {
        for (int j = i; j > 0 && cuts[j - 1] > cuts[j]; j--) {
            int swapped = cuts[j];
            cuts[j] = cuts[j - 1];
            cuts[j - 1] = swapped;
        }
    }
    int first = cuts[0], second = cuts[1], third = cuts[2];

    int length = 0; /* A C B D */
    memcpy(result + length, tour, first * sizeof(int));
    length += first;
    memcpy(result + length, tour + second, (third - second) * sizeof(int));
    length += third - second;
    memcpy(result + length, tour + first, (second - first) * sizeof(int));
    length += second - first;
    memcpy(result + length, tour + third, (k - third) * sizeof(int));
}

/* Replace NODE_EXCHANGES random selected nodes with random unselected ones (in place). */
static void exchange_random_nodes(const Instance *instance, int *tour, Rng *rng) {
    int *selected = calloc(instance->n, sizeof(int));
    for (int i = 0; i < instance->k; i++) selected[tour[i]] = 1;

    for (int e = 0; e < NODE_EXCHANGES; e++) {
        int i = rng_below(rng, instance->k);
        int added = rng_below(rng, instance->n);
        while (selected[added]) added = rng_below(rng, instance->n);
        selected[tour[i]] = 0;
        tour[i] = added;
        selected[added] = 1;
    }
    free(selected);
}

/* A new tour (into perturbed): double bridge, then exchange of a few selected nodes with unselected ones. */
static void perturb(const Instance *instance, const int *tour, Rng *rng, int *perturbed) {
    double_bridge(tour, instance->k, rng, perturbed);
    exchange_random_nodes(instance, perturbed, rng);
}

/* Perturb the current local optimum and run local search on it, until the time limit.
 * Keep the result only if it is strictly better than the current local optimum. */
static void iterated_local_search(const Instance *instance, Rng *rng, int trace, int *current) {
    long long deadline = now_ns() + time_limit();
    int k = instance->k;
    int *candidate = malloc(k * sizeof(int));

    random_start(instance, rng, current);
    local_search(instance, current);
    long long current_objective = objective(instance, current);
    long long local_searches = 1;
    if (trace) emit_step(current, k);

    while (now_ns() < deadline) {
        perturb(instance, current, rng, candidate);
        if (trace) emit_step(candidate, k);
        local_search(instance, candidate);
        local_searches++;
        if (trace) emit_step(candidate, k);

        long long candidate_objective = objective(instance, candidate);
        if (candidate_objective < current_objective) {
            current_objective = candidate_objective;
            memcpy(current, candidate, k * sizeof(int));
        }
    }

    report_iterations(local_searches);
    free(candidate);
}

/* ------------------------------------------------------------------------- */
/* Entry points required by the protocol                                     */
/* ------------------------------------------------------------------------- */

/* Run one run of a method and return the number of nodes in the resulting tour. */
static int solve(const Instance *instance, const char *method, int run, uint64_t seed, int trace, int *tour) {
    (void)run;
    Rng rng = {seed};
    if (!strcmp(method, MSLS)) multiple_start_local_search(instance, &rng, trace, tour);
    else iterated_local_search(instance, &rng, trace, tour);
    return instance->k;
}

/* Number of runs of a method, or -1 if the method does not exist. */
static int runs_for(const char *method, int n) {
    (void)n;
    if (!strcmp(method, MSLS) || !strcmp(method, ILS)) return RUNS;
    return -1;
}

int main(int argc, char **argv) { return run_solver(argc, argv, solve, runs_for); }
