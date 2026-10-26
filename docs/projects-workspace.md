# Projects workspace — GUI-first implementation (2026-10-25)

## Scope and current state

The **first navigation tab** is now Projects. The GUI is intentionally a
**local, in-memory interaction prototype**: fictional server profiles, sample projects, branches,
revisions, annotations, and graph relationships are not returned by the DB
server, stored in application settings, or saved to disk. New project creation,
rename, deletion, branch creation, tags, titles, and descriptions currently
change only those preview objects. A conspicuous `LOCAL UI PREVIEW` banner and
inspector explanation protect users from mistaking the preview for server data.

EPANET **Import** stays in Projects. Bundled EPANET examples are **read-only
project templates**, not revisions or projects on any configured server. The
separate **Browse built-in examples…** button opens the searchable **Example
Library**. **Open in editor** emits the existing importer signal and loads the
selected sample as the current working model, **not a DB checkout**.
**Create project from example…** opens a destination wizard (server profile,
project name, first branch, tags and description). It creates an in-memory
project with an explicitly marked initial-revision **placeholder** and records
the template resource as provenance. **No network data is copied; no DB call
or connection is made; no persistent project is created**. The project and
revision inspector disclose this. This preview exercises future creation and
branching UX only. `examples_builtin_enable` gates the library entry point.
The bundled sample is never mutated and can be reused to create arbitrarily
many independent projects, including projects on different server profiles.

## Layout

- Toolbar: preview project selector, branch selector, session status, disabled
  future Save-to-revision action, working History action. The previous revision
  dropdown is removed. The selectors and Projects sidebar stay synchronized.
- Left pane: a new top `GroupBoxCollapsible` **Servers** section with a
  server-profile list and a **+ Server** action. Server renaming uses the
  inspector's Name field, and server deletion is available only from its
  inspector (with confirmation). Selecting a
  server chooses the scope of the project and revision trees and the current
  preview graph. The default profiles **Local preview** and **Remote preview**
  are fictional and are never contacted. Adding, renaming, or deleting profiles
  affects RAM only. Deleting a preview server also removes its in-memory sample
  projects after confirmation. Server inspector fields include name, endpoint
  URL, authentication method (choice only, no credentials), tags, and description;
  nothing is saved to application settings or a DB. The selected server is
  highlighted in its own tree even when inspecting a project or branch.
  Project/branch toolbar selectors list the currently selected server's
  projects. The separate Example Library remains available independently of
  the selected server profile.
- Under Servers, reusable `GroupBoxCollapsible` **Projects & Branches** and
  **Revisions** groups. Projects tree contains only projects and branches;
  project and branch actions work on the preview. The sidebar has no
  redundant Rename/Delete buttons: the inspector's Name field renames the
  selected server, project or branch, and a contextual Delete action handles
  preview removals with confirmation. Historical revisions cannot be deleted
  from the inspector. The Revisions group starts with its view switch, without
  an instructional caption. The bundled examples instead
  have a full-width library entry separate from the server hierarchy. The Revisions section offers
  a two-state **Editable heads / Full tree** switch (Full tree by default).
  Both modes display **revision titles only**, never IDs or hashes. **Full tree**
  nests each revision under its first known parent, including unreferenced
  historical revisions; a merge revision appears once in this sidebar and all
  its parent edges remain visible in the center DAG. **Editable heads** shows
  one entry for each distinct revision currently referenced by a branch HEAD.
  Branch heads are movable pointers: their underlying revision snapshots are
  still immutable. Graph selection of a historical revision switches the list
  to Full tree so that it can be highlighted. Filtering an already selected
  historical revision down to heads selects its branch's head instead.
