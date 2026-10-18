"""Report generator shared by all labs. Standard library only (PDF export needs Chrome/Chromium).

labs/labN/report/Report.md is the source of truth and is meant to be edited by hand.
Text outside the `<!-- auto:NAME -->` ... `<!-- /auto:NAME -->` blocks is never touched.
The blocks (results, plots, solutions, source) are rebuilt from out/results on every run,
and re-added at the end of the file if they were deleted, so the report always has every part.

Everything lives in labs/labN/report/ (Report.md, ReportLabN_<author>.pdf, report_files/*.svg).
First generation starts from tool/report_template.md; algorithms and conclusions are written by hand in Report.md.
"""
import glob
import html
import json
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
    "plots": "Visualisation of the best solutions",
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

def svg_plot(nodes, tour):
    """Best tour: node size and shade show the cost (dark/large = expensive); unselected nodes are small grey dots."""
    size, pad = 600, 14
    xs, ys = [n[0] for n in nodes], [n[1] for n in nodes]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    sx = lambda x: pad + (x - x0) / ((x1 - x0) or 1) * (size - 2 * pad)
    sy = lambda y: size - pad - (y - y0) / ((y1 - y0) or 1) * (size - 2 * pad)
    cmin, cmax = min(n[2] for n in nodes), max(n[2] for n in nodes)
    used = set(tour)
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {size} {size}" width="{size}" height="{size}">',
           f'<rect width="{size}" height="{size}" fill="#fff"/>']
    for i, n in enumerate(nodes):
        if i not in used:
            out.append(f'<circle cx="{sx(n[0]):.1f}" cy="{sy(n[1]):.1f}" r="3" fill="#ccc"/>')
    pts = " ".join(f"{sx(nodes[v][0]):.1f},{sy(nodes[v][1]):.1f}" for v in tour + tour[:1])
    out.append(f'<polyline points="{pts}" fill="none" stroke="#4a6fa5" stroke-width="2"/>')
    for v in tour:
        t = (nodes[v][2] - cmin) / ((cmax - cmin) or 1)
        g = int(235 - 215 * t)
        out.append(f'<circle cx="{sx(nodes[v][0]):.1f}" cy="{sy(nodes[v][1]):.1f}" r="{4 + 5 * t:.1f}" '
                   f'fill="rgb({g},{g},{g})" stroke="#222" stroke-width=".8"/>')
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
    """(lab, method, display name) for every solution to compare. With several labs the name says which lab it comes from."""
    many = len(labs) > 1
    return [(l, m, f"Lab {l['num']}: {m['name']}" if many else m["name"]) for l in labs for m in l["methods"]]


def time_section(inst, entries, by_lab):
    """Markdown for one instance: table of the average time per run, methods by language."""
    rows = []  # (method name, {lang: avg ns})
    for lab, m, mname in entries:
        by = {lang: r["stats"]["ns_avg"] for lang, r in by_lab[lab["id"]].get(m["id"], {}).items()}
        if by:
            rows.append((mname, by))
    if not rows:
        return f"### Instance {inst}\n\nno benchmark results\n"
    langs = [l for l in LANG_ORDER if any(l in by for _, by in rows)]
    table = ["| Method | " + " | ".join(LANG_NAMES[l] for l in langs) + " |", "|---|" + "--:|" * len(langs)]
    for mname, by in rows:
        fastest = min(by.values())
        table.append(f"| {mname} | " + " | ".join(
            "" if l not in by else f"**{fmt_time(by[l])}**" if by[l] == fastest else fmt_time(by[l]) for l in langs) + " |")
    return f"### Instance {inst}\n\n" + "\n".join(table) + "\n"


