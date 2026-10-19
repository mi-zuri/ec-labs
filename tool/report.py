"""Report generator shared by all labs. Standard library only (PDF export needs Chrome/Chromium).

labs/labN/report/Report.md is the source of truth and is meant to be edited by hand.
Text outside the `<!-- auto:NAME -->` ... `<!-- /auto:NAME -->` blocks is never touched.
The blocks (results, solutions, source) are rebuilt from out/results on every run,
and re-added at the end of the file if they were deleted, so the report always has every part.

Everything lives in labs/labN/report/ (Report.md, ReportLabN_<author>.pdf, report_files/*.svg).
First generation starts from tool/report_template.md; algorithms and conclusions are written by hand in Report.md.
Hand-made figures (e.g. flowcharts) go to labs/labN/report/figures/ (report_files/ is regenerated).
Two columns side by side: `<!-- columns -->` left part `<!-- column -->` right part `<!-- /columns -->`.
`<!-- pagebreak -->` starts a new page at the next heading.
"""
import glob
import html
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile

TOOL = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOL)
DATA = os.path.join(ROOT, "data")
RESULTS = os.path.join(ROOT, "out", "results")
TEMPLATE = os.path.join(TOOL, "report_template.md")
LANG_ORDER = ["python", "c", "cpp", "rust"]
LANG_NAMES = {"python": "Python", "c": "C", "cpp": "C++", "rust": "Rust"}
LANG_SRC = {"python": "python/solver.py", "c": "c/solver.c", "cpp": "cpp/solver.cpp", "rust": "rust/solver.rs"}

MD_NAME, FILES_DIR, REPORT_DIR = "Report.md", "report_files", "report"
PDF_AUTHOR = "MichałŻurawski"  # used in the PDF file name
BLOCK_TITLES = {
    "results": "Results",
    "times": "Running time",
    "solutions": "Best solutions (0-based node indices)",
    "source": "Source code",
}
BLOCK_RE = re.compile(r"<!-- auto:(\w+)\b[^>]*-->.*?<!-- /auto:\1 -->", re.S)


# ---------------------------------------------------------------- data

def read_text(path, default=""):
    try:
        with open(path, encoding="utf-8") as f:
            return f.read().strip()
    except OSError:
        return default


def load_nodes(instance):
    nodes = []
    with open(os.path.join(DATA, instance + ".csv")) as f:
        for line in f:
            if line.strip():
                nodes.append([int(v) for v in line.strip().split(";")])
    return nodes


def load_results(lab_id, instance):
    """{method: {lang: result}}"""
    out = {}
    for path in glob.glob(os.path.join(RESULTS, lab_id, instance, "*", "*.json")):
        with open(path) as f:
            r = json.load(f)
        out.setdefault(r["method"], {})[r["lang"]] = r
    return out


def pick(by_lang):
    """The result to report: first available language in a fixed order (all languages are expected to agree)."""
    for lang in LANG_ORDER:
        if lang in by_lang:
            return by_lang[lang]
    return None


def github_base():
    """(https url of the repo, default branch) from the git remote, or (None, None)."""
    def git(*args):
        try:
            return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, timeout=10).stdout.strip()
        except (OSError, subprocess.SubprocessError):
            return ""
    url = git("remote", "get-url", "origin")
    m = re.match(r"(?:git@github\.com:|https?://(?:[^@/]+@)?github\.com/)([^/]+/[^/]+?)(?:\.git)?/?$", url)
    if not m:
        return None, None
    head = git("symbolic-ref", "--short", "refs/remotes/origin/HEAD")  # e.g. origin/main
    return "https://github.com/" + m.group(1), (head.split("/", 1)[1] if "/" in head else "main")


# ---------------------------------------------------------------- plots

# Same colours as the viewer dashboard (COST_STOPS and --tour in tool/viewer).
COST_STOPS = [(42, 157, 143), (233, 196, 106), (228, 87, 46)]  # cheap -> expensive
TOUR_COLOR = "#3157d5"
START_COLOR = "#e11d2e"  # the start node is marked with a red X, the first edges start red
FADE_FRACTION = 0.15  # the first 15% of the tour length fades from red to the tour colour


