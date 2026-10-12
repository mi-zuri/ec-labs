/* Shared helpers for C solvers: instance loading, RNG, timing and the output protocol.
 * See common/PROTOCOL.md for the contract every solver follows.
 * Include this header first in solver.c (it sets _POSIX_C_SOURCE for clock_gettime). */
#ifndef TSP_H
#define TSP_H

#define _POSIX_C_SOURCE 199309L
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    int n, k;
    int *cost;
    int *dist; /* n * n, row major */
} Instance;

static inline int D(const Instance *in, int i, int j) { return in->dist[i * in->n + j]; }

static void fail(const char *msg) {
    printf("{\"t\":\"error\",\"msg\":\"%s\"}\n", msg);
    fflush(stdout);
    exit(2);
}

static Instance load_instance(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) fail("cannot open instance file");
    int cap = 64, n = 0, x, y, c;
    int *xs = malloc(cap * sizeof(int)), *ys = malloc(cap * sizeof(int)), *cost = malloc(cap * sizeof(int));
    while (fscanf(f, "%d;%d;%d", &x, &y, &c) == 3) {
        if (n == cap) {
            cap *= 2;
            xs = realloc(xs, cap * sizeof(int));
            ys = realloc(ys, cap * sizeof(int));
            cost = realloc(cost, cap * sizeof(int));
        }
        xs[n] = x; ys[n] = y; cost[n] = c;
        n++;
    }
    fclose(f);

    Instance in = {n, (n + 1) / 2, cost, malloc((size_t)n * n * sizeof(int))};
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            double dx = xs[i] - xs[j], dy = ys[i] - ys[j];
            in.dist[i * n + j] = (int)floor(sqrt(dx * dx + dy * dy) + 0.5);
        }
    free(xs);
    free(ys);
    return in;
}

static void evaluate(const Instance *in, const int *tour, int m, long long *len, long long *cost) {
    *len = 0;
    *cost = 0;
    for (int i = 0; i < m; i++) {
        if (m > 1) *len += D(in, tour[i], tour[(i + 1) % m]);
        *cost += in->cost[tour[i]];
    }
}

/* splitmix64, identical in every language so random runs match exactly */
typedef struct { uint64_t state; } Rng;

static uint64_t rng_next(Rng *r) {
    r->state += 0x9E3779B97F4A7C15ULL;
    uint64_t z = r->state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static int rng_below(Rng *r, int n) { return (int)(rng_next(r) % (uint64_t)n); }

static void rng_shuffle(Rng *r, int *a, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = rng_below(r, i + 1), t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void print_tour(const int *tour, int m) {
    putchar('[');
    for (int i = 0; i < m; i++) printf(i ? ",%d" : "%d", tour[i]);
    putchar(']');
}

static void emit_step(const int *tour, int m) {
    fputs("{\"t\":\"step\",\"tour\":", stdout);
    print_tour(tour, m);
    fputs("}\n", stdout);
    fflush(stdout);
}

/* solve(in, method, run, seed, trace, tour) fills tour and returns its length;
 * runs_for(method, n) returns the number of runs or -1 for an unknown method. */
typedef int (*SolveFn)(const Instance *, const char *, int, uint64_t, int, int *);
typedef int (*RunsFn)(const char *, int);

static int run_solver(int argc, char **argv, SolveFn solve, RunsFn runs_for) {
    const char *instance = NULL, *method = NULL, *mode = "bench";
    int start = 0;
    unsigned long long seed = 42;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--instance")) instance = argv[i + 1];
        else if (!strcmp(argv[i], "--method")) method = argv[i + 1];
        else if (!strcmp(argv[i], "--mode")) mode = argv[i + 1];
        else if (!strcmp(argv[i], "--start")) start = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--seed")) seed = strtoull(argv[i + 1], NULL, 10);
        else fail("unknown argument");
    }
    if (!instance || !method) fail("--instance and --method are required");

    Instance in = load_instance(instance);
    int trace = !strcmp(mode, "trace");
    int total = runs_for(method, in.n);
    if (total < 0) fail("unknown method");
    if (trace && (start < 0 || start >= total)) fail("start out of range");
    int first = trace ? start : 0, count = trace ? 1 : total;

    printf("{\"t\":\"meta\",\"n\":%d,\"k\":%d,\"runs\":%d,\"mode\":\"%s\"}\n", in.n, in.k, count,
           trace ? "trace" : "bench");
    fflush(stdout);

    int *tour = malloc(in.n * sizeof(int));
    long long total_ns = 0;
    for (int r = first; r < first + count; r++) {
        long long t0 = now_ns();
        int m = solve(&in, method, r, seed + (uint64_t)r, trace, tour);
        long long ns = now_ns() - t0;
        total_ns += ns;
        long long len, cost;
        evaluate(&in, tour, m, &len, &cost);
        printf("{\"t\":\"run_end\",\"run\":%d,\"obj\":%lld,\"len\":%lld,\"cost\":%lld,\"ns\":%lld,\"tour\":", r,
               len + cost, len, cost, ns);
        print_tour(tour, m);
        fputs("}\n", stdout);
        fflush(stdout);
    }
    printf("{\"t\":\"done\",\"total_ns\":%lld}\n", total_ns);
    fflush(stdout);
    free(tour);
    return 0;
}

#endif
