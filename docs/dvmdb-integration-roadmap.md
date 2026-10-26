# DVMDB integration in the Projects workspace

## Stage 1 — data-source UX (previous patch)

- The left section is **Databases**, with a small header `+` action.
- A fresh GUI installation with no configured sources has no open inspector. The
  main panel offers **Add database**; a source with nothing loaded instead
  displays **No projects loaded**, without claiming its remote/local store is empty.
- Both entry points start the same unsaved inspector draft. **Create** commits
  the profile in one operation, and **Cancel** or closing the dock discards it.
- Profiles identify a connection type: `remote` (REST URL) or `local` (embedded
  DVMDB data directory). Older profiles without a `connection_type` remain
  `remote`; the existing `projects/servers` settings key is preserved.
- With native DVMDB enabled, Standalone provisions a non-deletable **Local
  database** profile on first use at
  `$XDG_DATA_HOME/aowis-standalone-dvmdb` (normally
  `~/.local/share/aowis-standalone-dvmdb`). It does **not** create or open the
  repository yet. The directory is deliberately different from the DVMDB HTTP
  daemon's default `aowis-dvmdb` root. Windows/macOS builds without the DVMDB
  library do not offer embedded creation or auto-provisioning.
- All project/branch/revision UI data is still explicitly marked **preview**;
  saved source profiles do not imply a successful database connection.

## Stage 2 — real data read paths (implemented as the read-only slice below)

Implement a shared project data-source interface, with two implementations:

1. Native `ProjectService` via the embedded DVMDB libraries (Standalone).
2. DVMDB REST API client with deadlines, cancellation, error handling and
   connection state.

Both must report one normalized set of project/branch/revision models to the
existing navigator and graph. Add explicit **Connect / Retry / Disconnect**
states and ensure the empty state distinguishes *no projects*, *loading*,
*offline*, and *error*. Keep the preview fixture separate from real data.

## Stage 3 — writes and checkout

Wire project creation, branch creation, checkout and revision writes through
the same source interface. Never silently treat GUI-only preview edits as
database changes. Protect optimistic expected-head writes and preserve dirty
working copies on conflicts. Coordinate with DVMDB's
`docs/gui-integration-requirements.md` for missing rename/delete lifecycle,
network enumeration, mutable annotations and full DAG pagination contracts.
The current REST API does not implement authentication, so the GUI must not
misrepresent the descriptive auth-mode setting as an authenticated session.

## Stage 4 — sync and metadata

Design offline/online bundles, annotations, ACLs, provenance and revision graph
pagination around stable API contracts. If SQL metadata is introduced, provide
**equivalent SQLite and PostgreSQL** schemas, migrations and queries; the
present data-source profile storage uses Qt settings/JSON and introduces no SQL.
## Stage 2 — read-only project catalogue (current patch)

- One normalized `ProjectSourceReader` reads `projects`, `branches`, and the
  latest 100 first-parent revisions per branch. Real IDs and parent hashes are
  preserved. Earlier ancestors, side-parent history and graph paging are not
  fetched; results must not be mistaken for a complete DAG.
- Embedded DVMDB uses `ProjectService` on a worker thread. The standalone build
  with DVMDB support automatically loads its default local database when the
  Projects workspace opens. The library is not accessed by GUI-only builds.
- REST uses GET requests under `/dvmdb/v1` with absolute and inactivity
  deadlines, JSON/size validation, explicit unsupported-auth rejection and
  cancellation on source switches. No authentication is attempted or claimed.
- The UI separates disconnected, loading, failed, and successfully loaded
  *snapshots*. Connect, Retry, Cancel, Disconnect and Refresh are available as
  appropriate; a successfully read catalogue can still become stale later.
- DVMDB-backed project, branch and revision inspector fields are read-only.
  Preview-only creation, deletion and annotation edits cannot touch real
  database data. Bundled examples may still be opened in the editor; the
  fake project-creation workflow remains confined to demonstration profiles.
- No native or REST write APIs, checkout, network enumeration, live sync, full
  DAG pagination, background polling, or SQL persistence are introduced yet.

## Stage 3a — supported project/branch creation (current patch)

- Native and REST backends now support project creation and branch creation,
  including a new branch based on the selected historical revision. The native
  write runs on a worker thread; REST uses a bounded asynchronous POST.
- A successful write is **always** followed by a catalogue reload and selection
  of the resulting project/branch. The GUI does not add an unverified item to
  the navigator. Source switching, disconnect and repeated creates are blocked
  while a write is in progress.
- A failed/lost response also triggers a catalogue refresh: REST POST is not
  automatically retried, since a timeout does not prove the write was rolled
  back. Duplicate/conflict errors are displayed from DVMDB's structured 409.
- Preview actions remain isolated from persisted DVMDB. Real project and branch
  names are not locally editable, and Delete stays unavailable because neither
  the DVMDB library nor its REST API currently implements rename/delete.
- Native/REST checkout and model revision writes still require network
  enumeration and pinned-session/expected-head integration; both are deferred.
  Do not treat the absence of these backend operations as completed CRUD.

## Stage 3b — automatic source monitoring and aggregated explorer (this patch)

- Database profiles are **always active**: embedded sources open on startup,
  and remote sources start asynchronous liveness checks and catalogue loading.
  The old Connect/Disconnect sidebar controls are removed, and manual **Refresh
  catalogue** now belongs to the selected database's inspector.
- A shared `HttpHealthMonitor` replaces the Map Server footer's unbounded
  polling and is also used for remote DVMDB connections. Each request has
  HTTP-code and service-response validation, no redirect following, an
  inactivity timeout and an absolute deadline. The first failure is degraded;
  two consecutive failures mark a service offline; recovery is automatic.
  Map Server monitoring remains disabled in standalone as before. Embedded
  DVMDB uses no network polling.
- Health probes use `/dvmdb/v1/projects`; they **do not** fetch revision
  history. Independent catalogue readers load each database on recovery and
  refresh every 60 seconds. Offline catalogue snapshots remain visibly marked
  as caches and are **not writable**. Unchecking a source only filters the
  navigator and graph; it does not stop the health monitor.
- Visibility checkboxes persist per database profile, defaulting to enabled
  for existing saved profiles. The navigator groups projects by source, and
  the revision graph shows data from every checked real database. GUI project
  IDs and graph revision IDs are source-qualified to prevent collisions;
  backend REST/native write calls continue to use raw DVMDB IDs.
- The revision-sidebar remains scoped to the selected project, and the
  top-bar project chooser uses qualified "Database / Project" labels for
  real sources to disambiguate duplicate project names. The in-memory preview
  fixture keeps its isolated one-project graph and flat navigator semantics.
- Loopback tests cover two services with colliding IDs, checkbox persistence,
  graph filtering, invalid HTTP 200 responses, offline/degraded state, and
  recovery. The graph renderer remains the existing QWidget fallback and does
  **not** create a second bgfx device. Full DAG paging, network checkout,
  revision saving, lifecycle rename/delete and authentication are not added.
