#ifndef MAP_RHI_JUNCTION_MODEL_H
#define MAP_RHI_JUNCTION_MODEL_H

#include <QVector>
#include <QtGlobal>

struct MapRhiJunctionImpostorVertex
{
    float corner_x = 0.0f;
    float corner_y = 0.0f;
};

struct MapRhiJunctionInstance
{
    quint32 render_id = 0;
    // Kept as a float because the shader bundle includes GLSL/ESSL 100,
    // whose legacy targets do not support unsigned integer attributes.
    float style_index = 0.0f;
    float center_x = 0.0f;
    float center_y = 0.0f;
    float center_z = 0.0f;
    float radius_world = 0.0f;
};

static_assert(sizeof(MapRhiJunctionInstance) == 24,
              "Junction impostor instances must remain compact");

const QVector<MapRhiJunctionImpostorVertex> &mapRhiJunctionImpostorVertices();

#endif // MAP_RHI_JUNCTION_MODEL_H
