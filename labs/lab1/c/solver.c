/* Lab 1 - greedy heuristics: random, nn_end, nn_any, greedy_cycle.
 *
 * Greedy run i starts from node i. Each step adds the unused node with the smallest
 * (tour length increase + node cost); ties go to the lowest node, then lowest position. */
#include "../../../common/c/tsp.h"

#define RANDOM_RUNS 200

/* A tour under construction, plus which nodes are already in it. */
typedef struct {
    int *nodes; /* caller-provided buffer with room for n nodes */
    int length;
    char *used; /* used[v] is 1 if node v is in the tour */
    int trace;
} Builder;

static Builder start_tour(const Instance *instance, int start, int trace, int *nodes) {
    Builder tour = {nodes, 1, calloc(instance->n, 1), trace};
    tour.nodes[0] = start;
    tour.used[start] = 1;
    if (trace) emit_step(tour.nodes, tour.length);
    return tour;
}

static void add_node(Builder *tour, int position, int node) {
    int nodes_to_shift = tour->length - position;
    memmove(tour->nodes + position + 1, tour->nodes + position, nodes_to_shift * sizeof(int));
    tour->nodes[position] = node;
    tour->length++;
    tour->used[node] = 1;
    if (tour->trace) emit_step(tour->nodes, tour->length);
}

/* Unused node u minimizing D(from_node, u) + cost[u]. */
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

/* Returns the cheapest insertion index for `node` and stores the tour length increase in *length_increase. */
typedef int (*PositionFn)(const Instance *instance, const Builder *tour, int node, long long *length_increase);

/* Index 0 is before the first node, `length` is after the last one. */
static int cheapest_position_in_path(const Instance *instance, const Builder *tour, int node,
                                     long long *length_increase) {
    const int *nodes = tour->nodes;
    int length = tour->length;
    int best_position = -1;
    long long best_increase = LLONG_MAX;

    for (int p = 0; p <= length; p++) {
        long long increase;
        if (p == 0) {
            increase = D(instance, node, nodes[0]);
        } else if (p == length) {
            increase = D(instance, nodes[length - 1], node);
        } else {
            int before = nodes[p - 1], after = nodes[p];
            increase = D(instance, before, node) + D(instance, node, after) - D(instance, before, after);
        }
        if (increase < best_increase) {
            best_position = p;
            best_increase = increase;
        }
    }

    *length_increase = best_increase;
    return best_position;
}

/* Edge i is nodes[i] -> nodes[i+1] (the last wraps to nodes[0]); inserting there gives index i + 1. */
static int cheapest_position_in_cycle(const Instance *instance, const Builder *tour, int node,
                                      long long *length_increase) {
    int best_position = -1;
    long long best_increase = LLONG_MAX;

    for (int i = 0; i < tour->length; i++) {
        int before = tour->nodes[i];
        int after = tour->nodes[(i + 1) % tour->length];
        long long increase = D(instance, before, node) + D(instance, node, after) - D(instance, before, after);
        if (increase < best_increase) {
            best_position = i + 1;
            best_increase = increase;
        }
    }

    *length_increase = best_increase;
    return best_position;
}

/* Unused node and position with the smallest total cost of adding. */
static void cheapest_insertion(const Instance *instance, const Builder *tour, PositionFn cheapest_position,
                               int *best_node, int *best_position) {
    long long best_value = LLONG_MAX;
    *best_node = -1;
    *best_position = -1;

    for (int u = 0; u < instance->n; u++) {
        if (tour->used[u]) continue;
        long long length_increase;
        int position = cheapest_position(instance, tour, u, &length_increase);
        long long value = length_increase + instance->cost[u];
        if (value < best_value) {
            *best_node = u;
            *best_position = position;
            best_value = value;
        }
    }
}

static int random_solution(const Instance *instance, uint64_t seed, int trace, int *tour) {
    Rng rng = {seed};
    int *nodes = malloc(instance->n * sizeof(int));
    for (int i = 0; i < instance->n; i++) nodes[i] = i;
    rng_shuffle(&rng, nodes, instance->n);

    memcpy(tour, nodes, instance->k * sizeof(int));
    free(nodes);

    if (trace) {
        for (int length = 1; length <= instance->k; length++) emit_step(tour, length);
    }
    return instance->k;
}

/* Append the nearest unused node to the end of the path. */
static int nn_end(const Instance *instance, int start, int trace, int *tour) {
    Builder path = start_tour(instance, start, trace, tour);

    while (path.length < instance->k) {
        int node = nearest_unused_node(instance, &path, path.nodes[path.length - 1]);
        add_node(&path, path.length, node);
    }

    free(path.used);
    return path.length;
}

/* Insert the best node at the best position of the path. */
static int nn_any(const Instance *instance, int start, int trace, int *tour) {
    Builder path = start_tour(instance, start, trace, tour);

    while (path.length < instance->k) {
        int node, position;
        cheapest_insertion(instance, &path, cheapest_position_in_path, &node, &position);
        add_node(&path, position, node);
    }

    free(path.used);
    return path.length;
}

/* Start from the best pair, then insert the best node into the best edge of the cycle. */
static int greedy_cycle(const Instance *instance, int start, int trace, int *tour) {
    Builder cycle = start_tour(instance, start, trace, tour);

    int partner = nearest_unused_node(instance, &cycle, start);
    add_node(&cycle, cycle.length, partner);

    while (cycle.length < instance->k) {
        int node, position;
        cheapest_insertion(instance, &cycle, cheapest_position_in_cycle, &node, &position);
        add_node(&cycle, position, node);
    }

    free(cycle.used);
    return cycle.length;
}

static int solve(const Instance *instance, const char *method, int run, uint64_t seed, int trace, int *tour) {
    if (!strcmp(method, "random")) return random_solution(instance, seed, trace, tour);
    if (!strcmp(method, "nn_end")) return nn_end(instance, run, trace, tour);
    if (!strcmp(method, "nn_any")) return nn_any(instance, run, trace, tour);
    return greedy_cycle(instance, run, trace, tour);
}

static int runs_for(const char *method, int n) {
    if (!strcmp(method, "random")) return RANDOM_RUNS;
    if (!strcmp(method, "nn_end") || !strcmp(method, "nn_any") || !strcmp(method, "greedy_cycle")) return n;
    return -1;
}

int main(int argc, char **argv) { return run_solver(argc, argv, solve, runs_for); }
