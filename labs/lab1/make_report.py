"""Builds labs/lab1/Report.html from saved benchmark results (stdlib only)."""
import json, os
R = os.path.join(os.path.dirname(__file__), "..", "..")
M = [("random", "Random"), ("nn_end", "NN (end only)"), ("nn_any", "NN (any position)"), ("greedy_cycle", "Greedy cycle")]
I = ["TSPA", "TSPB"]; L = ["python", "c", "cpp", "rust"]
def load(i, m, l): return json.load(open(f"{R}/out/results/lab1/{i}/{m}/{l}.json"))
def pts(i):
    return [tuple(int(v) for v in s.split(";")) for s in open(f"{R}/data/{i}.csv") if s.strip()]
def svg(i, tour):
    P = pts(i); xs = [p[0] for p in P]; ys = [p[1] for p in P]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys); W = 300; pad = 8
    sx = lambda x: pad + (x - x0) / (x1 - x0) * (W - 2 * pad)
    sy = lambda y: W - pad - (y - y0) / (y1 - y0) * (W - 2 * pad)
    cmax = max(p[2] for p in P); sel = set(tour)
    o = f'<svg viewBox="0 0 {W} {W}" class="plot">'
    for p in P:
        if P.index(p) not in sel: o += f'<circle cx="{sx(p[0]):.1f}" cy="{sy(p[1]):.1f}" r="1.5" fill="#ccc"/>'
    path = " ".join(f"{sx(P[v][0]):.1f},{sy(P[v][1]):.1f}" for v in tour + [tour[0]])
    o += f'<polyline points="{path}" fill="none" stroke="#4a6fa5" stroke-width="1"/>'
    for v in tour:
        g = int(235 - 235 * P[v][2] / cmax)
        o += f'<circle cx="{sx(P[v][0]):.1f}" cy="{sy(P[v][1]):.1f}" r="{2 + 3 * P[v][2] / cmax:.1f}" fill="rgb({g},{g},{g})" stroke="#222" stroke-width=".4"/>'
    return o + "</svg>"
plots = {i: "" for i in I}; lists = {i: "" for i in I}
rows = ""
same = True
for i in I:
    for m, name in M:
        d = {l: load(i, m, l) for l in L}
        s = d["python"]["stats"]; b = d["python"]["best"]
        same &= all(d[l]["stats"]["obj_min"] == s["obj_min"] and d[l]["best"]["tour"] == b["tour"] for l in L)
        chk = all(d[l]["checked"] for l in L)
        rows += f"<tr><td>{i}</td><td>{name}</td><td>{s['obj_min']}</td><td>{s['obj_max']}</td><td>{s['obj_avg']:.0f}</td><td>{'yes' if chk else 'NO'}</td></tr>"
        plots[i] += f'<figure>{svg(i, b["tour"])}<figcaption><b>{name}</b><br>obj {b["obj"]} (length {b["len"]} + cost {b["cost"]})</figcaption></figure>'
        lists[i] += f"<h4>{name} (obj {b['obj']})</h4><p class=tour>{b['tour']}</p>"