def rgb(hex_color):
    v = int(hex_color.lstrip("#"), 16)
    return v >> 16, (v >> 8) & 255, v & 255


def cost_color(t):
    """Teal -> yellow -> red gradient for a cost position t in [0, 1]."""
    seg, f = (0, t * 2) if t < 0.5 else (1, (t - 0.5) * 2)
    a, b = COST_STOPS[seg], COST_STOPS[seg + 1]
    return "rgb(%d,%d,%d)" % tuple(round(a[i] + (b[i] - a[i]) * f) for i in range(3))


def fade_edges(nodes, tour):
    """(a, b, f_a, f_b) for the edges at the start of the tour, a -> b.
    f is the fade position: 0 = red at the start node, 1 = tour colour after FADE_FRACTION of the tour length (may exceed 1 for f_b)."""
    dist = lambda a, b: math.hypot(nodes[a][0] - nodes[b][0], nodes[a][1] - nodes[b][1])
    fade_length = FADE_FRACTION * sum(dist(tour[i - 1], tour[i]) for i in range(len(tour)))
    walked = 0  # distance from the start to the beginning of the current edge
    for a, b in zip(tour, tour[1:]):
        if walked >= fade_length:
            return
        yield a, b, walked / fade_length, (walked + dist(a, b)) / fade_length
        walked += dist(a, b)


def svg_plot(nodes, tour):
    """Best tour as on the dashboard: node colour and size show the cost (teal/small = cheap, red/large = expensive);
    unselected nodes are faded."""
    size, pad = 600, 14
    xs, ys = [n[0] for n in nodes], [n[1] for n in nodes]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    sx = lambda x: pad + (x - x0) / ((x1 - x0) or 1) * (size - 2 * pad)
    sy = lambda y: size - pad - (y - y0) / ((y1 - y0) or 1) * (size - 2 * pad)
    cmin, cmax = min(n[2] for n in nodes), max(n[2] for n in nodes)
    used = set(tour)
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {size} {size}" width="{size}" height="{size}">',
           f'<rect width="{size}" height="{size}" fill="#fff"/>']
    pts = " ".join(f"{sx(nodes[v][0]):.1f},{sy(nodes[v][1]):.1f}" for v in tour + tour[:1])
    out.append(f'<polyline points="{pts}" fill="none" stroke="{TOUR_COLOR}" stroke-width="2" stroke-linejoin="round"/>')
    start, end = rgb(START_COLOR), rgb(TOUR_COLOR)
    fade_color = lambda f: "rgb(%d,%d,%d)" % tuple(round(start[c] + (end[c] - start[c]) * min(f, 1)) for c in range(3))
    defs, lines = [], []
    for k, (a, b, f_a, f_b) in enumerate(fade_edges(nodes, tour)):  # the start of the tour fades from red to the tour colour: shows where it starts and its direction
        (ax, ay), (bx, by) = (sx(nodes[a][0]), sy(nodes[a][1])), (sx(nodes[b][0]), sy(nodes[b][1]))
        blue_at = min(1, (1 - f_a) / (f_b - f_a))  # where along the edge the fade is complete
        defs.append(f'<linearGradient id="fade{k}" gradientUnits="userSpaceOnUse" x1="{ax:.1f}" y1="{ay:.1f}" x2="{bx:.1f}" y2="{by:.1f}">'
                    f'<stop offset="0" stop-color="{fade_color(f_a)}"/><stop offset="{blue_at:.3f}" stop-color="{fade_color(f_b)}"/></linearGradient>')
        lines.append(f'<line x1="{ax:.1f}" y1="{ay:.1f}" x2="{bx:.1f}" y2="{by:.1f}" stroke="url(#fade{k})" stroke-width="3" stroke-linecap="round"/>')
    out.append("<defs>" + "".join(defs) + "</defs>" + "".join(lines))
    for i, n in enumerate(nodes):
        t = (n[2] - cmin) / ((cmax - cmin) or 1)
        style = f'stroke="#1c2230" stroke-opacity=".6" stroke-width=".8"' if i in used else 'opacity=".28"'
        out.append(f'<circle cx="{sx(n[0]):.1f}" cy="{sy(n[1]):.1f}" r="{3 + 5 * t:.1f}" fill="{cost_color(t)}" {style}/>')
    x, y, d = sx(nodes[tour[0]][0]), sy(nodes[tour[0]][1]), 8
    cross = f"M{x - d:.1f},{y - d:.1f}L{x + d:.1f},{y + d:.1f}M{x - d:.1f},{y + d:.1f}L{x + d:.1f},{y - d:.1f}"
    for color, width in (("#fff", 5.5), (START_COLOR, 3)):  # white outline keeps the X visible on any node
        out.append(f'<path d="{cross}" stroke="{color}" stroke-width="{width}" stroke-linecap="round" fill="none"/>')
    out.append("</svg>")
    return "".join(out)


