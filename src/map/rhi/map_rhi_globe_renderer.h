#ifndef MAP_RHI_GLOBE_RENDERER_H
#define MAP_RHI_GLOBE_RENDERER_H

#include "geo/geo_wgs84_ellipsoid.h"
#include "map/render/map_globe_surface_render_frame.h"
#include "map/render/map_globe_surface_preparation.h"
#include "map/render/map_globe_heatmap_scene.h"
#include "map/rhi/map_rhi_globe_surface_backend.h"

#include <aowis/model/gis.h>

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QMatrix4x4>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVector>

#include <map>
#include <memory>
#include <vector>

class MapModel;
class MapTerrainRepository;
class MapTileRepository;
class QRhi;
class QRhiCommandBuffer;
class QRhiRenderPassDescriptor;
class QRhiResourceUpdateBatch;

// Renders planet Earth as a WGS84 ellipsoid for the "Globe" map view mode.
//
// The visible-region tile set is a genuine quadtree, not a single-zoom
// rectangular window: MapGlobeSurfaceScene::selectVisibleLeaves() walks the tile
// hierarchy from the whole-planet root, at each node deciding independently
// whether that node's own on-screen projected size still calls for more
// detail (subdivide into 4 children) or is already fine enough to leave as
// a single leaf, and separately culling any node hidden behind the planet's
// own curvature (real ellipsoidal horizon occlusion, not just a screen
// rectangle test) or outside the camera's field of view. This is what a
// tilted view revealing the horizon needs: near-camera ground stays at
// fine, many-tile detail while the terrain approaching the horizon is
// covered by a handful of large, coarse leaves, so total tile count stays
// bounded regardless of pitch -- a single uniform zoom level cannot do that,
// since covering a horizon-spanning footprint at near-camera resolution
// means the tile-index range explodes (potentially to the entire zoom
// level's tile grid) the moment the limb enters view. Each leaf still goes
// through the same per-tile pipeline as before it (imagery/terrain request,
// texture cache, background terrain-mesh generation via
// MapTerrainMeshScheduler) -- only the question of *which* (zoom, x, y)
// tiles exist this frame changed.
//
// Two independent pieces of geometry:
//  - "window" tiles: the dynamic quadtree-selected basemap imagery leaves
//    described above.
//  - "cap" tiles: a small, fixed pair of flat-colored polar fans covering
//    the area above/below Web Mercator's +-85.05 degree limit, which no
//    imagery tile at any zoom will ever cover. Rebuilt only when the sticky
//    Globe render origin changes; entirely unrelated to the LOD system above.
class MapRhiGlobeRenderer
{
public:
    // Compatibility name for current callers. Heatmap marker/state/layout
    // preparation is backend-neutral and owned by MapGlobeHeatmapScene.
    using HeatmapMarker = MapGlobeHeatmapMarker;

    MapRhiGlobeRenderer(MapModel *map_model, MapTileRepository *tile_repository);
    ~MapRhiGlobeRenderer();

    void setTileRepository(MapTileRepository *tile_repository);
    void setTerrainRepository(MapTerrainRepository *terrain_repository);
    // Repository signals can arrive while a terrain-height animation is
    // otherwise eligible for the camera-only path. Mark their content work
    // explicitly so that frame performs normal tile preparation instead.
    void notifyTileRepositoryChanged();
    void notifyTerrainRepositoryChanged();
    // Immediately rebuild the visible Globe tile window from the current
    // camera and dispatch missing DEM requests. This is used after a network
    // fit so terrain loading starts at the fitted view without requiring any
    // subsequent zoom/pan input.
    void requestTerrainForCurrentView(const QSize &viewport_size);
    void invalidateTerrainView();
    // Keeps terrain/caps in the same origin-relative coordinate frame as
    // Globe network geometry. Returns true only on a real origin change.
    bool setRenderOriginEcef(
        const GeoWgs84Ellipsoid::EcefPositionD &origin_ecef);
    void notifyTerrainTileAvailable(const QString &key);
    void invalidateTerrain();
    void setWireframeVisible(bool visible);
    void setMapVisible(bool visible);
    bool hasPendingTerrainMeshes() const;
    void terrainMeshProgress(int *completed, int *total, bool *active) const;
    // Intersects an ECEF ray against the exact Globe DEM triangles currently
    // retained for rendering. Only tiles whose retained vertex range really
    // contains DEM relief participate, including still-visible old relief
    // while a replacement is built; a cached DEM that has not reached the
    // visible mesh is never invented here. Returns the nearest forward hit
    // in ECEF meters. There is deliberately no ellipsoid fallback here: the
    // caller can distinguish "real rendered DEM" from its own fallback.
    bool visibleTerrainRayIntersection(
        const GeoWgs84Ellipsoid::EcefPositionD &ray_origin_ecef,
        const QVector3D &ray_direction_ecef,
        GeoWgs84Ellipsoid::EcefPositionD *intersection_ecef,
        double *distance_m) const;
    // Resolves the terrain LOD that is actually retained for rendering at
    // coordinate and reports its physical mesh-cell size. X-Ray
    // classification uses this instead of a fixed metre interval so its
    // sampling density follows the same terrain detail the user sees.
    bool visibleTerrainSamplingAtCoordinate(
        const CoordinateWGS84 &coordinate,
        int *terrain_zoom,
        double *cell_size_m) const;
    // Tracks visible heatmap changes separately from geographic layout
    // changes. Colors and active flags invalidate tile pixels, while stable
    // render ids, coordinates and radius govern the retained stamp layout.
    // radius_m is real-world meters, unlike the flat renderer's
    // already-projected "world units" radius: see
    // MapRhiWidget::globeHeatmapRadiusMeters() for why Globe can take a
    // real distance directly rather than needing a zoom-dependent
    // conversion first.
    void setHeatmapOverlay(
        const QVector<HeatmapMarker> &markers, double radius_m, double solid_fraction);

