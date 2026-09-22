#include "map/render/map_globe_surface_preparation.h"

#include "config/gui_configuration.h"
#include "geo/geo_web_mercator.h"
#include "map/core/map_model.h"
#include "map/data/map_terrain_repository.h"
#include "map/data/map_terrain_tile.h"
#include "map/render/map_globe_picking.h"
#include "map/render/map_terrain_mesh_scheduler.h"

#include <QDebug>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace
{
constexpr int GlobeImageryMaxZoom = MapModel::MaxZoom;
constexpr int GlobeTerrainReliefMinimumZoom =
    MapGlobeSurfaceScene::TerrainReliefMinimumZoom;
constexpr int GlobeAsyncTerrainMeshMinimumCellCount = 16;
constexpr qint64 GlobeMinimumTerrainLodRebuildIntervalMs = 120;
constexpr int GlobePolarCapSegments = 48;

int subdivisionsForZoom(int zoom)
{
    if (zoom <= 0)
        return 32;
    if (zoom <= 1)
        return 24;
    if (zoom <= 3)
        return 12;
    if (zoom <= 6)
        return 4;
    return 2;
}

void appendIndexedGridIndices(
    QVector<quint32> *indices, qsizetype first_vertex, int cell_count)
{
    if (indices == nullptr || first_vertex < 0 || cell_count <= 0)
        return;

    const qsizetype grid_width_size = qsizetype(cell_count) + 1;
    const qsizetype last_vertex = first_vertex
        + grid_width_size * grid_width_size - 1;
    if (last_vertex > qsizetype(std::numeric_limits<quint32>::max()))
        return;

    const quint32 first = quint32(first_vertex);
    const quint32 grid_width = quint32(cell_count + 1);
    for (int row = 0; row < cell_count; ++row)
    {
        for (int column = 0; column < cell_count; ++column)
        {
            const quint32 p00 = first
                + quint32(row) * grid_width + quint32(column);
            const quint32 p10 = p00 + 1;
            const quint32 p01 = p00 + grid_width;
            const quint32 p11 = p01 + 1;
            indices->append(p00);
            indices->append(p01);
            indices->append(p10);
            indices->append(p10);
            indices->append(p01);
            indices->append(p11);
        }
    }
}

int terrainZoomForImageryZoom(int imagery_zoom)
{
    const int configured_max_detail_zoom = qMax(
        GlobeTerrainReliefMinimumZoom,
        guiConfiguration().map_performance.terrain_max_detail_zoom);
    return qBound(
        GlobeTerrainReliefMinimumZoom, imagery_zoom, configured_max_detail_zoom);
}

bool terrainDatumIsOrthometric(MapTerrainVerticalDatum datum)
{
    return datum == MapTerrainVerticalDatum::Egm96
        || datum == MapTerrainVerticalDatum::Egm2008;
}

bool rayAabbIntersectionDistanceRange(
    const QVector3D &ray_origin, const QVector3D &ray_direction,
    const QVector3D &bounds_min, const QVector3D &bounds_max,
    double *entry_distance_m, double *exit_distance_m)
{
    if (entry_distance_m == nullptr || exit_distance_m == nullptr)
        return false;

    double entry = 0.0;
    double exit = std::numeric_limits<double>::infinity();

    for (int axis = 0; axis < 3; ++axis)
    {
        const double origin = double(ray_origin[axis]);
        const double direction = double(ray_direction[axis]);
        const double minimum = double(bounds_min[axis]);
        const double maximum = double(bounds_max[axis]);

        if (std::abs(direction) <= 1e-12)
        {
            if (origin < minimum || origin > maximum)
                return false;
            continue;
        }

        double near_distance = (minimum - origin) / direction;
        double far_distance = (maximum - origin) / direction;
        if (near_distance > far_distance)
            std::swap(near_distance, far_distance);

        entry = qMax(entry, near_distance);
        exit = qMin(exit, far_distance);
        if (exit < entry)
            return false;
    }

    if (!(exit > 0.0) || !std::isfinite(entry))
        return false;

    *entry_distance_m = entry;
    *exit_distance_m = exit;
    return true;
}
}

int mapGlobeSurfaceTileRequestPriority(
    int tile_x, int tile_y, int zoom,
    double center_lon_deg, double center_lat_deg)
{
    const double tile_lon_deg = GeoWebMercator::tileXToLon(
        double(tile_x) + 0.5, zoom);
    const double tile_lat_deg = GeoWebMercator::tileYToLat(
        double(tile_y) + 0.5, zoom);

    const double center_lat_rad = qDegreesToRadians(center_lat_deg);
    const double tile_lat_rad = qDegreesToRadians(tile_lat_deg);
    const double lon_delta_rad = qDegreesToRadians(
        GeoWebMercator::normalizeLongitude(tile_lon_deg - center_lon_deg));
    const double cosine_angle = qBound(
        -1.0,
        std::sin(center_lat_rad) * std::sin(tile_lat_rad)
            + std::cos(center_lat_rad) * std::cos(tile_lat_rad)
                * std::cos(lon_delta_rad),
        1.0);

    return int(std::lround((1.0 - cosine_angle) * 1000000.0));
}

QString mapGlobeTerrainDatasetId()
{
    return QStringLiteral("copernicus-glo30");
}

bool mapGlobeTerrainDatumUsable(MapTerrainVerticalDatum datum)
{
    return datum == MapTerrainVerticalDatum::Wgs84Ellipsoid
        || datum == MapTerrainVerticalDatum::Egm96
        || datum == MapTerrainVerticalDatum::Egm2008;
}

MapGlobeSurfacePreparation::MapGlobeSurfacePreparation(MapModel *map_model)
    : map_model(map_model),
      terrain_mesh_scheduler(std::make_unique<MapTerrainMeshScheduler>())
{
}

MapGlobeSurfacePreparation::~MapGlobeSurfacePreparation() = default;

bool MapGlobeSurfacePreparation::setTerrainRepository(
    MapTerrainRepository *terrain_repository)
{
    if (this->terrain_repository == terrain_repository)
        return false;

    this->terrain_repository = terrain_repository;
    this->reported_orthometric_datum_warning = false;
    this->reported_unusable_datum_warning = false;
    this->terrain_lod_rebuild_pending = false;
    this->terrain_lod_rebuild_clock.invalidate();
    this->prepared_view_state_valid = false;
    this->window_dirty = true;
    return true;
}

MapTerrainRepository *MapGlobeSurfacePreparation::terrainRepository() const
{
    return this->terrain_repository;
}

bool MapGlobeSurfacePreparation::requestTerrainForCurrentView(
    const QSize &viewport_size)
{
    if (this->map_model == nullptr || this->terrain_repository == nullptr
        || !viewport_size.isValid())
    {
        return false;
    }

    const QVector<MapGlobeQuadtreeLeaf> desired_leaves =
        this->surface_scene.selectVisibleLeaves(
            *this->map_model, viewport_size, this->terrain_repository);
    this->rebuildWindow(desired_leaves, viewport_size);
    this->requestMissingTerrainTiles();
    this->rememberViewSelection(viewport_size);
    return true;
}

