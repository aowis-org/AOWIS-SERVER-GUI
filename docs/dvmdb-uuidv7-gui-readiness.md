# UUIDv7 DVMDB GUI integration — canonical native and HTTP cutover

**2026-10-27 — aligned to DVMDB snapshot `dvmdb(20261027-211243).zip`.**

DVMDB now exposes RFC 9562 UUIDv7 as the canonical project, branch and
revision identity in *both* `ProjectService` and `/dvmdb/v1`. SHA-256 is
**integrity metadata**, not a revision address. This is a **pre-release
breaking change**; obsolete SHA-addressed development repositories are not
silently migrated. Consult DVMDB `docs/uuidv7-http-cutover.md` and
`docs/uuidv7-identity-format-v2.md` for the current native/storage contract.

## GUI behavior

- **Remote project reading** uses `GET /dvmdb/v1/projects`, then paginated
  `GET /projects/{id}/revision-graph?limit=256[&cursor=...]` until `complete`.
  A project ID, each `heads[].branchId`, every `heads[].revision`, every
  `revisions[].id` and all revision parents must be UUIDv7.
- Each branch HEAD has an expected canonical `heads[].recordHash` SHA-256.
  Every revision has a canonical `recordHash` and matching ordered
  `parentRecordHashes`; the reader checks cross-page HEAD invariance,
  all parent SHA bindings, closed ancestry and acyclicity **before** publishing
  the snapshot. Incomplete or oversized responses fail the refresh, leaving
  previously displayed data stale instead of fabricating graph nodes.
- **Embedded/Standalone** uses `ProjectService::revisionGraphV2()` when the
  linked submodule supports it. Older embedded backends fail with an explicit
  upgrade error; there is **no** legacy SHA graph fallback in this release.
  The reader retains cooperative native cancellation and project graph limits.
- **Write operations** use canonical REST `POST /projects` and
  `POST /projects/{id}/branches`; native embedded writers call
  `createProjectV2`, `createBranchV2`, or `createBranchAtRevisionV2`. A selected
  historical revision is passed as a UUIDv7 and an actual reachable source
  branch. A branch creates a new reference and does **not** create a revision.
  Responses are validated for UUIDv7 and stable branch names/identities.
  Uncertain network writes still require an authoritative refresh before
  retrying, not a blind retry.
- Graph and navigator selection remain qualified by source and project.
  The inspector displays the full revision UUIDv7 and canonical SHA-256
  independently; revision cards use the trailing 12 UUID digits merely as a
  *label*, never a selection key.

## Remaining scope

The GUI can now create projects and named branches, inspect their complete
revision DAGs, and fork from historical revisions for both REST and supported
native libraries. This patch does **not** implement checkout, network editing,
model commits, hydraulic merge validation, branch rename/delete or model
comparison. These belong to later P1 work. The GUI still requires Qt-enabled
integration tests on the developer system; the standalone C++ native API
compile probe alone is not a substitute for them.

Use `./compile_linux_tests.sh` in AOWIS-SERVER-GUI and test a fresh DVMDB
repository with root revisions, divergent branches, a merge and multi-page
history in both connection modes. Do not point old SHA-development stores at
the new UUIDv7 service expecting automatic migration.
