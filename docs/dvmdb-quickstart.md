# Connect AOWIS Projects to DVMDB

This guide describes the intended GUI workflow under the
[Projects workspace contract](projects-workspace.md) and
[DVMDB version-control specification](../../AOWIS-SERVER-DVMDB/docs/version-control-semantics.md).
Use a DVMDB server and GUI build supporting that contract; do not assume an
older binary's data lifecycle matches it.

## 0. Configure your personal DVMDB working repository

The project source you browse is not where uncommitted user edits belong.
Standalone uses its built-in persistent DVMDB as the personal working repo;
basic desktop or browser/web GUI requires an explicitly configured private
HTTP DVMDB working repository. See
[configuration contract](working-repository-configuration.md). A shared
repository must never silently become a user's private working repo.
Without a working repo, browsing may be read-only but editing/Commit is off.

## 1. Choose a database source

Open **Projects → Databases** and add a source using its `+` action, or
choose **Add database** on an empty workspace. Create an embedded/local
source in a supported native Standalone build, or a remote DVMDB REST
source by entering its endpoint. Validate connectivity and refresh the
project catalogue. A visibility checkbox controls which sources appear in
the navigator and canvas; it does not delete or disconnect a database.

The inspector identifies the selected database and exposes its connection
settings, availability and projects. A checked, empty database still gets
a server/source card on the graph canvas.

## 2. Create a project

Select an available database, choose **Create project** in the inspector,
enter its name and confirm. The created project receives a UUIDv7 and a
new UUIDv7 branch named **main**.

**No revision has been committed yet.** Its main branch has an **unborn
HEAD** (`null`). The navigator shows the project and `main`; the graph
shows `No commits yet`, not a synthetic R0 card.

## 3. Start editing privately

Choose **Checkout / Create working copy** for `main`. A working copy is
private to the user/session, has its own UUIDv7, and starts from the empty
model state. Editing this model does not modify the repository's committed
HEAD or anyone else's working copy.

When a branch already has commits, checkout pins a precise base committed
revision UUIDv7. The inspector distinguishes **current branch HEAD** from
**my working-copy base** and **my unsaved changes**.

## 4. Make the first commit

Select **Commit locally** from the working-copy editor, inspect model
validation and commit metadata. The GUI commits to your *personal* DVMDB
with an expected local HEAD of `null` on the first commit. This creates
immutable revision R0 there but **does not publish to the shared DVMDB**.
To share, select **Publish**, which sends verified R0 and atomically moves
the shared unborn `main` HEAD from `null` to R0, provided it is still unborn.

Every later commit creates another immutable UUIDv7 revision, with its
committed parent bound by the canonical SHA-256 integrity record.

## 5. Create a branch

A project with unborn `main` must be committed first; you cannot branch
from an uncommitted empty project. Select a branch or immutable historical
revision. **Create branch** shows
the committed starting revision, branch name and source. The new branch
gets a new branch UUIDv7 and records the source branch UUIDv7 plus the
exact fork revision UUIDv7. Its initial committed HEAD points to the
already existing revision. **No extra revision is created.**

If your working copy has uncommitted changes, explicitly **commit first**
(or choose to branch from its last committed base without those edits).
No other editor's working copy is automatically finalized. Branch names
support valid UTF-8 and spaces.

## 6. Concurrent work and conflicts

Multiple users can work from the same shared R7 in their **own personal
DVMDB repositories**, each with private working copies. Both can commit
locally. The first compatible Publish advances the shared branch; the
second, still expecting shared R7, is rejected even if their edits do not
overlap. Neither their local immutable commits nor drafts disappear. The
GUI offers Fetch, compare/reconcile and an explicit subsequent Publish. The full hydraulic model is revalidated before
publication.

## 7. History, inspection and synchronization

The project canvas shows one Git-style committed revision ancestry DAG,
with branch labels at their referenced HEAD revisions and optional layout
lanes. Actual independent commits cause visible forks and merge commits
have two parent edges. The branch inspector records creation source/fork
metadata without drawing an extra ancestry line. Working copies are
editor/session objects, never immutable revision cards.

Native and REST sources must expose the same UUIDv7 identities, immutable
SHA-256 records, branch-head/null states and conflict behavior. Replication
preserves all those values without inventing new branch origins.

For implementation gates and acceptance tests, see the
[integration roadmap](dvmdb-integration-roadmap.md).