void MapGlobeSurfacePreparation::invalidateTerrainView()
{
    this->window_dirty = true;
    this->terrain_lod_rebuild_pending = false;
    this->terrain_lod_rebuild_clock.invalidate();
    this->prepared_view_state_valid = false;
    this->surface_scene.clearVisibilityHistory();
}

bool MapGlobeSurfacePreparation::setRenderOriginEcef(
    const GeoWgs84Ellipsoid::EcefPositionD &origin_ecef)
{
    if (this->render_origin_ecef.x == origin_ecef.x
        && this->render_origin_ecef.y == origin_ecef.y
        && this->render_origin_ecef.z == origin_ecef.z)
    {
        return false;
    }

    this->render_origin_ecef = origin_ecef;
    for (MapGlobeSurfaceTile &tile : this->window_tiles)
        tile.terrain_mesh_request_id = 0;

    this->window_dirty = true;
    this->prepared_view_state_valid = false;
    this->caps_built = false;
    this->cap_vertices.clear();
    this->cap_indices.clear();
    this->cap_tiles.clear();
    return true;
}

void MapGlobeSurfacePreparation::notifyTerrainTileAvailable(const QString &key)
{
    if (key.isEmpty())
        return;

    for (MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        if (tile.terrain_key != key)
            continue;
        tile.terrain_mesh_request_id = 0;
        tile.terrain_mesh_applied = false;
    }
}

void MapGlobeSurfacePreparation::invalidateTerrain()
{
    for (MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        if (tile.terrain_key.isEmpty())
            continue;
        tile.terrain_mesh_request_id = 0;
        tile.terrain_mesh_applied = false;
    }
}

bool MapGlobeSurfacePreparation::setWireframeVisible(bool visible)
{
    if (this->wireframe_visible == visible)
        return false;

    this->wireframe_visible = visible;
    if (visible)
        this->rebuildWireframeVertices();
    return true;
}

bool MapGlobeSurfacePreparation::wireframeVisible() const
{
    return this->wireframe_visible;
}

MapGlobeSurfaceVertex MapGlobeSurfacePreparation::makeTileVertex(
    double lon_deg, double lat_deg, float u, float v) const
{
    const GeoWgs84Ellipsoid::EcefPositionD position =
        GeoWgs84Ellipsoid::geodeticToEcefD(lon_deg, lat_deg, 0.0);
    MapGlobeSurfaceVertex vertex;
    vertex.x = float(position.x - this->render_origin_ecef.x);
    vertex.y = float(position.y - this->render_origin_ecef.y);
    vertex.z = float(position.z - this->render_origin_ecef.z);
    vertex.u = u;
    vertex.v = v;
    return vertex;
}

void MapGlobeSurfacePreparation::buildPolarCap(bool north)
{
    const double ring_lat = north
        ? GeoWebMercator::MaximumLatitude
        : -GeoWebMercator::MaximumLatitude;
    const double pole_lat = north ? 90.0 : -90.0;

    MapGlobeSurfaceTile cap;
    cap.surface_tile_index = this->cap_tiles.size();
    cap.is_cap = true;
    cap.first_vertex = this->cap_vertices.size();
    cap.first_index = this->cap_indices.size();

    const MapGlobeSurfaceVertex pole_vertex =
        this->makeTileVertex(0.0, pole_lat, 0.5f, 0.5f);
    this->cap_vertices.append(pole_vertex);
    for (int segment = 0; segment <= GlobePolarCapSegments; ++segment)
    {
        const double longitude_deg = -180.0
            + 360.0 * double(segment) / double(GlobePolarCapSegments);
        this->cap_vertices.append(
            this->makeTileVertex(longitude_deg, ring_lat, 0.5f, 0.5f));
    }

    const quint32 pole_index = quint32(cap.first_vertex);
    for (int segment = 0; segment < GlobePolarCapSegments; ++segment)
    {
        const quint32 ring0 = pole_index + 1 + quint32(segment);
        const quint32 ring1 = ring0 + 1;
        this->cap_indices.append(pole_index);
        if (north)
        {
            this->cap_indices.append(ring0);
            this->cap_indices.append(ring1);
        }
        else
        {
            this->cap_indices.append(ring1);
            this->cap_indices.append(ring0);
        }
    }

    cap.vertex_count = this->cap_vertices.size() - cap.first_vertex;
    cap.index_count = this->cap_indices.size() - cap.first_index;
    this->cap_tiles.append(cap);
}

bool MapGlobeSurfacePreparation::ensureCapsBuilt()
{
    if (this->caps_built)
        return false;

    this->cap_vertices.clear();
    this->cap_indices.clear();
    this->cap_tiles.clear();
    this->buildPolarCap(true);
    this->buildPolarCap(false);
    this->caps_built = true;
    if (this->wireframe_visible)
        this->rebuildWireframeVertices();
    return true;
}

int MapGlobeSurfacePreparation::terrainCellCountForTile(
    const MapGlobeSurfaceTile &tile,
    const QSize &viewport_size,
    const GeoWgs84Ellipsoid::OrbitCameraBasis *camera_basis_override) const
{
    if (this->map_model == nullptr)
        return 1;

    MapGlobeTerrainLodTile surface_tile;
    surface_tile.virtual_x = tile.virtual_x;
    surface_tile.tile_x = tile.tile_x;
    surface_tile.tile_y = tile.tile_y;
    surface_tile.zoom = tile.zoom;
    surface_tile.terrain_zoom = tile.terrain_zoom;
    surface_tile.terrain_available = !tile.terrain_key.isEmpty();
    surface_tile.terrain_cell_count = tile.terrain_cell_count;
    return this->surface_scene.terrainCellCountForTile(
        *this->map_model, surface_tile, viewport_size, camera_basis_override);
}

void MapGlobeSurfacePreparation::updateTerrainStitchCellCounts(
    QVector<MapGlobeSurfaceTile> *tiles) const
{
    if (tiles == nullptr)
        return;

    QVector<MapGlobeTerrainLodTile> surface_tiles;
    surface_tiles.reserve(tiles->size());
    for (const MapGlobeSurfaceTile &tile : *tiles)
    {
        MapGlobeTerrainLodTile surface_tile;
        surface_tile.virtual_x = tile.virtual_x;
        surface_tile.tile_x = tile.tile_x;
        surface_tile.tile_y = tile.tile_y;
        surface_tile.zoom = tile.zoom;
        surface_tile.terrain_zoom = tile.terrain_zoom;
        surface_tile.terrain_available = !tile.terrain_key.isEmpty();
        surface_tile.terrain_cell_count = tile.terrain_cell_count;
        surface_tiles.append(surface_tile);
    }

    MapGlobeSurfaceScene::updateTerrainStitchCellCounts(&surface_tiles);
    for (qsizetype index = 0; index < tiles->size(); ++index)
    {
        MapGlobeSurfaceTile &tile = (*tiles)[index];
        const MapGlobeTerrainLodTile &surface_tile = surface_tiles.at(index);
        tile.terrain_stitch_top_cell_count =
            surface_tile.terrain_stitch_top_cell_count;
        tile.terrain_stitch_right_cell_count =
            surface_tile.terrain_stitch_right_cell_count;
        tile.terrain_stitch_bottom_cell_count =
            surface_tile.terrain_stitch_bottom_cell_count;
        tile.terrain_stitch_left_cell_count =
            surface_tile.terrain_stitch_left_cell_count;
    }
}

