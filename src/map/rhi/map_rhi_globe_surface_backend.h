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
    QVector<int> free_layers;
    int first_draw_index = 0;
    int draw_index_count = 0;
};

struct MapRhiGlobeHeatmapArrayPage
{
    QVector<int> free_layers;
};

struct MapRhiGlobeTerrainHeightArrayPage
{
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

enum class MapRhiGlobeHeatmapBakeDestination
{
    TileHeatmapTexture,
    HeatmapArrayLayer
};

struct MapRhiGlobeHeatmapBakeJob
{
    MapRhiGlobeHeatmapBakeDestination destination =
        MapRhiGlobeHeatmapBakeDestination::TileHeatmapTexture;
    quint64 tile_gpu_resource_id = 0;
    int heatmap_array_page = -1;
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
    int imagery_page_index = -1;
    int heatmap_page_index = -1;
    int first_draw_index = 0;
    int draw_index_count = 0;
};

struct MapRhiGlobeSurfaceTileDrawState
{
    quint64 gpu_resource_id = 0;
    bool array_ready = false;
};

struct MapRhiGlobeSurfaceDrawResources
{
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

    bool hasContext() const;
    bool contextMatches(QRhi *rhi) const;
    bool renderPassMatches(
        QRhiRenderPassDescriptor *render_pass_descriptor,
        int sample_count) const;
    void setContext(
        QRhi *rhi,
        QRhiRenderPassDescriptor *render_pass_descriptor,
        int sample_count);

    void invalidateWindowGeometry();
    void invalidateWindowVertices();
    void invalidateCaps();
    void invalidateWireframe();

    bool ensureSharedResources(
        QRhi *rhi,
        QRhiRenderPassDescriptor *render_pass_descriptor,
        int sample_count);
    bool ensureSharedResources();
    bool ensureImageryArrayPipeline(
        QRhi *rhi,
        QRhiRenderPassDescriptor *render_pass_descriptor,
        int sample_count);
    bool ensureImageryArrayPipeline();
    bool ensureHeatmapArrayPipeline(
        QRhi *rhi,
        QRhiRenderPassDescriptor *render_pass_descriptor,
        int sample_count);
    bool ensureHeatmapArrayPipeline();
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

    bool ensureHeatmapArrayBatchBinding(
        QRhi *rhi, int imagery_page_index, int heatmap_page_index);
    bool ensureHeatmapArrayBatchBinding(
        int imagery_page_index, int heatmap_page_index);
    void clearHeatmapArrayBatchBindings();

    bool supportsTextureArrays(QRhi *rhi) const;
    bool supportsR32fTextures(QRhi *rhi) const;
    bool isYUpInNdc(QRhi *rhi) const;
    bool supportsTextureArrays() const;
    bool supportsR32fTextures() const;
    bool isYUpInNdc() const;

    bool uploadTileTextureImage(
        QRhiResourceUpdateBatch *resource_updates,
        quint64 resource_id,
        const QImage &image) const;
    bool uploadTileHeatmapTextureImage(
        QRhiResourceUpdateBatch *resource_updates,
        quint64 resource_id,
        const QImage &image) const;
    bool imageryArrayPageReady(int page_index) const;
    bool heatmapArrayPageReady(int page_index) const;
    bool terrainHeightArrayPageReady(int page_index) const;
    bool uploadImageryArrayPageLayer(
        QRhiResourceUpdateBatch *resource_updates,
        int page_index, int layer, const QImage &image) const;
    bool uploadHeatmapArrayPageLayer(
        QRhiResourceUpdateBatch *resource_updates,
        int page_index, int layer, const QImage &image) const;
    bool uploadTerrainHeightArrayPageLayerRaw(
        QRhiResourceUpdateBatch *resource_updates,
        int page_index, int layer, const QByteArray &data) const;

