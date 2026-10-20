#include "map/render/map_terrain_mesh_scheduler.h"

#include "geo/geo_web_mercator.h"
#include "geo/geo_wgs84_ellipsoid.h"
#include "map/render/map_globe_vertical_transform.h"

#include <QMutexLocker>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

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

// Per-column / per-row parts of the WGS84 geodetic -> ECEF conversion of a
// tile grid: the longitude depends on the column only, the latitude on the
// row only, so the trigonometry (and the Web Mercator inverse, an atan of a
// sinh) is evaluated (cells + 1) times per axis instead of once per vertex.
// Per vertex only the height enters: X = (N + h) cos(lat) cos(lon),
// Y = (N + h) cos(lat) sin(lon), Z = (N (1 - e^2) + h) sin(lat), the
// closed form GeographicLib evaluates (sub-millimetre apart from its
// exact-degree trigonometry). A 64-cell tile cost 0.62 ms with a
// GeographicLib conversion and two Mercator inverses per vertex.
struct GridColumn
{
    double cos_lon = 0.0;
    double sin_lon = 0.0;
};

struct GridRow
{
    double cos_lat = 0.0;
    double sin_lat = 0.0;
    double prime_vertical_m = 0.0;
};

GridColumn gridColumn(double lon_deg)
{
    const double lon_rad = qDegreesToRadians(lon_deg);
    return GridColumn{std::cos(lon_rad), std::sin(lon_rad)};
}

GridRow gridRow(double lat_deg)
{
    constexpr double Flattening = 1.0 / GeoWgs84Ellipsoid::FlatteningInverse;
    constexpr double EccentricitySquared = Flattening * (2.0 - Flattening);
    const double lat_rad = qDegreesToRadians(lat_deg);
    const double sin_lat = std::sin(lat_rad);
    return GridRow{std::cos(lat_rad), sin_lat,
                   GeoWgs84Ellipsoid::EquatorialRadiusM
                       / std::sqrt(1.0 - EccentricitySquared * sin_lat * sin_lat)};
}

MapTerrainMeshVertex gridVertex(const GridColumn &column, const GridRow &row, double height_m,
                                const MapTerrainMeshRequest &request, float u, float v)
{
    constexpr double Flattening = 1.0 / GeoWgs84Ellipsoid::FlatteningInverse;
    constexpr double EccentricitySquared = Flattening * (2.0 - Flattening);
    const double horizontal_m = (row.prime_vertical_m + height_m) * row.cos_lat;
    const double x = horizontal_m * column.cos_lon;
    const double y = horizontal_m * column.sin_lon;
    const double z = (row.prime_vertical_m * (1.0 - EccentricitySquared) + height_m) * row.sin_lat;
    // Relative to the render origin in double, then narrowed.
    return MapTerrainMeshVertex{float(x - request.globe_render_origin_x),
                                float(y - request.globe_render_origin_y),
                                float(z - request.globe_render_origin_z), u, v};
}

}

double mapGlobeTileSkirtDepthM(int zoom, int tile_y, int cells, double vertical_exaggeration,
                               double elevation_range_m)
{
    constexpr double MinimumSkirtDepthM = 20.0;
    const double latitude_deg = GeoWebMercator::tileYToLat(double(tile_y) + 0.5, zoom);
    const double tile_width_m = 2.0 * M_PI * GeoWgs84Ellipsoid::EquatorialRadiusM
        * std::cos(qDegreesToRadians(latitude_deg)) / std::ldexp(1.0, zoom);
    const double cell_length_m = tile_width_m / double(std::max(1, cells));
    return (elevation_range_m + std::max(MinimumSkirtDepthM, cell_length_m))
        * std::max(1.0, vertical_exaggeration);
}

