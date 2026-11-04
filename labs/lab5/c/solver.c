/* Lab 5 - move evaluations from previous iterations (list of improving moves) in local search.
 *
 * Method: steepest local search from a random solution with the neighborhood that was best in lab 3:
 *   inter-route  exchange a selected node with an unselected one
 *   intra-route  edges exchange (2-opt, reverses a segment)
 * but improving moves are kept in a list LM (best first) and reused in later iterations
 * instead of evaluating the whole neighborhood in every iteration:
 *
 *   LM = all improving moves of the solution, sorted from the best
 *   repeat
 *       browse LM from the best until an applicable move is found (removing the obsolete ones)
 *       found     -> apply it
 *       not found -> evaluate all moves again into LM, stop if none is applicable
 *
 * A move is stored by the nodes of the edges it removes, not by positions (positions change).
 * When browsing LM, a stored move is in one of 3 situations:
 *   a removed edge is no longer in the tour                      -> obsolete, remove it from LM
 *   the removed edges are in the tour, but one of them reversed  -> not applicable now, keep it
 *   the removed edges are in the tour in the same relative
 *   direction (both as stored or both reversed)                  -> apply it and remove it from LM
 * That is why, when evaluating, both relative directions of every pair of edges are stored.
 *
 * Run i uses seed + i, so it starts from the same random solution as lab 3's steepest LS
 * (the baseline without the list of moves).
 */
#include "../../../common/c/tsp.h"

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

static const char *METHODS[] = {"ls_move_list_steepest_edges_random"};
static const int METHOD_COUNT = sizeof(METHODS) / sizeof(METHODS[0]);

/* ------------------------------------------------------------------------- */
/* Starting solution                                                         */
/* ------------------------------------------------------------------------- */

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

/* ------------------------------------------------------------------------- */
/* Local search                                                              */
/* ------------------------------------------------------------------------- */

/* Apply moves from LM; when LM has no applicable move, evaluate all moves again.
 * Stop when a fresh LM has no applicable move (local optimum).
 *
 * LM is rebuilt from scratch instead of adding the new moves to it: the moves left in LM
 * (not applicable now) are found again by the full evaluation, with the same delta,
 * because a delta depends only on the nodes of the move. */
static void local_search(const Instance *instance, int *tour, int trace) {
    int n = instance->n, k = instance->k;
    int *position = malloc(n * sizeof(int));
    tour_positions(instance, tour, position);
    if (trace) emit_step(tour, k);

    /* at most k * (n - k) exchanges + 2 directions * k * k / 2 edge pairs */
    MoveList list = {malloc(((size_t)k * (n - k) + (size_t)k * k) * sizeof(Move)), 0};
    Move move;
    while (1) {
        evaluate_all_moves(instance, tour, position, &list);
        if (!take_applicable_move(&list, tour, position, k, &move)) break;

        do {
            apply_move(tour, position, k, &move);
            if (trace) emit_step(tour, k);
        } while (take_applicable_move(&list, tour, position, k, &move));
    }
    free(list.moves);
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
    random_start(instance, &rng, tour);
    local_search(instance, tour, trace);
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
