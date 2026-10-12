//! Shared helpers for Rust solvers: instance loading, RNG, timing and the output protocol.
//! See common/PROTOCOL.md for the contract every solver follows.
#![allow(dead_code)]

use std::env;
use std::fs;
use std::io::{self, Write};
use std::time::Instant;

pub fn emit(line: &str) {
    let stdout = io::stdout();
    let mut out = stdout.lock();
    let _ = writeln!(out, "{}", line);
    let _ = out.flush();
}

pub fn fail(msg: &str) -> ! {
    emit(&format!("{{\"t\":\"error\",\"msg\":\"{}\"}}", msg.replace('\\', "\\\\").replace('"', "'")));
    std::process::exit(2)
}

pub struct Instance {
    pub n: usize,
    pub k: usize,
    pub cost: Vec<i64>,
    dist: Vec<i64>, // n * n, row major
}

impl Instance {
    pub fn load(path: &str) -> Instance {
        let text = fs::read_to_string(path).unwrap_or_else(|_| fail("cannot open instance file"));
        let mut xs: Vec<i64> = Vec::new();
        let mut ys: Vec<i64> = Vec::new();
        let mut cost: Vec<i64> = Vec::new();
        for line in text.lines() {
            let line = line.trim();
            if line.is_empty() {
                continue;
            }
            let v: Vec<i64> = line
                .split(';')
                .map(|s| s.trim().parse::<i64>().unwrap_or_else(|_| fail("bad number in instance")))
                .collect();
            if v.len() != 3 {
                fail("expected 3 columns per line");
            }
            xs.push(v[0]);
            ys.push(v[1]);
            cost.push(v[2]);
        }
        let n = cost.len();
        let mut dist = vec![0i64; n * n];
        for i in 0..n {
            for j in 0..n {
                let dx = (xs[i] - xs[j]) as f64;
                let dy = (ys[i] - ys[j]) as f64;
                dist[i * n + j] = ((dx * dx + dy * dy).sqrt() + 0.5).floor() as i64;
            }
        }
        Instance { n, k: (n + 1) / 2, cost, dist }
    }

    #[inline]
    pub fn d(&self, i: usize, j: usize) -> i64 {
        self.dist[i * self.n + j]
    }

    /// Returns (cycle length, sum of node costs).
    pub fn evaluate(&self, tour: &[usize]) -> (i64, i64) {
        let m = tour.len();
        let mut len = 0;
        let mut cost = 0;
        for i in 0..m {
            if m > 1 {
                len += self.d(tour[i], tour[(i + 1) % m]);
            }
            cost += self.cost[tour[i]];
        }
        (len, cost)
    }
}

/// splitmix64, identical in every language so random runs match exactly.
pub struct Rng {
    state: u64,
}

impl Rng {
    pub fn new(seed: u64) -> Rng {
        Rng { state: seed }
    }

    pub fn next(&mut self) -> u64 {
        self.state = self.state.wrapping_add(0x9E3779B97F4A7C15);
        let mut z = self.state;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58476D1CE4E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D049BB133111EB);
        z ^ (z >> 31)
    }

    pub fn below(&mut self, n: usize) -> usize {
        (self.next() % n as u64) as usize
    }

    pub fn shuffle(&mut self, a: &mut [usize]) {
        let mut i = a.len();
        while i > 1 {
            i -= 1;
            let j = self.below(i + 1);
            a.swap(i, j);
        }
    }
}

pub fn tour_json(tour: &[usize]) -> String {
    let parts: Vec<String> = tour.iter().map(|v| v.to_string()).collect();
    format!("[{}]", parts.join(","))
}

pub fn emit_step(tour: &[usize]) {
    emit(&format!("{{\"t\":\"step\",\"tour\":{}}}", tour_json(tour)));
}

/// solve(inst, method, run, seed, trace) -> tour; runs_for(method, n) -> number of runs (None = unknown method).
pub type SolveFn = fn(&Instance, &str, usize, u64, bool) -> Vec<usize>;
pub type RunsFn = fn(&str, usize) -> Option<usize>;

pub fn run(solve: SolveFn, runs_for: RunsFn) {
    let argv: Vec<String> = env::args().skip(1).collect();
    let mut instance = String::new();
    let mut method = String::new();
    let mut trace = false;
    let mut start: usize = 0;
    let mut seed: u64 = 42;
    let mut i = 0;
    while i + 1 < argv.len() {
        let val = argv[i + 1].clone();
        match argv[i].as_str() {
            "--instance" => instance = val,
            "--method" => method = val,
            "--mode" => trace = val == "trace",
            "--start" => start = val.parse().unwrap_or_else(|_| fail("bad --start")),
            "--seed" => seed = val.parse().unwrap_or_else(|_| fail("bad --seed")),
            other => fail(&format!("unknown argument {}", other)),
        }
        i += 2;
    }
    if instance.is_empty() || method.is_empty() {
        fail("--instance and --method are required");
    }

    let inst = Instance::load(&instance);
    let total = match runs_for(&method, inst.n) {
        Some(t) => t,
        None => fail(&format!("unknown method {}", method)),
    };
    if trace && start >= total {
        fail("start out of range");
    }
    let ids: Vec<usize> = if trace { vec![start] } else { (0..total).collect() };

    emit(&format!(
        "{{\"t\":\"meta\",\"n\":{},\"k\":{},\"runs\":{},\"mode\":\"{}\"}}",
        inst.n,
        inst.k,
        ids.len(),
        if trace { "trace" } else { "bench" }
    ));
    let mut total_ns: u128 = 0;
    for &r in &ids {
        let t0 = Instant::now();
        let tour = solve(&inst, &method, r, seed.wrapping_add(r as u64), trace);
        let ns = t0.elapsed().as_nanos();
        total_ns += ns;
        let (len, cost) = inst.evaluate(&tour);
        emit(&format!(
            "{{\"t\":\"run_end\",\"run\":{},\"obj\":{},\"len\":{},\"cost\":{},\"ns\":{},\"tour\":{}}}",
            r,
            len + cost,
            len,
            cost,
            ns,
            tour_json(&tour)
        ));
    }
    emit(&format!("{{\"t\":\"done\",\"total_ns\":{}}}", total_ns));
}
