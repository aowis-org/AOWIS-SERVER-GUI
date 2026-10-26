# Project graph: revision DAG, branch HEAD labels and camera

**Implementation snapshot: 2026-10-27.** The Projects graph follows DVMDB's
`AOWIS-SERVER-DVMDB/docs/version-control-semantics.md`
when that repository is checked out alongside the GUI. The DVMDB document is
the product-level contract; this document specifies the currently implemented
GUI presentation. It does **not** imply a complete all-parent DVMDB graph API
has shipped.

## Visible hierarchy

```text
Server A                 Server B             GUI-only source containers
    |                        |
 Project X                Project Y           DVMDB repositories
    |                        |
    R0                       R0                oldest loaded revisions
    |                        |
    R1                       R1 [main · HEAD]   user-named branch reference
   /  
  R2  F2 [feature · HEAD]
  |
  R3 [main · HEAD]
```

Every revision card represents **one immutable DVMDB revision**. Its `parents`
are revision-to-revision ancestry edges. A branch is a movable, user-named
reference to exactly one revision, so `main · HEAD` and `feature · HEAD` are
**clickable labels within the referenced revision card**, not independent DAG
vertices. One revision may carry several labels; a branch HEAD can be a
non-leaf revision (R1 in a history continued by another branch). Historical
revisions have no fictitious “current branch” badge. Selecting a revision in
the graph or sidebar never moves a HEAD.

An unloaded HEAD in a partial catalogue produces **no fake revision**. The
project card indicates how many branch tips are not loaded. The sidebar may
still inspect those branches. A truly complete graph requires the planned
snapshot-consistent native and REST full-DAG reader, including second parents
and paging/frontier state; the current first-parent history API is not that.

## Layout engine and rendering

- `src/graph/compound_dag_layout.h` is the Qt-independent, deterministic
  C++17 ranked-DAG layout. It positions **revisions**, not branch labels.
  Revision ancestry determines rank, oldest at the top, newer below.
- `src/tabs/project_graph_placement.h` packs a whole project DAG into one
  corridor, then all projects of a server side by side and all server cards on
  the **same top row**. Single-line ancestry remains on the project centerline.
- Branch HEAD labels occupy rows inside their referenced revision card.
  Card height and inter-rank separation expand for several labels; even
  multiple branches sharing one HEAD create no extra ancestry nodes/lanes.
- `src/tabs/project_revision_graph.cpp` renders those positions using
  `QWidget/QPainter`, **not** a second bgfx device. The map owns process-global
  bgfx. The renderer draws chronological ancestry and structural
  server/project edges separately, using straight lines for aligned nodes.
- Barycentric crossing-reduction sweeps stabilize fork/merge placement,
  without promising globally minimal crossings. Different projects and
  database sources qualify their revision IDs separately.

## Interaction and camera

Click a server or project card to open its inspector. Click the body of a
revision card to inspect that immutable revision. Click the named `· HEAD`
badge **inside** a revision to inspect its branch. Sidebar selection
highlights the same card/badge, including multiple branch references on one
revision. The branch badge's stable ID is independent of its user-chosen name.

Selecting cards, changing graph display mode and refreshing catalogues do not
re-fit the graph. Fit happens on initial content load and the explicit
**Fit graph** action. Drag, inertia and wheel zoom are clamped to the node
bounding box plus a sensible margin; an undersized graph remains centered.
Folded history summaries do not invent revisions, and the graph can be flipped
top-down/bottom-up without rotating text or changing ancestry semantics.

## Testing and limitations

`tests/project_revision_graph_test.cpp` covers one chain, non-leaf HEAD,
shared HEAD references, badge selection, growing card heights, forks/merges,
project corridor isolation and bounded camera. The Qt-independent DAG kernel
is tested in `tests/compound_dag_layout_test.cpp`. Execute:

```bash
./compile_linux_tests.sh
```

The current GUI builds from bounded first-parent history pages. Its renderer
must not claim to know unseen merge ancestry until the DVMDB full-graph API
has been implemented and integrated. The graph is not itself a network
checkout or mutable working copy.
