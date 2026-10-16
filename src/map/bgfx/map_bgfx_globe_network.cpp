#include "map/bgfx/map_bgfx_globe_network.h"

#include "map/bgfx/map_bgfx_backend.h"
#include "map/core/map_model.h"
#include "map/render/map_globe_camera.h"
#include "map/render/map_globe_render_frame.h"
#include "map/render/map_globe_reservoir_model.h"
#include "map/render/map_globe_tank_model.h"
#include "network/network_symbology_rendering.h"

#include <QByteArray>
#include <QDebug>
#include <QImage>
#include <QMatrix4x4>
#include <QVector3D>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace
{
// Premultiplied "over", as every QRhi globe network pipeline.
constexpr uint64_t kBlendState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA
    | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
// Depth-tested and written (pipes, markers, junctions): LessEqual.
constexpr uint64_t kDepthState = kBlendState | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LEQUAL;
// Depth-tested, not written (flow chevrons).
constexpr uint64_t kDepthTestState = kBlendState | BGFX_STATE_DEPTH_TEST_LEQUAL;
// No depth test (Solid mode, selection outlines, X-Ray runs, icons).
constexpr uint64_t kOverlayState = kBlendState;
// Tank and reservoir models: back faces culled (QRhi: Back with CCW front
// faces).
constexpr uint64_t kModelState = kDepthState | BGFX_STATE_CULL_CW;
constexpr uint64_t kAlbedoSamplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
// The six corners of an impostor quad (MapGlobeJunctionImpostorVertex).
const float kImpostorCorners[6][2] = {
    {-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};

template <typename Handle>
void destroyHandle(Handle *handle)
{
    if (bgfx::isValid(*handle))
    {
        bgfx::destroy(*handle);
        *handle = BGFX_INVALID_HANDLE;
    }
}

// Bytes after the last attribute (render id, entity type, padding).
template <typename Vertex>
uint8_t tailBytes(std::size_t last_attribute_end)
{
    return uint8_t(sizeof(Vertex) - last_attribute_end);
}

// RGBA8 texture with its complete mip chain (premultiplied, smooth
// scaling), as the 2D icon atlas: the models are seen at every distance.
bgfx::TextureHandle mipmappedTexture(const QImage &source, uint64_t flags)
{
    if (source.isNull())
        return BGFX_INVALID_HANDLE;
    QImage level = source.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const int width = level.width();
    const int height = level.height();
    QByteArray pixels;
    int level_width = width;
    int level_height = height;
    while (true)
    {
        for (int row = 0; row < level.height(); ++row)
            pixels.append(reinterpret_cast<const char *>(level.constScanLine(row)), level.width() * 4);
        if (level_width == 1 && level_height == 1)
            break;
        level_width = std::max(1, level_width / 2);
        level_height = std::max(1, level_height / 2);
        level = level.scaled(level_width, level_height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                    .convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    }
    return bgfx::createTexture2D(uint16_t(width), uint16_t(height), true, 1, bgfx::TextureFormat::RGBA8,
                                 flags, bgfx::copy(pixels.constData(), uint32_t(pixels.size())));
}

bool sameSymbologySizes(const MapNetworkRenderSymbology &a, const MapNetworkRenderSymbology &b)
{
    return a.link_thickness_unit == b.link_thickness_unit && a.link_thickness_px == b.link_thickness_px
        && a.link_thickness_m == b.link_thickness_m;
}
}

MapBgfxGlobeNetwork::MapBgfxGlobeNetwork() = default;

MapBgfxGlobeNetwork::~MapBgfxGlobeNetwork() = default;

bool MapBgfxGlobeNetwork::initialize()
{
    // Vertex layouts: the scene's structs uploaded unchanged; the trailing
    // render id / entity type are skipped.
    this->link_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 1, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord3, 3, bgfx::AttribType::Float)
        .skip(tailBytes<MapNetworkLinkVertex>(offsetof(MapNetworkLinkVertex, width_direction_z) + sizeof(float)))
        .end();
    this->node_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 1, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord4, 1, bgfx::AttribType::Float)
        .skip(tailBytes<MapNetworkNodeVertex>(offsetof(MapNetworkNodeVertex, metric_billboard) + sizeof(float)))
        .end();
    this->icon_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord5, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .skip(tailBytes<MapNetworkIconVertex>(offsetof(MapNetworkIconVertex, alpha) + sizeof(float)))
        .end();
    this->junction_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 1, bgfx::AttribType::Float)
        .end();
    // Tank and reservoir model vertices share one layout.
    static_assert(sizeof(MapGlobeTankModelVertex) == sizeof(MapGlobeReservoirModelVertex),
                  "Tank and reservoir model vertices must share one layout");
    this->model_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord5, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 1, bgfx::AttribType::Float)
        .skip(tailBytes<MapGlobeTankModelVertex>(offsetof(MapGlobeTankModelVertex, selected) + sizeof(float)))
        .end();
    if (this->link_layout.getStride() != sizeof(MapNetworkLinkVertex)
        || this->node_layout.getStride() != sizeof(MapNetworkNodeVertex)
        || this->icon_layout.getStride() != sizeof(MapNetworkIconVertex)
        || this->junction_layout.getStride() != sizeof(JunctionVertex)
        || this->model_layout.getStride() != sizeof(MapGlobeTankModelVertex))
    {
        qWarning() << "bgfx globe network: vertex layouts do not match the scene's vertex structs";
        return false;
    }

    this->link_program = MapBgfxBackend::loadProgram("vs_globe_link", "fs_globe_link");
    this->link_xray_program = MapBgfxBackend::loadProgram("vs_globe_link", "fs_globe_link_xray");
    this->flow_program = MapBgfxBackend::loadProgram("vs_globe_flow", "fs_globe_link");
    this->node_program = MapBgfxBackend::loadProgram("vs_globe_node", "fs_globe_node");
    this->junction_program = MapBgfxBackend::loadProgram("vs_globe_junction", "fs_globe_junction");
    this->junction_xray_program = MapBgfxBackend::loadProgram("vs_globe_junction", "fs_globe_junction_xray");
    this->icon_program = MapBgfxBackend::loadProgram("vs_globe_icon", "fs_globe_icon");
    this->model_program = MapBgfxBackend::loadProgram("vs_globe_model", "fs_globe_model");
    this->view_projection_uniform = bgfx::createUniform("u_net_view_proj", bgfx::UniformType::Mat4);
    this->viewport_uniform = bgfx::createUniform("u_net_viewport", bgfx::UniformType::Vec4);
    this->params_uniform = bgfx::createUniform("u_net_params", bgfx::UniformType::Vec4);
    this->camera_right_uniform = bgfx::createUniform("u_net_camera_right", bgfx::UniformType::Vec4);
    this->camera_up_uniform = bgfx::createUniform("u_net_camera_up", bgfx::UniformType::Vec4);
    this->camera_eye_uniform = bgfx::createUniform("u_net_camera_eye", bgfx::UniformType::Vec4);
    this->icon_sampler = bgfx::createUniform("s_net_icons", bgfx::UniformType::Sampler);
    this->albedo_sampler = bgfx::createUniform("s_net_albedo", bgfx::UniformType::Sampler);
    this->tank_texture = mipmappedTexture(mapGlobeTankAlbedoImage(), kAlbedoSamplerFlags);
    this->reservoir_texture = mipmappedTexture(mapGlobeReservoirAlbedoImage(), kAlbedoSamplerFlags);
    // Everything goes up again on the (new) device.
    this->dirty_streams = AllStreams;
    if (!ready())
    {
        qWarning() << "bgfx globe network unavailable: programs" << bgfx::isValid(this->link_program)
                   << bgfx::isValid(this->link_xray_program) << bgfx::isValid(this->flow_program)
                   << bgfx::isValid(this->node_program) << bgfx::isValid(this->junction_program)
                   << bgfx::isValid(this->junction_xray_program) << bgfx::isValid(this->icon_program)
                   << bgfx::isValid(this->model_program);
        release();
        return false;
    }
    return true;
}