def nice_axis(top, ticks=8):
    """(step, max) with a whole-number step (1, 2 or 5 times a power of ten) so that `ticks` steps cover [0, top]."""
    raw = top / ticks
    power = 10 ** math.floor(math.log10(raw))
    step = next(m * power for m in (1, 2, 5, 10) if m * power >= raw)
    return step, step * math.ceil(top / step)


def svg_pareto(points, lang_name):
    """Average objective against average run time (both minimised, linear axes from 0, objective axis flipped so the best is at the top).
    points: [(label, ns, objective)]. The Pareto front (no other method is both faster and better) is joined by a step line."""
    w, h, left, right, top, bottom = 1200, 560, 90, 480, 30, 70
    t_unit, t_name = next((u, n) for u, n in ((1e9, "s"), (1e6, "ms"), (1e3, "µs"), (1, "ns")) if max(t for _, t, _ in points) >= u or u == 1)
    t_step, t_max = nice_axis(max(t for _, t, _ in points) / t_unit)
    o_step, o_max = nice_axis(max(o for _, _, o in points))
    t_max *= t_unit
    sx = lambda t: left + t / t_max * (w - left - right)
    sy = lambda o: top + o / o_max * (h - top - bottom)  # flipped: the best (lowest) objective is at the top
    dominated = lambda a: any(b[1] <= a[1] and b[2] <= a[2] and (b[1], b[2]) != (a[1], a[2]) for b in points)
    front = sorted((p for p in points if not dominated(p)), key=lambda p: p[1])
    plot_w, plot_h = w - left - right, h - top - bottom
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" font-family="Helvetica, Arial, sans-serif" font-size="15">',
           f'<rect width="{w}" height="{h}" fill="#fff"/>',
           f'<rect x="{left}" y="{top}" width="{plot_w}" height="{plot_h}" fill="#f8f9fc"/>']
    for k in range(round(t_max / t_unit / t_step) + 1):
        x = sx(k * t_step * t_unit)
        out.append(f'<line x1="{x:.1f}" y1="{top}" x2="{x:.1f}" y2="{h - bottom}" stroke="#e1e5ee"/>'
                   f'<text x="{x:.1f}" y="{h - bottom + 24}" text-anchor="middle" fill="#4a5163">{k * t_step:g}</text>')
    for k in range(round(o_max / o_step) + 1):
        y = sy(k * o_step)
        out.append(f'<line x1="{left}" y1="{y:.1f}" x2="{w - right}" y2="{y:.1f}" stroke="#e1e5ee"/>'
                   f'<text x="{left - 10}" y="{y + 5:.1f}" text-anchor="end" fill="#4a5163">{k * o_step:g}</text>')
    out.append(f'<rect x="{left}" y="{top}" width="{plot_w}" height="{plot_h}" fill="none" stroke="#9aa1b3"/>')
    out.append(f'<text x="{left + plot_w / 2}" y="{h - 14}" text-anchor="middle" font-size="17" fill="#1c2230">average running time ({lang_name.lower()}), {t_name}</text>')
    out.append(f'<text transform="translate(22,{top + plot_h / 2}) rotate(-90)" text-anchor="middle" font-size="17" fill="#1c2230">score</text>')
    steps = f"M{sx(front[0][1]):.1f},{sy(front[0][2]):.1f}"
    for _, t, o in front[1:]:
        steps += f"H{sx(t):.1f}V{sy(o):.1f}"
    steps += f"H{w - right}"  # the best objective found so far holds for any longer running time
    out.append(f'<path d="{steps}" fill="none" stroke="{TOUR_COLOR}" stroke-width="2" stroke-dasharray="6 4" opacity=".7"/>')
    for k, (label, t, o) in enumerate(points, 1):  # numbered points (names would overlap), the legend on the right gives the names
        on_front = (label, t, o) in front
        color = TOUR_COLOR if on_front else "#8b92a5"
        ly = top + 20 + (k - 1) * 32
        weight = ' font-weight="bold"' if on_front else ""
        out.append(f'<circle cx="{sx(t):.1f}" cy="{sy(o):.1f}" r="12" fill="{color}" stroke="#fff" stroke-width="2"/>'
                   f'<text x="{sx(t):.1f}" y="{sy(o) + 5:.1f}" text-anchor="middle" fill="#fff" font-size="13" font-weight="bold">{k}</text>'
                   f'<circle cx="{w - right + 36}" cy="{ly - 5}" r="12" fill="{color}"/>'
                   f'<text x="{w - right + 36}" y="{ly}" text-anchor="middle" fill="#fff" font-size="13" font-weight="bold">{k}</text>'
                   f'<text x="{w - right + 58}" y="{ly}" fill="#1c2230"{weight}>{html.escape(label)}</text>')
    out.append("</svg>")
    return "".join(out)


