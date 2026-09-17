#ifndef MAP_RHI_TERRAIN_MESH_SCHEDULER_H
#define MAP_RHI_TERRAIN_MESH_SCHEDULER_H

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
struct MapRhiTerrainMeshVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

// Everything needed to build one Globe terrain tile, carried entirely by
// value so the background thread never touches a live main-thread-owned
// object (MapTerrainRepository, MapModel, or any QRhi resource).
//
// terrain_tile is a value copy of the cached DEM tile. Copying it on the main
// thread before submitting is safe per Qt's implicit-sharing/copy-on-write
// thread-safety guarantee.
struct MapRhiTerrainMeshRequest
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
    // in MapRhiTerrainMeshResult::cell_count.
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

struct MapRhiTerrainMeshResult
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
    QVector<MapRhiTerrainMeshVertex> vertices;
};

// Builds one Globe terrain vertex grid. Pure and thread-safe.
MapRhiTerrainMeshResult buildTerrainMeshResult(const MapRhiTerrainMeshRequest &request);

// Runs buildTerrainMeshResult() on a dedicated background thread so DEM
// resampling for newly-needed Globe terrain never blocks the render thread.
class MapRhiTerrainMeshScheduler : public QThread
{
public:
    MapRhiTerrainMeshScheduler();
    ~MapRhiTerrainMeshScheduler() override;

    void submit(const MapRhiTerrainMeshRequest &request);
    void collectReady(QVector<MapRhiTerrainMeshResult> *results);

protected:
    void run() override;

private:
    QMutex mutex;
    QWaitCondition wait_condition;
    std::deque<MapRhiTerrainMeshRequest> pending_requests;
    std::deque<MapRhiTerrainMeshResult> completed_results;
    bool shutting_down = false;
};

#endif // MAP_RHI_TERRAIN_MESH_SCHEDULER_H