bool MapBgfxGlobeNetwork::ready() const
{
    return bgfx::isValid(this->link_program) && bgfx::isValid(this->link_xray_program)
        && bgfx::isValid(this->flow_program) && bgfx::isValid(this->node_program)
        && bgfx::isValid(this->junction_program) && bgfx::isValid(this->junction_xray_program)
        && bgfx::isValid(this->icon_program) && bgfx::isValid(this->model_program)
        && bgfx::isValid(this->view_projection_uniform) && bgfx::isValid(this->viewport_uniform)
        && bgfx::isValid(this->params_uniform) && bgfx::isValid(this->camera_right_uniform)
        && bgfx::isValid(this->camera_up_uniform) && bgfx::isValid(this->camera_eye_uniform)
        && bgfx::isValid(this->icon_sampler) && bgfx::isValid(this->albedo_sampler)
        && bgfx::isValid(this->tank_texture) && bgfx::isValid(this->reservoir_texture);
}

void MapBgfxGlobeNetwork::destroyBuffers()
{
    for (Buffer *buffer : {&this->link_buffer, &this->node_buffer, &this->selected_link_buffer,
                           &this->selected_node_buffer, &this->diagnostic_link_buffer,
                           &this->diagnostic_node_buffer, &this->flow_buffer, &this->icon_buffer,
                           &this->underground_link_buffer, &this->junction_buffer,
                           &this->underground_junction_buffer, &this->tank_buffer, &this->reservoir_buffer})
    {
        destroyHandle(&buffer->handle);
        buffer->count = 0;
    }
}