def fmt_time(ns):
    """Same units as the viewer's fmtTime."""
    if ns < 1e3:
        return f"{ns:.0f} ns"
    if ns < 1e6:
        return f"{ns / 1e3:.1f} µs"
    if ns < 1e9:
        return f"{ns / 1e6:.2f} ms"
    return f"{ns / 1e9:.2f} s"


# ---------------------------------------------------------------- generated blocks

def report_dir(lab):
    return os.path.join(lab["dir"], REPORT_DIR)


def block(name, body):
    return f"<!-- auto:{name} (generated: edits between these markers are overwritten) -->\n{body.strip()}\n<!-- /auto:{name} -->"


def entries_of(labs):
    """(lab, method, display name) for every solution to compare."""
    return [(l, m, m["name"]) for l in labs for m in l["methods"]]


def time_section(inst, entries, by_lab, files_dir):
    """Markdown for one instance: table of the average time per run, methods by language, and the time/quality Pareto plot."""
    rows = []  # (method name, {lang: avg ns})
    pareto = []  # (method name, avg ns, avg objective), one language for all methods
    for lab, m, mname in entries:
        by = {lang: r["stats"]["ns_avg"] for lang, r in by_lab[lab["id"]].get(m["id"], {}).items()}
        if by:
            rows.append((mname, by))
            r = pick(by_lab[lab["id"]][m["id"]])
            pareto.append((mname, r["stats"]["ns_avg"], r["stats"]["obj_avg"], r["lang"]))
    if not rows:
        return f"### Instance {inst}\n\nno benchmark results\n"
    langs = [l for l in LANG_ORDER if any(l in by for _, by in rows)]
    table = ["| Method | " + " | ".join(LANG_NAMES[l] for l in langs) + " |", "|---|" + "--:|" * len(langs)]
    for mname, by in rows:
        fastest = min(by.values())
        table.append(f"| {mname} | " + " | ".join(
            "" if l not in by else f"**{fmt_time(by[l])}**" if by[l] == fastest else fmt_time(by[l]) for l in langs) + " |")
    name = f"{inst}_pareto.svg"
    with open(os.path.join(files_dir, name), "w") as f:
        f.write(svg_pareto([(n, t, o) for n, t, o, _ in pareto], LANG_NAMES[pareto[0][3]]))
    caption = f"Pareto plot, instance {inst}"
    return f"### Instance {inst}\n\n" + "\n".join(table) + f"\n\n### Pareto plot (Instance {inst})\n\n![{caption}]({FILES_DIR}/{name})\n"


