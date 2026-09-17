#include "map/render/map_globe_surface_scene.h"

#include "config/gui_configuration.h"
#include "geo/geo_web_mercator.h"
#include "map/core/map_model.h"
#include "map/data/map_terrain_repository.h"
#include "map/data/map_terrain_tile.h"
#include "map/render/map_globe_vertical_transform.h"

#include <QHash>
#include <QString>
#include <QVector3D>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
constexpr int GlobeQuadtreeRootZoom = 0;
constexpr double GlobeQuadtreeSubdivideScreenPx = 512.0;
constexpr double GlobeQuadtreeMergeScreenPx = 256.0;
constexpr int GlobeQuadtreeMaxVisitedNodes = 20000;
constexpr double GlobeQuadtreeViewConeMarginRad = 0.5;
constexpr double GlobeQuadtreeHorizonOcclusionMarginFactor = 3.0;
constexpr int GlobeQuadtreeViewConeCullMinZoom = 2;
constexpr int GlobeTerrainMinimumLodCellCount = 1;

int terrainZoomForImageryZoom(int imagery_zoom)
{
    const int configured_max_detail_zoom = qMax(
        MapGlobeSurfaceScene::TerrainReliefMinimumZoom,
        guiConfiguration().map_performance.terrain_max_detail_zoom);
    return qBound(
        MapGlobeSurfaceScene::TerrainReliefMinimumZoom,
        imagery_zoom,
        configured_max_detail_zoom);
}

QString terrainDatasetId()
{
    return QStringLiteral("copernicus-glo30");
}

bool terrainDatumUsable(MapTerrainVerticalDatum datum)
{
    return datum == MapTerrainVerticalDatum::Wgs84Ellipsoid
        || datum == MapTerrainVerticalDatum::Egm96
        || datum == MapTerrainVerticalDatum::Egm2008;
}

void quadtreeNodeBoundingSphere(
    int zoom,
    int tile_x,
    int tile_y,
    double reference_elevation_m,
    QVector3D *center,
    double *radius_m)
{
    if (center == nullptr || radius_m == nullptr)
        return;

    if (zoom <= 1)
    {
        *center = QVector3D(0.0f, 0.0f, 0.0f);
        *radius_m = GeoWgs84Ellipsoid::EquatorialRadiusM
            + qMax(0.0, reference_elevation_m);
        return;
    }

    const double lon0 = GeoWebMercator::tileXToLon(double(tile_x), zoom);
    const double lon1 = GeoWebMercator::tileXToLon(double(tile_x + 1), zoom);
    const double lat0 = GeoWebMercator::tileYToLat(double(tile_y), zoom);
    const double lat1 = GeoWebMercator::tileYToLat(double(tile_y + 1), zoom);
    const double lon_mid = 0.5 * (lon0 + lon1);
    const double lat_mid = 0.5 * (lat0 + lat1);

    const QVector3D samples[5] = {
        GeoWgs84Ellipsoid::geodeticToEcef(lon0, lat0, reference_elevation_m),
        GeoWgs84Ellipsoid::geodeticToEcef(lon1, lat0, reference_elevation_m),
        GeoWgs84Ellipsoid::geodeticToEcef(lon0, lat1, reference_elevation_m),
        GeoWgs84Ellipsoid::geodeticToEcef(lon1, lat1, reference_elevation_m),
        GeoWgs84Ellipsoid::geodeticToEcef(lon_mid, lat_mid, reference_elevation_m),
    };

    constexpr int SampleCount = int(sizeof(samples) / sizeof(samples[0]));
    QVector3D centroid(0.0f, 0.0f, 0.0f);
    for (const QVector3D &sample : samples)
        centroid += sample;
    centroid /= float(SampleCount);

    double max_distance = 0.0;
    for (const QVector3D &sample : samples)
        max_distance = qMax(max_distance, double((sample - centroid).length()));

    *center = centroid;
    *radius_m = max_distance;
}

