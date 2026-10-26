# Projects workspace — current behavior and boundaries

**Updated 2026-10-26.** This is a reference for the *current* GUI. The
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
| Catalogue | Projects, branch names/heads and a bounded first-parent history page (100 revisions per branch), normalized across the two adapters. |
| Real writes | Project create and branch create/fork only. Subsequent catalogue reload confirms persisted results. |
| Preview | Explicit demo profiles and built-in example *placeholders* remain in-memory and cannot be mistaken for DVMDB revisions. |
| Graph | Source-qualified nodes for all checked real databases. QWidget/QPainter revision graph; no second bgfx instance. |

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
  workspace offers **Add database**. With sources but no currently visible
  loaded projects, it displays **No projects to display** rather than a
  fictitious network graph.

### Left: Projects & Branches

- Real projects are grouped by **database**, and branches appear beneath
  their project. Source-qualified IDs prevent a collision when two
  databases independently use the same project UUID or revision hash.
- **+ Project** writes a project to the selected available DVMDB source.
  **+ Branch** creates a branch in the selected project. Selecting a
  historical revision before branching passes that revision as the fork
  point, along with its origin branch. Project/branch creates reload the
  catalogue; an uncertain HTTP POST outcome is *not* blindly retried.
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
- For real DVMDB, current labels are generated from the first 12 characters
  of the immutable revision hash; the GUI does not yet fetch/store editable
  revision titles. The source reader loads up to the most recent 100
  first-parent revisions **per branch**, so older ancestors and second-parent
  lineage can be missing from the displayed graph. Do not interpret a
  missing edge as proof that histories are unrelated.
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
- The renderer is a **QWidget/QPainter** surface. The map owns the
  process-global bgfx lifecycle; do **not** initialize another bgfx
  instance for a future GPU revision graph. A future renderer can share
  lifecycle correctly and retain this fallback.

### Right: contextual inspector

- The inspector is a closable, movable, floatable `QDockWidget`, nested
  in the Projects workspace. Selecting a database/project/branch/revision
  or deliberately starting creation opens it. Closing it does not destroy
  a persisted database/profile; closing an **unsaved creation draft**
  cancels that draft.
- For a database profile, it edits **profile metadata**, endpoint or
  embedded path and shows source status. Connection type is immutable.
  **Refresh catalogue** belongs here, not in the narrow left sidebar.
- For real DVMDB projects, branches and revisions, the inspector shows
  immutable IDs/head/parents; field edits and Delete are unavailable.
  This is intentional until backend contracts exist.
- For explicitly selected **preview** objects only, editable titles,
  tags, descriptions and deletions affect RAM, not native or REST data.
  Preview labels and warnings distinguish that mode from real DVMDB.

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