def build_blocks(labs, instances, out_dir):
    """Return ({block name: markdown}, [warnings]). Also writes the plot SVGs.

    `labs` are the labs whose solutions are compared (the report's own lab is normally one of them).
    """
    warnings = []
    entries = entries_of(labs)
    res, times, plots, sols = [], [], [], []
    files_dir = os.path.join(out_dir, FILES_DIR)
    os.makedirs(files_dir, exist_ok=True)
    for old in glob.glob(os.path.join(files_dir, "*.svg")):
        os.remove(old)

    all_checked, all_same, multi_lang = True, True, False
    for inst in instances:
        by_lab = {l["id"]: load_results(l["id"], inst) for l in labs}
        nodes = load_nodes(inst)
        res.append(f"### Instance {inst}\n\n| Method | Min | Max | Avg | Best solution checked |\n|---|--:|--:|--:|:-:|")
        plots.append(f"### Instance {inst}\n")
        times.append(time_section(inst, entries, by_lab))
        sols.append(f"### Instance {inst}\n")
        rows = []  # (method name, [min, max, avg], checked) or (method name, None, None) when not run
        for lab, m, mname in entries:
            by = by_lab[lab["id"]]
            r = pick(by.get(m["id"], {}))
            if r is None:
                warnings.append(f"no benchmark results for {inst} / {mname}")
                rows.append((mname, None, None))
                continue
            langs = by[m["id"]]
            if len(langs) > 1:
                multi_lang = True
                all_same &= all(x["stats"]["obj_min"] == r["stats"]["obj_min"] and x["best"]["tour"] == r["best"]["tour"]
                                for x in langs.values())
            all_checked &= bool(r["checked"])
            s, b = r["stats"], r["best"]
            rows.append((mname, [s["obj_min"], s["obj_max"], round(s["obj_avg"])], r["checked"]))
            name = f"{inst}_{lab['id']}_{m['id']}.svg"
            with open(os.path.join(files_dir, name), "w") as f:
                f.write(svg_plot(nodes, b["tour"]))
            plots.append(f"![{mname}: objective {b['obj']} (length {b['len']} + cost {b['cost']})]({FILES_DIR}/{name})")
            sols.append(f"**{mname}** (objective {b['obj']}):\n{', '.join(map(str, b['tour']))}\n")
        # objective is minimised, so the lowest value in each column is the best (ties are all bold)
        best = [min((v[k] for _, v, _ in rows if v), default=None) for k in range(3)]
        for mname, vals, checked in rows:
            if vals is None:
                res.append(f"| {mname} | not run | | | |")
                continue
            cells = [f"**{v}**" if v == best[k] else str(v) for k, v in enumerate(vals)]
            res.append(f"| {mname} | {' | '.join(cells)} | {'yes' if checked else '**NO**'} |")
        res.append("")
        plots.append("")

    n_runs = ", ".join(sorted({str(len(r["objs"])) for i in instances for l in labs for by in [load_results(l["id"], i)]
                               for langs in by.values() for r in langs.values()}))
    summary = (f"Objective = cycle length + sum of node costs, over {n_runs or '0'} runs per method. The best value in each column is in bold.\n"
               f"All best solutions were checked with the solution checker (the same computation as the lecturer's Solution checker.xlsx): **{'yes' if all_checked else 'NO'}**.")
    if multi_lang:
        summary += f"\nResults are identical in all implemented languages: **{'yes' if all_same else 'NO'}**."
    times_intro = ("Average time of one run of the algorithm only, over the same runs as above. "
                   "The fastest language in each row is in bold.")
    plots_intro = "Node colour and size show the node cost (dark and large = expensive). Grey dots are nodes not selected."

    base, branch = github_base()
    source_lines = []
    for lab in labs:
        rel = f"labs/{lab['id']}"
        if base:
            links = [f"[{LANG_NAMES[l]}]({base}/blob/{branch}/{rel}/{LANG_SRC[l]})" for l in LANG_ORDER if l in lab["langs"]]
            source_lines.append(f"- Lab {lab['num']}: [{rel}]({base}/tree/{branch}/{rel}) ({', '.join(links)})")
        else:
            source_lines.append(f"- Lab {lab['num']}: `{rel}/` (no GitHub remote found, so no link)")
    source = "Source code of the compared labs:\n\n" + "\n".join(source_lines)

    return {
        "results": summary + "\n\n" + "\n".join(res),
        "times": times_intro + "\n\n" + "\n".join(times),
        "plots": plots_intro + "\n\n" + "\n".join(plots),
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
    blocks, warnings = build_blocks(labs, instances, out_dir)

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
    s = re.sub(r"!\[(.*?)\]\((.*?)\)", lambda m: f'<figure><img src="{m.group(2)}" alt="{m.group(1)}"><figcaption>{m.group(1)}</figcaption></figure>', s)
    s = re.sub(r"\[(.*?)\]\((.*?)\)", r'<a href="\2">\1</a>', s)
    s = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", s)
    s = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"<i>\1</i>", s)
    s = re.sub(r"(?<![\w])_(?!\s)(.+?)(?<!\s)_(?![\w])", r"<i>\1</i>", s)
    return re.sub(r"\0(\d+)\0", lambda m: f"<code>{codes[int(m.group(1))]}</code>", s)


def markdown_to_html(md, base_dir, title):
    lines = md.splitlines()
    out, i = [], 0
    para = []
    sec = {"at": None}  # index in `out` where the current ### block started

    def close_block():
        """Wrap a ### block in a div; blocks with a table are kept on one page (figure grids may continue on the next page, row by row)."""
        if sec["at"] is not None:
            chunk = out[sec["at"]:]
            keep = any(c.startswith("<table") for c in chunk)
            out[sec["at"]:] = ['<div class="%s">' % ("blk keep" if keep else "blk") + "".join(chunk) + "</div>"]
            sec["at"] = None

    def flush():
        if para:
            if all(p.startswith("![") for p in para):
                out.append('<div class="grid%s">' % (" one" if len(para) == 1 else "") + inline("".join(para)) + "</div>")
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
        elif line.lstrip().startswith("<!--"):
            flush()
            if re.match(r"\s*<!-- auto:(results|times|plots|solutions)\b", line) and out and out[-1].startswith("<h2>"):
                out[-1] = out[-1].replace("<h2>", '<h2 class="pb">', 1)  # new page before the Results, Running time, Visualisation and Best solutions sections
            while "-->" not in lines[i]:
                i += 1
            i += 1
        elif m := re.match(r"(#{1,6})\s+(.*)", line):
            flush()
            if len(m.group(1)) <= 3:
                close_block()
            if len(m.group(1)) == 3:
                sec["at"] = len(out)
            out.append(f"<h{len(m.group(1))}>{inline(m.group(2))}</h{len(m.group(1))}>")
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
h2.pb{{break-before:page}}h3{{font-size:15px;margin:1.3em 0 .4em}}h2,h3{{break-after:avoid}}
table{{break-inside:avoid;border-collapse:collapse;margin:.5em 0}}td,th{{border:1px solid #ccc;padding:4px 9px}}th{{background:#f3f3f3;text-align:left}}
pre{{background:#f5f5f5;padding:9px;font-size:11px;white-space:pre-wrap;break-inside:avoid}}code{{background:#f0f0f0;padding:0 3px}}
.keep{{break-inside:avoid}}.grid{{display:grid;grid-template-columns:1fr 1fr;gap:10px;width:84%;margin:0 auto}}.grid.one{{grid-template-columns:1fr}}figure{{margin:0;text-align:center;font-size:11px;break-inside:avoid}}
img{{width:100%;border:1px solid #ddd}}p{{margin:.5em 0;overflow-wrap:anywhere}}a{{color:#1a55a5}}
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