void quadtreeNodeVisibilityBoundingSphere(
    int zoom,
    int tile_x,
    int tile_y,
    const MapTerrainRepository *terrain_repository,
    double vertical_exaggeration,
    QVector3D *center,
    double *radius_m)
{
    const MapGlobeVerticalTransform vertical_transform(vertical_exaggeration);
    double minimum_elevation_m = 0.0;
    double maximum_elevation_m = 0.0;

    if (terrain_repository != nullptr
        && zoom >= MapGlobeSurfaceScene::TerrainReliefMinimumZoom)
    {
        const int terrain_zoom = terrainZoomForImageryZoom(zoom);
        const int zoom_delta = zoom - terrain_zoom;
        const quint32 terrain_x = quint32(tile_x) >> zoom_delta;
        const quint32 terrain_y = quint32(tile_y) >> zoom_delta;
        const MapTerrainTile *terrain_tile = terrain_repository->tile(
            terrainDatasetId(), terrain_zoom, terrain_x, terrain_y);
        if (terrain_tile != nullptr
            && terrainDatumUsable(terrain_tile->vertical_datum)
            && std::isfinite(terrain_tile->minimum_elevation_m)
            && std::isfinite(terrain_tile->maximum_elevation_m))
        {
            const double first_elevation_m = vertical_transform.terrainHeightM(
                terrain_tile->minimum_elevation_m);
            const double second_elevation_m = vertical_transform.terrainHeightM(
                terrain_tile->maximum_elevation_m);
            minimum_elevation_m = qMin(
                0.0, qMin(first_elevation_m, second_elevation_m));
            maximum_elevation_m = qMax(
                0.0, qMax(first_elevation_m, second_elevation_m));
        }
    }

    const double reference_elevation_m =
        0.5 * (minimum_elevation_m + maximum_elevation_m);
    quadtreeNodeBoundingSphere(
        zoom, tile_x, tile_y, reference_elevation_m, center, radius_m);
    *radius_m += 0.5 * (maximum_elevation_m - minimum_elevation_m);
}

bool quadtreeNodeOccludedByHorizon(
    const QVector3D &node_center,
    double node_radius_m,
    const QVector3D &eye)
{
    const QVector3D to_node = node_center - eye;
    const double distance_to_node = double(to_node.length());
    if (distance_to_node <= qMax(1.0, node_radius_m))
        return false;

    QVector3D direction = to_node;
    direction.normalize();
    QVector3D intersection;
    if (!GeoWgs84Ellipsoid::rayIntersection(eye, direction, &intersection))
        return false;

    const double distance_to_surface = double((intersection - eye).length());
    return distance_to_surface
        < distance_to_node
            - qMax(1.0, node_radius_m * GlobeQuadtreeHorizonOcclusionMarginFactor);
}

bool quadtreeNodeInViewCone(
    const QVector3D &node_center,
    double node_radius_m,
    const GeoWgs84Ellipsoid::OrbitCameraBasis &camera_basis,
    double half_fov_rad)
{
    const QVector3D to_node = node_center - camera_basis.eye;
    const double distance = double(to_node.length());
    if (distance <= 1e-6)
        return true;

    const double forward_component = double(
        QVector3D::dotProduct(to_node, camera_basis.forward));
    if (forward_component <= 0.0)
        return node_radius_m > distance;

    const double angular_radius_rad = std::atan2(node_radius_m, distance);
    const double view_angle_rad = std::acos(
        qBound(-1.0, forward_component / distance, 1.0));
    return view_angle_rad
        <= half_fov_rad + angular_radius_rad + GlobeQuadtreeViewConeMarginRad;
}

double quadtreeNodeProjectedSizePx(
    const QVector3D &node_center,
    double node_radius_m,
    const GeoWgs84Ellipsoid::OrbitCameraBasis &camera_basis,
    double viewport_height_px,
    double tan_half_fov)
{
    const double center_distance_m =
        double((node_center - camera_basis.eye).length());
    if (center_distance_m <= node_radius_m + 1e-6)
        return std::numeric_limits<double>::infinity();

    const double tangent_distance_m = std::sqrt(qMax(
        1e-12,
        center_distance_m * center_distance_m - node_radius_m * node_radius_m));
    const double projected_diameter = 2.0 * node_radius_m / tangent_distance_m;
    return projected_diameter * (viewport_height_px / (2.0 * tan_half_fov));
}

