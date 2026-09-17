#ifndef MAP_RHI_JUNCTION_MODEL_H
#define MAP_RHI_JUNCTION_MODEL_H

#include "map/render/map_globe_render_instances.h"

#include <QVector>
#include <QtGlobal>

struct MapRhiJunctionImpostorVertex
{
    float corner_x = 0.0f;
    float corner_y = 0.0f;
};

using MapRhiJunctionInstance = MapGlobeJunctionInstance;

static_assert(sizeof(MapRhiJunctionInstance) == 24,
              "Junction impostor instances must remain compact");

const QVector<MapRhiJunctionImpostorVertex> &mapRhiJunctionImpostorVertices();

#endif // MAP_RHI_JUNCTION_MODEL_H
