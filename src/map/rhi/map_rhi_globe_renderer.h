#ifndef MAP_RHI_GLOBE_RENDERER_H
#define MAP_RHI_GLOBE_RENDERER_H

#include "geo/geo_wgs84_ellipsoid.h"

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
class MapRhiTerrainMeshScheduler;
class MapTileRepository;
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QRhiTextureRenderTarget;

// Zoom/tile-index identity of one leaf produced by the visible-region
// quadtree walk (selectVisibleGlobeQuadtreeLeaves() in the .cpp). Plain POD
// so it can cross from the free-function quadtree walk into
// MapRhiGlobeRenderer's private API without making the leaf itself depend on
// geo or RHI types. Replaces the old single-zoom rectangular tile window:
// every leaf carries its own zoom, so near-camera ground can stay at fine detail while
// terrain approaching the horizon is covered by a handful of coarse,
// low-zoom leaves instead of forcing the whole visible footprint to one
// uniform resolution (see the class comment below for why that mattered).
struct MapRhiGlobeQuadtreeLeaf
{
    int zoom = 0;
    int tile_x = 0;
    int tile_y = 0;
};

// Renders planet Earth as a WGS84 ellipsoid for the "Globe" map view mode.
//
// The visible-region tile set is a genuine quadtree, not a single-zoom
// rectangular window: selectVisibleGlobeQuadtreeLeaves() walks the tile
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
// MapRhiTerrainMeshScheduler) -- only the question of *which* (zoom, x, y)
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
    // Stable node identity plus its coordinate and current heatmap state.
    // Inactive markers deliberately remain in the vector: simulation
    // timesteps are allowed to add/remove values without changing the
    // geographic stamp layout. No radius/opacity/solid-fraction lives here;
    // those values are shared across every marker for a given call (see
    // setHeatmapOverlay()). Lon/lat rather than CoordinateWGS84 keeps this a
    // plain POD with minimal includes.
    struct HeatmapMarker
    {
        quint32 render_id = 0;
        double longitude_deg = 0.0;
        double latitude_deg = 0.0;
        bool active = false;
        QColor color;

        bool operator==(const HeatmapMarker &other) const
        {
            return this->render_id == other.render_id
                && this->longitude_deg == other.longitude_deg
                && this->latitude_deg == other.latitude_deg
                && this->active == other.active
                && this->color == other.color;
        }
    };

    MapRhiGlobeRenderer(MapModel *map_model, MapTileRepository *tile_repository);
    ~MapRhiGlobeRenderer();

    void setTileRepository(MapTileRepository *tile_repository);
    void setTerrainRepository(MapTerrainRepository *terrain_repository);
    // Keeps terrain/caps in the same origin-relative coordinate frame as
    // Globe network geometry. Returns true only on a real origin change.
    bool setRenderOriginEcef(
        const GeoWgs84Ellipsoid::EcefPositionD &origin_ecef);
    void notifyTerrainTileAvailable(const QString &key);
    void invalidateTerrain();
    void setWireframeVisible(bool visible);
    void setMapVisible(bool visible);
    bool hasPendingTerrainMeshes() const;
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
    // to regenerate).
    bool prepare(QRhiResourceUpdateBatch *resource_updates,
                const QMatrix4x4 &view_projection, const QSize &viewport_size,
                float heatmap_opacity, const QColor &background_color,
                float background_opacity);
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

    // Drops all cached tile textures/bindings and forces the window to be
    // rebuilt from scratch on the next prepare() call. Used when the
    // imagery provider changes, since the tile keys/endpoints already
    // requested are for the old provider.
    void invalidateImagery();

    // Releases all GPU resources; call before the RHI instance itself goes
    // away (mirrors MapRhiBasemapRenderer::releaseResources()).
    void releaseResources();

