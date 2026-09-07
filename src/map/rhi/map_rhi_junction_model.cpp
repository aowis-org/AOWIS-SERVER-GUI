#include "map/rhi/map_rhi_junction_model.h"

namespace
{
QVector<MapRhiJunctionImpostorVertex> buildImpostorVertices()
{
    QVector<MapRhiJunctionImpostorVertex> vertices;
    vertices.reserve(6);
    vertices.append({-1.0f, -1.0f});
    vertices.append({1.0f, -1.0f});
    vertices.append({1.0f, 1.0f});
    vertices.append({-1.0f, -1.0f});
    vertices.append({1.0f, 1.0f});
    vertices.append({-1.0f, 1.0f});
    return vertices;
}
}

const QVector<MapRhiJunctionImpostorVertex> &mapRhiJunctionImpostorVertices()
{
    static const QVector<MapRhiJunctionImpostorVertex> vertices = buildImpostorVertices();
    return vertices;
}