def build_blocks(labs, instances, out_dir, own):
    """Return ({block name: markdown}, [warnings]). Also writes the plot SVGs.

    `labs` are the labs whose solutions are compared (the report's own lab is normally one of them).
    `own` is the report's lab: the best solutions and source code sections cover only that lab.
    """
    warnings = []
    entries = entries_of(labs)
    res, times, sols = [], [], []
    files_dir = os.path.join(out_dir, FILES_DIR)
    os.makedirs(files_dir, exist_ok=True)
    for old in glob.glob(os.path.join(files_dir, "*.svg")):
        os.remove(old)

    all_checked, all_same, multi_lang = True, True, False
    for inst in instances:
        by_lab = {l["id"]: load_results(l["id"], inst) for l in labs}
        nodes = load_nodes(inst)
        res.append(f"### Instance {inst}\n\n| Method | Min | Max | Avg |\n|---|--:|--:|--:|")
        plots = []  # the visualisations of this instance, shown under its results table
        times.append(time_section(inst, entries, by_lab, files_dir))
        sols.append(f"### Instance {inst}\n")
        rows = []  # (method name, [min, max, avg]) or (method name, None) when not run
        for lab, m, mname in entries:
            by = by_lab[lab["id"]]
            r = pick(by.get(m["id"], {}))
            if r is None:
                warnings.append(f"no benchmark results for {inst} / {mname}")
                rows.append((mname, None))
                continue
            langs = by[m["id"]]
            if len(langs) > 1:
                multi_lang = True
                all_same &= all(x["stats"]["obj_min"] == r["stats"]["obj_min"] and x["best"]["tour"] == r["best"]["tour"]
                                for x in langs.values())
            all_checked &= bool(r["checked"])
            s, b = r["stats"], r["best"]
            rows.append((mname, [s["obj_min"], s["obj_max"], round(s["obj_avg"])]))
            name = f"{inst}_{lab['id']}_{m['id']}.svg"
            with open(os.path.join(files_dir, name), "w") as f:
                f.write(svg_plot(nodes, b["tour"]))
            plots.append(f"![{mname}<br>**{b['obj']}** (length {b['len']} + cost {b['cost']})]({FILES_DIR}/{name})")
            if lab["id"] == own["id"]:
                sols.append(f"**{mname}**\n{', '.join(map(str, b['tour']))}\n")
        # objective is minimised, so the lowest value in each column is the best (ties are all bold)
        best = [min((v[k] for _, v in rows if v), default=None) for k in range(3)]
        for mname, vals in rows:
            if vals is None:
                res.append(f"| {mname} | not run | | |")
                continue
            cells = [f"**{v}**" if v == best[k] else str(v) for k, v in enumerate(vals)]
            res.append(f"| {mname} | {' | '.join(cells)} |")
        res.append("")
        res.append(f"### Visualisation (Instance {inst})\n")
        res.extend(plots)
        res.append("")

    n_runs = ", ".join(sorted({str(len(r["objs"])) for i in instances for l in labs for by in [load_results(l["id"], i)]
                               for langs in by.values() for r in langs.values()}))
    summary = (f"Objective = cycle length + sum of node costs, over {n_runs or '0'} runs per method. The best value in each column is in bold.\n"
               f"All best solutions were checked with the solution checker (the same computation as the lecturer's Solution checker.xlsx): **{'yes' if all_checked else 'NO'}**.")
    if multi_lang:
        summary += f"\nResults are identical in all implemented languages: **{'yes' if all_same else 'NO'}**."
    times_intro = ("Average time of one run of the algorithm only, over the same runs as above. "
                   "The fastest language in each row is in bold.")

    base, branch = github_base()
    rel = f"labs/{own['id']}"
    if base:
        links = [f"[{LANG_NAMES[l]}]({base}/blob/{branch}/{rel}/{LANG_SRC[l]})" for l in LANG_ORDER if l in own["langs"]]
        source = f"[{rel}]({base}/tree/{branch}/{rel}) ({', '.join(links)})"
    else:
        source = f"`{rel}/` (no GitHub remote found, so no link)"

    return {
        "results": summary + "\n\n" + "\n".join(res),
        "times": times_intro + "\n\n" + "\n".join(times),
        "solutions": "\n".join(sols),
        "source": source,
    }, warnings


# ---------------------------------------------------------------- Markdown file

def first_text(lab):
    return read_text(TEMPLATE).replace("{{title}}", "Lab %d - %s" % (lab["num"], lab["title"]))


