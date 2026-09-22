#ifndef MAP_GLOBE_SURFACE_PREPARATION_H
#define MAP_GLOBE_SURFACE_PREPARATION_H

#include "geo/geo_wgs84_ellipsoid.h"
#include "map/render/map_globe_surface_render_frame.h"
#include "map/render/map_globe_surface_scene.h"

#include <aowis/model/gis.h>

#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVector>
#include <QVector3D>

#include <memory>

class MapModel;
class MapTerrainRepository;
class MapTerrainMeshScheduler;
enum class MapTerrainVerticalDatum;

struct MapGlobeSurfaceTerrainRayRowBounds
{
    QVector3D minimum;
    QVector3D maximum;
};

// Backend-neutral retained state for one visible Globe surface tile. This is
// CPU preparation state only: there are deliberately no texture handles,
// descriptor/binding objects, array-page assignments, or backend resource
// pointers here.
struct MapGlobeSurfaceTile
{
    int surface_tile_index = -1;
    int virtual_x = 0;
    int tile_x = 0;
    int tile_y = 0;
    int zoom = 0;
    bool is_cap = false;

    int first_vertex = 0;
    int vertex_count = 0;
    int first_index = 0;
    int index_count = 0;
    QString imagery_key;

    int terrain_zoom = -1;
    QString terrain_key;
    int terrain_cell_count = 1;
    int terrain_stitch_top_cell_count = 0;
    int terrain_stitch_right_cell_count = 0;
    int terrain_stitch_bottom_cell_count = 0;
    int terrain_stitch_left_cell_count = 0;
    quint64 terrain_mesh_request_id = 0;
    bool terrain_mesh_applied = false;
    bool terrain_mesh_has_relief = false;

    QVector3D terrain_ray_bounds_min;
    QVector3D terrain_ray_bounds_max;
    bool terrain_ray_bounds_valid = false;
    QVector<MapGlobeSurfaceTerrainRayRowBounds> terrain_ray_row_bounds;
};

struct MapGlobeSurfaceVertexPatch
{
    qsizetype first_vertex = 0;
    qsizetype vertex_count = 0;
};

// Owns all backend-independent preparation of the retained Globe surface:
// visible quadtree selection, ellipsoid/cap geometry, DEM LOD and stitching,
// asynchronous terrain mesh generation/application, wireframe generation,
// and exact retained-DEM picking. Concrete GPU backends consume the prepared
// vertices/indices/tiles and attach their own resource state separately.
class MapGlobeSurfacePreparation
{
public:
    explicit MapGlobeSurfacePreparation(MapModel *map_model);
    ~MapGlobeSurfacePreparation();

    bool setTerrainRepository(MapTerrainRepository *terrain_repository);
    MapTerrainRepository *terrainRepository() const;

    bool requestTerrainForCurrentView(const QSize &viewport_size);
    void invalidateTerrainView();
    bool setRenderOriginEcef(
        const GeoWgs84Ellipsoid::EcefPositionD &origin_ecef);
    void notifyTerrainTileAvailable(const QString &key);
    void invalidateTerrain();

    bool setWireframeVisible(bool visible);
    bool wireframeVisible() const;

    bool ensureCapsBuilt();
    bool prepareVisibleWindow(const QSize &viewport_size);
    bool applyReadyTerrainMeshes(
        QVector<MapGlobeSurfaceVertexPatch> *vertex_patches);
    void requestMissingTerrainTiles();
    void scheduleReadyTerrainMeshes();

    bool hasPendingTerrainMeshes() const;
    void terrainMeshProgress(int *completed, int *total, bool *active) const;

    bool visibleTerrainRayIntersection(
        const GeoWgs84Ellipsoid::EcefPositionD &ray_origin_ecef,
        const QVector3D &ray_direction_ecef,
        GeoWgs84Ellipsoid::EcefPositionD *intersection_ecef,
        double *distance_m) const;
    bool visibleTerrainSamplingAtCoordinate(
        const CoordinateWGS84 &coordinate,
        int *terrain_zoom,
        double *cell_size_m) const;

