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
    float center_x = 0.0f;
    float center_y = 0.0f;
    float center_z = 0.0f;
    float radius_world = 0.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 1.0f;
    float selected = 0.0f;
};

const QVector<MapRhiJunctionImpostorVertex> &mapRhiJunctionImpostorVertices();

#endif // MAP_RHI_JUNCTION_MODEL_H