- Center: branch/revision DAG showing parent edges (not just first-parent
  history), branch lanes, revision title/short hash, HEAD badges, selection
  highlights, inertial drag/keyboard pan, smooth wheel/key zoom and fit-to-view.
  Navigation shares the map's configurable pan/zoom shortcuts, with no hover
  tooltip or instructional overlay obscuring graph nodes. A **Top down / Bottom up**
  toggle above the graph controls revision flow (default: Top down, oldest
  root above its descendants, newest HEAD at the bottom), reflecting only the DAG's vertical layout so labels and hit targets
  remain upright. The canvas additionally has its own **Editable heads / Full
  tree** segmented control (without a redundant "View:" label), independent of the left sidebar. The active mode
  is highlighted and **Full tree** is the default. **Editable heads** keeps
  branch HEADs and their nearest shared branch ancestors visible, with
  dashed, clickable folded-history markers on the connections between them.
  The branches stay visibly connected through their common ancestor even when
  their intermediate histories are folded. Each marker shows the number of
  hidden revisions; clicking it reveals that path without expanding unrelated
  branches. Switching from Full tree back to Editable heads, or clicking the
  active Editable heads segment again, collapses previously unfolded paths.
  A project/DAG change also clears per-path expansion state. A historical revision selected before
  filtering is replaced with its branch head so there is no invisible active
  selection. Both orientations preserve the fold markers, edges, and hit targets.
  **Fit graph** remains available, but the graph also fits
  automatically on first display (after the dock/splitter geometry settles), when a
  different project is loaded, or when revision topology changes. Ordinary
  selections and dock resize preserve the user's camera. Selecting a node selects its revision
  in the left list and inspector. Selecting a branch pill selects that branch
  and updates the toolbar. The viewport is a **renderer-neutral QWidget with
  a QPainter implementation for now**, not a second bgfx renderer.
- Right pane: context-aware project/branch/revision metadata inspector, hosted
  in a real closable, movable, floatable `QDockWidget` on the right of the
  Projects workspace. Its title-bar close button hides only that inspector;
  there is **no toggle above the graph**. Closing it leaves it hidden until an
  action selects a project, branch, or revision, or otherwise changes
  the inspector content. Selection from the tree, graph, or toolbar, and the
  explicit History action, automatically restore it. The graph uses the
  newly available space without resetting the user's camera. This nested dock
  host is deliberately separate from the map's
  main-window docks. Fields include display name/title, comma-separated tags,
  description, immutable revision identity, ancestry, and author/timestamp
  preview data. Edits update the in-memory tree/graph; revision annotations
  never mutate an immutable model snapshot.

The existing geographic bgfx surface owns a process-global bgfx instance and
its presentation loop. **Do not initialize another bgfx instance in the DAG
widget**. A future GPU version must share lifecycle and separate render
views/framebuffers correctly, be optional, support WASM and desktop, and keep
a QWidget fallback for disabled/unavailable GPU acceleration. The renderer-
independent `ProjectGraphNode` structure and `revisionActivated` /
`branchActivated` signals are the boundary for that later work.

## Accepted interaction contract for database integration (not implemented)

1. Opening a branch loads its current HEAD snapshot and records the exact
   immutable base revision separately from the mutable working copy.
2. Editing marks the working copy dirty; no stored revision changes in place.
3. Saving creates a new immutable revision using `expectedHead`, with a
   description and metadata. A stale HEAD must not be overwritten.
4. Historical revisions initially open read-only; create a branch to continue
   work from one. Editing the inspector's *annotation* is separate from editing
   the model snapshot.
5. Switching project/branch with dirty edits prompts Save / Discard / Cancel.
6. A concurrent update/HTTP 409 leaves local edits intact and offers a compare
   / review / merge path; never silently retries or replaces another HEAD.
7. Branch merge is a dedicated conflict-resolution workflow, not an implicit
   save action.
8. Project/branch rename/delete and metadata changes require explicit server
   capabilities and permissions, with confirmation, optimistic concurrency,
   and clear UX for dependent branches, revisions and network references.

## Future integration seams / remaining TODOs

- A central application-owned ProjectSession: repository/server, project UUID,
  branch stable identity/name, checked-out HEAD/base revision, loaded network
  UUID, dirty state, and optional historical revision. Notify both toolbar and
  Projects pane from one authoritative state (not their widgets).
- Project/branch/revision navigator backed by paginated, authenticated service
  queries. Fetch a complete parent DAG as needed; API `history` is first-parent
  only. Maintain stable IDs and avoid creating phantom nodes for unpaged data.