    const GeoWgs84Ellipsoid::EcefPositionD &renderOriginEcef() const;
    const QVector<MapGlobeSurfaceVertex> &windowVertices() const;
    const QVector<quint32> &windowIndices() const;
    const QVector<MapGlobeSurfaceTile> &windowTiles() const;
    QVector<MapGlobeSurfaceTile> &windowTiles();
    const QSet<quint64> &windowPositionKeys() const;
    bool windowDirty() const;

    const QVector<MapGlobeSurfaceVertex> &capVertices() const;
    const QVector<quint32> &capIndices() const;
    const QVector<MapGlobeSurfaceTile> &capTiles() const;
    QVector<MapGlobeSurfaceTile> &capTiles();

    const QVector<MapGlobeSurfaceWireframeVertex> &wireframeVertices() const;

    void refreshRenderFrame(
        MapGlobeSurfaceRenderFrame *frame,
        const QSize &viewport_size, bool map_visible,
        float heatmap_opacity, quint64 heatmap_revision,
        quint64 heatmap_layout_revision,
        int active_heatmap_marker_count) const;
    void rebuildRenderFrame(
        MapGlobeSurfaceRenderFrame *frame,
        const QSize &viewport_size, bool map_visible,
        float heatmap_opacity, quint64 heatmap_revision,
        quint64 heatmap_layout_revision,
        int active_heatmap_marker_count) const;

private:
    MapGlobeSurfaceVertex makeTileVertex(
        double lon_deg, double lat_deg, float u, float v) const;
    void buildPolarCap(bool north);
    void rebuildWindow(
        const QVector<MapGlobeQuadtreeLeaf> &leaves,
        const QSize &viewport_size);
    QVector<MapGlobeQuadtreeLeaf> currentWindowLeaves() const;
    int terrainCellCountForTile(
        const MapGlobeSurfaceTile &tile,
        const QSize &viewport_size,
        const GeoWgs84Ellipsoid::OrbitCameraBasis *camera_basis_override = nullptr) const;
    void updateTerrainStitchCellCounts(
        QVector<MapGlobeSurfaceTile> *tiles) const;
    void updateTerrainRayBounds(MapGlobeSurfaceTile *tile);
    bool currentTerrainLodMatches(const QSize &viewport_size) const;
    void appendWireframeEdges(
        const QVector<MapGlobeSurfaceVertex> &vertices,
        const QVector<quint32> &indices);
    void rebuildWireframeVertices();
    bool viewSelectionMatches(const QSize &viewport_size) const;
    void rememberViewSelection(const QSize &viewport_size);

    MapModel *map_model = nullptr;
    MapTerrainRepository *terrain_repository = nullptr;
    GeoWgs84Ellipsoid::EcefPositionD render_origin_ecef;

    QVector<MapGlobeSurfaceVertex> window_vertices;
    QVector<quint32> window_indices;
    QVector<MapGlobeSurfaceTile> window_tiles;
    QSet<quint64> window_position_keys;
    QHash<quint64, qsizetype> window_tile_indices_by_position;
    bool window_dirty = true;
    MapGlobeSurfaceScene surface_scene;

    QVector<MapGlobeSurfaceWireframeVertex> wireframe_vertices;
    bool wireframe_visible = false;

    QVector<MapGlobeSurfaceVertex> cap_vertices;
    QVector<quint32> cap_indices;
    QVector<MapGlobeSurfaceTile> cap_tiles;
    bool caps_built = false;

    std::unique_ptr<MapTerrainMeshScheduler> terrain_mesh_scheduler;
    quint64 next_terrain_mesh_request_id = 1;
    bool reported_orthometric_datum_warning = false;
    bool reported_unusable_datum_warning = false;
    QElapsedTimer terrain_lod_rebuild_clock;
    bool terrain_lod_rebuild_pending = false;

    bool prepared_view_state_valid = false;
    QSize prepared_viewport_size;
    double prepared_center_lon_deg = 0.0;
    double prepared_center_lat_deg = 0.0;
    double prepared_yaw_deg = 0.0;
    double prepared_pitch_deg = 0.0;
    double prepared_distance_m = 0.0;
};

int mapGlobeSurfaceTileRequestPriority(
    int tile_x, int tile_y, int zoom,
    double center_lon_deg, double center_lat_deg);
QString mapGlobeTerrainDatasetId();
bool mapGlobeTerrainDatumUsable(MapTerrainVerticalDatum datum);

#endif // MAP_GLOBE_SURFACE_PREPARATION_H
