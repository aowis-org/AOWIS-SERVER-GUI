#include "map/rhi/map_rhi_globe_network_backend.h"

#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

namespace
{
constexpr int CameraUniformFloatCount = 48;
constexpr int CameraUniformBytes = CameraUniformFloatCount * int(sizeof(float));

int boundedBufferSize(qsizetype vertex_count, qsizetype vertex_size)
{
    const qsizetype bytes = vertex_count * vertex_size;
    if (bytes <= 0)
        return 1;
    if (bytes > qsizetype(std::numeric_limits<int>::max()))
        return 0;
    return int(bytes);
}

bool ensureDynamicVertexBuffer(
    QRhi &rhi,
    std::unique_ptr<QRhiBuffer> *buffer,
    int *buffer_size,
    int required_bytes,
    const QString &failure_message,
    QString *failure_reason)
{
    if (buffer == nullptr || buffer_size == nullptr)
        return false;
    if (*buffer && *buffer_size == required_bytes)
        return true;

    buffer->reset(rhi.newBuffer(
        QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_bytes));
    if (!*buffer || !(*buffer)->create())
    {
        if (failure_reason != nullptr)
            *failure_reason = failure_message;
        return false;
    }

    *buffer_size = required_bytes;
    return true;
}
}

MapRhiGlobeNetworkBackend::MapRhiGlobeNetworkBackend() = default;
MapRhiGlobeNetworkBackend::~MapRhiGlobeNetworkBackend() = default;

void MapRhiGlobeNetworkBackend::reset()
{
    this->underground_junction_instance_buffer.reset();
    this->underground_link_vertex_buffer.reset();
    this->icon_vertex_buffer.reset();
    this->flow_direction_vertex_buffer.reset();
    this->diagnostic_node_vertex_buffer.reset();
    this->diagnostic_link_vertex_buffer.reset();
    this->selected_node_vertex_buffer.reset();
    this->selected_link_vertex_buffer.reset();
    this->node_vertex_buffer.reset();
    this->junction_instance_buffer.reset();
    this->link_vertex_buffer.reset();

    this->underground_junction_instance_buffer_size = 0;
    this->underground_link_vertex_buffer_size = 0;
    this->icon_vertex_buffer_size = 0;
    this->flow_direction_vertex_buffer_size = 0;
    this->diagnostic_node_vertex_buffer_size = 0;
    this->diagnostic_link_vertex_buffer_size = 0;
    this->selected_node_vertex_buffer_size = 0;
    this->selected_link_vertex_buffer_size = 0;
    this->node_vertex_buffer_size = 0;
    this->junction_instance_buffer_size = 0;
    this->link_vertex_buffer_size = 0;

    invalidateAll();
}

void MapRhiGlobeNetworkBackend::invalidateAll()
{
    this->geometry_upload_pending = true;
    this->highlight_upload_pending = true;
    this->flow_direction_upload_pending = true;
    this->icon_upload_pending = true;
    this->underground_upload_pending = true;
    this->junction_instance_upload_pending = true;
}

void MapRhiGlobeNetworkBackend::invalidateGeometry()
{
    this->geometry_upload_pending = true;
}

void MapRhiGlobeNetworkBackend::invalidateHighlights()
{
    this->highlight_upload_pending = true;
}

void MapRhiGlobeNetworkBackend::invalidateFlowDirections()
{
    this->flow_direction_upload_pending = true;
}

void MapRhiGlobeNetworkBackend::invalidateIcons()
{
    this->icon_upload_pending = true;
}

void MapRhiGlobeNetworkBackend::invalidateUnderground()
{
    this->underground_upload_pending = true;
}

void MapRhiGlobeNetworkBackend::invalidateJunctionInstances()
{
    this->junction_instance_upload_pending = true;
}