    // Called whenever the RHI/render pass may have changed, same contract
    // as MapRhiBasemapRenderer::initialize(). Safe to call every frame; all
    // work below is guarded by "already created" checks.
    bool initialize(QRhi *rhi, QRhiRenderPassDescriptor *render_pass_descriptor,
                    int sample_count);

    // Recomputes the visible tile window (rebuilding it only if the zoom
    // level or window actually needs to change), uploads any pending
    // geometry/camera data, and requests any imagery tiles that are not yet
    // cached. view_projection must be relative to the origin most recently
    // supplied to setRenderOriginEcef(). Must be called before draw() each
    // frame, inside the same resource-update batch that beginPass() below
    // will consume.
    // heatmap_opacity and the basemap background blend are re-uploaded into
    // the (extended) GlobeCameraBlock uniform every call regardless of
    // whether they changed, exactly like view_projection already is -- see
    // the class comment on
    // ensureHeatmapTexture() for why that's fine to do unconditionally
    // (it's cheap, and unlike the texture itself, doesn't force any tile
    // to regenerate). allow_camera_only_prepare is only a hint: the renderer
    // honors it when the non-height view key and every resource/terrain dirty
    // guard still match the last successful full preparation.
    bool prepare(QRhiResourceUpdateBatch *resource_updates,
                const QMatrix4x4 &view_projection, const QSize &viewport_size,
                float heatmap_opacity, const QColor &background_color,
                float background_opacity,
                bool allow_camera_only_prepare = false);
    // Records all visible heatmap bakes queued by prepare(), copying each
    // atlas slot either into an ordinary fallback texture or directly into
    // a texture-array layer. Returns false only when the GPU path failed and
    // was disabled; the next frame regenerates through the retained CPU path.
    bool runPendingHeatmapGpuBakes(QRhiCommandBuffer *command_buffer);
    // Records one opt-in, diagnostic render-to-texture bake per heatmap
    // revision. A fixed asymmetric stamp pattern makes the check independent
    // of camera position and visible content. The generated texture is not
    // displayed: an asynchronous readback compares it against the established
    // CPU raster for orientation and premultiplied source-over compatibility.
    void runDiagnosticHeatmapGpuBake(QRhiCommandBuffer *command_buffer);
    void draw(QRhiCommandBuffer *command_buffer);

    const MapGlobeSurfaceRenderFrame &surfaceRenderFrame() const;

    // Drops all cached tile textures/bindings and forces the window to be
    // rebuilt from scratch on the next prepare() call. Used when the
    // imagery provider changes, since the tile keys/endpoints already
    // requested are for the old provider.
    void invalidateImagery();

    // Releases all GPU resources; call before the RHI instance itself goes
    // away (mirrors MapRhiBasemapRenderer::releaseResources()).
    void releaseResources();

private:
    using TileVertex = MapGlobeSurfaceVertex;

    using HeatmapStamp = MapGlobeHeatmapStamp;
    using HeatmapRasterStats = MapGlobeHeatmapRasterStats;

