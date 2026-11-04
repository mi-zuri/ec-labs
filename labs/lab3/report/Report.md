# Lab 3 - Local search

Author: Michał Żurawski 160252

## Description of the problem

Each instance has n = 200 nodes with coordinates (x, y) and a cost. Select exactly **half of the nodes** (k = 100) and connect them into a **closed cycle** that minimises **cycle length + sum of node costs**. Distances are Euclidean, rounded to integers.

## Algorithms

Eight **local search** methods: **steepest or greedy** × **nodes or edges exchange** inside the tour × **random start** (run i uses seed 42 + i) or **greedy start** (lab 2's best heuristic, greedy cycle with 2-regret + best change, run i starts from node i). Instead of building a tour, they take a complete one and keep improving it with **small moves**.

### Intuition

**Problem:** a construction heuristic never takes a decision back: edges may end up crossing, or a cheap unused node could replace an expensive one.

**Idea:** take a complete tour and keep applying small changes that lower the objective until none helps: swap a tour node for an unused one, and swap two tour nodes or reverse a part of the tour; this makes random tours 3.5–4 times better. **Steepest** applies the best change of the whole neighborhood, **greedy** the first improving one in random order.

### Core calculations

<!-- columns -->
For every move we calculate **Δ = the change of the objective**, from the changed edges and costs only; p(x), s(x) = predecessor, successor of x.

**Inter-route**, selected u ↔ unselected v:

`Δ = c(v) − c(u) + d(p,v) + d(v,s) − d(p,u) − d(u,s)`

**Nodes exchange** a ↔ b (for neighbours only the 2 outer edges change):

`Δ = d(pa,b) + d(b,sa) + d(pb,a) + d(a,sb)`
`  − d(pa,a) − d(a,sa) − d(pb,b) − d(b,sb)`

**Edges exchange** (a,b), (c,d) → (a,c), (b,d), path b … c reversed:

`Δ = d(a,c) + d(b,d) − d(a,b) − d(c,d)`

A move is applied **only if Δ < 0**; 14 950 moves per step (n = 200, k = 100).

| Method | picks | intra-route |
|---|---|---|
| Steepest | min Δ of all | nodes / edges |
| Greedy | first Δ < 0 | nodes / edges |
<!-- column -->
![](figures/local_search_flowchart.svg)
<!-- /columns -->

<!-- pagebreak -->
### Complexity

| Method | Complexity per step | Δ per run |
|---|---|--:|
| Steepest LS, nodes exchange, random start | O(nk) | 2 350 663 |
| Steepest LS, edges exchange, random start | O(nk) | 2 003 300 |
| Greedy LS, nodes exchange, random start | ≤ O(nk) | 141 558 |
| Greedy LS, edges exchange, random start | ≤ O(nk) | 96 941 |
| Steepest LS, nodes exchange, greedy start | O(nk), start O(nk²) | 726 309 |
| Steepest LS, edges exchange, greedy start | O(nk), start O(nk²) | 726 159 |
| Greedy LS, nodes exchange, greedy start | ≤ O(nk), start O(nk²) | 694 710 |
| Greedy LS, edges exchange, greedy start | ≤ O(nk), start O(nk²) | 694 983 |

Δ per run is the TSPA average over 200 runs.

#### Explanation

- **Steepest LS:** every step computes the Δ of every move, k(n − k) inter-route + k(k − 1)/2 intra-route ≤ nk → **O(nk)** per step: `k(n − k) + k(k − 1)/2 = 14 950`; 134–157 steps per run from random starts.
- **Greedy LS:** a step computes Δ in random move order until the first improving move, at most all 14 950 → **≤ O(nk)** per step; on average 230–340 Δ per step and about 415 steps per run from random starts.
- **Greedy start:** built once by lab 2's greedy cycle with 2-regret + best change → **start O(nk²)**: `661 650` Δ, counted in Δ per run. That tour is almost a local optimum, so LS then takes only 3–5 steps (33 000–65 000 Δ).
- So **greedy LS computes 17–21× fewer Δ than steepest LS** from random starts, and from greedy starts the construction costs **10–20× more Δ than the local search** itself.

### Pseudocode

Simplified from `python/solver.py`. All eight methods run `local_search`; they differ in the starting tour (`random_start` / `greedy_start`), in `intra_kind` (`SWAP_NODES` / `SWAP_EDGES`) and in the function that picks the move.

```
def local_search(tour, unselected, search, intra_kind):
    moves = all_moves(k, n - k, intra_kind)    # inter-route + intra-route moves
    while True:
        if search == STEEPEST:
            move = find_best_move(tour, unselected, moves)
        else:
            move = find_first_improving_move(tour, unselected, moves, rng)
        if move is None:
            return tour                         # local optimum
        apply_move(tour, unselected, move)

def find_best_move(tour, unselected, moves):
    best_move, best_delta = None, 0
    for move in moves:
        delta = move_delta(tour, unselected, move)
        if delta < best_delta:
            best_move, best_delta = move, delta
    return best_move

def find_first_improving_move(tour, unselected, moves, rng):
    for tried in range(len(moves)):
        pick = tried + rng.below(len(moves) - tried)
        moves[tried], moves[pick] = moves[pick], moves[tried]
        if move_delta(tour, unselected, moves[tried]) < 0:
            return moves[tried]
    return None

def apply_move(tour, unselected, move):
    kind, i, j = move
    if kind == EXCHANGE_SELECTED: swap tour[i] and unselected[j]
    if kind == SWAP_NODES:        swap tour[i] and tour[j]
    if kind == SWAP_EDGES:        reverse tour[i+1 .. j]
```

**Randomization of the greedy version.** A move is a triple (kind, i, j) of positions, and all moves of both kinds are kept in a single list. Each step browses that list with a lazy Fisher-Yates shuffle: the next move is drawn uniformly from the moves not yet tried in this step, and only that move is evaluated. So every order of the whole neighborhood is equally likely, inter- and intra-route moves are fully mixed, and the step stops at the first improving move, so Δ is never computed for the moves it has not reached.

## Results

<!-- auto:results (generated: edits between these markers are overwritten) -->
Objective = cycle length + node costs, 200 runs per method. Cells: av (min – max), best of each per instance in bold.
All best solutions were checked with the solution checker: **yes**.
Results are identical in all implemented languages: **yes**.

| Method | TSPA | TSPB |
|---|:-:|:-:|
| Steepest LS, nodes exchange, random start | 89742 (81937 – 96386) | 66055 (59059 – 74933) |
| Steepest LS, edges exchange, random start | 76225 (74248 – 79388) | **52461** (50265 – **55173**) |
| Greedy LS, nodes exchange, random start | 87889 (81686 – 95320) | 63656 (56270 – 70172) |
| Greedy LS, edges exchange, random start | 76265 (73545 – 78428) | 52913 (**49955** – 55677) |
| Steepest LS, nodes exchange, greedy start | 73018 (**71955** – **74682**) | 57376 (52778 – 63043) |
| Steepest LS, edges exchange, greedy start | 72997 (**71955** – **74682**) | 57019 (52566 – 62301) |
| Greedy LS, nodes exchange, greedy start | 73015 (**71955** – **74682**) | 57379 (52672 – 63043) |
| Greedy LS, edges exchange, greedy start | **72971** (**71955** – **74682**) | 57067 (52672 – 62500) |

### Visualisation (Instance TSPA)

![Steepest LS, nodes exchange, random start<br>**81937** (length 30984 + cost 50953)](report_files/TSPA_lab3_ls_steepest_nodes_random.svg)
![Steepest LS, edges exchange, random start<br>**74248** (length 22259 + cost 51989)](report_files/TSPA_lab3_ls_steepest_edges_random.svg)
![Greedy LS, nodes exchange, random start<br>**81686** (length 30012 + cost 51674)](report_files/TSPA_lab3_ls_greedy_nodes_random.svg)
![Greedy LS, edges exchange, random start<br>**73545** (length 22205 + cost 51340)](report_files/TSPA_lab3_ls_greedy_edges_random.svg)
![Steepest LS, nodes exchange, greedy start<br>**71955** (length 21123 + cost 50832)](report_files/TSPA_lab3_ls_steepest_nodes_greedy.svg)
![Steepest LS, edges exchange, greedy start<br>**71955** (length 21123 + cost 50832)](report_files/TSPA_lab3_ls_steepest_edges_greedy.svg)
![Greedy LS, nodes exchange, greedy start<br>**71955** (length 21123 + cost 50832)](report_files/TSPA_lab3_ls_greedy_nodes_greedy.svg)
![Greedy LS, edges exchange, greedy start<br>**71955** (length 21123 + cost 50832)](report_files/TSPA_lab3_ls_greedy_edges_greedy.svg)

### Visualisation (Instance TSPB)

![Steepest LS, nodes exchange, random start<br>**59059** (length 28949 + cost 30110)](report_files/TSPB_lab3_ls_steepest_nodes_random.svg)
![Steepest LS, edges exchange, random start<br>**50265** (length 19741 + cost 30524)](report_files/TSPB_lab3_ls_steepest_edges_random.svg)
![Greedy LS, nodes exchange, random start<br>**56270** (length 23242 + cost 33028)](report_files/TSPB_lab3_ls_greedy_nodes_random.svg)
![Greedy LS, edges exchange, random start<br>**49955** (length 20215 + cost 29740)](report_files/TSPB_lab3_ls_greedy_edges_random.svg)
![Steepest LS, nodes exchange, greedy start<br>**52778** (length 16865 + cost 35913)](report_files/TSPB_lab3_ls_steepest_nodes_greedy.svg)
![Steepest LS, edges exchange, greedy start<br>**52566** (length 17522 + cost 35044)](report_files/TSPB_lab3_ls_steepest_edges_greedy.svg)
![Greedy LS, nodes exchange, greedy start<br>**52672** (length 17168 + cost 35504)](report_files/TSPB_lab3_ls_greedy_nodes_greedy.svg)
![Greedy LS, edges exchange, greedy start<br>**52672** (length 17168 + cost 35504)](report_files/TSPB_lab3_ls_greedy_edges_greedy.svg)
<!-- /auto:results -->

## Running time

<!-- auto:times (generated: edits between these markers are overwritten) -->
Average time of one run **in ms** (algorithm only, same runs as above); the fastest language per row is in bold.

### Instance TSPA

| Method | Python | C | C++ | Rust |
|---|--:|--:|--:|--:|
| Steepest LS, nodes exchange, random start | 336.95 | 4.69 | **4.31** | 6.62 |
| Steepest LS, edges exchange, random start | 255.24 | 3.48 | **3.38** | 4.13 |
| Greedy LS, nodes exchange, random start | 70.59 | 1.29 | **1.26** | 1.37 |
| Greedy LS, edges exchange, random start | 44.41 | 0.86 | **0.81** | 0.87 |
| Steepest LS, nodes exchange, greedy start | 45.03 | 0.91 | **0.89** | 0.96 |
| Steepest LS, edges exchange, greedy start | 43.54 | 0.89 | **0.87** | 0.95 |
| Greedy LS, nodes exchange, greedy start | 52.27 | 1.08 | **1.05** | 1.13 |
| Greedy LS, edges exchange, greedy start | 50.53 | 1.06 | **1.04** | 1.11 |

### Pareto plot (Instance TSPA)

![Pareto plot, instance TSPA](report_files/TSPA_pareto.svg)

### Instance TSPB

| Method | Python | C | C++ | Rust |
|---|--:|--:|--:|--:|
| Steepest LS, nodes exchange, random start | 329.26 | 4.57 | **4.37** | 5.46 |
| Steepest LS, edges exchange, random start | 294.92 | 3.54 | **3.44** | 4.28 |
| Greedy LS, nodes exchange, random start | 74.65 | 1.34 | **1.27** | 1.29 |
| Greedy LS, edges exchange, random start | 48.77 | **0.83** | 0.87 | 0.94 |
| Steepest LS, nodes exchange, greedy start | 52.11 | 1.05 | **1.00** | 1.10 |
| Steepest LS, edges exchange, greedy start | 59.17 | 1.12 | **1.04** | 1.16 |
| Greedy LS, nodes exchange, greedy start | 57.89 | 1.17 | **1.12** | 1.23 |
| Greedy LS, edges exchange, greedy start | 59.31 | 1.21 | **1.18** | 1.26 |

### Pareto plot (Instance TSPB)

![Pareto plot, instance TSPB](report_files/TSPB_pareto.svg)
<!-- /auto:times -->

## Best solutions (0-based node indices)

<!-- auto:solutions (generated: edits between these markers are overwritten) -->
### Instance TSPA

**Steepest LS, nodes exchange, random start**
152, 49, 149, 63, 168, 97, 127, 171, 2, 187, 30, 160, 123, 83, 141, 180, 28, 156, 102, 87, 98, 114, 181, 58, 99, 131, 72, 151, 57, 182, 192, 158, 150, 106, 113, 89, 42, 134, 84, 135, 73, 45, 185, 35, 67, 144, 52, 33, 54, 9, 136, 163, 29, 101, 147, 47, 130, 159, 153, 78, 88, 107, 176, 16, 51, 139, 112, 4, 91, 80, 48, 31, 116, 60, 108, 38, 71, 157, 115, 39, 186, 184, 178, 164, 8, 142, 85, 1, 169, 3, 50, 93, 174, 36, 177, 179, 53, 140, 76, 132

**Steepest LS, edges exchange, random start**
108, 60, 31, 48, 80, 38, 91, 157, 71, 4, 76, 132, 90, 123, 30, 180, 35, 51, 16, 67, 144, 140, 53, 179, 52, 177, 36, 99, 131, 33, 54, 9, 164, 8, 174, 176, 150, 106, 43, 85, 3, 50, 169, 1, 178, 184, 136, 29, 101, 147, 47, 93, 130, 159, 146, 170, 75, 153, 193, 78, 88, 107, 113, 89, 42, 151, 57, 182, 111, 138, 72, 134, 84, 58, 158, 192, 181, 114, 135, 61, 73, 45, 185, 28, 87, 141, 187, 2, 171, 92, 69, 127, 97, 168, 116, 149, 63, 11, 49, 152

**Greedy LS, nodes exchange, random start**
174, 87, 160, 83, 123, 152, 108, 49, 60, 80, 91, 157, 71, 4, 76, 100, 90, 132, 139, 112, 115, 186, 136, 184, 1, 50, 93, 106, 89, 42, 150, 176, 131, 36, 177, 179, 52, 144, 67, 185, 35, 16, 51, 156, 28, 180, 30, 161, 11, 63, 149, 116, 168, 97, 127, 69, 171, 2, 187, 141, 114, 181, 134, 72, 151, 107, 153, 193, 78, 88, 57, 182, 138, 158, 192, 58, 84, 135, 73, 45, 140, 53, 99, 8, 142, 85, 3, 164, 9, 54, 33, 143, 62, 178, 29, 101, 147, 47, 159, 130

**Greedy LS, edges exchange, random start**
60, 116, 149, 63, 168, 49, 108, 152, 100, 90, 123, 83, 160, 97, 95, 127, 69, 129, 104, 92, 171, 2, 187, 30, 141, 180, 28, 87, 102, 185, 67, 144, 52, 143, 33, 9, 54, 131, 99, 36, 177, 179, 53, 140, 45, 73, 135, 114, 181, 192, 158, 58, 84, 134, 72, 176, 174, 8, 164, 85, 150, 106, 113, 89, 42, 151, 182, 57, 107, 88, 78, 193, 153, 170, 146, 159, 130, 47, 93, 50, 3, 1, 178, 136, 186, 39, 115, 112, 51, 16, 35, 156, 132, 76, 71, 157, 91, 80, 48, 31

**Steepest LS, nodes exchange, greedy start**
169, 3, 50, 93, 85, 174, 8, 54, 9, 33, 131, 36, 177, 52, 179, 53, 140, 144, 67, 16, 51, 35, 185, 102, 28, 180, 132, 76, 4, 71, 157, 91, 80, 48, 31, 108, 152, 100, 123, 83, 160, 49, 60, 116, 149, 63, 168, 97, 127, 161, 30, 187, 171, 2, 189, 141, 87, 45, 73, 135, 114, 181, 192, 158, 58, 84, 134, 72, 176, 150, 106, 113, 89, 42, 151, 138, 182, 57, 64, 107, 88, 78, 193, 153, 170, 146, 159, 130, 47, 37, 147, 101, 55, 29, 178, 184, 136, 186, 164, 1

**Steepest LS, edges exchange, greedy start**
169, 3, 50, 93, 85, 174, 8, 54, 9, 33, 131, 36, 177, 52, 179, 53, 140, 144, 67, 16, 51, 35, 185, 102, 28, 180, 132, 76, 4, 71, 157, 91, 80, 48, 31, 108, 152, 100, 123, 83, 160, 49, 60, 116, 149, 63, 168, 97, 127, 161, 30, 187, 171, 2, 189, 141, 87, 45, 73, 135, 114, 181, 192, 158, 58, 84, 134, 72, 176, 150, 106, 113, 89, 42, 151, 138, 182, 57, 64, 107, 88, 78, 193, 153, 170, 146, 159, 130, 47, 37, 147, 101, 55, 29, 178, 184, 136, 186, 164, 1

**Greedy LS, nodes exchange, greedy start**
169, 3, 50, 93, 85, 174, 8, 54, 9, 33, 131, 36, 177, 52, 179, 53, 140, 144, 67, 16, 51, 35, 185, 102, 28, 180, 132, 76, 4, 71, 157, 91, 80, 48, 31, 108, 152, 100, 123, 83, 160, 49, 60, 116, 149, 63, 168, 97, 127, 161, 30, 187, 171, 2, 189, 141, 87, 45, 73, 135, 114, 181, 192, 158, 58, 84, 134, 72, 176, 150, 106, 113, 89, 42, 151, 138, 182, 57, 64, 107, 88, 78, 193, 153, 170, 146, 159, 130, 47, 37, 147, 101, 55, 29, 178, 184, 136, 186, 164, 1

**Greedy LS, edges exchange, greedy start**
169, 3, 50, 93, 85, 174, 8, 54, 9, 33, 131, 36, 177, 52, 179, 53, 140, 144, 67, 16, 51, 35, 185, 102, 28, 180, 132, 76, 4, 71, 157, 91, 80, 48, 31, 108, 152, 100, 123, 83, 160, 49, 60, 116, 149, 63, 168, 97, 127, 161, 30, 187, 171, 2, 189, 141, 87, 45, 73, 135, 114, 181, 192, 158, 58, 84, 134, 72, 176, 150, 106, 113, 89, 42, 151, 138, 182, 57, 64, 107, 88, 78, 193, 153, 170, 146, 159, 130, 47, 37, 147, 101, 55, 29, 178, 184, 136, 186, 164, 1

### Instance TSPB

**Steepest LS, nodes exchange, random start**
154, 140, 21, 133, 136, 151, 98, 88, 99, 171, 74, 20, 2, 14, 153, 90, 129, 127, 80, 85, 45, 134, 175, 174, 1, 132, 29, 197, 6, 180, 115, 170, 155, 71, 60, 40, 64, 69, 160, 8, 117, 104, 54, 106, 192, 159, 162, 76, 135, 172, 183, 30, 130, 191, 38, 47, 59, 73, 167, 51, 82, 87, 179, 67, 66, 10, 114, 196, 105, 94, 81, 187, 70, 190, 57, 89, 72, 46, 86, 3, 184, 96, 198, 121, 120, 24, 34, 152, 181, 97, 158, 149, 28, 145, 65, 178, 103, 177, 125, 26

**Steepest LS, edges exchange, random start**
183, 130, 30, 21, 38, 47, 140, 154, 97, 71, 40, 60, 157, 45, 134, 175, 36, 197, 29, 132, 178, 103, 6, 115, 170, 125, 177, 158, 145, 28, 149, 37, 116, 147, 42, 50, 121, 198, 96, 120, 72, 46, 89, 57, 185, 76, 190, 70, 81, 94, 105, 61, 139, 196, 114, 148, 10, 78, 54, 162, 192, 80, 127, 129, 90, 52, 182, 153, 20, 14, 2, 74, 17, 171, 39, 99, 31, 67, 24, 34, 181, 179, 87, 82, 51, 85, 59, 73, 167, 98, 151, 122, 3, 184, 117, 133, 136, 172, 160, 69

**Greedy LS, nodes exchange, random start**
191, 30, 176, 38, 155, 71, 60, 157, 45, 134, 175, 145, 28, 65, 178, 132, 29, 6, 115, 170, 177, 125, 26, 97, 154, 140, 33, 64, 73, 59, 85, 51, 82, 87, 179, 181, 34, 24, 25, 98, 27, 111, 151, 69, 189, 183, 21, 47, 141, 40, 167, 152, 67, 99, 39, 171, 2, 14, 20, 74, 153, 182, 80, 52, 90, 129, 127, 192, 76, 86, 46, 72, 156, 96, 121, 198, 48, 89, 57, 196, 114, 148, 10, 78, 54, 162, 104, 185, 3, 184, 173, 122, 160, 172, 136, 133, 8, 117, 120, 130

**Greedy LS, edges exchange, random start**
151, 122, 184, 3, 86, 46, 72, 117, 120, 133, 136, 172, 160, 69, 64, 40, 71, 60, 157, 45, 134, 175, 197, 6, 29, 132, 178, 65, 145, 28, 149, 146, 37, 116, 158, 177, 125, 170, 115, 97, 154, 140, 47, 38, 176, 21, 183, 30, 130, 121, 198, 96, 48, 156, 32, 89, 57, 185, 76, 162, 192, 54, 190, 70, 187, 196, 114, 148, 10, 90, 129, 127, 80, 182, 153, 118, 110, 93, 2, 14, 20, 74, 17, 171, 99, 31, 67, 24, 34, 152, 181, 179, 87, 82, 167, 51, 85, 59, 73, 98

**Steepest LS, nodes exchange, greedy start**
97, 155, 71, 175, 134, 45, 157, 60, 141, 40, 33, 64, 4, 124, 69, 160, 172, 136, 133, 120, 8, 117, 72, 46, 86, 3, 184, 173, 122, 151, 98, 73, 59, 85, 51, 167, 82, 87, 179, 88, 126, 181, 152, 34, 24, 67, 66, 11, 27, 135, 76, 162, 192, 80, 20, 74, 2, 14, 153, 90, 129, 127, 54, 104, 185, 79, 57, 89, 32, 156, 43, 48, 96, 198, 121, 63, 130, 191, 30, 183, 21, 38, 47, 140, 154, 26, 125, 145, 28, 65, 178, 103, 132, 29, 36, 197, 6, 180, 115, 170

**Steepest LS, edges exchange, greedy start**
39, 99, 163, 88, 87, 82, 179, 51, 85, 59, 73, 167, 181, 152, 34, 24, 31, 67, 66, 27, 111, 98, 151, 122, 69, 160, 172, 136, 133, 168, 189, 21, 64, 33, 40, 60, 157, 45, 134, 175, 71, 155, 97, 36, 197, 132, 29, 6, 180, 115, 170, 177, 125, 26, 154, 140, 47, 38, 30, 183, 191, 130, 116, 147, 121, 198, 96, 48, 120, 8, 117, 72, 46, 86, 184, 3, 135, 76, 185, 79, 89, 57, 77, 104, 162, 54, 192, 80, 127, 129, 90, 52, 182, 153, 14, 2, 20, 74, 17, 171

**Greedy LS, nodes exchange, greedy start**
125, 155, 71, 175, 134, 45, 157, 60, 141, 40, 33, 64, 4, 124, 69, 160, 172, 136, 133, 120, 8, 117, 72, 46, 86, 3, 184, 173, 122, 151, 98, 73, 59, 85, 51, 167, 82, 87, 179, 88, 126, 181, 152, 34, 24, 67, 66, 11, 27, 135, 76, 162, 192, 80, 20, 74, 2, 14, 153, 90, 129, 127, 54, 104, 185, 79, 57, 89, 32, 156, 43, 48, 96, 198, 121, 63, 130, 191, 30, 183, 21, 38, 47, 140, 154, 26, 158, 149, 28, 65, 178, 103, 132, 29, 36, 197, 6, 180, 115, 170

**Greedy LS, edges exchange, greedy start**
125, 155, 71, 175, 134, 45, 157, 60, 141, 40, 33, 64, 4, 124, 69, 160, 172, 136, 133, 120, 8, 117, 72, 46, 86, 3, 184, 173, 122, 151, 98, 73, 59, 85, 51, 167, 82, 87, 179, 88, 126, 181, 152, 34, 24, 67, 66, 11, 27, 135, 76, 162, 192, 80, 20, 74, 2, 14, 153, 90, 129, 127, 54, 104, 185, 79, 57, 89, 32, 156, 43, 48, 96, 198, 121, 63, 130, 191, 30, 183, 21, 38, 47, 140, 154, 26, 158, 149, 28, 65, 178, 103, 132, 29, 36, 197, 6, 180, 115, 170
<!-- /auto:solutions -->

## Source code

<!-- auto:source (generated: edits between these markers are overwritten) -->
[labs/lab3](https://github.com/mi-zuri/ec-labs/tree/main/labs/lab3) ([Python](https://github.com/mi-zuri/ec-labs/blob/main/labs/lab3/python/solver.py), [C](https://github.com/mi-zuri/ec-labs/blob/main/labs/lab3/c/solver.c), [C++](https://github.com/mi-zuri/ec-labs/blob/main/labs/lab3/cpp/solver.cpp), [Rust](https://github.com/mi-zuri/ec-labs/blob/main/labs/lab3/rust/solver.rs))
<!-- /auto:source -->

## Conclusions

- **Local search beats every earlier method**: the best average drops from 73 350 to 72 971 on TSPA and from 55 698 to 52 461 on TSPB.
- **Edges exchange is much better than nodes exchange** from random starts (average TSPA 76 225 vs 89 742, TSPB 52 461 vs 66 055).
- From greedy starts all four variants end **within 1 % of each other**, since the start is already close to a local optimum.
- Greedy LS reaches the same quality as steepest LS **about 4× faster** from random starts.
- **Two results break the expected pattern.** On TSPB random starts beat greedy starts (52 461 vs 57 019): the construction builds a short tour through expensive nodes, and local search cannot move it to a cheaper region. From greedy starts greedy LS is slower than steepest LS: improving moves are rare near a local optimum, so greedy browses most of the neighborhood anyway and pays for a random draw on every move.
