# Maps workspace architecture

## Modes

The **Maps** main-navigation page exposes four modes, in tab/page order:

| Mode | Domain | Interaction | Tools |
| --- | --- | --- | --- |
| Distribution Monitor | Distribution | Monitor | Available |
| Distribution Editor | Distribution | Editor | Available |
| Drainage Monitor | Drainage | Monitor | Empty placeholder |
| Drainage Editor | Drainage | Editor | Empty placeholder |

`src/tabs/map_workspace_mode.h` is the Qt-independent source of mode labels,
icons, domains, activities and tool availability. `MapWorkspaceWidget::Mode`
retains a Qt `Q_ENUM` for UI integration; compile-time assertions enforce
matching indices. The default mode is Distribution Monitor. Switching to a
blank drainage mode is valid and must not accidentally display distribution
networks or run distribution editing.

## Ownership and boundaries

**Current implementation (Stage 5):** `MapWorkspaceWidget` owns the single
Distribution `MapModel` and hosts the persistent `MapWidget` and
`MapBgfxSurface` in `MapWorkspaceViewportPage`. The renderer is constructed
by the monitor controller once; the editor receives non-owning pointers to
that same map and surface. The editor keeps its `MapCanvasWidget` as an
input-only, offscreen QWidget. It does not create another navigation widget,
native window or bgfx surface. Its canvas is resized with the live viewport
and receives forwarded pointer/keyboard/wheel events from the surface on
desktop or from the `MapWidget` event filter on WASM.

Distribution Monitor ↔ Editor changes only the visible sidebar in a nested
`QStackedWidget`, plus active navigation handlers, HUD actions, editor overlay
builders, network symbology and labels. The right viewport is never hidden
or reparented during these switches. `MapMonitorContainer` owns the GPU
lifecycle; `MapEditorContainer` supplies editor behavior and keeps its editor
callbacks until shutdown. The workspace explicitly destroys the editor
controller before the monitor's renderer to avoid dangling captured pointers.

