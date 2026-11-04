# {{title}}

Author: Michał Żurawski 160252

## Description of the problem

Each instance has n = 200 nodes with coordinates (x, y) and a cost. Select exactly **half of the nodes** (k = 100) and connect them into a **closed cycle** that minimises **cycle length + sum of node costs**. Distances are Euclidean, rounded to integers.

## Algorithms

_TODO: 1-2 sentences: which methods, from which start nodes, and what they share with / change from the previous lab. No run counts (the Results section gives them) and no lead-in like "One method, 200 runs:". **Bold** the key terms: the method, its neighborhood or variants, the start, and the new idea of this lab._

_Goal of this section: someone who does not know the algorithm understands it at a glance. The intro, Intuition, Core calculations and the flowchart fill the first page of the PDF (render it and check); Complexity starts the second page, Pseudocode follows it on the same page when it fits (otherwise the report moves it to the third page)._

### Intuition

_TODO: two short labelled paragraphs in plain words: no formulas, symbols, abbreviations or worked examples (Core calculations has those). Aim for 1-2 lines each._

_**Problem:** one sentence: what a simpler method (or the previous lab) wastes or misses, and why._

_**Idea:** what the algorithm does instead, in one sentence; then its gain as one counted or measured number, if one helps (e.g. "at most 100 · 10 · 2 = 2 000 instead of 14 950 moves per step"); then the price in a few words, if there is one (e.g. "The improving moves it skips cost a little quality."). Variants: one short sentence on how they differ. No exact result numbers per instance; Results and Conclusions give them._

### Core calculations

<!-- columns -->

_TODO: start with one sentence naming in **bold** what is calculated (e.g. **Δ = the change of the objective**) and how it is used. Then a **bold** label before each formula (e.g. **Node exchange**). Only the few formulas the algorithm is built on, one per line in `code`, from the basic quantity (e.g. the insertion delta Δ) to the value used to choose the next move (e.g. the score). One sentence on how that value is used. A small table of the variants and their parameters/weights. No function names or implementation details here._

<!-- column -->

_TODO: flowchart of one run, hand-made SVG in figures/ (report_files/ is regenerated), no caption: `![](figures/flowchart.svg)`. It shows only the **control flow**: the order of the steps, the loop and when it stops. The formulas, tables and text beside it explain each step, so the chart never repeats them._

- _**Shapes:** start and `return tour` as filled pills, each step a box, each condition a diamond (`|tour| < k ?`, `min Δ < 0 ?`), `yes` / `no` on the branches, one arrow back into the loop. The core calculations step is the only highlighted box (yellow)._
- _**Labels:** one short bold line per shape, 2-5 words (`random tour`, `Δ of candidate moves`, `apply it`). No subtitles, formulas, definitions, parameters, variants or case rules: those are in Core calculations._
- _**Size:** as few shapes as the loop needs (usually 5-7); copy the layout of an earlier lab's flowchart: 400 px wide, a 200 px main column, `return tour` right of its diamond, the loop back on the left._

<!-- /columns -->

<!-- pagebreak -->

### Complexity

_TODO: one table, one row per method that matters for this lab: the baseline it improves on (e.g. random solution, or the plain method it modifies), every method of this lab, and only those methods of other labs the explanation's conclusion compares against (not every algorithm included in Results). Exact method names (no "lab N"), each row on one line:_

_`| Method | Complexity per step | Δ per run |` (constructions: `Complexity per run`). The complexity is exact, not "when ..." phrases: e.g. `O(nk)`, `browse ≤ O(nk), rebuild O(nk log nk)`. Δ per run is counted, not estimated: an exact formula for deterministic constructions, otherwise the TSPA average over the runs measured by counting the Δ computations in python/solver.py._

Δ per run is the TSPA average over 200 runs.

_Then the subheading `#### Explanation`: it explains both columns of the table in one place. Under it (only if needed) one line defining the symbols (e.g. t = the number of tour nodes before the step). Then one bullet per method (or group of methods that cost the same): **bold name:** what one step computes in plain words, its size bounded by n and k → the complexity in bold exactly as in the table (e.g. **O(nk)** per step), then the exact Δ calculation with its result in one `code` span (the report glues the rest of a formula, e.g. `k → O(n)`, `t = 1 … 99`, `14 850`, with non-breaking spaces, as long as operators have a space on both sides) (e.g. `k(n − k) + k(k − 1)/2 = 14 950`; a sum with its range after it, e.g. `Σ (n − t) = 14 850` for t = 1 … 99), and how many steps a run takes. Anything else needed to read the table goes into these bullets too (e.g. that the greedy start's Δ per run includes building the start). Last bullet: the conclusion with the claim in bold (e.g. **LM computes 11× fewer Δ than the full search**)._

### Pseudocode

_TODO: one sentence on where it comes from and how the methods map onto it. Then one code block: python/solver.py simplified into pseudocode, keeping its function and variable names so it can be matched with the source, but dropping bookkeeping (used flags, tracing, instance arguments, tie handling). Top-level loop first, then the core step it calls. Short `#` comments only where a line hides a variant difference._

## Results

<!-- auto:results -->
<!-- /auto:results -->

## Running time

<!-- auto:times -->
<!-- /auto:times -->

## Best solutions (0-based node indices)

<!-- auto:solutions -->
<!-- /auto:solutions -->

## Source code

<!-- auto:source -->
<!-- /auto:source -->

## Conclusions

_TODO: 3 short bullets, one sentence each, one claim per bullet. **Bold** the claim itself (e.g. **Candidate moves cost little quality**, **2.7–5.3× faster**), not the numbers that back it:_

- _TODO: how the variants of this lab compare to each other (and in a few words why)._
- _TODO: how the best method compares to the previous labs (or to the baseline in lab 1)._
- _TODO: the running time cost._
