# Experimental production bgfx Map Monitor

The surface is integrated into the actual Map Monitor on desktop and WASM and
built by default (`AOWIS_ENABLE_BGFX_MAP=ON`). The desktop CPU (QPainter) map
monitor was removed: it displayed the network at the integer tile zoom and was
the source of step-wise zoom. Without a working GPU surface the monitor shows a
status message, never a CPU-rendered map. bgfx is the default renderer on
desktop and WASM. QRhi remains selectable (`gui/map_desktop_renderer=rhi`,
`map_wasm_renderer=rhi`) for the monitor until the bgfx globe is done. The
map editor draws with bgfx only (roadmap Phase 2); the QPainter editor
(`MapEditorRenderer`), its QRhi overlay path and `MapWidget`'s CPU tile
painting were removed. The JavaScript/WebGL map renderer (monitor and
editor) was removed.

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

## On-demand rendering

The frame loop (desktop: a timer slightly faster than the display; WASM:
`requestAnimationFrame`) keeps ticking for the navigation clock, but a tick
presents a frame only when something visible changed: the camera
(`mapCameraUpdate()` against the last presented frame), the size or device
ratio, a call of any surface setter or `requestRedraw()` (the editor overlay's
content), an arriving tile, an expose; or while something animates by itself
(`MapBgfxHud::animating()`: download busy bars, a tooltip inside its delay;
tile uploads over the per-frame budget; deferred labels). An idle map costs
an early return per refresh instead of a full frame - in the browser it no
longer keeps a core busy. Plain pointer moves over the map present nothing;
only HUD hover feedback does. Unchanged download numbers (the monitor polls
every 400 ms) present nothing either.

Around the surface: `MapWidget` neither repaints nor scrolls its backing store
while a GPU surface is active (a full-size copy and flush per pan step), and
the footer's map readouts are coalesced to at most one update per 100 ms
(each label change relayouts the status bar). WASM builds drop the function
name section by default (`AOWIS_WASM_FUNCTION_NAMES=ON` to keep it).

## Native HUD (slices 6a, 6b)

QWidgets cannot sit on top of bgfx's native window, so the HUD is drawn by
bgfx itself, last in the map view (always on top), as one indexed draw per
frame:

* `map_bgfx_hud_canvas.h`: logical-pixel draw list (rects, rounded rects,
  circles, lines, atlas sprites) with premultiplied ABGR vertex colours and
  32-bit indices. Solid shapes sample one white atlas texel, so everything is
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
  what was drawn; boxes of a popup closed since then are ignored.
* Map legend (slice 6b), bottom right, like `EntityMapLegendHud`: node /
  link / heatmap metric combos (list opens on press, below or above the
  combo and tall enough for every entry; only when neither side has room it
  scrolls, with a scroll bar: drag the thumb, click the track to page, wheel),
  colour ramps (gradient through the palette stops, 7/5/3 value labels that
  never collide, "uniform", "No finite values"), the hover value swatch, the
  palette menu (Sequential / Diverging, preview + Flip per palette, Reset)
  and QToolTip-style tooltips after 0.7 s. A press outside an open popup
  closes it and is swallowed; Escape and focus loss close it too.
* `map_bgfx_legend_content.*` (application side): the legend's entries,
  ranges in the displayed unit, palette stops and defaults, computed exactly
  like `EntityMapLegendHud` from the symbology settings and the ranges
  `applySymbology()` already has. While the bgfx map is active the legend
  dock stays hidden; a HUD choice goes through the same container paths as
  the left menu and the dock (`setNodePalette()`, `applySymbology()`).
* `map_bgfx_hud_gpu.*`, `vs_hud.sc`/`fs_hud.sc`: point-sampled atlas
  (texels on the physical pixel grid), premultiplied blend, transient buffers.

Input: every map mouse event goes to the HUD first
(`MapMonitorContainer::routeBgfxHudMouseEvent()`); a press on a HUD panel is
consumed together with its moves and release and never reaches navigation,
picking or hover. Wheel events over a HUD panel scroll an open list or are
swallowed (`handleHudWheelEvent()`), never zooming the map underneath.
Button and legend actions are delivered queued as `MapBgfxHud::Command`
(`setHudActionHandler()`), outside the native window's event dispatch,
because fullscreen reparents the map. Desktop cursors (pointing hand, cross
over ramps) are set on the embedded native window (`setMapCursor()`), the
only cursor visible there.

