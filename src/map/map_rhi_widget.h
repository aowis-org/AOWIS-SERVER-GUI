#ifndef MAP_RHI_WIDGET_H
#define MAP_RHI_WIDGET_H

#include "map_rhi_camera.h"
#include "map_rhi_scene.h"

#include <QRhiWidget>
#include <QSize>
#include <QString>

#include <memory>

class MapModel;
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiShaderResourceBindings;
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
    void reportFailure(const QString &reason);

    MapModel *map_model = nullptr;
    QString surface_name;
    QRhi *active_rhi = nullptr;
    QRhiRenderPassDescriptor *render_pass_descriptor = nullptr;
    QSize viewport_size;
    MapRhiCamera camera;
    MapRhiScene scene;
    MapRhiSymbology applied_symbology;

    std::unique_ptr<QRhiBuffer> uniform_buffer;
    std::unique_ptr<QRhiBuffer> link_vertex_buffer;
    std::unique_ptr<QRhiBuffer> node_vertex_buffer;
    std::unique_ptr<QRhiBuffer> selected_link_vertex_buffer;
    std::unique_ptr<QRhiBuffer> selected_node_vertex_buffer;
    std::unique_ptr<QRhiBuffer> diagnostic_link_vertex_buffer;
    std::unique_ptr<QRhiBuffer> diagnostic_node_vertex_buffer;
    std::unique_ptr<QRhiBuffer> flow_direction_vertex_buffer;
    std::unique_ptr<QRhiShaderResourceBindings> shader_resource_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> link_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> node_pipeline;
    int link_vertex_buffer_size = 0;
    int node_vertex_buffer_size = 0;
    int selected_link_vertex_buffer_size = 0;
    int selected_node_vertex_buffer_size = 0;
    int diagnostic_link_vertex_buffer_size = 0;
    int diagnostic_node_vertex_buffer_size = 0;
    int flow_direction_vertex_buffer_size = 0;
    bool geometry_upload_pending = true;
    bool highlight_upload_pending = true;
    bool flow_direction_upload_pending = true;
    bool symbology_initialized = false;
    bool ready_reported = false;
    bool failure_reported = false;
};

#endif // MAP_RHI_WIDGET_H