void MapBgfxGlobeNetwork::release()
{
    destroyBuffers();
    destroyHandle(&this->reservoir_texture);
    destroyHandle(&this->tank_texture);
    destroyHandle(&this->albedo_sampler);
    destroyHandle(&this->icon_sampler);
    destroyHandle(&this->camera_eye_uniform);
    destroyHandle(&this->camera_up_uniform);
    destroyHandle(&this->camera_right_uniform);
    destroyHandle(&this->params_uniform);
    destroyHandle(&this->viewport_uniform);
    destroyHandle(&this->view_projection_uniform);
    destroyHandle(&this->model_program);
    destroyHandle(&this->icon_program);
    destroyHandle(&this->junction_xray_program);
    destroyHandle(&this->junction_program);
    destroyHandle(&this->node_program);
    destroyHandle(&this->flow_program);
    destroyHandle(&this->link_xray_program);
    destroyHandle(&this->link_program);
    this->dirty_streams = AllStreams;
}

// --- Inputs ---------------------------------------------------------------

void MapBgfxGlobeNetwork::setNetworkSnapshot(const NetworkRenderSnapshot &snapshot)
{
    this->snapshot = snapshot;
    this->snapshot_pending = true;
}

void MapBgfxGlobeNetwork::setHiddenEntityUuids(const QSet<QUuid> &uuids)
{
    if (this->hidden_entity_uuids == uuids)
        return;
    this->hidden_entity_uuids = uuids;
    this->hidden_pending = true;
}

void MapBgfxGlobeNetwork::setSymbology(const MapNetworkRenderSymbology &symbology)
{
    this->symbology = symbology;
    this->symbology_pending = true;
}

void MapBgfxGlobeNetwork::setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid)
{
    if (this->selected_entity_type == entity_type && this->selected_entity_uuid == uuid)
        return;
    this->selected_entity_type = entity_type;
    this->selected_entity_uuid = uuid;
    this->selection_pending = true;
}

void MapBgfxGlobeNetwork::setSimulationErrorEntities(
    const QHash<QUuid, InfrastructureEntity> &error_entities, const QSet<QUuid> &stale_entity_uuids)
{
    if (this->simulation_error_entities == error_entities
        && this->simulation_stale_entity_uuids == stale_entity_uuids)
        return;
    this->simulation_error_entities = error_entities;
    this->simulation_stale_entity_uuids = stale_entity_uuids;
    this->errors_pending = true;
}

void MapBgfxGlobeNetwork::setUse3dIconModels(bool enabled)
{
    if (this->use_3d_icon_models == enabled)
        return;
    this->use_3d_icon_models = enabled;
    this->icon_models_pending = true;
}

void MapBgfxGlobeNetwork::setNodeDeclutteringEnabled(bool enabled)
{
    if (this->node_decluttering_enabled == enabled)
        return;
    this->node_decluttering_enabled = enabled;
    this->declutter_pending = true;
}