const QVector<quint16> &mapGlobeTileIndices(int cells)
{
    static const QVector<quint16> empty;
    if (cells < 1 || cells > MapGlobeTileMaximumCellCount)
        return empty;

    // Built under the lock, never changed afterwards: a returned reference
    // stays valid (and its data at the same address) for the process.
    static QMutex mutex;
    static std::array<QVector<quint16>, MapGlobeTileMaximumCellCount + 1> topologies;
    QMutexLocker locker(&mutex);
    QVector<quint16> &indices = topologies[std::size_t(cells)];
    if (!indices.isEmpty())
        return indices;

    const quint16 grid_width = quint16(cells + 1);
    indices.resize(mapGlobeTileIndexCount(cells));
    quint16 *target = indices.data();
    for (int row = 0; row < cells; ++row)
    {
        for (int column = 0; column < cells; ++column)
        {
            const quint16 p00 = quint16(row * grid_width + column);
            const quint16 p10 = quint16(p00 + 1);
            const quint16 p01 = quint16(p00 + grid_width);
            const quint16 p11 = quint16(p01 + 1);
            *target++ = p00;
            *target++ = p01;
            *target++ = p10;
            *target++ = p10;
            *target++ = p01;
            *target++ = p11;
        }
    }

    // Skirt: one quad per edge segment between the grid edge vertices g0, g1
    // and their lowered copies s0, s1 (mapGlobeAppendTileSkirt() order: top
    // row, bottom row, left column, right column). The surface state culls
    // no faces, so the winding does not matter.
    const int skirt_first = mapGlobeTileGridVertexCount(cells);
    for (int edge = 0; edge < 4; ++edge)
    {
        for (int index = 0; index < cells; ++index)
        {
            int g0 = 0;
            int g1 = 0;
            if (edge == 0)
            {
                g0 = index;
                g1 = g0 + 1;
            }
            else if (edge == 1)
            {
                g0 = cells * grid_width + index;
                g1 = g0 + 1;
            }
            else
            {
                const int column = edge == 2 ? 0 : cells;
                g0 = index * grid_width + column;
                g1 = g0 + grid_width;
            }
            const int s0 = skirt_first + edge * grid_width + index;
            const int s1 = s0 + 1;
            *target++ = quint16(g0);
            *target++ = quint16(s0);
            *target++ = quint16(g1);
            *target++ = quint16(g1);
            *target++ = quint16(s0);
            *target++ = quint16(s1);
        }
    }
    return indices;
}

