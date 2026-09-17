#ifndef MAP_RHI_GLOBE_SURFACE_BACKEND_H
#define MAP_RHI_GLOBE_SURFACE_BACKEND_H

#include "map/render/map_globe_surface_render_frame.h"

#include <QColor>
#include <QMatrix4x4>
#include <QSize>
#include <QString>
#include <QVector>

#include <map>
#include <memory>
#include <vector>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiResourceUpdateBatch;
class QRhiReadbackResult;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QRhiTextureRenderTarget;
class QByteArray;
class QImage;

struct MapRhiGlobeImageryArrayPage
{
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
    QVector<int> free_layers;
    int first_draw_index = 0;
    int draw_index_count = 0;
};

struct MapRhiGlobeHeatmapArrayPage
{
    std::unique_ptr<QRhiTexture> texture;
    QVector<int> free_layers;
};

struct MapRhiGlobeTerrainHeightArrayPage
{
    std::unique_ptr<QRhiTexture> texture;
    QVector<int> free_layers;
};

struct MapRhiGlobeHeatmapBakeAtlas
{
    int slot_count = 0;
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiRenderPassDescriptor> render_pass_descriptor;
    std::unique_ptr<QRhiTextureRenderTarget> target;
};

struct MapRhiGlobeHeatmapBakeVertex
{
    float corner_x = 0.0f;
    float corner_y = 0.0f;
};

struct MapRhiGlobeHeatmapBakeInstance
{
    float center_x_pixels = 0.0f;
    float center_y_pixels = 0.0f;
    float radius_pixels = 0.0f;
    float solid_fraction = 0.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    // Maps this tile-local X coordinate into one slot of the horizontal
    // visible-bake atlas. Diagnostic bakes retain 1/0 and therefore keep
    // targeting their standalone square texture.
    float target_scale_x = 1.0f;
    float target_offset_x = 0.0f;
};

struct MapRhiGlobeHeatmapBakeResources
{
    std::unique_ptr<QRhiTexture> diagnostic_texture;
    std::unique_ptr<QRhiRenderPassDescriptor> diagnostic_render_pass_descriptor;
    std::unique_ptr<QRhiTextureRenderTarget> diagnostic_target;
    std::unique_ptr<QRhiShaderResourceBindings> diagnostic_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> diagnostic_pipeline;
    std::unique_ptr<QRhiBuffer> diagnostic_vertex_buffer;
    std::unique_ptr<QRhiBuffer> diagnostic_instance_buffer;
    int diagnostic_instance_buffer_size = 0;
    bool diagnostic_vertex_upload_pending = true;

    std::map<int, MapRhiGlobeHeatmapBakeAtlas> visible_atlases;
    int maximum_visible_atlas_slots = 0;

    std::unique_ptr<QRhiBuffer> visible_instance_buffer;
    int visible_instance_buffer_size = 0;
};

struct MapRhiGlobeHeatmapBakeJob
{
    QRhiTexture *destination_texture = nullptr;
    int destination_layer = 0;
    const QVector<MapRhiGlobeHeatmapBakeInstance> *instances = nullptr;
};

struct MapRhiGlobeHeatmapBakeExecutionStats
{
    int recorded_tiles = 0;
    int recorded_stamps = 0;
    int recorded_passes = 0;
    int recorded_copy_batches = 0;
    int recorded_array_copies = 0;
    int maximum_atlas_slots = 0;
};

struct MapRhiGlobeSurfaceBatch
{
    QRhiShaderResourceBindings *bindings = nullptr;
    int first_draw_index = 0;
    int draw_index_count = 0;
};

struct MapRhiGlobeSurfaceTileDrawState
{
    QRhiShaderResourceBindings *bindings = nullptr;
    bool array_ready = false;
};

