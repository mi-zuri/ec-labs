#!/usr/bin/env python3
"""EC lab runner: serves the viewer, compiles solvers on demand, runs them and streams their output.

Usage: python3 tool/server.py [--port 8000]   then open http://127.0.0.1:8000
Standard library only.
"""
import glob
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

TOOL = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOL)  # project root
VIEWER = os.path.join(TOOL, "viewer")
DATA = os.path.join(ROOT, "data")
LABS = os.path.join(ROOT, "labs")
COMMON = os.path.join(ROOT, "common")
BUILD = os.path.join(ROOT, "out", "build")
RESULTS = os.path.join(ROOT, "out", "results")
DEFAULT_SEED = 42

LANGS = {
    "python": {"name": "Python", "src": "python/solver.py", "tool": sys.executable},
    "c": {"name": "C", "src": "c/solver.c", "tool": "gcc"},
    "cpp": {"name": "C++", "src": "cpp/solver.cpp", "tool": "g++"},
    "rust": {"name": "Rust", "src": "rust/solver.rs", "tool": "rustc"},
}

BUILD_LOCK = threading.Lock()  # one compiler at a time
BENCH_LOCK = threading.Lock()  # one benchmark at a time, so timings are not disturbed
INSTANCE_CACHE = {}


# ---------------------------------------------------------------- catalog

def find_labs():
    labs = []
    for path in glob.glob(os.path.join(LABS, "lab*", "lab.json")):
        folder = os.path.basename(os.path.dirname(path))
        match = re.fullmatch(r"lab(\d+)", folder)
        if not match:
            continue
        with open(path) as f:
            spec = json.load(f)
        lab_dir = os.path.dirname(path)
        labs.append({
            "id": "lab" + match.group(1),
            "num": int(match.group(1)),
            "dir": lab_dir,
            "title": spec.get("title", folder),
            "methods": spec.get("methods", []),
            "langs": [l for l, info in LANGS.items() if os.path.exists(os.path.join(lab_dir, info["src"]))],
        })
    return sorted(labs, key=lambda l: l["num"])


def find_instances():
    return sorted(os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(DATA, "*.csv")))


def lang_status():
    out = {}
    for lang, info in LANGS.items():
        out[lang] = {"name": info["name"], "available": shutil.which(info["tool"]) is not None, "tool": os.path.basename(info["tool"])}
    return out


