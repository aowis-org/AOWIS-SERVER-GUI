#ifndef MAP_TERRAIN_MESH_SCHEDULER_H
#define MAP_TERRAIN_MESH_SCHEDULER_H

#include "map/data/map_terrain_tile.h"
#include "map/render/map_globe_surface_vertex.h"
#include "map/render/map_globe_terrain_levels.h"

#include <QMutex>
#include <QString>
#include <QThread>
#include <QVector>
#include <QWaitCondition>

#include <cmath>
#include <deque>

// One Globe terrain-mesh vertex. Results contain one row-major
// (cell_count + 1)^2 ECEF-relative vertex grid; the Globe renderer owns the
// shared deterministic index topology, so terrain updates only transfer the
// positions/UVs that can actually change.
struct MapTerrainMeshVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    MapGlobePackedNormal normal;
};

// The four DEM tiles beside a mesh's own DEM tile (same zoom), in this
// order: their samples next to the shared edge give the surface normals of
// the edge vertices the same central differences as everywhere else, so the
// hillshading has no seam along DEM tile borders.
enum MapTerrainMeshNeighbor
{
    MapTerrainMeshNeighborWest = 0,
    MapTerrainMeshNeighborEast = 1,
    MapTerrainMeshNeighborNorth = 2,
    MapTerrainMeshNeighborSouth = 3,
    MapTerrainMeshNeighborCount = 4
};

// Everything needed to build one Globe terrain tile, carried entirely by
// value so the background thread never touches a live main-thread-owned
// object (MapTerrainRepository, MapModel, or any GPU resource).
//
// terrain_tile is a value copy of the cached DEM tile. Copying it on the main
// thread before submitting is safe per Qt's implicit-sharing/copy-on-write
// thread-safety guarantee.
struct MapTerrainMeshRequest
{
    quint64 request_id = 0;
    QString terrain_key;
    MapTerrainTile terrain_tile;
    bool terrain_available = false;
    // MapTerrainMeshNeighbor order; a neighbor without samples is not cached
    // (its edge normals are one-sided then). Only for the tile's own DEM.
    MapTerrainTile terrain_neighbors[MapTerrainMeshNeighborCount];
    // While the tile's own DEM is loading: terrain_tile is the cached coarser
    // DEM tile at this zoom, resampled at terrain_zoom by
    // buildTerrainMeshResult() (mapTerrainTileFromAncestor()); -1: the
    // tile's own DEM.
    int stand_in_zoom = -1;

    // Identifies the rendered imagery tile. virtual_x remains unwrapped so
    // tiles on opposite sides of the antimeridian stay distinct even when
    // tile_x wraps to the same dataset address.
    int virtual_x = 0;
    int tile_x = 0;
    int y = 0;
    int imagery_zoom = 0;
    int terrain_zoom = 0;

    // Generate geodetic WGS84/ECEF positions relative to the same sticky ECEF
    // render origin used by the Globe camera/network. The subtraction happens
    // in double precision before narrowing into float vertices.
    double globe_vertical_exaggeration = 1.0;
    double globe_render_origin_x = 0.0;
    double globe_render_origin_y = 0.0;
    double globe_render_origin_z = 0.0;
};

struct MapTerrainMeshResult
{
    quint64 request_id = 0;
    QString terrain_key;
    int virtual_x = 0;
    int y = 0;
    bool terrain_available = false;
    // The request's stand_in_zoom.
    int stand_in_zoom = -1;
    // mapGlobeTerrainCellCount(imagery_zoom, terrain_zoom): the grid always
    // has exactly the DEM resolution, every vertex on a DEM sample.
    int cell_count = 0;
    // Bit (1 << MapTerrainMeshNeighbor) per neighbor DEM tile the edge
    // normals needed but the request did not carry (not cached yet); a
    // neighbor beyond the map's edge is never missing. The mesh is built
    // again when one of them arrives.
    int missing_neighbor_mask = 0;
    QVector<MapTerrainMeshVertex> vertices;
};

// --- Globe surface tile layout ---------------------------------------------
// Every globe surface tile (terrain mesh, stand-in or flat) is its
// row-major (cells + 1)^2 grid followed by its skirt: the edge vertices
// again (top row, bottom row, left column, right column; cells + 1 each),
// lowered along the vertical by mapGlobeTileSkirtDepthM(). Neighbours drawn
// from the same DEM zoom share their edge vertices exactly (both lie on the
// same DEM samples, mapGlobeTerrainCellCount()); neighbours from different
// DEM zooms, a stand-in next to a final mesh or a flat tile next to relief
// disagree about it, and the skirt fills that crack and lies hidden below
// the neighbour everywhere else. The grid alone is the surface for rays.
inline int mapGlobeTileGridVertexCount(int cells) { return (cells + 1) * (cells + 1); }
inline int mapGlobeTileVertexCount(int cells) { return mapGlobeTileGridVertexCount(cells) + 4 * (cells + 1); }
inline int mapGlobeTileGridIndexCount(int cells) { return cells * cells * 6; }
inline int mapGlobeTileIndexCount(int cells) { return mapGlobeTileGridIndexCount(cells) + 4 * cells * 6; }