def write_markdown(lab, instances, labs):
    """Create or refresh Report.md. Returns (path, created, warnings)."""
    out_dir = report_dir(lab)
    os.makedirs(out_dir, exist_ok=True)
    md_path = os.path.join(out_dir, MD_NAME)
    created = not os.path.exists(md_path)
    text = first_text(lab) if created else read_text(md_path)
    text = re.sub(r"## Visualisation of the best solutions\s*<!-- auto:plots.*?<!-- /auto:plots -->\s*", "", text, flags=re.S)  # old separate section: the plots now live under Results
    blocks, warnings = build_blocks(labs, instances, out_dir, lab)

    seen = set()

    def fill(m):
        name = m.group(1)
        if name not in blocks:
            return m.group(0)
        seen.add(name)
        return block(name, blocks[name])

    text = BLOCK_RE.sub(fill, text)
    for name in blocks:  # a block the user deleted comes back at the end, so the report is always complete
        if name not in seen:
            if not created:
                warnings.append(f"block '{name}' was missing and has been added at the end")
            text += f"\n\n## {BLOCK_TITLES[name]}\n\n{block(name, blocks[name])}"
    with open(md_path, "w", encoding="utf-8") as f:
        f.write(text.strip() + "\n")
    return md_path, created, warnings


# ---------------------------------------------------------------- Markdown -> HTML (the subset used in reports)

def inline(s):
    codes = []
    s = re.sub(r"`([^`]+)`", lambda m: codes.append(html.escape(m.group(1))) or f"\0{len(codes) - 1}\0", s)
    s = html.escape(s, quote=False)
    s = re.sub(r"!\[(.*?)\]\((.*?)\)", lambda m: f'<figure><img src="{m.group(2)}" alt="{re.sub(r"&lt;br&gt;|[*]{2}", " ", m.group(1))}">'
               f'<figcaption>{m.group(1).replace("&lt;br&gt;", "<br>")}</figcaption></figure>', s)
    s = re.sub(r"\[(.*?)\]\((.*?)\)", r'<a href="\2">\1</a>', s)
    s = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", s)
    s = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"<i>\1</i>", s)
    s = re.sub(r"(?<![\w])_(?!\s)(.+?)(?<!\s)_(?![\w])", r"<i>\1</i>", s)
    return re.sub(r"\0(\d+)\0", lambda m: f"<code>{codes[int(m.group(1))]}</code>", s)


