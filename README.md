# EC Labs: solver runner

The same problem in every lab: choose half of the nodes (rounded up) and form a cycle through them,
minimising cycle length + sum of node costs. Each lab uses a different method, implemented in
Python, C, C++ and Rust. The tool in `tool/` lets you run any solver from the browser,
watch the solution being built and compare languages.

## Start

```sh
python3 tool/server.py            # or: python3 tool/server.py --port 8001
```

Open <http://127.0.0.1:8000>.

- **Run tab**: pick lab, instance, method and language.
  - **Trace**: one run, every step is streamed and animated. Use play/pause, step (← →),
    the slider and the speed setting. Click a node to start from it.
  - **Bench**: all runs (200). Shows live statistics, objective per run and the best tour.
    The time per run is the algorithm only. Results are saved in `out/results/`.
- **Compare tab**: min/avg/max per method × language, time per run, speed-up vs Python,
  whether all languages give identical results, best tour per method.
  "Run missing benchmarks" fills the table. "Generate report" builds the lab report (see Reports).

Compiled languages are rebuilt automatically when their source changes (`out/build/`).
Every solution returned is checked by an independent checker in the server.

## Layout

```text
course/                   course material (problem description)
data/                     instances (*.csv): every file here appears in the tool
labs/labN/                one folder per lab
  ├── Assignment N.pdf
  ├── lab.json            lab title + method list (this is how the tool finds the lab)
  └── python/ c/ cpp/ rust/   solver.* : only the algorithms of this lab
common/                   code shared by all labs
  ├── PROTOCOL.md         the contract every solver follows (CLI, output, RNG, tie-breaking)
  └── python/ c/ cpp/ rust/   instance + distances, RNG, timing, output, run loop
tool/                     the app: server.py (compiles, runs, streams, checks, saves) + viewer/ (web page)
out/                      generated, safe to delete
  ├── build/labN/<lang>/solver                     compiled programs
  └── results/labN/<instance>/<method>/<lang>.json saved benchmarks
```

## Add a lab

1. Create `labs/lab2/` and put the assignment PDF in it.
2. Write `labs/lab2/lab.json`:

   ```json
   {
     "title": "Local search",
     "methods": [
       { "id": "ls_greedy", "name": "Greedy local search", "shape": "cycle" }
     ]
   }
   ```

3. Copy `labs/lab1/<lang>/solver.*` to `labs/lab2/<lang>/`, replace the algorithms,
   keep `solve` and `runs_for`. Languages without a solver file are disabled for that lab.
4. Reload the page. Details in `common/PROTOCOL.md`.

## Reports

Compare tab → **Generate report** creates `labs/labN/report/Report.md` and `ReportLabN_MichałŻurawski.pdf` (plots in `report_files/`) (all instances, from the saved
benchmarks); the button then becomes **Regenerate report**. Edit `Report.md` by hand, then regenerate:
text outside the `<!-- auto:... -->` blocks is kept, while results, visualisations, best-solution lists and the
GitHub source link (taken from the git remote) are rebuilt every time (a deleted block is re-added).
First-time text comes from `tool/report_template.md`; write the algorithms and conclusions by hand in `Report.md`.
PDF export needs Chrome/Chromium (set `CHROME` if it is not found). CLI: `python3 tool/report.py lab1`.

Run a solver by hand (from the project root):

```sh
python3 labs/lab1/python/solver.py --instance data/TSPA.csv --method nn_any --mode trace --start 0
```
