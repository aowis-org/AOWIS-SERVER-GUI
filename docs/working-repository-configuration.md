# Per-user DVMDB working repository configuration

**Normative GUI integration design**, coordinated with the
[DVMDB personal repository contract](../../AOWIS-SERVER-DVMDB/docs/private-working-repositories.md),
[Projects workspace](projects-workspace.md), and
[deferred security notes](deferred-security-considerations.md).
The [GUI implementation roadmap](dvmdb-integration-roadmap.md) lists the
backend prerequisites, per-user configuration work, decision gates and tests.
The configured HTTP profile is implemented in
`src/config/personal_repository_storage.cpp`; the complete editor lifecycle
is not. The exact stored JSON/INI key is documented below.

## Explicit roles, not interchangeable source profiles

The GUI has one configured **personal working repository** for the active
user. It is the durable owner of their private drafts, local commits,
local branches and remote-tracking data. The navigator may also contain
one or more **shared project sources**: servers/repositories that hold
published project histories. A checked source is not automatically selected
as the personal working repo. Users must be able to inspect a shared source
without giving it custody of their private draft data.

| Mode | Working DVMDB configuration | Behavior |
|---|---|---|
| Standalone | Dedicated native personal DVMDB root (`aowis-personal-dvmdb` under per-user generic data), **not** the navigable `aowis-standalone-dvmdb` local project source | Edit and local commit without HTTP once VC-04 enables verified private checkout; publish to a separately selected shared source |
| Desktop/basic GUI | **Required** private HTTP DVMDB endpoint and repository identity | Draft saving, checkout and local commit go to that private service |
| Browser/web GUI | **Required** private HTTP DVMDB endpoint and repository identity | No editing with browser memory alone; server-side durable draft recovery |

One configured working repo belongs to one intended user and can serve
multiple of their sessions, projects and devices. This is a functional
repository-selection model, not an authentication or ownership policy.
Each session creates its **own working-copy UUID**
by default. Reopening one existing draft requires an exclusive expiring
writer lease, fencing token and expected saved-state generation on every
Save; read-only sessions may inspect the draft, and a stale writer never
saves over recovered work. The GUI must show the current working repository,
draft identity, lease/recovery status and saved-state generation.

## Actual configuration format

The desktop GUI stores a compact JSON object under the Qt INI settings key
`projects/personal_repository` in `aowis-server-gui.ini`. The object has
**exactly** these meaningful fields:

```json
{"endpoint":"http://127.0.0.1:8755","repositoryId":"<exact-lowercase-uuidv7>"}
```

The provided placeholder must be replaced with the private DVMDB's actual
`GET /dvmdb/v1/repository` UUID. `savePersonalRepositoryProfile` validates
HTTP(S) root URLs without credentials/query/fragment and validates the UUIDv7.
The WebAssembly build stores the same JSON under browser localStorage key
`aowis.projects.personalRepository`. **This is endpoint selection, not a
credential or secret store.**

Standalone resolves a separate embedded personal DVMDB path through
`defaultPersonalDvmdbPath()` (per-user GenericDataLocation,
`aowis-personal-dvmdb`), and rejects conflicts with a navigable local source.
It does not reuse the selected shared source path as a draft root.

Shared project connections remain separate GUI database-source profiles.
This version-control configuration does not define access tokens, passwords,
accounts or an authentication mechanism. Any future credential handling is
separate [security backlog](deferred-security-considerations.md) work.

## Committed branch checkout into a personal HTTP repository

For a selected committed branch from a REST DVMDB source, the Projects
working-copy dialog can explicitly copy that branch into the configured
personal HTTP DVMDB without modifying the source. The GUI verifies the
source repository UUID and currently selected HEAD, provisions the exact
logical project UUID in the personal repository, transfers a full verified
revision bundle, and requires the imported HEAD to match the selection.
The bundle preserves the branch UUID and immutable revision ancestry, while
the two repository roots retain independent UUIDs and writable branch HEADs.
A matching local project can accept an idempotent full replay; conflicting
local history is rejected rather than silently overwritten.

This path handles committed REST branches up to 64 MiB per transfer.
Unborn branches cannot use revision bundles, and native embedded/local
sources need a separate checkout adapter. Neither checkout nor importing a
bundle creates a working-copy draft; the user explicitly creates that draft
at the imported HEAD afterward. The model editor Save and Commit operations
require their own lifecycle integration.

## Startup and connectivity behavior

1. Resolve the active user's working-repository selection *before* starting
   any editable session. Embedded path must be durable, writable and not a
   transient cache. HTTP endpoint/repository must be explicitly configured,
   reachable and identified as the expected personal repository.
2. A missing/unavailable private repo makes editing and Commit unavailable;
   published history may remain readable from configured shared sources.
   Never silently fall back to shared DVMDB, a different user repo or RAM.
3. On reconnect, offer recovery of saved working-copy UUIDs, pinned bases,
   local unpublished commit histories and pending publications. Preserve
   draft dirty state across navigation and connection changes.
4. Distinguish **Save draft**, **Commit locally** and **Publish to shared**.
   Show local HEAD, remote-tracking HEAD and current shared HEAD explicitly.
   The same logical branch UUIDv7 may appear at both endpoints, but those
   HEAD refs are independently mutable and identified by repository UUID.
   Store/verify remote targets rather than matching human-readable names.
5. Publication failure retains committed local revisions and the editable
   draft. Fetch/reconcile operations need user confirmation before changing
   local branch history or working-copy bases. Commit and Publish each use a
   persisted UUIDv7 operation ID and payload digest. After a timeout, query
   authoritative status and never submit a fresh write as an implicit retry.
6. A newly created local branch retains its UUIDv7 on explicitly chosen
   remote provisioning. An absent remote branch differs from an existing
   unborn branch with HEAD `null`; first Publish from `null` checks the exact
   expected value. New projects start unborn; unsupported experimental
   repository formats are rejected rather than migrated.

The private HTTP service being configured does **not** prove that only the
intended user can access it. Treat unauthenticated HTTP editing as suitable
only for a controlled, isolated deployment; do not expose it publicly.
This is operational guidance, **not** an additional VC implementation milestone
or an acceptance gate for this document. See the
[deferred security notes](deferred-security-considerations.md).

## GUI acceptance

- Standalone edits and commits against its own embedded personal repo;
  shared database selection does not redirect local drafts.
- Basic and web modes cannot edit until their personal HTTP repo is valid;
  an unavailable repo does not discard draft metadata or silently fallback.
- Two users viewing the same shared R7 have separate working repos and can
  save drafts and locally commit without changing shared R7.
- `Publish` checks the shared HEAD and rejects stale history, leaving the
  private commit and working copy recoverable.
- Working-copy IDs and remote-tracking metadata are not branch/revision
  ancestry vertices; the logical branch UUID is stable across clone and
  Publish, while local and shared HEAD values can differ.
- The same draft reopened by two sessions is writable by only the current
  fenced lease holder with the expected generation; a stale save is rejected.
- A completed Publish with a lost HTTP response is recovered by querying
  its original operation UUID, without duplicating the remote ref update.
- Three-way merge conflicts remain in private working-copy state and require
  explicit user action; no automatic rebase, force Push or last-writer-wins.