void MapBgfxGlobeNetwork::setGroundOffsetM(double offset_m)
{
    this->ground_offset_m = offset_m;
    this->ground_offset_pending = true;
}

void MapBgfxGlobeNetwork::setVerticalExaggeration(double exaggeration)
{
    this->vertical_exaggeration = exaggeration;
    this->exaggeration_pending = true;
}

void MapBgfxGlobeNetwork::setTerrainElevationResolver(MapGlobeNetworkScene::TerrainElevationResolver resolver)
{
    this->network_scene.setTerrainElevationResolver(std::move(resolver));
}

void MapBgfxGlobeNetwork::setUndergroundMode(MapUndergroundMode mode)
{
    if (this->underground_mode == mode)
        return;
    this->underground_mode = mode;
    this->underground_pending = true;
}

bool MapBgfxGlobeNetwork::refreshUndergroundXRay()
{
    synchronize();
    if (!this->network_scene.refreshUndergroundXRayGeometry())
        return false;
    this->dirty_streams |= UndergroundStream;
    return true;
}

void MapBgfxGlobeNetwork::synchronize()
{
    // Placement inputs first: each rebuilds the whole network geometry.
    if (this->exaggeration_pending)
    {
        this->exaggeration_pending = false;
        if (this->network_scene.setVerticalExaggeration(this->vertical_exaggeration))
            this->dirty_streams = AllStreams;
    }
    if (this->ground_offset_pending)
    {
        this->ground_offset_pending = false;
        if (this->network_scene.setGroundOffsetM(this->ground_offset_m))
            this->dirty_streams = AllStreams;
    }
    if (this->declutter_pending)
    {
        this->declutter_pending = false;
        if (this->network_scene.setNodeDeclutteringEnabled(this->node_decluttering_enabled))
            this->dirty_streams = AllStreams;
    }
    if (this->hidden_pending)
    {
        this->hidden_pending = false;
        if (this->network_scene.setHiddenEntityUuids(this->hidden_entity_uuids))
            this->dirty_streams = AllStreams;
    }
    if (this->snapshot_pending)
    {
        this->snapshot_pending = false;
        const quint64 revision = this->network_scene.geometryRevision();
        this->network_scene.setNetworkSnapshot(this->snapshot);
        if (this->network_scene.geometryRevision() != revision)
        {
            this->dirty_streams = AllStreams;
            this->has_attachments = !this->snapshot.demand_point_attachments.isEmpty();
            this->junction_uuids.clear();
            for (const NetworkRenderNode &node : this->snapshot.nodes)
            {
                if (node.entity_type == InfrastructureEntity::Junction)
                    this->junction_uuids.insert(node.render_id, node.uuid);
            }
        }
        // Only the geometry is needed here: the copy's lists are released.
        this->snapshot = NetworkRenderSnapshot();
    }
    if (this->symbology_pending)
    {
        this->symbology_pending = false;
        // What changed decides the streams, as MapRhiWidget::setSymbology().
        const MapNetworkRenderSymbology &previous = this->applied_symbology;
        const MapNetworkRenderSymbology &next = this->symbology;
        const bool first = !this->symbology_applied;
        const bool node_colors_changed = first || previous.node_colors != next.node_colors;
        const bool link_colors_changed = first || previous.link_colors != next.link_colors;
        const bool junctions_changed = first || previous.show_junctions != next.show_junctions
            || previous.node_size_unit != next.node_size_unit
            || (next.node_size_unit == NetworkSymbologySizeUnit::Meters && previous.node_size_m != next.node_size_m);
        const bool thickness_changed = first || !sameSymbologySizes(previous, next);
        const bool icons_changed = first || previous.icon_size_unit != next.icon_size_unit
            || previous.icon_size_px != next.icon_size_px || previous.icon_size_m != next.icon_size_m
            || previous.show_icons != next.show_icons
            || previous.icon_default_fill_color != next.icon_default_fill_color
            || previous.visual_node != next.visual_node || previous.visual_link != next.visual_link
            || node_colors_changed || link_colors_changed;
        const bool flow_changed = first || previous.show_flow_direction != next.show_flow_direction
            || previous.flow_direction_size_px != next.flow_direction_size_px
            || previous.flow_directions != next.flow_directions || thickness_changed || link_colors_changed;
        this->network_scene.setSymbology(next);
        this->applied_symbology = next;
        this->symbology_applied = true;
        if (node_colors_changed || link_colors_changed)
            this->dirty_streams |= GeometryStream;
        // Junction colours are resolved on the CPU (junctionVertices()).
        if (node_colors_changed || junctions_changed)
            this->dirty_streams |= JunctionStream | UndergroundStream;
        if (link_colors_changed)
            this->dirty_streams |= UndergroundStream;
        if (thickness_changed)
        {
            this->dirty_streams |= HighlightStream;
            // With demand point attachments the scene rebuilds the geometry.
            if (this->has_attachments)
                this->dirty_streams = AllStreams;
        }
        if (icons_changed)
            this->dirty_streams |= IconStream;
        if (flow_changed)
            this->dirty_streams |= FlowStream;
    }
    if (this->selection_pending)
    {
        this->selection_pending = false;
        this->network_scene.setSelectedEntity(this->selected_entity_type, this->selected_entity_uuid);
        this->dirty_streams |= HighlightStream | IconStream | JunctionStream | UndergroundStream;
    }
    if (this->errors_pending)
    {
        this->errors_pending = false;
        this->network_scene.setSimulationErrorEntities(this->simulation_error_entities,
                                                       this->simulation_stale_entity_uuids);
        this->dirty_streams |= HighlightStream | JunctionStream | UndergroundStream;
    }
    if (this->icon_models_pending)
    {
        this->icon_models_pending = false;
        if (this->network_scene.setUse3dIconModels(this->use_3d_icon_models))
            this->dirty_streams |= IconStream;
    }
    if (this->underground_pending)
    {
        this->underground_pending = false;
        if (this->network_scene.setUndergroundXRayEnabled(this->underground_mode == MapUndergroundMode::XRay))
            this->dirty_streams |= UndergroundStream | JunctionStream;
    }
}