struct QuadtreeChild
{
    int x = 0;
    int y = 0;
    double distance_sq = 0.0;
};

void collectQuadtreeLeaves(
    int zoom,
    int tile_x,
    int tile_y,
    const GeoWgs84Ellipsoid::OrbitCameraBasis &visibility_camera_basis,
    const GeoWgs84Ellipsoid::OrbitCameraBasis &lod_camera_basis,
    const MapTerrainRepository *terrain_repository,
    double vertical_exaggeration,
    double viewport_height_px,
    double tan_half_fov,
    double half_fov_rad,
    const QSet<quint64> &previously_subdivided_nodes,
    QSet<quint64> *currently_subdivided_nodes,
    QVector<MapGlobeQuadtreeLeaf> *leaves,
    int *visit_budget)
{
    if (leaves == nullptr || visit_budget == nullptr
        || *visit_budget <= 0
        || leaves->size() >= MapGlobeSurfaceScene::MaximumLeafCount)
    {
        return;
    }
    --(*visit_budget);

    QVector3D visibility_node_center;
    double visibility_node_radius_m = 0.0;
    quadtreeNodeVisibilityBoundingSphere(
        zoom, tile_x, tile_y, terrain_repository, vertical_exaggeration,
        &visibility_node_center, &visibility_node_radius_m);

    if (quadtreeNodeOccludedByHorizon(
            visibility_node_center,
            visibility_node_radius_m,
            visibility_camera_basis.eye))
    {
        return;
    }
    if (zoom >= GlobeQuadtreeViewConeCullMinZoom
        && !quadtreeNodeInViewCone(
            visibility_node_center,
            visibility_node_radius_m,
            visibility_camera_basis,
            half_fov_rad))
    {
        return;
    }

    QVector3D lod_node_center;
    double lod_node_radius_m = 0.0;
    quadtreeNodeBoundingSphere(
        zoom, tile_x, tile_y, 0.0, &lod_node_center, &lod_node_radius_m);
    const double projected_size_px = quadtreeNodeProjectedSizePx(
        lod_node_center,
        lod_node_radius_m,
        lod_camera_basis,
        viewport_height_px,
        tan_half_fov);
    const bool was_subdivided = previously_subdivided_nodes.contains(
        MapGlobeSurfaceScene::positionKey(zoom, tile_x, tile_y));
    const double subdivide_threshold_px = was_subdivided
        ? GlobeQuadtreeMergeScreenPx
        : GlobeQuadtreeSubdivideScreenPx;
    const bool can_subdivide = zoom < MapModel::MaxZoom;

    if (!can_subdivide || projected_size_px <= subdivide_threshold_px)
    {
        leaves->append(MapGlobeQuadtreeLeaf{zoom, tile_x, tile_y});
        return;
    }

    if (currently_subdivided_nodes != nullptr)
    {
        currently_subdivided_nodes->insert(
            MapGlobeSurfaceScene::positionKey(zoom, tile_x, tile_y));
    }

    const int child_zoom = zoom + 1;
    const int child_tile_span = 1 << child_zoom;
    const int child_x = tile_x * 2;
    const int child_y = tile_y * 2;
    QuadtreeChild children[4];
    int child_count = 0;
    for (int dx = 0; dx < 2; ++dx)
    {
        for (int dy = 0; dy < 2; ++dy)
        {
            const int cy = child_y + dy;
            if (cy < 0 || cy >= child_tile_span)
                continue;

            QVector3D child_center;
            double child_radius_m = 0.0;
            quadtreeNodeVisibilityBoundingSphere(
                child_zoom,
                child_x + dx,
                cy,
                terrain_repository,
                vertical_exaggeration,
                &child_center,
                &child_radius_m);
            children[child_count] = QuadtreeChild{
                child_x + dx,
                cy,
                double((child_center - visibility_camera_basis.eye).lengthSquared())};
            ++child_count;
        }
    }

    std::sort(
        children,
        children + child_count,
        [](const QuadtreeChild &first, const QuadtreeChild &second)
        {
            return first.distance_sq < second.distance_sq;
        });

    for (int index = 0; index < child_count; ++index)
    {
        collectQuadtreeLeaves(
            child_zoom,
            children[index].x,
            children[index].y,
            visibility_camera_basis,
            lod_camera_basis,
            terrain_repository,
            vertical_exaggeration,
            viewport_height_px,
            tan_half_fov,
            half_fov_rad,
            previously_subdivided_nodes,
            currently_subdivided_nodes,
            leaves,
            visit_budget);
    }
}
}

