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

**Present implementation (Stage 2):** `MainWindow` owns two `MapModel`s. The distribution
monitor and editor each own a `MapWidget`, `MapBgfxSurface`, map-specific HUD
and interaction logic. Their left controls are now hosted by independent
`MapModeToolPanel` widgets (`src/tabs/map_mode_tool_panel.h/.cpp`), whose
ownership includes only the controls, scrolling and mode-selector widgets;
they never own map models, viewports or GPU resources. The legacy containers
still host these panels alongside their map stacks, preserving existing widget
hierarchy and behavior until the renderer migration. The workspace can discover
the selected panel through `activeToolPanel()` and containers expose
`toolPanel()` / `mapViewport()` as explicit separation points. `MapWorkspaceWidget` owns the pages and tab selectors,
but not their renderers. Only one map surface owns the process-global bgfx
device at a time; switching between distribution modes can stop and start it.

`MapWorkspaceWidget::activeMap()` and `activeModel()` provide *non-owning*
navigation access, returning `nullptr` for the currently empty drainage modes.
MainWindow uses these accessors to route footer zoom edits. They are also the
compatibility seam for giving the workspace one authoritative map in the
future without making MainWindow depend on which legacy page is active.

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

Do not merge these ownership changes into one large untestable patch. The tool-panel extraction is now complete (Stage 2). Next attach the monitor to the shared
viewport, migrate the editor, and finally remove obsolete sync/device-handover
logic. Fullscreen and WASM input should remain working at every intermediate
stage.

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

These diagnostics establish a baseline, not an assertion that the shared
viewport exists already. Once consolidated, a mode switch should increase
`workspace_mode_switch` but **not** `bgfx_device_started`,
`bgfx_device_stopped` or network geometry counters. Initial startup, a true
model change, or native surface loss may legitimately affect those counters.

Automated checks (including the new Qt Widgets panel ownership test):

```bash
./compile_linux_tests.sh --debug
ctest --test-dir build-linux-tests-debug --output-on-failure \
  -R 'aowis-map-(workspace|mode-tool-panel|camera-invalidation|retained-network-scene)'
```

The tool-panel test checks that each controls tree is independently parented,
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

The end state is **one persistent GPU renderer, one authoritative camera and
four selectable modes**; no CPU-era zoom buckets or geometry rebuilds tied
to fractional zoom are permitted.
