/* Lab 3 - local search.
 *
 * Methods: ls_<search>_<intra>_<start>
 *   search  steepest - apply the best move of the whole neighborhood
 *           greedy   - apply the first improving move, moves browsed in random order
 *   intra   nodes    - intra-route move exchanges two nodes of the tour
 *           edges    - intra-route move exchanges two edges (2-opt, reverses a segment)
 *   start   random   - random solution
 *           greedy   - greedy cycle, 2-regret + best change (best heuristic of lab 2)
 *
 * The neighborhood always contains both kinds of moves:
 *   inter-route  exchange a selected node with an unselected one
 *   intra-route  nodes or edges exchange (see above)
 * Every move is evaluated by its delta (change of the objective), never from scratch.
 *
 * Run i uses seed + i (random start and greedy browsing order),
 * and the greedy start of run i begins at node i.
 */
#include "../../../common/c/tsp.h"

typedef enum { STEEPEST, GREEDY } Search;
typedef enum { RANDOM_START, GREEDY_START } Start;

/* Move kinds. A move is (kind, i, j) of positions: */
typedef enum {
    EXCHANGE_SELECTED, /* inter-route: tour[i] <-> unselected[j] */
    SWAP_NODES,        /* intra-route: tour[i] <-> tour[j], i < j */
    SWAP_EDGES         /* intra-route: edges (tour[i], tour[i+1]) and (tour[j], tour[j+1]) -> reverse tour[i+1..j], i < j */
} MoveKind;

typedef struct {
    MoveKind kind;
    int i, j;
} Move;

typedef struct {
    const char *id;
    Search search;
    MoveKind intra_kind;
    Start start;
} Method;

static const Method METHODS[] = {
    {"ls_steepest_nodes_random", STEEPEST, SWAP_NODES, RANDOM_START},
    {"ls_steepest_edges_random", STEEPEST, SWAP_EDGES, RANDOM_START},
    {"ls_greedy_nodes_random", GREEDY, SWAP_NODES, RANDOM_START},
    {"ls_greedy_edges_random", GREEDY, SWAP_EDGES, RANDOM_START},
    {"ls_steepest_nodes_greedy", STEEPEST, SWAP_NODES, GREEDY_START},
    {"ls_steepest_edges_greedy", STEEPEST, SWAP_EDGES, GREEDY_START},
    {"ls_greedy_nodes_greedy", GREEDY, SWAP_NODES, GREEDY_START},
    {"ls_greedy_edges_greedy", GREEDY, SWAP_EDGES, GREEDY_START},
};
static const int METHOD_COUNT = sizeof(METHODS) / sizeof(METHODS[0]);

static const Method *find_method(const char *id) {
    for (int m = 0; m < METHOD_COUNT; m++) {
        if (!strcmp(METHODS[m].id, id)) return &METHODS[m];
    }
    return NULL;
}

/* A complete solution: k nodes of the cycle and the n - k other nodes. */
typedef struct {
    int *tour;       /* caller-provided buffer with room for n nodes */
    int k;
    int *unselected; /* n - k nodes */
    int unselected_count;
} Solution;

/* ------------------------------------------------------------------------- */
/* Starting solutions                                                        */
/* ------------------------------------------------------------------------- */

/* Random k nodes in random order. */
static void random_start(const Instance *instance, Rng *rng, Solution *solution) {
    int *nodes = malloc(instance->n * sizeof(int));
    for (int v = 0; v < instance->n; v++) nodes[v] = v;
    rng_shuffle(rng, nodes, instance->n);

    memcpy(solution->tour, nodes, instance->k * sizeof(int));
    memcpy(solution->unselected, nodes + instance->k, solution->unselected_count * sizeof(int));
    free(nodes);
}

/* Return the unused node u minimizing D(from_node, u) + cost[u]. */
static int nearest_unused_node(const Instance *instance, const char *used, int from_node) {
    int best_node = -1;
    long long best_value = LLONG_MAX;
    for (int u = 0; u < instance->n; u++) {
        if (used[u]) continue;
        long long value = D(instance, from_node, u) + instance->cost[u];
        if (value < best_value) {
            best_node = u;
            best_value = value;
        }
    }
    return best_node;
}