QVector<MapGlobeQuadtreeLeaf> MapGlobeSurfaceScene::selectVisibleLeaves(
    const MapModel &map_model,
    const QSize &viewport_size,
    const MapTerrainRepository *terrain_repository)
{
    QVector<MapGlobeQuadtreeLeaf> leaves;
    if (!viewport_size.isValid())
        return leaves;

    const double pitch_deg = qBound(
        MapModel::MinViewGlobePitchDeg,
        map_model.viewGlobePitchDeg(),
        MapModel::MaxViewGlobePitchDeg);
    const double distance_m = qMax(
        MapModel::MinViewGlobeDistanceM,
        map_model.viewGlobeDistanceM());

    const GeoWgs84Ellipsoid::OrbitCameraBasis visibility_camera_basis =
        GeoWgs84Ellipsoid::orbitCameraBasis(
            map_model.centerLon(),
            map_model.centerLat(),
            map_model.viewGlobeYawDeg(),
            pitch_deg,
            distance_m,
            map_model.viewGlobeVerticalOffsetM(),
            map_model.viewGlobeCameraCollisionLiftM());

    const GeoWgs84Ellipsoid::OrbitCameraBasis lod_camera_basis =
        GeoWgs84Ellipsoid::orbitCameraBasis(
            map_model.centerLon(),
            map_model.centerLat(),
            map_model.viewGlobeYawDeg(),
            pitch_deg,
            distance_m);
    const double viewport_height_px = double(qMax(1, viewport_size.height()));
    const double half_fov_rad = qDegreesToRadians(
        MapModel::GlobeFieldOfViewDeg * 0.5);
    const double tan_half_fov = std::tan(half_fov_rad);

    QSet<quint64> currently_subdivided_nodes;
    int visit_budget = GlobeQuadtreeMaxVisitedNodes;
    collectQuadtreeLeaves(
        GlobeQuadtreeRootZoom,
        0,
        0,
        visibility_camera_basis,
        lod_camera_basis,
        terrain_repository,
        map_model.view3dVerticalExaggeration(),
        viewport_height_px,
        tan_half_fov,
        half_fov_rad,
        this->previously_subdivided_quadtree_nodes,
        &currently_subdivided_nodes,
        &leaves,
        &visit_budget);

    this->previously_subdivided_quadtree_nodes =
        std::move(currently_subdivided_nodes);

    if (leaves.isEmpty())
    {
        const int fallback_zoom = qBound(
            0,
            int(std::lround(MapModel::viewGlobeZoomLevelForDistanceM(
                qMax(1.0, map_model.viewGlobeDistanceM()),
                map_model.centerLat(),
                int(viewport_height_px)))),
            MapModel::MaxZoom);
        const int fallback_tile_span = 1 << fallback_zoom;
        const int fallback_x = qBound(
            0,
            int(std::floor(GeoWebMercator::lonToTileX(
                GeoWebMercator::normalizeLongitude(map_model.centerLon()),
                fallback_zoom))),
            fallback_tile_span - 1);
        const int fallback_y = qBound(
            0,
            int(std::floor(GeoWebMercator::latToTileY(
                map_model.centerLat(), fallback_zoom))),
            fallback_tile_span - 1);
        leaves.append(MapGlobeQuadtreeLeaf{
            fallback_zoom, fallback_x, fallback_y});
    }

    return leaves;
}

void MapGlobeSurfaceScene::clearVisibilityHistory()
{
    this->previously_subdivided_quadtree_nodes.clear();
}

