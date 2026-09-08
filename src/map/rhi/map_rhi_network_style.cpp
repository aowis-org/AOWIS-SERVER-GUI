#include "map/rhi/map_rhi_network_style.h"

#include "network/network_symbology_rendering.h"

#include <QtGlobal>

#include <limits>

namespace
{
constexpr quint64 StyleTexelsPerEntity = 2;
constexpr quint64 StyleTextureRowTexels = 2048;

constexpr int DiagnosticNone = 0;
constexpr int DiagnosticStale = 1;
constexpr int DiagnosticError = 2;

quint8 diagnosticByte(int diagnostic_state)
{
    if (diagnostic_state == DiagnosticError)
        return 255;
    if (diagnostic_state == DiagnosticStale)
        return 128;
    return 0;
}

quint8 flowDirectionByte(int flow_direction)
{
    if (flow_direction < 0)
        return 0;
    if (flow_direction > 0)
        return 255;
    return 128;
}
}

MapRhiNetworkStyleTable::MapRhiNetworkStyleTable()
    : image_data(2, 1, QImage::Format_RGBA8888)
{
    this->image_data.fill(Qt::transparent);
}

void MapRhiNetworkStyleTable::rebuild(
    const NetworkRenderSnapshot &snapshot,
    const QSet<QUuid> &hidden_entity_uuids,
    const MapRhiSymbology &symbology,
    InfrastructureEntity selected_entity_type,
    const QUuid &selected_entity_uuid,
    const QHash<QUuid, InfrastructureEntity> &simulation_error_entities,
    const QSet<QUuid> &simulation_stale_entity_uuids)
{
    quint32 maximum_node_render_id = 0;
    for (const NetworkRenderNode &node : snapshot.nodes)
        maximum_node_render_id = qMax(maximum_node_render_id, node.render_id);

    quint32 maximum_link_render_id = 0;
    for (const NetworkRenderLink &link : snapshot.links)
        maximum_link_render_id = qMax(maximum_link_render_id, link.render_id);

    this->link_style_base = maximum_node_render_id;
    const quint64 maximum_link_style_index =
        quint64(this->link_style_base) + quint64(maximum_link_render_id);
    const quint64 maximum_style_index = qMax(
        quint64(maximum_node_render_id), maximum_link_style_index);
    const quint64 style_count = qMax<quint64>(1, maximum_style_index + 1);
    const quint64 texel_count = style_count * StyleTexelsPerEntity;
    const quint64 texture_width = qMin(StyleTextureRowTexels, texel_count);
    const quint64 texture_height =
        (texel_count + texture_width - 1) / texture_width;
    if (texture_height > quint64(std::numeric_limits<int>::max()))
    {
        this->image_data = QImage();
        return;
    }

    this->image_data = QImage(
        int(texture_width), int(texture_height), QImage::Format_RGBA8888);
    if (this->image_data.isNull())
        return;
    this->image_data.fill(Qt::transparent);

    for (const NetworkRenderNode &node : snapshot.nodes)
    {
        const bool selected = node.entity_type == selected_entity_type
            && node.uuid == selected_entity_uuid;
        int diagnostic_state = DiagnosticNone;
        if (simulation_error_entities.value(
                node.uuid, InfrastructureEntity::Unknown) == node.entity_type)
        {
            diagnostic_state = simulation_stale_entity_uuids.contains(node.uuid)
                ? DiagnosticStale : DiagnosticError;
        }
        const bool visible = !hidden_entity_uuids.contains(node.uuid)
            && (node.entity_type != InfrastructureEntity::Junction
                || symbology.show_junctions);
        const QRgb color = symbology.node_colors.value(
            node.render_id, networkSymbologyDefaultColor());
        writeStyle(
            nodeStyleIndex(node.render_id), color, selected,
            diagnostic_state, visible, 0);
    }

    for (const NetworkRenderLink &link : snapshot.links)
    {
        const bool selected = link.entity_type == selected_entity_type
            && link.uuid == selected_entity_uuid;
        int diagnostic_state = DiagnosticNone;
        if (simulation_error_entities.value(
                link.uuid, InfrastructureEntity::Unknown) == link.entity_type)
        {
            diagnostic_state = simulation_stale_entity_uuids.contains(link.uuid)
                ? DiagnosticStale : DiagnosticError;
        }
        const bool visible = !hidden_entity_uuids.contains(link.uuid);
        const QRgb color = symbology.link_colors.value(
            link.render_id, networkSymbologyDefaultColor());
        const int flow_direction = qBound<int>(
            -1, int(symbology.flow_directions.value(link.render_id, 0)), 1);
        writeStyle(
            linkStyleIndex(link.render_id), color, selected,
            diagnostic_state, visible, flow_direction);
    }
}

const QImage &MapRhiNetworkStyleTable::image() const
{
    return this->image_data;
}

bool MapRhiNetworkStyleTable::isValid() const
{
    return !this->image_data.isNull();
}

bool MapRhiNetworkStyleTable::isDrawable(quint32 style_index) const
{
    const quint64 first_texel = quint64(style_index) * StyleTexelsPerEntity;
    const uchar *color = texel(first_texel);
    const uchar *state = texel(first_texel + 1);
    if (color == nullptr || state == nullptr)
        return false;

    const bool selected = state[0] != 0;
    const bool visible = state[2] != 0;
    return visible && (selected || color[3] != 0);
}

quint32 MapRhiNetworkStyleTable::nodeStyleIndex(quint32 render_id)
{
    return render_id;
}

quint32 MapRhiNetworkStyleTable::linkStyleIndex(quint32 render_id) const
{
    if (render_id > std::numeric_limits<quint32>::max() - this->link_style_base)
        return 0;
    return this->link_style_base + render_id;
}

quint32 MapRhiNetworkStyleTable::linkStyleBase() const
{
    return this->link_style_base;
}

void MapRhiNetworkStyleTable::writeStyle(
    quint32 style_index, QRgb color, bool selected,
    int diagnostic_state, bool visible, int flow_direction)
{
    const quint64 first_texel = quint64(style_index) * StyleTexelsPerEntity;
    writeTexel(
        first_texel,
        quint8(qRed(color)), quint8(qGreen(color)),
        quint8(qBlue(color)), quint8(qAlpha(color)));
    writeTexel(
        first_texel + 1,
        selected ? 255 : 0,
        diagnosticByte(diagnostic_state),
        visible ? 255 : 0,
        flowDirectionByte(flow_direction));
}

void MapRhiNetworkStyleTable::writeTexel(
    quint64 texel_index, quint8 red, quint8 green,
    quint8 blue, quint8 alpha)
{
    if (this->image_data.isNull())
        return;

    const quint64 texture_width = quint64(this->image_data.width());
    const quint64 row = texel_index / texture_width;
    const quint64 column = texel_index % texture_width;
    if (row >= quint64(this->image_data.height()))
        return;

    uchar *pixel = this->image_data.scanLine(int(row)) + int(column) * 4;
    pixel[0] = red;
    pixel[1] = green;
    pixel[2] = blue;
    pixel[3] = alpha;
}

const uchar *MapRhiNetworkStyleTable::texel(quint64 texel_index) const
{
    if (this->image_data.isNull())
        return nullptr;

    const quint64 texture_width = quint64(this->image_data.width());
    const quint64 row = texel_index / texture_width;
    const quint64 column = texel_index % texture_width;
    if (row >= quint64(this->image_data.height()))
        return nullptr;

    return this->image_data.constScanLine(int(row)) + int(column) * 4;
}
