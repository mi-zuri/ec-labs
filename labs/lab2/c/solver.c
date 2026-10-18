/* Lab 2 - greedy regret heuristics.
 *
 * Methods:
 *   nn_regret     - nearest neighbor (any position), 2-regret
 *   nn_weighted   - nearest neighbor (any position), 2-regret + best change
 *   gc_regret     - greedy cycle, 2-regret
 *   gc_weighted   - greedy cycle, 2-regret + best change
 *
 * For every unused node:
 *   regret      = (second cheapest length increase) - (cheapest length increase)
 *   best change = (cheapest length increase) + (cost of the node)
 * Every step inserts the node with the highest
 * regret weight * regret - change weight * best change at its cheapest position.
 *
 * All methods start from a given node, and run i starts from node i.
 * On ties the first candidate found wins (lowest node index, then lowest position).
 */
#include "../../../common/c/tsp.h"

typedef struct {
    long long regret;
    long long change;
} Weights;

static const Weights PURE_REGRET = {1, 0};
static const Weights EQUAL_WEIGHTS = {1, 1};

/* ------------------------------------------------------------------------- */
/* Building blocks shared by the methods                                     */
/* ------------------------------------------------------------------------- */

/* A tour under construction, plus which nodes are already in it. */
typedef struct {
    int *nodes;            /* caller-provided buffer with room for n nodes */
    int length;            /* how many entries of `nodes` are in use */
    char *used;            /* used[v] is 1 if node v is already in the tour */
    long long *increases;  /* scratch buffer for insertion costs */
    int trace;             /* print every intermediate tour? */
} Builder;

/* Begin a tour containing only the start node. */
static Builder start_tour(const Instance *instance, int start, int trace, int *nodes) {
    Builder tour = {nodes, 1, calloc(instance->n, 1), malloc((instance->n + 1) * sizeof(long long)), trace};
    tour.nodes[0] = start;
    tour.used[start] = 1;
    if (trace) emit_step(tour.nodes, tour.length);
    return tour;
}

static void free_tour(Builder *tour) {
    free(tour->used);
    free(tour->increases);
}

/* Insert `node` into the tour at index `position`. */
static void add_node(Builder *tour, int position, int node) {
    int nodes_to_shift = tour->length - position;
    memmove(tour->nodes + position + 1, tour->nodes + position, nodes_to_shift * sizeof(int));
    tour->nodes[position] = node;
    tour->length++;
    tour->used[node] = 1;
    if (tour->trace) emit_step(tour->nodes, tour->length);
}

/* Return the unused node u minimizing D(from_node, u) + cost[u]. */
static int nearest_unused_node(const Instance *instance, const Builder *tour, int from_node) {
    int best_node = -1;
    long long best_value = LLONG_MAX;
    for (int u = 0; u < instance->n; u++) {
        if (tour->used[u]) continue;
        long long value = D(instance, from_node, u) + instance->cost[u];
        if (value < best_value) {
            best_node = u;
            best_value = value;
        }
    }
    return best_node;
}

/* Fill increases[p] with the length increase of inserting `node` at index p; return the count. */
typedef int (*IncreasesFn)(const Instance *instance, const Builder *tour, int node, long long *increases);

/* Every insertion index p of the open path (p = 0 before the first node, p = length after the last one). */
static int path_insertion_increases(const Instance *instance, const Builder *tour, int node,
                                    long long *increases) {
    const int *nodes = tour->nodes;
    int length = tour->length;

    increases[0] = D(instance, node, nodes[0]);
    for (int p = 1; p < length; p++) {
        int before = nodes[p - 1], after = nodes[p];
        increases[p] = D(instance, before, node) + D(instance, node, after) - D(instance, before, after);
    }
    increases[length] = D(instance, nodes[length - 1], node);
    return length + 1;
}

/* Every insertion index p of the closed cycle, i.e. between nodes[p-1] and nodes[p] (p = 0 is the closing edge). */
static int cycle_insertion_increases(const Instance *instance, const Builder *tour, int node,
                                     long long *increases) {
    const int *nodes = tour->nodes;
    int length = tour->length;

    for (int p = 0; p < length; p++) {
        int before = nodes[(p - 1 + length) % length], after = nodes[p];
        increases[p] = D(instance, before, node) + D(instance, node, after) - D(instance, before, after);
    }
    return length;
}

