# Projects workspace — current behavior and boundaries

**Updated 2026-10-27.** This is a reference for the *current* GUI. The
workspace has real DVMDB catalogue integration **and** separately identified
in-memory demo content. They must never be mistaken for each other.
For a hands-on connection guide, see [DVMDB quickstart](dvmdb-quickstart.md);
for future checkout/save work, see the
[integration roadmap](dvmdb-integration-roadmap.md).

## Current architecture at a glance

| Component | Implemented behavior |
|---|---|
| Data source profile | Saved locally (native GUI INI or browser `localStorage`), with a stable ID, name, type, endpoint/path, descriptive metadata and visibility. |
| Local embedded source | C++ DVMDB `ProjectService` in supported native Standalone builds; no HTTP and no remote health checks. |
| Remote source | REST `/dvmdb/v1`, automatic health checks, async catalogue reads and periodic refresh. Authentication **not** implemented. |
| Catalogue | Projects and consistent, fully paginated two-parent revision DAGs: native and REST use UUIDv7 identities and canonical record SHA-256 integrity bindings. |
| Real writes | Project create and branch create/fork only. Subsequent catalogue reload confirms persisted results. |
| Preview | Explicit demo profiles and built-in example *placeholders* remain in-memory and cannot be mistaken for DVMDB revisions. |
| Graph | Source-qualified nodes for databases, projects, immutable revisions and separately selectable branch HEAD references. QWidget/QPainter revision graph; no second bgfx instance. |

The current GUI does **not** enumerate and check out hydraulic networks,
commit edited models to a new revision, perform remote project/branch
rename/delete, or manage authenticated REST sessions. The underlying engine
has additional APIs not yet attached to the editor. There is no SQL profile
database: settings are stored using Qt settings/JSON.

## Layout and interactions

### Left: Databases

- The small **+** beside **Databases** (or the central **Add database**
  empty-state action) opens an **unsaved inspector draft**. The inspector
  contains type, name, URL/local path and descriptive settings. **Create**
  persists the new profile; **Cancel** or closing the creation inspector
  discards it. No intermediate name-only dialog.
- The type is **Remote database (REST)** or **Local database (embedded)**.
  The embedded option requires a Standalone build with native DVMDB support.
  Type cannot be changed after creation, to avoid silently reinterpreting
  URLs as local filesystem paths.
- The **checkbox** beside each database filters its participation in the
  project tree and revision graph. It is persisted and is **not** a
  Connect/Disconnect switch. All configured databases stay active.
- The row's health indicator describes remote availability; selecting the
  row opens its inspector with status and **Refresh catalogue**. Remote
  health is polled about every 5 seconds, with degraded/offline transitions
  and recovery. Catalogues refresh about every 60 seconds while healthy.
  Local embedded sources do not receive HTTP probes.
- Cached projects may remain displayed when a source goes offline, but
  they are stale/read-only. A failed probe must not trigger unsafe writes.
  Profiles can be removed from the inspector; removing a profile never
  deletes remote or native database files. The default Standalone local
  source cannot be deleted.
- With no configured sources, the inspector is closed and the central
  workspace offers **Add database**. Every checked database remains visible
  as a real server node in the central graph even if its catalogue contains
  zero projects. If databases are configured but all are unchecked, the
  canvas displays **No databases selected**. No project or revision nodes
  are fabricated for empty catalogues.

### Left: Projects & Branches

- Real projects are grouped by **database**, and branches appear beneath
  their project. Source-qualified IDs prevent a collision when two
  databases independently use the same project UUID or revision hash.
- The navigator has **no project/branch/import action row**. Selecting an
  available database opens its inspector, where **Create project** starts an
  inline draft. Selecting a project, branch or revision exposes **Create
  branch** in that object's inspector.
- The inline branch form offers explicit **start from** choices: a named
  branch's HEAD or any loaded historical revision (chosen revisions resolve
  a source branch through both-parent reachability). The default is `main`
  for a project, the selected branch HEAD for a branch, and the *exact*
  selected revision for a revision. The form displays the full resulting
  HEAD revision UUID, explains that branch creation **does not commit**, and
  validates UTF-8 branch labels, including spaces (maximum 96 bytes, no path
  separators or control characters), before allowing Create. Cancel discards the draft; changing selection
  with an entered name asks before discarding. Writes refresh the authoritative
  catalogue; uncertain HTTP results are never blindly retried.
- A persisted DVMDB project/branch name, description and tags are displayed
  read-only in the inspector: the server does **not** yet expose
  rename/delete and mutable annotation operations. A profile's own
  descriptive settings are separate from persisted DVMDB project metadata.
- The top-bar project selector qualifies real projects as
  `Database / Project`, so duplicate names are not ambiguous.

### Left: Revisions

- **Full tree** shows currently loaded revisions (each once), nested by
  their first *loaded* parent when available. Other loaded merge-parent
  edges remain in the central graph.
- **Editable heads** shows the loaded revision objects referenced by
  branch HEADs. It does **not** grant historical model editing; the label
  describes branch heads, not a database write permission.
