#include "map/render/map_globe_picking.h"
#include "map/render/map_globe_vertical_transform.h"
#include "map/render/map_node_declutter.h"
#include "map/render/map_render_cache_math.h"

#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QVector>
#include <QVector3D>

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
}

int main()
{
    testVerticalTransform();
    testRenderCacheMath();
    testNodeDeclutter();
    testRayIntersections();
    testCanonicalScreenRay();

    if (failure_count != 0)
    {
        std::fprintf(stderr, "%d neutral rendering test(s) failed.\n", failure_count);
        return 1;
    }

    std::printf("All neutral rendering tests passed.\n");
    return 0;
}
