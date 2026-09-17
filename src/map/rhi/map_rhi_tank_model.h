#ifndef MAP_RHI_TANK_MODEL_H
#define MAP_RHI_TANK_MODEL_H

#include "map/render/map_globe_render_instances.h"

#include <QImage>
#include <QtGlobal>
#include <QVector>
#include <QVector3D>

using MapRhiTankInstance = MapGlobeTankInstance;

struct MapRhiTankModelVertex
{
    float position_x = 0.0f;
    float position_y = 0.0f;
    float position_z = 0.0f;
    float normal_x = 0.0f;
    float normal_y = 0.0f;
    float normal_z = 1.0f;
    float u = 0.0f;
    float v = 0.0f;
    float selected = 0.0f;
    quint32 render_id = 0;
};

QImage mapRhiTankAlbedoImage();
QVector<MapRhiTankModelVertex> mapRhiBuildTankModelVertices(
    const QVector<MapRhiTankInstance> &instances);

#endif // MAP_RHI_TANK_MODEL_H
