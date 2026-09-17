#include "map/render/map_terrain_mesh_scheduler.h"

#include "geo/geo_web_mercator.h"
#include "geo/geo_wgs84_ellipsoid.h"
#include "map/render/map_globe_vertical_transform.h"

#include <QMutexLocker>
#include <QVector3D>
#include <QtMath>

#include <cmath>
#include <utility>

namespace
{
float terrainSample(const MapTerrainTile &terrain_tile, int row, int column)
{
    const int bounded_row = qBound(0, row, MapTerrainTileGridSize - 1);
    const int bounded_column = qBound(0, column, MapTerrainTileGridSize - 1);
    return terrain_tile.elevations_m.at(
        bounded_row * MapTerrainTileGridSize + bounded_column);
}

float nearestFiniteTerrainSample(const MapTerrainTile &terrain_tile, int row, int column)
{
    const int bounded_row = qBound(0, row, MapTerrainTileGridSize - 1);
    const int bounded_column = qBound(0, column, MapTerrainTileGridSize - 1);
    const float direct = terrainSample(terrain_tile, bounded_row, bounded_column);
    if (std::isfinite(double(direct)))
        return direct;

    for (int ring = 1; ring < MapTerrainTileGridSize; ++ring)
    {
        const int row_minimum = qMax(0, bounded_row - ring);
        const int row_maximum = qMin(MapTerrainTileGridSize - 1, bounded_row + ring);
        const int column_minimum = qMax(0, bounded_column - ring);
        const int column_maximum = qMin(MapTerrainTileGridSize - 1, bounded_column + ring);

        for (int sample_column = column_minimum; sample_column <= column_maximum; ++sample_column)
        {
            const float top = terrainSample(terrain_tile, row_minimum, sample_column);
            if (std::isfinite(double(top)))
                return top;
            const float bottom = terrainSample(terrain_tile, row_maximum, sample_column);
            if (std::isfinite(double(bottom)))
                return bottom;
        }
        for (int sample_row = row_minimum + 1; sample_row < row_maximum; ++sample_row)
        {
            const float left = terrainSample(terrain_tile, sample_row, column_minimum);
            if (std::isfinite(double(left)))
                return left;
            const float right = terrainSample(terrain_tile, sample_row, column_maximum);
            if (std::isfinite(double(right)))
                return right;
        }
    }

    return 0.0f;
}

float bilinearTerrainSample(const MapTerrainTile &terrain_tile, double u, double v)
{
    const double sample_x = qBound(0.0, u, 1.0) * MapTerrainTileCellCount;
    const double sample_y = qBound(0.0, v, 1.0) * MapTerrainTileCellCount;
    const int x0 = qBound(0, int(std::floor(sample_x)), MapTerrainTileGridSize - 1);
    const int y0 = qBound(0, int(std::floor(sample_y)), MapTerrainTileGridSize - 1);
    const int x1 = qMin(x0 + 1, MapTerrainTileGridSize - 1);
    const int y1 = qMin(y0 + 1, MapTerrainTileGridSize - 1);
    const double tx = sample_x - x0;
    const double ty = sample_y - y0;

    const float samples[4] = {
        terrainSample(terrain_tile, y0, x0),
        terrainSample(terrain_tile, y0, x1),
        terrainSample(terrain_tile, y1, x0),
        terrainSample(terrain_tile, y1, x1)
    };
    const double weights[4] = {
        (1.0 - tx) * (1.0 - ty),
        tx * (1.0 - ty),
        (1.0 - tx) * ty,
        tx * ty
    };

    double weighted_sum = 0.0;
    double weight_sum = 0.0;
    for (int index = 0; index < 4; ++index)
    {
        if (!std::isfinite(double(samples[index])))
            continue;
        weighted_sum += double(samples[index]) * weights[index];
        weight_sum += weights[index];
    }
    if (weight_sum > 0.0)
        return float(weighted_sum / weight_sum);

    return nearestFiniteTerrainSample(
        terrain_tile, int(std::lround(sample_y)), int(std::lround(sample_x)));
}

float terrainElevationMetersAt(
    const MapTerrainMeshRequest &request, double terrain_u, double terrain_v)
{
    if (!request.terrain_available
        || request.terrain_tile.elevations_m.size() != MapTerrainTileSampleCount)
    {
        return 0.0f;
    }

    return bilinearTerrainSample(request.terrain_tile, terrain_u, terrain_v);
}

int normalizedTerrainStitchCellCount(int requested_cell_count, int cell_count)
{
    if (requested_cell_count <= 0
        || requested_cell_count >= cell_count
        || cell_count % requested_cell_count != 0)
    {
        return 0;
    }

    return requested_cell_count;
}

QVector3D globeTerrainPositionAt(
    const MapTerrainMeshRequest &request,
    double imagery_u, double imagery_v,
    double terrain_u, double terrain_v)
{
    const MapGlobeVerticalTransform vertical_transform(
        request.globe_vertical_exaggeration);
    double elevation_m = vertical_transform.terrainHeightM(
        double(terrainElevationMetersAt(request, terrain_u, terrain_v)));

    // Web Mercator imagery/DEM coverage ends at +-85.051 degrees, while the
    // Globe renderer closes the last few degrees with static polar fans. Keep
    // the shared ring exactly on the ellipsoid to avoid a vertical crack.
    const int imagery_tile_span = 1 << request.imagery_zoom;
    if ((request.y == 0 && imagery_v <= 0.0)
        || (request.y == imagery_tile_span - 1 && imagery_v >= 1.0))
    {
        elevation_m = 0.0;
    }

    const double lon_deg = GeoWebMercator::tileXToLon(
        double(request.virtual_x) + imagery_u, request.imagery_zoom);
    const double lat_deg = GeoWebMercator::tileYToLat(
        double(request.y) + imagery_v, request.imagery_zoom);
    const GeoWgs84Ellipsoid::EcefPositionD position =
        GeoWgs84Ellipsoid::geodeticToEcefD(lon_deg, lat_deg, elevation_m);
    return QVector3D(
        float(position.x - request.globe_render_origin_x),
        float(position.y - request.globe_render_origin_y),
        float(position.z - request.globe_render_origin_z));
}

QVector3D stitchedHorizontalGlobeTerrainEdgePosition(
    const MapTerrainMeshRequest &request,
    int vertex_column, int cell_count, int stitch_cell_count,
    double imagery_v, double terrain_u_min, double terrain_u_span,
    double terrain_v)
{
    const int normalized_stitch_cell_count =
        normalizedTerrainStitchCellCount(stitch_cell_count, cell_count);
    if (normalized_stitch_cell_count <= 0)
    {
        const double imagery_u = double(vertex_column) / double(cell_count);
        const double terrain_u = terrain_u_min + imagery_u * terrain_u_span;
        return globeTerrainPositionAt(
            request, imagery_u, imagery_v, terrain_u, terrain_v);
    }

    const int fine_cells_per_stitch_cell =
        cell_count / normalized_stitch_cell_count;
    const int stitch_vertex_index = vertex_column / fine_cells_per_stitch_cell;
    const int remainder = vertex_column % fine_cells_per_stitch_cell;
    const double imagery_u0 =
        double(stitch_vertex_index) / double(normalized_stitch_cell_count);
    const double terrain_u0 = terrain_u_min + imagery_u0 * terrain_u_span;
    if (remainder == 0)
    {
        return globeTerrainPositionAt(
            request, imagery_u0, imagery_v, terrain_u0, terrain_v);
    }

    const double imagery_u1 =
        double(stitch_vertex_index + 1) / double(normalized_stitch_cell_count);
    const double terrain_u1 = terrain_u_min + imagery_u1 * terrain_u_span;
    const QVector3D p0 = globeTerrainPositionAt(
        request, imagery_u0, imagery_v, terrain_u0, terrain_v);
    const QVector3D p1 = globeTerrainPositionAt(
        request, imagery_u1, imagery_v, terrain_u1, terrain_v);
    const float interpolation = float(remainder) / float(fine_cells_per_stitch_cell);

    // The coarse neighbor's shared edge is one straight ECEF segment between
    // its vertices. Interpolate the full ECEF position so the fine edge shares
    // the exact same polyline rather than re-curving onto the ellipsoid.
    return p0 + (p1 - p0) * interpolation;
}

QVector3D stitchedVerticalGlobeTerrainEdgePosition(
    const MapTerrainMeshRequest &request,
    int vertex_row, int cell_count, int stitch_cell_count,
    double imagery_u, double terrain_v_min, double terrain_v_span,
    double terrain_u)
{
    const int normalized_stitch_cell_count =
        normalizedTerrainStitchCellCount(stitch_cell_count, cell_count);
    if (normalized_stitch_cell_count <= 0)
    {
        const double imagery_v = double(vertex_row) / double(cell_count);
        const double terrain_v = terrain_v_min + imagery_v * terrain_v_span;
        return globeTerrainPositionAt(
            request, imagery_u, imagery_v, terrain_u, terrain_v);
    }

    const int fine_cells_per_stitch_cell =
        cell_count / normalized_stitch_cell_count;
    const int stitch_vertex_index = vertex_row / fine_cells_per_stitch_cell;
    const int remainder = vertex_row % fine_cells_per_stitch_cell;
    const double imagery_v0 =
        double(stitch_vertex_index) / double(normalized_stitch_cell_count);
    const double terrain_v0 = terrain_v_min + imagery_v0 * terrain_v_span;
    if (remainder == 0)
    {
        return globeTerrainPositionAt(
            request, imagery_u, imagery_v0, terrain_u, terrain_v0);
    }

    const double imagery_v1 =
        double(stitch_vertex_index + 1) / double(normalized_stitch_cell_count);
    const double terrain_v1 = terrain_v_min + imagery_v1 * terrain_v_span;
    const QVector3D p0 = globeTerrainPositionAt(
        request, imagery_u, imagery_v0, terrain_u, terrain_v0);
    const QVector3D p1 = globeTerrainPositionAt(
        request, imagery_u, imagery_v1, terrain_u, terrain_v1);
    const float interpolation = float(remainder) / float(fine_cells_per_stitch_cell);
    return p0 + (p1 - p0) * interpolation;
}

QVector3D globeTerrainMeshVertexPosition(
    const MapTerrainMeshRequest &request,
    int vertex_column, int vertex_row, int cell_count,
    int stitch_top_cell_count, int stitch_right_cell_count,
    int stitch_bottom_cell_count, int stitch_left_cell_count,
    double terrain_u_min, double terrain_v_min,
    double terrain_u_span, double terrain_v_span)
{
    const double imagery_u = double(vertex_column) / double(cell_count);
    const double imagery_v = double(vertex_row) / double(cell_count);
    const double terrain_u = terrain_u_min + imagery_u * terrain_u_span;
    const double terrain_v = terrain_v_min + imagery_v * terrain_v_span;

    if (vertex_row == 0 && stitch_top_cell_count > 0)
    {
        return stitchedHorizontalGlobeTerrainEdgePosition(
            request, vertex_column, cell_count, stitch_top_cell_count,
            0.0, terrain_u_min, terrain_u_span, terrain_v_min);
    }
    if (vertex_row == cell_count && stitch_bottom_cell_count > 0)
    {
        return stitchedHorizontalGlobeTerrainEdgePosition(
            request, vertex_column, cell_count, stitch_bottom_cell_count,
            1.0, terrain_u_min, terrain_u_span,
            terrain_v_min + terrain_v_span);
    }
    if (vertex_column == 0 && stitch_left_cell_count > 0)
    {
        return stitchedVerticalGlobeTerrainEdgePosition(
            request, vertex_row, cell_count, stitch_left_cell_count,
            0.0, terrain_v_min, terrain_v_span, terrain_u_min);
    }
    if (vertex_column == cell_count && stitch_right_cell_count > 0)
    {
        return stitchedVerticalGlobeTerrainEdgePosition(
            request, vertex_row, cell_count, stitch_right_cell_count,
            1.0, terrain_v_min, terrain_v_span,
            terrain_u_min + terrain_u_span);
    }

    return globeTerrainPositionAt(
        request, imagery_u, imagery_v, terrain_u, terrain_v);
}
}

