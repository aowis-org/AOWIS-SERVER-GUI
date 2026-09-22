#include "geo/geo_web_mercator.h"
#include "map/render/map_globe_heatmap_scene.h"
#include "map/render/map_globe_junction_model.h"
#include "map/render/map_globe_picking.h"
#include "map/render/map_globe_reservoir_model.h"
#include "map/render/map_globe_surface_render_frame.h"
#include "map/render/map_globe_tank_model.h"
#include "map/render/map_globe_vertical_transform.h"
#include "map/render/map_node_declutter.h"
#include "map/render/map_render_cache_math.h"
#include "map/render/map_terrain_mesh_scheduler.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QVector>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
int failure_count = 0;

void expectTrue(bool condition, const char *message)
{
    if (condition)
        return;

    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failure_count;
}

void expectNear(double actual, double expected, double tolerance, const char *message)
{
    if (std::isfinite(actual) && std::abs(actual - expected) <= tolerance)
        return;

    std::fprintf(
        stderr,
        "FAIL: %s (actual=%.12f expected=%.12f tolerance=%.12f)\n",
        message,
        actual,
        expected,
        tolerance);
    ++failure_count;
}

template<typename Vertex>
QVector3D modelVertexPosition(const Vertex &vertex)
{
    return QVector3D(vertex.position_x, vertex.position_y, vertex.position_z);
}

template<typename Vertex>
bool triangleMeshHitDistance(
    const QVector<Vertex> &vertices,
    const QVector3D &ray_origin,
    const QVector3D &ray_direction,
    double *nearest_distance_m)
{
    if (nearest_distance_m == nullptr || vertices.size() % 3 != 0)
        return false;

    double nearest_distance = std::numeric_limits<double>::infinity();
    bool hit = false;
    for (qsizetype index = 0; index + 2 < vertices.size(); index += 3)
    {
        double distance_m = 0.0;
        if (!mapGlobeRayTriangleIntersectionDistance(
                ray_origin,
                ray_direction,
                modelVertexPosition(vertices.at(index)),
                modelVertexPosition(vertices.at(index + 1)),
                modelVertexPosition(vertices.at(index + 2)),
                &distance_m))
        {
            continue;
        }

        if (distance_m < nearest_distance)
        {
            nearest_distance = distance_m;
            hit = true;
        }
    }

    if (hit)
        *nearest_distance_m = nearest_distance;
    return hit;
}

template<typename Vertex>
void expectModelVertexMetadata(
    const QVector<Vertex> &vertices,
    quint32 render_id,
    float selected,
    const char *render_id_message,
    const char *selected_message,
    const char *normal_message)
{
    bool render_ids_match = !vertices.isEmpty();
    bool selected_values_match = !vertices.isEmpty();
    bool normals_are_unit_length = !vertices.isEmpty();
    for (const Vertex &vertex : vertices)
    {
        render_ids_match = render_ids_match && vertex.render_id == render_id;
        selected_values_match = selected_values_match
            && std::abs(double(vertex.selected - selected)) <= 1e-6;
        const double normal_length = std::sqrt(
            double(vertex.normal_x) * vertex.normal_x
            + double(vertex.normal_y) * vertex.normal_y
            + double(vertex.normal_z) * vertex.normal_z);
        normals_are_unit_length = normals_are_unit_length
            && std::isfinite(normal_length)
            && std::abs(normal_length - 1.0) <= 1e-5;
    }

    expectTrue(render_ids_match, render_id_message);
    expectTrue(selected_values_match, selected_message);
    expectTrue(normals_are_unit_length, normal_message);
}

void testVerticalTransform()
{
    const MapGlobeVerticalTransform transform(2.0, 0.0);
    expectNear(transform.terrainHeightM(10.0), 20.0, 1e-12,
        "terrain elevation uses vertical exaggeration");
    expectNear(transform.networkLiftM(), 2.0, 1e-12,
        "network anti-z-fighting lift is retained");
    expectNear(transform.networkHeightM(10.0), 22.0, 1e-12,
        "network elevation uses the same vertical transform plus lift");
    expectNear(transform.networkDepthBelowTerrainM(5.0, 10.0), 8.0, 1e-12,
        "underground depth follows rendered positions");

    MapGlobeVerticalTransform sanitized_transform(
        std::numeric_limits<double>::quiet_NaN(), -5.0);
    expectNear(sanitized_transform.verticalExaggeration(), 1.0, 1e-12,
        "invalid vertical exaggeration falls back to one");
    expectNear(sanitized_transform.networkGroundOffsetM(), 0.0, 1e-12,
        "negative network offset clamps to zero");
}