bool MapGlobeSurfacePreparation::currentTerrainLodMatches(
    const QSize &viewport_size) const
{
    if (this->map_model == nullptr)
        return true;

    QVector<MapGlobeTerrainLodTile> surface_tiles;
    surface_tiles.reserve(this->window_tiles.size());
    for (const MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        MapGlobeTerrainLodTile surface_tile;
        surface_tile.virtual_x = tile.virtual_x;
        surface_tile.tile_x = tile.tile_x;
        surface_tile.tile_y = tile.tile_y;
        surface_tile.zoom = tile.zoom;
        surface_tile.terrain_zoom = tile.terrain_zoom;
        surface_tile.terrain_available = !tile.terrain_key.isEmpty();
        surface_tile.terrain_cell_count = tile.terrain_cell_count;
        surface_tiles.append(surface_tile);
    }

    return this->surface_scene.terrainLodMatches(
        *this->map_model, surface_tiles, viewport_size);
}

void MapGlobeSurfacePreparation::updateTerrainRayBounds(
    MapGlobeSurfaceTile *tile)
{
    if (tile == nullptr)
        return;

    tile->terrain_ray_bounds_valid = false;
    tile->terrain_ray_row_bounds.clear();
    if (!tile->terrain_mesh_has_relief
        || tile->first_vertex < 0 || tile->vertex_count <= 0)
    {
        return;
    }

    const qsizetype vertex_begin = qsizetype(tile->first_vertex);
    const qsizetype vertex_end = vertex_begin + qsizetype(tile->vertex_count);
    if (vertex_begin < 0 || vertex_end > this->window_vertices.size())
        return;

    const MapGlobeSurfaceVertex &first_vertex =
        this->window_vertices.at(vertex_begin);
    QVector3D bounds_min(first_vertex.x, first_vertex.y, first_vertex.z);
    QVector3D bounds_max = bounds_min;

    for (qsizetype index = vertex_begin + 1; index < vertex_end; ++index)
    {
        const MapGlobeSurfaceVertex &vertex = this->window_vertices.at(index);
        bounds_min.setX(qMin(bounds_min.x(), vertex.x));
        bounds_min.setY(qMin(bounds_min.y(), vertex.y));
        bounds_min.setZ(qMin(bounds_min.z(), vertex.z));
        bounds_max.setX(qMax(bounds_max.x(), vertex.x));
        bounds_max.setY(qMax(bounds_max.y(), vertex.y));
        bounds_max.setZ(qMax(bounds_max.z(), vertex.z));
    }

    tile->terrain_ray_bounds_min = bounds_min;
    tile->terrain_ray_bounds_max = bounds_max;
    tile->terrain_ray_bounds_valid = true;

    const int cell_count = qMax(1, tile->terrain_cell_count);
    const qsizetype grid_width = qsizetype(cell_count) + 1;
    const qsizetype expected_vertex_count = grid_width * grid_width;
    if (qsizetype(tile->vertex_count) != expected_vertex_count)
        return;

    tile->terrain_ray_row_bounds.reserve(cell_count);
    for (int row = 0; row < cell_count; ++row)
    {
        const qsizetype row_begin = vertex_begin
            + qsizetype(row) * grid_width;
        const qsizetype row_end = row_begin + grid_width * 2;
        if (row_begin < vertex_begin || row_end > vertex_end)
        {
            tile->terrain_ray_row_bounds.clear();
            return;
        }

        const MapGlobeSurfaceVertex &row_first =
            this->window_vertices.at(row_begin);
        QVector3D row_min(row_first.x, row_first.y, row_first.z);
        QVector3D row_max = row_min;
        for (qsizetype index = row_begin + 1; index < row_end; ++index)
        {
            const MapGlobeSurfaceVertex &vertex =
                this->window_vertices.at(index);
            row_min.setX(qMin(row_min.x(), vertex.x));
            row_min.setY(qMin(row_min.y(), vertex.y));
            row_min.setZ(qMin(row_min.z(), vertex.z));
            row_max.setX(qMax(row_max.x(), vertex.x));
            row_max.setY(qMax(row_max.y(), vertex.y));
            row_max.setZ(qMax(row_max.z(), vertex.z));
        }

        MapGlobeSurfaceTerrainRayRowBounds row_bounds;
        row_bounds.minimum = row_min;
        row_bounds.maximum = row_max;
        tile->terrain_ray_row_bounds.append(row_bounds);
    }
}

