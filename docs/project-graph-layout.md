# Project graph layout and navigation

The default top-down graph displays **database → project → oldest loaded revision → … → current revision (HEAD)**, like a Git history viewer. The top-down direction follows real revision parent edges from ancestor to descendant; when history is paged, "oldest" means the oldest *loaded* revision, not necessarily the first revision ever committed.

**Branch names are pointers attached alongside their target revisions.** They are not ancestry nodes, and they must never move HEAD above its ancestors. Branch cards remain selectable and open the same inspectors as sidebar branch selection. Several branches can reference one HEAD without cloning that revision. The actual stored revision parent IDs are unchanged.

## Layout architecture

**Implementation:**

- `src/graph/compound_dag_layout.h` is a **Qt-independent, header-only C++17
  ranked-DAG layout kernel**. This is the reusable ordering module; it has no
  Graphviz, subprocess, network, or platform dependency.
- `src/tabs/project_graph_placement.h` converts GUI nodes into independent
  project subgraphs, measures and packs the resulting project rectangles, and
  then places complete database groups alongside one another in a single row.
- `src/tabs/project_revision_graph.cpp` renders nodes and edges from those
  coordinates. The painter does **not** decide where nodes belong.

### Ranked DAG ordering

1. Stable IDs determine the initial order, never response order or displayed branch names.
2. Revision cards alone enter the ranked-DAG kernel. Every revision depends on its actual loaded parents, so roots occupy the first rank and children descend toward HEAD. Merges join predecessor edges without cloning their revision cards.
3. Twelve alternating dependency/successor barycentric sweeps reduce crossings. Different revisions at one rank receive separate slots; a linear ancestry chain remains on one vertical centerline.
4. Branch cards live in a separate side lane at the *same vertical rank* as the revision they reference. Multiple aliases at that rank occupy independent horizontal slots. A branch whose HEAD is not loaded is still displayed and attached to its project, but no fake revision is introduced.
5. The project header is aligned over the center of its revision DAG rather than over the wider branch-label side lanes. Database headers align over their projects. Revision parent IDs and branch pointers are never rewritten for layout.

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

Every database/server card shares **one top-level Y coordinate**. Complete
server groups grow downward side by side, regardless of the depth of each
project's revision history. Server groups never wrap onto lower rows and
never share their descendants' horizontal corridors. A lone project is
centered under its slightly wider database card.

A very large number of visible servers can therefore produce a wide graph.
That is intentional for this layout contract: panning and zooming are used
to explore it instead of moving later servers below earlier histories.

The resulting trade-off is intentional: a database with very many distinct
projects is wider than a single project. Future collapsing, filtering, and
virtualization are preferable to breaking the parent/child spatial hierarchy.

### Connectors

- Database → Project: solid structural link.
- Project → oldest loaded revision root: solid structural link.
- Older revision → newer revision (HEAD): solid history edge, using immutable parent IDs.
- Revision (HEAD) → Branch: dashed lateral reference link. A branch is not another level in history.
- Project → unresolved Branch: structural fallback when the branch HEAD is not loaded.
- Folded history uses a marker between an older ancestor and its newer descendant; when no ancestor is visible, the project connects to the marker and the marker connects to the first visible revision.
- Straight vertical chains are centered; merges/forks use smooth connecting paths.

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

- `tests/project_revision_graph_test.cpp` validates oldest-to-HEAD
  chronological ordering, branch labels alongside the referenced revision,
  shared-HEAD aliases and unchanged revision ancestry semantics.
- `tests/compound_dag_layout_test.cpp` runs the standalone layout kernel
  with **no Qt dependency**: chronological chains, fan-out, merge ranks,
  deterministic input order and corrupt cycles.
- `tests/project_revision_graph_test.cpp` checks full GUI placement: the
  exact reported **two projects under one database, one shared HEAD per
  project, three and two branches** shape, corridor separation, sibling
  ranks, selection, and navigation. It also covers a common server-card layer across uneven histories,
  viewport-independent placement, overlapping-card detection, and stable
  placement after shuffled input.

Run `./compile_linux_tests.sh`. The isolated kernel can additionally be
compiled with a plain C++17 compiler, independent of the GUI:

```bash
g++ -std=c++17 -Isrc tests/compound_dag_layout_test.cpp -o /tmp/aowis-dag-test && /tmp/aowis-dag-test
```