    quint64 createTileGpuResource();
    void releaseTileGpuResource(quint64 resource_id);
    void clearTileGpuResources();
    bool hasTileTexture(quint64 resource_id) const;
    bool hasTileHeatmapTexture(quint64 resource_id) const;
    bool hasTileBindings(quint64 resource_id) const;
    void invalidateTileBindings(quint64 resource_id);
    void clearTileHeatmapTexture(quint64 resource_id);
    bool recreateTileTexture(
        QRhi *rhi, quint64 resource_id, const QSize &size);
    bool recreateTileHeatmapTexture(
        QRhi *rhi, quint64 resource_id, const QSize &size);
    bool rebuildTileBindings(
        QRhi *rhi, quint64 resource_id);
    bool recreateTileTexture(
        quint64 resource_id, const QSize &size);
    bool recreateTileHeatmapTexture(
        quint64 resource_id, const QSize &size);
    bool rebuildTileBindings(quint64 resource_id);

    void releaseDiagnosticHeatmapBakeResources();
    void releaseVisibleHeatmapBakeAtlasResources();
    bool ensureHeatmapBakeResources(
        QRhi *rhi,
        int texture_size);
    bool ensureHeatmapBakeResources(int texture_size);
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
    bool recordVisibleHeatmapBakes(
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
    bool recordDiagnosticHeatmapBake(
        QRhiCommandBuffer *command_buffer,
        const QVector<MapRhiGlobeHeatmapBakeInstance> &instances,
        int texture_size,
        QString *failure_reason);
    bool queueDiagnosticHeatmapReadback(
        QRhi *rhi,
        QRhiCommandBuffer *command_buffer,
        QRhiReadbackResult *readback_result) const;
    bool queueDiagnosticHeatmapReadback(
        QRhiCommandBuffer *command_buffer,
        QRhiReadbackResult *readback_result) const;

    bool fallbackTextureUploadsPending() const;
    bool imageryArrayPipelineReady() const;
    bool heatmapArrayPipelineReady() const;

    bool hasPendingGeometryUploads(
        const MapGlobeSurfaceRenderFrame &frame) const;

    bool uploadGeometry(
        QRhi *rhi,
        QRhiResourceUpdateBatch *resource_updates,
        const MapGlobeSurfaceRenderFrame &frame);
    bool uploadGeometry(
        QRhiResourceUpdateBatch *resource_updates,
        const MapGlobeSurfaceRenderFrame &frame);

    bool uploadImageryArrayDrawIndices(
        QRhi *rhi,
        QRhiResourceUpdateBatch *resource_updates,
        const QVector<quint32> &indices,
        qsizetype maximum_index_count);
    bool uploadImageryArrayDrawIndices(
        QRhiResourceUpdateBatch *resource_updates,
        const QVector<quint32> &indices,
        qsizetype maximum_index_count);
    bool uploadHeatmapArrayDrawIndices(
        QRhi *rhi,
        QRhiResourceUpdateBatch *resource_updates,
        const QVector<quint32> &indices,
        qsizetype maximum_index_count);
    bool uploadHeatmapArrayDrawIndices(
        QRhiResourceUpdateBatch *resource_updates,
        const QVector<quint32> &indices,
        qsizetype maximum_index_count);
    bool uploadImageryArrayLayers(
        QRhi *rhi,
        QRhiResourceUpdateBatch *resource_updates,
        const QVector<float> &layers);
    bool uploadImageryArrayLayers(
        QRhiResourceUpdateBatch *resource_updates,
        const QVector<float> &layers);
    bool uploadHeatmapArrayLayers(
        QRhi *rhi,
        QRhiResourceUpdateBatch *resource_updates,
        const QVector<float> &layers);
    bool uploadHeatmapArrayLayers(
        QRhiResourceUpdateBatch *resource_updates,
        const QVector<float> &layers);

    bool createImageryArrayPage(
        QRhi *rhi,
        const QSize &layer_size,
        int layer_count,
        int maximum_page_count);
    bool createImageryArrayPage(
        const QSize &layer_size,
        int layer_count,
        int maximum_page_count);
    bool createHeatmapArrayPage(
        QRhi *rhi,
        QRhiResourceUpdateBatch *resource_updates,
        const QSize &layer_size,
        int layer_count,
        int maximum_page_count);
    bool createHeatmapArrayPage(
        QRhiResourceUpdateBatch *resource_updates,
        const QSize &layer_size,
        int layer_count,
        int maximum_page_count);
    bool createTerrainHeightArrayPage(
        QRhi *rhi,
        const QSize &layer_size,
        int layer_count,
        int maximum_page_count);
    bool createTerrainHeightArrayPage(
        const QSize &layer_size,
        int layer_count,
        int maximum_page_count);
    void popImageryArrayPage();
    void popHeatmapArrayPage();
    void clearTerrainHeightArrayPages();

    // Applies a small CPU-side window-vertex change directly to the retained
    // GPU allocation when that allocation is current and large enough.
    // Returns false when the caller must request a full window upload instead.
    bool patchWindowVertices(
        QRhiResourceUpdateBatch *resource_updates,
        int first_vertex,
        qsizetype vertex_count,
        const QVector<MapGlobeSurfaceVertex> &vertices);

    bool patchImageryArrayLayers(
        QRhiResourceUpdateBatch *resource_updates,
        int first_vertex,
        qsizetype vertex_count,
        const QVector<float> &layers);

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
    bool rebuildTileBindings(
        QRhi *rhi,
        QRhiTexture *imagery_texture,
        QRhiTexture *heatmap_texture,
        std::unique_ptr<QRhiShaderResourceBindings> *bindings);
    bool recreateRgba8Texture(
        QRhi *rhi,
        const QSize &size,
        std::unique_ptr<QRhiTexture> *texture);

    struct ImageryArrayPageGpuResource
    {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
    };

    struct HeatmapArrayPageGpuResource
    {
        std::unique_ptr<QRhiTexture> texture;
    };

    struct TerrainHeightArrayPageGpuResource
    {
        std::unique_ptr<QRhiTexture> texture;
    };

    struct TileGpuResource
    {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiTexture> heatmap_texture;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
    };

    TileGpuResource *tileGpuResource(quint64 resource_id);
    const TileGpuResource *tileGpuResource(quint64 resource_id) const;

    QRhi *rhi = nullptr;
    QRhiRenderPassDescriptor *render_pass_descriptor = nullptr;
    int sample_count = 1;
    QRhiShaderResourceBindings *tileBindings(quint64 resource_id) const;
    QRhiTexture *heatmapBakeDestinationTexture(
        const MapRhiGlobeHeatmapBakeJob &job) const;
    QRhiShaderResourceBindings *heatmapArrayBatchBinding(
        int imagery_page_index, int heatmap_page_index) const;
    static quint64 heatmapArrayBatchBindingKey(
        int imagery_page_index, int heatmap_page_index);

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
    std::unique_ptr<QRhiBuffer> imagery_array_layer_buffer;
    int imagery_array_layer_buffer_size = 0;
    std::unique_ptr<QRhiBuffer> heatmap_array_draw_index_buffer;
    int heatmap_array_draw_index_buffer_size = 0;
    std::unique_ptr<QRhiBuffer> heatmap_array_layer_buffer;
    int heatmap_array_layer_buffer_size = 0;

    std::vector<MapRhiGlobeImageryArrayPage> imagery_array_pages;
    std::vector<ImageryArrayPageGpuResource> imagery_array_page_gpu_resources;
    std::vector<MapRhiGlobeHeatmapArrayPage> heatmap_array_pages;
    std::vector<HeatmapArrayPageGpuResource> heatmap_array_page_gpu_resources;
    std::vector<MapRhiGlobeTerrainHeightArrayPage> terrain_height_array_pages;
    std::vector<TerrainHeightArrayPageGpuResource>
        terrain_height_array_page_gpu_resources;
    std::map<quint64, std::unique_ptr<QRhiShaderResourceBindings>>
        heatmap_array_batch_bindings;
    MapRhiGlobeHeatmapBakeResources heatmap_bake_resources;
};

#endif // MAP_RHI_GLOBE_SURFACE_BACKEND_H
