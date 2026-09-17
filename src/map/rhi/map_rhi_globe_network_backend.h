#ifndef MAP_RHI_GLOBE_NETWORK_BACKEND_H
#define MAP_RHI_GLOBE_NETWORK_BACKEND_H

#include "map/render/map_globe_render_frame.h"

#include <QSize>
#include <QString>

#include <memory>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiResourceUpdateBatch;
class QRhiShaderResourceBindings;

struct MapRhiGlobeNetworkDrawResources
{
    QRhiBuffer *junction_mesh_vertex_buffer = nullptr;
    QRhiBuffer *tank_vertex_buffer = nullptr;
    QRhiBuffer *reservoir_vertex_buffer = nullptr;

    QRhiShaderResourceBindings *junction_shader_resource_bindings = nullptr;
    QRhiShaderResourceBindings *icon_shader_resource_bindings = nullptr;
    QRhiShaderResourceBindings *tank_shader_resource_bindings = nullptr;
    QRhiShaderResourceBindings *reservoir_shader_resource_bindings = nullptr;

    QRhiGraphicsPipeline *link_pipeline = nullptr;
    QRhiGraphicsPipeline *flow_direction_pipeline = nullptr;
    QRhiGraphicsPipeline *selected_link_pipeline = nullptr;
    QRhiGraphicsPipeline *node_pipeline = nullptr;
    QRhiGraphicsPipeline *node_overlay_pipeline = nullptr;
    QRhiGraphicsPipeline *icon_overlay_pipeline = nullptr;
    QRhiGraphicsPipeline *tank_pipeline = nullptr;
    QRhiGraphicsPipeline *reservoir_pipeline = nullptr;
    QRhiGraphicsPipeline *junction_pipeline = nullptr;
    QRhiGraphicsPipeline *junction_no_depth_pipeline = nullptr;
    QRhiGraphicsPipeline *link_xray_pipeline = nullptr;
    QRhiGraphicsPipeline *junction_xray_pipeline = nullptr;
    QRhiGraphicsPipeline *link_no_depth_pipeline = nullptr;

    quint32 junction_impostor_vertex_count = 0;
    quint32 tank_vertex_count = 0;
    quint32 reservoir_vertex_count = 0;
    bool underground_solid = false;
    bool underground_xray = false;
};

class MapRhiGlobeNetworkBackend
{
public:
    MapRhiGlobeNetworkBackend();
    ~MapRhiGlobeNetworkBackend();

    void reset();

    void invalidateAll();
    void invalidateGeometry();
    void invalidateHighlights();
    void invalidateFlowDirections();
    void invalidateIcons();
    void invalidateUnderground();
    void invalidateJunctionInstances();

    bool ensureBuffers(
        QRhi &rhi,
        const MapGlobeRenderResources &resources,
        QString *failure_reason);
    void updateCameraUniforms(
        QRhiResourceUpdateBatch *resource_updates,
        QRhiBuffer *uniform_buffer,
        QRhi &rhi,
        const MapGlobeRenderFrame &frame,
        const QSize &network_style_texture_size);
    void uploadGeometry(
        QRhiResourceUpdateBatch *resource_updates,
        const MapGlobeRenderResources &resources);
    void draw(
        QRhiCommandBuffer *command_buffer,
        const MapGlobeRenderResources &resources,
        const MapRhiGlobeNetworkDrawResources &draw_resources) const;

private:
    std::unique_ptr<QRhiBuffer> link_vertex_buffer;
    std::unique_ptr<QRhiBuffer> node_vertex_buffer;
    std::unique_ptr<QRhiBuffer> junction_instance_buffer;
    std::unique_ptr<QRhiBuffer> selected_link_vertex_buffer;
    std::unique_ptr<QRhiBuffer> selected_node_vertex_buffer;
    std::unique_ptr<QRhiBuffer> diagnostic_link_vertex_buffer;
    std::unique_ptr<QRhiBuffer> diagnostic_node_vertex_buffer;
    std::unique_ptr<QRhiBuffer> flow_direction_vertex_buffer;
    std::unique_ptr<QRhiBuffer> icon_vertex_buffer;
    std::unique_ptr<QRhiBuffer> underground_link_vertex_buffer;
    std::unique_ptr<QRhiBuffer> underground_junction_instance_buffer;

    int link_vertex_buffer_size = 0;
    int node_vertex_buffer_size = 0;
    int junction_instance_buffer_size = 0;
    int selected_link_vertex_buffer_size = 0;
    int selected_node_vertex_buffer_size = 0;
    int diagnostic_link_vertex_buffer_size = 0;
    int diagnostic_node_vertex_buffer_size = 0;
    int flow_direction_vertex_buffer_size = 0;
    int icon_vertex_buffer_size = 0;
    int underground_link_vertex_buffer_size = 0;
    int underground_junction_instance_buffer_size = 0;

    bool geometry_upload_pending = true;
    bool highlight_upload_pending = true;
    bool flow_direction_upload_pending = true;
    bool icon_upload_pending = true;
    bool underground_upload_pending = true;
    bool junction_instance_upload_pending = true;
};

#endif // MAP_RHI_GLOBE_NETWORK_BACKEND_H