int MapGlobeSurfaceScene::terrainCellCountForTile(
    const MapModel &map_model,
    const MapGlobeTerrainLodTile &tile,
    const QSize &viewport_size,
    const GeoWgs84Ellipsoid::OrbitCameraBasis *camera_basis_override) const
{
    if (tile.terrain_zoom < TerrainReliefMinimumZoom
        || tile.zoom < tile.terrain_zoom
        || !viewport_size.isValid())
    {
        return 1;
    }

    const int zoom_delta = tile.zoom - tile.terrain_zoom;
    const int cell_divisor = 1 << qMin(zoom_delta, 6);
    const int native_cell_count = qMax(1, MapTerrainTileCellCount / cell_divisor);
    const int maximum_cell_count = native_cell_count;
    const int minimum_cell_count = qMin(
        maximum_cell_count, GlobeTerrainMinimumLodCellCount);
    if (maximum_cell_count <= minimum_cell_count)
        return maximum_cell_count;

    const double tile_center_lon_deg = GeoWebMercator::tileXToLon(
        double(tile.virtual_x) + 0.5, tile.zoom);
    const double tile_center_lat_deg = GeoWebMercator::tileYToLat(
        double(tile.tile_y) + 0.5, tile.zoom);
    const QVector3D tile_center = GeoWgs84Ellipsoid::geodeticToEcef(
        tile_center_lon_deg, tile_center_lat_deg, 0.0);

    const double tile_lat_top_deg = GeoWebMercator::tileYToLat(
        double(tile.tile_y), tile.zoom);
    const double tile_lat_bottom_deg = GeoWebMercator::tileYToLat(
        double(tile.tile_y) + 1.0, tile.zoom);
    const double tile_width_m =
        (2.0 * M_PI * GeoWgs84Ellipsoid::EquatorialRadiusM
         * qMax(0.0, std::cos(qDegreesToRadians(tile_center_lat_deg))))
        / double(1 << tile.zoom);
    const double tile_height_m = GeoWgs84Ellipsoid::EquatorialRadiusM
        * std::abs(qDegreesToRadians(tile_lat_top_deg - tile_lat_bottom_deg));
    const double tile_reference_size_m = qMax(tile_width_m, tile_height_m);

    GeoWgs84Ellipsoid::OrbitCameraBasis local_camera_basis;
    const GeoWgs84Ellipsoid::OrbitCameraBasis *camera_basis =
        camera_basis_override;
    if (camera_basis == nullptr)
    {
        local_camera_basis = GeoWgs84Ellipsoid::orbitCameraBasis(
            map_model.centerLon(),
            map_model.centerLat(),
            map_model.viewGlobeYawDeg(),
            qBound(
                MapModel::MinViewGlobePitchDeg,
                map_model.viewGlobePitchDeg(),
                MapModel::MaxViewGlobePitchDeg),
            qMax(
                MapModel::MinViewGlobeDistanceM,
                map_model.viewGlobeDistanceM()));
        camera_basis = &local_camera_basis;
    }

    const double ground_distance_from_focus_m =
        double((tile_center - camera_basis->target).length());
    if (tile.zoom >= guiConfiguration().map_performance.terrain_full_detail_zoom
        && ground_distance_from_focus_m < tile_reference_size_m * 0.75)
    {
        return maximum_cell_count;
    }

    const double native_camera_distance_m =
        MapModel::viewGlobeDistanceMForZoomLevel(
            double(tile.zoom),
            map_model.centerLat(),
            qMax(1, viewport_size.height()));
    const double camera_to_tile_distance_m =
        double((tile_center - camera_basis->eye).length());
    const double camera_to_focus_distance_m =
        double((camera_basis->target - camera_basis->eye).length());
    const double focus_falloff_distance_m = std::hypot(
        camera_to_focus_distance_m, ground_distance_from_focus_m);
    const double lod_distance_m = qMax(
        camera_to_tile_distance_m, focus_falloff_distance_m);
    const double projected_tile_scale = qBound(
        0.0,
        native_camera_distance_m / qMax(1e-9, lod_distance_m),
        4.0);
    const double target_cell_size_px = qMax(
        1.0,
        guiConfiguration().map_performance.terrain_lod_target_cell_size_px);
    const double desired_cell_count =
        (double(MapModel::TileSize) / target_cell_size_px)
        * projected_tile_scale;

    int cell_count = minimum_cell_count;
    while (cell_count < maximum_cell_count)
    {
        const int next_cell_count = qMin(maximum_cell_count, cell_count * 2);
        const double threshold = std::sqrt(
            double(cell_count) * double(next_cell_count));
        if (desired_cell_count < threshold)
            break;
        cell_count = next_cell_count;
    }

    return cell_count;
}

