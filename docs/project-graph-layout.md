# Project graph layout and navigation

The graph displays **database → project → revision DAG → branch labels**.
Structural ownership and branch-to-HEAD links are not revision parents. Only
persisted revision parent IDs define the immutable ancestry topology.

## Layout architecture

**Implementation:**

- `src/graph/compound_dag_layout.h` is a **Qt-independent, header-only C++17
  ranked-DAG layout kernel**. This is the reusable ordering module; it has no
  Graphviz, subprocess, network, or platform dependency.
- `src/tabs/project_graph_placement.h` converts GUI nodes into independent
  project subgraphs, measures and packs the resulting project rectangles, and
  then packs complete database groups into rows.
- `src/tabs/project_revision_graph.cpp` renders nodes and edges from those
  coordinates. The painter does **not** decide where nodes belong.

### Ranked DAG ordering

1. Stable IDs determine the initial order, never response order or displayed
   branch names.
2. Revision ranks are computed from loaded parent relationships. Merges appear
   strictly below all of their loaded parents. Malformed cyclic ancestry
   cannot recurse indefinitely.
3. Branch cards are ordinary *layout leaves* attached to their loaded HEAD.
   This gives a branch its own click target without adding a bogus revision
   ancestry edge. A branch with no loaded HEAD occupies a fallback last rank.
4. Twelve alternating parent/child barycentric sweeps reduce crossings.
   Different nodes at one rank always receive separate non-overlapping slots.
   Straight single-node chains remain on one vertical centerline.
5. Node coordinates are centered in a measured rectangle that encloses the
   entire ranked project DAG. One project is laid out without knowing about
   its neighbours.

This is a deterministic, bounded-space layered layout, **not** a guarantee of
an absolute global minimum crossing count (that problem is computationally
hard). Future improvements can replace the ordering kernel without changing
GUI selection, project ownership, or rendering.

### Compound packing: the critical invariant

**All projects of one database are siblings on one common vertical layer.**
Their complete revision/branch subgraphs are packed into **non-overlapping
horizontal corridors**. A project's descendants can never occupy the space
between another project's project card and its revision/branch descendants.

This replaces the previous *per-project shelf wrapping*, which routed a
server-to-project connector around an unrelated project and its branches.
The old reserved-gutter routing code has been removed.

For many databases, **complete database groups** can wrap into different
rows. Database groups never share their descendants' rectangles. A lone
project is centered under its slightly wider database card.

The resulting trade-off is intentional: a database with very many distinct
projects is wider than a single project. Future collapsing, filtering, and
virtualization are preferable to breaking the parent/child spatial hierarchy.

### Connectors

- Database → Project: solid structural link, now drawn within the one project
  sibling layer instead of through another project's descendants.
- Project → root revision: solid structural link.
- Revision → revision: ancestry edge from actual immutable parent IDs.
- Revision HEAD → Branch: dashed label link, **not** revision ancestry.
- Aligned pairs use a straight line; other pairs use smooth curves.

## Selection and navigation

Database, project, revision and branch nodes have stable identities and are
clickable. Graph clicks select the corresponding sidebar item and inspector;
sidebar selection highlights the corresponding graph node. Branch labels and
revision badges use stable branch IDs rather than relying on names.

The camera clamps panning, inertia and zoom against the graph's rendered
bounding box plus a sensible viewport margin. A graph smaller than the
viewport is centered instead of drifting into empty space.

The top-down/bottom-up orientation only changes the presentation of node
coordinates; it does not alter immutable parents or card hit testing.
Heads-only mode folds revision history without removing project/database
ownership and branch labels.

## Regression coverage

- `tests/compound_dag_layout_test.cpp` runs the standalone layout kernel
  with **no Qt dependency**: chains, branch fan-out, merge ranks,
  deterministic input order and corrupt cycles.
- `tests/project_revision_graph_test.cpp` checks full GUI placement: the
  exact reported **two projects under one database, one shared HEAD per
  project, three and two branches** shape, corridor separation, sibling
  ranks, selection, and navigation. It also covers multiple database rows,
  overlapping-card detection, and stable placement after shuffled input.

Run `./compile_linux_tests.sh`. The isolated kernel can additionally be
compiled with a plain C++17 compiler, independent of the GUI:

```bash
g++ -std=c++17 -Isrc tests/compound_dag_layout_test.cpp -o /tmp/aowis-dag-test && /tmp/aowis-dag-test
```
