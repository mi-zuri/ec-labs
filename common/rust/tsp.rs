//! Shared helpers for Rust solvers: instance loading, RNG and the output protocol.
//! See common/PROTOCOL.md. A lab solver provides `solve` and `runs_for` and calls `run` from main.
#![allow(dead_code)]

use std::env;
use std::fs;
use std::io::{self, Write};
use std::time::Instant;

const DEFAULT_SEED: u64 = 42;

pub fn emit(line: &str) {
    let stdout = io::stdout();
    let mut out = stdout.lock();
    let _ = writeln!(out, "{}", line);
    let _ = out.flush();
}

pub fn fail(message: &str) -> ! {
    let escaped = message.replace('\\', "\\\\").replace('"', "'");
    emit(&format!("{{\"t\":\"error\",\"msg\":\"{}\"}}", escaped));
    std::process::exit(2)
}

pub fn tour_json(tour: &[usize]) -> String {
    let nodes: Vec<String> = tour.iter().map(|node| node.to_string()).collect();
    format!("[{}]", nodes.join(","))
}

pub fn emit_step(tour: &[usize]) {
    emit(&format!("{{\"t\":\"step\",\"tour\":{}}}", tour_json(tour)));
}

/// Distance matrix and node costs (coordinates are dropped after building the matrix).
pub struct Instance {
    /// Number of nodes.
    pub n: usize,
    /// Number of nodes to select.
    pub k: usize,
    /// `cost[v]`: cost of node v.
    pub cost: Vec<i64>,
    /// n * n, row major: rounded Euclidean distances. Use `d`.
    dist: Vec<i64>,
}

fn rounded_distance(x1: i64, y1: i64, x2: i64, y2: i64) -> i64 {
    let dx = (x1 - x2) as f64;
    let dy = (y1 - y2) as f64;
    ((dx * dx + dy * dy).sqrt() + 0.5).floor() as i64
}

impl Instance {
    /// Read "x;y;cost" lines and build the instance.
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
            let fields: Vec<i64> = line
                .split(';')
                .map(|field| field.trim().parse::<i64>().unwrap_or_else(|_| fail("bad number in instance")))
                .collect();
            if fields.len() != 3 {
                fail("expected 3 columns per line");
            }
            xs.push(fields[0]);
            ys.push(fields[1]);
            cost.push(fields[2]);
        }

        let n = cost.len();
        let mut dist = vec![0i64; n * n];
        for i in 0..n {
            for j in 0..n {
                dist[i * n + j] = rounded_distance(xs[i], ys[i], xs[j], ys[j]);
            }
        }

        Instance { n, k: (n + 1) / 2, cost, dist }
    }

    #[inline]
    pub fn d(&self, i: usize, j: usize) -> i64 {
        self.dist[i * self.n + j]
    }

    /// Returns (cycle length including the closing edge, total node cost).
    pub fn evaluate(&self, tour: &[usize]) -> (i64, i64) {
        let length = tour.len();
        let mut cycle_length = 0;
        let mut total_cost = 0;
        for i in 0..length {
            let next = (i + 1) % length;
            if length > 1 {
                cycle_length += self.d(tour[i], tour[next]);
            }
            total_cost += self.cost[tour[i]];
        }
        (cycle_length, total_cost)
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

    /// Fisher-Yates, in place.
    pub fn shuffle(&mut self, items: &mut [usize]) {
        for i in (1..items.len()).rev() {
            let j = self.below(i + 1);
            items.swap(i, j);
        }
    }
}

/// solve(instance, method, run, seed, trace) -> tour; runs_for(method, n) -> number of runs (None = unknown method).
pub type SolveFn = fn(&Instance, &str, usize, u64, bool) -> Vec<usize>;
pub type RunsFn = fn(&str, usize) -> Option<usize>;

struct Args {
    instance_path: String,
    method: String,
    is_trace: bool,
    /// Trace mode: run to trace.
    start: usize,
    seed: u64,
}

fn parse_args() -> Args {
    let argv: Vec<String> = env::args().skip(1).collect();
    let mut args = Args {
        instance_path: String::new(),
        method: String::new(),
        is_trace: false,
        start: 0,
        seed: DEFAULT_SEED,
    };

    for pair in argv.chunks_exact(2) {
        let flag = pair[0].as_str();
        let value = pair[1].clone();
        match flag {
            "--instance" => args.instance_path = value,
            "--method" => args.method = value,
            "--mode" => args.is_trace = value == "trace",
            "--start" => args.start = value.parse().unwrap_or_else(|_| fail("bad --start")),
            "--seed" => args.seed = value.parse().unwrap_or_else(|_| fail("bad --seed")),
            other => fail(&format!("unknown argument {}", other)),
        }
    }

    if args.instance_path.is_empty() || args.method.is_empty() {
        fail("--instance and --method are required");
    }
    args
}

/// Protocol driver. Bench mode runs every run; trace mode only the run given by --start.
pub fn run(solve: SolveFn, runs_for: RunsFn) {
    let args = parse_args();
    let instance = Instance::load(&args.instance_path);

    let total_runs = match runs_for(&args.method, instance.n) {
        Some(total) => total,
        None => fail(&format!("unknown method {}", args.method)),
    };
    if args.is_trace && args.start >= total_runs {
        fail("start out of range");
    }

    let run_indices: Vec<usize> = if args.is_trace { vec![args.start] } else { (0..total_runs).collect() };

    emit(&format!(
        "{{\"t\":\"meta\",\"n\":{},\"k\":{},\"runs\":{},\"mode\":\"{}\"}}",
        instance.n,
        instance.k,
        run_indices.len(),
        if args.is_trace { "trace" } else { "bench" }
    ));

    let mut total_ns: u128 = 0;
    for &run_index in &run_indices {
        let seed = args.seed.wrapping_add(run_index as u64);

        let started = Instant::now();
        let tour = solve(&instance, &args.method, run_index, seed, args.is_trace);
        let elapsed_ns = started.elapsed().as_nanos();
        total_ns += elapsed_ns;

        let (cycle_length, total_cost) = instance.evaluate(&tour);
        emit(&format!(
            "{{\"t\":\"run_end\",\"run\":{},\"obj\":{},\"len\":{},\"cost\":{},\"ns\":{},\"tour\":{}}}",
            run_index,
            cycle_length + total_cost,
            cycle_length,
            total_cost,
            elapsed_ns,
            tour_json(&tour)
        ));
    }

    emit(&format!("{{\"t\":\"done\",\"total_ns\":{}}}", total_ns));
}