def markdown_to_html(md, base_dir, title):
    lines = md.splitlines()
    out, i = [], 0
    para = []
    sec = {"at": None, "n": 0, "first": True, "h2": "", "h3": ""}  # index in `out` where the current ### block started; ### blocks seen in this ## section

    def close_block():
        """Wrap a ### block in a div; blocks with a table are kept on one page (figure grids may continue on the next page, row by row)."""
        if sec["at"] is not None:
            chunk = out[sec["at"]:]
            keep = any(c.startswith("<table") for c in chunk)
            new_page = not sec["first"] and sec["h2"] in (BLOCK_TITLES["results"], BLOCK_TITLES["times"]) and sec["h3"].startswith("Instance")  # every instance but the first starts a page
            out[sec["at"]:] = ['<div class="%s">' % ("blk keep" if keep else "blk") + "".join(chunk) + "</div>"]
            if new_page:
                out[sec["at"]] = out[sec["at"]].replace('class="blk', 'class="pbk blk', 1)
            sec["at"] = None

    def flush():
        if para:
            if all(p.startswith("![") for p in para):
                out.append('<div class="grid%s">' % (" one" if len(para) == 1 and "pareto" not in para[0] else " wide" if len(para) == 1 else "") + inline("".join(para)) + "</div>")
            else:
                out.append("<p>" + inline("\n".join(para)).replace("\n", "<br>") + "</p>")
            para.clear()

    while i < len(lines):
        line = lines[i]
        if line.startswith("```"):
            flush()
            j = i + 1
            while j < len(lines) and not lines[j].startswith("```"):
                j += 1
            out.append("<pre>" + html.escape("\n".join(lines[i + 1:j])) + "</pre>")
            i = j + 1
        elif m := re.match(r"\s*<!-- (columns|column|/columns) -->\s*$", line):  # two columns side by side (GitHub hides the markers and shows them stacked)
            flush()
            out.append({"columns": '<div class="cols"><div>', "column": "</div><div>", "/columns": "</div></div>"}[m.group(1)])
            i += 1
        elif re.match(r"\s*<!-- pagebreak -->\s*$", line):  # the next heading starts a new page
            flush()
            sec["break"] = True
            i += 1
        elif line.lstrip().startswith("<!--"):
            flush()
            if re.match(r"\s*<!-- auto:(results|times|solutions|source)\b", line) and out and out[-1].startswith("<h2>"):
                out[-1] = out[-1].replace("<h2>", '<h2 class="pb">', 1)  # new page before the Results, Running time, Best solutions and Source code sections
            while "-->" not in lines[i]:
                i += 1
            i += 1
        elif m := re.match(r"(#{1,6})\s+(.*)", line):
            flush()
            if len(m.group(1)) <= 3:
                close_block()
            if len(m.group(1)) == 2:
                sec["n"], sec["h2"] = 0, m.group(2)
            if len(m.group(1)) == 3:
                sec["h3"] = m.group(2)
                sec["first"] = sec["n"] == 0
                sec["n"] += 1
                sec["at"] = len(out)
            page_break = ' class="pb"' if sec.pop("break", False) else ""
            out.append(f"<h{len(m.group(1))}{page_break}>{inline(m.group(2))}</h{len(m.group(1))}>")
            i += 1
        elif line.startswith("|"):
            flush()
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                i += 1
            align = ["right" if c.endswith(":") and not c.startswith(":") else "center" if c.startswith(":") and c.endswith(":") else "left"
                     for c in rows[1]] if len(rows) > 1 else []
            t = "<table><tr>" + "".join(f"<th>{inline(c)}</th>" for c in rows[0]) + "</tr>"
            for r in rows[2:]:
                t += "<tr>" + "".join(f'<td style="text-align:{align[k] if k < len(align) else "left"}">{inline(c)}</td>' for k, c in enumerate(r)) + "</tr>"
            out.append(t + "</table>")
        elif re.match(r"\s*([-*]|\d+\.)\s+", line):
            flush()
            ordered = bool(re.match(r"\s*\d+\.", line))
            items = []
            while i < len(lines) and re.match(r"\s*([-*]|\d+\.)\s+", lines[i]):
                items.append(re.sub(r"^\s*([-*]|\d+\.)\s+", "", lines[i]))
                i += 1
            tag = "ol" if ordered else "ul"
            out.append(f"<{tag}>" + "".join(f"<li>{inline(x)}</li>" for x in items) + f"</{tag}>")
        elif not line.strip():
            flush()
            i += 1
        else:
            para.append(line.strip())
            i += 1
    flush()
    close_block()
    body = "\n".join(out)
    body = re.sub(r'(<img src=")(?!https?:|file:|/)([^"]+)"', lambda m: m.group(1) + "file://" + os.path.join(base_dir, m.group(2)) + '"', body)
    return f"""<!doctype html><html><head><meta charset="utf-8"><title>{html.escape(title)}</title><style>
body{{font:13px/1.5 -apple-system,Helvetica,Arial,sans-serif;color:#222;margin:0}}
h1{{font-size:24px;margin:0 0 .3em}}h2{{font-size:18px;border-bottom:1px solid #ddd;padding-bottom:3px;margin-top:1.8em}}
.pb,.pbk{{break-before:page}}h3{{font-size:15px;margin:1.3em 0 .4em}}h2,h3{{break-after:avoid}}
table{{break-inside:avoid;border-collapse:collapse;margin:.5em 0}}td,th{{border:1px solid #ccc;padding:4px 9px}}th{{background:#f3f3f3;text-align:left}}
pre{{background:#f5f5f5;padding:9px;font-size:11px;white-space:pre-wrap;break-inside:avoid}}code{{background:#f0f0f0;padding:0 3px}}
.keep{{break-inside:avoid}}.grid{{display:grid;grid-template-columns:repeat(4,1fr);gap:6px;width:100%}}.grid.wide{{grid-template-columns:1fr;width:95%;margin:0}}.grid.wide figcaption{{display:none}}.grid.one{{grid-template-columns:1fr;width:25%;margin:0 auto}}figure{{margin:0;text-align:center;font-size:9px;break-inside:avoid}}
img{{width:100%;border:1px solid #ddd}}.cols{{display:flex;gap:18px;align-items:flex-start;break-inside:avoid}}.cols>div{{flex:1 1 0;min-width:0}}.cols>div+div{{flex:0 0 38%}}.cols .grid.one{{width:100%}}.cols img{{border:none}}p{{margin:.5em 0;overflow-wrap:anywhere}}a{{color:#1a55a5}}
</style></head><body>{body}</body></html>"""