def load_instance(name):
    if name not in INSTANCE_CACHE:
        nodes = []
        with open(os.path.join(DATA, name + ".csv")) as f:
            for line in f:
                line = line.strip()
                if line:
                    nodes.append([int(v) for v in line.split(";")])
        n = len(nodes)
        dist = [[int(math.floor(math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) + 0.5)) for b in nodes] for a in nodes]
        INSTANCE_CACHE[name] = {"nodes": nodes, "dist": dist, "n": n, "k": (n + 1) // 2}
    return INSTANCE_CACHE[name]


# ---------------------------------------------------------------- build

def build_command(lang, src, out):
    if lang == "c":
        return ["gcc", "-O2", "-std=c11", "-Wall", "-o", out, src, "-lm"]
    if lang == "cpp":
        return ["g++", "-O2", "-std=c++17", "-Wall", "-o", out, src]
    return ["rustc", "--edition=2021", "-C", "opt-level=3", "-o", out, src]


def ensure_built(lab, lang):
    """Return (command to run the solver, info about the compile step)."""
    src = os.path.join(lab["dir"], LANGS[lang]["src"])
    if lang == "python":
        return [sys.executable, "-u", src], {"status": "interpreted"}
    if not shutil.which(LANGS[lang]["tool"]):
        return None, {"status": "error", "log": LANGS[lang]["tool"] + " not found on PATH (see README)"}

    out = os.path.join(BUILD, lab["id"], lang, "solver")
    deps = [src] + glob.glob(os.path.join(COMMON, lang, "*"))
    with BUILD_LOCK:
        if os.path.exists(out) and os.path.getmtime(out) >= max(os.path.getmtime(p) for p in deps):
            return [out], {"status": "cached"}
        os.makedirs(os.path.dirname(out), exist_ok=True)
        cmd = build_command(lang, src, out)
        t0 = time.perf_counter()
        proc = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
        info = {
            "status": "ok" if proc.returncode == 0 else "error",
            "cmd": " ".join(os.path.relpath(c, ROOT) if os.path.isabs(c) else c for c in cmd),
            "log": (proc.stdout + proc.stderr).strip(),
            "ms": round((time.perf_counter() - t0) * 1000),
        }
        return ([out] if proc.returncode == 0 else None), info


# ---------------------------------------------------------------- checker + results

def check_run(inst, msg):
    """Independent solution checker: k distinct valid nodes and the reported objective is correct."""
    tour = msg.get("tour", [])
    n, k, dist, nodes = inst["n"], inst["k"], inst["dist"], inst["nodes"]
    if len(tour) != k:
        return "expected %d nodes, got %d" % (k, len(tour))
    if len(set(tour)) != k or any(not 0 <= v < n for v in tour):
        return "duplicate or invalid node index"
    length = sum(dist[tour[i]][tour[(i + 1) % k]] for i in range(k))
    cost = sum(nodes[v][2] for v in tour)
    if (length, cost, length + cost) != (msg.get("len"), msg.get("cost"), msg.get("obj")):
        return "reported objective %s, checker computed %d" % (msg.get("obj"), length + cost)
    return None


def summarize(runs):
    objs = [r["obj"] for r in runs]
    times = [r["ns"] for r in runs]
    return {
        "obj_min": min(objs), "obj_max": max(objs), "obj_avg": sum(objs) / len(objs),
        "ns_min": min(times), "ns_max": max(times), "ns_avg": sum(times) / len(times), "ns_total": sum(times),
    }


def result_path(lab_id, instance, method, lang):
    return os.path.join(RESULTS, lab_id, instance, method, lang + ".json")


def save_result(lab_id, instance, method, lang, runs, best, wall_ms, all_ok, seed):
    data = {
        "lab": lab_id, "instance": instance, "method": method, "lang": lang, "seed": seed,
        "when": time.strftime("%Y-%m-%d %H:%M:%S"), "wall_ms": wall_ms, "checked": all_ok,
        "stats": summarize(runs),
        "objs": [r["obj"] for r in runs],
        "times": [r["ns"] for r in runs],
        "best": {k: best[k] for k in ("run", "obj", "len", "cost", "tour")},
    }
    path = result_path(lab_id, instance, method, lang)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(data, f)
    return os.path.relpath(path, ROOT)


def load_results(lab_id, instance):
    out = []
    for path in sorted(glob.glob(os.path.join(RESULTS, lab_id, instance, "*", "*.json"))):
        with open(path) as f:
            out.append(json.load(f))
    return out


# ---------------------------------------------------------------- HTTP

class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        if "/api/run" in self.path:
            sys.stderr.write("[run] %s\n" % self.path)

    def send_json(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def send_file(self, path, ctype):
        with open(path, "rb") as f:
            body = f.read()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(url.query).items()}
        try:
            if url.path == "/":
                return self.send_file(os.path.join(VIEWER, "index.html"), "text/html; charset=utf-8")
            if re.fullmatch(r"/[\w-]+\.(js|css)", url.path):
                path = os.path.join(VIEWER, url.path[1:])
                ctype = "text/javascript" if path.endswith(".js") else "text/css"
                if os.path.exists(path):
                    return self.send_file(path, ctype + "; charset=utf-8")
            if url.path == "/api/catalog":
                labs = [{k: v for k, v in lab.items() if k != "dir"} for lab in find_labs()]
                return self.send_json({"labs": labs, "instances": find_instances(), "langs": lang_status()})
            if url.path == "/api/instance":
                if q.get("name") not in find_instances():
                    return self.send_json({"error": "unknown instance"}, 404)
                inst = load_instance(q["name"])
                return self.send_json({"name": q["name"], "nodes": inst["nodes"], "k": inst["k"]})
            if url.path == "/api/results":
                return self.send_json(load_results(q.get("lab", ""), q.get("instance", "")))
            if url.path == "/api/run":
                return self.api_run(q)
            self.send_json({"error": "not found"}, 404)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def api_run(self, q):
        labs = {lab["id"]: lab for lab in find_labs()}
        lab = labs.get(q.get("lab"))
        if not lab:
            return self.send_json({"error": "unknown lab"}, 400)
        lang, method, instance, mode = q.get("lang"), q.get("method"), q.get("instance"), q.get("mode", "trace")
        if lang not in lab["langs"]:
            return self.send_json({"error": "no %s solver in this lab" % lang}, 400)
        if method not in [m["id"] for m in lab["methods"]]:
            return self.send_json({"error": "unknown method"}, 400)
        if instance not in find_instances():
            return self.send_json({"error": "unknown instance"}, 400)
        if mode not in ("trace", "bench"):
            return self.send_json({"error": "mode must be trace or bench"}, 400)
        try:
            start, seed = int(q.get("start", 0)), int(q.get("seed", DEFAULT_SEED))
        except ValueError:
            return self.send_json({"error": "start and seed must be integers"}, 400)

        self.send_response(200)
        self.send_header("Content-Type", "application/x-ndjson")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()

        def send(obj):
            self.wfile.write((json.dumps(obj, separators=(",", ":")) + "\n").encode())

        send({"t": "compile", "status": "building"})
        cmd, info = ensure_built(lab, lang)
        send(dict(info, t="compile"))
        if cmd is None:
            return send({"t": "end", "code": -1})

        args = ["--instance", os.path.join(DATA, instance + ".csv"), "--method", method,
                "--mode", mode, "--start", str(start), "--seed", str(seed)]
        if mode == "trace":
            return self.stream(cmd + args, send, lab, lang, method, instance, mode, seed)
        if not BENCH_LOCK.acquire(blocking=False):
            send({"t": "queued"})
            BENCH_LOCK.acquire()
        try:
            self.stream(cmd + args, send, lab, lang, method, instance, mode, seed)
        finally:
            BENCH_LOCK.release()

    def stream(self, cmd, send, lab, lang, method, instance, mode, seed):
        inst = load_instance(instance)
        runs, best, all_ok = [], None, True
        with tempfile.TemporaryFile(mode="w+") as err:
            wall0 = time.perf_counter()
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=err, text=True, bufsize=1, cwd=ROOT)
            try:
                for line in proc.stdout:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        msg = json.loads(line)
                    except ValueError:
                        send({"t": "log", "msg": line})
                        continue
                    if msg.get("t") == "run_end":
                        problem = check_run(inst, msg)
                        msg["ok"] = problem is None
                        if problem:
                            msg["why"] = problem
                            all_ok = False
                        runs.append({"run": msg["run"], "obj": msg["obj"], "ns": msg["ns"]})
                        if best is None or msg["obj"] < best["obj"]:
                            best = msg
                    elif msg.get("t") == "done":
                        msg["wall_ms"] = round((time.perf_counter() - wall0) * 1000, 1)
                        if mode == "bench" and runs:
                            msg["saved"] = save_result(lab["id"], instance, method, lang, runs, best,
                                                       msg["wall_ms"], all_ok, seed)
                    send(msg)
                proc.wait()
                if proc.returncode != 0:
                    err.seek(0)
                    send({"t": "error", "msg": err.read()[-4000:] or "solver exited with code %d" % proc.returncode})
                send({"t": "end", "code": proc.returncode})
            finally:
                if proc.poll() is None:  # client went away (Stop button): kill the solver
                    proc.kill()
                    proc.wait()


def main():
    port = 8000
    if "--port" in sys.argv:
        port = int(sys.argv[sys.argv.index("--port") + 1])
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.daemon_threads = True
    print("EC lab runner on http://127.0.0.1:%d  (Ctrl+C to stop)" % port)
    for lang, info in lang_status().items():
        if not info["available"]:
            print("  note: %s not found, %s solvers are disabled" % (info["tool"], info["name"]))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