void MapGlobeSurfacePreparation::rebuildWindow(
    const QVector<MapGlobeQuadtreeLeaf> &leaves,
    const QSize &viewport_size)
{
    const bool geometry_reuse_allowed = !this->window_dirty;
    QVector<MapGlobeSurfaceVertex> previous_vertices =
        std::move(this->window_vertices);
    QVector<MapGlobeSurfaceTile> previous_tiles =
        std::move(this->window_tiles);
    QHash<quint64, qsizetype> previous_tiles_by_position;
    if (geometry_reuse_allowed)
    {
        previous_tiles_by_position.reserve(previous_tiles.size());
        for (qsizetype index = 0; index < previous_tiles.size(); ++index)
        {
            const MapGlobeSurfaceTile &tile = previous_tiles.at(index);
            previous_tiles_by_position.insert(
                MapGlobeSurfaceScene::positionKey(
                    tile.zoom, tile.tile_x, tile.tile_y),
                index);
        }
    }

    QVector<MapGlobeSurfaceTile> next_tiles;
    next_tiles.reserve(leaves.size());
    QSet<quint64> seen_positions;
    seen_positions.reserve(leaves.size());

    GeoWgs84Ellipsoid::OrbitCameraBasis terrain_camera_basis;
    const GeoWgs84Ellipsoid::OrbitCameraBasis *terrain_camera_basis_ptr = nullptr;
    if (this->map_model != nullptr && this->terrain_repository != nullptr
        && viewport_size.isValid())
    {
        terrain_camera_basis = GeoWgs84Ellipsoid::orbitCameraBasis(
            this->map_model->centerLon(), this->map_model->centerLat(),
            this->map_model->viewGlobeYawDeg(),
            qBound(
                MapModel::MinViewGlobePitchDeg,
                this->map_model->viewGlobePitchDeg(),
                MapModel::MaxViewGlobePitchDeg),
            qMax(
                MapModel::MinViewGlobeDistanceM,
                this->map_model->viewGlobeDistanceM()));
        terrain_camera_basis_ptr = &terrain_camera_basis;
    }

    for (const MapGlobeQuadtreeLeaf &leaf : leaves)
    {
        const quint64 position_key = MapGlobeSurfaceScene::positionKey(
            leaf.zoom, leaf.tile_x, leaf.tile_y);
        if (seen_positions.contains(position_key))
            continue;
        seen_positions.insert(position_key);

        const bool terrain_enabled =
            this->terrain_repository != nullptr
            && leaf.zoom >= GlobeTerrainReliefMinimumZoom;
        const int terrain_zoom = terrain_enabled
            ? terrainZoomForImageryZoom(leaf.zoom)
            : -1;

        MapGlobeSurfaceTile tile;
        tile.virtual_x = leaf.tile_x;
        tile.tile_x = leaf.tile_x;
        tile.tile_y = leaf.tile_y;
        tile.zoom = leaf.zoom;
        if (this->map_model != nullptr)
        {
            tile.imagery_key = this->map_model->tileCacheKeyAtZoom(
                leaf.tile_x, leaf.tile_y, leaf.zoom);
        }

        if (terrain_enabled)
        {
            const int zoom_delta = leaf.zoom - terrain_zoom;
            MapTerrainTileAddress terrain_address;
            terrain_address.zoom = terrain_zoom;
            terrain_address.x = quint32(leaf.tile_x) >> zoom_delta;
            terrain_address.y = quint32(leaf.tile_y) >> zoom_delta;
            tile.terrain_zoom = terrain_zoom;
            tile.terrain_key = mapTerrainTileKey(
                mapGlobeTerrainDatasetId(), terrain_address);
            tile.terrain_cell_count = this->terrainCellCountForTile(
                tile, viewport_size, terrain_camera_basis_ptr);
        }

        next_tiles.append(tile);
    }

    this->window_position_keys = std::move(seen_positions);
    this->updateTerrainStitchCellCounts(&next_tiles);

    qsizetype estimated_vertex_count = 0;
    qsizetype estimated_index_count = 0;
    for (const MapGlobeSurfaceTile &tile : next_tiles)
    {
        const int subdivisions = !tile.terrain_key.isEmpty()
            ? qMax(1, tile.terrain_cell_count)
            : subdivisionsForZoom(tile.zoom);
        const qsizetype grid_width = qsizetype(subdivisions) + 1;
        estimated_vertex_count += grid_width * grid_width;
        estimated_index_count +=
            qsizetype(subdivisions) * qsizetype(subdivisions) * 6;
    }

    this->window_vertices.clear();
    this->window_vertices.reserve(estimated_vertex_count);
    this->window_indices.clear();
    this->window_indices.reserve(estimated_index_count);
    this->window_tiles.clear();
    this->window_tiles.reserve(next_tiles.size());
    this->window_tile_indices_by_position.clear();
    this->window_tile_indices_by_position.reserve(next_tiles.size());

    for (MapGlobeSurfaceTile &tile : next_tiles)
    {
        const bool terrain_enabled = !tile.terrain_key.isEmpty();
        tile.surface_tile_index = this->window_tiles.size();
        tile.first_vertex = this->window_vertices.size();
        tile.first_index = this->window_indices.size();
        const int subdivisions = terrain_enabled
            ? qMax(1, tile.terrain_cell_count)
            : subdivisionsForZoom(tile.zoom);
        const int grid_width = subdivisions + 1;
        const int expected_vertex_count = grid_width * grid_width;
        const int expected_index_count = subdivisions * subdivisions * 6;

        bool reused = false;
        const QHash<quint64, qsizetype>::const_iterator previous_iterator =
            previous_tiles_by_position.constFind(
                MapGlobeSurfaceScene::positionKey(
                    tile.zoom, tile.tile_x, tile.tile_y));
        if (geometry_reuse_allowed
            && previous_iterator != previous_tiles_by_position.cend())
        {
            const MapGlobeSurfaceTile &previous_tile =
                previous_tiles.at(previous_iterator.value());
            const bool same_geometry =
                previous_tile.zoom == tile.zoom
                && previous_tile.imagery_key == tile.imagery_key
                && previous_tile.terrain_key == tile.terrain_key
                && previous_tile.terrain_cell_count == tile.terrain_cell_count
                && previous_tile.terrain_stitch_top_cell_count
                    == tile.terrain_stitch_top_cell_count
                && previous_tile.terrain_stitch_right_cell_count
                    == tile.terrain_stitch_right_cell_count
                && previous_tile.terrain_stitch_bottom_cell_count
                    == tile.terrain_stitch_bottom_cell_count
                && previous_tile.terrain_stitch_left_cell_count
                    == tile.terrain_stitch_left_cell_count
                && previous_tile.vertex_count == expected_vertex_count
                && previous_tile.index_count == expected_index_count
                && previous_tile.first_vertex >= 0
                && previous_tile.first_vertex + previous_tile.vertex_count
                    <= previous_vertices.size();
            if (same_geometry)
            {
                const MapGlobeSurfaceVertex *source =
                    previous_vertices.constData() + previous_tile.first_vertex;
                for (int index = 0; index < previous_tile.vertex_count; ++index)
                    this->window_vertices.append(source[index]);
                tile.vertex_count = previous_tile.vertex_count;
                tile.terrain_mesh_request_id =
                    previous_tile.terrain_mesh_request_id;
                tile.terrain_mesh_applied =
                    previous_tile.terrain_mesh_applied;
                tile.terrain_mesh_has_relief =
                    previous_tile.terrain_mesh_has_relief;
                reused = true;
            }
        }

        if (!reused)
        {
            bool terrain_built = false;
            if (terrain_enabled
                && this->terrain_repository != nullptr
                && !tile.terrain_key.isEmpty()
                && tile.terrain_cell_count
                    < GlobeAsyncTerrainMeshMinimumCellCount)
            {
                const MapTerrainTile *terrain_tile =
                    this->terrain_repository->tile(tile.terrain_key);
                if (terrain_tile != nullptr
                    && terrain_tile->elevations_m.size()
                        == MapTerrainTileSampleCount
                    && mapGlobeTerrainDatumUsable(
                        terrain_tile->vertical_datum))
                {
                    if (terrainDatumIsOrthometric(
                            terrain_tile->vertical_datum)
                        && !this->reported_orthometric_datum_warning)
                    {
                        qWarning().noquote()
                            << QStringLiteral(
                                   "Globe terrain tiles use an orthometric EGM vertical datum; "
                                   "using it directly as local ellipsoid-normal displacement until "
                                   "the terrain service exposes WGS84-ellipsoid tile heights.");
                        this->reported_orthometric_datum_warning = true;
                    }

                    MapTerrainMeshRequest request;
                    request.terrain_key = tile.terrain_key;
                    request.terrain_tile = *terrain_tile;
                    request.terrain_available = true;
                    request.virtual_x = tile.virtual_x;
                    request.tile_x = tile.tile_x;
                    request.y = tile.tile_y;
                    request.imagery_zoom = tile.zoom;
                    request.terrain_zoom = tile.terrain_zoom;
                    request.requested_cell_count = tile.terrain_cell_count;
                    request.stitch_top_cell_count =
                        tile.terrain_stitch_top_cell_count;
                    request.stitch_right_cell_count =
                        tile.terrain_stitch_right_cell_count;
                    request.stitch_bottom_cell_count =
                        tile.terrain_stitch_bottom_cell_count;
                    request.stitch_left_cell_count =
                        tile.terrain_stitch_left_cell_count;
                    if (this->map_model != nullptr)
                    {
                        request.globe_vertical_exaggeration =
                            this->map_model->view3dVerticalExaggeration();
                    }
                    request.globe_render_origin_x =
                        this->render_origin_ecef.x;
                    request.globe_render_origin_y =
                        this->render_origin_ecef.y;
                    request.globe_render_origin_z =
                        this->render_origin_ecef.z;

                    const MapTerrainMeshResult result =
                        buildTerrainMeshResult(request);
                    if (result.vertices.size() == expected_vertex_count)
                    {
                        for (const MapTerrainMeshVertex &vertex : result.vertices)
                        {
                            this->window_vertices.append(
                                MapGlobeSurfaceVertex{
                                    vertex.x, vertex.y, vertex.z,
                                    vertex.u, vertex.v});
                        }
                        tile.vertex_count = expected_vertex_count;
                        tile.terrain_mesh_applied = true;
                        tile.terrain_mesh_has_relief = true;
                        terrain_built = true;
                    }
                }
            }

            if (!terrain_built)
            {
                for (int row = 0; row <= subdivisions; ++row)
                {
                    const double v =
                        double(row) / double(subdivisions);
                    const double latitude_deg = GeoWebMercator::tileYToLat(
                        double(tile.tile_y) + v, tile.zoom);

                    for (int column = 0; column <= subdivisions; ++column)
                    {
                        const double u =
                            double(column) / double(subdivisions);
                        const double longitude_deg =
                            GeoWebMercator::tileXToLon(
                                double(tile.virtual_x) + u, tile.zoom);
                        this->window_vertices.append(this->makeTileVertex(
                            longitude_deg, latitude_deg,
                            float(u), float(v)));
                    }
                }
                tile.vertex_count = expected_vertex_count;
            }
        }

        appendIndexedGridIndices(
            &this->window_indices, tile.first_vertex, subdivisions);
        tile.index_count = this->window_indices.size() - tile.first_index;
        this->updateTerrainRayBounds(&tile);

        this->window_tiles.append(tile);
        this->window_tile_indices_by_position.insert(
            MapGlobeSurfaceScene::positionKey(
                tile.zoom, tile.tile_x, tile.tile_y),
            this->window_tiles.size() - 1);
    }

    this->window_dirty = false;
    this->terrain_lod_rebuild_pending = false;
    this->terrain_lod_rebuild_clock.restart();

    if (this->wireframe_visible)
        this->rebuildWireframeVertices();
}

