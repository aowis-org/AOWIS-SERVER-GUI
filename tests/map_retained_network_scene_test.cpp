#include "map/render/map_retained_network_scene.h"

#include <QString>

#include <cstdio>
#include <limits>

namespace
{
bool require(bool condition, const char *message)
{
    if (!condition)
        std::fprintf(stderr, "map_retained_network_scene: %s\n", message);
    return condition;
}

CoordinateWGS84 at(double longitude, double latitude)
{
    CoordinateWGS84 point;
    point.longitude_deg = longitude;
    point.latitude_deg = latitude;
    return point;
}

QUuid uuid(const char *string)
{
    return QUuid(QString::fromLatin1(string));
}
}

int main()
{
    const QUuid node_uuid = uuid("{00000000-0000-0000-0000-000000000001}");
    const QUuid demand_uuid = uuid("{00000000-0000-0000-0000-000000000002}");
    const QUuid link_uuid = uuid("{00000000-0000-0000-0000-000000000003}");

    NetworkRenderSnapshot original;
    // A first-ever revision of zero is valid, including an empty scene.
    original.geometry_revision = 0;
    original.visual_revision = 0;

    NetworkRenderNode node;
    node.uuid = node_uuid;
    node.render_id = 71;
    node.entity_type = InfrastructureEntity::Junction;
    node.coordinate_wgs84 = at(18.19, 11.94);
    original.nodes.append(node);

    NetworkRenderNode demand;
    demand.uuid = demand_uuid;
    demand.render_id = 72;
    demand.entity_type = InfrastructureEntity::DemandPoint;
    demand.coordinate_wgs84 = at(18.1901, 11.9401);
    original.nodes.append(demand);

    NetworkRenderLink pipe;
    pipe.uuid = link_uuid;
    pipe.render_id = 9;
    pipe.entity_type = InfrastructureEntity::Pipe;
    pipe.vertices_wgs84.append(node.coordinate_wgs84);
    pipe.vertices_wgs84.append(at(18.191, 11.94));
    original.links.append(pipe);

    NetworkRenderDemandPointAttachment attachment;
    attachment.demand_point_uuid = demand_uuid;
    attachment.demand_point_render_id = 72;
    attachment.demand_point_coordinate_wgs84 = demand.coordinate_wgs84;
    attachment.attachment_coordinate_wgs84 = at(18.1901, 11.94);
    original.demand_point_attachments.append(attachment);

    MapRetainedNetworkScene scene;
    const MapRetainedNetworkUpdate first = scene.setSnapshot(original);
    if (!require(first.geometry_changed && first.visual_changed,
                 "first revision-zero snapshot was skipped")
        || !require(scene.hasSnapshot(), "snapshot not retained")
        || !require(scene.nodesByUuid().size() == 2 && scene.linksByUuid().size() == 1,
                    "stable node/link UUID index incomplete")
        || !require(scene.nodesByUuid().value(demand_uuid).render_id == 72,
                    "demand-point render identifier missing")
        || !require(scene.attachmentsByDemandPointUuid().value(demand_uuid, -1) == 0,
                    "demand-point attachment missing")
        || !require(scene.chunks().size() == 1,
                    "co-located nodes, links and attachments split into multiple anchors"))
    {
        return 1;
    }

    const MapRetainedNetworkUpdate unchanged = scene.setSnapshot(original);
    if (!require(!unchanged.geometry_changed && !unchanged.visual_changed,
                 "identical snapshot caused invalidation"))
        return 1;

    NetworkRenderSnapshot visual = original;
    visual.visual_revision = 5;
    const MapRetainedNetworkUpdate visual_update = scene.setSnapshot(visual);
    if (!require(!visual_update.geometry_changed && visual_update.visual_changed,
                 "visual-only update invalidated geometry")
        || !require(scene.snapshot().visual_revision == 5,
                    "visual revision not retained")
        || !require(scene.snapshot().nodes.size() == original.nodes.size(),
                    "visual-only update lost nodes")
        || !require(scene.attachmentsByDemandPointUuid().contains(demand_uuid),
                    "visual-only update lost attachment index"))
    {
        return 1;
    }

    // A new geometry revision may renumber numeric render IDs; UUIDs are
    // the persistent entity identity that must survive that renumbering.
    NetworkRenderSnapshot changed = visual;
    changed.geometry_revision = 1;
    changed.nodes[1].render_id = 172;
    changed.demand_point_attachments[0].demand_point_render_id = 172;
    changed.nodes[1].coordinate_wgs84 = at(18.45, 12.20);
    changed.demand_point_attachments[0].demand_point_coordinate_wgs84 =
        changed.nodes[1].coordinate_wgs84;
    const MapRetainedNetworkUpdate geometry_update = scene.setSnapshot(changed);
    if (!require(geometry_update.geometry_changed,
                 "geometry revision did not invalidate retained scene")
        || !require(scene.nodesByUuid().value(demand_uuid).render_id == 172,
                    "UUID lookup did not remap render ID")
        || !require(scene.chunks().size() == 2,
                    "spatial grouping not refreshed for moved demand point"))
    {
        return 1;
    }

    quint64 east = 0;
    quint64 west = 0;
    quint64 invalid = 0;
    if (!require(MapRetainedNetworkScene::chunkKey(at(180.0, 11.94), &east)
                     && MapRetainedNetworkScene::chunkKey(at(-180.0, 11.94), &west)
                     && east == west, "antimeridian cells do not normalize")
        || !require(!MapRetainedNetworkScene::chunkKey(
                         at(std::numeric_limits<double>::quiet_NaN(), 0.0), &invalid),
                    "non-finite coordinates accepted")
        || !require(!MapRetainedNetworkScene::chunkKey(at(0.0, 91.0), &invalid),
                    "out-of-range latitudes accepted"))
    {
        return 1;
    }

    MapRetainedNetworkScene empty;
    const MapRetainedNetworkUpdate empty_first = empty.setSnapshot(NetworkRenderSnapshot{});
    if (!require(empty_first.geometry_changed && empty.hasSnapshot()
                     && empty.chunks().isEmpty(),
                 "initial empty snapshot was not registered"))
    {
        return 1;
    }

    std::puts("Retained network identity, revision and anchor indexing passed");
    return 0;
}
