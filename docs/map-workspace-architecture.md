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

**Present implementation (Stage 3):** `MapWorkspaceWidget` now owns the
Distribution Monitor `MapModel` and hosts its **existing** map stack (including
`MapWidget`, `MapBgfxSurface`, and the native bgfx window) beside its independent
`MapModeToolPanel`. `MapWorkspaceViewportPage` performs this single, pre-show
transfer without recreating or reparenting the renderer surface itself.
The existing `MapMonitorContainer` survives as a hidden *behavior/controller*
widget in that page: its event filters, HUD, picking, symbology, fullscreen,
timers and signal connections remain connected to the same map and controls.
The controller is made the page's **first child** so its destructor can safely
clear map resolvers before the viewport is destroyed. The old controller
`QHBoxLayout` retains neither migrated widget.

`MainWindow` still constructs the two models for now, but immediately after
constructing the workspace the monitor model becomes its child. The Distribution
Editor still has its separate `MapModel`, `MapWidget`, `MapBgfxSurface`, tool panel,
and editor interaction/controller system. The editor page retains its existing
widget hierarchy. The drainage modes are empty placeholders.

**This is deliberately not the final one-renderer state.** The workspace's
`QStackedWidget` still hides the monitor viewport when showing the legacy editor
page. Because bgfx is process-global and there are still two GPU surfaces,
the normal editor/monitor switch continues to stop/restart bgfx until Stage 4.
Moving the monitor *once at construction* is safe; moving a live native surface
between pages during mode switches is explicitly prohibited.

`MapModeToolPanel` owns scrolling and the mode-selector UI but never owns a
map model, native window or renderer. Mode tools and the monitor viewport are
siblings in the workspace page. The controller currently retains the behavior
of the old monitor until its responsibilities can be extracted into a
non-widget controller when completing the single-viewport migration.

`MapWorkspaceWidget::activeMap()` and `activeModel()` provide *non-owning*
navigation access, returning `nullptr` for the currently empty drainage modes.
MainWindow uses these accessors to route footer zoom edits. They will point to
the same shared map for both distribution modes after the editor migrates.

**Intended shared viewport:** the Maps workspace exclusively owns one
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

Do not merge these ownership changes into one large untestable patch. The
tool-panel extraction (Stage 2) and the monitor viewport ownership transfer
(Stage 3) are complete. Next migrate the editor's rendering and interaction to
the workspace's monitor-origin viewport (Stage 4), then isolate mode-specific
HUD/symbology, and finally remove obsolete sync/device-handover logic.
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

These diagnostics establish a baseline. In Stage 3 the monitor's viewport is
workspace-owned, but there are still two GPU surfaces; device shutdown/start
counts on monitor/editor switches are still **expected**. Once consolidated,
a mode switch should increase
`workspace_mode_switch` but **not** `bgfx_device_started`,
`bgfx_device_stopped` or network geometry counters. Initial startup, a true
model change, or native surface loss may legitimately affect those counters.

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

Manual acceptance baseline before moving ownership:

1. Open Distribution Monitor; pan and zoom continuously, change basemap and
   2D/3D view; observe one consistent camera and no geometry rebuild during
   pure camera movement.
2. Switch to Distribution Editor; exercise entity selection, editing overlays,
   and fullscreen, and switch back. Compare device start/stop counters and
   geometry-preparation counters with the initial snapshot.
3. Repeat rapid monitor/editor switching, including with a large network.
   Check the HUD, editor tools, native-window focus, configurable F1–F4
   provider shortcuts and any camera synchronization setting currently used.
4. Open both drainage placeholders: neither may enter a distribution editing
   mode. Returning to Distribution Monitor must restore its controls.
5. Repeat on Linux desktop, Windows and WASM during each relevant migration.
   A unit test without a GPU cannot prove native-window behavior.

The end state, *not yet achieved in Stage 3*, is **one persistent GPU renderer, one authoritative camera and
four selectable modes**; no CPU-era zoom buckets or geometry rebuilds tied
to fractional zoom are permitted.
