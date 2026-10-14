#ifndef MAP_CACHED_TERRAIN_ALTITUDE_H
#define MAP_CACHED_TERRAIN_ALTITUDE_H

#include <aowis/model/gis.h>

#include <cmath>
#include <functional>

// Resolve height at the same WGS84 coordinate as the 2D map readout.
// The lookup only consults an existing terrain cache; it must not fetch tiles.
using MapCachedTerrainElevationLookup =
    std::function<bool(const CoordinateWGS84 &coordinate, double *elevation_m)>;

[[nodiscard]] inline CoordinateWGS84 mapWithCachedTerrainAltitude(
    const CoordinateWGS84 &coordinate, const MapCachedTerrainElevationLookup &lookup)
{
    CoordinateWGS84 result = coordinate;
    result.altitude_m.reset();
    if (lookup)
    {
        double elevation_m = 0.0;
        if (lookup(coordinate, &elevation_m) && std::isfinite(elevation_m))
            result.altitude_m = elevation_m;
    }
    return result;
}

#endif // MAP_CACHED_TERRAIN_ALTITUDE_H
