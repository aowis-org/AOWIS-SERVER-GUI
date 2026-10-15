# Experimental production bgfx Map Monitor

The surface is integrated into the actual Map Monitor on desktop and WASM and
built by default (`AOWIS_ENABLE_BGFX_MAP=ON`). The desktop CPU (QPainter) map
monitor was removed: it displayed the network at the integer tile zoom and was
the source of step-wise zoom. Without a working GPU surface the monitor shows a
status message, never a CPU-rendered map. QRhi remains selectable
(`gui/map_desktop_renderer=rhi`) until the bgfx globe is done; the editor still
renders with QPainter until it moves to bgfx (roadmap Phase 2).

## Retained 2D network GPU drawing

* Inputs: `MapRetainedNetworkScene` and `MapBgfx2dScene` remain the
  authoritative network/geometry-revision sources.
* **Spatial chunks** (`map_bgfx_2d_mesh.h`): primitives are grouped by a fixed
  world grid (zoom-12 cells, ~9.8 km). Each chunk has its own local origin
  (float error ~0.002 px at zoom 19 for networks of any extent) and bounds
  for per-frame view culling. Off-screen chunks issue no draw calls.
* **Thick lines**: one indexed quad per segment, extruded in `vs_line_2d.sc`
  in screen pixels with square caps of half the width, which close polyline
  joins. Constant on-screen width at every zoom; a metre-based width is
  converted per frame with `MapBgfx2dChunking::pixelsForMeters()` (continuous
  in the zoom). GL/WebGL line primitives are not used (1 px on core/WebGL).
* **Nodes**: unit-corner quads scaled by a size uniform and cut to
  antialiased circles in `fs_marker_2d.sc`. Every snapshot node is drawn once,
  including demand points (entity `DemandPoint`); the attachment list only
  provides the attachment lines. (Before slice 4b demand points were also
  taken from the attachment list and drawn twice.)
* **Declutter** (`MapBgfx2dScene`): coincident nodes (pump/valve inlet and
  outlet digitized at one coordinate) are spread with the shared
  `computeNodeDeclutterOffsets()` at 1 m, same as QRhi and the globe. Link
  endpoints and demand-point attachments follow their node's offset. Links
  are kept as polylines so icons and chevrons are placed along the whole link.
* **Icons** (`vs_icon_2d.sc`, `fs_icon_2d.sc`): node icons for non-junction
  nodes and link icons (pumps, valves) at the middle of the link length, from
  `mapIconAtlasImage()` (premultiplied, full CPU-built mip chain, trilinear).
  Constant pixel size (px or metres, continuous in the zoom). The fragment
  shader is the QRhi icon shader's fill-mask logic. When icons are shown the
  node dot of an entity with an icon gets alpha 0.
* **Flow chevrons** (`vs_flow_2d.sc`, `fs_flow_2d.sc`): zoom-independent.
  Every link gets chevrons at fixed world positions centred on the link
  (pumps/valves at 30 %, the icon owns 50 %), organised as a hierarchy:
  position j has level `ctz(|j|) + base_level`, the centre is always shown.
  The vertex shader shows the levels whose on-screen spacing is >= 100 px
  (`MapBgfx2dFlow::thresholdLevel()` from the continuous zoom) and fades the
  next finer level in over one octave, so chevrons never pop at imagery level
  boundaries. At most 17 per link (long links use a coarser base level);
  none on links shorter than 18 px. One quad per chevron; the fragment shader
  draws the two strokes as an antialiased distance field. The direction is the
  sign in the attribute stream, so a timeline step never touches geometry.
* **Symbology** (`map_bgfx_2d_symbology.h`): geometry (vertex stream 0) and
  attributes (stream 1, per-chunk dynamic buffers: one ABGR colour per
  vertex; for chevrons colour + flow sign) are separate. Attributes are rebuilt
  from `MapNetworkRenderSymbology` only when the symbology changes (timeline
  step, results, palette, edits) or after a geometry upload; sizes (link
  width, node size, icon size, chevron size; px or metres) are per-frame
  uniforms. Rules match QRhi: links from `link_colors`; nodes, demand points
  and attachments from `node_colors`; missing entries are black; hidden
  junctions get alpha 0; icons use `icon_default_fill_color` unless node/link
  colourization is active (then the value colour, or the icon-unavailable
  fill); chevrons are black or white by link luminance.