void testRenderCacheMath()
{
    const QSize cache_size = MapRenderCacheMath::boundedCacheLogicalSize(
        QSize(1000, 500), 1.0);
    expectTrue(cache_size == QSize(3000, 1500),
        "normal cache receives three-times overscan");

    const QSize bounded_size = MapRenderCacheMath::boundedCacheLogicalSize(
        QSize(4000, 3000), 2.0);
    expectTrue(bounded_size.width() <= 4000 && bounded_size.height() <= 3000,
        "large high-DPI cache does not grow beyond its viewport");

    const QRectF centered = MapRenderCacheMath::centeredWorldRect(
        QPointF(10.0, 20.0), QSize(200, 100), 10.0);
    expectNear(centered.left(), 0.0, 1e-12,
        "centered cache left edge is correct");
    expectNear(centered.top(), 15.0, 1e-12,
        "centered cache top edge is correct");
    expectNear(centered.width(), 20.0, 1e-12,
        "centered cache width is correct");
    expectNear(centered.height(), 10.0, 1e-12,
        "centered cache height is correct");

    expectTrue(MapRenderCacheMath::coverageCoversView(
        QRectF(-10.0, -10.0, 20.0, 20.0),
        QRectF(-5.0, -5.0, 10.0, 10.0),
        10.0,
        false),
        "cache coverage accepts a contained view");
}

void testNodeDeclutter()
{
    QVector<MapNodeDeclutterInput> nodes;
    nodes.append({30, QPointF(100.0, 100.0)});
    nodes.append({10, QPointF(100.0, 100.0)});
    nodes.append({20, QPointF(100.0, 100.0)});
    nodes.append({40, QPointF(500.0, 500.0)});

    const QHash<quint32, QPointF> offsets = computeNodeDeclutterOffsets(nodes, 12.0);
    expectTrue(offsets.contains(10) && offsets.contains(20) && offsets.contains(30),
        "coincident nodes receive declutter offsets");
    expectTrue(!offsets.contains(40),
        "isolated node remains unmoved");

    const QPointF first_position = QPointF(100.0, 100.0) + offsets.value(10);
    const QPointF second_position = QPointF(100.0, 100.0) + offsets.value(20);
    const QPointF third_position = QPointF(100.0, 100.0) + offsets.value(30);
    expectNear(std::hypot(
        first_position.x() - second_position.x(),
        first_position.y() - second_position.y()), 12.0, 1e-9,
        "first two coincident nodes are separated by the requested distance");
    expectNear(std::hypot(
        second_position.x() - third_position.x(),
        second_position.y() - third_position.y()), 12.0, 1e-9,
        "second and third coincident nodes are separated by the requested distance");
    expectNear(std::hypot(
        third_position.x() - first_position.x(),
        third_position.y() - first_position.y()), 12.0, 1e-9,
        "third and first coincident nodes are separated by the requested distance");
}

void testRayIntersections()
{
    double distance_m = 0.0;
    expectTrue(mapGlobeRayTriangleIntersectionDistance(
        QVector3D(0.0f, 0.0f, 1.0f),
        QVector3D(0.0f, 0.0f, -1.0f),
        QVector3D(-1.0f, -1.0f, 0.0f),
        QVector3D(1.0f, -1.0f, 0.0f),
        QVector3D(0.0f, 1.0f, 0.0f),
        &distance_m),
        "ray intersects triangle");
    expectNear(distance_m, 1.0, 1e-9,
        "triangle intersection distance is correct");

    expectTrue(mapGlobeRaySphereIntersectionDistance(
        QVector3D(0.0f, 0.0f, 5.0f),
        QVector3D(0.0f, 0.0f, -1.0f),
        QVector3D(0.0f, 0.0f, 0.0f),
        2.0,
        &distance_m),
        "ray intersects sphere");
    expectNear(distance_m, 3.0, 1e-9,
        "sphere intersection returns nearest positive hit");

    double nearest_distance_m = 10.0;
    expectTrue(mapGlobeUpdateNearestHitDistance(4.0, &nearest_distance_m),
        "closer finite hit replaces nearest distance");
    expectNear(nearest_distance_m, 4.0, 1e-12,
        "nearest hit distance is updated");
    expectTrue(!mapGlobeUpdateNearestHitDistance(5.0, &nearest_distance_m),
        "farther hit does not replace nearest distance");
}

