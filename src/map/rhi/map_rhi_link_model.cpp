#include "map/rhi/map_rhi_link_model.h"

namespace
{
QVector<MapRhiLinkImpostorVertex> buildImpostorVertices()
{
    QVector<MapRhiLinkImpostorVertex> vertices;
    vertices.reserve(6);
    vertices.append({0.0f, -1.0f});
    vertices.append({1.0f, -1.0f});
    vertices.append({1.0f, 1.0f});
    vertices.append({0.0f, -1.0f});
    vertices.append({1.0f, 1.0f});
    vertices.append({0.0f, 1.0f});
    return vertices;
}
}

const QVector<MapRhiLinkImpostorVertex> &mapRhiLinkImpostorVertices()
{
    static const QVector<MapRhiLinkImpostorVertex> vertices = buildImpostorVertices();
    return vertices;
}