- Save and load through a dedicated DB service adapter; keep network editor and
  persistence concerns separate. Initially use validated full-network save.
- Safe branch creation from selected revision and independent mutable revision
  annotations, rich tags, review state and provenance.
- Future compare, dry-run merge, conflict resolution, offline bundle flows,
  background loading/cancellation, errors and disconnected/retry states.
- Native bgfx graph rendering once the shared renderer lifecycle permits it.

Server-side contract gaps and implementation priorities are tracked in the
**DB repository** under `docs/gui-integration-requirements.md`. The DB OpenAPI
contract remains unchanged until endpoints actually exist.

## Future multi-server graph / distributed workflows (not implemented)

The graph currently displays **one selected project on one selected preview
server**. This is intentional; the server selector is the first step toward
multi-server exploration, not a claim that push/pull/merge is implemented.
For the later integration phase:

1. Make a stable server profile identity independent from display name and URL.
   Identify a graph object with a composite **(server, project, revision)**
   identity; commit hashes alone are not a safe cross-server key. Use server
   lanes or swimlanes, distinct server headings, and optional server-visibility
   controls so multiple repository DAGs can be viewed at once. Fetch each
   source independently and clearly mark offline/stale results.
2. Resolve repository equivalence using explicit IDs/origin mappings (not
   coincidentally identical project names). Cross-server ancestry may be shared,
   unrelated, or only partially present. Display transfer relationships and
   divergence without inventing common ancestors or deduplicating unrelated
   commits. Continue to support Full tree / Editable heads and fold markers
   per repository, including the real visible merge base.
3. Introduce explicit **Fetch / Pull / Push** previews with source, destination,
   direction, authentication identity, destination branch, expected HEAD,
   ahead/behind summary, conflict policy and a confirmation step. Graph gestures
   can propose a transfer; they must never commit one silently. Pull means
   fetch plus an explicit integration choice, not automatic overwrite.
4. **Merge** should compare parent DAGs and immutable snapshots, offer a dry-run,
   show entity-level conflicts and authorization checks, and use optimistic
   concurrency. A merge across servers requires data transfer/identity mapping
   first; it is not an ordinary same-repository merge.
5. Store credentials in an OS credential manager, never in GUI preview objects,
   URLs, plaintext configuration, or logged diagnostics. Check certificate
   trust/TLS, authentication, server capabilities, API versions, permission
   scopes, pagination and offline behavior before exposing server operations.
   Keep the GUI functional when a server is unavailable.
6. Keep server selection (active editor/checkout) separate from visibility
   selection (which servers are drawn). The inspector must show unambiguous
   server/repository origin for each selected branch/revision. Add backend
   support for multi-remote discovery and push/pull/merge only after contracts
   and conflict handling are defined. Do not silently couple the currently
   edited working copy to a remote server just because it is visible in the DAG.

These are implementation requirements for the subsequent integration phase,
not features supplied by the current GUI prototype.

## Example Library and eventual local/remote storage

The Example Library is the entry point for bundled network templates. It has
search, descriptions, keyboard/double-click open, and two explicit outcomes:

- **Open in editor:** import a bundled resource via `builtinExampleRequested`
  into the editor's working copy. The existing import implementation is reused.
  The action does not imply a DB checkout or create a project.
- **Create project from example:** choose **any configured server**, a name,
  first branch (default `main`), tags, and description. At present the wizard
  creates **only preview project/branch/revision records**, even for the local
  preview server. The source resource path is kept as provenance; the network
  content itself is not persisted, and the placeholder is not an actual DB
  revision. The original bundled example remains unchanged.

During DB integration, replace the preview creation with a validated operation
that imports the example's hydraulic network into the target server's project
and creates its initial immutable revision. Local embedded DB and external
remote DB must use the same application-level creation flow. Users must be
able to create multiple independent copies, change target servers, and then
branch/revise as usual. For models already edited in the working copy, provide
**Save current model as a new project** (separate follow-up; do not silently
re-import the original template). Preserve example provenance as optional
metadata rather than as a fake branch or pseudo-server. Use the working-copy
and change-tracking/session architecture for later commits; these preview
controls must not suggest that they saved anything remotely.
