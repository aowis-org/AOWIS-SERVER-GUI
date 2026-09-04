#ifndef MAP_RHI_GLOBE_RENDERER_H
#define MAP_RHI_GLOBE_RENDERER_H

#include <QColor>
#include <QMatrix4x4>
#include <QSize>
#include <QString>
#include <QVector>

#include <map>
#include <memory>

class MapModel;
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

// Renders planet Earth as a WGS84 ellipsoid for the "Globe" map view mode.
//
// This mirrors MapRhiBasemapRenderer's own architecture rather than
// reinventing one: a single dynamic imagery zoom level (picked from camera
// distance, playing the same role MapModel::zoom() plays for 2D/3D), a tile
// window covering the visible area plus a retention margin so ordinary
// panning does not immediately fall outside it, and a "does the current
// window still cover what's needed" dirty check so the mesh/tile requests
// are only rebuilt when the window actually needs to move or the zoom level
// changes -- not every frame. There is no terrain relief or network entity
// rendering on the globe yet, and (unlike the flat renderer) no per-tile
// view-frustum culling: the retained tile window is derived from samples of
// the actual projected ellipsoid boundary, including the visible limb. This
// keeps the complete on-screen globe footprint covered even where Mercator
// tile density changes strongly or longitude wraps.
//
// Two independent pieces of geometry:
//  - "window" tiles: the dynamic, zoom/pan-dependent basemap imagery grid
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

    struct TileResource
    {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
        qint64 pixmap_cache_key = -1;
    };

    struct GlobeTile
    {
        int tile_x = 0;
        int tile_y = 0;
        int zoom = 0;
        bool is_cap = false;
        int first_vertex = 0;
        int vertex_count = 0;
        QString imagery_key;
        // Non-owning; points into tile_resources (or at cap_resource for
        // polar caps) and is only valid for the frame it was resolved in.
        TileResource *resource = nullptr;
    };

    void buildCaps();
    void buildPolarCap(bool north);
    void rebuildWindow(int zoom, int x_min, int x_max, int y_min, int y_max, int tile_span);
    void pruneUnusedTileResources();
    static TileVertex makeTileVertex(double lon_deg, double lat_deg, float u, float v);
    bool ensureSharedResources();
    bool rebuildTileBindings(TileResource *resource);
    bool ensureTileResource(GlobeTile &tile, QRhiResourceUpdateBatch *resource_updates);
    void requestMissingTiles(QRhiResourceUpdateBatch *resource_updates);

    MapModel *map_model = nullptr;
    MapTileRepository *tile_repository = nullptr;
    QRhi *rhi = nullptr;
    QRhiRenderPassDescriptor *render_pass_descriptor = nullptr;
    int sample_count = 1;

    // Dynamic imagery window (see class comment above).
    QVector<TileVertex> window_vertices;
    QVector<GlobeTile> window_tiles;
    int window_zoom = -1; // -1 == not yet built
    int window_tile_x_min = 0;
    int window_tile_x_max = -1;
    int window_tile_y_min = 0;
    int window_tile_y_max = -1;
    bool window_dirty = true;
    bool window_tiles_requested = false;
    bool window_vertex_upload_pending = false;
    std::unique_ptr<QRhiBuffer> window_vertex_buffer;
    int window_vertex_buffer_size = 0;

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
    std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    std::map<QString, std::unique_ptr<TileResource>> tile_resources;
    TileResource cap_resource;
};

#endif // MAP_RHI_GLOBE_RENDERER_H