* **Heatmap** (slice 4c, `fs_heatmap_2d.sc`): one spot per node (incl. demand
  points, at the decluttered position) with a heatmap value. No extra
  geometry: the heatmap pass draws the node marker quads with
  `vs_network_2d.sc`, the radius as size uniform and its own attribute stream
  (palette colour from `heatmap_fractions`, alpha 0 without a value). Radius in
  px or metres (metres converted from the continuous zoom, so it grows smoothly),
  opacity and solid centre are uniforms; same radial profile and premultiplied
  "over" compositing as QRhi. Culled with the radius as margin; skipped when no
  heatmap is selected or the opacity is 0.
* Layer order across all chunks: heatmap, attachments, links, chevrons, nodes,
  icons (the heatmap sits directly above the imagery, as in QRhi).
* Buffers are uploaded only on geometry revisions; camera movement changes
  only per-chunk uniforms. The debug overlay shows uploads and
  drawn/total chunks.
* 4x MSAA (`BGFX_RESET_MSAA_X4`, clamped by bgfx to renderer support) for
  antialiased line and marker edges.
* Shaders are compiled per renderer family and embedded in Qt resources
  (see "Renderer selection").

* **Picking** (slice 5, `map_bgfx_2d_picking.h`): `hitTest()` for click
  selection and the hover cursor, with the flat QRhi priorities and radii
  (icons max(12, size/2 + 6) px, nodes incl. demand points max(10, r + 5) px,
  links max(8, w/2 + 5) px), but against what bgfx draws: decluttered
  positions, link icons at the middle of the link length, sizes from the
  continuous zoom and the model's current camera. Hidden icons/junctions are
  not pickable. CPU pass in screen space on click/mouse move only.
* **Highlights** (slice 5): selection (0,190,255) and simulation errors (red,
  stale results grey) as in QRhi: wider copies of the entity drawn on top of
  links resp. nodes (link width + 2 px, + 6 px when the selected entity has an
  error; node radius + 2 / + 5 px). Small separate meshes built from
  `MapBgfx2dScene::filtered()` (exact drawn positions), rebuilt only when the
  selection, the error set, the prepared geometry or the junction visibility
  changes.
* **Node declutter toggle**: `setNodeDeclutteringEnabled()` re-prepares the
  scene; the GPU mesh, highlights and picking follow
  `MapBgfx2dScene::preparationId()` (no geometry revision needed).

## Native HUD (slice 6a)

QWidgets cannot sit on top of bgfx's native window, so the HUD is drawn by
bgfx itself, last in the map view (always on top), as one indexed draw per
frame:

* `map_bgfx_hud_canvas.h`: logical-pixel draw list (rects, rounded rects,
  circles, lines, atlas sprites) with premultiplied ABGR vertex colours and
  16-bit indices. Solid shapes sample one white atlas texel, so everything is
  one program, one texture, one draw.
* `map_bgfx_hud_text_cache.*`: whole text runs and images rasterized by Qt
  (QPainter, the widget font, the screen's logical DPI and device pixel
  ratio) into one 1024 x 1024 premultiplied RGBA8 atlas. Shelf packing; only
  the changed rectangle is uploaded. When the atlas is full it is cleared and
  the HUD frame is rebuilt once against the refilled atlas.