Fullscreen reparents `map_stack` like the QRhi path, but shuts bgfx down
first (`suspendForNativeWindowChange()`) and restarts it once the native
window is exposed again (`resumeAfterNativeWindowChange()`). Qt Wayland
destroys the embedded window's wl_surface on reparent without a
`QPlatformSurfaceEvent`; a Vulkan swapchain still alive on it ends the whole
Wayland connection ("wp_linux_drm_syncobj_surface_v1: Surface got destroyed
already"). `QPlatformSurfaceEvent` handling remains as the fallback for
unplanned native-window recreation.

bgfx's statistics text overlay would cover the HUD; it is shown only with
`AOWIS_BGFX_DEBUG=1`.

**Not yet implemented:** terrain/globe.

## Map labels

Text on the map next to the network entities, like EPANET's *Map Options ->
Notation*: node IDs, link IDs and (monitor) the values of the metric the map
is coloured by, in the legend's unit (`MapLabelSettings`,
`map/render/map_label_settings.h`; switches under "Labels" in the left
menu's `MapNavigationWidget`, all off by default).

* `map_bgfx_labels.*` (pure Qt Gui, unit-tested in
  `tests/bgfx_labels_test.cpp`): greedy, decluttered screen-space placement.
  Candidates are the entities on screen at their drawn positions (links: the
  middle of their visible length; pipes shorter than 24 px get none). Every
  on-screen node symbol and device icon is an obstacle; labels are tried in
  priority order (tanks/reservoirs, pumps/valves, junctions, pipes, demand
  points; ties by render id, stable while panning) at up to four positions
  around their symbol, and kept only where they overlap neither a label nor
  a symbol and lie inside the viewport. A 48 px grid keeps each test local.
  Above 30000 on-screen nodes only tanks, reservoirs, pumps and valves are
  labelled.
* Text: `MapBgfxHudTextCache::haloText()` rasterizes each run once as one
  colour sprite, dark text with a white outline (`QPainterPath` stroke +
  fill), readable over imagery and lines; node labels near black, link
  labels dark blue. ID and value are two centred lines. The labels' own
  2048 x 2048 atlas is created on first use; placement stops before one
  frame's labels would exceed 70 % of it, and at most 160 new runs are
  rasterized per frame (the rest keep their slot and appear in the next
  frames).
* `MapBgfxSurface::renderLabels()`: drawn after the network and the editor
  overlay, before the HUD, with the network's camera (the editor's
  full-network move included); rebuilt only when the camera, the viewport,
  the prepared scene, the atlas or the content (settings, symbology, value
  formats, hidden entities, renames) changed. Values: canonical metric values
  by render id in `MapNetworkRenderSymbology::node_values / link_values`,
  converted with `mapBgfxLabelValueFormat()` (the legend ramp's affine unit
  conversion). Symbol boxes: the GPU network's dots and icons (monitor), the
  editor's bottom-anchored / centred icon rects (`setLabelSymbolRects()`).
* Renames change no geometry: `HydraulicData::setNodeId()` & co. rebuild the
  current render snapshot (same render ids), the retained scene takes the
  IDs over on the visual-revision update, and the containers re-send the
  snapshot on `signalNodeChanged` / `signalLinkChanged` /
  `signalDemandPointChanged`.

## Map editor (Phase 2)

The Map Editor uses a second `MapBgfxSurface`, and only bgfx. It draws the
imagery, the static network in the editor look (black pipes, brown device
links, black 10 px node dots: `applyRenderSurfaceEditorSymbology()`), every
editor overlay, map labels and the HUD (crosshair, GPS dot, scale bar, the
2D/3D switch, the fullscreen button on desktop). `MapCanvasWidget` is the input
owner (editor controller first, then MapWidget navigation) and paints
nothing. There is no fallback renderer: until the first frame, and when bgfx
cannot start (or is not in the build), a status label takes the map's place,
as in the monitor.