// Most cells per side of a globe surface tile (a whole DEM tile).
inline constexpr int MapGlobeTileMaximumCellCount = MapTerrainTileCellCount;

// The triangles of a tile with `cells` cells per side, as indices relative
// to its first vertex: the grid's cells row by row (two triangles each, so a
// grid row is one contiguous block of cells * 6 indices), then the skirt's
// walls. The topology depends on the cell count only, so every tile with
// that count shares it: the renderer draws a tile with this one index list
// and the tile's first vertex as base vertex, and window rebuilds write no
// indices at all. 16 bit: 64 cells have 4485 vertices. Built once per cell
// count, kept for the process (the GPU references it without a copy);
// empty for a count outside 1..MapGlobeTileMaximumCellCount. Thread-safe.
const QVector<quint16> &mapGlobeTileIndices(int cells);

// Skirt depth of a tile: the elevation range of its DEM plus one grid
// cell's ground length (at least 20 m), times the vertical exaggeration.
// Both neighbours' edges interpolate heights from within that range, so the
// bottom of whichever skirt hangs from the higher edge, lowered below the
// lowest height, reaches past the lower one; the cell length is the margin
// for DEM levels smoothed differently and for the flat tiles' chords.
double mapGlobeTileSkirtDepthM(int zoom, int tile_y, int cells, double vertical_exaggeration,
                               double elevation_range_m);

// Appends the skirt of the grid that starts at grid_first (positions
// relative to the render origin; the vertical is the geocentric direction,
// within a fraction of a degree of the ellipsoid normal).
template <typename Vertex>
void mapGlobeAppendTileSkirt(QVector<Vertex> *vertices, qsizetype grid_first, int cells, double depth_m,
                             double origin_x, double origin_y, double origin_z)
{
    const int width = cells + 1;
    const qsizetype skirt_first = vertices->size();
    vertices->resize(skirt_first + 4 * qsizetype(width));
    Vertex *skirt = vertices->data() + skirt_first;
    const Vertex *grid = vertices->constData() + grid_first;
    for (int edge = 0; edge < 4; ++edge)
    {
        for (int index = 0; index < width; ++index)
        {
            const int row = edge == 0 ? 0 : edge == 1 ? cells : index;
            const int column = edge == 2 ? 0 : edge == 3 ? cells : index;
            Vertex vertex = grid[row * width + column];
            const double x = origin_x + double(vertex.x);
            const double y = origin_y + double(vertex.y);
            const double z = origin_z + double(vertex.z);
            const double length = std::sqrt(x * x + y * y + z * z);
            if (length > 0.0)
            {
                vertex.x = float(double(vertex.x) - x / length * depth_m);
                vertex.y = float(double(vertex.y) - y / length * depth_m);
                vertex.z = float(double(vertex.z) - z / length * depth_m);
            }
            skirt[edge * width + index] = vertex;
        }
    }
}

// Builds one Globe terrain vertex grid and its skirt. Pure and thread-safe.
MapTerrainMeshResult buildTerrainMeshResult(const MapTerrainMeshRequest &request);

// Stand-in DEM tile at `address`, bilinearly resampled from the matching
// sub-rectangle of a cached coarser `ancestor` tile (same dataset, lower
// zoom, covering address). Used while the tile's own DEM is still loading:
// a mesh built from it has the same grid as the final one (so the real mesh
// later replaces it in place) and lies at the right height instead of on
// the ellipsoid. Pure and thread-safe.
MapTerrainTile mapTerrainTileFromAncestor(
    const MapTerrainTile &ancestor, const MapTerrainTileAddress &address);

// Runs buildTerrainMeshResult() on a dedicated background thread so DEM
// resampling for newly-needed Globe terrain never blocks the render thread.
class MapTerrainMeshScheduler : public QThread
{
public:
    MapTerrainMeshScheduler();
    ~MapTerrainMeshScheduler() override;

    void submit(const MapTerrainMeshRequest &request);
    void collectReady(QVector<MapTerrainMeshResult> *results);

protected:
    void run() override;

private:
    QMutex mutex;
    QWaitCondition wait_condition;
    std::deque<MapTerrainMeshRequest> pending_requests;
    std::deque<MapTerrainMeshResult> completed_results;
    bool shutting_down = false;
};

#endif // MAP_TERRAIN_MESH_SCHEDULER_H
