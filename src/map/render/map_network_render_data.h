#ifndef MAP_NETWORK_RENDER_DATA_H
#define MAP_NETWORK_RENDER_DATA_H

#include "common/_enums_structs.h"

#include <QtGlobal>

// Backend-neutral retained network vertex layouts. Keep field order and types
// stable: the current QRhi backend uploads these structs directly, and future
// GPU backends should be able to consume the same scene data unchanged.
struct MapNetworkLinkVertex
{
    float start_x = 0.0f;
    float start_y = 0.0f;
    float start_z = 0.0f;
    float end_x = 0.0f;
    float end_y = 0.0f;
    float end_z = 0.0f;
    float along = 0.0f;
    float side = 0.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 1.0f;
    float size_adjust_px = 0.0f;
    // Optional world-space unit direction used to measure metre-sized link
    // half-width. Flat 2D geometry leaves this zero so the shared shader
    // retains its XY-plane perpendicular; Globe geometry fills it with the
    // local WGS84 tangent direction.
    float width_direction_x = 0.0f;
    float width_direction_y = 0.0f;
    float width_direction_z = 0.0f;
    quint32 render_id = 0;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
};

struct MapNetworkIconVertex
{
    float center_x = 0.0f;
    float center_y = 0.0f;
    float center_z = 0.0f;
    float offset_x_ratio = 0.0f;
    float offset_y_ratio = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 1.0f;
    quint32 render_id = 0;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
};

struct MapNetworkHeatmapVertex
{
    float center_x = 0.0f;
    float center_y = 0.0f;
    float center_z = 0.0f;
    float corner_x = 0.0f;
    float corner_y = 0.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
};

struct MapNetworkNodeVertex
{
    float center_x = 0.0f;
    float center_y = 0.0f;
    float center_z = 0.0f;
    float corner_x = 0.0f;
    float corner_y = 0.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 1.0f;
    float size_adjust_px = 0.0f;
    // Globe generic-node quads are screen-facing billboards. When this flag
    // is set, metre sizing is measured along the camera-right world axis
    // instead of the flat renderer's global X axis, preserving true
    // perspective size without introducing an ECEF-axis dependency.
    float metric_billboard = 0.0f;
    quint32 render_id = 0;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
};

#endif // MAP_NETWORK_RENDER_DATA_H