    struct TileResource
    {
        quint64 gpu_resource_id = 0;
        qint64 pixmap_cache_key = -1;
        // Monotonic identity of the pixels currently held by texture.
        // array_content_revision only catches up after those exact pixels
        // have also been uploaded into array_page/array_layer.
        quint64 content_revision = 0;
        quint64 array_content_revision = 0;
        int array_page = -1;
        int array_layer = -1;
        // Compared against the renderer's own heatmap_revision (bumped by
        // setHeatmapOverlay() whenever markers/radius/solid-fraction
        // actually change) to decide whether this tile's heatmap_texture
        // is stale and needs regenerating -- see ensureHeatmapTexture().
        // Mirrors MapRhiBasemapRenderer::TileResource::heatmap_revision
        // exactly.
        quint64 heatmap_revision = 0;
        // Revision actually uploaded into the ordinary per-tile fallback
        // texture. The fused array path intentionally leaves this behind so
        // the same raster is not uploaded twice.
        quint64 heatmap_texture_revision = 0;
        // The heatmap array is packed independently from imagery. Only
        // resources whose current heatmap actually contains pixels own a
        // page/layer; heatmap_array_revision catches up after upload.
        quint64 heatmap_array_revision = 0;
        int heatmap_array_page = -1;
        int heatmap_array_layer = -1;
        bool heatmap_has_content = false;
        // Backend-neutral retained stamp layout. Marker colors/active flags
        // are resolved by MapGlobeHeatmapScene without repeating geographic
        // projection work.
        MapGlobeHeatmapTileLayoutCache heatmap_layout_cache;
        // True while this resource holds a cropped-and-upscaled placeholder
        // derived from an already-loaded ancestor tile rather than the
        // tile's own imagery -- see ensureTileResource(). Cleared the
        // moment the tile's own imagery actually arrives.
        bool is_provisional = false;
        // Which ancestor's cache key the current provisional image was
        // derived from, so a closer ancestor becoming available later is
        // recognized as an upgrade instead of being silently ignored, and
        // so an unchanged ancestor is recognized as "nothing to redo" on
        // the next frame instead of re-deriving the same crop every time.
        QString provisional_source_key;
    };

    using GlobeTile = MapGlobeSurfaceTile;

    struct TileGpuState
    {
        TileResource *resource = nullptr;
        int terrain_height_array_page = -1;
        int terrain_height_array_layer = -1;
        bool terrain_height_array_ready = false;
        bool array_ready = false;
        bool heatmap_array_ready = false;
    };

    using TileArrayPage = MapRhiGlobeImageryArrayPage;
    using HeatmapArrayPage = MapRhiGlobeHeatmapArrayPage;
    using TerrainHeightArrayPage = MapRhiGlobeTerrainHeightArrayPage;

    struct TerrainHeightCacheEntry
    {
        int array_page = -1;
        int array_layer = -1;
        quint64 last_used_frame = 0;
        bool uploaded = false;
    };

    struct TerrainHeightCacheProfileCounters
    {
        bool enabled = false;
        int visible_terrain_tiles = 0;
        int unique_visible_dem_tiles = 0;
        int available_dem_tiles = 0;
        int ready_terrain_tiles = 0;
        int cache_hits = 0;
        int cache_misses = 0;
        int uploads = 0;
        int pending_uploads = 0;
        int evictions = 0;
        int capacity_misses = 0;
        quint64 upload_bytes = 0;
        qint64 cpu_ns = 0;
    };

    struct HeatmapArrayDrawBatch
    {
        int imagery_page_index = -1;
        int heatmap_page_index = -1;
        int first_draw_index = 0;
        int draw_index_count = 0;
    };

    using HeatmapBakeInstance = MapRhiGlobeHeatmapBakeInstance;

    struct HeatmapGpuBakeJob
    {
        TileResource *resource = nullptr;
        MapRhiGlobeHeatmapBakeDestination destination =
            MapRhiGlobeHeatmapBakeDestination::TileHeatmapTexture;
        quint64 tile_gpu_resource_id = 0;
        int heatmap_array_page = -1;
        // Zero targets an ordinary per-tile heatmap texture. Positive values
        // target the corresponding layer of a heatmap texture array.
        int destination_layer = 0;
        quint64 revision = 0;
        QVector<HeatmapBakeInstance> instances;
    };