/* Find the unused node with the highest 2-regret - best change, and its cheapest position. */
static void regret_insertion(const Instance *instance, const int *tour, int length, const char *used,
                             int *best_node, int *best_position) {
    long long best_score = LLONG_MIN;
    *best_node = -1;
    *best_position = -1;

    for (int u = 0; u < instance->n; u++) {
        if (used[u]) continue;
        /* cheapest and second cheapest length increase over all edges of the cycle */
        int position = -1;
        long long cheapest = LLONG_MAX, second_cheapest = LLONG_MAX;
        for (int p = 0; p < length; p++) {
            int before = tour[(p - 1 + length) % length], after = tour[p];
            long long increase = D(instance, before, u) + D(instance, u, after) - D(instance, before, after);
            if (increase < cheapest) {
                position = p;
                second_cheapest = cheapest;
                cheapest = increase;
            } else if (increase < second_cheapest) {
                second_cheapest = increase;
            }
        }

        long long regret = second_cheapest - cheapest;
        long long best_change = cheapest + instance->cost[u];
        long long score = regret - best_change;
        if (score > best_score) {
            *best_node = u;
            *best_position = position;
            best_score = score;
        }
    }
}

/* Greedy cycle with 2-regret + best change (lab 2), from the start node. */
static void greedy_start(const Instance *instance, int start, Solution *solution) {
    int *tour = solution->tour;
    char *used = calloc(instance->n, 1);
    int length = 0;

    tour[length++] = start;
    used[start] = 1;

    int partner = nearest_unused_node(instance, used, start);
    tour[length++] = partner;
    used[partner] = 1;

    while (length < instance->k) {
        int node, position;
        regret_insertion(instance, tour, length, used, &node, &position);
        memmove(tour + position + 1, tour + position, (length - position) * sizeof(int));
        tour[position] = node;
        length++;
        used[node] = 1;
    }

    int count = 0;
    for (int v = 0; v < instance->n; v++) {
        if (!used[v]) solution->unselected[count++] = v;
    }
    free(used);
}

/* ------------------------------------------------------------------------- */
/* Moves: deltas and application                                             */
/* ------------------------------------------------------------------------- */

/* Every move of the neighborhood: all inter-route moves, then all intra-route moves.
 * Returns the array and stores its length in *count. */
static Move *all_moves(int k, int unselected_count, MoveKind intra_kind, int *count) {
    Move *moves = malloc(((size_t)k * unselected_count + (size_t)k * (k - 1) / 2) * sizeof(Move));
    *count = 0;
    for (int i = 0; i < k; i++) {
        for (int j = 0; j < unselected_count; j++) moves[(*count)++] = (Move){EXCHANGE_SELECTED, i, j};
    }
    for (int i = 0; i < k; i++) {
        for (int j = i + 1; j < k; j++) moves[(*count)++] = (Move){intra_kind, i, j};
    }
    return moves;
}

/* Replace tour[i] with new_node: its two edges and its cost change. */
static long long exchange_selected_delta(const Instance *instance, const int *tour, int k, int i, int new_node) {
    int before = tour[(i - 1 + k) % k], old_node = tour[i], after = tour[(i + 1) % k];
    return (long long)instance->cost[new_node] - instance->cost[old_node] + D(instance, before, new_node) +
           D(instance, new_node, after) - D(instance, before, old_node) - D(instance, old_node, after);
}

/* Swap the nodes at positions i < j. */
static long long swap_nodes_delta(const Instance *instance, const int *tour, int k, int i, int j) {
    int a = tour[i], b = tour[j];

    if (j == i + 1) {
        /* neighbours: before a b after -> before b a after (edge a-b stays) */
        int before = tour[(i - 1 + k) % k], after = tour[(j + 1) % k];
        return D(instance, before, b) + D(instance, a, after) - D(instance, before, a) - D(instance, b, after);
    }

    if (i == 0 && j == k - 1) {
        /* neighbours over the closing edge: before b a after -> before a b after */
        int before = tour[j - 1], after = tour[i + 1];
        return D(instance, before, a) + D(instance, b, after) - D(instance, before, b) - D(instance, a, after);
    }

    int before_a = tour[(i - 1 + k) % k], after_a = tour[i + 1];
    int before_b = tour[j - 1], after_b = tour[(j + 1) % k];
    return D(instance, before_a, b) + D(instance, b, after_a) + D(instance, before_b, a) + D(instance, a, after_b) -
           D(instance, before_a, a) - D(instance, a, after_a) - D(instance, before_b, b) - D(instance, b, after_b);
}

/* Remove edges (a, b) = (tour[i], tour[i+1]) and (c, d) = (tour[j], tour[j+1]),
 * add (a, c) and (b, d), i < j. */