bool MapRhiGlobeNetworkBackend::ensureBuffers(
    QRhi &rhi,
    const MapGlobeRenderResources &resources,
    QString *failure_reason)
{
    if (!resources.isValid())
    {
        if (failure_reason != nullptr)
            *failure_reason = QStringLiteral("Invalid Globe network render resources");
        return false;
    }

    const int required_link_bytes = boundedBufferSize(
        resources.link_vertices->size(), qsizetype(sizeof(MapNetworkLinkVertex)));
    const int required_node_bytes = boundedBufferSize(
        resources.node_vertices->size(), qsizetype(sizeof(MapNetworkNodeVertex)));
    const int required_selected_link_bytes = boundedBufferSize(
        resources.selected_link_vertices->size(), qsizetype(sizeof(MapNetworkLinkVertex)));
    const int required_selected_node_bytes = boundedBufferSize(
        resources.selected_node_vertices->size(), qsizetype(sizeof(MapNetworkNodeVertex)));
    const int required_diagnostic_link_bytes = boundedBufferSize(
        resources.diagnostic_link_vertices->size(), qsizetype(sizeof(MapNetworkLinkVertex)));
    const int required_diagnostic_node_bytes = boundedBufferSize(
        resources.diagnostic_node_vertices->size(), qsizetype(sizeof(MapNetworkNodeVertex)));
    const int required_flow_direction_bytes = boundedBufferSize(
        resources.flow_direction_vertices->size(), qsizetype(sizeof(MapNetworkLinkVertex)));
    const int required_icon_bytes = boundedBufferSize(
        resources.icon_vertices->size(), qsizetype(sizeof(MapNetworkIconVertex)));
    const int required_underground_link_bytes = boundedBufferSize(
        resources.underground_link_vertices->size(), qsizetype(sizeof(MapNetworkLinkVertex)));
    const int required_underground_junction_bytes = boundedBufferSize(
        resources.underground_junction_instances->size(),
        qsizetype(sizeof(MapGlobeJunctionInstance)));
    const int required_junction_instance_bytes = boundedBufferSize(
        resources.junction_instances->size(), qsizetype(sizeof(MapGlobeJunctionInstance)));

    if (required_link_bytes == 0 || required_node_bytes == 0
        || required_selected_link_bytes == 0 || required_selected_node_bytes == 0
        || required_diagnostic_link_bytes == 0 || required_diagnostic_node_bytes == 0
        || required_flow_direction_bytes == 0 || required_icon_bytes == 0
        || required_underground_link_bytes == 0
        || required_underground_junction_bytes == 0
        || required_junction_instance_bytes == 0)
    {
        if (failure_reason != nullptr)
        {
            *failure_reason = QStringLiteral(
                "RHI globe network geometry exceeds supported buffer size");
        }
        return false;
    }

    const int previous_link_size = this->link_vertex_buffer_size;
    if (!ensureDynamicVertexBuffer(
            rhi, &this->link_vertex_buffer, &this->link_vertex_buffer_size,
            required_link_bytes,
            QStringLiteral("Failed to create RHI globe link vertex buffer"),
            failure_reason))
    {
        return false;
    }
    if (previous_link_size != this->link_vertex_buffer_size)
        invalidateGeometry();

    const int previous_node_size = this->node_vertex_buffer_size;
    if (!ensureDynamicVertexBuffer(
            rhi, &this->node_vertex_buffer, &this->node_vertex_buffer_size,
            required_node_bytes,
            QStringLiteral("Failed to create RHI globe node vertex buffer"),
            failure_reason))
    {
        return false;
    }
    if (previous_node_size != this->node_vertex_buffer_size)
        invalidateGeometry();

    const int previous_selected_link_size = this->selected_link_vertex_buffer_size;
    if (!ensureDynamicVertexBuffer(
            rhi, &this->selected_link_vertex_buffer,
            &this->selected_link_vertex_buffer_size, required_selected_link_bytes,
            QStringLiteral("Failed to create RHI globe selected-link vertex buffer"),
            failure_reason))
    {
        return false;
    }
    if (previous_selected_link_size != this->selected_link_vertex_buffer_size)
        invalidateHighlights();

    const int previous_selected_node_size = this->selected_node_vertex_buffer_size;
    if (!ensureDynamicVertexBuffer(
            rhi, &this->selected_node_vertex_buffer,
            &this->selected_node_vertex_buffer_size, required_selected_node_bytes,
            QStringLiteral("Failed to create RHI globe selected-node vertex buffer"),
            failure_reason))
    {
        return false;
    }
    if (previous_selected_node_size != this->selected_node_vertex_buffer_size)
        invalidateHighlights();

    const int previous_diagnostic_link_size = this->diagnostic_link_vertex_buffer_size;
    if (!ensureDynamicVertexBuffer(
            rhi, &this->diagnostic_link_vertex_buffer,
            &this->diagnostic_link_vertex_buffer_size, required_diagnostic_link_bytes,
            QStringLiteral("Failed to create RHI globe diagnostic-link vertex buffer"),
            failure_reason))
    {
        return false;
    }
    if (previous_diagnostic_link_size != this->diagnostic_link_vertex_buffer_size)
        invalidateHighlights();

    const int previous_diagnostic_node_size = this->diagnostic_node_vertex_buffer_size;
    if (!ensureDynamicVertexBuffer(
            rhi, &this->diagnostic_node_vertex_buffer,
            &this->diagnostic_node_vertex_buffer_size, required_diagnostic_node_bytes,
            QStringLiteral("Failed to create RHI globe diagnostic-node vertex buffer"),
            failure_reason))
    {
        return false;
    }
    if (previous_diagnostic_node_size != this->diagnostic_node_vertex_buffer_size)
        invalidateHighlights();

    const int previous_flow_size = this->flow_direction_vertex_buffer_size;
    if (!ensureDynamicVertexBuffer(
            rhi, &this->flow_direction_vertex_buffer,
            &this->flow_direction_vertex_buffer_size, required_flow_direction_bytes,
            QStringLiteral("Failed to create RHI globe flow-direction vertex buffer"),
            failure_reason))
    {
        return false;
    }
    if (previous_flow_size != this->flow_direction_vertex_buffer_size)
        invalidateFlowDirections();

    const int previous_icon_size = this->icon_vertex_buffer_size;
    if (!ensureDynamicVertexBuffer(
            rhi, &this->icon_vertex_buffer, &this->icon_vertex_buffer_size,
            required_icon_bytes,
            QStringLiteral("Failed to create RHI globe icon vertex buffer"),
            failure_reason))
    {
        return false;
    }
    if (previous_icon_size != this->icon_vertex_buffer_size)
        invalidateIcons();

    const int previous_underground_link_size = this->underground_link_vertex_buffer_size;
    if (!ensureDynamicVertexBuffer(
            rhi, &this->underground_link_vertex_buffer,
            &this->underground_link_vertex_buffer_size, required_underground_link_bytes,
            QStringLiteral("Failed to create RHI globe underground-link vertex buffer"),
            failure_reason))
    {
        return false;
    }
    if (previous_underground_link_size != this->underground_link_vertex_buffer_size)
        invalidateUnderground();

    const int previous_underground_junction_size =
        this->underground_junction_instance_buffer_size;
    if (!ensureDynamicVertexBuffer(
            rhi, &this->underground_junction_instance_buffer,
            &this->underground_junction_instance_buffer_size,
            required_underground_junction_bytes,
            QStringLiteral("Failed to create RHI globe underground-junction instance buffer"),
            failure_reason))
    {
        return false;
    }
    if (previous_underground_junction_size
        != this->underground_junction_instance_buffer_size)
    {
        invalidateUnderground();
    }

    const int previous_junction_size = this->junction_instance_buffer_size;
    if (!ensureDynamicVertexBuffer(
            rhi, &this->junction_instance_buffer, &this->junction_instance_buffer_size,
            required_junction_instance_bytes,
            QStringLiteral("Failed to create RHI globe junction instance buffer"),
            failure_reason))
    {
        return false;
    }
    if (previous_junction_size != this->junction_instance_buffer_size)
        invalidateJunctionInstances();

    return true;
}