    // Per-request counters for the opt-in Globe heatmap performance log.
    // Kept entirely outside rendering state so enabling diagnostics cannot
    // change which tiles, pixels, textures, or draw paths are selected.
    struct HeatmapProfileCounters
    {
        bool enabled = false;
        int visible_tiles = 0;
        int dirty_tiles = 0;
        int raster_calls = 0;
        int raster_tiles_with_content = 0;
        int candidate_markers = 0;
        int candidate_bucket_cells = 0;
        int marker_tile_pairs = 0;
        int stamp_layout_cache_hits = 0;
        int stamp_layout_cache_misses = 0;
        int fallback_uploads = 0;
        int array_uploads = 0;
        int gpu_visible_bake_passes = 0;
        int gpu_visible_bake_stamps = 0;
        int gpu_visible_copies = 0;
        int gpu_visible_copy_batches = 0;
        int gpu_visible_array_copies = 0;
        int gpu_bake_passes = 0;
        int gpu_bake_stamps = 0;
        int gpu_validation_readbacks = 0;
        quint64 upload_bytes = 0;
        qint64 stamp_ns = 0;
        qint64 raster_ns = 0;
        qint64 cpu_ns = 0;
    };

    void resetTerrainHeightCache();
    bool createTerrainHeightArrayPage();
    TerrainHeightCacheEntry *ensureTerrainHeightCacheEntry(
        const QString &terrain_key,
        const QSet<QString> &protected_terrain_keys);
    void releaseTerrainHeightCacheEntry(const QString &terrain_key);
    void prepareTerrainHeightCache(
        QRhiResourceUpdateBatch *resource_updates);
    void reportTerrainHeightCacheProfile() const;
    void pruneUnusedTileResources();
    TileGpuState *tileGpuState(GlobeTile &tile);
    const TileGpuState *tileGpuState(const GlobeTile &tile) const;
    TileResource *tileResource(GlobeTile &tile);
    const TileResource *tileResource(const GlobeTile &tile) const;
    void setTileResource(GlobeTile &tile, TileResource *resource);
    bool tileArrayReady(const GlobeTile &tile) const;
    bool tileHeatmapArrayReady(const GlobeTile &tile) const;
    void handleWindowGeometryRebuilt();