QVector<MapGlobeQuadtreeLeaf>
MapGlobeSurfacePreparation::currentWindowLeaves() const
{
    QVector<MapGlobeQuadtreeLeaf> leaves;
    leaves.reserve(this->window_tiles.size());
    for (const MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        leaves.append(MapGlobeQuadtreeLeaf{
            tile.zoom, tile.tile_x, tile.tile_y});
    }
    return leaves;
}

bool MapGlobeSurfacePreparation::viewSelectionMatches(
    const QSize &viewport_size) const
{
    if (!this->prepared_view_state_valid || this->map_model == nullptr)
        return false;

    return viewport_size == this->prepared_viewport_size
        && this->map_model->centerLon() == this->prepared_center_lon_deg
        && this->map_model->centerLat() == this->prepared_center_lat_deg
        && this->map_model->viewGlobeYawDeg() == this->prepared_yaw_deg
        && this->map_model->viewGlobePitchDeg() == this->prepared_pitch_deg
        && this->map_model->viewGlobeDistanceM()
            == this->prepared_distance_m;
}

void MapGlobeSurfacePreparation::rememberViewSelection(
    const QSize &viewport_size)
{
    if (this->map_model == nullptr)
        return;

    this->prepared_viewport_size = viewport_size;
    this->prepared_center_lon_deg = this->map_model->centerLon();
    this->prepared_center_lat_deg = this->map_model->centerLat();
    this->prepared_yaw_deg = this->map_model->viewGlobeYawDeg();
    this->prepared_pitch_deg = this->map_model->viewGlobePitchDeg();
    this->prepared_distance_m = this->map_model->viewGlobeDistanceM();
    this->prepared_view_state_valid = true;
}

bool MapGlobeSurfacePreparation::prepareVisibleWindow(
    const QSize &viewport_size)
{
    if (this->map_model == nullptr || !viewport_size.isValid())
        return false;

    const QVector<MapGlobeQuadtreeLeaf> desired_leaves =
        this->surface_scene.selectVisibleLeaves(
            *this->map_model, viewport_size, this->terrain_repository);

    bool leaves_match_window = !this->window_dirty
        && desired_leaves.size() == this->window_tiles.size()
        && this->window_position_keys.size() == this->window_tiles.size();
    if (leaves_match_window)
    {
        for (const MapGlobeQuadtreeLeaf &leaf : desired_leaves)
        {
            if (!this->window_position_keys.contains(
                    MapGlobeSurfaceScene::positionKey(
                        leaf.zoom, leaf.tile_x, leaf.tile_y)))
            {
                leaves_match_window = false;
                break;
            }
        }
    }

    bool geometry_rebuilt = false;
    if (!leaves_match_window)
    {
        this->rebuildWindow(desired_leaves, viewport_size);
        geometry_rebuilt = true;
    }
    else
    {
        const bool terrain_enabled = this->terrain_repository != nullptr;
        const bool terrain_view_changed =
            !this->viewSelectionMatches(viewport_size);
        const bool terrain_lod_check_needed = terrain_enabled
            && (terrain_view_changed || this->terrain_lod_rebuild_pending);

        if (!terrain_enabled)
        {
            this->terrain_lod_rebuild_pending = false;
        }
        else if (terrain_lod_check_needed
                 && this->terrain_lod_rebuild_clock.isValid()
                 && this->terrain_lod_rebuild_clock.elapsed()
                    < GlobeMinimumTerrainLodRebuildIntervalMs)
        {
            this->terrain_lod_rebuild_pending = true;
        }
        else if (terrain_lod_check_needed)
        {
            if (!this->currentTerrainLodMatches(viewport_size))
            {
                this->rebuildWindow(
                    this->currentWindowLeaves(), viewport_size);
                geometry_rebuilt = true;
            }
            else
            {
                this->terrain_lod_rebuild_pending = false;
            }
        }
    }

    this->rememberViewSelection(viewport_size);
    return geometry_rebuilt;
}