// --- GPU ------------------------------------------------------------------

bool MapBgfxGlobeNetwork::junctionColor(quint32 render_id, float (&rgba)[4]) const
{
    const QUuid uuid = this->junction_uuids.value(render_id);
    if (!this->applied_symbology.show_junctions || this->hidden_entity_uuids.contains(uuid))
        return false;
    QRgb color = this->applied_symbology.node_colors.value(render_id, networkSymbologyDefaultColor());
    float alpha = float(qAlpha(color)) / 255.0f;
    // Diagnostics before selection (they used to be drawn on top of it).
    if (this->simulation_error_entities.value(uuid, InfrastructureEntity::Unknown)
        == InfrastructureEntity::Junction)
    {
        color = this->simulation_stale_entity_uuids.contains(uuid) ? qRgb(128, 128, 128) : qRgb(255, 0, 0);
        alpha = 1.0f;
    }
    else if (this->selected_entity_type == InfrastructureEntity::Junction && this->selected_entity_uuid == uuid)
    {
        color = qRgb(0, 190, 255);
        alpha = 1.0f;
    }
    if (alpha <= 0.0f)
        return false;
    rgba[0] = float(qRed(color)) / 255.0f;
    rgba[1] = float(qGreen(color)) / 255.0f;
    rgba[2] = float(qBlue(color)) / 255.0f;
    rgba[3] = alpha;
    return true;
}

QVector<MapBgfxGlobeNetwork::JunctionVertex> MapBgfxGlobeNetwork::junctionVertices(
    const QVector<MapGlobeJunctionInstance> &instances) const
{
    QVector<JunctionVertex> vertices;
    vertices.reserve(instances.size() * 6);
    for (const MapGlobeJunctionInstance &instance : instances)
    {
        float rgba[4];
        if (!junctionColor(instance.render_id, rgba))
            continue;
        for (const float (&corner)[2] : kImpostorCorners)
        {
            JunctionVertex vertex;
            vertex.x = instance.center_x;
            vertex.y = instance.center_y;
            vertex.z = instance.center_z;
            vertex.corner_x = corner[0];
            vertex.corner_y = corner[1];
            vertex.red = rgba[0];
            vertex.green = rgba[1];
            vertex.blue = rgba[2];
            vertex.alpha = rgba[3];
            vertex.radius = instance.radius_world;
            vertices.append(vertex);
        }
    }
    return vertices;
}