void testCanonicalScreenRay()
{
    MapGlobeScreenRayParameters parameters;
    parameters.target_lon_deg = 0.0;
    parameters.target_lat_deg = 0.0;
    parameters.yaw_deg = 0.0;
    parameters.pitch_deg = 90.0;
    parameters.distance_m = 1000.0;
    parameters.target_height_m = 0.0;
    parameters.field_of_view_deg = 45.0;

    MapGlobeScreenRay ray;
    expectTrue(mapGlobeBuildScreenRay(
        QPointF(400.0, 300.0), QSize(800, 600), parameters, &ray),
        "center-screen Globe ray can be constructed");
    expectNear(ray.direction.length(), 1.0, 1e-6,
        "constructed Globe screen ray is normalized");

    expectTrue(!mapGlobeBuildScreenRay(
        QPointF(0.0, 0.0), QSize(), parameters, &ray),
        "invalid viewport is rejected");
}

void testHeatmapScene()
{
    MapGlobeHeatmapScene scene(8);
    expectTrue(scene.isEmpty(), "new heatmap scene starts empty");
    expectTrue(scene.bucketLevelCount() == 0,
        "empty heatmap scene has no marker buckets before layout is set");

    MapGlobeHeatmapMarker marker;
    marker.render_id = 17;
    marker.longitude_deg = 11.0;
    marker.latitude_deg = 50.0;
    marker.active = true;
    marker.color = QColor(20, 120, 230);

    QVector<MapGlobeHeatmapMarker> markers;
    markers.append(marker);

    const quint64 initial_revision = scene.revision();
    const quint64 initial_layout_revision = scene.layoutRevision();
    expectTrue(scene.setOverlay(markers, 25000.0, 0.25),
        "setting initial heatmap overlay changes scene");
    expectTrue(scene.revision() != initial_revision,
        "initial heatmap overlay advances content revision");
    expectTrue(scene.layoutRevision() != initial_layout_revision,
        "initial heatmap overlay advances layout revision");
    expectTrue(scene.markerCount() == 1 && scene.activeMarkerCount() == 1,
        "heatmap scene tracks marker and active-marker counts");
    expectTrue(scene.bucketLevelCount() == 9,
        "heatmap scene builds one bucket level per configured zoom");
    expectNear(scene.radiusM(), 25000.0, 1e-9,
        "heatmap scene retains radius");
    expectNear(scene.solidFraction(), 0.25, 1e-12,
        "heatmap scene retains solid fraction");

    const int zoom = 6;
    MapGlobeHeatmapTile tile;
    tile.zoom = zoom;
    tile.virtual_x = int(std::floor(GeoWebMercator::lonToTileX(
        marker.longitude_deg, zoom)));
    tile.tile_y = int(std::floor(GeoWebMercator::latToTileY(
        marker.latitude_deg, zoom)));

    MapGlobeHeatmapTileLayoutCache layout_cache;
    MapGlobeHeatmapRasterStats first_stats;
    const QVector<MapGlobeHeatmapStamp> first_stamps = scene.stampsForTile(
        tile, &layout_cache, &first_stats);
    expectTrue(first_stamps.size() == 1,
        "active marker produces one heatmap stamp on containing tile");
    expectTrue(!first_stats.stamp_layout_cache_hit,
        "first heatmap stamp lookup builds layout cache");
    expectTrue(first_stats.marker_tile_pairs == 1,
        "heatmap stats count active marker-tile pair");
    expectTrue(first_stats.candidate_markers >= 1,
        "heatmap spatial index returns containing marker as candidate");
    expectTrue(first_stamps.at(0).marker_render_id == marker.render_id,
        "heatmap stamp preserves render ID");
    expectTrue(first_stamps.at(0).radius_pixels > 0.0,
        "heatmap stamp has positive pixel radius");

    MapGlobeHeatmapRasterStats cached_stats;
    const QVector<MapGlobeHeatmapStamp> cached_stamps = scene.stampsForTile(
        tile, &layout_cache, &cached_stats);
    expectTrue(cached_stamps.size() == 1 && cached_stats.stamp_layout_cache_hit,
        "second heatmap stamp lookup reuses layout cache");

    const quint64 color_layout_revision = scene.layoutRevision();
    const quint64 color_content_revision = scene.revision();
    markers[0].color = QColor(220, 40, 70);
    expectTrue(scene.setOverlay(markers, 25000.0, 0.25),
        "heatmap color change updates scene");
    expectTrue(scene.revision() != color_content_revision,
        "heatmap color change advances content revision");
    expectTrue(scene.layoutRevision() == color_layout_revision,
        "heatmap color change does not invalidate stamp layout");

    MapGlobeHeatmapRasterStats recolored_stats;
    const QVector<MapGlobeHeatmapStamp> recolored_stamps = scene.stampsForTile(
        tile, &layout_cache, &recolored_stats);
    expectTrue(recolored_stats.stamp_layout_cache_hit,
        "heatmap color change reuses cached stamp layout");
    expectTrue(recolored_stamps.size() == 1
        && recolored_stamps.at(0).color == markers.at(0).color,
        "cached heatmap layout receives current marker color");

    const QImage image = scene.renderStamps(recolored_stamps);
    expectTrue(!image.isNull()
        && image.size() == QSize(
            MapGlobeHeatmapScene::TextureSize,
            MapGlobeHeatmapScene::TextureSize),
        "heatmap rasterization produces expected tile-sized image");
    if (!image.isNull() && !recolored_stamps.isEmpty())
    {
        const int center_x = qBound(
            0, int(std::lround(recolored_stamps.at(0).center_x_pixels)),
            image.width() - 1);
        const int center_y = qBound(
            0, int(std::lround(recolored_stamps.at(0).center_y_pixels)),
            image.height() - 1);
        expectTrue(qAlpha(image.pixel(center_x, center_y)) > 0,
            "heatmap rasterization paints stamp center");
    }

    const quint64 inactive_layout_revision = scene.layoutRevision();
    markers[0].active = false;
    expectTrue(scene.setOverlay(markers, 25000.0, 0.25),
        "heatmap active-state change updates scene");
    expectTrue(scene.layoutRevision() == inactive_layout_revision,
        "heatmap active-state change keeps cached layout valid");
    MapGlobeHeatmapRasterStats inactive_stats;
    const QVector<MapGlobeHeatmapStamp> inactive_stamps = scene.stampsForTile(
        tile, &layout_cache, &inactive_stats);
    expectTrue(inactive_stats.stamp_layout_cache_hit && inactive_stamps.isEmpty(),
        "cached layout filters inactive marker without rebuilding");

    const quint64 radius_layout_revision = scene.layoutRevision();
    expectTrue(scene.setOverlay(markers, 50000.0, 0.25),
        "heatmap radius change updates scene");
    expectTrue(scene.layoutRevision() != radius_layout_revision,
        "heatmap radius change invalidates stamp layout");

    MapGlobeHeatmapTile cap_tile = tile;
    cap_tile.is_cap = true;
    expectTrue(scene.stampsForTile(cap_tile, nullptr).isEmpty(),
        "polar cap never receives Web-Mercator heatmap stamps");

    markers[0].active = true;
    markers[0].longitude_deg = 179.9;
    markers[0].latitude_deg = 0.0;
    expectTrue(scene.setOverlay(markers, 50000.0, 0.25),
        "moving heatmap marker updates layout");
    MapGlobeHeatmapTile wrapped_tile;
    wrapped_tile.zoom = 2;
    wrapped_tile.virtual_x = -1;
    wrapped_tile.tile_y = 2;
    expectTrue(!scene.stampsForTile(wrapped_tile, nullptr).isEmpty(),
        "heatmap stamp lookup follows virtual tiles across antimeridian");
}