static long long swap_edges_delta(const Instance *instance, const int *tour, int k, int i, int j) {
    int a = tour[i], b = tour[i + 1];
    int c = tour[j], d = tour[(j + 1) % k];
    return D(instance, a, c) + D(instance, b, d) - D(instance, a, b) - D(instance, c, d);
}

/* Change of the objective if the move were applied (negative = better). */
static long long move_delta(const Instance *instance, const Solution *solution, Move move) {
    switch (move.kind) {
    case EXCHANGE_SELECTED:
        return exchange_selected_delta(instance, solution->tour, solution->k, move.i, solution->unselected[move.j]);
    case SWAP_NODES:
        return swap_nodes_delta(instance, solution->tour, solution->k, move.i, move.j);
    default:
        return swap_edges_delta(instance, solution->tour, solution->k, move.i, move.j);
    }
}

static void swap_ints(int *a, int *b) {
    int swapped = *a;
    *a = *b;
    *b = swapped;
}

static void apply_move(Solution *solution, Move move) {
    int *tour = solution->tour;
    switch (move.kind) {
    case EXCHANGE_SELECTED:
        swap_ints(&tour[move.i], &solution->unselected[move.j]);
        break;
    case SWAP_NODES:
        swap_ints(&tour[move.i], &tour[move.j]);
        break;
    default: /* reverse tour[i+1..j] */
        for (int left = move.i + 1, right = move.j; left < right; left++, right--) swap_ints(&tour[left], &tour[right]);
    }
}

/* ------------------------------------------------------------------------- */
/* Local search                                                              */
/* ------------------------------------------------------------------------- */

/* Steepest: evaluate every move, store the best improving one. Returns 0 if none improves. */
static int find_best_move(const Instance *instance, const Solution *solution, const Move *moves, int count,
                          Move *best_move) {
    long long best_delta = 0;
    for (int m = 0; m < count; m++) {
        long long delta = move_delta(instance, solution, moves[m]);
        if (delta < best_delta) {
            *best_move = moves[m];
            best_delta = delta;
        }
    }
    return best_delta < 0;
}

/* Greedy: browse the moves in random order, store the first improving one. Returns 0 if none improves.
 *
 * The order is a lazy Fisher-Yates shuffle: each step draws the next move at random from
 * the ones not yet tried, so only the moves actually evaluated cost any work. */
static int find_first_improving_move(const Instance *instance, const Solution *solution, Move *moves, int count,
                                     Rng *rng, Move *improving_move) {
    for (int tried = 0; tried < count; tried++) {
        int pick = tried + rng_below(rng, count - tried);
        Move swapped = moves[tried];
        moves[tried] = moves[pick];
        moves[pick] = swapped;
        if (move_delta(instance, solution, moves[tried]) < 0) {
            *improving_move = moves[tried];
            return 1;
        }
    }
    return 0;
}

/* Apply improving moves until none is left (local optimum). */
static void local_search(const Instance *instance, Solution *solution, Search search, MoveKind intra_kind, Rng *rng,
                         int trace) {
    int count;
    Move *moves = all_moves(solution->k, solution->unselected_count, intra_kind, &count);
    if (trace) emit_step(solution->tour, solution->k);

    while (1) {
        Move move;
        int found = search == STEEPEST ? find_best_move(instance, solution, moves, count, &move)
                                       : find_first_improving_move(instance, solution, moves, count, rng, &move);
        if (!found) break;

        apply_move(solution, move);
        if (trace) emit_step(solution->tour, solution->k);
    }
    free(moves);
}

/* ------------------------------------------------------------------------- */
/* Entry points required by the protocol                                     */
/* ------------------------------------------------------------------------- */

/* Run one run of a method and return the number of nodes in the resulting tour. */
static int solve(const Instance *instance, const char *method, int run, uint64_t seed, int trace, int *tour) {
    const Method *chosen = find_method(method);
    Rng rng = {seed};
    Solution solution = {tour, instance->k, malloc((instance->n - instance->k) * sizeof(int)),
                         instance->n - instance->k};

    if (chosen->start == RANDOM_START) {
        random_start(instance, &rng, &solution);
    } else {
        greedy_start(instance, run, &solution);
    }
    local_search(instance, &solution, chosen->search, chosen->intra_kind, &rng, trace);

    free(solution.unselected);
    return solution.k;
}

/* Number of runs of a method, or -1 if the method does not exist. */
static int runs_for(const char *method, int n) {
    if (find_method(method)) return n; /* 200 random starts / one greedy start per node */
    return -1;
}

int main(int argc, char **argv) { return run_solver(argc, argv, solve, runs_for); }
