#ifndef MAP_EDITOR_VISUAL_STATE_H
#define MAP_EDITOR_VISUAL_STATE_H

#include <QHash>
#include <QList>
#include <QPointF>
#include <QRect>
#include <QSet>
#include <QString>
#include <QUuid>
#include <QtGlobal>

#include "map/core/map_models.h"

struct MapEditorPlacementVisualState
{
    bool creating = false;
    bool floating_marker_visible = false;
    InfrastructureEntity entity = InfrastructureEntity::Unknown;
    QPointF mouse_position;
    CoordinateWGS84 mouse_coordinate_wgs84;
    bool mouse_coordinate_wgs84_valid = false;
    QUuid connection_target_uuid;
    QUuid pipe_start_node_uuid;
    QList<CoordinateWGS84> pipe_intermediate_vertices;
    QUuid device_link_start_node_uuid;
    int floating_width = 0;
};


struct MapEditorDynamicMarkerVisualState
{
    InfrastructureEntity entity = InfrastructureEntity::Unknown;
    QUuid uuid;
    CoordinateWGS84 coordinate_wgs84;
    QString pixmap_path;
};

struct MapEditorDynamicLinkVisualState
{
    InfrastructureEntity entity = InfrastructureEntity::Unknown;
    QUuid uuid;
    QList<CoordinateWGS84> vertices_wgs84;
};

struct MapEditorMoveVisualState
{
    bool active = false;
    quint64 session_id = 0;
    QList<MapEditorDynamicMarkerVisualState> markers;
    QList<MapEditorDynamicLinkVisualState> links;
};

struct MapEditorVisualState
{
    quint64 revision = 0;
    QList<QUuid> selected_marker_uuids;
    QList<QUuid> selected_pipe_uuids;
    QHash<QUuid, InfrastructureEntity> simulation_error_entities;
    QSet<QUuid> simulation_stale_diagnostic_entity_uuids;
    MapEditorPlacementVisualState placement;
    MapEditorMoveVisualState move;
    double wrap_reference_longitude = 0.0;
    int entity_width = 10;
};

struct MapEditorViewportRenderState
{
    int background_opacity = 0;

    bool tile_selection_visible = false;
    int tile_x_min = 0;
    int tile_x_max = -1;
    int tile_y_min = 0;
    int tile_y_max = -1;

    bool rectangle_selection_visible = false;
    QRect rectangle_selection;
    CoordinateWGS84 rectangle_selection_north_west;
    CoordinateWGS84 rectangle_selection_south_east;
    bool rectangle_selection_wgs84_valid = false;
};

#endif // MAP_EDITOR_VISUAL_STATE_H
