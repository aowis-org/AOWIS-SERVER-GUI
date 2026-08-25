#ifndef MAP_RHI_BASEMAP_RENDERER_H
#define MAP_RHI_BASEMAP_RENDERER_H

#include <QPointF>
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

class MapRhiBasemapRenderer
{
public:
    struct TileVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
    };

    MapRhiBasemapRenderer(MapModel *map_model, MapTileRepository *tile_repository);
    ~MapRhiBasemapRenderer();

    void setTileRepository(MapTileRepository *tile_repository);
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
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
        qint64 pixmap_cache_key = 0;
        quint64 last_used_serial = 0;
    };

    struct VisibleTile
    {
        QString key;
        int virtual_x = 0;
        int y = 0;
        int first_vertex = 0;
        TileResource *resource = nullptr;

        bool operator==(const VisibleTile &other) const
        {
            return this->key == other.key
                && this->virtual_x == other.virtual_x
                && this->y == other.y;
        }
    };

    bool createSharedResources();
    bool rebuildVisibleTiles(const QPointF &origin_world, const QSize &viewport_size);
    bool ensureTileResource(const QString &key, TileResource **resource,
                            QRhiResourceUpdateBatch *resource_updates);
    void pruneTextureCache();

    MapModel *map_model = nullptr;
    MapTileRepository *tile_repository = nullptr;
    QRhi *rhi = nullptr;
    QRhiRenderPassDescriptor *render_pass_descriptor = nullptr;
    QRhiBuffer *camera_uniform_buffer = nullptr;
    int sample_count = 1;

    std::unique_ptr<QRhiBuffer> vertex_buffer;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiTexture> dummy_texture;
    std::unique_ptr<QRhiShaderResourceBindings> template_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    int vertex_buffer_size = 0;
    bool vertex_upload_pending = true;
    bool layout_dirty = true;
    quint64 usage_serial = 0;

    QVector<TileVertex> vertices;
    QVector<VisibleTile> visible_tiles;
    std::map<QString, std::unique_ptr<TileResource>> tile_resources;
};

#endif // MAP_RHI_BASEMAP_RENDERER_H
