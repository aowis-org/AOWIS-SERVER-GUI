#ifndef MAP_RHI_WIDGET_H
#define MAP_RHI_WIDGET_H

#include "map_rhi_basemap_renderer.h"
#include "map_rhi_camera.h"
#include "map_rhi_scene.h"

#include <QRhiWidget>
#include <QSize>
#include <QString>

#include <memory>

class MapModel;
class MapTileRepository;
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QResizeEvent;

class MapRhiWidget final : public QRhiWidget
{
    Q_OBJECT

public:
    explicit MapRhiWidget(MapModel *map_model, const QString &surface_name,
                          QWidget *parent = nullptr);
    ~MapRhiWidget() override;

    QString graphicsApiName() const;
    void setNetworkSnapshot(const NetworkRenderSnapshot &snapshot);
    void setSymbology(const MapRhiSymbology &symbology);
    void setTileRepository(MapTileRepository *tile_repository);
    void setBackgroundOpacity(int opacity);
    void setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid);
    void setSimulationErrorEntities(
        const QHash<QUuid, InfrastructureEntity> &error_entities,
        const QSet<QUuid> &stale_entity_uuids);

signals:
    void signalRendererReady();
    void signalRendererFailed(const QString &reason);

protected:
    void initialize(QRhiCommandBuffer *command_buffer) override;
    void render(QRhiCommandBuffer *command_buffer) override;
    void releaseResources() override;
    void resizeEvent(QResizeEvent *event) override;

private:
    bool createPersistentResources();
    bool createPipelines();
    bool ensureGeometryBuffers();
    void resetGpuResources();
    void syncViewState();
    QPointF renderOriginWorld() const;
    float heatmapRadiusPixels() const;
    void reportFailure(const QString &reason);

    MapModel *map_model = nullptr;
    QString surface_name;
    QRhi *active_rhi = nullptr;
    QRhiRenderPassDescriptor *render_pass_descriptor = nullptr;
    QSize viewport_size;
    QPointF fallback_origin_world;
    MapRhiCamera camera;
    MapRhiScene scene;
    MapRhiSymbology applied_symbology;
    MapTileRepository *tile_repository = nullptr;
    std::unique_ptr<MapRhiBasemapRenderer> basemap_renderer;
    int background_opacity = 0;

    std::unique_ptr<QRhiBuffer> uniform_buffer;
    std::unique_ptr<QRhiBuffer> link_vertex_buffer;
    std::unique_ptr<QRhiBuffer> node_vertex_buffer;
    std::unique_ptr<QRhiBuffer> selected_link_vertex_buffer;
    std::unique_ptr<QRhiBuffer> selected_node_vertex_buffer;
    std::unique_ptr<QRhiBuffer> diagnostic_link_vertex_buffer;
    std::unique_ptr<QRhiBuffer> diagnostic_node_vertex_buffer;
    std::unique_ptr<QRhiBuffer> flow_direction_vertex_buffer;
    std::unique_ptr<QRhiBuffer> icon_vertex_buffer;
    std::unique_ptr<QRhiBuffer> heatmap_vertex_buffer;
    std::unique_ptr<QRhiTexture> icon_atlas_texture;
    std::unique_ptr<QRhiSampler> icon_sampler;
    std::unique_ptr<QRhiShaderResourceBindings> shader_resource_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> heatmap_shader_resource_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> icon_shader_resource_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> link_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> node_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> icon_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> heatmap_pipeline;
    int link_vertex_buffer_size = 0;
    int node_vertex_buffer_size = 0;
    int selected_link_vertex_buffer_size = 0;
    int selected_node_vertex_buffer_size = 0;
    int diagnostic_link_vertex_buffer_size = 0;
    int diagnostic_node_vertex_buffer_size = 0;
    int flow_direction_vertex_buffer_size = 0;
    int icon_vertex_buffer_size = 0;
    int heatmap_vertex_buffer_size = 0;
    bool geometry_upload_pending = true;
    bool highlight_upload_pending = true;
    bool flow_direction_upload_pending = true;
    bool icon_upload_pending = true;
    bool heatmap_upload_pending = true;
    bool icon_atlas_upload_pending = true;
    bool symbology_initialized = false;
    bool ready_reported = false;
    bool failure_reported = false;
};

#endif // MAP_RHI_WIDGET_H
