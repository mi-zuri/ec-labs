/* Lab 1 - greedy heuristics: random, nearest neighbor (end / any position), greedy cycle. */
#include "../../../common/c/tsp.h"

#define RANDOM_RUNS 200

static void insert_at(int *tour, int m, int pos, int node) {
    memmove(tour + pos + 1, tour + pos, (size_t)(m - pos) * sizeof(int));
    tour[pos] = node;
}

static int random_solution(const Instance *in, uint64_t seed, int trace, int *tour) {
    Rng rng = {seed};
    int *perm = malloc(in->n * sizeof(int));
    for (int i = 0; i < in->n; i++) perm[i] = i;
    rng_shuffle(&rng, perm, in->n);
    memcpy(tour, perm, in->k * sizeof(int));
    free(perm);
    if (trace)
        for (int m = 1; m <= in->k; m++) emit_step(tour, m);
    return in->k;
}

/* Append the node with the best (distance from last node + node cost). */
static int nn_end(const Instance *in, int start, int trace, int *tour) {
    char *used = calloc(in->n, 1);
    int m = 1;
    tour[0] = start;
    used[start] = 1;
    if (trace) emit_step(tour, m);
    while (m < in->k) {
        int last = tour[m - 1], best_u = -1;
        long long best = LLONG_MAX;
        for (int u = 0; u < in->n; u++) {
            if (used[u]) continue;
            long long delta = D(in, last, u) + in->cost[u];
            if (delta < best) best = delta, best_u = u;
        }
        tour[m++] = best_u;
        used[best_u] = 1;
        if (trace) emit_step(tour, m);
    }
    free(used);
    return m;
}

/* Insert the best node at the best position of the path: front, between two nodes, or end. */
static int nn_any(const Instance *in, int start, int trace, int *tour) {
    char *used = calloc(in->n, 1);
    int m = 1;
    tour[0] = start;
    used[start] = 1;
    if (trace) emit_step(tour, m);
    while (m < in->k) {
        int best_u = -1, best_pos = -1;
        long long best = LLONG_MAX;
        for (int u = 0; u < in->n; u++) {
            if (used[u]) continue;
            for (int p = 0; p <= m; p++) { /* p = index u is inserted at */
                long long delta;
                if (p == 0) delta = D(in, u, tour[0]);
                else if (p == m) delta = D(in, tour[m - 1], u);
                else delta = D(in, tour[p - 1], u) + D(in, u, tour[p]) - D(in, tour[p - 1], tour[p]);
                delta += in->cost[u];
                if (delta < best) best = delta, best_u = u, best_pos = p;
            }
        }
        insert_at(tour, m++, best_pos, best_u);
        used[best_u] = 1;
        if (trace) emit_step(tour, m);
    }
    free(used);
    return m;
}

/* Start from the best pair, then repeatedly insert the best node into the best edge of the cycle. */
static int greedy_cycle(const Instance *in, int start, int trace, int *tour) {
    char *used = calloc(in->n, 1);
    int m = 1;
    tour[0] = start;
    used[start] = 1;
    if (trace) emit_step(tour, m);

    int best_u = -1;
    long long best = LLONG_MAX;
    for (int u = 0; u < in->n; u++) {
        if (used[u]) continue;
        long long delta = D(in, start, u) + in->cost[u];
        if (delta < best) best = delta, best_u = u;
    }
    tour[m++] = best_u;
    used[best_u] = 1;
    if (trace) emit_step(tour, m);

    while (m < in->k) {
        int best_pos = -1;
        best_u = -1;
        best = LLONG_MAX;
        for (int u = 0; u < in->n; u++) {
            if (used[u]) continue;
            for (int i = 0; i < m; i++) { /* edge tour[i] -> tour[i+1], incl. the closing edge */
                int a = tour[i], b = tour[(i + 1) % m];
                long long delta = D(in, a, u) + D(in, u, b) - D(in, a, b) + in->cost[u];
                if (delta < best) best = delta, best_u = u, best_pos = i + 1;
            }
        }
        insert_at(tour, m++, best_pos, best_u);
        used[best_u] = 1;
        if (trace) emit_step(tour, m);
    }
    free(used);
    return m;
}

static int solve(const Instance *in, const char *method, int run, uint64_t seed, int trace, int *tour) {
    if (!strcmp(method, "random")) return random_solution(in, seed, trace, tour);
    if (!strcmp(method, "nn_end")) return nn_end(in, run, trace, tour);
    if (!strcmp(method, "nn_any")) return nn_any(in, run, trace, tour);
    return greedy_cycle(in, run, trace, tour);
}

static int runs_for(const char *method, int n) {
    if (!strcmp(method, "random")) return RANDOM_RUNS;
    if (!strcmp(method, "nn_end") || !strcmp(method, "nn_any") || !strcmp(method, "greedy_cycle")) return n;
    return -1;
}

int main(int argc, char **argv) { return run_solver(argc, argv, solve, runs_for); }
