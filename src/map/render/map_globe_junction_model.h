#ifndef MAP_GLOBE_JUNCTION_MODEL_H
#define MAP_GLOBE_JUNCTION_MODEL_H

#include "map/render/map_globe_render_instances.h"

#include <QVector>
#include <QtGlobal>

struct MapGlobeJunctionImpostorVertex
{
    float corner_x = 0.0f;
    float corner_y = 0.0f;
};

static_assert(sizeof(MapGlobeJunctionInstance) == 24,
              "Junction impostor instances must remain compact");

const QVector<MapGlobeJunctionImpostorVertex> &mapGlobeJunctionImpostorVertices();

#endif // MAP_GLOBE_JUNCTION_MODEL_H
