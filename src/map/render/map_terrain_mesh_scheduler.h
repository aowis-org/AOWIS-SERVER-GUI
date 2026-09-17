#ifndef MAP_TERRAIN_MESH_SCHEDULER_H
#define MAP_TERRAIN_MESH_SCHEDULER_H

#include "map/data/map_terrain_tile.h"

#include <QMutex>
#include <QString>
#include <QThread>
#include <QVector>
#include <QWaitCondition>

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

    // Identifies the rendered imagery tile. virtual_x remains unwrapped so
    // tiles on opposite sides of the antimeridian stay distinct even when
    // tile_x wraps to the same dataset address.
    int virtual_x = 0;
    int tile_x = 0;
    int y = 0;
    int imagery_zoom = 0;
    int terrain_zoom = 0;

    // LOD-selected desired cell count. The actual resolved count, after
    // clamping to DEM resolution and imagery/terrain zoom delta, is returned
    // in MapTerrainMeshResult::cell_count.
    int requested_cell_count = 1;

    // Crack-free Globe terrain LOD stitching. Each value is the number of
    // edge segments used by a coarser same-zoom neighboring tile on that side,
    // or 0 when no stitching is needed.
    int stitch_top_cell_count = 0;
    int stitch_right_cell_count = 0;
    int stitch_bottom_cell_count = 0;
    int stitch_left_cell_count = 0;

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
    int cell_count = 0;
    int stitch_top_cell_count = 0;
    int stitch_right_cell_count = 0;
    int stitch_bottom_cell_count = 0;
    int stitch_left_cell_count = 0;
    QVector<MapTerrainMeshVertex> vertices;
};

// Builds one Globe terrain vertex grid. Pure and thread-safe.
MapTerrainMeshResult buildTerrainMeshResult(const MapTerrainMeshRequest &request);

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