bool MapGlobeSurfaceScene::terrainLodMatches(
    const MapModel &map_model,
    const QVector<MapGlobeTerrainLodTile> &tiles,
    const QSize &viewport_size) const
{
    const GeoWgs84Ellipsoid::OrbitCameraBasis camera_basis =
        GeoWgs84Ellipsoid::orbitCameraBasis(
            map_model.centerLon(),
            map_model.centerLat(),
            map_model.viewGlobeYawDeg(),
            qBound(
                MapModel::MinViewGlobePitchDeg,
                map_model.viewGlobePitchDeg(),
                MapModel::MaxViewGlobePitchDeg),
            qMax(
                MapModel::MinViewGlobeDistanceM,
                map_model.viewGlobeDistanceM()));

    for (const MapGlobeTerrainLodTile &tile : tiles)
    {
        if (!tile.terrain_available)
            continue;
        if (tile.terrain_cell_count
            != terrainCellCountForTile(
                map_model, tile, viewport_size, &camera_basis))
        {
            return false;
        }
    }
    return true;
}

void MapGlobeSurfaceScene::updateTerrainStitchCellCounts(
    QVector<MapGlobeTerrainLodTile> *tiles)
{
    if (tiles == nullptr)
        return;

    QHash<quint64, qsizetype> tiles_by_position;
    tiles_by_position.reserve(tiles->size());
    for (qsizetype index = 0; index < tiles->size(); ++index)
    {
        MapGlobeTerrainLodTile &tile = (*tiles)[index];
        tile.terrain_stitch_top_cell_count = 0;
        tile.terrain_stitch_right_cell_count = 0;
        tile.terrain_stitch_bottom_cell_count = 0;
        tile.terrain_stitch_left_cell_count = 0;

        if (!tile.terrain_available || tile.terrain_cell_count <= 0)
            continue;
        tiles_by_position.insert(
            positionKey(tile.zoom, tile.tile_x, tile.tile_y), index);
    }

    for (qsizetype index = 0; index < tiles->size(); ++index)
    {
        MapGlobeTerrainLodTile &tile = (*tiles)[index];
        if (!tile.terrain_available || tile.terrain_cell_count <= 1)
            continue;

        const int left_x = GeoWebMercator::wrapTileX(
            tile.tile_x - 1, tile.zoom);
        const int right_x = GeoWebMercator::wrapTileX(
            tile.tile_x + 1, tile.zoom);

        const quint64 neighbor_keys[4] = {
            positionKey(tile.zoom, tile.tile_x, tile.tile_y - 1),
            positionKey(tile.zoom, right_x, tile.tile_y),
            positionKey(tile.zoom, tile.tile_x, tile.tile_y + 1),
            positionKey(tile.zoom, left_x, tile.tile_y),
        };
        int *stitch_targets[4] = {
            &tile.terrain_stitch_top_cell_count,
            &tile.terrain_stitch_right_cell_count,
            &tile.terrain_stitch_bottom_cell_count,
            &tile.terrain_stitch_left_cell_count,
        };

        for (int side = 0; side < 4; ++side)
        {
            const QHash<quint64, qsizetype>::const_iterator iterator =
                tiles_by_position.constFind(neighbor_keys[side]);
            if (iterator == tiles_by_position.cend())
                continue;

            const MapGlobeTerrainLodTile &neighbor = tiles->at(iterator.value());
            if (neighbor.terrain_cell_count > 0
                && neighbor.terrain_cell_count < tile.terrain_cell_count
                && tile.terrain_cell_count % neighbor.terrain_cell_count == 0)
            {
                *stitch_targets[side] = neighbor.terrain_cell_count;
            }
        }
    }
}

quint64 MapGlobeSurfaceScene::positionKey(int zoom, int tile_x, int tile_y)
{
    return (quint64(quint32(zoom)) << 48)
        | (quint64(quint32(tile_x)) << 24)
        | quint64(quint32(tile_y));
}
