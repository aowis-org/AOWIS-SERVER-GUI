# Project graph: revision DAG, branch HEAD labels and camera

**Implementation snapshot: 2026-10-27.** The Projects graph follows DVMDB's
`AOWIS-SERVER-DVMDB/docs/version-control-semantics.md`
when that repository is checked out alongside the GUI. The DVMDB document is
the product-level contract; this document specifies the currently implemented
GUI presentation and the P0 full-DAG source reader.

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

The GUI now loads the native `ProjectService::revisionGraph()` or remote
`GET /dvmdb/v1/projects/{id}/revision-graph` and follows the pinned cursor
until `complete=true`. It validates every referenced HEAD and ancestor before
publishing the project graph. Truncated or invalid pages fail the refresh
explicitly; the existing catalogue remains visible as a stale cache. There
are no fake revision cards or hidden partial parent edges in a successful load.
To protect the interactive GUI, an oversized graph fails with an explicit
limit error (25,000 revisions per project / 60,000 per source).

## Layout engine and rendering

**`src/graph/layered_dag_layout.h` is the production revision-DAG
layout engine.** It is a self-contained C++17 implementation of a
Sugiyama-style layered graph layout (not a copy of Graphviz source). Its
stages are: deterministic topological longest-path ranking; insertion of
virtual vertices for long edges; repeated barycentric sweeps to reduce
crossings; constrained coordinate relaxation using isotonic regression to
prevent variable-sized cards from overlapping; and cubic Bézier route
construction through the allocated long-edge lanes. Input vertex IDs are
sorted for stable placement across backend responses. The engine rejects
malformed cyclic ancestry rather than claiming to lay out a valid DAG.

There is **no runtime Graphviz dependency**, no external executable, no
`QProcess` and no added third-party library. The graph remains rendered by
`QWidget/QPainter` using the native layout's edge paths, and remains
independent of the map's process-global bgfx context. The legacy
`src/graph/compound_dag_layout.h` continues to serve its independent
ranked-layout users/tests; it is not the production project history path.
The obsolete `graphviz_dot_layout.h` adapter is an inert compatibility header
for worktrees that already received the earlier experimental patch.

- `src/tabs/project_graph_placement.h` independently places each project's
  real revision DAG, measures its full bounds **including spline lanes**, and
  reserves that entire width before placing the next project. It then packs
  complete server groups side by side; **every server card stays on the same
  top row**. This fixes an older hard-coded block width that could let wide
  revision DAGs overlap adjacent projects.
- Actual branch HEADs are decorations **inside** their immutable revision
  cards, not artificial ancestry vertices. HEAD rows contribute to the node
  heights passed to the native layout engine. Shared and non-leaf HEADs remain supported.
- The renderer retains routed ancestry edges in Full tree mode. Folded
  history changes visible coordinates, so its omission markers and their
  connections are routed separately rather than reusing now-invalid splines.
  Server/project ownership connections are distinct from revision ancestry.
- Every project is positioned in its own source-scoped identity namespace;
  equal hashes from different projects are not silently merged.

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
shared HEAD references, badge selection, non-overlapping header/badge geometry,
growing card heights, forks/merges, routed native-DAG edges, measured project
corridor isolation, deterministic reordering and bounded camera. The native Qt-free layout engine is tested in
`tests/layered_dag_layout_test.cpp`; the earlier ranked-DAG kernel keeps
its own tests in `tests/compound_dag_layout_test.cpp`. Execute:

```bash
./compile_linux_tests.sh
```

`tests/project_source_reader_test.cpp` checks full-DAG REST pagination, pinned
HEAD consistency, merge parents and cancellation; `tests/projects_multi_source_test.cpp`
uses the graph endpoint for its two independent database sources. Native
reads use the same source model through `ProjectService::revisionGraph()`.
The graph is not itself a network checkout or mutable working copy.
