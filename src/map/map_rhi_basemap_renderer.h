#ifndef MAP_RHI_BASEMAP_RENDERER_H
#define MAP_RHI_BASEMAP_RENDERER_H

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QPointF>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVector>

#include <map>
#include <memory>

class MapModel;
class MapRhiCamera;
class MapRhiScene;
class MapTerrainRepository;
struct MapTerrainTile;
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

class MapRhiBasemapRenderer
{
public:
    struct HeatmapMarker
    {
        QPointF center;
        QColor color;

        bool operator==(const HeatmapMarker &other) const
        {
            return this->center == other.center && this->color == other.color;
        }
    };

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

    MapRhiBasemapRenderer(MapModel *map_model, MapRhiScene *scene,
                          MapTileRepository *tile_repository,
                          MapTerrainRepository *terrain_repository = nullptr);
    ~MapRhiBasemapRenderer();

    void setTileRepository(MapTileRepository *tile_repository);
    void setTerrainRepository(MapTerrainRepository *terrain_repository);
    void setCamera(const MapRhiCamera *camera);
    void notifyTerrainTileAvailable(const QString &key);
    void setHeatmapOverlay(const QVector<HeatmapMarker> &markers,
                           double radius_world, double solid_fraction);
    void setHeatmapStyle(double radius_world, double solid_fraction);
    void setWireframeVisible(bool visible);
    void setMapVisible(bool visible);
    void invalidate();
    void releaseResources();

    bool initialize(QRhi *rhi, QRhiRenderPassDescriptor *render_pass_descriptor,
                    QRhiBuffer *camera_uniform_buffer, int sample_count);
    bool prepare(QRhiResourceUpdateBatch *resource_updates,
                 const QPointF &origin_world, const QSize &viewport_size);
    void draw(QRhiCommandBuffer *command_buffer);

private:
    struct TileResource
    {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiTexture> heatmap_texture;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
        qint64 pixmap_cache_key = 0;
        quint64 heatmap_revision = 0;
        quint64 last_used_serial = 0;
    };

    struct VisibleTile
    {
        QString imagery_key;
        QString terrain_key;
        int virtual_x = 0;
        int tile_x = 0;
        int y = 0;
        int imagery_zoom = 0;
        int terrain_zoom = 0;
        int first_vertex = 0;
        int vertex_count = 0;
        bool foreground = false;
        int terrain_cell_count = 0;
        TileResource *resource = nullptr;

        bool operator==(const VisibleTile &other) const
        {
            return this->imagery_key == other.imagery_key
                && this->terrain_key == other.terrain_key
                && this->virtual_x == other.virtual_x
                && this->tile_x == other.tile_x
                && this->y == other.y
                && this->imagery_zoom == other.imagery_zoom
                && this->terrain_zoom == other.terrain_zoom
                && this->foreground == other.foreground
                && this->terrain_cell_count == other.terrain_cell_count;
        }
    };

    bool createSharedResources();
    bool rebuildVisibleTiles(const QPointF &origin_world, const QSize &viewport_size);
    bool updateDirtyTerrainTiles(QRhiResourceUpdateBatch *resource_updates);
    bool currentLayoutCoversForeground(int imagery_zoom, int foreground_start_x,
                                       int foreground_start_y, int foreground_tiles_x,
                                       int foreground_tiles_y, int tile_count,
                                       const QString &imagery_key_prefix) const;
    int terrainCellCountForTile(const VisibleTile &tile,
                                const QSize &viewport_size) const;
    bool currentTerrainLodMatches(const QSize &viewport_size) const;
    bool tileReadyForZoomHandoff(const VisibleTile &tile, bool relief_enabled) const;
    QVector<VisibleTile> progressiveProviderLayout(
        const QVector<VisibleTile> &target_tiles, int target_zoom,
        const QString &imagery_key_prefix, bool relief_enabled) const;
    QVector<VisibleTile> progressiveZoomLayout(
        const QVector<VisibleTile> &target_tiles, int target_zoom,
        bool relief_enabled) const;
    bool ensureTileResource(const VisibleTile &tile, TileResource **resource,
                            QRhiResourceUpdateBatch *resource_updates);
    bool ensureHeatmapTexture(const VisibleTile &tile, TileResource *resource,
                              QRhiResourceUpdateBatch *resource_updates);
    bool rebuildTileBindings(TileResource *resource);
    bool isTileInViewFrustum(const VisibleTile &tile, const QSize &viewport_size,
                             const QPointF &origin_world) const;
    QImage renderHeatmapTile(const VisibleTile &tile) const;
    void rebuildHeatmapMarkerBuckets();
    QVector<int> heatmapMarkerCandidates(double tile_left, double tile_top,
                                         double tile_right, double tile_bottom,
                                         double radius_world) const;
    void pruneTextureCache();
    void rebuildWireframeVertices();
    bool uploadWireframeVertices(QRhiResourceUpdateBatch *resource_updates);
    void appendFlatTileVertices(QVector<TileVertex> *target, VisibleTile *tile,
                                float left, float top, float right, float bottom);
    bool appendReliefTileVertices(QVector<TileVertex> *target, VisibleTile *tile,
                                  const MapTerrainTile *terrain_tile,
                                  float tile_left, float tile_top,
                                  float tile_world_size);
    float terrainElevationWorldZ(double elevation_m) const;

    MapModel *map_model = nullptr;
    MapRhiScene *scene = nullptr;
    MapTileRepository *tile_repository = nullptr;
    MapTerrainRepository *terrain_repository = nullptr;
    const MapRhiCamera *camera = nullptr;
    QRhi *rhi = nullptr;
    QRhiRenderPassDescriptor *render_pass_descriptor = nullptr;
    QRhiBuffer *camera_uniform_buffer = nullptr;
    int sample_count = 1;

    std::unique_ptr<QRhiBuffer> vertex_buffer;
    std::unique_ptr<QRhiBuffer> wireframe_vertex_buffer;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiTexture> dummy_texture;
    std::unique_ptr<QRhiShaderResourceBindings> template_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> wireframe_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> wireframe_pipeline;
    int vertex_buffer_size = 0;
    int wireframe_vertex_buffer_size = 0;
    bool vertex_upload_pending = true;
    bool wireframe_vertex_upload_pending = true;
    bool dummy_texture_upload_pending = true;
    bool layout_dirty = true;
    QElapsedTimer terrain_lod_rebuild_clock;
    quint64 usage_serial = 0;
    bool wireframe_visible = false;
    bool map_visible = true;

    QVector<TileVertex> vertices;
    QVector<WireframeVertex> wireframe_vertices;
    QVector<VisibleTile> visible_tiles;
    QSet<QString> dirty_terrain_keys;
    QPointF layout_origin_world;
    QVector<HeatmapMarker> heatmap_markers;
    QHash<quint64, QVector<int>> heatmap_marker_buckets;
    double heatmap_radius_world = 0.0;
    double heatmap_solid_fraction = 0.0;
    quint64 heatmap_revision = 1;
    std::map<QString, std::unique_ptr<TileResource>> tile_resources;
};

#endif // MAP_RHI_BASEMAP_RENDERER_H
