#ifndef MAP_RHI_RESERVOIR_MODEL_H
#define MAP_RHI_RESERVOIR_MODEL_H

#include <QImage>
#include <QtGlobal>
#include <QVector>
#include <QVector3D>

struct MapRhiReservoirInstance
{
    quint32 render_id = 0;
    QVector3D base_center;
    float radius_world = 0.0f;
    float wall_height_world = 0.0f;
    float selected = 0.0f;
};

struct MapRhiReservoirModelVertex
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

QImage mapRhiReservoirAlbedoImage();
QVector<MapRhiReservoirModelVertex> mapRhiBuildReservoirModelVertices(
    const QVector<MapRhiReservoirInstance> &instances);

#endif // MAP_RHI_RESERVOIR_MODEL_H
