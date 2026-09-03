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
// This is intentionally much simpler than MapRhiBasemapRenderer: the globe
// is a whole-planet overview, not a navigable close-up surface, so there is
// no LOD streaming, no terrain relief, and no network entity rendering here
// (yet) -- just a fixed, low zoom level tile grid of basemap imagery draped
// over the ellipsoid, plus simple flat-colored polar caps covering the area
// above/below Web Mercator's +-85.05 degree limit that basemap tiles don't
// reach. The imagery zoom level is intentionally decoupled from the 2D/3D
// zoom (see MapModel::tileCacheKeyAtZoom()/tileEndpointAtZoom()): panning
// into a close 2D/3D zoom should not also fetch dozens of high zoom globe
// tiles, and the globe's own zoom has no reason to track it.
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

    // Uploads any pending geometry/camera data and requests any imagery
    // tiles that are not yet cached. Must be called before draw() each
    // frame, inside the same resource-update batch that beginPass() below
    // will consume.
    bool prepare(QRhiResourceUpdateBatch *resource_updates,
                const QMatrix4x4 &view_projection, const QSize &viewport_size);
    void draw(QRhiCommandBuffer *command_buffer);

    // Drops all cached tile textures/bindings so the next prepare() call
    // re-requests and re-uploads them, without rebuilding the (static)
    // ellipsoid mesh itself. Used when the imagery provider changes, since
    // the tile keys/endpoints already requested are for the old provider.
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

    void buildMesh();
    void buildPolarCap(bool north);
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

    QVector<TileVertex> tile_vertices;
    QVector<GlobeTile> tiles;
    bool mesh_built = false;
    bool tiles_requested = false;
    bool tile_vertex_upload_pending = true;

    std::unique_ptr<QRhiBuffer> tile_vertex_buffer;
    int tile_vertex_buffer_size = 0;
    std::unique_ptr<QRhiBuffer> camera_uniform_buffer;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiTexture> dummy_texture;
    std::unique_ptr<QRhiShaderResourceBindings> template_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    std::map<QString, std::unique_ptr<TileResource>> tile_resources;
    TileResource cap_resource;
};

#endif // MAP_RHI_GLOBE_RENDERER_H
