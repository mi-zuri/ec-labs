// Shared helpers for C++ solvers: instance loading, RNG, timing and the output protocol.
// See common/PROTOCOL.md for the contract every solver follows.
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
#include <vector>

namespace tsp {

[[noreturn]] inline void fail(const std::string &msg) {
    std::cout << "{\"t\":\"error\",\"msg\":\"" << msg << "\"}" << std::endl;
    std::exit(2);
}

struct Instance {
    int n = 0, k = 0;
    std::vector<int> cost;
    std::vector<int> dist;  // n * n, row major

    int d(int i, int j) const { return dist[i * n + j]; }

    static Instance load(const std::string &path) {
        std::ifstream f(path);
        if (!f) fail("cannot open instance file");
        std::vector<int> xs, ys;
        Instance in;
        std::string line;
        while (std::getline(f, line)) {
            int x, y, c;
            char s1, s2;
            std::istringstream row(line);
            if (row >> x >> s1 >> y >> s2 >> c) {
                xs.push_back(x);
                ys.push_back(y);
                in.cost.push_back(c);
            }
        }
        in.n = static_cast<int>(in.cost.size());
        in.k = (in.n + 1) / 2;
        in.dist.resize(static_cast<size_t>(in.n) * in.n);
        for (int i = 0; i < in.n; i++)
            for (int j = 0; j < in.n; j++) {
                double dx = xs[i] - xs[j], dy = ys[i] - ys[j];
                in.dist[i * in.n + j] = static_cast<int>(std::floor(std::sqrt(dx * dx + dy * dy) + 0.5));
            }
        return in;
    }

    // returns {cycle length, sum of node costs}
    std::pair<long long, long long> evaluate(const std::vector<int> &tour) const {
        long long len = 0, c = 0;
        size_t m = tour.size();
        for (size_t i = 0; i < m; i++) {
            if (m > 1) len += d(tour[i], tour[(i + 1) % m]);
            c += cost[tour[i]];
        }
        return {len, c};
    }
};

// splitmix64, identical in every language so random runs match exactly
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
    void shuffle(std::vector<int> &a) {
        for (int i = static_cast<int>(a.size()) - 1; i > 0; i--) std::swap(a[i], a[below(i + 1)]);
    }
};

inline std::string tour_json(const std::vector<int> &tour, size_t m) {
    std::string s = "[";
    for (size_t i = 0; i < m; i++) {
        if (i) s += ',';
        s += std::to_string(tour[i]);
    }
    return s + "]";
}

inline void emit_step(const std::vector<int> &tour, size_t m) {
    std::cout << "{\"t\":\"step\",\"tour\":" << tour_json(tour, m) << "}" << std::endl;
}
inline void emit_step(const std::vector<int> &tour) { emit_step(tour, tour.size()); }

using SolveFn = std::function<std::vector<int>(const Instance &, const std::string &, int, uint64_t, bool)>;
using RunsFn = std::function<int(const std::string &, int)>;  // -1 = unknown method

inline int run(int argc, char **argv, const SolveFn &solve, const RunsFn &runs_for) {
    std::ios::sync_with_stdio(false);
    std::string instance, method, mode = "bench";
    int start = 0;
    uint64_t seed = 42;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string key = argv[i], val = argv[i + 1];
        if (key == "--instance") instance = val;
        else if (key == "--method") method = val;
        else if (key == "--mode") mode = val;
        else if (key == "--start") start = std::stoi(val);
        else if (key == "--seed") seed = std::stoull(val);
        else fail("unknown argument " + key);
    }
    if (instance.empty() || method.empty()) fail("--instance and --method are required");

    Instance in = Instance::load(instance);
    bool trace = mode == "trace";
    int total = runs_for(method, in.n);
    if (total < 0) fail("unknown method " + method);
    if (trace && (start < 0 || start >= total)) fail("start out of range");
    int first = trace ? start : 0, count = trace ? 1 : total;

    std::cout << "{\"t\":\"meta\",\"n\":" << in.n << ",\"k\":" << in.k << ",\"runs\":" << count
              << ",\"mode\":\"" << (trace ? "trace" : "bench") << "\"}" << std::endl;

    long long total_ns = 0;
    for (int r = first; r < first + count; r++) {
        auto t0 = std::chrono::steady_clock::now();
        std::vector<int> tour = solve(in, method, r, seed + static_cast<uint64_t>(r), trace);
        long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
        total_ns += ns;
        auto [len, c] = in.evaluate(tour);
        std::cout << "{\"t\":\"run_end\",\"run\":" << r << ",\"obj\":" << len + c << ",\"len\":" << len
                  << ",\"cost\":" << c << ",\"ns\":" << ns << ",\"tour\":" << tour_json(tour, tour.size()) << "}"
                  << std::endl;
    }
    std::cout << "{\"t\":\"done\",\"total_ns\":" << total_ns << "}" << std::endl;
    return 0;
}

}  // namespace tsp