MapTerrainMeshResult buildTerrainMeshResult(const MapTerrainMeshRequest &request)
{
    MapTerrainMeshResult result;
    result.request_id = request.request_id;
    result.terrain_key = request.terrain_key;
    result.virtual_x = request.virtual_x;
    result.y = request.y;
    result.terrain_available = request.terrain_available
        && request.terrain_tile.elevations_m.size() == MapTerrainTileSampleCount;

    if (request.imagery_zoom < request.terrain_zoom)
        return result;

    const int zoom_delta = request.imagery_zoom - request.terrain_zoom;
    const double subdivision_count = std::ldexp(1.0, zoom_delta);
    const quint32 terrain_x = result.terrain_available
        ? request.terrain_tile.address.x
        : (quint32(request.tile_x) >> zoom_delta);
    const quint32 terrain_y = result.terrain_available
        ? request.terrain_tile.address.y
        : (quint32(request.y) >> zoom_delta);
    const double local_tile_x =
        double(request.tile_x) - double(terrain_x) * subdivision_count;
    const double local_tile_y =
        double(request.y) - double(terrain_y) * subdivision_count;
    const double terrain_u_min = local_tile_x / subdivision_count;
    const double terrain_v_min = local_tile_y / subdivision_count;
    const double terrain_u_span = 1.0 / subdivision_count;
    const double terrain_v_span = 1.0 / subdivision_count;

    const int cell_divisor = 1 << qMin(zoom_delta, 6);
    const int native_cell_count = qMax(1, MapTerrainTileCellCount / cell_divisor);
    const int cell_count = qBound(
        1, request.requested_cell_count, native_cell_count);
    result.cell_count = cell_count;
    result.stitch_top_cell_count = normalizedTerrainStitchCellCount(
        request.stitch_top_cell_count, cell_count);
    result.stitch_right_cell_count = normalizedTerrainStitchCellCount(
        request.stitch_right_cell_count, cell_count);
    result.stitch_bottom_cell_count = normalizedTerrainStitchCellCount(
        request.stitch_bottom_cell_count, cell_count);
    result.stitch_left_cell_count = normalizedTerrainStitchCellCount(
        request.stitch_left_cell_count, cell_count);

    const qsizetype grid_width = qsizetype(cell_count) + 1;
    result.vertices.reserve(grid_width * grid_width);
    for (int row = 0; row <= cell_count; ++row)
    {
        const double v = double(row) / double(cell_count);
        for (int column = 0; column <= cell_count; ++column)
        {
            const double u = double(column) / double(cell_count);
            const QVector3D position = globeTerrainMeshVertexPosition(
                request, column, row, cell_count,
                result.stitch_top_cell_count,
                result.stitch_right_cell_count,
                result.stitch_bottom_cell_count,
                result.stitch_left_cell_count,
                terrain_u_min, terrain_v_min,
                terrain_u_span, terrain_v_span);
            result.vertices.append(MapTerrainMeshVertex{
                position.x(), position.y(), position.z(), float(u), float(v)});
        }
    }

    return result;
}

