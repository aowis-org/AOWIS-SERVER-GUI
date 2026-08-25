#include "map_rhi_junction_model.h"

#include <QtMath>
#include <QVector3D>

#include <cmath>

namespace
{
constexpr int LongitudeSegments = 24;
constexpr int LatitudeSegments = 16;
constexpr float Pi = 3.14159265358979323846f;

MapRhiJunctionMeshVertex makeVertex(float latitude_angle, float longitude_angle)
{
    const float cos_latitude = std::cos(latitude_angle);
    const QVector3D normal(
        cos_latitude * std::cos(longitude_angle),
        cos_latitude * std::sin(longitude_angle),
        std::sin(latitude_angle));

    MapRhiJunctionMeshVertex vertex;
    vertex.position_x = normal.x();
    vertex.position_y = normal.y();
    vertex.position_z = normal.z();
    vertex.normal_x = normal.x();
    vertex.normal_y = normal.y();
    vertex.normal_z = normal.z();
    return vertex;
}

QVector<MapRhiJunctionMeshVertex> buildSphereMesh()
{
    QVector<MapRhiJunctionMeshVertex> vertices;
    vertices.reserve(LongitudeSegments * LatitudeSegments * 6);

    for (int latitude_index = 0; latitude_index < LatitudeSegments; ++latitude_index)
    {
        const float latitude0 = -Pi * 0.5f
            + Pi * float(latitude_index) / float(LatitudeSegments);
        const float latitude1 = -Pi * 0.5f
            + Pi * float(latitude_index + 1) / float(LatitudeSegments);

        for (int longitude_index = 0; longitude_index < LongitudeSegments; ++longitude_index)
        {
            const float longitude0 = 2.0f * Pi
                * float(longitude_index) / float(LongitudeSegments);
            const float longitude1 = 2.0f * Pi
                * float(longitude_index + 1) / float(LongitudeSegments);

            const MapRhiJunctionMeshVertex p00 = makeVertex(latitude0, longitude0);
            const MapRhiJunctionMeshVertex p01 = makeVertex(latitude0, longitude1);
            const MapRhiJunctionMeshVertex p11 = makeVertex(latitude1, longitude1);
            const MapRhiJunctionMeshVertex p10 = makeVertex(latitude1, longitude0);

            vertices.append(p00);
            vertices.append(p01);
            vertices.append(p11);
            vertices.append(p00);
            vertices.append(p11);
            vertices.append(p10);
        }
    }

    return vertices;
}
}

const QVector<MapRhiJunctionMeshVertex> &mapRhiJunctionSphereMeshVertices()
{
    static const QVector<MapRhiJunctionMeshVertex> vertices = buildSphereMesh();
    return vertices;
}
