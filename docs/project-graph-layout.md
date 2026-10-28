# Projects graph — branch lanes and committed revision history

**Normative GUI design.** The revision graph follows
`AOWIS-SERVER-DVMDB/docs/version-control-semantics.md`. The graph is a
visualization of **committed history organized into development branches**,
not a tree of branch-creation events and not a view of mutable editor state.

## The three levels

1. **Database/source**: a connection or embedded database container. Every
   selected/visible source has a node, including those without projects.
2. **Project**: a UUIDv7 model-history container. A newly created project has
   a default `main` branch but no committed revisions.
3. **Branch lanes with revision history**: each UUIDv7 branch has a stable
   header, name and committed HEAD reference; immutable UUIDv7 revisions
   form the history. A revision has zero, one or two parents.

A branch is a persistent development line, **not a revision leaf**. Its
committed HEAD is a *movable reference* to the latest committed revision on
that line. A separate private working copy is what a user edits.

```text
Database
   |
 Project A
   |
   +-- main -----------------+-- feature -----------+  (branch headers/lanes)
       R0 (finalized)        |                      |
       |                     |                      |
       R1 (finalized) -------+                      |  (shared fork revision)
       |                     |                      |
       R2 (main HEAD)        F2 (feature HEAD)      |  (independent commits)
```

This is a **conceptual lane diagram**, not an instruction to duplicate R1
in storage. There is one underlying R1 UUIDv7 and one immutable record.
Its descendants establish the visible fork. The branch inspector shows
`Created from: main at R1`, without an additional branch-provenance line.

## Empty projects and branches

Creating a project gives `main` with **no HEAD revision**. The graph displays
its project and branch lane, with `No commits yet`—never a fabricated
empty revision card. Creating a branch from an existing committed revision
adds a branch lane referencing that commit but **does not create an extra
revision**. Divergence appears only when independent commits exist.

Branches that share a committed HEAD are separate named lanes, not several
labels squeezed into a single card and not fake descendant cards.
The same committed revision remains one logical object regardless of how
many branch histories reach it. A branch HEAD can be a non-leaf of the
project-wide revision DAG.

## What an edge means

- **Revision ancestry edge**: from one committed revision to another; only
  real ordered revision-parent IDs create these edges.
- **Server/project/branch organization**: grouping or lane geometry, not
  revision ancestry or a commit.
- **Branch creation provenance**: show source branch UUIDv7 and exact fork
  revision UUIDv7 in inspector metadata, **not** a dashed extra graph edge.
- **Working-copy relationship**: an editor workspace/status indicator, not
  a committed revision node or ancestry edge.

Do not connect a branch card to both another branch and a revision. Do not
make branch creation itself appear as a revision or a new level in history.
Do not infer ancestry by comparing UUIDv7 timestamps, branch names or screen
positions.

## Selection and inspector

The graph and navigator share the same project, branch and revision UUIDs.
Selecting a branch opens its inspector with name, UUIDv7, committed HEAD,
origin branch, immutable fork revision and available working-copy operations.
Selecting a revision opens its immutable snapshot and ordered parents.
Selecting an active working copy opens the editor; it must not change the
committed HEAD merely by changing selection.

The inspector remains a compact standard-width dock; object identity stays
above contextual tabs, and actions are tied to the selected object. Keep
long UUIDs and SHA-256 digest fields copyable without forcing the dock wider.

## Layout and interaction requirements

- Use `src/graph/layered_dag_layout.h` for genuine revision ancestry only;
  branch-lane placement is a separate, stable presentation layer.
- Rank history oldest-to-newest top-down. Route merge parent edges with no
  invented intermediate committed vertices.
- Reserve enough space for branch headers and badges; never overlap sibling
  lanes or adjacent projects. All source groups remain visually distinct.
- The `Full history` view covers all branch tips and *both* merge parents;
  `HEADs` selects branch-local latest committed revisions. Unborn HEADs are
  displayed explicitly, not omitted as broken pointers.
- A page/graph snapshot is published only after ancestry closure and
  branch-HEAD consistency are validated. Cancellation cannot merge two
  different graph generations.
- Selection, inspector tab changes and refresh do not unexpectedly refit
  or reset the camera. Fit is an explicit action after initial display.

## Graph acceptance examples

1. One empty project: project + `main`, no revision vertices.
2. `main` and `feature` share committed R0: two branch lanes, one logical R0.
3. A new branch from R0 changes labels/lanes but not revision count.
4. Commits on both branches produce true divergence from R0; no extra
   provenance edge and no false double connection.
5. A merge has two real parents, preserves the source branch and updates
   only the target branch's committed HEAD.
6. Two simultaneous working copies of the same branch do not add branches,
   move HEADs, or create revision vertices until a commit succeeds.

This document specifies the intended graph behavior. The renderer and Qt
tests must be updated to match it before the corresponding UI is considered
complete.
