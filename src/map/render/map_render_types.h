#ifndef MAP_RENDER_TYPES_H
#define MAP_RENDER_TYPES_H

// Small value types of the map renderer (picking results, globe surface
// hits, the underground display mode).

#include "common/_enums_structs.h"
#include "geo/geo_wgs84_ellipsoid.h"

#include <aowis/model/gis.h>

#include <QUuid>

enum class MapUndergroundMode
{
    XRay,
    Hide,
    Solid
};

struct MapRenderHit
{
    quint32 render_id = 0;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    QUuid uuid;

    bool isValid() const
    {
        return this->render_id != 0
            && this->entity_type != InfrastructureEntity::Unknown;
    }
};

enum class MapGlobeSurfaceHitSource
{
    Invalid,
    TerrainMesh,
    EllipsoidFallback
};

struct MapGlobeSurfaceHit
{
    CoordinateWGS84 coordinate;
    GeoWgs84Ellipsoid::EcefPositionD ecef_position;
    double surface_height_m = 0.0;
    double distance_m = 0.0;
    MapGlobeSurfaceHitSource source = MapGlobeSurfaceHitSource::Invalid;

    bool isValid() const
    {
        return this->source != MapGlobeSurfaceHitSource::Invalid;
    }

    bool isTerrainMesh() const
    {
        return this->source == MapGlobeSurfaceHitSource::TerrainMesh;
    }
};

#endif // MAP_RENDER_TYPES_H