MapTerrainMeshScheduler::MapTerrainMeshScheduler()
{
    start();
}

MapTerrainMeshScheduler::~MapTerrainMeshScheduler()
{
    {
        QMutexLocker locker(&this->mutex);
        this->shutting_down = true;
    }
    this->wait_condition.wakeAll();
    wait();
}

void MapTerrainMeshScheduler::submit(const MapTerrainMeshRequest &request)
{
    {
        QMutexLocker locker(&this->mutex);
        if (this->shutting_down)
            return;
        this->pending_requests.push_back(request);
    }
    this->wait_condition.wakeOne();
}

void MapTerrainMeshScheduler::collectReady(QVector<MapTerrainMeshResult> *results)
{
    if (results == nullptr)
        return;

    QMutexLocker locker(&this->mutex);
    if (this->completed_results.empty())
        return;

    results->reserve(results->size() + qsizetype(this->completed_results.size()));
    for (MapTerrainMeshResult &result : this->completed_results)
        results->append(std::move(result));
    this->completed_results.clear();
}

void MapTerrainMeshScheduler::run()
{
    for (;;)
    {
        MapTerrainMeshRequest request;
        {
            QMutexLocker locker(&this->mutex);
            while (this->pending_requests.empty() && !this->shutting_down)
                this->wait_condition.wait(&this->mutex);

            if (this->pending_requests.empty())
                return;

            request = std::move(this->pending_requests.front());
            this->pending_requests.pop_front();
        }

        MapTerrainMeshResult result = buildTerrainMeshResult(request);

        {
            QMutexLocker locker(&this->mutex);
            this->completed_results.push_back(std::move(result));
        }
    }
}