html = f"""<!doctype html><meta charset=utf-8><title>Lab 1 Report</title><style>
body{{font:15px/1.5 system-ui,sans-serif;max-width:860px;margin:2em auto;padding:0 16px;color:#222}}
h1{{margin-bottom:0}}h2{{border-bottom:1px solid #ddd;padding-bottom:4px;margin-top:2em}}
table{{border-collapse:collapse;width:100%}}td,th{{border:1px solid #ddd;padding:5px 9px;text-align:right}}td:nth-child(-n+2),th:nth-child(-n+2){{text-align:left}}
th{{background:#f4f4f4}}pre{{background:#f6f6f6;padding:10px;overflow:auto;font-size:13px}}
.grid{{display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:12px}}h3{{margin-top:1.5em;break-after:avoid}}h4{{margin:1em 0 .3em;break-after:avoid}}figure,.tour{{break-inside:avoid}}figure{{margin:0;text-align:center;font-size:12px}}
.plot{{width:100%;background:#fff;border:1px solid #ddd}}.tour{{font:12px monospace;word-break:break-all;background:#f6f6f6;padding:8px}}
</style>
<h1>Lab 1 - Greedy heuristics</h1><p>Evolutionary Computation, Assignment 1</p>
<h2>1. Problem</h2>
<p>Each instance (TSPA, TSPB) has 200 nodes with coordinates (x, y) and a cost. Select exactly <b>k = 100</b> nodes (half, rounded up) and connect them in a <b>cycle</b>. Minimise
<b>objective = cycle length + sum of selected node costs</b>. Distances are Euclidean, rounded to integers.</p>
<h2>2. Algorithms</h2>
<p>200 solutions per greedy method (one starting from each node) and 200 random solutions. "Nearest" always means the best <i>increase of the objective</i> (distance change + node cost), not distance alone.</p>
<pre>Random:       tour = first k nodes of a random permutation

NN-end(start):
  tour = [start]
  while |tour| &lt; k:
      u = unvisited node minimising dist(last, u) + cost(u)
      append u

NN-any(start):
  tour = [start]
  while |tour| &lt; k:
      for each unvisited u, each position p (front, between nodes, end):
          delta = added length(u at p) + cost(u)
      insert the (u, p) with smallest delta

Greedy-cycle(start):
  v = unvisited node minimising dist(start, v) + cost(v);  tour = [start, v]
  while |tour| &lt; k:
      for each unvisited u, each cycle edge (a,b):
          delta = d(a,u) + d(u,b) - d(a,b) + cost(u)
      insert the (u, edge) with smallest delta</pre>
<h2>3. Results (objective value, 200 runs each)</h2>
<table><tr><th>Instance</th><th>Method</th><th>Min</th><th>Max</th><th>Avg</th><th>Checked</th></tr>{rows}</table>
<p>All best solutions were verified with the solver checker in the tool (column "Checked"). Python, C, C++ and Rust give identical results: <b>{'yes' if same else 'NO'}</b>.</p>
<h2>4. Best solutions - visualisation</h2>
<p>Node colour and size = cost (dark/large = expensive). Grey dots are unselected nodes.</p>
{''.join(f'<h3>Instance {i}</h3><div class=grid>{plots[i]}</div>' for i in I)}
<h2>5. Best solutions - node lists (0-based)</h2>{''.join(f'<h3>Instance {i}</h3>{lists[i]}' for i in I)}
<h2>6. Source code</h2>
<p><code>labs/lab1/{{python,c,cpp,rust}}/solver.*</code>, shared code in <code>common/</code> (this folder, <code>make_report.py</code> builds the report).</p>
<h2>7. Conclusions</h2>
<ul><li>Random solutions are far worse than any greedy method (about 3-4x the objective).</li>
<li>On TSPA, inserting at any position (NN-any, greedy cycle) is clearly better than appending at the end (avg ~74000 vs ~87500), because the paths become much shorter.</li>
<li>On TSPA, NN-any and greedy cycle are close (best 72328 vs 72590; greedy cycle has the slightly better average).</li>
<li>On TSPB all three greedy methods have similar averages (~59500); NN-any and greedy cycle reach better best values (~53000 vs 56007), so the benefit shows mainly in the best start nodes.</li>
<li>Greedy methods are deterministic given the start node, so spread comes only from the start choice; trying all starts and keeping the best is cheap and worthwhile.</li></ul>"""
open(os.path.join(os.path.dirname(__file__), "Report.html"), "w").write(html)
print("ok", same)

# ---- Markdown + plot files (editable source for Word/Pages) ----
here = os.path.dirname(os.path.abspath(__file__))
os.makedirs(here + "/plots", exist_ok=True)
md = ["# Lab 1 - Greedy heuristics\n", "Evolutionary Computation, Assignment 1\n", "## 1. Problem\n",
 "Each instance (TSPA, TSPB) has 200 nodes with coordinates (x, y) and a cost. Select exactly **k = 100** nodes and connect them in a **cycle**. Minimise **objective = cycle length + sum of selected node costs**. Distances are Euclidean, rounded to integers.\n",
 "## 2. Algorithms\n",
 "200 solutions per greedy method (one starting from each node) and 200 random solutions. \"Nearest\" always means the best increase of the objective (distance change + node cost).\n"]
import re
pre = re.search(r"<pre>(.*?)</pre>", html, re.S).group(1).replace("&lt;", "<")
md += ["```\n" + pre + "\n```\n", "## 3. Results (objective value, 200 runs each)\n",
       "| Instance | Method | Min | Max | Avg | Checked |", "|---|---|--:|--:|--:|---|"]
for r in re.findall(r"<tr><td>(.*?)</td><td>(.*?)</td><td>(.*?)</td><td>(.*?)</td><td>(.*?)</td><td>(.*?)</td></tr>", rows):
    md.append("| " + " | ".join(r) + " |")
md.append("\nAll best solutions were verified with the solver checker. Python, C, C++ and Rust give identical results: " + ("yes" if same else "NO") + ".\n")
md.append("## 4. Best solutions - visualisation\n\nNode colour and size = cost (dark/large = expensive). Grey dots are unselected nodes.\n")
best = {(i, m): load(i, m, "python")["best"] for i in I for m, _ in M}
for i in I:
    md.append(f"### Instance {i}\n")
    for m, name in M:
        b = best[(i, m)]
        open(f"{here}/plots/{i}_{m}.svg", "w").write(svg(i, b["tour"]).replace("<svg ", '<svg xmlns="http://www.w3.org/2000/svg" width="600" height="600" ', 1))
        md.append(f"![{i} - {name}: objective {b['obj']} (length {b['len']} + cost {b['cost']})](plots/{i}_{m}.png){{width=8cm}}\n")
md.append("## 5. Best solutions - node lists (0-based)\n")
for i in I:
    md.append(f"### Instance {i}\n")
    for m, name in M:
        b = best[(i, m)]
        md.append(f"**{name}** (objective {b['obj']}): {', '.join(map(str, b['tour']))}\n")
md.append("## 6. Source code\n\n`labs/lab1/{python,c,cpp,rust}/solver.*`, shared code in `common/`.\n")
md.append("## 7. Conclusions\n")
md += ["- " + re.sub(r"<.*?>", "", x) for x in re.findall(r"<li>(.*?)</li>", html.split("7. Conclusions")[1], re.S)]
open(here + "/Report.md", "w").write("\n".join(md) + "\n")
