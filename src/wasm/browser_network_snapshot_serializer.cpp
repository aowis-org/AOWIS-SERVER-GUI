#include "browser_network_snapshot_serializer.h"

#include "../network_render_snapshot.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