* `map_bgfx_hud.*` (pure Qt Gui, unit-tested in `tests/bgfx_hud_test.cpp`):
  state, layout and input. Same content, positions and sizes as the QWidget
  HUD: centred crosshair, GPS dot (red, black outline) at the continuous
  camera's projection, scale bar bottom left (sampled through the viewport
  centre from the continuous camera, label in the user's distance unit),
  download panels top centre (map tiles, terrain: busy bar, "n active ·
  m queued", Cancel stays disabled until fresh numbers arrive), 2D/3D switch
  top left (3D disabled until the bgfx globe exists), fullscreen button top
  right (desktop). Hit boxes come from the last build: clickable is exactly
  what was drawn.
* `map_bgfx_hud_gpu.*`, `vs_hud.sc`/`fs_hud.sc`: point-sampled atlas
  (texels on the physical pixel grid), premultiplied blend, transient buffers.

Input: every map mouse event goes to the HUD first
(`MapMonitorContainer::routeBgfxHudMouseEvent()`); a press on a HUD panel is
consumed together with its moves and release and never reaches navigation,
picking or hover. Button actions are delivered queued
(`setHudActionHandler()`), outside the native window's event dispatch,
because fullscreen reparents the map. Desktop cursors are set on the
embedded native window (`setHoverCursor()`), the only cursor visible there.

Fullscreen reparents `map_stack` like the QRhi path. If the platform destroys
and recreates the native window meanwhile, the surface shuts bgfx down on
`SurfaceAboutToBeDestroyed` and restarts on the new surface
(`QPlatformSurfaceEvent`).

bgfx's statistics text overlay would cover the HUD; it is shown only with
`AOWIS_BGFX_DEBUG=1`.

**Not yet implemented:** map legend HUD (slice 6b), labels, hidden-entity
filter (`setHiddenEntityUuids`) and network screen translation (editor
features, Phase 2), terrain/globe.

## 2D imagery

* `map_bgfx_2d_imagery_layout.h` (pure C++, unit-tested): the imagery level is
  `round(continuous zoom)`, but quads are positioned with the *continuous*
  scale, so imagery stretches smoothly between levels and never steps. The
  test asserts continuity of a world point's screen position across x.5.
* A missing tile is covered by the nearest resident ancestor's sub-rectangle
  and by resident children on top; never a hole while tiles stream in.
* `map_bgfx_2d_imagery_gpu.*`: one RGBA8 texture per tile, LRU budget of 384,
  at most 8 uploads per frame (nearest first), one transient quad per tile.
* Requests go to the shared `MapTileRepository` only when the visible tile set
  changes or a failed tile becomes retryable. Provider switches and deleted
  tiles clear the texture cache.
* The "slide opacity" fades the imagery towards the background colour in the
  imagery shader.
A single scene-local origin is sufficient for regional water networks, but is
not a final worldwide-precision architecture. Spatially grouped buffers with
per-batch origins and culling are still mandatory for larger extents.

Do not reintroduce integer-zoom geometry caching on this new GPU path. Imagery
LOD will remain independent from retained network geometry.

## Renderer selection

bgfx selects the renderer (`RendererType::Count`: platform ranking with
fall-through on init failure). AOWIS does not reimplement that; it only:

* compiles in the renderers it serves (`AOWIS_BGFX_RENDERERS`,
  `cmake/aowis_bgfx.cmake`): Linux OpenGL + Vulkan, Windows Direct3D 11 +
  OpenGL, macOS Metal, WASM OpenGL ES 3 (WebGL 2);
* embeds the matching pre-compiled shader families
  (`:/aowis/map/bgfx/<glsl|essl|spirv|dx11|metal>/`, see "bgfx build");
* rejects a pick without embedded shaders and retries with the next supported
  renderer that has them (`map_bgfx_backend.h`);
* honours `AOWIS_BGFX_RENDERER=opengl|vulkan|d3d11|metal` for diagnosis
  (environment only, not an ini option).

A software rasterizer (llvmpipe / WARP) is not selected automatically yet.

## bgfx build

AOWIS does not use bgfx.cmake. `cmake/aowis_bgfx.cmake` builds bx, bimg and
bgfx as three upstream unity TUs with only `AOWIS_BGFX_RENDERERS` compiled in,
no video, no software texture decoders (so no ASTC encoder), single-threaded.
Linux needs no extra link libraries (bgfx dlopen()s EGL/GL/wayland-egl/Vulkan).
Measured on Linux (OpenGL + Vulkan): 3 TUs / ~55 s CPU instead of 76 TUs / ~262 s.

The submodules are shallow and sparse (`tools/bgfx_sparse_checkout.sh`):
~15 MB of bgfx instead of ~270 MB.

**Shaders are pre-compiled and committed** in `src/map/bgfx/shaders/bin/
<family>/` (`cmake/aowis_bgfx_shaders.cmake`), so neither desktop nor WASM
builds build a shader compiler (bgfx.cmake's shaderc was ~690 TUs incl.
Tint/Dawn). Each family has `inputs.sha256` over all `.sc` files,
`varying.def.sc`, bgfx's `bgfx_shader.sh`, profile and platform (line endings
normalised). Editing a shader re-runs CMake; a stale family is never embedded:
the configure step stops with the regeneration command.

