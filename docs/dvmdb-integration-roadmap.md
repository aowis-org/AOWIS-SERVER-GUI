# DVMDB / GUI integration requirements and delivery order

This is the execution plan for the [version-control contract](../../AOWIS-SERVER-DVMDB/docs/version-control-semantics.md),
[Projects workspace](projects-workspace.md), and
[branch-oriented graph](project-graph-layout.md). It defines requirements,
not claims that every workflow is already implemented.

## Foundation: common native and REST identity model

- Use real project, branch, committed-revision and private working-copy UUIDv7s.
- Carry canonical SHA-256 digest and Merkle roots separately from UUIDs.
- Carry branch creation source UUID and exact fork revision UUID.
- Support **unborn branch HEADs** explicitly: `null` means no commits yet.
- Make all project-wide graph pages snapshot-consistent with both merge parents
  and exact branch HEADs. Never display a graph assembled from incompatible
  generations or invented identities.

## Step 1 — Project/branch lifecycle and graph

1. `Create project` creates `main` with no committed revision. Remove any
   implicit empty-root revision publication from both native and REST.
2. `Create branch` from a committed revision records exact fork metadata,
   creates no revision and does not alter the source HEAD. A request based
   on uncommitted edits explicitly offers commit-first or fork-without-edits.
3. Present branches as **lanes of committed revisions**. No branch-reference
   leaf cards, double incoming links or extra branch-provenance ancestry
   edges. Distinct branches may share immutable committed revisions.
4. Show empty branches/projects with `No commits yet`, never fake R0.
5. Selecting any source/project/branch/revision must only inspect it; graph
   camera and source filtering must not alter version control state.

## Step 2 — Private working copies

1. Implement network discovery and checkout of a **fixed committed revision**
   (or a first empty model checkout for an unborn branch).
2. Give each editing session its **own UUIDv7 working copy**, linked to
   project, branch, owner/session and exact base revision/null.
3. Keep working changes recoverable across temporary disconnection,
   refresh and GUI navigation. Never use server branch HEAD as an editor's
   mutable model object.
4. Separate `Discard my draft` from version-control operations that would
   move the branch HEAD.

## Step 3 — Commit with optimistic concurrency

1. Validate full model references and hydraulic semantics on the candidate
   snapshot or deterministic change set.
2. Commit with project/branch UUIDv7, exact expected committed HEAD/null,
   working-copy identity and idempotency key.
3. Durably write one new immutable UUIDv7 revision with SHA-256 record hash,
   then atomically advance only the chosen branch HEAD.
4. On stale expected HEAD, show `Branch advanced` and retain the user's
   draft. Offer explicit three-way comparison/reconciliation/rebase.
5. After ambiguous timeout, refresh/query operation status before retrying.

## Step 4 — Merge, sync and permission-aware workflows

- Merge committed heads using pinned source/target values; publish a
  two-parent immutable revision only for a validated divergent merge.
- Replication bundles preserve branch UUIDs, immutable fork provenance,
  nullable HEADs, revision UUIDs, parent IDs and integrity bindings.
- Authentication, per-project rights, working-copy ownership and auditing
  must be enforced consistently in native and remote entry points.

## GUI acceptance gates

- An empty project displays `main`, zero committed revision cards and an
  available private checkout action.
- Two users start from R7 on `main`; the first commit succeeds, the stale
  second fails without losing draft edits.
- Branching from R7 preserves an exact fork point, without a new revision.
- Independent branch commits show actual DAG divergence; merges have two
  true revision-parent edges.
- Names with spaces and valid UTF-8, stable UUIDv7 identity and canonical
  SHA-256 verification work identically in embedded and REST modes.
- Inspector and navigator display ownership/provenance without creating
  extra graph ancestry lines or forcing the sidebar wider.

See the [practical connection guide](dvmdb-quickstart.md). The GUI is complete
for this design only when native/REST integration and its Qt regression suite
pass these behaviors end to end.