struct MapRhiGlobeSurfaceDrawResources
{
    QRhiGraphicsPipeline *tile_pipeline = nullptr;
    QRhiGraphicsPipeline *array_pipeline = nullptr;
    QRhiGraphicsPipeline *heatmap_array_pipeline = nullptr;
    QRhiGraphicsPipeline *wireframe_pipeline = nullptr;

    QRhiShaderResourceBindings *template_bindings = nullptr;
    QRhiShaderResourceBindings *wireframe_bindings = nullptr;

    QVector<MapRhiGlobeSurfaceTileDrawState> window_tiles;
    QVector<MapRhiGlobeSurfaceTileDrawState> cap_tiles;
    std::vector<MapRhiGlobeSurfaceBatch> imagery_array_batches;
    std::vector<MapRhiGlobeSurfaceBatch> heatmap_array_batches;

    bool imagery_array_active = false;
    bool heatmap_array_active = false;
};

class MapRhiGlobeSurfaceBackend
{
public:
    MapRhiGlobeSurfaceBackend();
    ~MapRhiGlobeSurfaceBackend();

    void reset();

    void invalidateWindowGeometry();
    void invalidateWindowVertices();
    void invalidateCaps();
    void invalidateWireframe();

    bool ensureSharedResources(
        QRhi *rhi,
        QRhiRenderPassDescriptor *render_pass_descriptor,
        int sample_count);
    bool ensureImageryArrayPipeline(
        QRhi *rhi,
        QRhiRenderPassDescriptor *render_pass_descriptor,
        int sample_count);
    bool ensureHeatmapArrayPipeline(
        QRhi *rhi,
        QRhiRenderPassDescriptor *render_pass_descriptor,
        int sample_count);
    void invalidateRenderPassPipelines();

    bool uploadPendingMissingTileTexture(
        QRhiResourceUpdateBatch *resource_updates,
        const QColor &missing_tile_color);
    bool uploadPendingHeatmapDummyTexture(
        QRhiResourceUpdateBatch *resource_updates);
    bool uploadCameraUniform(
        QRhiResourceUpdateBatch *resource_updates,
        const QMatrix4x4 &view_projection,
        float heatmap_opacity,
        const QColor &background_color,
        float background_opacity);

    bool rebuildTileBindings(
        QRhi *rhi,
        QRhiTexture *imagery_texture,
        QRhiTexture *heatmap_texture,
        std::unique_ptr<QRhiShaderResourceBindings> *bindings);
    bool rebuildHeatmapArrayBindings(
        QRhi *rhi,
        QRhiTexture *imagery_texture,
        QRhiTexture *heatmap_texture,
        std::unique_ptr<QRhiShaderResourceBindings> *bindings);

    bool recreateRgba8Texture(
        QRhi *rhi,
        const QSize &size,
        std::unique_ptr<QRhiTexture> *texture);

    bool supportsTextureArrays(QRhi *rhi) const;
    bool supportsR32fTextures(QRhi *rhi) const;
    bool isYUpInNdc(QRhi *rhi) const;

    void uploadTextureImage(
        QRhiResourceUpdateBatch *resource_updates,
        QRhiTexture *texture,
        const QImage &image) const;
    void uploadTextureArrayLayer(
        QRhiResourceUpdateBatch *resource_updates,
        QRhiTexture *texture,
        int layer,
        const QImage &image) const;
    void uploadTextureArrayLayerRaw(
        QRhiResourceUpdateBatch *resource_updates,
        QRhiTexture *texture,
        int layer,
        const QByteArray &data) const;