    bool ensureSharedResources();
    bool preparedViewSelectionMatches(const QSize &viewport_size) const;
    bool canUseCameraOnlyPrepare(const QSize &viewport_size) const;
    void rememberPreparedViewState(const QSize &viewport_size);
    void refreshPreparedSurfaceRenderFrameState(const QSize &viewport_size);
    void rebuildPreparedSurfaceRenderFrame(const QSize &viewport_size);
    bool uploadCameraUniform(
        QRhiResourceUpdateBatch *resource_updates,
        const QMatrix4x4 &view_projection,
        const QColor &background_color, float background_opacity);
    bool createTileArrayResources();
    bool createTileArrayPage();
    void trimUnusedTileArrayPages();
    bool arrayBatchingActive() const;
    void setTileArrayReady(GlobeTile &tile, bool ready);
    void rebuildTileArrayDrawIndices();
    bool uploadTileArrayDrawIndices(QRhiResourceUpdateBatch *resource_updates);
    bool createHeatmapArrayResources(
        QRhiResourceUpdateBatch *resource_updates);
    bool createHeatmapArrayPage(
        QRhiResourceUpdateBatch *resource_updates);
    void trimUnusedHeatmapArrayPages();
    bool heatmapArrayBatchingActive() const;
    void setTileHeatmapArrayReady(GlobeTile &tile, bool ready);
    bool rebuildHeatmapArrayDrawIndices();
    bool uploadHeatmapArrayDrawIndices(
        QRhiResourceUpdateBatch *resource_updates);
    bool uploadHeatmapArrayLayers(
        QRhiResourceUpdateBatch *resource_updates);
    bool ensureTileGpuResource(TileResource *resource);
    bool tileTextureReady(const TileResource *resource) const;
    bool tileHeatmapTextureReady(const TileResource *resource) const;
    bool tileBindingsReady(const TileResource *resource) const;
    void invalidateTileBindings(TileResource *resource);
    void clearTileHeatmapTexture(TileResource *resource);
    bool recreateTileTexture(TileResource *resource, const QSize &size);
    bool recreateTileHeatmapTexture(TileResource *resource, const QSize &size);
    bool rebuildTileBindings(TileResource *resource);
    bool ensureTileResource(
        GlobeTile &tile, QRhiResourceUpdateBatch *resource_updates,
        QImage *updated_image = nullptr,
        QImage *updated_heatmap_image = nullptr,
        QVector<HeatmapStamp> *updated_heatmap_stamps = nullptr);
    bool ensureTileArrayLayer(
        GlobeTile &tile, const QImage &updated_image,
        QRhiResourceUpdateBatch *resource_updates);
    QImage currentTileArrayImage(
        const GlobeTile &tile, const TileResource &resource) const;
    bool stampTileArrayLayer(
        GlobeTile &tile, const TileResource &resource,
        QRhiResourceUpdateBatch *resource_updates);
    void releaseTileArrayLayer(TileResource *resource);
    bool ensureTileHeatmapArray(
        GlobeTile &tile, const QImage &updated_image,
        const QVector<HeatmapStamp> &updated_stamps,
        QRhiResourceUpdateBatch *resource_updates);
    bool stampTileHeatmapArrayLayer(
        GlobeTile &tile, int layer,
        QRhiResourceUpdateBatch *resource_updates);
    void releaseHeatmapArrayLayer(TileResource *resource);
    void resetWindowArrayLayers();
    // See the definition's own comment: derives a cropped/upscaled
    // placeholder from the nearest already-loaded ancestor tile when
    // tile's own imagery isn't cached yet.
    bool ensureProvisionalTileResource(
        GlobeTile &tile, TileResource *resource,
        QRhiResourceUpdateBatch *resource_updates, QImage *updated_image);
    // Mirrors MapRhiBasemapRenderer::ensureHeatmapTexture()/renderHeatmapTile()
    // exactly, adapted to tile identity being (zoom, virtual_x, tile_y)
    // geodetic bounds instead of a flat world-pixel rectangle -- see
    // renderHeatmapTile()'s own comment for the coordinate conversion.
    // Regenerates heatmap content only when heatmap_revision is stale. The
    // per-tile and fused-array destinations are baked on the GPU when
    // supported. Only failed GPU setup retains the established QPainter
    // route. Most frames still do one integer comparison.
    bool ensureHeatmapTexture(
        const GlobeTile &tile, TileResource *resource,
        QRhiResourceUpdateBatch *resource_updates,
        QImage *updated_image = nullptr,
        QVector<HeatmapStamp> *updated_stamps = nullptr,
        bool upload_fallback_texture = true);
    bool ensureHeatmapFallbackTexture(
        const GlobeTile &tile, TileResource *resource,
        const QImage &updated_image,
        const QVector<HeatmapStamp> &updated_stamps,
        QRhiResourceUpdateBatch *resource_updates);
    QImage renderHeatmapTileProfiled(
        const GlobeTile &tile, TileResource *resource);
    QImage renderHeatmapTile(
        const GlobeTile &tile, TileResource *resource,
        HeatmapRasterStats *stats,
        QVector<HeatmapStamp> *rendered_stamps = nullptr,
        QImage *premultiplied_image = nullptr) const;
    QImage renderHeatmapStamps(
        const QVector<HeatmapStamp> &stamps) const;
    QVector<HeatmapStamp> heatmapStampsForTileProfiled(
        const GlobeTile &tile, TileResource *resource);
    QVector<HeatmapStamp> heatmapStampsForTile(
        const GlobeTile &tile, TileResource *resource,
        HeatmapRasterStats *stats) const;
    bool queueHeatmapGpuBake(
        TileResource *resource,
        MapRhiGlobeHeatmapBakeDestination destination,
        const QVector<HeatmapStamp> &stamps,
        int heatmap_array_page = -1,
        int destination_layer = 0);
    void disableHeatmapGpuBaking();
    void scheduleDiagnosticHeatmapGpuSelfTest();
    void scheduleDiagnosticHeatmapGpuBake(
        const GlobeTile &tile, const QVector<HeatmapStamp> &stamps,
        const QImage &premultiplied_image);
    bool ensureDiagnosticHeatmapGpuBakeResources();
    void releaseDiagnosticHeatmapGpuBakeResources();
    void releaseVisibleHeatmapGpuBakeAtlasResources();
    void reportHeatmapProfile() const;
    bool requestMissingTiles(QRhiResourceUpdateBatch *resource_updates);

    MapModel *map_model = nullptr;
    MapTileRepository *tile_repository = nullptr;
    MapTerrainRepository *terrain_repository = nullptr;
    MapGlobeSurfacePreparation surface_preparation;
    // A terrain-follow tick changes only the two omitted height values. When
    // the remaining view-selection inputs still match this last successful
    // full prepare, the current tile window and GPU resources can be reused.
    bool preparation_dirty = true;
    bool prepared_view_state_valid = false;
    QSize prepared_viewport_size;
    double prepared_center_lon_deg = 0.0;
    double prepared_center_lat_deg = 0.0;
    double prepared_yaw_deg = 0.0;
    double prepared_pitch_deg = 0.0;
    double prepared_distance_m = 0.0;
    QElapsedTimer full_prepare_clock;