private:
    struct TileVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        // Page-local imagery-array layer for the batched Globe pass. Layer
        // 0 is reserved as the "not array-ready" sentinel and is discarded
        // by the array fragment shader. Page selection happens by binding a
        // different array texture for each compact page draw; the ordinary
        // per-tile pipeline ignores this attribute.
        float layer = 0.0f;
    };

    struct WireframeVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // Tile-local description of one radial heatmap stamp. marker_index and
    // marker_render_id together validate the stable-node lookup, allowing
    // the expensive geographic projection/layout to survive simulation
    // frames that change marker colors or value availability.
    struct HeatmapStamp
    {
        double center_x_pixels = 0.0;
        double center_y_pixels = 0.0;
        double radius_pixels = 0.0;
        int marker_index = -1;
        quint32 marker_render_id = 0;
        QColor color;
    };

    // Web Mercator tile coordinates at zoom 0. Every higher-zoom tile
    // coordinate is this value multiplied by 2^zoom, so radius-only changes
    // never repeat longitude normalization or the latitude tan/log transform.
    struct HeatmapMarkerProjection
    {
        double tile_x_zoom0 = 0.0;
        double tile_y_zoom0 = 0.0;
        bool valid = false;
    };

    struct TileResource
    {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiTexture> heatmap_texture;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
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
        // Marker identities, positions and radius determine this layout;
        // marker colors and active flags do not. Dynamic simulation updates
        // therefore filter/recolor these cached entries instead of repeating
        // every marker/tile projection test.
        QVector<HeatmapStamp> heatmap_stamp_layout;
        quint64 heatmap_stamp_layout_revision = 0;
        int heatmap_stamp_layout_zoom = -1;
        int heatmap_stamp_layout_virtual_x = 0;
        int heatmap_stamp_layout_tile_y = 0;
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

    struct GlobeTile
    {
        int virtual_x = 0;
        int tile_x = 0;
        int tile_y = 0;
        int zoom = 0;
        bool is_cap = false;
        int first_vertex = 0;
        int vertex_count = 0;
        int first_index = 0;
        int index_count = 0;
        QString imagery_key;
        int terrain_zoom = -1;
        QString terrain_key;
        int terrain_cell_count = 1;
        int terrain_stitch_top_cell_count = 0;
        int terrain_stitch_right_cell_count = 0;
        int terrain_stitch_bottom_cell_count = 0;
        int terrain_stitch_left_cell_count = 0;
        quint64 terrain_mesh_request_id = 0;
        bool terrain_mesh_applied = false;
        // Future GPU-displaced terrain consumes one shared 65x65 height
        // layer per terrain_key. Multiple finer imagery leaves can therefore
        // point at the same page/layer without duplicating the DEM upload.
        // This foundation patch only populates the metadata/cache; the
        // established CPU mesh remains the rendered path.
        int terrain_height_array_page = -1;
        int terrain_height_array_layer = -1;
        bool terrain_height_array_ready = false;
        // Non-owning; points into tile_resources (or at cap_resource for
        // polar caps) and is only valid for the frame it was resolved in.
        TileResource *resource = nullptr;
        // Transient per-frame state: true only when this tile's current
        // pixels occupy its assigned texture-array page/layer. Ready leaves
        // are included in that page's compact draw-index range; leaves still
        // loading continue through the ordinary per-tile fallback.
        bool array_ready = false;
        // True only when this array-ready tile also has current heatmap
        // pixels in its independently packed heatmap page/layer.
        bool heatmap_array_ready = false;
    };

    struct TileArrayPage
    {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
        QVector<int> free_layers;
        int first_draw_index = 0;
        int draw_index_count = 0;
    };

    struct HeatmapArrayPage
    {
        std::unique_ptr<QRhiTexture> texture;
        QVector<int> free_layers;
    };

    struct TerrainHeightArrayPage
    {
        std::unique_ptr<QRhiTexture> texture;
        QVector<int> free_layers;
    };

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
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
        int first_draw_index = 0;
        int draw_index_count = 0;
    };

    struct HeatmapBakeVertex
    {
        float corner_x = 0.0f;
        float corner_y = 0.0f;
    };

    struct HeatmapBakeInstance
    {
        float center_x_pixels = 0.0f;
        float center_y_pixels = 0.0f;
        float radius_pixels = 0.0f;
        float solid_fraction = 0.0f;
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        // Maps this tile-local 0..256 X coordinate into one slot of the
        // horizontal visible-bake atlas. Diagnostic bakes retain 1/0 and
        // therefore continue targeting their standalone 256x256 texture.
        float target_scale_x = 1.0f;
        float target_offset_x = 0.0f;
    };

    struct HeatmapGpuBakeJob
    {
        TileResource *resource = nullptr;
        QRhiTexture *destination_texture = nullptr;
        // Zero targets an ordinary 2D fallback texture. Positive values
        // target the corresponding layer of a heatmap texture array.
        int destination_layer = 0;
        quint64 revision = 0;
        QVector<HeatmapBakeInstance> instances;
    };

    struct HeatmapGpuBakeAtlas
    {
        int slot_count = 0;
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiRenderPassDescriptor> render_pass_descriptor;
        std::unique_ptr<QRhiTextureRenderTarget> target;
    };

    struct HeatmapRasterStats
    {
        int candidate_markers = 0;
        int candidate_bucket_cells = 0;
        int marker_tile_pairs = 0;
        bool stamp_layout_cache_hit = false;
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

    void buildCaps();
    void buildPolarCap(bool north);
    void rebuildWindow(
        const QVector<MapRhiGlobeQuadtreeLeaf> &leaves, const QSize &viewport_size);
    QVector<MapRhiGlobeQuadtreeLeaf> currentWindowLeaves() const;
    int terrainCellCountForTile(const GlobeTile &tile, const QSize &viewport_size) const;
    void updateTerrainStitchCellCounts(QVector<GlobeTile> *tiles) const;
    bool currentTerrainLodMatches(const QSize &viewport_size) const;
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
    void rebuildWireframeVertices();
    void appendWireframeEdges(
        const QVector<TileVertex> &vertices,
        const QVector<quint32> &indices);
    bool uploadWireframeVertices(QRhiResourceUpdateBatch *resource_updates);
    TileVertex makeTileVertex(double lon_deg, double lat_deg, float u, float v) const;
    bool ensureSharedResources();
    bool createTileArrayResources();
    bool createTileArrayPage();
    void trimUnusedTileArrayPages();
    bool arrayBatchingActive() const;
    void setTileArrayReady(GlobeTile &tile, bool ready);
    void rebuildTileArrayDrawIndices();
    bool uploadTileArrayDrawIndices(QRhiResourceUpdateBatch *resource_updates);
    bool createHeatmapArrayResources();
    bool createHeatmapArrayPage();
    void trimUnusedHeatmapArrayPages();
    bool heatmapArrayBatchingActive() const;
    void setTileHeatmapArrayReady(GlobeTile &tile, bool ready);
    bool rebuildHeatmapArrayDrawIndices();
    bool uploadHeatmapArrayDrawIndices(
        QRhiResourceUpdateBatch *resource_updates);
    bool uploadHeatmapArrayLayers(
        QRhiResourceUpdateBatch *resource_updates);
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
        TileResource *resource, QRhiTexture *destination_texture,
        const QVector<HeatmapStamp> &stamps,
        int destination_layer = 0);
    void disableHeatmapGpuBaking();
    void scheduleDiagnosticHeatmapGpuSelfTest();
    void scheduleDiagnosticHeatmapGpuBake(
        const GlobeTile &tile, const QVector<HeatmapStamp> &stamps,
        const QImage &premultiplied_image);
    bool ensureDiagnosticHeatmapGpuBakeResources();
    void releaseDiagnosticHeatmapGpuBakeResources();
    HeatmapGpuBakeAtlas *ensureVisibleHeatmapGpuBakeAtlasResources(
        int slot_count);
    int maximumVisibleHeatmapGpuBakeAtlasSlots();
    void releaseVisibleHeatmapGpuBakeAtlasResources();
    void reportHeatmapProfile() const;
    void rebuildHeatmapMarkerBuckets();
    QVector<int> heatmapMarkerCandidates(
        const GlobeTile &tile, double radius_tile_fraction,
        int *visited_bucket_cells) const;
    bool requestMissingTiles(QRhiResourceUpdateBatch *resource_updates);
    void requestMissingTerrainTiles();
    void scheduleReadyTerrainMeshes();
    bool applyReadyTerrainMeshes(QRhiResourceUpdateBatch *resource_updates);

    MapModel *map_model = nullptr;
    MapTileRepository *tile_repository = nullptr;
    MapTerrainRepository *terrain_repository = nullptr;
    GeoWgs84Ellipsoid::EcefPositionD render_origin_ecef;
    QRhi *rhi = nullptr;
    QRhiRenderPassDescriptor *render_pass_descriptor = nullptr;
    int sample_count = 1;

    // Dynamic imagery window (see class comment above).
    QVector<TileVertex> window_vertices;
    QVector<quint32> window_indices;
    QVector<GlobeTile> window_tiles;
    bool window_dirty = true;
    // Which (zoom, tile_x, tile_y) nodes were subdivided into children on
    // the previous quadtree walk. Consulted by
    // selectVisibleGlobeQuadtreeLeaves() to apply hysteresis around the
    // subdivide/merge threshold -- without it, a node whose projected size
    // sits right at the boundary would flicker between one leaf and four
    // children every frame as the camera drifts by sub-pixel amounts.
    QSet<quint64> previously_subdivided_quadtree_nodes;
    bool window_tiles_requested = false;
    bool window_vertex_upload_pending = false;
    bool window_index_upload_pending = false;
    std::unique_ptr<QRhiBuffer> window_vertex_buffer;
    std::unique_ptr<QRhiBuffer> window_index_buffer;
    int window_vertex_buffer_size = 0;
    int window_index_buffer_size = 0;

    QVector<WireframeVertex> wireframe_vertices;
    bool wireframe_vertex_upload_pending = true;
    std::unique_ptr<QRhiBuffer> wireframe_vertex_buffer;
    int wireframe_vertex_buffer_size = 0;
    bool wireframe_visible = false;
    bool map_visible = true;

    // Static polar caps (see class comment above).
    QVector<TileVertex> cap_vertices;
    QVector<quint32> cap_indices;
    QVector<GlobeTile> cap_tiles;
    bool caps_built = false;
    bool cap_vertex_upload_pending = true;
    bool cap_index_upload_pending = true;
    std::unique_ptr<QRhiBuffer> cap_vertex_buffer;
    std::unique_ptr<QRhiBuffer> cap_index_buffer;

    std::unique_ptr<QRhiBuffer> camera_uniform_buffer;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiTexture> dummy_texture;
    bool dummy_texture_upload_pending = true;
    // Fallback bound at binding 2 (see rebuildTileBindings()) for any tile
    // with no heatmap_texture of its own -- fully transparent, unlike
    // dummy_texture above (GlobeMissingTileColor, opaque), since this one
    // gets *blended onto* the tile's real imagery rather than replacing it.
    std::unique_ptr<QRhiTexture> heatmap_dummy_texture;
    bool heatmap_dummy_texture_upload_pending = true;
    std::unique_ptr<QRhiShaderResourceBindings> template_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> wireframe_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> wireframe_pipeline;
    // Each page owns 255 usable imagery layers and is allocated only when
    // every existing page is full. TileResource keeps stable page/layer
    // ownership; a compact index stream groups otherwise non-contiguous
    // window tiles into one indexed draw range per page.
    std::vector<TileArrayPage> tile_array_pages;
    std::unique_ptr<QRhiGraphicsPipeline> array_pipeline;
    QVector<quint32> tile_array_draw_indices;
    bool tile_array_draw_indices_dirty = true;
    bool tile_array_draw_index_upload_pending = false;
    std::unique_ptr<QRhiBuffer> tile_array_draw_index_buffer;
    int tile_array_draw_index_buffer_size = 0;
    // Heatmap tiles remain sparsely packed into independent pages. When an
    // overlay is visible, the compact index stream groups every imagery
    // tile by its (imagery page, heatmap page) pair so imagery and heatmap
    // are sampled in one terrain pass rather than drawing terrain twice.
    std::vector<HeatmapArrayPage> heatmap_array_pages;
    std::unique_ptr<QRhiGraphicsPipeline> heatmap_array_pipeline;
    std::unique_ptr<QRhiShaderResourceBindings>
        heatmap_array_template_bindings;
    std::vector<HeatmapArrayDrawBatch> heatmap_array_draw_batches;
    QVector<quint32> heatmap_array_draw_indices;
    bool heatmap_array_draw_indices_dirty = true;
    bool heatmap_array_draw_index_upload_pending = false;
    std::unique_ptr<QRhiBuffer> heatmap_array_draw_index_buffer;
    int heatmap_array_draw_index_buffer_size = 0;
    // A separate float stream preserves TileVertex's compact imagery-path
    // stride; only the fused imagery/heatmap pipeline fetches this value.
    QVector<float> window_heatmap_array_layers;
    bool heatmap_array_layer_upload_pending = true;
    std::unique_ptr<QRhiBuffer> heatmap_array_layer_buffer;
    int heatmap_array_layer_buffer_size = 0;
    std::map<QString, std::unique_ptr<TileResource>> tile_resources;
    TileResource cap_resource;
    // Mirrors MapRhiBasemapRenderer's identically-named members exactly --
    // see setHeatmapOverlay(). heatmap_revision starts at 1, matching the
    // basemap renderer, so that a freshly-constructed TileResource's
    // default heatmap_revision of 0 is always seen as stale on its first
    // check, forcing (at worst) a single "definitely empty" texture
    // generation rather than an uninitialized-looking mismatch.
    QVector<HeatmapMarker> heatmap_markers;
    QVector<HeatmapMarkerProjection> heatmap_marker_projections;
    QVector<QHash<quint64, QVector<int>>> heatmap_marker_buckets_by_zoom;
    double heatmap_radius_m = 0.0;
    double heatmap_solid_fraction = 0.0;
    float heatmap_opacity = 0.0f;
    quint64 heatmap_revision = 1;
    int heatmap_active_marker_count = 0;
    // Changes only when stamp geometry can change (positions or radius),
    // unlike heatmap_revision which also changes for new simulation colors
    // or marker value availability.
    quint64 heatmap_stamp_layout_revision = 1;
    HeatmapProfileCounters heatmap_profile;
    bool heatmap_profile_report_pending = false;

    // Height pages are independent from imagery/heatmap pages because DEMs
    // have a different size, format and geographic reuse lifetime. The LRU
    // remains useful even before shader displacement lands: once uploaded,
    // a DEM can survive eviction from MapTerrainRepository's CPU QCache.
    std::vector<TerrainHeightArrayPage> terrain_height_array_pages;
    QHash<QString, TerrainHeightCacheEntry> terrain_height_cache;
    quint64 terrain_height_cache_frame = 0;
    bool terrain_height_cache_disabled = false;
    bool terrain_height_cache_page_growth_disabled = false;
    TerrainHeightCacheProfileCounters terrain_height_cache_profile;

    // Standalone 256x256 target retained for opt-in validation. The visible
    // atlas below shares its pipeline and geometry but never its texture, so
    // diagnostic readback dimensions and orientation remain unchanged.
    std::unique_ptr<QRhiTexture> diagnostic_heatmap_bake_texture;
    std::unique_ptr<QRhiRenderPassDescriptor>
        diagnostic_heatmap_bake_render_pass_descriptor;
    std::unique_ptr<QRhiTextureRenderTarget>
        diagnostic_heatmap_bake_target;
    std::unique_ptr<QRhiShaderResourceBindings>
        diagnostic_heatmap_bake_bindings;
    std::unique_ptr<QRhiGraphicsPipeline>
        diagnostic_heatmap_bake_pipeline;
    std::unique_ptr<QRhiBuffer> diagnostic_heatmap_bake_vertex_buffer;
    std::unique_ptr<QRhiBuffer> diagnostic_heatmap_bake_instance_buffer;
    int diagnostic_heatmap_bake_instance_buffer_size = 0;
    bool diagnostic_heatmap_bake_vertex_upload_pending = true;
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
    std::map<int, HeatmapGpuBakeAtlas> heatmap_gpu_bake_atlases;
    int heatmap_gpu_bake_maximum_atlas_slots = 0;
    std::unique_ptr<QRhiBuffer> heatmap_gpu_bake_instance_buffer;
    int heatmap_gpu_bake_instance_buffer_size = 0;
    bool heatmap_gpu_baking_disabled = false;

    std::unique_ptr<MapRhiTerrainMeshScheduler> terrain_mesh_scheduler;
    quint64 next_terrain_mesh_request_id = 1;
    bool reported_orthometric_datum_warning = false;
    bool reported_unusable_datum_warning = false;
    QElapsedTimer terrain_lod_rebuild_clock;
    bool terrain_lod_rebuild_pending = false;
};

#endif // MAP_RHI_GLOBE_RENDERER_H