- For real DVMDB, abbreviated labels use the final twelve hexadecimal digits
  of revision UUIDv7s; full UUIDs remain the selection keys. The GUI does not
  yet fetch/store editable revision titles. SHA-256 is displayed separately
  as immutable record-integrity metadata. The source reader now traverses the paginated DVMDB P0
  revision graph **for the whole project**, including both merge parents and
  shared ancestry. Embedded DVMDB uses `revisionGraphV2()` where supported to
  display persistent branch and revision UUIDv7 identities, including the
  canonical record SHA-256 in the revision inspector. The HTTP graph now uses
  the same UUIDv7 and SHA integrity contract. Oversized or invalid responses fail the refresh explicitly
  instead of silently hiding older revisions.
- Selecting a historical revision from the graph reveals it in **Full tree**
  when required.

### Center: revision graph

- The existing graph supports **Top down / Bottom up** ordering,
  **Full tree / Editable heads** display, branch/head styling, selection,
  pan/zoom and **Fit graph**. Full-tree versus heads filtering and fold
  markers affect only the visual representation, not stored revisions.
- Checked real databases contribute graph nodes concurrently. Their node
  and branch labels include source/project context. Revisions from
  different sources are never deduplicated merely because their content
  hashes or project names match.
- Each branch UUIDv7 is shown as a **separate green branch-reference card**,
  connected by a green dashed reference link to its exact immutable HEAD revision.
  When DVMDB supplies an `originBranchId`, a distinct **blue dashed edge** links
  it to its creation-source branch reference. This is branch provenance,
  never a revision ancestry edge; branches without this durable metadata are
  not given guessed parents.
  Multiple branches can reference the same revision without stacking their
  labels inside its card, duplicating commits or inventing ancestry. Clicking
  a branch card selects that specific branch; branches stay visible in both
  Full tree and Editable heads modes. Solid edges are reserved for actual
  revision ancestry and database/project ownership.
- The renderer is a **QWidget/QPainter** surface. The map owns the
  process-global bgfx lifecycle; do **not** initialize another bgfx
  instance for a future GPU revision graph. A future renderer can share
  lifecycle correctly and retain this fallback.

### Right: contextual tabbed inspector

- The inspector is a closable, movable, floatable `QDockWidget` embedded in
  the Projects workspace. Its selected **object identity is always above the
  tabs**. A stable action footer applies to the selected object, not to a tab.
- **Database:** `Overview` (editable local profile metadata), `Connection`
  (local data directory / REST URL, status and Refresh catalogue) and
  `Projects` if projects are loaded. Only database-profile metadata is
  editable; there is no backend-side server revision node.
- **Project:** `Overview` (identity and read-only persisted metadata) and
  `Branches` if branches exist, listing each name with its current HEAD.
  The project inspector provides **Create branch** and the existing
  **Import network…** action. Import is not a DVMDB revision save yet.
- **Branch:** `Overview` (name/HEAD) and `Revisions` for reachable history.
  Selecting an item navigates to the same revision inspector/graph selection.
- **Revision:** `Overview` (immutable ID, ordered parents, named HEAD refs)
  and `Ancestry` when parents/HEAD references exist. Clicking a parent or
  branch reference navigates to the corresponding object. HEAD is
  branch-local, not necessarily a global leaf.
- **Creation:** Create project/branch replaces the inspector's tab content
  with an **inline, cancellable form**, rather than opening a `QInputDialog`
  or adding a permanent Creation tab. A successful write follows the
  existing authoritative refresh/selection flow. New *database profiles*
  retain their existing unsaved inspector form on the Connection tab.
- Real DVMDB projects/branches/revisions show immutable read-only metadata;
  unsupported backend rename/delete/annotation controls are not offered.
  Preview-only metadata and Delete remain local to RAM.
- Selecting a graph/tree object shows the same inspector and does not refit
  the camera. The inspector keeps its information tab when selecting another
  object of the same kind, and switches to Overview for a different kind.

## Bundled Example Library and EPANET importer

The Example Library is a collection of read-only bundled model templates.
**Open in editor** uses the existing importer to load an example into the
working model, with no database checkout or saved revision implied.

**Create preview project…** is available only when a demonstration profile
with the reserved preview ID is configured. It creates in-memory placeholder
project/branch/revision records and records the example's source as preview
provenance. It does **not** serialize the example's network to a new DVMDB
revision. Real databases never receive fictional seeded projects merely
because they were added. Users must not mistake a preview placeholder for
a stored or recoverable model snapshot.

## Missing working-copy and server contracts

The required future editor behavior is:

1. Discover networks in a project at an **immutable pinned revision**.
   Network enumeration is not yet exposed by the DVMDB HTTP service.
2. Load a chosen branch HEAD/revision into a separate mutable working
   copy, remembering the immutable base and target source/project/branch.
3. Track dirty state independently of the revision graph.
4. Save a validated network against the **exact expected HEAD**;
   preserve unsaved edits on a stale-HEAD/HTTP 409 or uncertain timeout.
5. Prompt Save / Discard / Cancel before switching away from dirty work.
   Historical revisions are initially read-only; a branch can fork from
   a selected historical revision.
6. Keep merges, graph-based cross-server transfer, bundle push/pull,
   annotations, lifecycle rename/delete, and permission checks explicit.
   None may be inferred from a visual graph gesture.

See the DVMDB repository's `docs/gui-integration-requirements.md` for the
remaining API contracts and `docs/dvmdb-http-api.yaml` for **implemented**
server endpoints. Do not add speculative REST routes to the GUI or claim
a security boundary that the daemon does not enforce.