void testGlobeModelGeometry()
{
    MapGlobeTankInstance tank;
    tank.render_id = 101;
    tank.base_center = QVector3D(10.0f, 20.0f, 30.0f);
    tank.radius_world = 2.0f;
    tank.base_height_world = 1.0f;
    tank.body_height_world = 4.0f;
    tank.roof_height_world = 1.5f;
    tank.selected = 1.0f;

    QVector<MapGlobeTankInstance> tanks;
    tanks.append(tank);
    const QVector<MapGlobeTankModelVertex> tank_vertices =
        mapGlobeBuildTankModelVertices(tanks);
    expectTrue(!tank_vertices.isEmpty() && tank_vertices.size() % 3 == 0,
        "tank model generation produces triangle-list geometry");
    expectModelVertexMetadata(
        tank_vertices, tank.render_id, tank.selected,
        "tank vertices preserve render ID",
        "tank vertices preserve selection state",
        "tank vertex normals remain normalized");

    float tank_max_z = -std::numeric_limits<float>::infinity();
    for (const MapGlobeTankModelVertex &vertex : tank_vertices)
        tank_max_z = std::max(tank_max_z, vertex.position_z);
    double tank_hit_distance_m = 0.0;
    expectTrue(triangleMeshHitDistance(
        tank_vertices,
        QVector3D(tank.base_center.x(), tank.base_center.y(), tank_max_z + 5.0f),
        QVector3D(0.0f, 0.0f, -1.0f),
        &tank_hit_distance_m),
        "generated tank triangle mesh is pickable from above");
    expectTrue(tank_hit_distance_m > 0.0 && tank_hit_distance_m < 10.0,
        "tank mesh picking returns nearby positive distance");

    MapGlobeReservoirInstance reservoir;
    reservoir.render_id = 202;
    reservoir.base_center = QVector3D(-8.0f, 4.0f, 12.0f);
    reservoir.radius_world = 3.0f;
    reservoir.wall_height_world = 4.0f;
    reservoir.selected = 0.5f;

    QVector<MapGlobeReservoirInstance> reservoirs;
    reservoirs.append(reservoir);
    const QVector<MapGlobeReservoirModelVertex> reservoir_vertices =
        mapGlobeBuildReservoirModelVertices(reservoirs);
    expectTrue(!reservoir_vertices.isEmpty()
        && reservoir_vertices.size() % 3 == 0,
        "reservoir model generation produces triangle-list geometry");
    expectModelVertexMetadata(
        reservoir_vertices, reservoir.render_id, reservoir.selected,
        "reservoir vertices preserve render ID",
        "reservoir vertices preserve selection state",
        "reservoir vertex normals remain normalized");

    float reservoir_max_z = -std::numeric_limits<float>::infinity();
    for (const MapGlobeReservoirModelVertex &vertex : reservoir_vertices)
        reservoir_max_z = std::max(reservoir_max_z, vertex.position_z);
    double reservoir_hit_distance_m = 0.0;
    expectTrue(triangleMeshHitDistance(
        reservoir_vertices,
        QVector3D(
            reservoir.base_center.x(), reservoir.base_center.y(),
            reservoir_max_z + 5.0f),
        QVector3D(0.0f, 0.0f, -1.0f),
        &reservoir_hit_distance_m),
        "generated reservoir triangle mesh is pickable from above");
    expectTrue(reservoir_hit_distance_m > 0.0
        && reservoir_hit_distance_m < 10.0,
        "reservoir mesh picking returns nearby positive distance");

    const QVector<MapGlobeJunctionImpostorVertex> &junction_vertices =
        mapGlobeJunctionImpostorVertices();
    expectTrue(junction_vertices.size() == 6,
        "junction impostor is exactly two triangles");
    bool junction_corners_valid = true;
    for (const MapGlobeJunctionImpostorVertex &vertex : junction_vertices)
    {
        junction_corners_valid = junction_corners_valid
            && std::abs(std::abs(double(vertex.corner_x)) - 1.0) <= 1e-12
            && std::abs(std::abs(double(vertex.corner_y)) - 1.0) <= 1e-12;
    }
    expectTrue(junction_corners_valid,
        "junction impostor vertices stay on canonical unit-quad corners");
}