void MapRhiGlobeNetworkBackend::updateCameraUniforms(
    QRhiResourceUpdateBatch *resource_updates,
    QRhiBuffer *uniform_buffer,
    QRhi &rhi,
    const MapGlobeRenderFrame &frame,
    const QSize &network_style_texture_size)
{
    if (resource_updates == nullptr || uniform_buffer == nullptr)
        return;

    QMatrix4x4 view_projection = rhi.clipSpaceCorrMatrix();
    view_projection *= frame.canonical_view_projection;

    std::array<float, CameraUniformFloatCount> uniform_data{};
    const float *matrix_data = view_projection.constData();
    for (int index = 0; index < 16; ++index)
        uniform_data[std::size_t(index)] = matrix_data[index];

    uniform_data[16] = float(qMax(1, frame.logical_viewport_size.width()));
    uniform_data[17] = float(qMax(1, frame.logical_viewport_size.height()));
    uniform_data[18] = frame.resources.link_thickness_unit
            == NetworkSymbologySizeUnit::Meters
        ? -float(frame.resources.link_thickness_m * 0.5)
        : float(frame.resources.link_thickness_px) * 0.5f;
    uniform_data[19] = frame.resources.node_size_unit
            == NetworkSymbologySizeUnit::Meters
        ? -float(frame.resources.node_size_m * 0.5)
        : float(frame.resources.node_size_px) * 0.5f;
    uniform_data[20] = frame.output_pixels_per_logical_pixel_x;
    uniform_data[21] = frame.output_pixels_per_logical_pixel_y;
    uniform_data[28] = 0.0f;
    uniform_data[29] = 0.0f;
    uniform_data[30] = float(frame.orbit_distance_m);
    uniform_data[31] = frame.resources.icon_size_unit
            == NetworkSymbologySizeUnit::Meters
        ? -float(frame.resources.icon_size_m)
        : float(frame.resources.icon_size_px);

    uniform_data[32] = frame.camera_basis.right.x();
    uniform_data[33] = frame.camera_basis.right.y();
    uniform_data[34] = frame.camera_basis.right.z();
    uniform_data[35] = rhi.isYUpInNDC() ? 1.0f : -1.0f;
    uniform_data[36] = frame.camera_basis.up.x();
    uniform_data[37] = frame.camera_basis.up.y();
    uniform_data[38] = frame.camera_basis.up.z();
    uniform_data[40] = frame.camera_basis.eye.x();
    uniform_data[41] = frame.camera_basis.eye.y();
    uniform_data[42] = frame.camera_basis.eye.z();
    uniform_data[44] = rhi.isClipDepthZeroToOne() ? 1.0f : 0.5f;
    uniform_data[45] = rhi.isClipDepthZeroToOne() ? 0.0f : 0.5f;
    uniform_data[46] = float(qMax(1, network_style_texture_size.width()));
    uniform_data[47] = float(qMax(1, network_style_texture_size.height()));

    resource_updates->updateDynamicBuffer(
        uniform_buffer, 0, CameraUniformBytes, uniform_data.data());
}

