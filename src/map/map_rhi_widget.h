#ifndef MAP_RHI_WIDGET_H
#define MAP_RHI_WIDGET_H

#include "map_rhi_basemap_renderer.h"
#include "map_rhi_camera.h"
#include "map_rhi_scene.h"
#include "map_rhi_junction_model.h"
#include "map_rhi_tank_model.h"

#include <QRhiWidget>
#include <QPointF>
#include <QSet>
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

struct MapRhiHit
{
    quint32 render_id = 0;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    QUuid uuid;

    bool isValid() const
    {
        return this->render_id != 0 && this->entity_type != InfrastructureEntity::Unknown;
    }
};

class MapRhiWidget final : public QRhiWidget
{
    Q_OBJECT

public:
    explicit MapRhiWidget(MapModel *map_model, const QString &surface_name,
                          QWidget *parent = nullptr);
    ~MapRhiWidget() override;

    QString graphicsApiName() const;
    MapRhiHit hitTest(const QPointF &screen_position) const;
    void setNetworkSnapshot(const NetworkRenderSnapshot &snapshot);
    void setHiddenEntityUuids(const QSet<QUuid> &hidden_entity_uuids);
    void setNetworkScreenTranslation(const QPointF &translation_pixels);
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
    void rebuildTankModelGeometry();
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
    QPointF network_screen_translation;

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
    std::unique_ptr<QRhiBuffer> tank_vertex_buffer;
    std::unique_ptr<QRhiBuffer> junction_mesh_vertex_buffer;
    std::unique_ptr<QRhiBuffer> junction_instance_buffer;
    std::unique_ptr<QRhiTexture> icon_atlas_texture;
    std::unique_ptr<QRhiTexture> tank_texture;
    std::unique_ptr<QRhiSampler> icon_sampler;
    std::unique_ptr<QRhiSampler> tank_sampler;
    std::unique_ptr<QRhiShaderResourceBindings> shader_resource_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> heatmap_shader_resource_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> icon_shader_resource_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> tank_shader_resource_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> link_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> selected_link_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> node_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> icon_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> heatmap_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> tank_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> junction_pipeline;
    int link_vertex_buffer_size = 0;
    int node_vertex_buffer_size = 0;
    int selected_link_vertex_buffer_size = 0;
    int selected_node_vertex_buffer_size = 0;
    int diagnostic_link_vertex_buffer_size = 0;
    int diagnostic_node_vertex_buffer_size = 0;
    int flow_direction_vertex_buffer_size = 0;
    int icon_vertex_buffer_size = 0;
    int heatmap_vertex_buffer_size = 0;
    int tank_vertex_buffer_size = 0;
    int junction_mesh_vertex_buffer_size = 0;
    int junction_instance_buffer_size = 0;
    bool geometry_upload_pending = true;
    bool highlight_upload_pending = true;
    bool flow_direction_upload_pending = true;
    bool icon_upload_pending = true;
    bool heatmap_upload_pending = true;
    bool tank_upload_pending = true;
    bool junction_mesh_upload_pending = true;
    bool junction_instance_upload_pending = true;
    bool icon_atlas_upload_pending = true;
    bool tank_texture_upload_pending = true;
    QVector<MapRhiTankModelVertex> tank_model_vertices;
    bool symbology_initialized = false;
    bool ready_reported = false;
    bool failure_reported = false;
};

#endif // MAP_RHI_WIDGET_H
