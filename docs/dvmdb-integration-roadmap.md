# DVMDB / GUI integration requirements and delivery order

This is the execution plan for the [version-control contract](../../AOWIS-SERVER-DVMDB/docs/version-control-semantics.md),
[Projects workspace](projects-workspace.md),
[Git-style commit graph](project-graph-layout.md), and
[private working repo configuration](working-repository-configuration.md).
It defines requirements,
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
3. Present one **Git-style committed revision DAG**, where optional visual
   lanes route real ancestry edges and branch names label their HEAD tips.
   Never duplicate shared committed revisions, invent branch ancestry edges,
   or show branch-reference cards as revision vertices.
4. Show empty branches/projects with `No commits yet`, never fake R0.
5. Selecting any source/project/branch/revision must only inspect it; graph
   camera and source filtering must not alter version control state.

## Step 2 — Personal working DVMDB repositories and private working copies

1. Add explicit, validated personal working-repository configuration:
   Standalone uses built-in DVMDB, basic GUI and browser/web GUI require a
   user's **private HTTP DVMDB**. Shared project sources are distinct.
2. Implement fetch/checkout into the working repo, local/remote tracking
   refs and durable private drafts. An unavailable working repo disables
   editing, not published-history browsing.
3. Provide draft recovery and separate Save, Commit locally and Publish
   actions. A configured private HTTP URL alone is not access control.

### Individual working-copy requirements

1. Implement network discovery and checkout of a **fixed committed revision**
   (or a first empty model checkout for an unborn branch).
2. Give each editing session its **own UUIDv7 working copy**, linked to
   project, branch, owner/session and exact base revision/null.
3. Keep working changes recoverable across temporary disconnection,
   refresh and GUI navigation. Never use server branch HEAD as an editor's
   mutable model object.
4. Separate `Discard my draft` from version-control operations that would
   move the branch HEAD.

## Step 3 — Local commit and remote publication with optimistic concurrency

1. Validate full model references and hydraulic semantics on the candidate
   snapshot or deterministic change set.
2. **Commit locally** in the personal repository with exact expected
   local HEAD/null, working-copy identity and idempotency key.
3. **Publish** verified immutable commits and conditionally advance the
   shared branch from its separately pinned expected remote HEAD/null.
4. A stale local Commit retains the draft. A stale remote Publish retains
   both the already-committed local history and private drafts; offer
   fetch, explicit reconciliation/merge and safe re-publication.
5. After ambiguous local or remote timeout, query the corresponding
   repository's authoritative operation status before retrying.

## Step 4 — Merge, sync and permission-aware workflows

- Merge pinned committed histories in the personal repository, publishing
  the validated two-parent revision (or an eligible fast-forward) by an
  explicit conditional update of the shared target branch.
- Replication bundles preserve branch UUIDs, immutable fork provenance,
  nullable HEADs, revision UUIDs, parent IDs and integrity bindings.
- Authentication, per-project rights, working-copy ownership and auditing
  require a dedicated later AOWIS-wide design. Until implemented, private
  HTTP working repositories must be isolated by deployment controls; see
  [security backlog](../../AOWIS-SERVER-DVMDB/docs/security-permissions-design.md).

## GUI acceptance gates

- An empty project displays `main`, zero committed revision cards and an
  available private checkout action.
- Two users start from shared R7 in separate working DVMDBs. Both may
  commit locally, but only a compatible conditional remote Publish advances
  shared `main`; rejection preserves local commits and drafts.
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
