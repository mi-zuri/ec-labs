/* Shared helpers for C solvers: instance loading, RNG and the output protocol.
 * See common/PROTOCOL.md. Include this header first (it sets _POSIX_C_SOURCE for clock_gettime).
 * A lab solver provides solve() and runs_for() and calls run_solver() from main. */
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

#define DEFAULT_SEED 42
#define INITIAL_CAPACITY 64
#define NANOS_PER_SECOND 1000000000LL

static void fail(const char *message) {
    printf("{\"t\":\"error\",\"msg\":\"%s\"}\n", message);
    fflush(stdout);
    exit(2);
}

static void print_tour(const int *tour, int length) {
    putchar('[');
    for (int i = 0; i < length; i++) {
        if (i > 0) putchar(',');
        printf("%d", tour[i]);
    }
    putchar(']');
}

static void emit_step(const int *tour, int length) {
    fputs("{\"t\":\"step\",\"tour\":", stdout);
    print_tour(tour, length);
    fputs("}\n", stdout);
    fflush(stdout);
}

/* Distance matrix and node costs (coordinates are dropped after building the matrix). */
typedef struct {
    int n;     /* number of nodes */
    int k;     /* number of nodes to select */
    int *cost; /* cost[v]: cost of node v */
    int *dist; /* n * n, row major: rounded Euclidean distances */
} Instance;

static inline int D(const Instance *instance, int i, int j) { return instance->dist[i * instance->n + j]; }

static int rounded_distance(int x1, int y1, int x2, int y2) {
    double dx = x1 - x2;
    double dy = y1 - y2;
    return (int)floor(sqrt(dx * dx + dy * dy) + 0.5);
}

/* Read "x;y;cost" lines and build the instance. */
static Instance load_instance(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) fail("cannot open instance file");

    int capacity = INITIAL_CAPACITY;
    int n = 0;
    int *xs = malloc(capacity * sizeof(int));
    int *ys = malloc(capacity * sizeof(int));
    int *costs = malloc(capacity * sizeof(int));
    int x, y, cost;
    while (fscanf(file, "%d;%d;%d", &x, &y, &cost) == 3) {
        if (n == capacity) {
            capacity *= 2;
            xs = realloc(xs, capacity * sizeof(int));
            ys = realloc(ys, capacity * sizeof(int));
            costs = realloc(costs, capacity * sizeof(int));
        }
        xs[n] = x;
        ys[n] = y;
        costs[n] = cost;
        n++;
    }
    fclose(file);

    Instance instance;
    instance.n = n;
    instance.k = (n + 1) / 2;
    instance.cost = costs;
    instance.dist = malloc((size_t)n * n * sizeof(int));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            instance.dist[i * n + j] = rounded_distance(xs[i], ys[i], xs[j], ys[j]);
        }
    }

    free(xs);
    free(ys);
    return instance;
}

/* Cycle length (including the closing edge) and total node cost of a tour. */
static void evaluate(const Instance *instance, const int *tour, int length, long long *cycle_length,
                     long long *total_cost) {
    *cycle_length = 0;
    *total_cost = 0;
    for (int i = 0; i < length; i++) {
        int next = (i + 1) % length;
        if (length > 1) *cycle_length += D(instance, tour[i], tour[next]);
        *total_cost += instance->cost[tour[i]];
    }
}

/* splitmix64, identical in every language so random runs match exactly. */
typedef struct {
    uint64_t state;
} Rng;

static uint64_t rng_next(Rng *rng) {
    rng->state += 0x9E3779B97F4A7C15ULL;
    uint64_t z = rng->state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static int rng_below(Rng *rng, int n) { return (int)(rng_next(rng) % (uint64_t)n); }

/* Fisher-Yates, in place. */
static void rng_shuffle(Rng *rng, int *items, int length) {
    for (int i = length - 1; i > 0; i--) {
        int j = rng_below(rng, i + 1);
        int swapped = items[i];
        items[i] = items[j];
        items[j] = swapped;
    }
}

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * NANOS_PER_SECOND + ts.tv_nsec;
}

/* solve fills `tour` (room for n nodes) and returns how many nodes it contains.
 * runs_for returns the number of runs of a method, or -1 for an unknown method. */
typedef int (*SolveFn)(const Instance *instance, const char *method, int run, uint64_t seed, int trace, int *tour);
typedef int (*RunsFn)(const char *method, int n);

typedef struct {
    const char *instance_path;
    const char *method;
    const char *mode; /* "bench" or "trace" */
    int start;        /* trace mode: run to trace */
    uint64_t seed;
} Args;

static Args parse_args(int argc, char **argv) {
    Args args = {NULL, NULL, "bench", 0, DEFAULT_SEED};

    for (int i = 1; i + 1 < argc; i += 2) {
        const char *flag = argv[i];
        const char *value = argv[i + 1];
        if (!strcmp(flag, "--instance")) args.instance_path = value;
        else if (!strcmp(flag, "--method")) args.method = value;
        else if (!strcmp(flag, "--mode")) args.mode = value;
        else if (!strcmp(flag, "--start")) args.start = atoi(value);
        else if (!strcmp(flag, "--seed")) args.seed = strtoull(value, NULL, 10);
        else fail("unknown argument");
    }

    if (!args.instance_path || !args.method) fail("--instance and --method are required");
    return args;
}

/* Protocol driver. Bench mode runs every run; trace mode only the run given by --start. */
static int run_solver(int argc, char **argv, SolveFn solve, RunsFn runs_for) {
    Args args = parse_args(argc, argv);
    Instance instance = load_instance(args.instance_path);
    int is_trace = !strcmp(args.mode, "trace");

    int total_runs = runs_for(args.method, instance.n);
    if (total_runs < 0) fail("unknown method");
    if (is_trace && (args.start < 0 || args.start >= total_runs)) fail("start out of range");

    int first_run = is_trace ? args.start : 0;
    int run_count = is_trace ? 1 : total_runs;

    printf("{\"t\":\"meta\",\"n\":%d,\"k\":%d,\"runs\":%d,\"mode\":\"%s\"}\n", instance.n, instance.k, run_count,
           is_trace ? "trace" : "bench");
    fflush(stdout);

    int *tour = malloc(instance.n * sizeof(int));
    long long total_ns = 0;
    for (int run = first_run; run < first_run + run_count; run++) {
        uint64_t seed = args.seed + (uint64_t)run;

        long long started = now_ns();
        int tour_length = solve(&instance, args.method, run, seed, is_trace, tour);
        long long elapsed_ns = now_ns() - started;
        total_ns += elapsed_ns;

        long long cycle_length, total_cost;
        evaluate(&instance, tour, tour_length, &cycle_length, &total_cost);

        printf("{\"t\":\"run_end\",\"run\":%d,\"obj\":%lld,\"len\":%lld,\"cost\":%lld,\"ns\":%lld,\"tour\":", run,
               cycle_length + total_cost, cycle_length, total_cost, elapsed_ns);
        print_tour(tour, tour_length);
        fputs("}\n", stdout);
        fflush(stdout);
    }

    printf("{\"t\":\"done\",\"total_ns\":%lld}\n", total_ns);
    fflush(stdout);

    free(tour);
    return 0;
}

#endif
