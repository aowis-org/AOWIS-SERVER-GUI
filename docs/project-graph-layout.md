# Projects graph — Git-style committed revision DAG

**Normative GUI design.** The practical graph cutover is milestone VC-09
of the [GUI implementation roadmap](dvmdb-integration-roadmap.md).
The revision graph follows
`AOWIS-SERVER-DVMDB/docs/version-control-semantics.md`. The graph is a
visualization of **committed history organized into development branches**,
not a tree of branch-creation events and not a view of mutable editor state.
The commit/parent graph follows Git's `log --graph --all --decorate` model:
branch names label pointers to immutable committed revisions. Lanes are
routing choices for ancestry edges, **not branch ownership or lineage**.

## The three levels

1. **Database/source**: a connection or embedded database container. Every
   selected/visible source has a node, including those without projects.
2. **Project**: a UUIDv7 model-history container. A newly created project has
   a default `main` branch but no committed revisions.
3. **One revision DAG with branch decorations**: immutable UUIDv7 revisions
   and true parent edges form history. Each UUIDv7 branch names a movable
   pointer to its current committed revision (or null for unborn). The GUI
   may use colored lanes to route the DAG but does not force a separate
   historical column or duplicate ancestor for each branch.

A branch is a persistent development line, **not a revision leaf**. Its
committed HEAD is a *movable reference* to the latest committed revision on
that line. A separate private working copy is what a user edits.

```text
Database / Project A

              R0
               |
              R1
             /  \
            R2   F2      (two genuine commits with parent R1)
            |     |
       [main HEAD] [feature HEAD]   (branch refs, not vertices)
```

R1 exists only once. The real ancestry edges R1->R2 and R1->F2 make
its fork visible after the corresponding commits. Creating `feature` at R1
without a commit adds a *branch reference* to R1 but no vertex or edge.
The inspector may show `Created from main at R1`; this is optional metadata
and **not** an additional ancestry line.

## Empty projects and branches

Creating a project gives `main` with **no HEAD revision**. The graph displays the project, the unborn `main` ref and `No commits
yet`—never a fabricated revision card. Creating a branch requires an
existing committed revision (so the first commit must precede branching).
The new branch label references its exact fork revision, **without creating
another revision**. Divergence appears only when independent commits exist.

Branches that share a committed HEAD are distinct references to the same
node. Show their names in a readable ref list, badges beside the node, or a
synchronized branch panel; never create fake descendant cards or force all
labels inside one cramped revision card.
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

- Use `src/graph/layered_dag_layout.h` for genuine revision ancestry only.
  Lanes are inferred for stable, readable routing of the common commit DAG;
  they are not stored per-branch ownership of immutable revisions.
- Rank history oldest-to-newest top-down. Route merge parent edges with no
  invented intermediate committed vertices.
- Reserve enough space for reference labels, long UTF-8 branch names and
  project labels; never overlap graph lanes or adjacent projects. All source groups remain visually distinct.
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
2. `main` and `feature` share committed R0: one R0 vertex, two legible
   branch labels pointing to it; no fake second branch-ancestry line.
3. A new branch from R0 changes reference labels, not revision count.
4. Commits on both branches produce true divergence from R0; no extra
   provenance edge and no false double connection.
5. A merge has two real parents, preserves the source branch and updates
   only the target branch's committed HEAD.
6. Two simultaneous working copies of the same branch do not add branches,
   move HEADs, or create revision vertices until a commit succeeds.

This document specifies the intended graph behavior. The renderer and Qt
tests must be updated to match it before the corresponding UI is considered
complete.
