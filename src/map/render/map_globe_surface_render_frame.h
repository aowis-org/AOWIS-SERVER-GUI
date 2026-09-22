#ifndef MAP_GLOBE_SURFACE_RENDER_FRAME_H
#define MAP_GLOBE_SURFACE_RENDER_FRAME_H

#include "geo/geo_wgs84_ellipsoid.h"

#include <QSize>
#include <QString>
#include <QVector>
#include <QtGlobal>

struct MapGlobeSurfaceVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

struct MapGlobeSurfaceWireframeVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct MapGlobeSurfaceTileRenderState
{
    // Transient index into the prepared surface tile list. The geographic
    // identity below remains the stable key across frames/backends.
    int surface_tile_index = -1;
    quint64 position_key = 0;
    int virtual_x = 0;
    int tile_x = 0;
    int tile_y = 0;
    int zoom = 0;
    bool is_cap = false;
    QString imagery_key;

    int first_vertex = 0;
    int vertex_count = 0;
    int first_index = 0;
    int index_count = 0;

    int terrain_zoom = -1;
    QString terrain_key;
    int terrain_cell_count = 1;
    int terrain_stitch_top_cell_count = 0;
    int terrain_stitch_right_cell_count = 0;
    int terrain_stitch_bottom_cell_count = 0;
    int terrain_stitch_left_cell_count = 0;
    bool terrain_mesh_applied = false;
    bool terrain_mesh_has_relief = false;
};

struct MapGlobeSurfaceRenderResources
{
    const QVector<MapGlobeSurfaceVertex> *window_vertices = nullptr;
    const QVector<quint32> *window_indices = nullptr;
    const QVector<MapGlobeSurfaceVertex> *cap_vertices = nullptr;
    const QVector<quint32> *cap_indices = nullptr;
    const QVector<MapGlobeSurfaceWireframeVertex> *wireframe_vertices = nullptr;

    QVector<MapGlobeSurfaceTileRenderState> window_tiles;
    QVector<MapGlobeSurfaceTileRenderState> cap_tiles;

    bool isValid() const;
};

// Backend-neutral retained surface packet produced after Globe surface
// preparation. It deliberately contains no GPU resource handles, pipelines,
// samplers, texture-array page ownership, or command-buffer types. A concrete
// renderer backend maps the stable geometry/tile state below onto its own GPU
// resources.
struct MapGlobeSurfaceRenderFrame
{
    QSize viewport_size;
    GeoWgs84Ellipsoid::EcefPositionD render_origin_ecef;
    bool map_visible = true;
    bool wireframe_visible = false;
    float heatmap_opacity = 0.0f;
    quint64 heatmap_revision = 0;
    quint64 heatmap_layout_revision = 0;
    int active_heatmap_marker_count = 0;
    MapGlobeSurfaceRenderResources resources;

    bool isValid() const;
};

#endif // MAP_GLOBE_SURFACE_RENDER_FRAME_H