* `map_bgfx_editor_overlay.*` (pure Qt Gui, unit-tested in
  `tests/bgfx_editor_overlay_test.cpp`): port of the former QPainter editor's
  `paintRenderSurfaceOverlay()` into a HUD-kit draw list,
  rebuilt per frame from `MapEditorVisualState` with the continuous camera:
  tile-cache and rectangle selection frames, full-colour icons (nodes
  bottom-anchored at the rounded position, device links centred), pipe
  vertex dots, demand-point attachments (dashed, with handles), selection,
  connection target, simulation errors, pipe / device-link rubber bands, the
  floating marker and the move preview. Snapshot geometry is projected to
  Web Mercator once per geometry revision; icons are culled before they are
  requested.
* `map_bgfx_editor_icons.*`: the icon provider. Each (pixmap, width,
  highlight) is composited once at the device pixel ratio with the glows of
  `MapEntityPixmapRenderer::paint()` into the overlay's own 2048 x 2048
  atlas, then drawn as one quad.
* Cost rules (networks with many demand points): the editor's visual state
  (`MapCanvasWidget::visualState()`, O(demand points)) is rebuilt only when
  the editor's content changes, never per camera frame
  (`signalCameraChanged()` only refreshes the pointer's map coordinate) and
  never while the editor is hidden (synced map movement); pipe and device
  link endpoints come from the markers' uuid index
  (`MapCanvasMarkers::findMarker()`), not from a hash of all markers built
  per pipe or per demand point (that was quadratic: seconds per zoom level
  change); the overlay's icons are memoized per (entity, width, highlight)
  and atlas generation; zoom changes do no editor symbology work.
  Per frame the overlay culls demand-point attachments against the viewport,
  clips their dashes to it and draws handles only where the line is at least
  12 px long (or selected / edited); pipes with vertices are culled by their
  cached map bounds before projection. Per pointer move the hover hit tests
  reject markers and pipes outside a map-coordinate box around the pointer
  (`MapCanvasHitBox`) before projecting them or looking up pixmaps.
  Dashed lines are textured quads over a dash-pattern strip in the overlay
  atlas (one quad per 512 px instead of one per dash), attachment handles
  are one disc sprite each, circles and lines go straight into the draw
  list (no temporary outlines), and an unchanged frame (same camera, no
  editor change) reuses the last overlay canvas. `compile_linux.sh` builds
  RelWithDebInfo by default (`AOWIS_BUILD_TYPE`): without a build type
  CMake compiled without optimization.
* Move preview: dragged entities are hidden in the GPU network
  (`setHiddenEntityUuids()`: alpha 0 in the colour stream, no geometry
  upload) and drawn by the overlay; a full-network move shifts the network
  by the camera (`setNetworkScreenTranslation()`), the imagery stays.
* The overlay is drawn between the network and the HUD with a second
  `MapBgfxHudGpu`. HUD canvases use 32-bit indices; bgfx's transient buffers
  are raised to 16 MiB vertices / 8 MiB indices (`Init::limits`) for one icon
  quad per visible node.
* Input: desktop forwards the native window's events to the canvas
  (`sendEvent`, same geometry); keyboard focus stays on the canvas
  (`setKeyboardFocusWidget()`: the native window does not take focus), so
  the editor's window shortcuts keep working. WASM: the canvas is on top of
  the Qt stack and receives the pointer directly. Either way the container's
  event filter on the canvas gives the HUD every pointer event, wheel and
  Escape first (`routeBgfxHudEvent()`); a HUD button's pointing hand wins
  over the tool's cursor.
* Fullscreen (desktop): the HUD button and the map fullscreen shortcut
  (`MapMonitorFullscreen`, parented to each map stack, so only the visible
  map's fires) reparent the editor's map stack exactly like the monitor
  (suspend bgfx, top-level window, `showFullScreen()`, resume). The left
  menu is then in another window, so its buttons' window shortcuts cannot
  fire; the canvas' key presses try them first
  (`MapEditorMenuWidget::triggerButtonShortcut()`), as the shortcut map would
  have.

One bgfx device per process belongs to the visible surface: hiding a map
(tab switch) shuts bgfx down, showing the other one starts it (the
fullscreen restart path). In the browser both maps share the one canvas;
only the owner positions or hides it.

## 2D imagery

* `map_bgfx_2d_imagery_layout.h` (pure C++, unit-tested): the imagery level is
  `round(continuous zoom)`, but quads are positioned with the *continuous*
  scale, so imagery stretches smoothly between levels and never steps. The
  test asserts continuity of a world point's screen position across x.5.
* A missing tile is covered by the nearest resident ancestor's sub-rectangle
  and by resident children on top; never a hole while tiles stream in.
* `map_bgfx_2d_imagery_gpu.*` with `map_bgfx_2d_imagery_atlas.*`: tiles are
  cells of a few atlas pages (edge-replicating 1-texel gutters, so bilinear
  sampling equals clamp-to-edge), LRU budget of 384 tiles, at most 8 uploads
  per frame (nearest first), one draw per run of quads on the same page
  (normally a single draw call for all imagery). The globe shares the atlas.
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

## Globe (Phase 3, in progress)

On by default since slice 3b (2026-10-17): the HUD offers 3D in the Map
Monitor and Globe mode draws on bgfx. `AOWIS_BGFX_GLOBE=0` (environment)
switches it off (3D disabled again). The editor never offers it
(`MapBgfxSurface::setGlobeOffered(false)`).

Slice 1, the terrain surface (`map_bgfx_globe_surface.*`):

* CPU work is the backend-neutral `MapGlobeSurfacePreparation` the QRhi globe
  uses: quadtree leaves with horizon and view-cone culling, ellipsoid and polar
  cap geometry relative to `MapGlobeCamera`'s sticky render origin, DEM
  requests, LOD and stitching, asynchronous terrain meshes, wireframe edges.
* GPU: window and cap vertices/indices in dynamic buffers (full upload on a
  window/cap rebuild, terrain meshes as vertex-range patches), one indexed draw
  per tile, depth test and write LessEqual. The camera's canonical OpenGL clip
  space gets the [0, 1] depth correction where bgfx reports no homogeneous
  depth (Direct3D, Vulkan, Metal). `QMatrix4x4::constData()` is column-major,
  as bgfx's `mul(matrix, vector)` expects.
* Imagery from the shared 2D atlas: a tile samples its own cell, or the
  covering part of its nearest resident ancestor (`map_bgfx_globe_imagery.h`,
  texture coordinates mapped into the ancestor's cell, no CPU compositing),
  or a solid colour (nothing loaded; polar caps). Uploads share the 8 per
  frame budget; requests go to the tile repository in one batch per window.
* Globe shaders (`vs/fs_globe_surface`, `vs/fs_globe_wireframe`) declare
  their vec3 positions in `varying_globe.def.sc`; the 2D shaders keep
  `varying.def.sc` (and byte-identical binaries).

Slice 2, terrain-aware navigation (`src/map/render/map_globe_terrain_navigation.*`,
backend-neutral, the port of MapRhiWidget's terrain code):

* Camera height: the orbit rig's vertical offset follows the DEM
  perpendicularly beneath the eye; while rotating (frozen pivot) an eye-only
  collision lift keeps `MinViewGlobeCameraGroundClearanceM`, released into the
  offset when the orbit ends. Monotonic exponential filter (0.22 s / 0.16 s),
  settled at a quarter pixel. Camera changes take a step at once (and request
  the DEM tile under the eye); the surface's frame loop continues the filter
  while `following()` (no extra timer).
* Rays against the drawn surface (`MapGlobeSurfacePreparation`'s retained
  meshes, ellipsoid elsewhere): the pointer coordinate with DEM altitude,
  `globeSurfaceRayHitAtScreen()`, and globe pans anchored to the grabbed DEM
  point (`MapModel::panGlobeByTerrainPointerDrag`, blended out at grazing
  angles). The Map Monitor installs MapWidget's resolvers for the bgfx
  surface as for QRhi; the 2D footer altitude reads cached DEM tiles too.
* A vertical exaggeration change rebuilds the terrain meshes (vertex
  patches); a network opened in Globe mode requests its DEM tiles at once.

Slice 3, the network on the globe (`map_bgfx_globe_network.*`):

* Geometry is the backend-neutral `MapGlobeNetworkScene` of the QRhi globe
  (WGS84 placement relative to a retained network origin, adaptive geodesic
  subdivision, metre/pixel sizing, declutter, X-Ray classification against
  the DEM the globe shows). Its inputs are stored and applied on the next
  globe frame (`synchronize()`), so the 2D map never builds globe geometry.
  GPU streams are re-uploaded per category (links/nodes, highlights,
  chevrons, icons + models, X-Ray runs, junctions), as the QRhi backend's
  invalidations.
* Shaders: the QRhi globe network shaders in bgfx form (`vs_globe_link`,
  `vs_globe_flow`, `fs_globe_link(_xray)`, `vs/fs_globe_node`,
  `vs/fs_globe_junction(_xray)`, `vs/fs_globe_icon`, `vs/fs_globe_model`),
  their own `varying_globe_network.def.sc` and shared uniforms/helpers in
  `globe_network.sh`. Pixel-space segment coordinates are interpolated
  screen-linearly (passed times w, divided in the fragment shader; WebGL 2
  has no `noperspective`), which also replaces the QRhi chevron shader's
  `gl_FragCoord` reconstruction.
* Junctions: sphere impostors writing the sphere's depth (`gl_FragDepth`,
  NDC to window depth per bgfx's homogeneous-depth cap). Expanded to six
  vertices each with the colour resolved on the CPU (symbology, selection
  cyan, error red / stale grey, hidden, show-junctions) instead of instancing
  plus a style-table texture read in the vertex shader.
* Draw order and states of `MapRhiGlobeNetworkBackend::draw()`: Solid draws
  the network once without depth test first; X-Ray draws the buried runs
  (dashed) and junctions (dithered) through the terrain; Hide lets the
  terrain occlude them. Premultiplied blending; tank/reservoir models with
  back-face culling. The chevrons keep their 8 px pull towards the eye but
  have no extra depth bias (bgfx has no depth-bias state).
* The underground mode is set through `setUndergroundMode()` (default
  X-Ray); its HUD buttons arrive with the globe HUD (slice 5). X-Ray
  classification runs 500 ms after the last camera change or DEM arrival,
  once no terrain mesh is being built, like the QRhi globe.

Slice 3b, the globe heatmap (`map_bgfx_globe_surface.*`):

* Markers, radius and solid centre as `MapRhiWidget::syncGlobeHeatmapOverlay()`
  (node positions, ramp colours of the heatmap metric; a pixel radius is
  converted at the orbit distance when the heatmap data or style change).
  `MapGlobeHeatmapScene` gives each window tile its stamps in the tile's
  256 px space.
* GPU bake: atlas pages of 2048 x 2048 (64 cells, up to 4 pages, created as
  needed) rendered in bake views 1-4, which `bgfx::setViewOrder()` puts
  before the map view 0. Per cell a clear quad (no blending), then the stamp
  quads (`vs/fs_globe_heatmap_bake`, premultiplied source-over, clamped to
  the cell). The bake's Y follows `Caps::originBottomLeft`, so the cell is
  sampled with the tile's own v-down texture coordinates on every backend.
* Cells per tile and scene revision (LRU over pages, 24 bakes per frame);
  a tile without stamps has no cell. Tiles with a cell are drawn by
  `vs/fs_globe_surface_heatmap` (the imagery, then the heatmap with the
  symbology opacity: rgb * (1 - a * opacity) + heat * opacity).

Still on QRhi only: globe picking and the globe HUD controls (slices 4-5,
roadmap).

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
spirv, metal and dx11 into the build tree and copies them plus `inputs.sha256`
into `src/map/bgfx/shaders/bin`. Commit that directory.

dx11 (DXBC, for the Windows build's Direct3D 11 renderer) is compiled by
Microsoft's `d3dcompiler_47.dll`, which bgfx ships in `tools/bin/windows`
(part of the `--shader-tools` sparse checkout). A Windows host loads it
directly (copied next to `aowis-bgfx-shaderc.exe`); Linux and macOS run it
through bgfx's d3d4linux helper under **Wine** (any `wine` on `PATH`, or
`-DAOWIS_BGFX_WINE=/path/to/wine`; it uses its own prefix in the build tree).
Without Wine the dx11 family is skipped with a warning and the committed
binaries stay as they are; a Windows build without current dx11 binaries
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
