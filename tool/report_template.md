# {{title}}

Author: Michał Żurawski 160252

## Description of the problem

Each instance contains nodes with coordinates (x, y) and a cost. Select exactly half of the nodes (rounded up) and connect them into a closed cycle. Minimise **cycle length + sum of the costs of the selected nodes**. Distances are Euclidean, rounded to the nearest integer.

## Algorithms

_TODO: 1-2 sentences: which methods, how many runs each and from which start nodes, and what they share with / change from the previous lab._

_Goal of this section: someone who does not know the algorithm understands it at a glance. Keep the intro, Intuition, Core calculations and the flowchart on the first page of the PDF._

### Intuition

_TODO: one short paragraph in plain words, no formulas: what weakness of a simpler method this algorithm fixes and why its idea helps. Then a tiny numeric example where the simpler method and this one make different choices. One sentence on how the variants differ._

### Core calculations

<!-- columns -->
_TODO: only the few formulas the algorithm is built on, one per line in `code`, from the basic quantity (e.g. the insertion delta Δ) to the value used to choose the next move (e.g. the score). One sentence on how that value is used. A small table of the variants and their parameters/weights. No function names or implementation details here._
<!-- column -->
_TODO: flowchart of one run, hand-made SVG in figures/ (report_files/ is regenerated), no caption: `![](figures/flowchart.svg)`. Start, loop condition, one box per step, the core calculations box highlighted, arrow back to the loop._
<!-- /columns -->

<!-- pagebreak -->
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

_TODO: 3 short bullets, one sentence each, one claim per bullet:_

- _TODO: how the variants of this lab compare to each other (and in a few words why)._
- _TODO: how the best method compares to the previous labs (or to the baseline in lab 1)._
- _TODO: the running time cost._
