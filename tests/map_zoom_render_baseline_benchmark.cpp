#include "map/render/map_render_diagnostics.h"
#include "map/rhi/map_rhi_scene.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstdio>

// This is a baseline, not a bgfx acceptance test. It deliberately exercises
// the current retained 2D preparation path and records its zoom-dependent
// rebuilds. Step 2+ will change those numbers. Machine timings are reported
// but NEVER used as CTest thresholds.
namespace
{
constexpr int GridSide = 100;
constexpr int DemandPointCount = GridSide * GridSide;
constexpr int ZoomTransitions = 80;

QUuid stableUuid(int index)
{
    return QUuid(QStringLiteral("{00000000-0000-0000-0000-%1}")
        .arg(index, 12, 16, QLatin1Char('0')));
}

CoordinateWGS84 coordinate(double longitude, double latitude)
{
    CoordinateWGS84 result;
    result.longitude_deg = longitude;
    result.latitude_deg = latitude;
    return result;
}

NetworkRenderSnapshot syntheticSnapshot()
{
    NetworkRenderSnapshot snapshot;
    snapshot.geometry_revision = 1;
    snapshot.visual_revision = 1;
    snapshot.nodes.reserve(DemandPointCount + 2 * GridSide);
    snapshot.links.reserve(GridSide);
    snapshot.demand_point_attachments.reserve(DemandPointCount);

    for (int row = 0; row < GridSide; ++row)
    {
        const double latitude = 11.94 + row * 0.0001;
        const quint32 start_id = quint32(DemandPointCount + 2 * row + 1);
        const quint32 end_id = start_id + 1;

        NetworkRenderNode start;
        start.render_id = start_id;
        start.uuid = stableUuid(int(start_id));
        start.entity_type = InfrastructureEntity::Junction;
        start.coordinate_wgs84 = coordinate(18.18, latitude);
        snapshot.nodes.append(start);

        NetworkRenderNode end;
        end.render_id = end_id;
        end.uuid = stableUuid(int(end_id));
        end.entity_type = InfrastructureEntity::Junction;
        end.coordinate_wgs84 = coordinate(18.20, latitude);
        snapshot.nodes.append(end);

        NetworkRenderLink pipe;
        pipe.render_id = quint32(row + 1);
        pipe.uuid = stableUuid(20000 + row);
        pipe.entity_type = InfrastructureEntity::Pipe;
        pipe.start_node_render_id = start_id;
        pipe.end_node_render_id = end_id;
        pipe.vertices_wgs84.append(start.coordinate_wgs84);
        pipe.vertices_wgs84.append(end.coordinate_wgs84);
        pipe.elevations_m.append(0.0);
        pipe.elevations_m.append(0.0);
        snapshot.links.append(pipe);

        for (int column = 0; column < GridSide; ++column)
        {
            const quint32 id = quint32(row * GridSide + column + 1);
            const double longitude = 18.1801 + column * 0.00019;
            NetworkRenderNode demand_point;
            demand_point.render_id = id;
            demand_point.uuid = stableUuid(int(id));
            demand_point.entity_type = InfrastructureEntity::DemandPoint;
            demand_point.coordinate_wgs84 = coordinate(longitude, latitude + 0.00005);
            snapshot.nodes.append(demand_point);

            NetworkRenderDemandPointAttachment attachment;
            attachment.demand_point_render_id = id;
            attachment.demand_point_uuid = demand_point.uuid;
            attachment.demand_point_coordinate_wgs84 = demand_point.coordinate_wgs84;
            attachment.attachment_coordinate_wgs84 = coordinate(longitude, latitude);
            snapshot.demand_point_attachments.append(attachment);
        }
    }
    return snapshot;
}

bool require(bool condition, const char *message)
{
    if (!condition)
        std::fprintf(stderr, "map_zoom_render_baseline_benchmark: %s\n", message);
    return condition;
}
}

