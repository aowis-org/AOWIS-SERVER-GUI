# Projects graph: placement and navigation

The Projects canvas displays a **compound revision DAG**, with distinct cards
for databases, projects, branches and revisions. Revision ancestry comes only
from immutable revision parent IDs. Database/project containment and
branch-to-HEAD annotations are **structural links**, never extra revision
parents; they do not affect ancestry traversal, folding or checkout.

## Deterministic compound layout

Implementation: `src/tabs/project_graph_placement.h`, invoked by
`ProjectsWidget::rebuildGraph()`.

1. **Revision depth:** Assign each revision the maximum loaded-parent depth
   plus one. Disconnected roots may share depth zero; merges keep all parents.
   Guard cyclic/malformed histories to avoid recursive loops.
2. **Order:** Sort initial positions by immutable ID, then perform alternating
   parent/child barycentre sweeps. Each card at a given depth has a distinct
   slot; branch names do not reserve invisible lanes.
3. **Branch cards:** Position live branch labels beneath the revision layers,
   linked to their actual HEAD revision by a separate dashed connector.
   Branch-card identities use stable branch IDs, not project/branch display
   names; duplicate branch names on different databases remain distinct.
4. **Projects:** Measure each revision subgraph and pack project blocks into
   shelves of bounded width beneath their database card. Center each shelf
   within the database group, **including a single project narrower than the
   database card**. With a single database, project, revision and branch,
   all four card centers lie on one vertical line.
5. **Databases:** Pack the database groups into balanced rows, preserving
   source order. Long histories grow vertically, rather than stretching
   unrelated sources horizontally.
6. **Connectors:** Aligned cards use straight vertical strokes. Nonaligned
   cards use smooth curves; wrapped shelf links route around the outer gutter.
   Structural edges are visually distinct from ancestry and branch-to-HEAD
   annotations.

The top-down/bottom-up orientation is a presentation transformation, so
labels and click regions stay upright. Heads-only view keeps structural
cards and branch labels visible while folding hidden ancestry.

## Selection synchronization

Selecting a database, project, branch or revision in the sidebar selects its
corresponding graph card. Branch selections also highlight associated branch
badges on revision cards. Clicking a graph card (or a revision's branch badge)
selects the corresponding inspector and sidebar tree item. IDs are scoped per
DVMDB source to avoid collisions across independently hosted catalogues.
Selecting an offscreen graph item pans it back into view without resetting
zoom unless the underlying graph topology changes.

## Camera bounds

The interactive camera is restricted to the rendered node/card bounds, with
up to 110 viewport pixels of breathing room on each axis (or 22% of a small
viewport). When a graph is smaller than the viewport along an axis, it stays
centered there and **cannot be panned into empty space**. Larger graphs can
move only until their outermost cards reach the margin. The same constraint
applies to mouse drag, momentum, keyboard pan, animated wheel zoom, window
resize and Fit graph. Keyboard and mouse inertia stop at boundaries.

A deliberately zoomed-out view may center differently from the wheel pointer
when preserving that pointer would violate the camera bounds.

## Regression coverage

`tests/project_revision_graph_test.cpp` exercises deterministic layout,
nonoverlap, root/HEAD order, centered single-chain placement, branch-card
activation, inspector selection IDs, pan inertia and content bounds. Inspector
and multi-source tests cover sidebar synchronization and duplicate remote IDs.

Run `./compile_linux_tests.sh` and check the targets
`aowis-project-revision-graph`, `aowis-projects-inspector-dock`, and
`aowis-projects-multi-source`.

## Future scaling

Hundreds or thousands of cards will benefit from viewport-aware expansion,
spatial indexing, edge bundling and perhaps a dedicated crossing minimizer
such as ELK. A large graph-layout library is not necessary to fix a
one-project centerline, camera constraints or basic selection semantics.
