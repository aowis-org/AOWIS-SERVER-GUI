#ifndef MAP_GLOBE_SURFACE_SCENE_H
#define MAP_GLOBE_SURFACE_SCENE_H

#include "geo/geo_wgs84_ellipsoid.h"

#include <QSet>
#include <QSize>
#include <QVector>
#include <QtGlobal>

class MapModel;
class MapTerrainRepository;

struct MapGlobeQuadtreeLeaf
{
    int zoom = 0;
    int tile_x = 0;
    int tile_y = 0;
};

// Backend-neutral terrain LOD/stitch state for one visible Globe imagery leaf.
// The renderer may keep additional GPU/cache metadata alongside this record,
// but mesh density and neighbor stitching are surface-scene decisions.
struct MapGlobeTerrainLodTile
{
    int virtual_x = 0;
    int tile_x = 0;
    int tile_y = 0;
    int zoom = 0;
    int terrain_zoom = -1;
    bool terrain_available = false;
    int terrain_cell_count = 1;
    int terrain_stitch_top_cell_count = 0;
    int terrain_stitch_right_cell_count = 0;
    int terrain_stitch_bottom_cell_count = 0;
    int terrain_stitch_left_cell_count = 0;
};

// Backend-neutral visible-surface decision layer. It owns the quadtree
// hysteresis that persists between frames and contains all policy for which
// imagery leaves are visible, how dense their terrain mesh should be, and how
// mismatched neighboring terrain grids must be stitched. No GPU resources or
// renderer-backend types live here.
class MapGlobeSurfaceScene
{
public:
    static constexpr int MaximumLeafCount = 3000;
    static constexpr int TerrainReliefMinimumZoom = 8;

    QVector<MapGlobeQuadtreeLeaf> selectVisibleLeaves(
        const MapModel &map_model,
        const QSize &viewport_size,
        const MapTerrainRepository *terrain_repository);

    void clearVisibilityHistory();

    int terrainCellCountForTile(
        const MapModel &map_model,
        const MapGlobeTerrainLodTile &tile,
        const QSize &viewport_size,
        const GeoWgs84Ellipsoid::OrbitCameraBasis *camera_basis_override = nullptr) const;

    bool terrainLodMatches(
        const MapModel &map_model,
        const QVector<MapGlobeTerrainLodTile> &tiles,
        const QSize &viewport_size) const;

    static void updateTerrainStitchCellCounts(
        QVector<MapGlobeTerrainLodTile> *tiles);

    static quint64 positionKey(int zoom, int tile_x, int tile_y);

private:
    QSet<quint64> previously_subdivided_quadtree_nodes;
};

#endif // MAP_GLOBE_SURFACE_SCENE_H