    quint64 createTileGpuResource();
    void releaseTileGpuResource(quint64 resource_id);
    void clearTileGpuResources();
    bool hasTileTexture(quint64 resource_id) const;
    bool hasTileHeatmapTexture(quint64 resource_id) const;
    bool hasTileBindings(quint64 resource_id) const;
    QRhiTexture *tileTexture(quint64 resource_id) const;
    QRhiTexture *tileHeatmapTexture(quint64 resource_id) const;
    QRhiShaderResourceBindings *tileBindings(quint64 resource_id) const;
    void invalidateTileBindings(quint64 resource_id);
    void clearTileHeatmapTexture(quint64 resource_id);
    bool recreateTileTexture(
        QRhi *rhi, quint64 resource_id, const QSize &size);
    bool recreateTileHeatmapTexture(
        QRhi *rhi, quint64 resource_id, const QSize &size);
    bool rebuildTileBindings(
        QRhi *rhi, quint64 resource_id);

    void releaseDiagnosticHeatmapBakeResources();
    void releaseVisibleHeatmapBakeAtlasResources();
    bool ensureHeatmapBakeResources(
        QRhi *rhi,
        int texture_size);
    MapRhiGlobeHeatmapBakeAtlas *ensureVisibleHeatmapBakeAtlasResources(
        QRhi *rhi,
        int slot_count,
        int texture_size);
    int maximumVisibleHeatmapBakeAtlasSlots(
        QRhi *rhi,
        int texture_size,
        int maximum_slots);
    bool ensureVisibleHeatmapBakeInstanceBuffer(
        QRhi *rhi,
        int required_bytes);
    bool ensureDiagnosticHeatmapBakeInstanceBuffer(
        QRhi *rhi,
        int required_bytes);
    bool recordVisibleHeatmapBakes(
        QRhi *rhi,
        QRhiCommandBuffer *command_buffer,
        const QVector<MapRhiGlobeHeatmapBakeJob> &jobs,
        int texture_size,
        int array_layer_count,
        int maximum_atlas_slots,
        MapRhiGlobeHeatmapBakeExecutionStats *stats);
    bool recordDiagnosticHeatmapBake(
        QRhi *rhi,
        QRhiCommandBuffer *command_buffer,
        const QVector<MapRhiGlobeHeatmapBakeInstance> &instances,
        int texture_size,
        QString *failure_reason);
    bool queueDiagnosticHeatmapReadback(
        QRhi *rhi,
        QRhiCommandBuffer *command_buffer,
        QRhiReadbackResult *readback_result) const;

    bool fallbackTextureUploadsPending() const;
    bool imageryArrayPipelineReady() const;
    bool heatmapArrayPipelineReady() const;
    void populateSharedDrawResources(
        MapRhiGlobeSurfaceDrawResources *draw_resources) const;

    bool hasPendingGeometryUploads(
        const MapGlobeSurfaceRenderFrame &frame) const;

    bool uploadGeometry(
        QRhi *rhi,
        QRhiResourceUpdateBatch *resource_updates,
        const MapGlobeSurfaceRenderFrame &frame);

    bool uploadImageryArrayDrawIndices(
        QRhi *rhi,
        QRhiResourceUpdateBatch *resource_updates,
        const QVector<quint32> &indices,
        qsizetype maximum_index_count);
    bool uploadHeatmapArrayDrawIndices(
        QRhi *rhi,
        QRhiResourceUpdateBatch *resource_updates,
        const QVector<quint32> &indices,
        qsizetype maximum_index_count);
    bool uploadHeatmapArrayLayers(
        QRhi *rhi,
        QRhiResourceUpdateBatch *resource_updates,
        const QVector<float> &layers);

    bool createImageryArrayPage(
        QRhi *rhi,
        const QSize &layer_size,
        int layer_count,
        int maximum_page_count);
    bool createHeatmapArrayPage(
        QRhi *rhi,
        QRhiResourceUpdateBatch *resource_updates,
        const QSize &layer_size,
        int layer_count,
        int maximum_page_count);
    bool createTerrainHeightArrayPage(
        QRhi *rhi,
        const QSize &layer_size,
        int layer_count,
        int maximum_page_count);