void testSurfaceRenderFrameInvariants()
{
    QVector<MapGlobeSurfaceVertex> window_vertices;
    window_vertices.append({1.0f, 2.0f, 3.0f, 0.0f, 0.0f});
    QVector<quint32> window_indices;
    window_indices.append(0);
    QVector<MapGlobeSurfaceVertex> cap_vertices;
    cap_vertices.append({4.0f, 5.0f, 6.0f, 1.0f, 1.0f});
    QVector<quint32> cap_indices;
    cap_indices.append(0);
    QVector<MapGlobeSurfaceWireframeVertex> wireframe_vertices;
    wireframe_vertices.append({7.0f, 8.0f, 9.0f});

    MapGlobeSurfaceRenderFrame frame;
    frame.viewport_size = QSize(800, 600);
    frame.resources.window_vertices = &window_vertices;
    frame.resources.window_indices = &window_indices;
    frame.resources.cap_vertices = &cap_vertices;
    frame.resources.cap_indices = &cap_indices;
    frame.resources.wireframe_vertices = &wireframe_vertices;
    expectTrue(frame.resources.isValid(),
        "surface render resources require all neutral geometry streams");
    expectTrue(frame.isValid(),
        "surface render frame is valid with viewport and all neutral resources");

    frame.viewport_size = QSize();
    expectTrue(!frame.isValid(),
        "surface render frame rejects invalid viewport");
    frame.viewport_size = QSize(800, 600);
    frame.resources.window_vertices = nullptr;
    expectTrue(!frame.resources.isValid() && !frame.isValid(),
        "surface render frame rejects missing neutral geometry stream");
}