namespace
{
// Replaces a stream's content; the buffer grows as needed.
template <typename Vertex>
quint64 uploadVertices(bgfx::DynamicVertexBufferHandle *handle, quint32 *count,
                       const bgfx::VertexLayout &layout, const QVector<Vertex> &vertices)
{
    *count = quint32(vertices.size());
    if (vertices.isEmpty())
        return 0;
    if (!bgfx::isValid(*handle))
        *handle = bgfx::createDynamicVertexBuffer(*count, layout, BGFX_BUFFER_ALLOW_RESIZE);
    const uint32_t bytes = uint32_t(vertices.size() * qsizetype(sizeof(Vertex)));
    bgfx::update(*handle, 0, bgfx::copy(vertices.constData(), bytes));
    return bytes;
}
}

void MapBgfxGlobeNetwork::uploadStreams(FrameResult *result)
{
    const MapGlobeNetworkScene &scene = this->network_scene;
    quint64 bytes = 0;
    if (this->dirty_streams & GeometryStream)
    {
        bytes += uploadVertices(&this->link_buffer.handle, &this->link_buffer.count, this->link_layout,
                                scene.linkVertices());
        bytes += uploadVertices(&this->node_buffer.handle, &this->node_buffer.count, this->node_layout,
                                scene.nodeVertices());
    }
    if (this->dirty_streams & HighlightStream)
    {
        bytes += uploadVertices(&this->selected_link_buffer.handle, &this->selected_link_buffer.count,
                                this->link_layout, scene.selectedLinkVertices());
        bytes += uploadVertices(&this->selected_node_buffer.handle, &this->selected_node_buffer.count,
                                this->node_layout, scene.selectedNodeVertices());
        bytes += uploadVertices(&this->diagnostic_link_buffer.handle, &this->diagnostic_link_buffer.count,
                                this->link_layout, scene.diagnosticLinkVertices());
        bytes += uploadVertices(&this->diagnostic_node_buffer.handle, &this->diagnostic_node_buffer.count,
                                this->node_layout, scene.diagnosticNodeVertices());
    }
    if (this->dirty_streams & FlowStream)
    {
        bytes += uploadVertices(&this->flow_buffer.handle, &this->flow_buffer.count, this->link_layout,
                                scene.flowDirectionVertices());
    }
    if (this->dirty_streams & IconStream)
    {
        bytes += uploadVertices(&this->icon_buffer.handle, &this->icon_buffer.count, this->icon_layout,
                                scene.iconVertices());
        // The models are built per instance in their local frames.
        bytes += uploadVertices(&this->tank_buffer.handle, &this->tank_buffer.count, this->model_layout,
                                mapGlobeBuildTankModelVertices(scene.tankInstances()));
        bytes += uploadVertices(&this->reservoir_buffer.handle, &this->reservoir_buffer.count,
                                this->model_layout,
                                mapGlobeBuildReservoirModelVertices(scene.reservoirInstances()));
    }
    if (this->dirty_streams & UndergroundStream)
    {
        bytes += uploadVertices(&this->underground_link_buffer.handle, &this->underground_link_buffer.count,
                                this->link_layout, scene.undergroundLinkVertices());
        bytes += uploadVertices(&this->underground_junction_buffer.handle,
                                &this->underground_junction_buffer.count, this->junction_layout,
                                junctionVertices(scene.undergroundJunctionInstances()));
    }
    if (this->dirty_streams & JunctionStream)
    {
        bytes += uploadVertices(&this->junction_buffer.handle, &this->junction_buffer.count,
                                this->junction_layout, junctionVertices(scene.junctionInstances()));
    }
    this->dirty_streams = 0;
    result->upload_bytes += bytes;
}

