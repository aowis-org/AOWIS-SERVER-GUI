#include "map/render/map_globe_junction_model.h"

namespace
{
QVector<MapGlobeJunctionImpostorVertex> buildImpostorVertices()
{
    QVector<MapGlobeJunctionImpostorVertex> vertices;
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

const QVector<MapGlobeJunctionImpostorVertex> &mapGlobeJunctionImpostorVertices()
{
    static const QVector<MapGlobeJunctionImpostorVertex> vertices = buildImpostorVertices();
    return vertices;
}
