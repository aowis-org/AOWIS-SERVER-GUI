#ifndef MAP_RHI_GLOBE_RENDERER_H
#define MAP_RHI_GLOBE_RENDERER_H

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

// Zoom/tile-index identity of one leaf produced by the visible-region
// quadtree walk (selectVisibleGlobeQuadtreeLeaves() in the .cpp). Plain POD
// so it can cross from the free-function quadtree walk into
// MapRhiGlobeRenderer's private API without pulling geo/ or RHI headers in
// here. Replaces the old single-zoom rectangular tile window: every leaf
// carries its own zoom, so near-camera ground can stay at fine detail while
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
//    imagery tile at any zoom will ever cover. Built once and never
//    rebuilt; entirely unrelated to the LOD system above.
class MapRhiGlobeRenderer
{
public:
    MapRhiGlobeRenderer(MapModel *map_model, MapTileRepository *tile_repository);
    ~MapRhiGlobeRenderer();

    void setTileRepository(MapTileRepository *tile_repository);
    void setTerrainRepository(MapTerrainRepository *terrain_repository);
    void notifyTerrainTileAvailable(const QString &key);
    void invalidateTerrain();
    void setWireframeVisible(bool visible);
    void setMapVisible(bool visible);
    bool hasPendingTerrainMeshes() const;

    // Called whenever the RHI/render pass may have changed, same contract
    // as MapRhiBasemapRenderer::initialize(). Safe to call every frame; all
    // work below is guarded by "already created" checks.
    bool initialize(QRhi *rhi, QRhiRenderPassDescriptor *render_pass_descriptor,
                    int sample_count);

    // Recomputes the visible tile window (rebuilding it only if the zoom
    // level or window actually needs to change), uploads any pending
    // geometry/camera data, and requests any imagery tiles that are not yet
    // cached. Must be called before draw() each frame, inside the same
    // resource-update batch that beginPass() below will consume.
    bool prepare(QRhiResourceUpdateBatch *resource_updates,
                const QMatrix4x4 &view_projection, const QSize &viewport_size);
    void draw(QRhiCommandBuffer *command_buffer);

    // Drops all cached tile textures/bindings and forces the window to be
    // rebuilt from scratch on the next prepare() call. Used when the
    // imagery provider changes, since the tile keys/endpoints already
    // requested are for the old provider.
    void invalidateImagery();

    // True once every currently-visible, terrain-eligible tile (i.e. every
    // window tile with a non-empty terrain_key -- low-zoom/distant leaves
    // below GlobeTerrainReliefMinimumZoom are intentionally flat and don't
    // count) has an applied terrain mesh, or once there is nothing to wait
    // for at all (no terrain repository configured). False while imagery
    // has arrived but the DEM/relief mesh for the same area is still being
    // fetched or meshed on the background scheduler -- the window during
    // which anything drawn at real elevation (e.g. network geometry laid on
    // top of this renderer's output) would appear to float above the still-
    // flat terrain until relief catches up. See
    // MapRhiWidget::renderGlobe()/MapRhiGlobeNetworkScene::setTerrainReady().
    bool isVisibleTerrainReady() const;

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
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
        qint64 pixmap_cache_key = -1;
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
    void appendWireframeEdges(const QVector<TileVertex> &vertices);
    bool uploadWireframeVertices(QRhiResourceUpdateBatch *resource_updates);
    static TileVertex makeTileVertex(double lon_deg, double lat_deg, float u, float v);
    bool ensureSharedResources();
    bool rebuildTileBindings(TileResource *resource);
    bool ensureTileResource(GlobeTile &tile, QRhiResourceUpdateBatch *resource_updates);
    // See the definition's own comment: derives a cropped/upscaled
    // placeholder from the nearest already-loaded ancestor tile when
    // tile's own imagery isn't cached yet.
    bool ensureProvisionalTileResource(
        GlobeTile &tile, TileResource *resource, QRhiResourceUpdateBatch *resource_updates);
    void requestMissingTiles(QRhiResourceUpdateBatch *resource_updates);
    void requestMissingTerrainTiles();
    void scheduleReadyTerrainMeshes();
    bool applyReadyTerrainMeshes(QRhiResourceUpdateBatch *resource_updates);

    MapModel *map_model = nullptr;
    MapTileRepository *tile_repository = nullptr;
    MapTerrainRepository *terrain_repository = nullptr;
    QRhi *rhi = nullptr;
    QRhiRenderPassDescriptor *render_pass_descriptor = nullptr;
    int sample_count = 1;

    // Dynamic imagery window (see class comment above).
    QVector<TileVertex> window_vertices;
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
    std::unique_ptr<QRhiBuffer> window_vertex_buffer;
    int window_vertex_buffer_size = 0;

    QVector<WireframeVertex> wireframe_vertices;
    bool wireframe_vertex_upload_pending = true;
    std::unique_ptr<QRhiBuffer> wireframe_vertex_buffer;
    int wireframe_vertex_buffer_size = 0;
    bool wireframe_visible = false;
    bool map_visible = true;

    // Static polar caps (see class comment above).
    QVector<TileVertex> cap_vertices;
    QVector<GlobeTile> cap_tiles;
    bool caps_built = false;
    bool cap_vertex_upload_pending = true;
    std::unique_ptr<QRhiBuffer> cap_vertex_buffer;

    std::unique_ptr<QRhiBuffer> camera_uniform_buffer;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiTexture> dummy_texture;
    bool dummy_texture_upload_pending = true;
    std::unique_ptr<QRhiShaderResourceBindings> template_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> wireframe_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> wireframe_pipeline;
    std::map<QString, std::unique_ptr<TileResource>> tile_resources;
    TileResource cap_resource;

    std::unique_ptr<MapRhiTerrainMeshScheduler> terrain_mesh_scheduler;
    quint64 next_terrain_mesh_request_id = 1;
    bool reported_orthometric_datum_warning = false;
    bool reported_unusable_datum_warning = false;
    QElapsedTimer terrain_lod_rebuild_clock;
    bool terrain_lod_rebuild_pending = false;
};

#endif // MAP_RHI_GLOBE_RENDERER_H