int main(int argc, char **argv)
{
    // The standalone benchmark explicitly enables diagnostics before the
    // first counter access. Production builds keep them disabled by default.
    qputenv("AOWIS_MAP_RENDER_DIAGNOSTICS", "1");
    QGuiApplication application(argc, argv);
    const NetworkRenderSnapshot snapshot = syntheticSnapshot();

    if (!require(snapshot.nodes.size() == DemandPointCount + 2 * GridSide,
                 "incorrect synthetic node count")
        || !require(snapshot.demand_point_attachments.size() == DemandPointCount,
                    "incorrect synthetic demand point attachment count"))
    {
        return 1;
    }

    MapRhiScene scene;
    MapNetworkRenderSymbology symbology;
    for (const NetworkRenderLink &pipe : snapshot.links)
        symbology.flow_directions.insert(pipe.render_id, 1);
    scene.setSymbology(symbology);

    QElapsedTimer timer;
    timer.start();
    scene.setNetworkSnapshot(snapshot);
    const double initial_build_ms = double(timer.nsecsElapsed()) / 1.0e6;
    if (!require(scene.nodeVertices().size() >= DemandPointCount,
                 "synthetic demand points were not prepared")
        || !require(scene.linkVertices().size() >= DemandPointCount * 2,
                    "synthetic demand point attachment lines were not prepared"))
    {
        return 1;
    }
    // A repeated snapshot and an attribute-only revision must not prepare
    // or upload the 10,000 demand-point attachments again.
    scene.setNetworkSnapshot(snapshot);
    NetworkRenderSnapshot visual_update = snapshot;
    ++visual_update.visual_revision;
    scene.setNetworkSnapshot(visual_update);
    if (!require(scene.networkSnapshot().visual_revision == visual_update.visual_revision,
                 "visual revision was not retained")
        || !require(MapRenderDiagnostics::value(
                        MapRenderDiagnostics::Counter::FlatGeometryRebuilt) == 1,
                    "unchanged/visual-only snapshot triggered geometry rebuilding")
        || !require(MapRenderDiagnostics::value(
                        MapRenderDiagnostics::Counter::DemandAttachmentsPrepared)
                        == DemandPointCount,
                    "unchanged/visual-only snapshot reprocessed demand attachments"))
    {
        return 1;
    }

    const qsizetype original_link_vertices = scene.linkVertices().size();
    const qsizetype original_node_vertices = scene.nodeVertices().size();
    const quint64 original_geometry_revision = scene.geometryRevision();

    // Repeat integer zoom crossings without modifying snapshot geometry.
    // This intentionally exposes the present icons/flow rebuild cost.
    timer.restart();
    for (int index = 0; index < ZoomTransitions; ++index)
    {
        const int zoom = index % 2 == 0 ? 15 : 16;
        if (!require(scene.setViewZoom(zoom), "zoom transition was ignored"))
            return 1;
        if (!require(scene.geometryRevision() == original_geometry_revision,
                     "camera zoom mutated scene geometry revision")
            || !require(scene.linkVertices().size() == original_link_vertices,
                        "camera zoom changed static link/attachment vertex count")
            || !require(scene.nodeVertices().size() == original_node_vertices,
                        "camera zoom changed static node vertex count"))
        {
            return 1;
        }
    }
    const double zoom_ms = double(timer.nsecsElapsed()) / 1.0e6;

    QJsonObject report = MapRenderDiagnostics::snapshot();
    report.insert(QStringLiteral("kind"), QStringLiteral("aowis_map_zoom_baseline_benchmark"));
    report.insert(QStringLiteral("demand_points"), DemandPointCount);
    report.insert(QStringLiteral("attachment_lines"), DemandPointCount);
    report.insert(QStringLiteral("pipes"), GridSide);
    report.insert(QStringLiteral("zoom_transitions"), ZoomTransitions);
    report.insert(QStringLiteral("initial_build_ms"), initial_build_ms);
    report.insert(QStringLiteral("zoom_transitions_ms"), zoom_ms);
    report.insert(QStringLiteral("static_link_vertices"), double(original_link_vertices));
    report.insert(QStringLiteral("static_node_vertices"), double(original_node_vertices));
    const QByteArray json = QJsonDocument(report).toJson(QJsonDocument::Compact);
    std::printf("%s\n", json.constData());
    return 0;
}