    // Applies a small CPU-side window-vertex change directly to the retained
    // GPU allocation when that allocation is current and large enough.
    // Returns false when the caller must request a full window upload instead.
    bool patchWindowVertices(
        QRhiResourceUpdateBatch *resource_updates,
        int first_vertex,
        qsizetype vertex_count,
        const QVector<MapGlobeSurfaceVertex> &vertices);

    bool patchHeatmapArrayLayers(
        QRhiResourceUpdateBatch *resource_updates,
        int first_vertex,
        qsizetype vertex_count,
        const QVector<float> &layers);

    void draw(
        QRhiCommandBuffer *command_buffer,
        const MapGlobeSurfaceRenderFrame &frame,
        const MapRhiGlobeSurfaceDrawResources &draw_resources) const;

    std::vector<MapRhiGlobeImageryArrayPage> &imageryArrayPages();
    const std::vector<MapRhiGlobeImageryArrayPage> &imageryArrayPages() const;
    std::vector<MapRhiGlobeHeatmapArrayPage> &heatmapArrayPages();
    const std::vector<MapRhiGlobeHeatmapArrayPage> &heatmapArrayPages() const;
    std::vector<MapRhiGlobeTerrainHeightArrayPage> &terrainHeightArrayPages();
    const std::vector<MapRhiGlobeTerrainHeightArrayPage> &terrainHeightArrayPages() const;

private:
    struct TileGpuResource
    {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiTexture> heatmap_texture;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
    };

    TileGpuResource *tileGpuResource(quint64 resource_id);
    const TileGpuResource *tileGpuResource(quint64 resource_id) const;

    std::map<quint64, TileGpuResource> tile_gpu_resources;
    quint64 next_tile_gpu_resource_id = 1;

    std::unique_ptr<QRhiBuffer> camera_uniform_buffer;
    std::unique_ptr<QRhiSampler> sampler_resource;
    std::unique_ptr<QRhiTexture> dummy_texture;
    std::unique_ptr<QRhiTexture> heatmap_dummy_texture;
    bool dummy_texture_upload_pending = true;
    bool heatmap_dummy_texture_upload_pending = true;
    std::unique_ptr<QRhiShaderResourceBindings> template_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> wireframe_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> heatmap_array_template_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> tile_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> wireframe_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> imagery_array_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> heatmap_array_pipeline;

    std::unique_ptr<QRhiBuffer> window_vertex_buffer;
    std::unique_ptr<QRhiBuffer> window_index_buffer;
    int window_vertex_buffer_size = 0;
    int window_index_buffer_size = 0;
    bool window_vertex_upload_pending = true;
    bool window_index_upload_pending = true;

    std::unique_ptr<QRhiBuffer> cap_vertex_buffer;
    std::unique_ptr<QRhiBuffer> cap_index_buffer;
    int cap_vertex_buffer_size = 0;
    int cap_index_buffer_size = 0;
    bool cap_vertex_upload_pending = true;
    bool cap_index_upload_pending = true;

    std::unique_ptr<QRhiBuffer> wireframe_vertex_buffer;
    int wireframe_vertex_buffer_size = 0;
    bool wireframe_vertex_upload_pending = true;

    std::unique_ptr<QRhiBuffer> tile_array_draw_index_buffer;
    int tile_array_draw_index_buffer_size = 0;
    std::unique_ptr<QRhiBuffer> heatmap_array_draw_index_buffer;
    int heatmap_array_draw_index_buffer_size = 0;
    std::unique_ptr<QRhiBuffer> heatmap_array_layer_buffer;
    int heatmap_array_layer_buffer_size = 0;

    std::vector<MapRhiGlobeImageryArrayPage> imagery_array_pages;
    std::vector<MapRhiGlobeHeatmapArrayPage> heatmap_array_pages;
    std::vector<MapRhiGlobeTerrainHeightArrayPage> terrain_height_array_pages;
    MapRhiGlobeHeatmapBakeResources heatmap_bake_resources;
};

#endif // MAP_RHI_GLOBE_SURFACE_BACKEND_H