void MapRhiGlobeNetworkBackend::uploadGeometry(
    QRhiResourceUpdateBatch *resource_updates,
    const MapGlobeRenderResources &resources)
{
    if (resource_updates == nullptr || !resources.isValid())
        return;

    if (this->geometry_upload_pending)
    {
        if (!resources.link_vertices->isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->link_vertex_buffer.get(), 0,
                int(resources.link_vertices->size()
                    * qsizetype(sizeof(MapNetworkLinkVertex))),
                resources.link_vertices->constData());
        }
        if (!resources.node_vertices->isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->node_vertex_buffer.get(), 0,
                int(resources.node_vertices->size()
                    * qsizetype(sizeof(MapNetworkNodeVertex))),
                resources.node_vertices->constData());
        }
        this->geometry_upload_pending = false;
    }

    if (this->highlight_upload_pending)
    {
        if (!resources.selected_link_vertices->isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->selected_link_vertex_buffer.get(), 0,
                int(resources.selected_link_vertices->size()
                    * qsizetype(sizeof(MapNetworkLinkVertex))),
                resources.selected_link_vertices->constData());
        }
        if (!resources.selected_node_vertices->isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->selected_node_vertex_buffer.get(), 0,
                int(resources.selected_node_vertices->size()
                    * qsizetype(sizeof(MapNetworkNodeVertex))),
                resources.selected_node_vertices->constData());
        }
        if (!resources.diagnostic_link_vertices->isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->diagnostic_link_vertex_buffer.get(), 0,
                int(resources.diagnostic_link_vertices->size()
                    * qsizetype(sizeof(MapNetworkLinkVertex))),
                resources.diagnostic_link_vertices->constData());
        }
        if (!resources.diagnostic_node_vertices->isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->diagnostic_node_vertex_buffer.get(), 0,
                int(resources.diagnostic_node_vertices->size()
                    * qsizetype(sizeof(MapNetworkNodeVertex))),
                resources.diagnostic_node_vertices->constData());
        }
        this->highlight_upload_pending = false;
    }

    if (this->flow_direction_upload_pending)
    {
        if (!resources.flow_direction_vertices->isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->flow_direction_vertex_buffer.get(), 0,
                int(resources.flow_direction_vertices->size()
                    * qsizetype(sizeof(MapNetworkLinkVertex))),
                resources.flow_direction_vertices->constData());
        }
        this->flow_direction_upload_pending = false;
    }

    if (this->icon_upload_pending)
    {
        if (!resources.icon_vertices->isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->icon_vertex_buffer.get(), 0,
                int(resources.icon_vertices->size()
                    * qsizetype(sizeof(MapNetworkIconVertex))),
                resources.icon_vertices->constData());
        }
        this->icon_upload_pending = false;
    }

    if (this->underground_upload_pending)
    {
        if (!resources.underground_link_vertices->isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->underground_link_vertex_buffer.get(), 0,
                int(resources.underground_link_vertices->size()
                    * qsizetype(sizeof(MapNetworkLinkVertex))),
                resources.underground_link_vertices->constData());
        }
        if (!resources.underground_junction_instances->isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->underground_junction_instance_buffer.get(), 0,
                int(resources.underground_junction_instances->size()
                    * qsizetype(sizeof(MapGlobeJunctionInstance))),
                resources.underground_junction_instances->constData());
        }
        this->underground_upload_pending = false;
    }

    if (this->junction_instance_upload_pending)
    {
        if (!resources.junction_instances->isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->junction_instance_buffer.get(), 0,
                int(resources.junction_instances->size()
                    * qsizetype(sizeof(MapGlobeJunctionInstance))),
                resources.junction_instances->constData());
        }
        this->junction_instance_upload_pending = false;
    }
}