void MapBgfxGlobeNetwork::submitStream(bgfx::ViewId view, const Buffer &buffer, bgfx::ProgramHandle program,
                                       uint64_t state, const FrameUniforms &uniforms,
                                       bgfx::TextureHandle texture, bgfx::UniformHandle sampler,
                                       FrameResult *result) const
{
    if (buffer.count == 0 || !bgfx::isValid(buffer.handle))
        return;
    bgfx::setVertexBuffer(0, buffer.handle, 0, buffer.count);
    bgfx::setUniform(this->view_projection_uniform, uniforms.view_projection);
    bgfx::setUniform(this->viewport_uniform, uniforms.viewport);
    bgfx::setUniform(this->params_uniform, uniforms.params);
    bgfx::setUniform(this->camera_right_uniform, uniforms.camera_right);
    bgfx::setUniform(this->camera_up_uniform, uniforms.camera_up);
    bgfx::setUniform(this->camera_eye_uniform, uniforms.camera_eye);
    if (bgfx::isValid(texture))
        bgfx::setTexture(0, sampler, texture);
    bgfx::setState(state);
    bgfx::submit(view, program);
    ++result->draw_calls;
}

MapBgfxGlobeNetwork::FrameResult MapBgfxGlobeNetwork::render(bgfx::ViewId view, const MapGlobeCamera &camera,
                                                             const QSize &logical_viewport,
                                                             bgfx::TextureHandle icon_atlas)
{
    FrameResult result;
    if (!ready() || !logical_viewport.isValid())
        return result;
    synchronize();
    // An empty scene adopts the camera's origin once; networks keep their
    // own geometry anchor (camera rebases never touch the buffers).
    if (this->network_scene.setRenderOriginEcef(camera.renderOriginEcef()))
        this->dirty_streams = AllStreams;
    if (!this->network_scene.hasGeometry())
        return result;
    // Pixel-sized chevrons (and pixel-sized 3D models) in metres at the
    // orbit distance: rebuilt only when that scale changes.
    const double reference_depth_m = std::max(1.0, camera.orbitDistanceM());
    const double tan_half_fov = std::tan(qDegreesToRadians(MapModel::GlobeFieldOfViewDeg / 2.0));
    if (this->network_scene.setFlowDirectionPixelsPerMeter(
            double(logical_viewport.height()) / (2.0 * reference_depth_m * tan_half_fov)))
        this->dirty_streams |= FlowStream | IconStream;
    if (this->dirty_streams != 0)
        uploadStreams(&result);

    const MapGlobeRenderFrame frame = mapGlobeBuildRenderFrame(
        camera, this->network_scene, this->applied_symbology, logical_viewport, logical_viewport);
    // Canonical OpenGL clip space; [0, 1] depth where bgfx has no
    // homogeneous depth: z' = (z + w) / 2. NDC depth -> window depth for the
    // impostors' gl_FragDepth accordingly.
    QMatrix4x4 view_projection = frame.canonical_view_projection;
    const bgfx::Caps *caps = bgfx::getCaps();
    const bool homogeneous_depth = caps == nullptr || caps->homogeneousDepth;
    if (!homogeneous_depth)
    {
        const QMatrix4x4 depth_correction(1.0f, 0.0f, 0.0f, 0.0f,
                                          0.0f, 1.0f, 0.0f, 0.0f,
                                          0.0f, 0.0f, 0.5f, 0.5f,
                                          0.0f, 0.0f, 0.0f, 1.0f);
        view_projection = depth_correction * view_projection;
    }
    FrameUniforms uniforms;
    std::copy_n(view_projection.constData(), 16, uniforms.view_projection);
    const MapGlobeRenderResources &resources = frame.resources;
    uniforms.viewport[0] = float(logical_viewport.width());
    uniforms.viewport[1] = float(logical_viewport.height());
    uniforms.viewport[2] = resources.link_thickness_unit == NetworkSymbologySizeUnit::Meters
        ? -float(resources.link_thickness_m * 0.5) : float(resources.link_thickness_px) * 0.5f;
    uniforms.viewport[3] = resources.node_size_unit == NetworkSymbologySizeUnit::Meters
        ? -float(resources.node_size_m * 0.5) : float(resources.node_size_px) * 0.5f;
    uniforms.params[0] = float(frame.orbit_distance_m);
    uniforms.params[1] = resources.icon_size_unit == NetworkSymbologySizeUnit::Meters
        ? -float(resources.icon_size_m) : float(resources.icon_size_px);
    uniforms.params[2] = homogeneous_depth ? 0.5f : 1.0f;
    uniforms.params[3] = homogeneous_depth ? 0.5f : 0.0f;
    const QVector3D *basis[3] = {&frame.camera_basis.right, &frame.camera_basis.up, &frame.camera_basis.eye};
    float *targets[3] = {uniforms.camera_right, uniforms.camera_up, uniforms.camera_eye};
    for (int index = 0; index < 3; ++index)
    {
        targets[index][0] = basis[index]->x();
        targets[index][1] = basis[index]->y();
        targets[index][2] = basis[index]->z();
        targets[index][3] = 0.0f;
    }

    const bgfx::TextureHandle no_texture = BGFX_INVALID_HANDLE;
    const bgfx::UniformHandle no_sampler = BGFX_INVALID_HANDLE;
    const bool show_junctions = resources.show_junctions;
    // Draw order of the QRhi globe network (MapRhiGlobeNetworkBackend::draw).
    // Solid: the whole network once through the terrain first.
    if (this->underground_mode == MapUndergroundMode::Solid)
    {
        submitStream(view, this->link_buffer, this->link_program, kOverlayState, uniforms, no_texture,
                     no_sampler, &result);
        submitStream(view, this->node_buffer, this->node_program, kOverlayState, uniforms, no_texture,
                     no_sampler, &result);
        if (show_junctions)
            submitStream(view, this->junction_buffer, this->junction_program, kOverlayState, uniforms,
                         no_texture, no_sampler, &result);
        submitStream(view, this->selected_node_buffer, this->node_program, kOverlayState, uniforms, no_texture,
                     no_sampler, &result);
        submitStream(view, this->diagnostic_link_buffer, this->link_program, kOverlayState, uniforms,
                     no_texture, no_sampler, &result);
        submitStream(view, this->diagnostic_node_buffer, this->node_program, kOverlayState, uniforms,
                     no_texture, no_sampler, &result);
    }
    submitStream(view, this->link_buffer, this->link_program, kDepthState, uniforms, no_texture, no_sampler,
                 &result);
    submitStream(view, this->flow_buffer, this->flow_program, kDepthTestState, uniforms, no_texture, no_sampler,
                 &result);
    submitStream(view, this->node_buffer, this->node_program, kDepthState, uniforms, no_texture, no_sampler,
                 &result);
    if (show_junctions)
        submitStream(view, this->junction_buffer, this->junction_program, kDepthState, uniforms, no_texture,
                     no_sampler, &result);
    submitStream(view, this->tank_buffer, this->model_program, kModelState, uniforms, this->tank_texture,
                 this->albedo_sampler, &result);
    submitStream(view, this->reservoir_buffer, this->model_program, kModelState, uniforms,
                 this->reservoir_texture, this->albedo_sampler, &result);
    // Selection is a UI overlay: never depth-tested against its own pipe.
    submitStream(view, this->selected_link_buffer, this->link_program, kOverlayState, uniforms, no_texture,
                 no_sampler, &result);
    submitStream(view, this->selected_node_buffer, this->node_program, kDepthState, uniforms, no_texture,
                 no_sampler, &result);
    submitStream(view, this->diagnostic_link_buffer, this->link_program, kDepthState, uniforms, no_texture,
                 no_sampler, &result);
    submitStream(view, this->diagnostic_node_buffer, this->node_program, kDepthState, uniforms, no_texture,
                 no_sampler, &result);
    // X-Ray: the buried runs through the terrain.
    if (this->underground_mode == MapUndergroundMode::XRay)
    {
        submitStream(view, this->underground_link_buffer, this->link_xray_program, kOverlayState, uniforms,
                     no_texture, no_sampler, &result);
        if (show_junctions)
            submitStream(view, this->underground_junction_buffer, this->junction_xray_program, kOverlayState,
                         uniforms, no_texture, no_sampler, &result);
    }
    if (bgfx::isValid(icon_atlas))
        submitStream(view, this->icon_buffer, this->icon_program, kOverlayState, uniforms, icon_atlas,
                     this->icon_sampler, &result);
    return result;
}
