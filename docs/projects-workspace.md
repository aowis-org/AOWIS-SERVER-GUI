# Projects workspace — GUI-first implementation (2026-10-25)

## Scope and current state

The **first navigation tab** is now Projects. The GUI is intentionally a
**local, in-memory interaction prototype**: fictional sample projects, branches,
revisions, annotations, and graph relationships are not returned by the DB
server, stored in application settings, or saved to disk. New project creation,
rename, deletion, branch creation, tags, titles, and descriptions currently
change only those preview objects. A conspicuous `LOCAL UI PREVIEW` banner and
inspector explanation protect users from mistaking the preview for server data.

Previously supported EPANET import and built-in examples were moved out of the
toolbar into **Projects → Built-in examples** (under the Projects collapsible
section). Double-click or **Load example** uses the **existing** importer.
`examples_builtin_enable` still gates the examples list. Imported data is the
current editor network; **it is not a DB project/branch checkout**.

## Layout

- Toolbar: preview project selector, branch selector, session status, disabled
  future Save-to-revision action, working History action. The previous revision
  dropdown is removed. The selectors and Projects sidebar stay synchronized.
- Left pane: reusable `GroupBoxCollapsible` **Projects** and **Revisions**
  groups. Projects tree contains branches and built-in examples; project and
  branch actions work on the preview; revisions are separately selectable.
- Center: branch/revision DAG showing parent edges (not just first-parent
  history), branch lanes, revision title/short hash, HEAD badges, selection
  highlights, inertial drag/keyboard pan, smooth wheel/key zoom and fit-to-view.
  Navigation shares the map's configurable pan/zoom shortcuts, with no hover
  tooltip or instructional overlay obscuring graph nodes. A **Top down / Bottom up**
  toggle above the graph controls revision flow (default: Top down, oldest
  root above its descendants, newest HEAD at the bottom), reflecting only the DAG's vertical layout so labels and hit targets
  remain upright. **Fit graph** remains available, but the graph also fits
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
  action selects a project, branch, revision, or example, or otherwise changes
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