    MapGlobeSurfaceRenderFrame prepared_surface_render_frame;
    bool window_tiles_requested = false;
    bool map_visible = true;

    QVector<TileGpuState> window_tile_gpu_states;
    QVector<TileGpuState> cap_tile_gpu_states;

    MapRhiGlobeSurfaceBackend surface_backend;
    MapRhiGlobeSurfaceDrawResources surface_draw_resources;

    // Shared surface QRhi state (camera uniform, sampler, fallback textures,
    // bindings and visible-pass pipelines) is owned by surface_backend.
    // Each page owns 255 usable imagery layers and is allocated only when
    // every existing page is full. TileResource keeps stable page/layer
    // ownership; a compact index stream groups otherwise non-contiguous
    // window tiles into one indexed draw range per page.
    QVector<quint32> tile_array_draw_indices;
    bool tile_array_draw_indices_dirty = true;
    bool tile_array_draw_index_upload_pending = false;
    // Heatmap tiles remain sparsely packed into independent pages. When an
    // overlay is visible, the compact index stream groups every imagery
    // tile by its (imagery page, heatmap page) pair so imagery and heatmap
    // are sampled in one terrain pass rather than drawing terrain twice.
    std::vector<HeatmapArrayDrawBatch> heatmap_array_draw_batches;
    QVector<quint32> heatmap_array_draw_indices;
    bool heatmap_array_draw_indices_dirty = true;
    bool heatmap_array_draw_index_upload_pending = false;
    // A separate float stream preserves TileVertex's compact imagery-path
    // stride; only the fused imagery/heatmap pipeline fetches this value.
    QVector<float> window_heatmap_array_layers;
    bool heatmap_array_layer_upload_pending = true;
    std::map<QString, std::unique_ptr<TileResource>> tile_resources;
    TileResource cap_resource;
    // Mirrors MapRhiBasemapRenderer's identically-named members exactly --
    // see setHeatmapOverlay(). heatmap_revision starts at 1, matching the
    // basemap renderer, so that a freshly-constructed TileResource's
    // default heatmap_revision of 0 is always seen as stale on its first
    // check, forcing (at worst) a single "definitely empty" texture
    // generation rather than an uninitialized-looking mismatch.
    MapGlobeHeatmapScene heatmap_scene;
    float heatmap_opacity = 0.0f;
    HeatmapProfileCounters heatmap_profile;
    bool heatmap_profile_report_pending = false;

    // Height pages are independent from imagery/heatmap pages because DEMs
    // have a different size, format and geographic reuse lifetime. The LRU
    // remains useful even before shader displacement lands: once uploaded,
    // a DEM can survive eviction from MapTerrainRepository's CPU QCache.
    QHash<QString, TerrainHeightCacheEntry> terrain_height_cache;
    quint64 terrain_height_cache_frame = 0;
    bool terrain_height_cache_disabled = false;
    bool terrain_height_cache_page_growth_disabled = false;
    TerrainHeightCacheProfileCounters terrain_height_cache_profile;

    // Standalone 256x256 target retained for opt-in validation. The visible
    // atlas below shares its pipeline and geometry but never its texture, so
    // diagnostic readback dimensions and orientation remain unchanged.
    QVector<HeatmapBakeInstance> diagnostic_heatmap_bake_instances;
    bool diagnostic_heatmap_bake_pending = false;
    quint64 diagnostic_heatmap_bake_revision = 0;
    // Validation is deliberately once per QRhi lifetime. Repeating a GPU
    // readback on every simulation revision distorts the profile it measures.
    bool diagnostic_heatmap_gpu_validation_attempted = false;
    int diagnostic_heatmap_bake_zoom = 0;
    int diagnostic_heatmap_bake_tile_x = 0;
    int diagnostic_heatmap_bake_tile_y = 0;
    QImage diagnostic_heatmap_bake_cpu_reference;
    QVector<HeatmapGpuBakeJob> heatmap_gpu_bake_jobs;
    // Lazily-created power-of-two horizontal atlases avoid clearing a full
    // 64-slot strip when only a handful of new tiles need baking. Their Y
    // extent stays exactly one tile, preserving the validated orientation.
    bool heatmap_gpu_baking_disabled = false;

};

#endif // MAP_RHI_GLOBE_RENDERER_H