MapTerrainMeshResult buildTerrainMeshResult(const MapTerrainMeshRequest &request)
{
    // A stand-in carries its coarser ancestor DEM tile: resampled at the
    // tile's own DEM address first (here, on the mesh thread; 4225 bilinear
    // samples per tile cost the GUI thread frames while loading).
    if (request.terrain_tile.elevations_m.size() == MapTerrainTileSampleCount
        && request.terrain_tile.address.zoom < request.terrain_zoom
        && request.imagery_zoom >= request.terrain_zoom)
    {
        const int zoom_delta = request.imagery_zoom - request.terrain_zoom;
        MapTerrainTileAddress address;
        address.zoom = request.terrain_zoom;
        address.x = quint32(request.tile_x) >> zoom_delta;
        address.y = quint32(request.y) >> zoom_delta;
        MapTerrainMeshRequest resampled = request;
        resampled.terrain_tile = mapTerrainTileFromAncestor(request.terrain_tile, address);
        return buildTerrainMeshResult(resampled);
    }

    MapTerrainMeshResult result;
    result.request_id = request.request_id;
    result.terrain_key = request.terrain_key;
    result.stand_in_zoom = request.stand_in_zoom;
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

    // One mesh cell per DEM cell: with 2^zoom_delta imagery tiles across the
    // DEM tile, 64 / 2^zoom_delta cells each, so the vertex (row, column)
    // lies exactly on DEM sample (local_tile_y * cells + row, local_tile_x *
    // cells + column) and the bilinear lookup returns that sample unchanged:
    // the surface is the provider's heightfield, never a thinned copy.
    const int cell_count = mapGlobeTerrainCellCount(request.imagery_zoom, request.terrain_zoom);
    result.cell_count = cell_count;

    const MapGlobeVerticalTransform vertical_transform(request.globe_vertical_exaggeration);
    const int width = cell_count + 1;
    std::vector<GridColumn> columns(static_cast<std::size_t>(width));
    for (int column = 0; column < width; ++column)
    {
        columns[std::size_t(column)] = gridColumn(GeoWebMercator::tileXToLon(
            double(request.virtual_x) + double(column) / double(cell_count), request.imagery_zoom));
    }
    // Web Mercator imagery/DEM coverage ends at +-85.051 degrees, while the
    // Globe renderer closes the last few degrees with static polar fans. Keep
    // the shared ring exactly on the ellipsoid to avoid a vertical crack.
    const int imagery_tile_span = 1 << request.imagery_zoom;
    const bool polar_top = request.y == 0;
    const bool polar_bottom = request.y == imagery_tile_span - 1;

    result.vertices.resize(mapGlobeTileGridVertexCount(cell_count));
    MapTerrainMeshVertex *target = result.vertices.data();
    for (int row = 0; row < width; ++row)
    {
        const double v = double(row) / double(cell_count);
        const double terrain_v = terrain_v_min + v * terrain_v_span;
        const GridRow grid_row = gridRow(GeoWebMercator::tileYToLat(double(request.y) + v, request.imagery_zoom));
        const bool on_ellipsoid = (polar_top && row == 0) || (polar_bottom && row == cell_count);
        for (int column = 0; column < width; ++column)
        {
            const double u = double(column) / double(cell_count);
            const double terrain_u = terrain_u_min + u * terrain_u_span;
            const double height_m = on_ellipsoid
                ? 0.0
                : vertical_transform.terrainHeightM(double(terrainElevationMetersAt(request, terrain_u, terrain_v)));
            *target++ = gridVertex(columns[std::size_t(column)], grid_row, height_m, request, float(u), float(v));
        }
    }
    // A DEM tile whose samples are all missing has no finite range.
    const double minimum_m = request.terrain_tile.minimum_elevation_m;
    const double maximum_m = request.terrain_tile.maximum_elevation_m;
    const double elevation_range_m =
        std::isfinite(minimum_m) && std::isfinite(maximum_m) && maximum_m > minimum_m
        ? maximum_m - minimum_m
        : 0.0;
    mapGlobeAppendTileSkirt(&result.vertices, 0, cell_count,
                            mapGlobeTileSkirtDepthM(request.imagery_zoom, request.y, cell_count,
                                                    request.globe_vertical_exaggeration,
                                                    elevation_range_m),
                            request.globe_render_origin_x, request.globe_render_origin_y,
                            request.globe_render_origin_z);

    return result;
}

MapTerrainTile mapTerrainTileFromAncestor(
    const MapTerrainTile &ancestor, const MapTerrainTileAddress &address)
{
    MapTerrainTile result;
    result.address = address;
    result.dataset = ancestor.dataset;
    result.nominal_resolution_m = ancestor.nominal_resolution_m;
    result.vertical_datum = ancestor.vertical_datum;

    // The address's extent inside the ancestor in its normalised 0..1 grid
    // coordinates: 1 / 2^delta wide, offset by the address's index among the
    // ancestor's descendants at that zoom.
    const int zoom_delta = address.zoom - ancestor.address.zoom;
    const double extent = std::ldexp(1.0, -zoom_delta);
    const double u_min =
        (double(address.x) - std::ldexp(double(ancestor.address.x), zoom_delta)) * extent;
    const double v_min =
        (double(address.y) - std::ldexp(double(ancestor.address.y), zoom_delta)) * extent;

    result.elevations_m.resize(MapTerrainTileSampleCount);
    double minimum_m = std::numeric_limits<double>::infinity();
    double maximum_m = -std::numeric_limits<double>::infinity();
    for (int row = 0; row < MapTerrainTileGridSize; ++row)
    {
        const double v = v_min + extent * double(row) / double(MapTerrainTileCellCount);
        for (int column = 0; column < MapTerrainTileGridSize; ++column)
        {
            const double u = u_min + extent * double(column) / double(MapTerrainTileCellCount);
            const float elevation_m = bilinearTerrainSample(ancestor, u, v);
            result.elevations_m[row * MapTerrainTileGridSize + column] = elevation_m;
            minimum_m = std::min(minimum_m, double(elevation_m));
            maximum_m = std::max(maximum_m, double(elevation_m));
        }
    }
    result.minimum_elevation_m = minimum_m;
    result.maximum_elevation_m = maximum_m;
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
