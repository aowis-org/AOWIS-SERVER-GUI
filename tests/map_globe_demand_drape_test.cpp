// Demand points on the globe lie on the drawn terrain
// (MapGlobeNetworkScene::setSurfaceHeightResolver()): they have no elevation
// in the model. Checks the placement on a bare ellipsoid (no relief drawn
// yet) and on relief, the in-place move when the drawn terrain changes (no
// network rebuild), and that the attachment line ends on the junction / pipe
// at the network's own elevation.
#include "map/render/map_globe_network_scene.h"
#include "map/render/map_globe_vertical_transform.h"
#include "map/render/map_render_diagnostics.h"
#include "geo/geo_wgs84_ellipsoid.h"

#include <QGuiApplication>
#include <QString>
#include <QVector3D>

#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>

namespace
{
constexpr double Lift = MapGlobeVerticalTransform::MinimumNetworkAntiZFightingLiftM;
// Float32 vertices relative to the network origin, a few hundred metres away.
constexpr double ToleranceM = 0.02;

bool require(bool condition, const char *message)
{
    if (!condition)
        std::fprintf(stderr, "map_globe_demand_drape: %s\n", message);
    return condition;
}

CoordinateWGS84 coordinate(double longitude, double latitude)
{
    CoordinateWGS84 result;
    result.longitude_deg = longitude;
    result.latitude_deg = latitude;
    return result;
}

NetworkRenderNode makeNode(quint32 render_id, InfrastructureEntity entity_type, const CoordinateWGS84 &position,
                           double elevation_m)
{
    NetworkRenderNode node;
    node.render_id = render_id;
    node.id = QStringLiteral("N%1").arg(render_id);
    node.uuid = QUuid::createUuid();
    node.entity_type = entity_type;
    node.coordinate_wgs84 = position;
    node.elevation_m = elevation_m;
    return node;
}

// Geodetic height of a scene-relative position.
double heightOf(const MapGlobeNetworkScene &scene, float x, float y, float z)
{
    const GeoWgs84Ellipsoid::EcefPositionD origin = scene.renderOriginEcef();
    GeoWgs84Ellipsoid::EcefPositionD position;
    position.x = origin.x + double(x);
    position.y = origin.y + double(y);
    position.z = origin.z + double(z);
    double longitude = 0.0;
    double latitude = 0.0;
    double height = 0.0;
    GeoWgs84Ellipsoid::ecefToGeodetic(position, &longitude, &latitude, &height);
    return height;
}

// Height of the (first) node quad of a demand point; NaN when it has none.
double demandHeight(const MapGlobeNetworkScene &scene, quint32 render_id)
{
    for (const MapNetworkNodeVertex &vertex : scene.nodeVertices())
    {
        if (vertex.entity_type == InfrastructureEntity::DemandPoint && vertex.render_id == render_id)
            return heightOf(scene, vertex.center_x, vertex.center_y, vertex.center_z);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// The attachment line of a demand point: start and end heights.
bool attachmentHeights(const MapGlobeNetworkScene &scene, quint32 render_id, double *start, double *end)
{
    for (const MapNetworkLinkVertex &vertex : scene.linkVertices())
    {
        if (vertex.entity_type == InfrastructureEntity::DemandPoint && vertex.render_id == render_id)
        {
            *start = heightOf(scene, vertex.start_x, vertex.start_y, vertex.start_z);
            *end = heightOf(scene, vertex.end_x, vertex.end_y, vertex.end_z);
            return true;
        }
    }
    return false;
}

// Every icon / highlight vertex of a demand point at height.
bool copiesAt(const MapGlobeNetworkScene &scene, quint32 render_id, double height)
{
    int icons = 0;
    for (const MapNetworkIconVertex &vertex : scene.iconVertices())
    {
        if (vertex.entity_type != InfrastructureEntity::DemandPoint || vertex.render_id != render_id)
            continue;
        ++icons;
        if (std::abs(heightOf(scene, vertex.center_x, vertex.center_y, vertex.center_z) - height) > ToleranceM)
            return false;
    }
    int highlights = 0;
    for (const MapNetworkNodeVertex &vertex : scene.selectedNodeVertices())
    {
        if (vertex.entity_type != InfrastructureEntity::DemandPoint || vertex.render_id != render_id)
            continue;
        ++highlights;
        if (std::abs(heightOf(scene, vertex.center_x, vertex.center_y, vertex.center_z) - height) > ToleranceM)
            return false;
    }
    return icons == 6 && highlights == 6;
}

quint64 rebuilds()
{
    return MapRenderDiagnostics::value(MapRenderDiagnostics::Counter::GlobeGeometryRebuilt);
}
}

int main(int argc, char **argv)
{
    qputenv("AOWIS_MAP_RENDER_DIAGNOSTICS", "1");
    QGuiApplication application(argc, argv);

    // A pipe between two junctions (300 m, 320 m), demand point 3 attached to
    // junction 1, demand point 4 to the pipe at a quarter of its length.
    NetworkRenderSnapshot snapshot;
    snapshot.geometry_revision = 1;
    const NetworkRenderNode junction_a =
        makeNode(1, InfrastructureEntity::Junction, coordinate(10.000, 50.000), 300.0);
    const NetworkRenderNode junction_b =
        makeNode(2, InfrastructureEntity::Junction, coordinate(10.010, 50.000), 320.0);
    const double no_elevation = std::numeric_limits<double>::quiet_NaN();
    const NetworkRenderNode demand_junction =
        makeNode(3, InfrastructureEntity::DemandPoint, coordinate(10.000, 50.001), no_elevation);
    const NetworkRenderNode demand_pipe =
        makeNode(4, InfrastructureEntity::DemandPoint, coordinate(10.0025, 49.999), no_elevation);
    snapshot.nodes = {junction_a, junction_b, demand_junction, demand_pipe};
    NetworkRenderLink pipe;
    pipe.render_id = 1;
    pipe.uuid = QUuid::createUuid();
    pipe.entity_type = InfrastructureEntity::Pipe;
    pipe.start_node_render_id = 1;
    pipe.end_node_render_id = 2;
    pipe.vertices_wgs84 = {junction_a.coordinate_wgs84, junction_b.coordinate_wgs84};
    pipe.elevations_m = {300.0, 320.0};
    snapshot.links = {pipe};
    NetworkRenderDemandPointAttachment to_junction;
    to_junction.demand_point_render_id = 3;
    to_junction.demand_point_uuid = demand_junction.uuid;
    to_junction.demand_point_coordinate_wgs84 = demand_junction.coordinate_wgs84;
    to_junction.attachment_coordinate_wgs84 = junction_a.coordinate_wgs84;
    to_junction.attachment_elevation_m = 300.0;
    NetworkRenderDemandPointAttachment to_pipe;
    to_pipe.demand_point_render_id = 4;
    to_pipe.demand_point_uuid = demand_pipe.uuid;
    to_pipe.demand_point_coordinate_wgs84 = demand_pipe.coordinate_wgs84;
    to_pipe.attachment_coordinate_wgs84 = coordinate(10.0025, 50.000);
    to_pipe.attachment_elevation_m = 305.0;
    snapshot.demand_point_attachments = {to_junction, to_pipe};

    // The drawn terrain: none yet (bare ellipsoid), later a slope rising
    // 1 m per 0.001 deg longitude from 250 m.
    bool relief_drawn = false;
    double relief_base_m = 250.0;
    const std::function<double(const CoordinateWGS84 &)> relief = [&relief_base_m](const CoordinateWGS84 &position)
    {
        return relief_base_m + 1000.0 * (position.longitude_deg - 10.0);
    };

    MapGlobeNetworkScene scene;
    scene.setSurfaceHeightResolver([&relief_drawn, &relief](const CoordinateWGS84 &position, double *height_m)
    {
        if (!relief_drawn)
            return false;
        *height_m = relief(position);
        return true;
    });
    MapNetworkRenderSymbology symbology;
    symbology.show_icons = true;
    scene.setSymbology(symbology);
    scene.setNetworkSnapshot(snapshot);
    scene.setSelectedEntity(InfrastructureEntity::DemandPoint, demand_pipe.uuid);

    // Before any relief is drawn: on the ellipsoid, lifted like the network.
    double start = 0.0;
    double end = 0.0;
    if (!require(std::abs(demandHeight(scene, 3) - Lift) < ToleranceM, "bare ellipsoid: junction demand point")
        || !require(std::abs(demandHeight(scene, 4) - Lift) < ToleranceM, "bare ellipsoid: pipe demand point")
        || !require(attachmentHeights(scene, 3, &start, &end), "junction attachment line missing")
        || !require(std::abs(start - Lift) < ToleranceM, "bare ellipsoid: attachment start")
        || !require(std::abs(end - (300.0 + Lift)) < ToleranceM, "attachment ends at the junction's elevation")
        || !require(attachmentHeights(scene, 4, &start, &end), "pipe attachment line missing")
        || !require(std::abs(end - (305.0 + Lift)) < ToleranceM, "attachment ends at the pipe's elevation")
        || !require(copiesAt(scene, 4, Lift), "bare ellipsoid: icon / selection of the demand point"))
    {
        return 1;
    }

    // Relief drawn: moved in place onto it, without a network rebuild.
    const quint64 rebuilds_before = rebuilds();
    const MapNetworkNodeVertex *const node_storage = scene.nodeVertices().constData();
    relief_drawn = true;
    const double expected_3 = relief(demand_junction.coordinate_wgs84) + Lift;
    const double expected_4 = relief(demand_pipe.coordinate_wgs84) + Lift;
    if (!require(scene.refreshDemandPointHeights(), "relief: nothing moved")
        || !require(rebuilds() == rebuilds_before, "relief: the network was rebuilt")
        || !require(scene.nodeVertices().constData() == node_storage, "relief: node vertices reallocated")
        || !require(std::abs(demandHeight(scene, 3) - expected_3) < ToleranceM, "relief: junction demand point")
        || !require(std::abs(demandHeight(scene, 4) - expected_4) < ToleranceM, "relief: pipe demand point")
        || !require(attachmentHeights(scene, 4, &start, &end), "pipe attachment line lost")
        || !require(std::abs(start - expected_4) < ToleranceM, "relief: attachment start")
        || !require(std::abs(end - (305.0 + Lift)) < ToleranceM, "relief: attachment end moved")
        || !require(copiesAt(scene, 4, expected_4), "relief: icon / selection of the demand point")
        || !require(!scene.refreshDemandPointHeights(), "unchanged terrain moved the demand points"))
    {
        return 1;
    }

    // Other LOD (e.g. finer mesh): follows again; a rebuild samples the
    // current surface itself.
    relief_base_m = 262.5;
    if (!require(scene.refreshDemandPointHeights(), "terrain change: nothing moved")
        || !require(std::abs(demandHeight(scene, 3) - (relief(demand_junction.coordinate_wgs84) + Lift))
                        < ToleranceM,
                    "terrain change: junction demand point"))
    {
        return 1;
    }
    relief_base_m = 240.0;
    scene.setGroundOffsetM(5.0);
    if (!require(std::abs(demandHeight(scene, 3) - (relief(demand_junction.coordinate_wgs84) + 5.0)) < ToleranceM,
                 "rebuild: demand point not on the current surface with the ground offset")
        || !require(!scene.refreshDemandPointHeights(), "rebuild left the demand points stale"))
    {
        return 1;
    }

    std::printf("globe demand drape: ellipsoid, relief, in-place move, attachments, rebuild\n");
    return 0;
}