The two drainage modes remain blank pages for now (Stage 7). Switching from
a distribution mode to a drainage placeholder can therefore hide and stop
the viewport; this is not a distribution editor/monitor mode restart.
Node decluttering uses two cached camera-independent 2D scene presentations:
monitor may separate coincident nodes while editor displays exact coordinates.
The variants share one retained geometry revision and each is prepared once
per revision (editor's variant is lazy). Mode switches reuse their preparation
IDs without recomputing positions. The inactive variant is discarded when
actual network geometry changes to avoid retaining stale large networks.
The current GPU mesh still follows the selected variant's preparation ID and
may upload on mode changes; this cache eliminates CPU derived geometry
preparation, not all presentation-dependent GPU work. 3D decluttering and
symbol-size changes can also require appropriate derived globe stream updates.

Each mode owns its visualization profile. Entering editor activates its
symbology, overlays, input handlers and blank monitoring legend/diagnostic
highlights. The monitor's model-selection and simulation-diagnostic signals
may change while editing, but cannot overwrite the editor's GPU state; the
latest selected entity and diagnostics are restored when returning to monitor.
The monitor reinstates its own HUD action handlers, labels and symbology.

`MainWindow` has only one distribution `MapModel`. Camera movement and
provider changes are inherently shared across distribution modes; the old
cross-view synchronization controls remain temporarily but no longer drive
second-model updates. Their removal and fullscreen/navigation cleanup belong
to Stage 6.

`MapModeToolPanel` owns scrolling and the mode-selector UI but never owns a
map model, native window or renderer. Mode tools and the monitor viewport are
siblings in the workspace page. The controller currently retains the behavior
of the old monitor until its responsibilities can be extracted into a
non-widget controller when completing the single-viewport migration.

`MapWorkspaceWidget::activeMap()` and `activeModel()` provide *non-owning*
navigation access, returning `nullptr` for the currently empty drainage modes.
MainWindow uses these accessors to route footer zoom edits. They point to the same shared map for both distribution modes.

**Shared viewport contract:** the Maps workspace exclusively owns one
`MapModel` (camera and provider), one `MapWidget` (continuous navigation), one
`MapBgfxSurface` (GPU resources, retained network, terrain, basemap and HUD),
and shared repositories are supplied from MainWindow. This hierarchy must
stay attached to its native window / browser canvas while modes change. Mode
switches will not call `bgfx::shutdown()` or `bgfx::init()`.

The following contracts guide extraction and prevent reintroducing two
renderers:

- **Tools:** each sidebar owns its controls and user settings, not the camera,
  native window, bgfx resources, or a duplicate renderer. Controls send
  requests to their mode controller.
- **Domain visualization:** distribution monitoring supplies simulation
  symbology, legends, and picking; distribution editing supplies edit styling,
  editor overlays, hidden-entity previews and selection. Drainage behavior
  remains empty until its model and renderer integrations are ready.
- **Input:** the shared viewport retains the existing map navigation and
  HUD-first event handling. The active mode gets the next chance to consume
  input. Unconsumed input reaches ordinary navigation; hidden controllers
  must not receive editing actions.
- **Rendering:** camera, imagery and HUD changes must not invalidate retained
  network geometry. Geometry revisions reflect changes to actual networks.
  Presentation changes may require derived display preparation (currently
  node decluttering) but must not force a network snapshot rebuild. Simulation
  values are visual data, not new geometry.
- **Lifecycle:** destruction/recreation of the native surface can still
  require an explicit bgfx device restart. Merely switching modes must not.

The tool-panel extraction (Stage 2), monitor viewport ownership transfer
(Stage 3), single-surface distribution renderer migration (Stage 4), and
presentation isolation plus 2D variant caching (Stage 5) are complete in the
source architecture. Further work removes legacy sync and fullscreen paths
(Stage 6) and integrates drainage modes (Stage 7).
Fullscreen and WASM input must remain working at every intermediate stage.

## Diagnostics and regression baseline

Start the application with:

```bash
AOWIS_MAP_RENDER_DIAGNOSTICS=1 ./build-linux/aowis-server-gui
```

`MapRenderDiagnostics` emits compact JSON snapshots at most every five seconds
while the map renders, **plus an immediate snapshot after each mode change**.
The immediate snapshot is taken after the synchronous QWidget changes; an
asynchronous bgfx restart may appear in the *next* snapshot. These are
cumulative process-wide counters, including both legacy bgfx surfaces.

| Counter | Meaning |
| --- | --- |
| `workspace_mode_switch` | Number and CPU-time distribution of actual mode changes (does not include identical selections) |
| `bgfx_device_started` | Successful bgfx backend and shader/GPU resource setup |
| `bgfx_device_stopped` | Shut down a previously running bgfx backend |
| `bgfx_2d_scene_prepared` | Successfully regenerated derived 2D network geometry, including declutter changes |
| `zoom_change`, `center_change` | Camera updates; independently of scene preparation |
| `globe_geometry_rebuilt`, `globe_flow_rebuilt` | Existing globe preparation diagnostics |
| `frames` | Submitted frames and their CPU time, **not** GPU execution time |

In Stage 4 the distribution viewport is shared: ordinary Monitor ↔ Editor
mode switches should increment `workspace_mode_switch` **without incrementing**
`bgfx_device_started` or `bgfx_device_stopped`. The derived
`bgfx_2d_scene_prepared` can increase once for a previously unprepared
node-declutter presentation. Subsequent editor/monitor switches on the same
network must not increase it. Real network geometry revisions prepare the
current presentation and invalidate the other one.
Initial startup, true network changes, switching to a blank drainage page,
and native surface loss can legitimately change renderer lifecycle counters.

Automated checks (including the new Qt Widgets panel ownership test):

```bash
./compile_linux_tests.sh --debug
ctest --test-dir build-linux-tests-debug --output-on-failure \
  -R 'aowis-map-(workspace|mode-tool-panel|camera-invalidation|retained-network-scene)'
```

The workspace viewport-page test checks that a monitor-style controller's
map stack and tools transfer to the workspace without reconstruction, that
fullscreen only hides controls, and that the controller is destroyed before
the map it references. It uses ordinary Qt widgets rather than initializing
bgfx. The tool-panel test checks that each controls tree is independently parented,
mode tabs remain outside the scroll viewport, scrollbar behavior matches the
old controls, and fullscreen hides only the requested panel. It runs through
the `aowis-tests` aggregate build target using Qt's offscreen platform.
The separate mode-contract test compiles without Qt or a GPU. The diagnostics
test uses Qt Core but does not create a window. The existing camera
invalidation and retained-network-scene tests cover zoom/LOD and geometry
revision independence. A real GPU/window lifecycle check additionally
requires interactive validation on the target system.

Manual Stage 5 acceptance checks:

1. Open Distribution Monitor; pan and zoom continuously, change basemap and
   2D/3D view; observe one consistent camera and no geometry rebuild during
   pure camera movement.
2. Switch to Distribution Editor; exercise entity selection, editing overlays,
   and fullscreen, and switch back. There must be no device restart. The
   second decluttering presentation can prepare once on the first switch;
   subsequent switches must not increment `bgfx_2d_scene_prepared` until the
   actual network geometry changes.
3. Repeat rapid monitor/editor switching, including with a large network.
   Check the HUD, editor tools, native-window focus, configurable F1–F4
   provider shortcuts and any camera synchronization setting currently used.
   Change the selected entity and simulation timeline while the editor is
   active: its overlays and blank monitor legend must persist, while the
   latest monitor selection/diagnostics reappear on switching back.
4. Open both drainage placeholders: neither may enter a distribution editing
   mode. Returning to Distribution Monitor must restore its controls.
5. Repeat on Linux desktop, Windows and WASM during each relevant migration.
   A unit test without a GPU cannot prove native-window behavior.

The distribution modes now share **one persistent GPU renderer and camera**,
with separate editor/monitor presentation settings and cached 2D display
geometries. A renderer shutdown while opening blank drainage placeholders is
still expected until the drainage modes use the same viewport (Stage 7).
The full four-mode shared-map end state is still pending Stage 7's drainage
integration. No CPU-era zoom buckets or geometry rebuilds tied to fractional
zoom are permitted.