void testTerrainMeshGenerationAndStitching()
{
    MapTerrainMeshRequest request;
    request.request_id = 77;
    request.terrain_key = QStringLiteral("test-terrain");
    request.virtual_x = 512;
    request.tile_x = 512;
    request.y = 512;
    request.imagery_zoom = 10;
    request.terrain_zoom = 10;
    request.requested_cell_count = 4;
    request.stitch_top_cell_count = 2;

    const double center_lon = GeoWebMercator::tileXToLon(512.5, 10);
    const double center_lat = GeoWebMercator::tileYToLat(512.5, 10);
    const GeoWgs84Ellipsoid::EcefPositionD render_origin =
        GeoWgs84Ellipsoid::geodeticToEcefD(center_lon, center_lat, 0.0);
    request.globe_render_origin_x = render_origin.x;
    request.globe_render_origin_y = render_origin.y;
    request.globe_render_origin_z = render_origin.z;

    const MapTerrainMeshResult result = buildTerrainMeshResult(request);
    expectTrue(result.request_id == request.request_id
        && result.terrain_key == request.terrain_key,
        "terrain mesh result preserves request identity");
    expectTrue(result.cell_count == 4,
        "terrain mesh respects requested cell count below native resolution");
    expectTrue(result.stitch_top_cell_count == 2,
        "terrain mesh retains valid coarser top-edge stitch count");
    expectTrue(result.vertices.size() == 25,
        "four-cell terrain mesh produces five-by-five vertex grid");
    expectNear(result.vertices.at(0).u, 0.0, 1e-12,
        "terrain mesh first vertex has u=0");
    expectNear(result.vertices.at(0).v, 0.0, 1e-12,
        "terrain mesh first vertex has v=0");
    expectNear(result.vertices.constLast().u, 1.0, 1e-12,
        "terrain mesh last vertex has u=1");
    expectNear(result.vertices.constLast().v, 1.0, 1e-12,
        "terrain mesh last vertex has v=1");

    if (result.vertices.size() >= 3)
    {
        const QVector3D top0(
            result.vertices.at(0).x,
            result.vertices.at(0).y,
            result.vertices.at(0).z);
        const QVector3D top1(
            result.vertices.at(1).x,
            result.vertices.at(1).y,
            result.vertices.at(1).z);
        const QVector3D top2(
            result.vertices.at(2).x,
            result.vertices.at(2).y,
            result.vertices.at(2).z);
        const QVector3D stitched_midpoint = (top0 + top2) * 0.5f;
        expectNear((top1 - stitched_midpoint).length(), 0.0, 1e-3,
            "fine top-edge vertex is linearly stitched to coarser neighbor edge");
    }

    request.stitch_top_cell_count = 3;
    const MapTerrainMeshResult incompatible_stitch = buildTerrainMeshResult(request);
    expectTrue(incompatible_stitch.stitch_top_cell_count == 0,
        "terrain mesh rejects non-divisor stitch count");

    request.imagery_zoom = 8;
    request.terrain_zoom = 9;
    const MapTerrainMeshResult invalid_zoom_order = buildTerrainMeshResult(request);
    expectTrue(invalid_zoom_order.vertices.isEmpty()
        && invalid_zoom_order.cell_count == 0,
        "terrain mesh rejects imagery zoom below terrain zoom");
}
}

int main()
{
    testVerticalTransform();
    testRenderCacheMath();
    testNodeDeclutter();
    testRayIntersections();
    testCanonicalScreenRay();
    testHeatmapScene();
    testGlobeModelGeometry();
    testSurfaceRenderFrameInvariants();
    testTerrainMeshGenerationAndStitching();

    if (failure_count != 0)
    {
        std::fprintf(stderr, "%d neutral rendering test(s) failed.\n", failure_count);
        return 1;
    }

    std::printf("All neutral rendering tests passed.\n");
    return 0;
}
