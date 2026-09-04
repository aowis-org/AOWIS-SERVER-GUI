#ifndef MAP_RHI_TERRAIN_MESH_SCHEDULER_H
#define MAP_RHI_TERRAIN_MESH_SCHEDULER_H

#include "map/data/map_terrain_tile.h"

#include <QMutex>
#include <QString>
#include <QThread>
#include <QVector>
#include <QWaitCondition>

#include <deque>

// A single relief-mesh vertex, field-for-field identical to
// MapRhiBasemapRenderer::TileVertex. Kept as an independent type so this
// header has no dependency on map_rhi_basemap_renderer.h (and vice versa,
// avoiding a circular include); the renderer converts these to its own
// TileVertex when a result is applied.
enum class MapRhiTerrainMeshGeometry
{
    FlatWorld,
    GlobeEcef
};

struct MapRhiTerrainMeshVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

// Everything needed to build one tile's relief mesh, carried entirely by
// value so the background thread never touches a live, main-thread-owned
// object (MapTerrainRepository, MapRhiScene, MapModel, or any QRhi
// resource).
//
// terrain_tile is a value copy of the cached DEM tile. Copying it (on the
// main thread, before submitting) is safe per Qt's implicit-sharing /
// copy-on-write thread-safety guarantee: two independent copies of a
// QVector may be read/written from two different threads concurrently,
// because a write on either side detaches (deep-copies) before mutating
// rather than touching data the other side might still be reading.
//
// Flat RHI 3D uses an affine elevation(m)->world-Z conversion that depends
// on live MapRhiScene/MapModel state (vertical exaggeration, reference
// latitude, etc.), so instead of shipping a pointer to either object, the two
// coefficients of that affine function are precomputed on the main thread and
// shipped as plain floats. Globe requests use the same copied DEM data but
// generate geodetic WGS84/ECEF vertices directly, again without touching live
// GUI/render objects from the worker thread.
struct MapRhiTerrainMeshRequest
{
    quint64 request_id = 0;
    QString terrain_key;
    MapTerrainTile terrain_tile;
    bool terrain_available = false;
    // Identifies which VisibleTile in the apron this request/result belongs
    // to. virtual_x (not the wrapped tile_x) matches how rebuildVisibleTiles
    // itself distinguishes tiles, since tile_x wraps at the antimeridian and
    // so is not by itself unique within the apron. This -- not terrain_key
    // alone -- is what applyReadyTerrainMeshResultsToMemory() uses to place
    // a finished result back into the right buffer slot: terrain_key alone
    // can be shared by several tiles (one DEM tile backs multiple imagery
    // tiles whenever terrain zoom trails imagery zoom), and a result's
    // vertices carry the world position of the one tile it was built for.
    int virtual_x = 0;
    int tile_x = 0;
    int y = 0;
    int imagery_zoom = 0;
    int terrain_zoom = 0;
    // The LOD-selected desired cell count for this tile (VisibleTile's
    // terrain_cell_count); the actual resolved count -- after clamping to
    // what the DEM's own resolution and the imagery/terrain zoom delta
    // allow -- is echoed back in MapRhiTerrainMeshResult::cell_count.
    int requested_cell_count = 1;
    // Crack-free terrain LOD stitching. Each value is the number of edge
    // segments used by a coarser, same-zoom neighboring tile on that side,
    // or 0 when no stitching is needed. The fine tile keeps its interior
    // density, but boundary vertices are constrained to the coarser edge
    // polyline so both meshes meet exactly instead of forming T-junction
    // cracks.
    int stitch_top_cell_count = 0;
    int stitch_right_cell_count = 0;
    int stitch_bottom_cell_count = 0;
    int stitch_left_cell_count = 0;
    float tile_left = 0.0f;
    float tile_top = 0.0f;
    float tile_right = 0.0f;
    float tile_bottom = 0.0f;
    float tile_world_size = 0.0f;
    float elevation_world_z_offset = 0.0f;
    float elevation_world_z_scale = 0.0f;

    // Flat RHI 3D keeps the existing world-X/Y + world-Z geometry. Globe
    // requests instead generate geodetic WGS84 positions directly in ECEF.
    MapRhiTerrainMeshGeometry geometry = MapRhiTerrainMeshGeometry::FlatWorld;
    double globe_vertical_exaggeration = 1.0;
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

// Builds the relief vertex grid for one tile. A pure function -- reads only
// its argument, touches no live object -- so it is safe to call from any
// thread. Defined in map_rhi_basemap_renderer.cpp, where it can share the
// existing terrain-sampling helpers (bilinear DEM sampling with a
// nearest-finite-sample fallback) instead of duplicating that math here.
MapRhiTerrainMeshResult buildTerrainMeshResult(const MapRhiTerrainMeshRequest &request);

// Runs buildTerrainMeshResult() on a single dedicated background thread so
// DEM resampling for a newly-needed tile never blocks the render thread's
// frame budget. Deliberately not built on Qt's cross-thread signal/slot
// (which would need QMetaType registration for these custom value types);
// a plain mutex/condition-variable queue in both directions keeps the
// threading surface small enough to verify by inspection.
//
// submit() and collectReady() are safe to call only from the thread that
// constructed this scheduler (the render/GUI thread, matching how
// MapRhiBasemapRenderer uses it) -- they are not meant to be called
// concurrently from multiple producer threads.
class MapRhiTerrainMeshScheduler : public QThread
{
public:
    MapRhiTerrainMeshScheduler();
    ~MapRhiTerrainMeshScheduler() override;

    void submit(const MapRhiTerrainMeshRequest &request);

    // Moves every result completed since the last call into *results.
    // Never blocks the caller on the worker thread's progress.
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
