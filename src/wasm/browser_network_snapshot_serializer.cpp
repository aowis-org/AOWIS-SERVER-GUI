#include "browser_network_snapshot_serializer.h"

#include "../hydraulic_data.h"
#include "../network_render_snapshot.h"

#include <cmath>

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
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

QPair<double, double> nodeRange(const HydraulicData &hydraulic_data, VisualNode visual_node)
{
    switch (visual_node)
    {
    case VisualNode::Elevation:
        return qMakePair(hydraulic_data.nodeElevationMMinimum(), hydraulic_data.nodeElevationMMaximum());
    case VisualNode::BaseDemand:
        return qMakePair(hydraulic_data.nodeBaseDemandM3PerHMinimum(), hydraulic_data.nodeBaseDemandM3PerHMaximum());
    case VisualNode::TotalDemand:
        return qMakePair(hydraulic_data.nodeTotalDemandM3PerHMinimum(), hydraulic_data.nodeTotalDemandM3PerHMaximum());
    case VisualNode::DemandDeficit:
        return qMakePair(hydraulic_data.nodeDemandDeficitM3PerHMinimum(), hydraulic_data.nodeDemandDeficitM3PerHMaximum());
    case VisualNode::EmitterFlow:
        return qMakePair(hydraulic_data.nodeEmitterFlowM3PerHMinimum(), hydraulic_data.nodeEmitterFlowM3PerHMaximum());
    case VisualNode::Leakage:
        return qMakePair(hydraulic_data.nodeLeakageM3PerHMinimum(), hydraulic_data.nodeLeakageM3PerHMaximum());
    case VisualNode::Head:
        return qMakePair(hydraulic_data.nodeHeadMMinimum(), hydraulic_data.nodeHeadMMaximum());
    case VisualNode::Pressure:
        return qMakePair(hydraulic_data.nodePressureMMinimum(), hydraulic_data.nodePressureMMaximum());
    case VisualNode::Chlorine:
        return qMakePair(hydraulic_data.nodeChlorineMgPerLMinimum(), hydraulic_data.nodeChlorineMgPerLMaximum());
    case VisualNode::RiverWater:
        return qMakePair(hydraulic_data.nodeRiverWaterPercentMinimum(), hydraulic_data.nodeRiverWaterPercentMaximum());
    case VisualNode::LakeWater:
        return qMakePair(hydraulic_data.nodeLakeWaterPercentMinimum(), hydraulic_data.nodeLakeWaterPercentMaximum());
    case VisualNode::None:
        break;
    }

    return qMakePair(0.0, 0.0);
}

QPair<double, double> linkRange(const HydraulicData &hydraulic_data, VisualLink visual_link)
{
    switch (visual_link)
    {
    case VisualLink::Diameter:
        return qMakePair(hydraulic_data.linkDiameterMmMinimum(), hydraulic_data.linkDiameterMmMaximum());
    case VisualLink::Length:
        return qMakePair(hydraulic_data.linkLengthMMinimum(), hydraulic_data.linkLengthMMaximum());
    case VisualLink::Roughness:
        return qMakePair(hydraulic_data.linkRoughnessHwMinimum(), hydraulic_data.linkRoughnessHwMaximum());
    case VisualLink::FlowRate:
        return qMakePair(hydraulic_data.linkFlowRateM3PerHMinimum(), hydraulic_data.linkFlowRateM3PerHMaximum());
    case VisualLink::Velocity:
        return qMakePair(hydraulic_data.linkVelocityMPerSMinimum(), hydraulic_data.linkVelocityMPerSMaximum());
    case VisualLink::HeadLoss:
        return qMakePair(hydraulic_data.linkHeadLossMMinimum(), hydraulic_data.linkHeadLossMMaximum());
    case VisualLink::Leakage:
        return qMakePair(hydraulic_data.linkLeakageM3PerHMinimum(), hydraulic_data.linkLeakageM3PerHMaximum());
    case VisualLink::Chlorine:
        return qMakePair(hydraulic_data.linkChlorineMgPerLMinimum(), hydraulic_data.linkChlorineMgPerLMaximum());
    case VisualLink::RiverWater:
        return qMakePair(hydraulic_data.linkRiverWaterPercentMinimum(), hydraulic_data.linkRiverWaterPercentMaximum());
    case VisualLink::LakeWater:
        return qMakePair(hydraulic_data.linkLakeWaterPercentMinimum(), hydraulic_data.linkLakeWaterPercentMaximum());
    case VisualLink::None:
        break;
    }

    return qMakePair(0.0, 0.0);
}