void MapGlobeSurfacePreparation::requestMissingTerrainTiles()
{
    if (this->terrain_repository == nullptr || this->window_tiles.isEmpty()
        || this->map_model == nullptr)
    {
        return;
    }

    QVector<const MapGlobeSurfaceTile *> candidates;
    candidates.reserve(this->window_tiles.size());
    for (const MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        if (tile.terrain_zoom < GlobeTerrainReliefMinimumZoom
            || tile.terrain_key.isEmpty())
        {
            continue;
        }
        candidates.append(&tile);
    }

    std::sort(
        candidates.begin(), candidates.end(),
        [this](const MapGlobeSurfaceTile *first,
               const MapGlobeSurfaceTile *second)
        {
            const int first_priority = mapGlobeSurfaceTileRequestPriority(
                first->tile_x, first->tile_y, first->zoom,
                this->map_model->centerLon(), this->map_model->centerLat());
            const int second_priority = mapGlobeSurfaceTileRequestPriority(
                second->tile_x, second->tile_y, second->zoom,
                this->map_model->centerLon(), this->map_model->centerLat());
            if (first_priority != second_priority)
                return first_priority < second_priority;
            if (first->terrain_zoom != second->terrain_zoom)
                return first->terrain_zoom > second->terrain_zoom;
            if (first->tile_y != second->tile_y)
                return first->tile_y < second->tile_y;
            return first->tile_x < second->tile_x;
        });

    QSet<QString> requested_keys;
    requested_keys.reserve(candidates.size());
    for (const MapGlobeSurfaceTile *tile : candidates)
    {
        if (tile == nullptr
            || requested_keys.contains(tile->terrain_key)
            || this->terrain_repository->tile(tile->terrain_key) != nullptr)
        {
            continue;
        }
        requested_keys.insert(tile->terrain_key);

        const int zoom_delta = tile->zoom - tile->terrain_zoom;
        const quint32 terrain_x = quint32(tile->tile_x) >> zoom_delta;
        const quint32 terrain_y = quint32(tile->tile_y) >> zoom_delta;
        this->terrain_repository->requestTile(
            mapGlobeTerrainDatasetId(),
            tile->terrain_zoom, terrain_x, terrain_y);
    }
}

void MapGlobeSurfacePreparation::scheduleReadyTerrainMeshes()
{
    if (this->terrain_repository == nullptr
        || this->terrain_mesh_scheduler == nullptr
        || this->map_model == nullptr)
    {
        return;
    }

    QVector<MapGlobeSurfaceTile *> candidates;
    candidates.reserve(this->window_tiles.size());
    for (MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        if (tile.terrain_key.isEmpty()
            || tile.terrain_mesh_applied
            || tile.terrain_mesh_request_id != 0)
        {
            continue;
        }

        if (this->terrain_repository->tile(tile.terrain_key) == nullptr)
            continue;
        candidates.append(&tile);
    }

    std::sort(
        candidates.begin(), candidates.end(),
        [this](const MapGlobeSurfaceTile *first,
               const MapGlobeSurfaceTile *second)
        {
            const int first_priority = mapGlobeSurfaceTileRequestPriority(
                first->tile_x, first->tile_y, first->zoom,
                this->map_model->centerLon(), this->map_model->centerLat());
            const int second_priority = mapGlobeSurfaceTileRequestPriority(
                second->tile_x, second->tile_y, second->zoom,
                this->map_model->centerLon(), this->map_model->centerLat());
            return first_priority < second_priority;
        });

    for (MapGlobeSurfaceTile *tile : candidates)
    {
        if (tile == nullptr)
            continue;

        const MapTerrainTile *terrain_tile =
            this->terrain_repository->tile(tile->terrain_key);
        if (terrain_tile == nullptr)
            continue;

        if (!mapGlobeTerrainDatumUsable(terrain_tile->vertical_datum))
        {
            if (!this->reported_unusable_datum_warning)
            {
                qWarning().noquote()
                    << QStringLiteral(
                           "Globe terrain is ignoring terrain tiles with an unknown/local "
                           "vertical datum because they cannot be interpreted as global height.");
                this->reported_unusable_datum_warning = true;
            }
            tile->terrain_mesh_applied = true;
            continue;
        }

        if (terrainDatumIsOrthometric(terrain_tile->vertical_datum)
            && !this->reported_orthometric_datum_warning)
        {
            qWarning().noquote()
                << QStringLiteral(
                       "Globe terrain tiles use an orthometric EGM vertical datum; "
                       "using it directly as local ellipsoid-normal displacement until "
                       "the terrain service exposes WGS84-ellipsoid tile heights.");
            this->reported_orthometric_datum_warning = true;
        }

        MapTerrainMeshRequest request;
        request.request_id = this->next_terrain_mesh_request_id++;
        request.terrain_key = tile->terrain_key;
        request.terrain_tile = *terrain_tile;
        request.terrain_available =
            terrain_tile->elevations_m.size() == MapTerrainTileSampleCount;
        request.virtual_x = tile->virtual_x;
        request.tile_x = tile->tile_x;
        request.y = tile->tile_y;
        request.imagery_zoom = tile->zoom;
        request.terrain_zoom = tile->terrain_zoom;
        request.requested_cell_count = tile->terrain_cell_count;
        request.stitch_top_cell_count =
            tile->terrain_stitch_top_cell_count;
        request.stitch_right_cell_count =
            tile->terrain_stitch_right_cell_count;
        request.stitch_bottom_cell_count =
            tile->terrain_stitch_bottom_cell_count;
        request.stitch_left_cell_count =
            tile->terrain_stitch_left_cell_count;
        request.globe_vertical_exaggeration =
            this->map_model->view3dVerticalExaggeration();
        request.globe_render_origin_x = this->render_origin_ecef.x;
        request.globe_render_origin_y = this->render_origin_ecef.y;
        request.globe_render_origin_z = this->render_origin_ecef.z;

        tile->terrain_mesh_request_id = request.request_id;
        this->terrain_mesh_scheduler->submit(request);
    }
}

bool MapGlobeSurfacePreparation::applyReadyTerrainMeshes(
    QVector<MapGlobeSurfaceVertexPatch> *vertex_patches)
{
    if (vertex_patches != nullptr)
        vertex_patches->clear();
    if (this->terrain_mesh_scheduler == nullptr)
        return false;

    QVector<MapTerrainMeshResult> results;
    this->terrain_mesh_scheduler->collectReady(&results);
    if (results.isEmpty())
        return false;

    bool geometry_changed = false;
    for (const MapTerrainMeshResult &result : results)
    {
        for (MapGlobeSurfaceTile &tile : this->window_tiles)
        {
            if (tile.terrain_mesh_request_id != result.request_id)
                continue;

            tile.terrain_mesh_request_id = 0;
            if (!result.terrain_available
                || result.cell_count != tile.terrain_cell_count
                || result.stitch_top_cell_count
                    != tile.terrain_stitch_top_cell_count
                || result.stitch_right_cell_count
                    != tile.terrain_stitch_right_cell_count
                || result.stitch_bottom_cell_count
                    != tile.terrain_stitch_bottom_cell_count
                || result.stitch_left_cell_count
                    != tile.terrain_stitch_left_cell_count
                || result.vertices.size() != tile.vertex_count)
            {
                break;
            }

            const qsizetype first_vertex = tile.first_vertex;
            if (first_vertex < 0
                || first_vertex > this->window_vertices.size()
                || result.vertices.size()
                    > this->window_vertices.size() - first_vertex)
            {
                qWarning().noquote()
                    << QStringLiteral(
                           "Ignoring stale globe terrain mesh result outside "
                           "the current vertex window: request=%1 first=%2 "
                           "count=%3 window=%4")
                           .arg(result.request_id)
                           .arg(first_vertex)
                           .arg(result.vertices.size())
                           .arg(this->window_vertices.size());
                break;
            }

            for (qsizetype index = 0; index < result.vertices.size(); ++index)
            {
                const MapTerrainMeshVertex &vertex = result.vertices.at(index);
                MapGlobeSurfaceVertex &target =
                    this->window_vertices[first_vertex + index];
                target.x = vertex.x;
                target.y = vertex.y;
                target.z = vertex.z;
                target.u = vertex.u;
                target.v = vertex.v;
            }

            tile.terrain_mesh_applied = true;
            tile.terrain_mesh_has_relief = true;
            this->updateTerrainRayBounds(&tile);
            geometry_changed = true;
            if (vertex_patches != nullptr)
            {
                vertex_patches->append(MapGlobeSurfaceVertexPatch{
                    first_vertex, result.vertices.size()});
            }
            break;
        }
    }

    if (geometry_changed && this->wireframe_visible)
        this->rebuildWireframeVertices();
    return geometry_changed;
}

