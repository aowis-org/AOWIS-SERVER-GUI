#ifndef MAP_RHI_GLOBE_RENDERER_H
#define MAP_RHI_GLOBE_RENDERER_H

#include "geo/geo_wgs84_ellipsoid.h"

#include <QColor>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVector>

#include <map>
#include <memory>

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
class QImage;

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
    // Plain (coordinate, color) pair -- no radius/opacity/solid-fraction
    // here, those are shared across every marker for a given call (see
    // setHeatmapOverlay()), matching MapRhiBasemapRenderer::HeatmapMarker's
    // shape exactly. Lon/lat rather than CoordinateWGS84 for the same
    // "plain POD, minimal includes" reason MapRhiGlobeQuadtreeLeaf above
    // gives.
    struct HeatmapMarker
    {
        double longitude_deg = 0.0;
        double latitude_deg = 0.0;
        QColor color;

        bool operator==(const HeatmapMarker &other) const
        {
            return this->longitude_deg == other.longitude_deg
                && this->latitude_deg == other.latitude_deg
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
    // Mirrors MapRhiBasemapRenderer::setHeatmapOverlay() exactly, including
    // its change-detection (markers vs. radius/solid-fraction tracked
    // separately, only actually invalidating cached tile textures -- via
    // the heatmap_revision counter each TileResource compares itself
    // against -- when something that could visibly change a texture's
    // pixels changed). radius_m is real-world meters, unlike the flat
    // renderer's already-projected "world units" radius: see
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
        // Shared imagery-array layer for the batched Globe pass. Layer 0
        // is reserved as the "not array-ready" sentinel and is discarded
        // by the array fragment shader. The ordinary per-tile pipeline
        // ignores this attribute.
        float layer = 0.0f;
    };

    struct WireframeVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct TileResource
    {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiTexture> heatmap_texture;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
        qint64 pixmap_cache_key = -1;
        // Monotonic identity of the pixels currently held by texture.
        // array_content_revision only catches up after those exact pixels
        // have also been uploaded into array_layer.
        quint64 content_revision = 0;
        quint64 array_content_revision = 0;
        int array_layer = -1;
        // Compared against the renderer's own heatmap_revision (bumped by
        // setHeatmapOverlay() whenever markers/radius/solid-fraction
        // actually change) to decide whether this tile's heatmap_texture
        // is stale and needs regenerating -- see ensureHeatmapTexture().
        // Mirrors MapRhiBasemapRenderer::TileResource::heatmap_revision
        // exactly.
        quint64 heatmap_revision = 0;
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
        // Non-owning; points into tile_resources (or at cap_resource for
        // polar caps) and is only valid for the frame it was resolved in.
        TileResource *resource = nullptr;
        // Transient per-frame state: true only when this tile's current
        // pixels occupy its assigned texture-array layer. Once every leaf
        // is ready, draw() can skip the ordinary per-tile window entirely.
        bool array_ready = false;
    };

    void buildCaps();
    void buildPolarCap(bool north);
    void rebuildWindow(
        const QVector<MapRhiGlobeQuadtreeLeaf> &leaves, const QSize &viewport_size);
    QVector<MapRhiGlobeQuadtreeLeaf> currentWindowLeaves() const;
    int terrainCellCountForTile(const GlobeTile &tile, const QSize &viewport_size) const;
    void updateTerrainStitchCellCounts(QVector<GlobeTile> *tiles) const;
    bool currentTerrainLodMatches(const QSize &viewport_size) const;
    void pruneUnusedTileResources();
    void rebuildWireframeVertices();
    void appendWireframeEdges(
        const QVector<TileVertex> &vertices,
        const QVector<quint32> &indices);
    bool uploadWireframeVertices(QRhiResourceUpdateBatch *resource_updates);
    TileVertex makeTileVertex(double lon_deg, double lat_deg, float u, float v) const;
    bool ensureSharedResources();
    bool createTileArrayResources();
    bool arrayBatchingActive() const;
    bool rebuildTileBindings(TileResource *resource);
    bool ensureTileResource(
        GlobeTile &tile, QRhiResourceUpdateBatch *resource_updates,
        QImage *updated_image = nullptr);
    bool ensureTileArrayLayer(
        GlobeTile &tile, const QImage &updated_image,
        QRhiResourceUpdateBatch *resource_updates);
    QImage currentTileArrayImage(
        const GlobeTile &tile, const TileResource &resource) const;
    bool stampTileArrayLayer(
        GlobeTile &tile, const TileResource &resource,
        QRhiResourceUpdateBatch *resource_updates);
    void releaseTileArrayLayer(TileResource *resource);
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
    // Regenerates resource->heatmap_texture (a CPU-rasterized QImage,
    // uploaded once) only when resource->heatmap_revision is stale against
    // this->heatmap_revision, exactly like the tile's own imagery is only
    // re-fetched when its cache key changes -- most frames, for most
    // tiles, this is a single integer comparison and nothing else.
    bool ensureHeatmapTexture(
        const GlobeTile &tile, TileResource *resource, QRhiResourceUpdateBatch *resource_updates);
    QImage renderHeatmapTile(const GlobeTile &tile) const;
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
    // Patch 1 of the Globe batching roadmap intentionally contains one
    // fixed-size page. Windows beyond its 255 usable layers continue wholly
    // through the per-tile fallback; lazy multi-page allocation is later.
    std::unique_ptr<QRhiTexture> tile_array_texture;
    std::unique_ptr<QRhiShaderResourceBindings> array_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> array_pipeline;
    QVector<int> free_array_layers;
    std::map<QString, std::unique_ptr<TileResource>> tile_resources;
    TileResource cap_resource;
    // Mirrors MapRhiBasemapRenderer's identically-named members exactly --
    // see setHeatmapOverlay(). heatmap_revision starts at 1, matching the
    // basemap renderer, so that a freshly-constructed TileResource's
    // default heatmap_revision of 0 is always seen as stale on its first
    // check, forcing (at worst) a single "definitely empty" texture
    // generation rather than an uninitialized-looking mismatch.
    QVector<HeatmapMarker> heatmap_markers;
    double heatmap_radius_m = 0.0;
    double heatmap_solid_fraction = 0.0;
    quint64 heatmap_revision = 1;

    std::unique_ptr<MapRhiTerrainMeshScheduler> terrain_mesh_scheduler;
    quint64 next_terrain_mesh_request_id = 1;
    bool reported_orthometric_datum_warning = false;
    bool reported_unusable_datum_warning = false;
    QElapsedTimer terrain_lod_rebuild_clock;
    bool terrain_lod_rebuild_pending = false;
};

#endif // MAP_RHI_GLOBE_RENDERER_H
