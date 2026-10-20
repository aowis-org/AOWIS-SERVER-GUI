#include "geo/geo_web_mercator.h"
#include "map/data/map_terrain_dataset.h"
#include "map/render/map_globe_heatmap_scene.h"
#include "map/render/map_globe_picking.h"
#include "map/render/map_globe_reservoir_model.h"
#include "map/render/map_globe_surface_scene.h"
#include "map/render/map_globe_terrain_levels.h"
#include "map/render/map_globe_tank_model.h"
#include "map/render/map_globe_vertical_transform.h"
#include "map/core/map_cached_terrain_altitude.h"
#include "map/core/map_scale_distance.h"
#include "map/monitor/map_hud_length_units.h"
#include "map/render/map_node_declutter.h"
#include "map/render/map_render_cache_math.h"
#include "map/render/map_terrain_mesh_scheduler.h"
#include "test_harness.h"

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
#include <functional>
#include <limits>

namespace
{
AowisTestHarness test_harness;

void expectTrue(bool condition, const char *message)
{
    test_harness.expectTrue(condition, message);
}

void expectNear(double actual, double expected, double tolerance, const char *message)
{
    test_harness.expectNear(actual, expected, tolerance, message);
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

void testMapHudLengthUnits()
{
    UnitPresentationContext context;
    QJsonObject units;
    units.insert(QStringLiteral("length"), QStringLiteral("km"));
    units.insert(QStringLiteral("distance"), QStringLiteral("m"));
    units.insert(QStringLiteral("vertical_offset"), QStringLiteral("m"));
    context.initialize(units);
    expectTrue(MapHudLengthUnits::cameraDistance(500.0, context)
                   .endsWith(QStringLiteral(" m")),
               "pipe length does not affect metric camera distance");
    expectTrue(MapHudLengthUnits::cameraDistance(10000.0, context)
                   .endsWith(QStringLiteral(" km")),
               "far camera HUD displays kilometres");
    expectTrue(MapHudLengthUnits::networkOffset(0.1, context)
                   .endsWith(QStringLiteral(" m")),
               "pipe length does not affect the network offset");

    units.insert(QStringLiteral("distance"), QStringLiteral("ft"));
    context.update(units);
    expectTrue(MapHudLengthUnits::cameraDistance(500.0, context)
                   .endsWith(QStringLiteral(" ft")),
               "distance setting updates near camera display to feet");
    expectTrue(MapHudLengthUnits::cameraDistance(10000.0, context)
                   .endsWith(QStringLiteral(" mi")),
               "distance setting updates far camera display to miles");
    expectTrue(MapHudLengthUnits::networkOffset(50.0, context)
                   .endsWith(QStringLiteral(" m")),
               "camera distance setting does not affect network offset");

    units.insert(QStringLiteral("vertical_offset"), QStringLiteral("ft"));
    context.update(units);
    expectTrue(MapHudLengthUnits::networkOffset(50.0, context)
                   .endsWith(QStringLiteral(" ft")),
               "network offset uses selected vertical offset unit");
    expectTrue(MapHudLengthUnits::cameraDistance(500.0, context)
                   .endsWith(QStringLiteral(" ft")),
               "vertical offset unit does not change camera distance");

    units.insert(QStringLiteral("distance"), QStringLiteral("km"));
    context.update(units);
    expectTrue(MapHudLengthUnits::cameraDistance(0.1, context)
                   .endsWith(QStringLiteral(" km")),
               "explicit compact distance unit is respected");
    expectTrue(!MapHudLengthUnits::networkOffset(0.1, context)
                    .startsWith(QStringLiteral("0.0000 ")),
               "small nonzero offset remains visible with its independent unit");
}

void testMapScaleUnits()
{
    UnitPresentationContext context;
    QJsonObject effective_units;
    effective_units.insert(QStringLiteral("length"), QStringLiteral("km"));
    effective_units.insert(QStringLiteral("distance"), QStringLiteral("m"));
    context.initialize(effective_units);

    const MapScaleDistance::Scale metric =
        MapScaleDistance::forMaximumDistance(900.0, context);
    expectNear(metric.distance_m, 500.0, 1e-9,
        "metric scale uses a nice real-world distance");
    expectTrue(metric.label.endsWith(QStringLiteral(" m")),
        "metric scale follows distance, not pipe length");

    const MapScaleDistance::Scale kilometre =
        MapScaleDistance::forMaximumDistance(4000.0, context);
    expectNear(kilometre.distance_m, 3000.0, 1e-9,
        "large metric scale uses a nice kilometre distance");
    expectTrue(kilometre.label.endsWith(QStringLiteral(" km")),
        "large metric scale displays kilometres");

    effective_units.insert(QStringLiteral("distance"), QStringLiteral("ft"));
    expectTrue(context.update(effective_units),
        "changing distance units increments the display generation");
    const MapScaleDistance::Scale feet =
        MapScaleDistance::forMaximumDistance(900.0, context);
    expectTrue(feet.label.endsWith(QStringLiteral(" ft")),
        "imperial scale follows distance setting");
    expectTrue(feet.distance_m > 0.0 && feet.distance_m <= 900.0,
        "imperial scale fits its physical viewport distance");

    const MapScaleDistance::Scale mile =
        MapScaleDistance::forMaximumDistance(4000.0, context);
    expectTrue(mile.label.endsWith(QStringLiteral(" mi")),
        "large imperial scale displays miles");
    expectTrue(mile.distance_m > 0.0 && mile.distance_m <= 4000.0,
        "mile scale fits its physical viewport distance");

    effective_units.insert(QStringLiteral("distance"), QStringLiteral("mi"));
    context.update(effective_units);
    const MapScaleDistance::Scale small_miles =
        MapScaleDistance::forMaximumDistance(90.0, context);
    expectTrue(small_miles.label.endsWith(QStringLiteral(" mi")),
        "an explicit mile distance profile remains in miles at short distances");
    expectTrue(small_miles.distance_m > 0.0 && small_miles.distance_m <= 90.0,
        "explicit mile scale remains physically correct");

    // Changing pipe length alone must not affect either map scale units or geometry.
    effective_units.insert(QStringLiteral("length"), QStringLiteral("ft"));
    context.update(effective_units);
    const MapScaleDistance::Scale unchanged =
        MapScaleDistance::forMaximumDistance(90.0, context);
    expectTrue(unchanged.label == small_miles.label
               && unchanged.distance_m == small_miles.distance_m,
        "pipe length setting is independent from map distance");

    const MapScaleDistance::Scale invalid =
        MapScaleDistance::forMaximumDistance(-1.0, context);
    expectTrue(invalid.distance_m == 0.0 && invalid.label.isEmpty(),
        "invalid scale distances produce no label");
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
}

void testCachedTerrainAltitudeFollowsCoordinate()
{
    CoordinateWGS84 coordinate;
    coordinate.longitude_deg = 18.187213;
    coordinate.latitude_deg = 11.948855;
    coordinate.altitude_m = 500.0; // Should not leak through on a cache miss.

    int query_count = 0;
    const CoordinateWGS84 cached = mapWithCachedTerrainAltitude(
        coordinate, [&query_count](const CoordinateWGS84 &requested, double *height_m)
        {
            ++query_count;
            expectNear(requested.longitude_deg, 18.187213, 0.0,
                "terrain lookup receives original 2D longitude");
            expectNear(requested.latitude_deg, 11.948855, 0.0,
                "terrain lookup receives original 2D latitude");
            *height_m = 123.456;
            return true;
        });
    expectTrue(query_count == 1, "terrain lookup occurs exactly once");
    expectNear(cached.longitude_deg, coordinate.longitude_deg, 0.0,
        "cached terrain height leaves longitude unchanged");
    expectNear(cached.latitude_deg, coordinate.latitude_deg, 0.0,
        "cached terrain height leaves latitude unchanged");
    expectTrue(cached.altitude_m.has_value(), "cached terrain height becomes available");
    if (cached.altitude_m.has_value())
        expectNear(*cached.altitude_m, 123.456, 1e-12,
            "cached terrain elevation is displayed");

    const CoordinateWGS84 missing = mapWithCachedTerrainAltitude(
        coordinate, [](const CoordinateWGS84 &, double *) { return false; });
    expectTrue(!missing.altitude_m.has_value(),
        "missing terrain tile leaves footer height unknown");
    const CoordinateWGS84 unavailable = mapWithCachedTerrainAltitude(
        coordinate, MapCachedTerrainElevationLookup());
    expectTrue(!unavailable.altitude_m.has_value(),
        "absent renderer leaves footer height unknown");
    const CoordinateWGS84 invalid = mapWithCachedTerrainAltitude(
        coordinate, [](const CoordinateWGS84 &, double *height_m)
        {
            *height_m = std::numeric_limits<double>::quiet_NaN();
            return true;
        });
    expectTrue(!invalid.altitude_m.has_value(),
        "nonfinite cached terrain height is not displayed");
}

void testTerrainMeshGeneration()
{
    // Imagery four levels below the DEM: 64 >> 4 = 4 cells per imagery tile.
    MapTerrainMeshRequest request;
    request.request_id = 77;
    request.terrain_key = QStringLiteral("test-terrain");
    request.virtual_x = 512 * 16;
    request.tile_x = 512 * 16;
    request.y = 512 * 16;
    request.imagery_zoom = 14;
    request.terrain_zoom = 10;

    const double center_lon = GeoWebMercator::tileXToLon(512.5 * 16.0, 14);
    const double center_lat = GeoWebMercator::tileYToLat(512.5 * 16.0, 14);
    const GeoWgs84Ellipsoid::EcefPositionD render_origin =
        GeoWgs84Ellipsoid::geodeticToEcefD(center_lon, center_lat, 0.0);
    request.globe_render_origin_x = render_origin.x;
    request.globe_render_origin_y = render_origin.y;
    request.globe_render_origin_z = render_origin.z;

    const MapTerrainMeshResult result = buildTerrainMeshResult(request);
    expectTrue(result.request_id == request.request_id
        && result.terrain_key == request.terrain_key,
        "terrain mesh result preserves request identity");
    expectTrue(result.cell_count == 4 && mapGlobeTerrainCellCount(14, 10) == 4,
        "terrain mesh covers exactly the DEM cells under the imagery tile");
    expectTrue(result.vertices.size() == 25 + 4 * 5 && mapGlobeTileVertexCount(4) == 25 + 4 * 5,
        "four-cell terrain mesh produces a five-by-five vertex grid and its skirt");
    if (result.vertices.size() != mapGlobeTileVertexCount(4))
        return;
    expectNear(result.vertices.at(0).u, 0.0, 1e-12,
        "terrain mesh first vertex has u=0");
    expectNear(result.vertices.at(0).v, 0.0, 1e-12,
        "terrain mesh first vertex has v=0");
    expectNear(result.vertices.at(24).u, 1.0, 1e-12,
        "terrain mesh last grid vertex has u=1");
    expectNear(result.vertices.at(24).v, 1.0, 1e-12,
        "terrain mesh last grid vertex has v=1");

    // Skirt: top row, bottom row, left column, right column, each the edge
    // vertex lowered by the skirt depth with the edge vertex's texture
    // coordinates.
    const std::function<double(const MapTerrainMeshVertex &)> geocentric_radius_m =
        [&render_origin](const MapTerrainMeshVertex &vertex)
    {
        const double x = render_origin.x + double(vertex.x);
        const double y = render_origin.y + double(vertex.y);
        const double z = render_origin.z + double(vertex.z);
        return std::sqrt(x * x + y * y + z * z);
    };
    const double zoom_10_skirt_depth_m = mapGlobeTileSkirtDepthM(10, 512, 4, 1.0, 0.0);
    expectTrue(zoom_10_skirt_depth_m > 9000.0 && zoom_10_skirt_depth_m < 10000.0,
        "skirt depth of an equatorial zoom-10 four-cell tile is one cell length");
    expectNear(mapGlobeTileSkirtDepthM(10, 512, 4, 2.0, 1500.0), 2.0 * (zoom_10_skirt_depth_m + 1500.0), 1e-6,
        "skirt depth adds the DEM elevation range, all exaggerated");
    const double skirt_depth_m = mapGlobeTileSkirtDepthM(14, 512 * 16, 4, 1.0, 0.0);
    const int skirt_grid_vertex[4][2] = {{25, 0}, {25 + 5 + 4, 24}, {25 + 10 + 2, 10}, {25 + 15 + 3, 19}};
    for (const int (&pair)[2] : skirt_grid_vertex)
    {
        const MapTerrainMeshVertex &skirt = result.vertices.at(pair[0]);
        const MapTerrainMeshVertex &edge = result.vertices.at(pair[1]);
        expectNear(geocentric_radius_m(edge) - geocentric_radius_m(skirt), skirt_depth_m, 0.5,
            "skirt vertex hangs the skirt depth below its edge vertex");
        expectTrue(skirt.u == edge.u && skirt.v == edge.v,
            "skirt vertex keeps its edge vertex's texture coordinates");
    }

    request.imagery_zoom = 8;
    request.terrain_zoom = 9;
    const MapTerrainMeshResult invalid_zoom_order = buildTerrainMeshResult(request);
    expectTrue(invalid_zoom_order.vertices.isEmpty()
        && invalid_zoom_order.cell_count == 0,
        "terrain mesh rejects imagery zoom below terrain zoom");
}

// The drawn terrain is the provider's heightfield: one mesh cell per DEM
// cell, every vertex exactly on a DEM sample, at the imagery tile's own DEM
// zoom and at every subdivision of it.
void testTerrainMeshFollowsDemSamples()
{
    // A DEM whose every sample differs (no plane: a thinned or interpolated
    // mesh would not reproduce it).
    const std::function<double(int, int)> sample_height_m = [](int row, int column)
    {
        return 500.0 + 3.0 * double(row) + 7.0 * double(column) + 40.0 * double((row * 31 + column * 17) % 5);
    };
    MapTerrainTile terrain_tile;
    terrain_tile.address.zoom = 12;
    terrain_tile.address.x = 2150;
    terrain_tile.address.y = 1400;
    terrain_tile.dataset = QStringLiteral("test-dataset");
    terrain_tile.vertical_datum = MapTerrainVerticalDatum::Wgs84Ellipsoid;
    terrain_tile.elevations_m.resize(MapTerrainTileSampleCount);
    double minimum_m = std::numeric_limits<double>::infinity();
    double maximum_m = -std::numeric_limits<double>::infinity();
    for (int row = 0; row < MapTerrainTileGridSize; ++row)
    {
        for (int column = 0; column < MapTerrainTileGridSize; ++column)
        {
            const double height_m = sample_height_m(row, column);
            terrain_tile.elevations_m[row * MapTerrainTileGridSize + column] = float(height_m);
            minimum_m = std::min(minimum_m, height_m);
            maximum_m = std::max(maximum_m, height_m);
        }
    }
    terrain_tile.minimum_elevation_m = minimum_m;
    terrain_tile.maximum_elevation_m = maximum_m;

    // zoom_delta 0: the whole DEM tile, 64 cells. zoom_delta 2: imagery tile
    // (1, 3) of the 4 x 4 below it, 16 cells starting at DEM sample (48, 16).
    const int zoom_deltas[2] = {0, 2};
    for (const int zoom_delta : zoom_deltas)
    {
        const int local_x = zoom_delta == 0 ? 0 : 1;
        const int local_y = zoom_delta == 0 ? 0 : 3;
        MapTerrainMeshRequest request;
        request.terrain_tile = terrain_tile;
        request.terrain_available = true;
        request.imagery_zoom = 12 + zoom_delta;
        request.terrain_zoom = 12;
        request.tile_x = int(terrain_tile.address.x << zoom_delta) + local_x;
        request.virtual_x = request.tile_x;
        request.y = int(terrain_tile.address.y << zoom_delta) + local_y;
        const GeoWgs84Ellipsoid::EcefPositionD origin = GeoWgs84Ellipsoid::geodeticToEcefD(
            GeoWebMercator::tileXToLon(double(request.tile_x) + 0.5, request.imagery_zoom),
            GeoWebMercator::tileYToLat(double(request.y) + 0.5, request.imagery_zoom), 0.0);
        request.globe_render_origin_x = origin.x;
        request.globe_render_origin_y = origin.y;
        request.globe_render_origin_z = origin.z;

        const MapTerrainMeshResult result = buildTerrainMeshResult(request);
        const int cells = MapTerrainTileCellCount >> zoom_delta;
        expectTrue(result.cell_count == cells && result.vertices.size() == mapGlobeTileVertexCount(cells),
            "terrain mesh has one cell per DEM cell");
        if (result.vertices.size() != mapGlobeTileVertexCount(cells))
            continue;

        double largest_error_m = 0.0;
        for (int row = 0; row <= cells; ++row)
        {
            for (int column = 0; column <= cells; ++column)
            {
                const MapTerrainMeshVertex &vertex = result.vertices.at(row * (cells + 1) + column);
                GeoWgs84Ellipsoid::EcefPositionD position;
                position.x = origin.x + double(vertex.x);
                position.y = origin.y + double(vertex.y);
                position.z = origin.z + double(vertex.z);
                double longitude_deg = 0.0;
                double latitude_deg = 0.0;
                double height_m = 0.0;
                GeoWgs84Ellipsoid::ecefToGeodetic(position, &longitude_deg, &latitude_deg, &height_m);
                const double expected_m =
                    sample_height_m(local_y * cells + row, local_x * cells + column);
                largest_error_m = std::max(largest_error_m, std::abs(height_m - expected_m));
            }
        }
        // Float vertices relative to the render origin: centimetres.
        expectTrue(largest_error_m < 0.05,
            "every terrain mesh vertex lies on its DEM sample");
    }
}

// The tile topology every window tile with a cell count shares: relative to
// the tile's first vertex, grid rows contiguous, then the skirt, the same
// triangles the per-tile absolute index lists had.
void testSharedTileTopology()
{
    const int cell_counts[] = {1, 2, 4, 8, 12, 16, 24, 32, 64};
    for (const int cells : cell_counts)
    {
        const QVector<quint16> &indices = mapGlobeTileIndices(cells);
        expectTrue(indices.size() == mapGlobeTileIndexCount(cells),
            "shared topology has the tile's index count");
        expectTrue(&mapGlobeTileIndices(cells) == &indices
            && mapGlobeTileIndices(cells).constData() == indices.constData(),
            "shared topology is built once and keeps its address");
        int largest = 0;
        for (const quint16 index : indices)
            largest = std::max(largest, int(index));
        expectTrue(largest == mapGlobeTileVertexCount(cells) - 1,
            "shared topology addresses exactly the tile's grid and skirt vertices");

        // Reference: the former per-tile generator at first vertex 0.
        const int width = cells + 1;
        bool same = true;
        qsizetype at = 0;
        for (int row = 0; row < cells && same; ++row)
        {
            for (int column = 0; column < cells && same; ++column)
            {
                const int p00 = row * width + column;
                const int expected[6] = {p00, p00 + width, p00 + 1, p00 + 1, p00 + width, p00 + width + 1};
                for (const int value : expected)
                    same = same && int(indices.at(at++)) == value;
            }
        }
        const int skirt_first = mapGlobeTileGridVertexCount(cells);
        for (int edge = 0; edge < 4 && same; ++edge)
        {
            for (int index = 0; index < cells && same; ++index)
            {
                const int g0 = edge == 0 ? index
                    : edge == 1 ? cells * width + index
                    : index * width + (edge == 2 ? 0 : cells);
                const int g1 = edge < 2 ? g0 + 1 : g0 + width;
                const int s0 = skirt_first + edge * width + index;
                const int expected[6] = {g0, s0, g1, g1, s0, s0 + 1};
                for (const int value : expected)
                    same = same && int(indices.at(at++)) == value;
            }
        }
        expectTrue(same, "shared topology equals the former per-tile triangles");
    }
    expectTrue(mapGlobeTileIndices(0).isEmpty() && mapGlobeTileIndices(65).isEmpty(),
        "no topology outside 1..64 cells");
}

// Foreshortening weight of the quadtree's refinement: head-on 1, the square
// root of the facing cosine at an angle, the floor at a grazing angle, and 1
// for a node close to the eye whatever its angle.
void testQuadtreeForeshortening()
{
    const QVector3D center(float(GeoWgs84Ellipsoid::EquatorialRadiusM), 0.0f, 0.0f);
    const double radius_m = 1000.0;
    const QVector3D up(1.0f, 0.0f, 0.0f);
    const QVector3D along(0.0f, 1.0f, 0.0f);
    expectNear(mapGlobeQuadtreeNodeForeshortening(center, radius_m, center + up * 50000.0f), 1.0, 1e-6,
        "a node seen head-on keeps its projected size");
    const QVector3D oblique = center + (up + along).normalized() * 50000.0f;
    expectNear(mapGlobeQuadtreeNodeForeshortening(center, radius_m, oblique), std::sqrt(std::sqrt(0.5)), 1e-4,
        "a node seen at 45 degrees is weighted by the square root of its facing cosine");
    const QVector3D grazing = center + along * 50000.0f + up * 50.0f;
    expectNear(mapGlobeQuadtreeNodeForeshortening(center, radius_m, grazing), MapGlobeQuadtreeMinimumForeshortening,
        1e-3, "a node seen at a grazing angle is weighted by the floor");
    expectNear(mapGlobeQuadtreeNodeForeshortening(center, radius_m, center + along * 1500.0f), 1.0, 1e-6,
        "a node within two radii of the eye keeps its projected size");
    expectNear(mapGlobeQuadtreeNodeForeshortening(center, radius_m, center + along * 3000.0f),
        0.5 * (1.0 + MapGlobeQuadtreeMinimumForeshortening), 1e-3,
        "between two and four radii the weight blends to the centre's angle");
    expectNear(mapGlobeQuadtreeNodeForeshortening(QVector3D(0.0f, 0.0f, 0.0f), 6.0e6, up * 2.0e7f), 1.0, 1e-6,
        "a node centred on the Earth's centre (the root levels) is not weighted");
}

// The terrain service's catalog (AOWIS-SERVER-MAP
// encodeTerrainDatasetCatalogJson()) as the repository reads it.
void testTerrainDatasetCatalog()
{
    const QByteArray json = QByteArrayLiteral(
        "{\"default_dataset\":\"copernicus-glo30\",\"datasets\":["
        "{\"dataset\":\"copernicus-glo30\",\"provider\":\"copernicus\","
        "\"nominal_resolution_m\":30,\"vertical_datum\":\"egm2008\",\"surface_model\":\"dsm\","
        "\"minimum_zoom\":8,\"maximum_zoom\":14,\"tile_grid_size\":65},"
        "{\"dataset\":\"other-grid\",\"provider\":\"x\",\"nominal_resolution_m\":1,"
        "\"vertical_datum\":\"egm2008\",\"surface_model\":\"dtm\",\"minimum_zoom\":10,"
        "\"maximum_zoom\":19,\"tile_grid_size\":257},"
        "{\"dataset\":\"bad-range\",\"provider\":\"x\",\"nominal_resolution_m\":1,"
        "\"vertical_datum\":\"egm2008\",\"surface_model\":\"dtm\",\"minimum_zoom\":15,"
        "\"maximum_zoom\":12,\"tile_grid_size\":65}]}");
    QString error;
    const std::optional<MapTerrainDatasetCatalog> catalog = decodeMapTerrainDatasetCatalog(json, &error);
    expectTrue(catalog.has_value(), "terrain dataset catalog decodes");
    if (!catalog.has_value())
        return;
    expectTrue(catalog->default_dataset == QStringLiteral("copernicus-glo30")
        && catalog->datasets.size() == 1,
        "catalog keeps the valid dataset and drops other grids and invalid ranges");
    const MapTerrainDatasetInfo *info = catalog->find(QStringLiteral("copernicus-glo30"));
    expectTrue(info != nullptr && info->minimum_zoom == 8 && info->maximum_zoom == 14
        && info->vertical_datum == MapTerrainVerticalDatum::Egm2008
        && std::abs(info->nominal_resolution_m - 30.0) < 1e-9 && info->surface_model == QStringLiteral("dsm"),
        "catalog entry carries the provider's range, datum and resolution");
    expectTrue(catalog->find(QStringLiteral("other-grid")) == nullptr,
        "a dataset with another tile grid is not offered to the renderer");

    expectTrue(!decodeMapTerrainDatasetCatalog(QByteArrayLiteral("[]")).has_value()
        && !decodeMapTerrainDatasetCatalog(
               QByteArrayLiteral("{\"default_dataset\":\"\",\"datasets\":[]}")).has_value(),
        "malformed catalogs are rejected");
}

// The DEM zoom per imagery zoom: the dataset's range from its catalog, the
// detail reduction below the imagery, never finer than the native detail.
void testTerrainLevels()
{
    MapGlobeTerrainLevels levels;
    expectTrue(!levels.enabled() && levels.terrainZoomForImageryZoom(14) == -1,
        "no terrain dataset: no relief");

    levels.dataset = QStringLiteral("copernicus-glo30");
    levels.minimum_zoom = 8;
    levels.maximum_zoom = 14;
    levels.detail_reduction = 0;
    expectTrue(levels.enabled(), "a dataset with a zoom range draws relief");
    expectTrue(!levels.reliefAtImageryZoom(7) && levels.terrainZoomForImageryZoom(7) == -1,
        "no relief below the minimum zoom");
    expectTrue(levels.terrainZoomForImageryZoom(8) == 8 && levels.terrainZoomForImageryZoom(12) == 12
        && levels.terrainZoomForImageryZoom(14) == 14,
        "full detail draws the imagery's own DEM zoom");
    expectTrue(levels.terrainZoomForImageryZoom(17) == 14 && levels.terrainZoomForImageryZoom(19) == 14,
        "past the native detail the maximum-zoom DEM tile is subdivided");
    expectTrue(mapGlobeTerrainCellCount(12, 12) == 64 && mapGlobeTerrainCellCount(17, 14) == 8
        && mapGlobeTerrainCellCount(19, 14) == 2 && mapGlobeTerrainCellCount(21, 14) == 1,
        "cell count is the DEM's resolution under the imagery tile");

    levels.detail_reduction = 2;
    expectTrue(levels.terrainZoomForImageryZoom(12) == 10 && levels.terrainZoomForImageryZoom(9) == 8
        && levels.terrainZoomForImageryZoom(16) == 14 && levels.terrainZoomForImageryZoom(18) == 14,
        "a detail reduction draws coarser DEM levels, clamped to the dataset's range");
    expectTrue(mapGlobeTerrainCellCount(12, levels.terrainZoomForImageryZoom(12)) == 16,
        "two levels of reduction draw a quarter of the cells per side");

    MapGlobeTerrainLevels other = levels;
    expectTrue(other == levels, "identical terrain levels compare equal");
    other.maximum_zoom = 16;
    expectTrue(other != levels, "another dataset range is a change");
}

// Hillshading normals: perpendicular to the DEM's slope, identical on both
// sides of a DEM tile border when the neighbor tile is carried, and the
// missing neighbors reported otherwise.
MapTerrainTile globalSampleTile(int zoom, quint32 x, quint32 y,
                                const std::function<double(double, double)> &height_at_sample)
{
    MapTerrainTile tile;
    tile.address.zoom = zoom;
    tile.address.x = x;
    tile.address.y = y;
    tile.dataset = QStringLiteral("test-dataset");
    tile.vertical_datum = MapTerrainVerticalDatum::Wgs84Ellipsoid;
    tile.elevations_m.resize(MapTerrainTileSampleCount);
    double minimum_m = std::numeric_limits<double>::infinity();
    double maximum_m = -std::numeric_limits<double>::infinity();
    for (int row = 0; row < MapTerrainTileGridSize; ++row)
    {
        for (int column = 0; column < MapTerrainTileGridSize; ++column)
        {
            // Global sample coordinates: shared border samples agree.
            const double height_m = height_at_sample(double(x) * MapTerrainTileCellCount + column,
                                                     double(y) * MapTerrainTileCellCount + row);
            tile.elevations_m[row * MapTerrainTileGridSize + column] = float(height_m);
            minimum_m = std::min(minimum_m, height_m);
            maximum_m = std::max(maximum_m, height_m);
        }
    }
    tile.minimum_elevation_m = minimum_m;
    tile.maximum_elevation_m = maximum_m;
    return tile;
}

QVector3D unpackedNormal(const MapGlobePackedNormal &normal)
{
    return QVector3D(float(normal.x) / 127.5f - 1.0f, float(normal.y) / 127.5f - 1.0f,
                     float(normal.z) / 127.5f - 1.0f);
}

MapTerrainMeshRequest normalTestRequest(const MapTerrainTile &tile)
{
    MapTerrainMeshRequest request;
    request.terrain_tile = tile;
    request.terrain_available = true;
    request.imagery_zoom = tile.address.zoom;
    request.terrain_zoom = tile.address.zoom;
    request.tile_x = int(tile.address.x);
    request.virtual_x = request.tile_x;
    request.y = int(tile.address.y);
    const GeoWgs84Ellipsoid::EcefPositionD origin = GeoWgs84Ellipsoid::geodeticToEcefD(
        GeoWebMercator::tileXToLon(double(tile.address.x) + 0.5, tile.address.zoom),
        GeoWebMercator::tileYToLat(double(tile.address.y) + 0.5, tile.address.zoom), 0.0);
    request.globe_render_origin_x = origin.x;
    request.globe_render_origin_y = origin.y;
    request.globe_render_origin_z = origin.z;
    return request;
}

void testTerrainNormals()
{
    const int zoom = 12;
    const quint32 x = 2150;
    const quint32 y = 1400;
    const int cells = MapTerrainTileCellCount;

    // Flat DEM: every normal is the ellipsoid's up.
    {
        const MapTerrainTile flat = globalSampleTile(zoom, x, y, [](double, double) { return 300.0; });
        const MapTerrainMeshResult result = buildTerrainMeshResult(normalTestRequest(flat));
        const MapTerrainMeshVertex &vertex = result.vertices.at(32 * (cells + 1) + 32);
        const double lon = GeoWebMercator::tileXToLon(double(x) + 0.5, zoom);
        const double lat = GeoWebMercator::tileYToLat(double(y) + 0.5, zoom);
        const GeoWgs84Ellipsoid::LocalFrame frame = GeoWgs84Ellipsoid::localFrameAtGeodetic(lon, lat, 0.0);
        expectTrue(QVector3D::dotProduct(unpackedNormal(vertex.normal), frame.up) > 0.999f,
            "a flat DEM's normal is the ellipsoid's up");
    }

    // Rising towards the east: the normal leans west, by the slope.
    {
        const MapTerrainTile ramp = globalSampleTile(zoom, x, y, [](double gx, double) { return 4.0 * gx; });
        const MapTerrainMeshResult result = buildTerrainMeshResult(normalTestRequest(ramp));
        const MapTerrainMeshVertex &vertex = result.vertices.at(32 * (cells + 1) + 32);
        const double lon = GeoWebMercator::tileXToLon(double(x) + 0.5, zoom);
        const double lat = GeoWebMercator::tileYToLat(double(y) + 0.5, zoom);
        const GeoWgs84Ellipsoid::LocalFrame frame = GeoWgs84Ellipsoid::localFrameAtGeodetic(lon, lat, 0.0);
        const QVector3D normal = unpackedNormal(vertex.normal).normalized();
        // 4 m per sample over a sample spacing of the cell length.
        const double cell_m = 2.0 * M_PI * GeoWgs84Ellipsoid::EquatorialRadiusM
            * std::cos(qDegreesToRadians(lat)) / std::ldexp(double(cells), zoom);
        const double expected_east = -(4.0 / cell_m) / std::sqrt(1.0 + (4.0 / cell_m) * (4.0 / cell_m));
        expectNear(QVector3D::dotProduct(normal, frame.east), expected_east, 0.02,
            "a DEM rising eastwards leans the normal west by its slope");
        expectNear(QVector3D::dotProduct(normal, frame.north), 0.0, 0.01,
            "a DEM rising eastwards does not lean the normal north or south");
    }

    // Two tiles side by side: the shared edge's normals agree when each
    // carries the other; without, the missing neighbors are reported.
    const std::function<double(double, double)> hills = [](double gx, double gy)
    {
        return 800.0 + 300.0 * std::sin(gx * 0.11) * std::cos(gy * 0.07) + 2.0 * gx;
    };
    const MapTerrainTile west = globalSampleTile(zoom, x, y, hills);
    const MapTerrainTile east = globalSampleTile(zoom, x + 1, y, hills);
    MapTerrainMeshRequest west_request = normalTestRequest(west);
    MapTerrainMeshRequest east_request = normalTestRequest(east);
    const MapTerrainMeshResult west_alone = buildTerrainMeshResult(west_request);
    expectTrue(west_alone.missing_neighbor_mask == 0xf,
        "a mesh built without its four neighbor DEM tiles reports all of them");
    west_request.terrain_neighbors[MapTerrainMeshNeighborEast] = east;
    east_request.terrain_neighbors[MapTerrainMeshNeighborWest] = west;
    const MapTerrainMeshResult west_result = buildTerrainMeshResult(west_request);
    const MapTerrainMeshResult east_result = buildTerrainMeshResult(east_request);
    expectTrue(west_result.missing_neighbor_mask == (0xf & ~(1 << MapTerrainMeshNeighborEast)),
        "a carried neighbor is not reported missing");
    int edge_mismatches = 0;
    int alone_mismatches = 0;
    for (int row = 0; row <= cells; ++row)
    {
        const MapGlobePackedNormal &left = west_result.vertices.at(row * (cells + 1) + cells).normal;
        const MapGlobePackedNormal &right = east_result.vertices.at(row * (cells + 1)).normal;
        const MapGlobePackedNormal &alone = west_alone.vertices.at(row * (cells + 1) + cells).normal;
        if (std::abs(int(left.x) - int(right.x)) > 1 || std::abs(int(left.y) - int(right.y)) > 1
            || std::abs(int(left.z) - int(right.z)) > 1)
        {
            ++edge_mismatches;
        }
        if (alone.x != left.x || alone.y != left.y || alone.z != left.z)
            ++alone_mismatches;
    }
    expectTrue(edge_mismatches == 0, "normals agree across a DEM tile border (no hillshading seam)");
    expectTrue(alone_mismatches > 0, "without the neighbor the edge normals are one-sided (differ)");

    // Above the map's north edge there is no neighbor to miss.
    const MapTerrainTile top = globalSampleTile(zoom, x, 0, hills);
    const MapTerrainMeshResult top_result = buildTerrainMeshResult(normalTestRequest(top));
    expectTrue((top_result.missing_neighbor_mask & (1 << MapTerrainMeshNeighborNorth)) == 0,
        "no northern neighbor is missing at the map's top edge");
}

// Stand-in DEM while a tile's own DEM is loading
// (MapGlobeSurfacePreparation::scheduleAncestorTerrainStandIns()): the matching
// part of a cached coarser tile, resampled at the tile's own address.
void testTerrainStandInFromAncestor()
{
    // Ancestor at zoom 10 holding a plane over its normalised extent; bilinear
    // resampling reproduces a plane exactly.
    const std::function<double(double, double)> plane = [](double u, double v)
    {
        return 1000.0 + 400.0 * u + 200.0 * v;
    };
    MapTerrainTile ancestor;
    ancestor.address.zoom = 10;
    ancestor.address.x = 512;
    ancestor.address.y = 340;
    ancestor.dataset = QStringLiteral("copernicus-glo30");
    ancestor.nominal_resolution_m = 30.0;
    ancestor.vertical_datum = MapTerrainVerticalDatum::Egm2008;
    ancestor.elevations_m.resize(MapTerrainTileSampleCount);
    for (int row = 0; row < MapTerrainTileGridSize; ++row)
    {
        for (int column = 0; column < MapTerrainTileGridSize; ++column)
        {
            ancestor.elevations_m[row * MapTerrainTileGridSize + column] = float(plane(
                double(column) / MapTerrainTileCellCount, double(row) / MapTerrainTileCellCount));
        }
    }

    // Two levels finer: column 3, row 1 of the ancestor's 4 x 4 descendants,
    // i.e. u 0.75..1.0, v 0.25..0.5 of the ancestor.
    MapTerrainTileAddress address;
    address.zoom = 12;
    address.x = 512 * 4 + 3;
    address.y = 340 * 4 + 1;
    const MapTerrainTile stand_in = mapTerrainTileFromAncestor(ancestor, address);
    expectTrue(stand_in.address.zoom == 12 && stand_in.address.x == address.x
        && stand_in.address.y == address.y,
        "stand-in DEM has the requested address");
    expectTrue(stand_in.vertical_datum == ancestor.vertical_datum
        && stand_in.dataset == ancestor.dataset,
        "stand-in DEM keeps the ancestor's dataset and datum");
    expectTrue(stand_in.elevations_m.size() == MapTerrainTileSampleCount,
        "stand-in DEM is a full sample grid");
    if (stand_in.elevations_m.size() != MapTerrainTileSampleCount)
        return;
    expectNear(stand_in.elevations_m.at(0), plane(0.75, 0.25), 1e-3,
        "stand-in top-left sample is the ancestor at the sub-rectangle corner");
    expectNear(stand_in.elevations_m.at(MapTerrainTileSampleCount - 1), plane(1.0, 0.5), 1e-3,
        "stand-in bottom-right sample is the ancestor at the opposite corner");
    expectNear(stand_in.elevations_m.at(32 * MapTerrainTileGridSize + 32), plane(0.875, 0.375), 1e-3,
        "stand-in centre sample is the ancestor at the sub-rectangle centre");
    expectNear(stand_in.minimum_elevation_m, plane(0.75, 0.25), 1e-3,
        "stand-in minimum is the range of the resampled part");
    expectNear(stand_in.maximum_elevation_m, plane(1.0, 0.5), 1e-3,
        "stand-in maximum is the range of the resampled part");

    // A mesh from the stand-in has the grid the tile's own DEM will give it
    // (replaced in place later) and lies on the ancestor's relief.
    MapTerrainMeshRequest request;
    request.terrain_tile = stand_in;
    request.terrain_available = true;
    request.virtual_x = int(address.x) * 4;
    request.tile_x = int(address.x) * 4;
    request.y = int(address.y) * 4;
    request.imagery_zoom = 14;
    request.terrain_zoom = 12;
    const GeoWgs84Ellipsoid::EcefPositionD origin = GeoWgs84Ellipsoid::geodeticToEcefD(
        GeoWebMercator::tileXToLon(double(request.tile_x), 14),
        GeoWebMercator::tileYToLat(double(request.y), 14), 0.0);
    request.globe_render_origin_x = origin.x;
    request.globe_render_origin_y = origin.y;
    request.globe_render_origin_z = origin.z;
    const MapTerrainMeshResult result = buildTerrainMeshResult(request);
    expectTrue(result.cell_count == 16 && result.vertices.size() == mapGlobeTileVertexCount(16),
        "stand-in mesh has the final mesh's grid and skirt");
    if (result.vertices.size() != mapGlobeTileVertexCount(16))
        return;
    // The imagery tile is the top-left sixteenth of the terrain tile, whose
    // 16 x 16 cells match the stand-in's DEM resolution there.
    const std::function<double(const MapTerrainMeshVertex &)> height_m =
        [&origin](const MapTerrainMeshVertex &vertex)
    {
        GeoWgs84Ellipsoid::EcefPositionD position;
        position.x = origin.x + double(vertex.x);
        position.y = origin.y + double(vertex.y);
        position.z = origin.z + double(vertex.z);
        double longitude_deg = 0.0;
        double latitude_deg = 0.0;
        double height = 0.0;
        GeoWgs84Ellipsoid::ecefToGeodetic(position, &longitude_deg, &latitude_deg, &height);
        return height;
    };
    expectNear(height_m(result.vertices.constFirst()), plane(0.75, 0.25), 0.05,
        "stand-in mesh corner lies on the ancestor relief");
    expectNear(height_m(result.vertices.at(17 * 17 - 1)), plane(0.8125, 0.3125), 0.05,
        "stand-in mesh opposite corner lies on the ancestor relief");

    // The preparation hands the mesh thread the ancestor itself; the
    // builder resamples it there: the very same mesh.
    MapTerrainMeshRequest from_ancestor = request;
    from_ancestor.terrain_tile = ancestor;
    from_ancestor.stand_in_zoom = ancestor.address.zoom;
    const MapTerrainMeshResult built_from_ancestor = buildTerrainMeshResult(from_ancestor);
    bool same_mesh = built_from_ancestor.vertices.size() == result.vertices.size()
        && built_from_ancestor.stand_in_zoom == ancestor.address.zoom;
    for (qsizetype index = 0; same_mesh && index < result.vertices.size(); ++index)
    {
        same_mesh = built_from_ancestor.vertices.at(index).x == result.vertices.at(index).x
            && built_from_ancestor.vertices.at(index).y == result.vertices.at(index).y
            && built_from_ancestor.vertices.at(index).z == result.vertices.at(index).z;
    }
    expectTrue(same_mesh, "a request carrying the ancestor builds the same mesh as the resampled stand-in");
}
}

int main()
{
    test_harness.runCase("vertical transform", testVerticalTransform);
    test_harness.runCase("map scale unit presentation", testMapScaleUnits);
    test_harness.runCase("cached terrain footer coordinate", testCachedTerrainAltitudeFollowsCoordinate);
    test_harness.runCase("map HUD length presentation", testMapHudLengthUnits);
    test_harness.runCase("render cache math", testRenderCacheMath);
    test_harness.runCase("node declutter", testNodeDeclutter);
    test_harness.runCase("ray intersections", testRayIntersections);
    test_harness.runCase("canonical screen ray", testCanonicalScreenRay);
    test_harness.runCase("heatmap scene", testHeatmapScene);
    test_harness.runCase("globe model geometry", testGlobeModelGeometry);
    test_harness.runCase("terrain mesh generation", testTerrainMeshGeneration);
    test_harness.runCase("terrain mesh follows DEM samples", testTerrainMeshFollowsDemSamples);
    test_harness.runCase("shared tile topology", testSharedTileTopology);
    test_harness.runCase("terrain normals", testTerrainNormals);
    test_harness.runCase("quadtree foreshortening", testQuadtreeForeshortening);
    test_harness.runCase("terrain dataset catalog", testTerrainDatasetCatalog);
    test_harness.runCase("terrain levels", testTerrainLevels);
    test_harness.runCase("terrain stand-in from ancestor", testTerrainStandInFromAncestor);
    return test_harness.finish();
}