bool MapGlobeSurfacePreparation::hasPendingTerrainMeshes() const
{
    if (this->terrain_lod_rebuild_pending)
        return true;

    for (const MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        if (tile.terrain_mesh_request_id != 0)
            return true;
    }
    return false;
}

void MapGlobeSurfacePreparation::terrainMeshProgress(
    int *completed, int *total, bool *active) const
{
    int completed_count = 0;
    int total_count = 0;
    bool build_active = this->terrain_lod_rebuild_pending;

    for (const MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        if (tile.terrain_key.isEmpty())
            continue;

        if (tile.terrain_mesh_applied)
        {
            ++completed_count;
            ++total_count;
        }
        else if (tile.terrain_mesh_request_id != 0)
        {
            ++total_count;
            build_active = true;
        }
    }

    if (completed != nullptr)
        *completed = completed_count;
    if (total != nullptr)
        *total = total_count;
    if (active != nullptr)
        *active = build_active;
}

bool MapGlobeSurfacePreparation::visibleTerrainRayIntersection(
    const GeoWgs84Ellipsoid::EcefPositionD &ray_origin_ecef,
    const QVector3D &ray_direction_ecef,
    GeoWgs84Ellipsoid::EcefPositionD *intersection_ecef,
    double *distance_m) const
{
    if (intersection_ecef == nullptr || distance_m == nullptr)
        return false;

    QVector3D direction = ray_direction_ecef;
    if (direction.lengthSquared() <= 1e-12f)
        return false;
    direction.normalize();

    const QVector3D relative_origin(
        float(ray_origin_ecef.x - this->render_origin_ecef.x),
        float(ray_origin_ecef.y - this->render_origin_ecef.y),
        float(ray_origin_ecef.z - this->render_origin_ecef.z));

    bool found = false;
    double nearest_distance_m = std::numeric_limits<double>::infinity();

    for (const MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        if (!tile.terrain_mesh_has_relief
            || !tile.terrain_ray_bounds_valid
            || tile.first_index < 0 || tile.index_count < 3)
        {
            continue;
        }

        double bounds_entry_distance_m = 0.0;
        double bounds_exit_distance_m = 0.0;
        if (!rayAabbIntersectionDistanceRange(
                relative_origin, direction,
                tile.terrain_ray_bounds_min, tile.terrain_ray_bounds_max,
                &bounds_entry_distance_m, &bounds_exit_distance_m)
            || bounds_entry_distance_m >= nearest_distance_m)
        {
            continue;
        }

        const qsizetype index_begin = qsizetype(tile.first_index);
        const qsizetype index_end = index_begin + qsizetype(tile.index_count);
        if (index_begin < 0 || index_end > this->window_indices.size())
            continue;

        const std::function<void(qsizetype, qsizetype)> test_index_range =
            [this, &relative_origin, &direction, &nearest_distance_m, &found]
            (qsizetype range_begin, qsizetype range_end)
            {
                for (qsizetype index = range_begin;
                     index + 2 < range_end; index += 3)
                {
                    const quint32 a_index = this->window_indices.at(index);
                    const quint32 b_index = this->window_indices.at(index + 1);
                    const quint32 c_index = this->window_indices.at(index + 2);
                    if (qsizetype(a_index) >= this->window_vertices.size()
                        || qsizetype(b_index) >= this->window_vertices.size()
                        || qsizetype(c_index) >= this->window_vertices.size())
                    {
                        continue;
                    }

                    const MapGlobeSurfaceVertex &a_vertex =
                        this->window_vertices.at(a_index);
                    const MapGlobeSurfaceVertex &b_vertex =
                        this->window_vertices.at(b_index);
                    const MapGlobeSurfaceVertex &c_vertex =
                        this->window_vertices.at(c_index);
                    const QVector3D a(
                        a_vertex.x, a_vertex.y, a_vertex.z);
                    const QVector3D b(
                        b_vertex.x, b_vertex.y, b_vertex.z);
                    const QVector3D c(
                        c_vertex.x, c_vertex.y, c_vertex.z);

                    double candidate_distance_m = 0.0;
                    if (!mapGlobeRayTriangleIntersectionDistance(
                            relative_origin, direction, a, b, c,
                            &candidate_distance_m)
                        || !(candidate_distance_m > 0.0)
                        || !mapGlobeUpdateNearestHitDistance(
                            candidate_distance_m, &nearest_distance_m))
                    {
                        continue;
                    }

                    found = true;
                }
            };

        const int cell_count = qMax(1, tile.terrain_cell_count);
        const qsizetype row_index_count = qsizetype(cell_count) * 6;
        const bool have_row_bounds =
            tile.terrain_ray_row_bounds.size() == cell_count
            && qsizetype(tile.index_count)
                == qsizetype(cell_count) * qsizetype(cell_count) * 6;
        if (!have_row_bounds)
        {
            test_index_range(index_begin, index_end);
            continue;
        }

        for (int row = 0; row < cell_count; ++row)
        {
            const MapGlobeSurfaceTerrainRayRowBounds &row_bounds =
                tile.terrain_ray_row_bounds.at(row);
            double row_entry_distance_m = 0.0;
            double row_exit_distance_m = 0.0;
            if (!rayAabbIntersectionDistanceRange(
                    relative_origin, direction,
                    row_bounds.minimum, row_bounds.maximum,
                    &row_entry_distance_m, &row_exit_distance_m)
                || row_entry_distance_m >= nearest_distance_m)
            {
                continue;
            }

            const qsizetype row_begin = index_begin
                + qsizetype(row) * row_index_count;
            const qsizetype row_end = qMin(
                row_begin + row_index_count, index_end);
            test_index_range(row_begin, row_end);
        }
    }

    if (!found)
        return false;

    intersection_ecef->x = ray_origin_ecef.x
        + double(direction.x()) * nearest_distance_m;
    intersection_ecef->y = ray_origin_ecef.y
        + double(direction.y()) * nearest_distance_m;
    intersection_ecef->z = ray_origin_ecef.z
        + double(direction.z()) * nearest_distance_m;
    *distance_m = nearest_distance_m;
    return true;
}