void MapRhiGlobeNetworkBackend::draw(
    QRhiCommandBuffer *command_buffer,
    const MapGlobeRenderResources &resources,
    const MapRhiGlobeNetworkDrawResources &draw_resources) const
{
    if (command_buffer == nullptr || !resources.isValid())
        return;

    const QVector<MapNetworkLinkVertex> &link_vertices = *resources.link_vertices;
    const QVector<MapNetworkNodeVertex> &node_vertices = *resources.node_vertices;
    const QVector<MapNetworkLinkVertex> &selected_link_vertices =
        *resources.selected_link_vertices;
    const QVector<MapNetworkNodeVertex> &selected_node_vertices =
        *resources.selected_node_vertices;
    const QVector<MapNetworkLinkVertex> &diagnostic_link_vertices =
        *resources.diagnostic_link_vertices;
    const QVector<MapNetworkNodeVertex> &diagnostic_node_vertices =
        *resources.diagnostic_node_vertices;
    const QVector<MapNetworkLinkVertex> &flow_direction_vertices =
        *resources.flow_direction_vertices;
    const QVector<MapGlobeJunctionInstance> &junction_instances =
        *resources.junction_instances;

    if (draw_resources.underground_solid)
    {
        if (!link_vertices.isEmpty())
        {
            command_buffer->setGraphicsPipeline(draw_resources.link_no_depth_pipeline);
            command_buffer->setShaderResources();
            const QRhiCommandBuffer::VertexInput binding(this->link_vertex_buffer.get(), 0);
            command_buffer->setVertexInput(0, 1, &binding);
            command_buffer->draw(quint32(link_vertices.size()));
        }
        if (!node_vertices.isEmpty())
        {
            command_buffer->setGraphicsPipeline(draw_resources.node_overlay_pipeline);
            command_buffer->setShaderResources();
            const QRhiCommandBuffer::VertexInput binding(this->node_vertex_buffer.get(), 0);
            command_buffer->setVertexInput(0, 1, &binding);
            command_buffer->draw(quint32(node_vertices.size()));
        }
        if (resources.show_junctions && !junction_instances.isEmpty()
            && draw_resources.junction_impostor_vertex_count > 0)
        {
            command_buffer->setGraphicsPipeline(draw_resources.junction_no_depth_pipeline);
            command_buffer->setShaderResources(
                draw_resources.junction_shader_resource_bindings);
            const QRhiCommandBuffer::VertexInput bindings[] = {
                {draw_resources.junction_mesh_vertex_buffer, 0},
                {this->junction_instance_buffer.get(), 0}
            };
            command_buffer->setVertexInput(0, 2, bindings);
            command_buffer->draw(
                draw_resources.junction_impostor_vertex_count,
                quint32(junction_instances.size()));
        }
        if (!selected_node_vertices.isEmpty())
        {
            command_buffer->setGraphicsPipeline(draw_resources.node_overlay_pipeline);
            command_buffer->setShaderResources();
            const QRhiCommandBuffer::VertexInput binding(
                this->selected_node_vertex_buffer.get(), 0);
            command_buffer->setVertexInput(0, 1, &binding);
            command_buffer->draw(quint32(selected_node_vertices.size()));
        }
        if (!diagnostic_link_vertices.isEmpty())
        {
            command_buffer->setGraphicsPipeline(draw_resources.link_no_depth_pipeline);
            command_buffer->setShaderResources();
            const QRhiCommandBuffer::VertexInput binding(
                this->diagnostic_link_vertex_buffer.get(), 0);
            command_buffer->setVertexInput(0, 1, &binding);
            command_buffer->draw(quint32(diagnostic_link_vertices.size()));
        }
        if (!diagnostic_node_vertices.isEmpty())
        {
            command_buffer->setGraphicsPipeline(draw_resources.node_overlay_pipeline);
            command_buffer->setShaderResources();
            const QRhiCommandBuffer::VertexInput binding(
                this->diagnostic_node_vertex_buffer.get(), 0);
            command_buffer->setVertexInput(0, 1, &binding);
            command_buffer->draw(quint32(diagnostic_node_vertices.size()));
        }
    }

    if (!link_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(draw_resources.link_pipeline);
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(this->link_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(link_vertices.size()));
    }

    if (!flow_direction_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(draw_resources.flow_direction_pipeline);
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(
            this->flow_direction_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(flow_direction_vertices.size()));
    }

    if (!node_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(draw_resources.node_pipeline);
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(this->node_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(node_vertices.size()));
    }

    if (resources.show_junctions && !junction_instances.isEmpty()
        && draw_resources.junction_impostor_vertex_count > 0)
    {
        command_buffer->setGraphicsPipeline(draw_resources.junction_pipeline);
        command_buffer->setShaderResources(draw_resources.junction_shader_resource_bindings);
        const QRhiCommandBuffer::VertexInput bindings[] = {
            {draw_resources.junction_mesh_vertex_buffer, 0},
            {this->junction_instance_buffer.get(), 0}
        };
        command_buffer->setVertexInput(0, 2, bindings);
        command_buffer->draw(
            draw_resources.junction_impostor_vertex_count,
            quint32(junction_instances.size()));
    }

    if (draw_resources.tank_vertex_count > 0)
    {
        command_buffer->setGraphicsPipeline(draw_resources.tank_pipeline);
        command_buffer->setShaderResources(draw_resources.tank_shader_resource_bindings);
        const QRhiCommandBuffer::VertexInput binding(draw_resources.tank_vertex_buffer, 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(draw_resources.tank_vertex_count);
    }

    if (draw_resources.reservoir_vertex_count > 0)
    {
        command_buffer->setGraphicsPipeline(draw_resources.reservoir_pipeline);
        command_buffer->setShaderResources(draw_resources.reservoir_shader_resource_bindings);
        const QRhiCommandBuffer::VertexInput binding(draw_resources.reservoir_vertex_buffer, 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(draw_resources.reservoir_vertex_count);
    }

    if (!selected_link_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(draw_resources.selected_link_pipeline);
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(
            this->selected_link_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(selected_link_vertices.size()));
    }

    if (!selected_node_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(draw_resources.node_pipeline);
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(
            this->selected_node_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(selected_node_vertices.size()));
    }

    if (!diagnostic_link_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(draw_resources.link_pipeline);
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(
            this->diagnostic_link_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(diagnostic_link_vertices.size()));
    }

    if (!diagnostic_node_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(draw_resources.node_pipeline);
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(
            this->diagnostic_node_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(diagnostic_node_vertices.size()));
    }

    if (draw_resources.underground_xray)
    {
        if (!resources.underground_link_vertices->isEmpty())
        {
            command_buffer->setGraphicsPipeline(draw_resources.link_xray_pipeline);
            command_buffer->setShaderResources();
            const QRhiCommandBuffer::VertexInput binding(
                this->underground_link_vertex_buffer.get(), 0);
            command_buffer->setVertexInput(0, 1, &binding);
            command_buffer->draw(quint32(resources.underground_link_vertices->size()));
        }

        if (resources.show_junctions
            && !resources.underground_junction_instances->isEmpty()
            && draw_resources.junction_impostor_vertex_count > 0)
        {
            command_buffer->setGraphicsPipeline(draw_resources.junction_xray_pipeline);
            command_buffer->setShaderResources(
                draw_resources.junction_shader_resource_bindings);
            const QRhiCommandBuffer::VertexInput bindings[] = {
                {draw_resources.junction_mesh_vertex_buffer, 0},
                {this->underground_junction_instance_buffer.get(), 0}
            };
            command_buffer->setVertexInput(0, 2, bindings);
            command_buffer->draw(
                draw_resources.junction_impostor_vertex_count,
                quint32(resources.underground_junction_instances->size()));
        }
    }

    if (!resources.icon_vertices->isEmpty())
    {
        command_buffer->setGraphicsPipeline(draw_resources.icon_overlay_pipeline);
        command_buffer->setShaderResources(draw_resources.icon_shader_resource_bindings);
        const QRhiCommandBuffer::VertexInput binding(this->icon_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(resources.icon_vertices->size()));
    }
}
