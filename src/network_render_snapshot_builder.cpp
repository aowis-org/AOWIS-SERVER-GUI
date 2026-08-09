#include "network_render_snapshot_builder.h"

#include <QHash>

template <typename LinkType>
static bool appendNetworkRenderLink(const LinkType &source, InfrastructureEntity entity_type,
                                    quint32 render_id,
                                    const QList<NetworkRenderNode> &nodes,
                                    const QHash<QUuid, qsizetype> &node_indices,
                                    QList<NetworkRenderLink> &links)
{
    const QHash<QUuid, qsizetype>::const_iterator start_iterator =
        node_indices.constFind(source.node_uuid_from);
    const QHash<QUuid, qsizetype>::const_iterator end_iterator =
        node_indices.constFind(source.node_uuid_to);

    if (start_iterator == node_indices.cend() || end_iterator == node_indices.cend())
        return false;

    const NetworkRenderNode &start_node = nodes.at(start_iterator.value());
    const NetworkRenderNode &end_node = nodes.at(end_iterator.value());

    NetworkRenderLink link;
    link.render_id = render_id;
    link.id = source.id;
    link.uuid = source.uuid;
    link.entity_type = entity_type;
    link.start_node_render_id = start_node.render_id;
    link.end_node_render_id = end_node.render_id;
    link.vertices_wgs84.reserve(source.vertices.size() + 2);
    link.vertices_wgs84.append(start_node.coordinate_wgs84);
    for (const HydraulicLinkVertex &vertex : source.vertices)
        link.vertices_wgs84.append(vertex.coordinate_wgs84);
    link.vertices_wgs84.append(end_node.coordinate_wgs84);
    links.append(link);
    return true;
}

NetworkRenderSnapshot buildNetworkRenderSnapshot(const NetworkHydraulic &network,
                                                 quint64 geometry_revision,
                                                 quint64 visual_revision)
{
    NetworkRenderSnapshot snapshot;
    snapshot.geometry_revision = geometry_revision;
    snapshot.visual_revision = visual_revision;

    const qsizetype node_count = network.nodes_junctions.size() +
                                 network.nodes_reservoirs.size() +
                                 network.nodes_tanks.size();
    const qsizetype link_count = network.links_pipes.size() +
                                 network.links_pumps.size() +
                                 network.links_valves.size();
    snapshot.nodes.reserve(node_count);
    snapshot.links.reserve(link_count);

    QHash<QUuid, qsizetype> node_indices;
    node_indices.reserve(node_count);
    quint32 next_node_render_id = 1;

    const auto add_node = [&snapshot, &node_indices, &next_node_render_id](
        const auto &source, InfrastructureEntity entity_type)
    {
        NetworkRenderNode node;
        node.render_id = next_node_render_id++;
        node.id = source.id;
        node.uuid = source.uuid;
        node.entity_type = entity_type;
        node.coordinate_wgs84 = source.coordinate_wgs84;
        node_indices.insert(node.uuid, snapshot.nodes.size());
        snapshot.nodes.append(node);
    };

    for (const HydraulicNodeJunction &source : network.nodes_junctions)
        add_node(source, InfrastructureEntity::Junction);
    for (const HydraulicNodeReservoir &source : network.nodes_reservoirs)
        add_node(source, InfrastructureEntity::Reservoir);
    for (const HydraulicNodeTank &source : network.nodes_tanks)
        add_node(source, InfrastructureEntity::Tank);

    quint32 next_link_render_id = 1;
    for (const HydraulicLinkPipe &source : network.links_pipes)
    {
        if (appendNetworkRenderLink(source, InfrastructureEntity::Pipe, next_link_render_id,
                                    snapshot.nodes, node_indices, snapshot.links))
            ++next_link_render_id;
    }
    for (const HydraulicLinkPump &source : network.links_pumps)
    {
        if (appendNetworkRenderLink(source, InfrastructureEntity::Pump, next_link_render_id,
                                    snapshot.nodes, node_indices, snapshot.links))
            ++next_link_render_id;
    }
    for (const HydraulicLinkValve &source : network.links_valves)
    {
        if (appendNetworkRenderLink(source, InfrastructureEntity::Valve, next_link_render_id,
                                    snapshot.nodes, node_indices, snapshot.links))
            ++next_link_render_id;
    }

    return snapshot;
}
