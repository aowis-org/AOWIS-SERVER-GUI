#ifndef MAP_EDITOR_VISUAL_STATE_H
#define MAP_EDITOR_VISUAL_STATE_H

#include <QList>
#include <QPointF>
#include <QRect>
#include <QUuid>
#include <QtGlobal>

#include "map_models.h"

struct MapEditorPlacementVisualState
{
    bool creating = false;
    bool floating_marker_visible = false;
    InfrastructureEntity entity = InfrastructureEntity::Unknown;
    QPointF mouse_position;
    QUuid connection_target_uuid;
    QUuid pipe_start_node_uuid;
    QList<CoordinateWGS84> pipe_intermediate_vertices;
    QUuid device_link_start_node_uuid;
    int floating_width = 0;
};

struct MapEditorVisualState
{
    quint64 revision = 0;
    QList<QUuid> selected_marker_uuids;
    QList<QUuid> selected_pipe_uuids;
    MapEditorPlacementVisualState placement;
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
};

#endif // MAP_EDITOR_VISUAL_STATE_H