/* Find the cheapest and second cheapest increase; return the position of the cheapest. */
static int two_cheapest(const long long *increases, int count, long long *cheapest, long long *second_cheapest) {
    int best_position = -1;
    *cheapest = LLONG_MAX;
    *second_cheapest = LLONG_MAX;

    for (int p = 0; p < count; p++) {
        if (increases[p] < *cheapest) {
            best_position = p;
            *second_cheapest = *cheapest;
            *cheapest = increases[p];
        } else if (increases[p] < *second_cheapest) {
            *second_cheapest = increases[p];
        }
    }
    return best_position;
}

/* Find the unused node with the highest score and its cheapest position. */
static void regret_insertion(const Instance *instance, Builder *tour, IncreasesFn insertion_increases,
                             Weights weights, int *best_node, int *best_position) {
    long long best_score = LLONG_MIN;
    *best_node = -1;
    *best_position = -1;

    for (int u = 0; u < instance->n; u++) {
        if (tour->used[u]) continue;
        int count = insertion_increases(instance, tour, u, tour->increases);
        long long cheapest, second_cheapest;
        int position = two_cheapest(tour->increases, count, &cheapest, &second_cheapest);

        long long regret = second_cheapest - cheapest;
        long long best_change = cheapest + instance->cost[u];
        long long score = weights.regret * regret - weights.change * best_change;
        if (score > best_score) {
            *best_node = u;
            *best_position = position;
            best_score = score;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Methods. Each fills `tour` and returns the number of nodes in it.         */
/* ------------------------------------------------------------------------- */

/* Grow a path from the start node, inserting nodes at any position. */
static int nearest_neighbor_regret(const Instance *instance, int start, Weights weights, int trace, int *tour) {
    Builder path = start_tour(instance, start, trace, tour);

    while (path.length < instance->k) {
        int node, position;
        regret_insertion(instance, &path, path_insertion_increases, weights, &node, &position);
        add_node(&path, position, node);
    }

    free_tour(&path);
    return path.length;
}

/* Start a cycle with the start node and its best partner, then keep inserting
 * nodes into the edges of the cycle. */
static int greedy_cycle_regret(const Instance *instance, int start, Weights weights, int trace, int *tour) {
    Builder cycle = start_tour(instance, start, trace, tour);

    int partner = nearest_unused_node(instance, &cycle, start);
    add_node(&cycle, cycle.length, partner);

    while (cycle.length < instance->k) {
        int node, position;
        regret_insertion(instance, &cycle, cycle_insertion_increases, weights, &node, &position);
        add_node(&cycle, position, node);
    }

    free_tour(&cycle);
    return cycle.length;
}

/* ------------------------------------------------------------------------- */
/* Entry points required by the protocol                                     */
/* ------------------------------------------------------------------------- */

/* Run one run of a method and return the number of nodes in the resulting tour. */
static int solve(const Instance *instance, const char *method, int run, uint64_t seed, int trace, int *tour) {
    (void)seed;
    if (!strcmp(method, "nn_regret")) return nearest_neighbor_regret(instance, run, PURE_REGRET, trace, tour);
    if (!strcmp(method, "nn_weighted")) return nearest_neighbor_regret(instance, run, EQUAL_WEIGHTS, trace, tour);
    if (!strcmp(method, "gc_regret")) return greedy_cycle_regret(instance, run, PURE_REGRET, trace, tour);
    return greedy_cycle_regret(instance, run, EQUAL_WEIGHTS, trace, tour);
}

/* Number of runs of a method, or -1 if the method does not exist. */
static int runs_for(const char *method, int n) {
    if (!strcmp(method, "nn_regret") || !strcmp(method, "nn_weighted") || !strcmp(method, "gc_regret") ||
        !strcmp(method, "gc_weighted")) {
        return n; /* one run per starting node */
    }
    return -1;
}

int main(int argc, char **argv) { return run_solver(argc, argv, solve, runs_for); }