# ---------------------------------------------------------------- PDF

def pdf_name(lab):
    return f"ReportLab{lab['num']}_{PDF_AUTHOR}.pdf"


def find_chrome():
    cands = [os.environ.get("CHROME", ""),
             "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
             "/Applications/Chromium.app/Contents/MacOS/Chromium",
             "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge"]
    cands += [shutil.which(n) or "" for n in ("google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "chrome", "microsoft-edge")]
    cands += [os.path.join(os.environ.get(v, ""), p) for v in ("PROGRAMFILES", "PROGRAMFILES(X86)", "LOCALAPPDATA")
              for p in (r"Google\Chrome\Application\chrome.exe", r"Microsoft\Edge\Application\msedge.exe")]
    return next((c for c in cands if c and os.path.isfile(c)), None)


def write_pdf(out_dir, md_path, name):
    """Render Report.md to the PDF `name` with headless Chrome. Returns (pdf path or None, error or None)."""
    chrome = find_chrome()
    if not chrome:
        return None, "PDF not created: Chrome/Chromium not found (set the CHROME environment variable to its path)"
    pdf_path = os.path.join(out_dir, name)
    with tempfile.TemporaryDirectory() as tmp:
        page = os.path.join(tmp, "report.html")
        with open(page, "w", encoding="utf-8") as f:
            f.write(markdown_to_html(read_text(md_path), out_dir, "Report"))
        cmd = [chrome, "--headless", "--disable-gpu", "--no-pdf-header-footer", "--user-data-dir=" + os.path.join(tmp, "profile"),
               "--allow-file-access-from-files", "--print-to-pdf=" + pdf_path, "file://" + page]
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        except (OSError, subprocess.SubprocessError) as e:
            return None, "PDF not created: " + str(e)
    if not os.path.exists(pdf_path):
        return None, "PDF not created: " + (proc.stderr.strip()[-300:] or "Chrome failed")
    return pdf_path, None


# ---------------------------------------------------------------- entry points

def status(lab):
    md, pdf = os.path.join(report_dir(lab), MD_NAME), os.path.join(report_dir(lab), pdf_name(lab))
    return {"pdf_name": pdf_name(lab), "md": os.path.exists(md), "pdf": os.path.exists(pdf),
            "md_mtime": os.path.getmtime(md) if os.path.exists(md) else None,
            "pdf_mtime": os.path.getmtime(pdf) if os.path.exists(pdf) else None}


def generate(lab, instances, labs=None):
    """Build the report of `lab`, comparing the solutions of `labs` (default: just `lab`)."""
    md_path, created, warnings = write_markdown(lab, instances, labs or [lab])
    pdf_path, error = write_pdf(report_dir(lab), md_path, pdf_name(lab))
    if error:
        warnings.append(error)
    return {"created": created, "warnings": warnings, **status(lab)}


def save_markdown(lab, text):
    """Overwrite Report.md with user edits (call generate() afterwards to refresh the PDF)."""
    with open(os.path.join(report_dir(lab), MD_NAME), "w", encoding="utf-8") as f:
        f.write(text)
    return status(lab)


if __name__ == "__main__":  # python3 tool/report.py lab1
    sys.path.insert(0, TOOL)
    import server
    labs = {l["id"]: l for l in server.find_labs()}
    target = labs.get(sys.argv[1] if len(sys.argv) > 1 else "")
    included = [labs.get(a) for a in sys.argv[2:]]
    if not target or None in included:
        sys.exit("usage: python3 tool/report.py <labN> [labM ...]   (report of labN comparing the listed labs, default labN; known: %s)" % ", ".join(labs))
    print(json.dumps(generate(target, server.find_instances(), included or [target]), indent=1))