bool MapGlobeSurfacePreparation::visibleTerrainSamplingAtCoordinate(
    const CoordinateWGS84 &coordinate,
    int *terrain_zoom,
    double *cell_size_m) const
{
    if (terrain_zoom == nullptr || cell_size_m == nullptr
        || !std::isfinite(coordinate.longitude_deg)
        || !std::isfinite(coordinate.latitude_deg))
    {
        return false;
    }

    *terrain_zoom = -1;
    *cell_size_m = 0.0;

    const MapGlobeSurfaceTile *resolved_tile = nullptr;
    for (int zoom = GlobeImageryMaxZoom; zoom >= 0; --zoom)
    {
        const double coordinate_tile_x = GeoWebMercator::lonToTileX(
            coordinate.longitude_deg, zoom);
        const double coordinate_tile_y = GeoWebMercator::latToTileY(
            coordinate.latitude_deg, zoom);
        const int tile_count = 1 << zoom;
        const int coordinate_x = GeoWebMercator::wrapTileX(
            int(std::floor(coordinate_tile_x)), zoom);
        const int coordinate_y = qBound(
            0, int(std::floor(coordinate_tile_y)), tile_count - 1);
        const quint64 position_key = MapGlobeSurfaceScene::positionKey(
            zoom, coordinate_x, coordinate_y);
        const QHash<quint64, qsizetype>::const_iterator tile_iterator =
            this->window_tile_indices_by_position.constFind(position_key);
        if (tile_iterator == this->window_tile_indices_by_position.constEnd())
            continue;

        const qsizetype tile_index = tile_iterator.value();
        if (tile_index < 0 || tile_index >= this->window_tiles.size())
            return false;
        resolved_tile = &this->window_tiles.at(tile_index);
        break;
    }

    if (resolved_tile == nullptr || resolved_tile->is_cap
        || resolved_tile->terrain_key.isEmpty()
        || resolved_tile->terrain_zoom < GlobeTerrainReliefMinimumZoom
        || resolved_tile->terrain_cell_count <= 0
        || !resolved_tile->terrain_mesh_has_relief)
    {
        return false;
    }

    const int tile_count = 1 << resolved_tile->zoom;
    const double latitude_top_deg = GeoWebMercator::tileYToLat(
        double(resolved_tile->tile_y), resolved_tile->zoom);
    const double latitude_bottom_deg = GeoWebMercator::tileYToLat(
        double(resolved_tile->tile_y) + 1.0, resolved_tile->zoom);
    const double tile_width_m =
        (2.0 * M_PI * GeoWgs84Ellipsoid::EquatorialRadiusM
         * qMax(0.0, std::cos(qDegreesToRadians(coordinate.latitude_deg))))
        / double(tile_count);
    const double tile_height_m = GeoWgs84Ellipsoid::EquatorialRadiusM
        * std::abs(qDegreesToRadians(
            latitude_top_deg - latitude_bottom_deg));
    const double reference_size_m = qMax(tile_width_m, tile_height_m);
    const double resolved_cell_size_m =
        reference_size_m / double(resolved_tile->terrain_cell_count);
    if (!std::isfinite(resolved_cell_size_m) || resolved_cell_size_m <= 0.0)
        return false;

    *terrain_zoom = resolved_tile->terrain_zoom;
    *cell_size_m = resolved_cell_size_m;
    return true;
}

void MapGlobeSurfacePreparation::appendWireframeEdges(
    const QVector<MapGlobeSurfaceVertex> &vertices,
    const QVector<quint32> &indices)
{
    for (qsizetype index = 0; index + 2 < indices.size(); index += 3)
    {
        const quint32 a_index = indices.at(index);
        const quint32 b_index = indices.at(index + 1);
        const quint32 c_index = indices.at(index + 2);
        if (qsizetype(a_index) >= vertices.size()
            || qsizetype(b_index) >= vertices.size()
            || qsizetype(c_index) >= vertices.size())
        {
            continue;
        }

        const MapGlobeSurfaceVertex &a = vertices.at(a_index);
        const MapGlobeSurfaceVertex &b = vertices.at(b_index);
        const MapGlobeSurfaceVertex &c = vertices.at(c_index);
        const MapGlobeSurfaceWireframeVertex wa = {a.x, a.y, a.z};
        const MapGlobeSurfaceWireframeVertex wb = {b.x, b.y, b.z};
        const MapGlobeSurfaceWireframeVertex wc = {c.x, c.y, c.z};

        this->wireframe_vertices.append(wa);
        this->wireframe_vertices.append(wb);
        this->wireframe_vertices.append(wb);
        this->wireframe_vertices.append(wc);
        this->wireframe_vertices.append(wc);
        this->wireframe_vertices.append(wa);
    }
}

void MapGlobeSurfacePreparation::rebuildWireframeVertices()
{
    this->wireframe_vertices.clear();
    this->wireframe_vertices.reserve(
        (this->window_indices.size() + this->cap_indices.size()) * 2);
    this->appendWireframeEdges(this->window_vertices, this->window_indices);
    this->appendWireframeEdges(this->cap_vertices, this->cap_indices);
}

const GeoWgs84Ellipsoid::EcefPositionD &
MapGlobeSurfacePreparation::renderOriginEcef() const
{
    return this->render_origin_ecef;
}

const QVector<MapGlobeSurfaceVertex> &
MapGlobeSurfacePreparation::windowVertices() const
{
    return this->window_vertices;
}

QVector<MapGlobeSurfaceVertex> &MapGlobeSurfacePreparation::windowVertices()
{
    return this->window_vertices;
}

const QVector<quint32> &MapGlobeSurfacePreparation::windowIndices() const
{
    return this->window_indices;
}

const QVector<MapGlobeSurfaceTile> &
MapGlobeSurfacePreparation::windowTiles() const
{
    return this->window_tiles;
}

QVector<MapGlobeSurfaceTile> &MapGlobeSurfacePreparation::windowTiles()
{
    return this->window_tiles;
}

const QSet<quint64> &MapGlobeSurfacePreparation::windowPositionKeys() const
{
    return this->window_position_keys;
}

bool MapGlobeSurfacePreparation::windowDirty() const
{
    return this->window_dirty;
}

const QVector<MapGlobeSurfaceVertex> &
MapGlobeSurfacePreparation::capVertices() const
{
    return this->cap_vertices;
}

const QVector<quint32> &MapGlobeSurfacePreparation::capIndices() const
{
    return this->cap_indices;
}

const QVector<MapGlobeSurfaceTile> &
MapGlobeSurfacePreparation::capTiles() const
{
    return this->cap_tiles;
}

QVector<MapGlobeSurfaceTile> &MapGlobeSurfacePreparation::capTiles()
{
    return this->cap_tiles;
}

const QVector<MapGlobeSurfaceWireframeVertex> &
MapGlobeSurfacePreparation::wireframeVertices() const
{
    return this->wireframe_vertices;
}
