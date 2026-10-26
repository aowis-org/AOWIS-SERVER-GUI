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