After editing a `.sc` file, or after updating bgfx:

    ./compile_linux.sh --shader-tools

builds `aowis-bgfx-shaderc` (`cmake/aowis_bgfx_shaderc.cmake`: shaderc
without Tint/Dawn/DXC/spirv-reduce, ~280 TUs, once), recompiles glsl, essl,
spirv and metal into the build tree and copies them plus `inputs.sha256` into
`src/map/bgfx/shaders/bin`. Commit that directory. dx11 (DXBC) can only be
compiled on a Windows host; without current dx11 binaries a Windows build
warns and runs on OpenGL.

## Input

Desktop bgfx runs in an embedded native `QWindow`, which receives all input
over the map. Keys, wheel and mouse are forwarded to MapWidget (mouse via
`QCoreApplication::sendEvent`, so the container's click-selection filter still
runs). The Wayland orbit pointer lock targets the native window's own
`wl_surface` (`MapWidget::setOrbitPointerLockWindow`). Frames are not rendered
while the native window is unexposed, so the vsync swap cannot block the GUI
thread on a hidden Wayland surface.

## Frame clock and smooth zoom (do not regress)

* **One clock.** Once bgfx is active, `MapBgfxSurface` is the only navigation
  clock: each presented frame calls `MapWidget::advanceNavigationFrame(dt)`
  *before* sampling `MapModel::cameraViewState()`. MapWidget's own 16 ms pan
  timer is not started (`setNavigationFrameDriver(true)`). Two independent
  timers plus the vsync-blocking `bgfx::frame()` on the GUI thread advanced the
  camera by 0, 1 or 2 steps per displayed frame (simulated: ~28 % frozen
  frames with only 0-3 ms dispatch jitter). That was the keyboard-zoom judder.
* **Paced dt.** `MapFramePacer` quantizes measured frame time to whole refresh
  periods with carry, so a vsync-locked stream gets exactly one period per frame
  and camera time never drifts from wall time by more than half a period.
* **Desktop** uses a PreciseTimer 1 ms shorter than the refresh period; the
  vsync swap then locks presentation. **WASM** uses
  `emscripten_request_animation_frame_loop` (browser vsync), never QTimer.
* **Zoom motion** (`map_zoom_animation.h`): wheel, buttons and keys only move a
  target; the camera follows it as an exactly integrated critically damped
  spring. Velocity is continuous on press, tap->hold, reversal and release;
  results are frame-rate independent. No integer zoom anywhere in navigation.
* The camera uses **logical** pixels (`logicalSize()`); the view rectangle uses
  physical pixels. Using physical pixels for the camera was one octave off at
  devicePixelRatio 2.

## Build/test

Native Linux: `./compile_linux.sh` (GUI) and `./compile_linux_tests.sh
--release` (tests). `./compile_linux.sh --shader-tools` additionally
regenerates the committed shaders (see "bgfx build").

WASM: `sudo ./compile_wasm.sh`. It embeds the committed essl shaders; the
Docker image no longer builds a shader compiler. `AOWIS_ENABLE_BGFX_MAP=OFF`
disables the bgfx path.

User validation is still required for both desktop and WASM shader compilation,
linking, GPU draw behavior, and steady-state GPU-upload counter behavior.
