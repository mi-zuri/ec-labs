// Shared helpers for C++ solvers: instance loading, RNG and the output protocol.
// See common/PROTOCOL.md. A lab solver provides solve() and runs_for() and calls tsp::run() from main.
#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace tsp {

const uint64_t DEFAULT_SEED = 42;

[[noreturn]] inline void fail(const std::string &message) {
    std::cout << "{\"t\":\"error\",\"msg\":\"" << message << "\"}" << std::endl;
    std::exit(2);
}

// Format the first `length` nodes of a tour as a JSON array.
inline std::string tour_json(const std::vector<int> &tour, size_t length) {
    std::string json = "[";
    for (size_t i = 0; i < length; i++) {
        if (i > 0) json += ',';
        json += std::to_string(tour[i]);
    }
    return json + "]";
}

inline void emit_step(const std::vector<int> &tour, size_t length) {
    std::cout << "{\"t\":\"step\",\"tour\":" << tour_json(tour, length) << "}" << std::endl;
}

inline void emit_step(const std::vector<int> &tour) { emit_step(tour, tour.size()); }

inline int rounded_distance(int x1, int y1, int x2, int y2) {
    double dx = x1 - x2;
    double dy = y1 - y2;
    return static_cast<int>(std::floor(std::sqrt(dx * dx + dy * dy) + 0.5));
}

// Distance matrix and node costs (coordinates are dropped after building the matrix).
struct Instance {
    int n = 0;              // number of nodes
    int k = 0;              // number of nodes to select
    std::vector<int> cost;  // cost[v]: cost of node v
    std::vector<int> dist;  // n * n, row major: rounded Euclidean distances

    int d(int i, int j) const { return dist[i * n + j]; }

    // Read "x;y;cost" lines and build the instance.
    static Instance load(const std::string &path) {
        std::ifstream file(path);
        if (!file) fail("cannot open instance file");

        std::vector<int> xs, ys;
        Instance instance;

        std::string line;
        while (std::getline(file, line)) {
            std::istringstream fields(line);
            int x, y, cost;
            char separator1, separator2;
            if (fields >> x >> separator1 >> y >> separator2 >> cost) {
                xs.push_back(x);
                ys.push_back(y);
                instance.cost.push_back(cost);
            }
        }

        instance.n = static_cast<int>(instance.cost.size());
        instance.k = (instance.n + 1) / 2;
        instance.dist.resize(static_cast<size_t>(instance.n) * instance.n);
        for (int i = 0; i < instance.n; i++) {
            for (int j = 0; j < instance.n; j++) {
                instance.dist[i * instance.n + j] = rounded_distance(xs[i], ys[i], xs[j], ys[j]);
            }
        }
        return instance;
    }

    // Returns {cycle length including the closing edge, total node cost}.
    std::pair<long long, long long> evaluate(const std::vector<int> &tour) const {
        long long cycle_length = 0;
        long long total_cost = 0;
        size_t length = tour.size();
        for (size_t i = 0; i < length; i++) {
            size_t next = (i + 1) % length;
            if (length > 1) cycle_length += d(tour[i], tour[next]);
            total_cost += cost[tour[i]];
        }
        return {cycle_length, total_cost};
    }
};

// splitmix64, identical in every language so random runs match exactly.
struct Rng {
    uint64_t state;

    explicit Rng(uint64_t seed) : state(seed) {}

    uint64_t next() {
        state += 0x9E3779B97F4A7C15ULL;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    int below(int n) { return static_cast<int>(next() % static_cast<uint64_t>(n)); }

    // Fisher-Yates, in place.
    void shuffle(std::vector<int> &items) {
        for (int i = static_cast<int>(items.size()) - 1; i > 0; i--) {
            int j = below(i + 1);
            std::swap(items[i], items[j]);
        }
    }
};

using SolveFn = std::function<std::vector<int>(const Instance &instance, const std::string &method, int run,
                                               uint64_t seed, bool trace)>;
using RunsFn = std::function<int(const std::string &method, int n)>;  // -1 = unknown method

struct Args {
    std::string instance_path;
    std::string method;
    std::string mode = "bench";  // "bench" or "trace"
    int start = 0;               // trace mode: run to trace
    uint64_t seed = DEFAULT_SEED;
};

inline Args parse_args(int argc, char **argv) {
    Args args;

    for (int i = 1; i + 1 < argc; i += 2) {
        std::string flag = argv[i];
        std::string value = argv[i + 1];
        if (flag == "--instance") args.instance_path = value;
        else if (flag == "--method") args.method = value;
        else if (flag == "--mode") args.mode = value;
        else if (flag == "--start") args.start = std::stoi(value);
        else if (flag == "--seed") args.seed = std::stoull(value);
        else fail("unknown argument " + flag);
    }

    if (args.instance_path.empty() || args.method.empty()) fail("--instance and --method are required");
    return args;
}

// Protocol driver. Bench mode runs every run; trace mode only the run given by --start.
inline int run(int argc, char **argv, const SolveFn &solve, const RunsFn &runs_for) {
    std::ios::sync_with_stdio(false);

    Args args = parse_args(argc, argv);
    Instance instance = Instance::load(args.instance_path);
    bool is_trace = args.mode == "trace";

    int total_runs = runs_for(args.method, instance.n);
    if (total_runs < 0) fail("unknown method " + args.method);
    if (is_trace && (args.start < 0 || args.start >= total_runs)) fail("start out of range");

    int first_run = is_trace ? args.start : 0;
    int run_count = is_trace ? 1 : total_runs;

    std::cout << "{\"t\":\"meta\",\"n\":" << instance.n << ",\"k\":" << instance.k << ",\"runs\":" << run_count
              << ",\"mode\":\"" << (is_trace ? "trace" : "bench") << "\"}" << std::endl;

    long long total_ns = 0;
    for (int run = first_run; run < first_run + run_count; run++) {
        uint64_t seed = args.seed + static_cast<uint64_t>(run);

        auto started = std::chrono::steady_clock::now();
        std::vector<int> tour = solve(instance, args.method, run, seed, is_trace);
        auto elapsed = std::chrono::steady_clock::now() - started;
        long long elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
        total_ns += elapsed_ns;

        auto [cycle_length, total_cost] = instance.evaluate(tour);

        std::cout << "{\"t\":\"run_end\",\"run\":" << run << ",\"obj\":" << cycle_length + total_cost
                  << ",\"len\":" << cycle_length << ",\"cost\":" << total_cost << ",\"ns\":" << elapsed_ns
                  << ",\"tour\":" << tour_json(tour, tour.size()) << "}" << std::endl;
    }

    std::cout << "{\"t\":\"done\",\"total_ns\":" << total_ns << "}" << std::endl;
    return 0;
}

}  // namespace tsp