QPair<double, double> heatmapRange(const HydraulicData &hydraulic_data,
                                   VisualHeatmap visual_heatmap)
{
    switch (visual_heatmap)
    {
    case VisualHeatmap::Elevation:
        return qMakePair(hydraulic_data.heatmapElevationMMinimum(),
                         hydraulic_data.heatmapElevationMMaximum());
    case VisualHeatmap::BaseDemand:
        return qMakePair(hydraulic_data.nodeBaseDemandM3PerHMinimum(),
                         hydraulic_data.nodeBaseDemandM3PerHMaximum());
    case VisualHeatmap::TotalDemand:
        return qMakePair(hydraulic_data.heatmapTotalDemandM3PerHMinimum(),
                         hydraulic_data.heatmapTotalDemandM3PerHMaximum());
    case VisualHeatmap::DemandDeficit:
        return qMakePair(hydraulic_data.heatmapDemandDeficitM3PerHMinimum(),
                         hydraulic_data.heatmapDemandDeficitM3PerHMaximum());
    case VisualHeatmap::EmitterFlow:
        return qMakePair(hydraulic_data.nodeEmitterFlowM3PerHMinimum(),
                         hydraulic_data.nodeEmitterFlowM3PerHMaximum());
    case VisualHeatmap::Leakage:
        return qMakePair(hydraulic_data.heatmapLeakageM3PerHMinimum(),
                         hydraulic_data.heatmapLeakageM3PerHMaximum());
    case VisualHeatmap::Head:
        return qMakePair(hydraulic_data.heatmapHeadMMinimum(),
                         hydraulic_data.heatmapHeadMMaximum());
    case VisualHeatmap::Pressure:
        return qMakePair(hydraulic_data.heatmapPressureMMinimum(),
                         hydraulic_data.heatmapPressureMMaximum());
    case VisualHeatmap::Chlorine:
        return qMakePair(hydraulic_data.heatmapChlorineMgPerLMinimum(),
                         hydraulic_data.heatmapChlorineMgPerLMaximum());
    case VisualHeatmap::RiverWater:
        return qMakePair(hydraulic_data.heatmapRiverWaterPercentMinimum(),
                         hydraulic_data.heatmapRiverWaterPercentMaximum());
    case VisualHeatmap::LakeWater:
        return qMakePair(hydraulic_data.heatmapLakeWaterPercentMinimum(),
                         hydraulic_data.heatmapLakeWaterPercentMaximum());
    case VisualHeatmap::None:
        break;
    }

    return qMakePair(0.0, 0.0);
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
            values.insert(junction.uuid, junction.elevation_m);
        for (const HydraulicNodeReservoir &reservoir : network_hydraulic.nodes_reservoirs)
            values.insert(reservoir.uuid, reservoir.head_m);
        for (const HydraulicNodeTank &tank : network_hydraulic.nodes_tanks)
            values.insert(tank.uuid, tank.bottom_elevation_m);
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
    const HydraulicData &hydraulic_data, VisualNode visual_node, VisualLink visual_link,
    VisualHeatmap visual_heatmap, int heatmap_opacity, int heatmap_radius_m)
{
    const NetworkRenderSnapshot &snapshot = hydraulic_data.networkRenderSnapshot();
    const NetworkHydraulic &network_hydraulic = hydraulic_data.networkHydraulic();
    const QPair<double, double> node_range = nodeRange(hydraulic_data, visual_node);
    const QPair<double, double> link_range = linkRange(hydraulic_data, visual_link);
    const QPair<double, double> heatmap_range = heatmapRange(hydraulic_data, visual_heatmap);
    const QHash<QUuid, double> node_values = nodeValues(network_hydraulic, visual_node);
    const QHash<QUuid, double> link_values = linkValues(network_hydraulic, visual_link);
    const QHash<QUuid, double> heatmap_values = heatmapValues(network_hydraulic, visual_heatmap);

    QJsonObject root;
    root.insert(QStringLiteral("nodeVisual"), static_cast<int>(visual_node));
    root.insert(QStringLiteral("nodeMinimum"), node_range.first);
    root.insert(QStringLiteral("nodeMaximum"), node_range.second);
    root.insert(QStringLiteral("nodeValues"), nodeValuesToJson(snapshot, node_values));
    root.insert(QStringLiteral("linkVisual"), static_cast<int>(visual_link));
    root.insert(QStringLiteral("linkMinimum"), link_range.first);
    root.insert(QStringLiteral("linkMaximum"), link_range.second);
    root.insert(QStringLiteral("linkValues"), linkValuesToJson(snapshot, link_values));
    root.insert(QStringLiteral("heatmapVisual"), static_cast<int>(visual_heatmap));
    root.insert(QStringLiteral("heatmapMinimum"), heatmap_range.first);
    root.insert(QStringLiteral("heatmapMaximum"), heatmap_range.second);
    root.insert(QStringLiteral("heatmapValues"), nodeValuesToJson(snapshot, heatmap_values));
    root.insert(QStringLiteral("heatmapOpacity"), qBound(0, heatmap_opacity, 100));
    root.insert(QStringLiteral("heatmapRadiusMeters"), qBound(10, heatmap_radius_m, 500));
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
