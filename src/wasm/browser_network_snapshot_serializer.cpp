#include "browser_network_snapshot_serializer.h"

#include "../hydraulic_data.h"
#include "../network_render_snapshot.h"
#include "../network_symbology.h"
#include "../network_symbology_values.h"

#include <cmath>

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

namespace
{
QJsonArray coordinateToJson(const CoordinateWGS84 &coordinate)
{
    QJsonArray result;
    result.append(coordinate.longitude_deg);
    result.append(coordinate.latitude_deg);
    return result;
}

QJsonArray nodeToJson(const NetworkRenderNode &node)
{
    QJsonArray result;
    result.append(static_cast<double>(node.render_id));
    result.append(static_cast<int>(node.entity_type));
    result.append(node.uuid.toString(QUuid::WithoutBraces));
    result.append(node.id);
    result.append(node.coordinate_wgs84.longitude_deg);
    result.append(node.coordinate_wgs84.latitude_deg);
    return result;
}

QJsonArray linkToJson(const NetworkRenderLink &link)
{
    QJsonArray vertices;
    for (const CoordinateWGS84 &coordinate : link.vertices_wgs84)
        vertices.append(coordinateToJson(coordinate));

    QJsonArray result;
    result.append(static_cast<double>(link.render_id));
    result.append(static_cast<int>(link.entity_type));
    result.append(link.uuid.toString(QUuid::WithoutBraces));
    result.append(link.id);
    result.append(static_cast<double>(link.start_node_render_id));
    result.append(static_cast<double>(link.end_node_render_id));
    result.append(vertices);
    return result;
}

QByteArray serializeRoot(const NetworkRenderSnapshot &snapshot,
                         const QJsonArray &nodes, const QJsonArray &links)
{
    QJsonObject root;
    root.insert(QStringLiteral("geometryRevision"), QString::number(snapshot.geometry_revision));
    root.insert(QStringLiteral("visualRevision"), QString::number(snapshot.visual_revision));
    root.insert(QStringLiteral("nodes"), nodes);
    root.insert(QStringLiteral("links"), links);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QHash<QUuid, double> nodeValues(const NetworkHydraulic &network_hydraulic, VisualNode visual_node)
{
    QHash<QUuid, double> values;

    switch (visual_node)
    {
    case VisualNode::Elevation:
        values.reserve(network_hydraulic.nodes_junctions.size() +
                       network_hydraulic.nodes_reservoirs.size() +
                       network_hydraulic.nodes_tanks.size());
        for (const HydraulicNodeJunction &junction : network_hydraulic.nodes_junctions)
            values.insert(junction.uuid, resolvedSymbologyElevationM(junction));
        for (const HydraulicNodeReservoir &reservoir : network_hydraulic.nodes_reservoirs)
            values.insert(reservoir.uuid, resolvedSymbologyElevationM(reservoir));
        for (const HydraulicNodeTank &tank : network_hydraulic.nodes_tanks)
            values.insert(tank.uuid, resolvedSymbologyElevationM(tank));
        break;
    case VisualNode::BaseDemand:
        values.reserve(network_hydraulic.nodes_junctions.size());
        for (const HydraulicNodeJunction &junction : network_hydraulic.nodes_junctions)
        {
            double base_demand_m3_per_h = 0.0;
            for (const HydraulicNodeJunctionDemand &demand : junction.demands)
                base_demand_m3_per_h += demand.base_demand_m3_per_h;
            values.insert(junction.uuid, base_demand_m3_per_h);
        }
        break;
    case VisualNode::None:
    case VisualNode::TotalDemand:
    case VisualNode::DemandDeficit:
    case VisualNode::EmitterFlow:
    case VisualNode::Leakage:
    case VisualNode::Head:
    case VisualNode::Pressure:
    case VisualNode::Chlorine:
    case VisualNode::RiverWater:
    case VisualNode::LakeWater:
        break;
    }

    return values;
}

QHash<QUuid, double> linkValues(const NetworkHydraulic &network_hydraulic, VisualLink visual_link)
{
    QHash<QUuid, double> values;

    switch (visual_link)
    {
    case VisualLink::Diameter:
        values.reserve(network_hydraulic.links_pipes.size());
        for (const HydraulicLinkPipe &pipe : network_hydraulic.links_pipes)
            values.insert(pipe.uuid, pipe.diameter_mm);
        break;
    case VisualLink::Length:
        values.reserve(network_hydraulic.links_pipes.size());
        for (const HydraulicLinkPipe &pipe : network_hydraulic.links_pipes)
            values.insert(pipe.uuid, pipe.length_measured_m.value_or(pipe.length_calculated_m));
        break;
    case VisualLink::Roughness:
        values.reserve(network_hydraulic.links_pipes.size());
        for (const HydraulicLinkPipe &pipe : network_hydraulic.links_pipes)
            values.insert(pipe.uuid, pipe.roughness_hw);
        break;
    case VisualLink::None:
    case VisualLink::FlowRate:
    case VisualLink::Velocity:
    case VisualLink::HeadLoss:
    case VisualLink::Leakage:
    case VisualLink::Chlorine:
    case VisualLink::RiverWater:
    case VisualLink::LakeWater:
        break;
    }

    return values;
}

QHash<QUuid, double> heatmapValues(const NetworkHydraulic &network_hydraulic,
                                   VisualHeatmap visual_heatmap)
{
    switch (visual_heatmap)
    {
    case VisualHeatmap::Elevation:
        return nodeValues(network_hydraulic, VisualNode::Elevation);
    case VisualHeatmap::BaseDemand:
        return nodeValues(network_hydraulic, VisualNode::BaseDemand);
    case VisualHeatmap::TotalDemand:
        return nodeValues(network_hydraulic, VisualNode::TotalDemand);
    case VisualHeatmap::DemandDeficit:
        return nodeValues(network_hydraulic, VisualNode::DemandDeficit);
    case VisualHeatmap::EmitterFlow:
        return nodeValues(network_hydraulic, VisualNode::EmitterFlow);
    case VisualHeatmap::Leakage:
        return nodeValues(network_hydraulic, VisualNode::Leakage);
    case VisualHeatmap::Head:
        return nodeValues(network_hydraulic, VisualNode::Head);
    case VisualHeatmap::Pressure:
        return nodeValues(network_hydraulic, VisualNode::Pressure);
    case VisualHeatmap::Chlorine:
        return nodeValues(network_hydraulic, VisualNode::Chlorine);
    case VisualHeatmap::RiverWater:
        return nodeValues(network_hydraulic, VisualNode::RiverWater);
    case VisualHeatmap::LakeWater:
        return nodeValues(network_hydraulic, VisualNode::LakeWater);
    case VisualHeatmap::None:
        break;
    }

    return QHash<QUuid, double>();
}

QJsonArray nodeValuesToJson(const NetworkRenderSnapshot &snapshot,
                            const QHash<QUuid, double> &values)
{
    QJsonArray result;
    for (const NetworkRenderNode &node : snapshot.nodes)
    {
        const QHash<QUuid, double>::const_iterator iterator = values.constFind(node.uuid);
        if (iterator == values.cend() || !std::isfinite(iterator.value()))
            continue;

        QJsonArray entry;
        entry.append(static_cast<double>(node.render_id));
        entry.append(iterator.value());
        result.append(entry);
    }
    return result;
}

QJsonArray linkValuesToJson(const NetworkRenderSnapshot &snapshot,
                            const QHash<QUuid, double> &values)
{
    QJsonArray result;
    for (const NetworkRenderLink &link : snapshot.links)
    {
        const QHash<QUuid, double>::const_iterator iterator = values.constFind(link.uuid);
        if (iterator == values.cend() || !std::isfinite(iterator.value()))
            continue;

        QJsonArray entry;
        entry.append(static_cast<double>(link.render_id));
        entry.append(iterator.value());
        result.append(entry);
    }
    return result;
}
}

QByteArray BrowserNetworkSnapshotSerializer::serialize(const NetworkRenderSnapshot &snapshot)
{
    QJsonArray nodes;
    for (const NetworkRenderNode &node : snapshot.nodes)
        nodes.append(nodeToJson(node));

    QJsonArray links;
    for (const NetworkRenderLink &link : snapshot.links)
        links.append(linkToJson(link));

    return serializeRoot(snapshot, nodes, links);
}

QByteArray BrowserNetworkSnapshotSerializer::serializeGeometryPatch(
    const NetworkRenderSnapshot &snapshot, const QSet<QUuid> &node_uuids,
    const QSet<QUuid> &link_uuids)
{
    QJsonArray nodes;
    QSet<quint32> dirty_node_render_ids;
    for (const NetworkRenderNode &node : snapshot.nodes)
    {
        if (!node_uuids.contains(node.uuid))
            continue;

        nodes.append(nodeToJson(node));
        dirty_node_render_ids.insert(node.render_id);
    }

    QJsonArray links;
    for (const NetworkRenderLink &link : snapshot.links)
    {
        const bool connected_to_dirty_node =
            dirty_node_render_ids.contains(link.start_node_render_id) ||
            dirty_node_render_ids.contains(link.end_node_render_id);
        if (link_uuids.contains(link.uuid) || connected_to_dirty_node)
            links.append(linkToJson(link));
    }

    return serializeRoot(snapshot, nodes, links);
}

QByteArray BrowserNetworkSnapshotSerializer::serializeSymbology(
    const HydraulicData &hydraulic_data, const NetworkSymbologySettings &settings,
    const NetworkSymbologyRanges &ranges)
{
    const NetworkSymbologySettings bounded_settings = settings.bounded();
    const NetworkRenderSnapshot &snapshot = hydraulic_data.networkRenderSnapshot();
    const NetworkHydraulic &network_hydraulic = hydraulic_data.networkHydraulic();
    const QHash<QUuid, double> node_values =
        nodeValues(network_hydraulic, bounded_settings.visual_node);
    const QHash<QUuid, double> link_values =
        linkValues(network_hydraulic, bounded_settings.visual_link);
    const QHash<QUuid, double> heatmap_values =
        heatmapValues(network_hydraulic, bounded_settings.visual_heatmap);

    QJsonObject root;
    root.insert(QStringLiteral("nodeVisual"), static_cast<int>(bounded_settings.visual_node));
    root.insert(QStringLiteral("nodeSizePercent"), bounded_settings.node_size_percent);
    root.insert(QStringLiteral("iconSizePercent"), bounded_settings.icon_size_percent);
    root.insert(QStringLiteral("nodeMinimum"), ranges.node_minimum);
    root.insert(QStringLiteral("nodeMaximum"), ranges.node_maximum);
    root.insert(QStringLiteral("nodeValues"), nodeValuesToJson(snapshot, node_values));
    root.insert(QStringLiteral("linkVisual"), static_cast<int>(bounded_settings.visual_link));
    root.insert(QStringLiteral("linkThicknessPixels"), bounded_settings.link_thickness_px);
    root.insert(QStringLiteral("linkMinimum"), ranges.link_minimum);
    root.insert(QStringLiteral("linkMaximum"), ranges.link_maximum);
    root.insert(QStringLiteral("linkValues"), linkValuesToJson(snapshot, link_values));
    root.insert(QStringLiteral("heatmapVisual"), static_cast<int>(bounded_settings.visual_heatmap));
    root.insert(QStringLiteral("heatmapMinimum"), ranges.heatmap_minimum);
    root.insert(QStringLiteral("heatmapMaximum"), ranges.heatmap_maximum);
    root.insert(QStringLiteral("heatmapValues"), nodeValuesToJson(snapshot, heatmap_values));
    root.insert(QStringLiteral("heatmapOpacity"), bounded_settings.heatmap_opacity);
    root.insert(QStringLiteral("heatmapRadiusMeters"), bounded_settings.heatmap_radius_m);
    root.insert(QStringLiteral("heatmapSolidCenterPercent"),
                bounded_settings.heatmap_solid_center_percent);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
