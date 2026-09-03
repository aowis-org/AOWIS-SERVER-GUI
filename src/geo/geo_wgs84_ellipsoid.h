#ifndef GEO_WGS84_ELLIPSOID_H
#define GEO_WGS84_ELLIPSOID_H

#include <QVector3D>

// Converts geodetic coordinates (longitude/latitude/height above the WGS84
// ellipsoid) into earth-centered, earth-fixed (ECEF) coordinates, for the
// "Globe" map view mode, which renders planet Earth as its actual WGS84
// ellipsoid shape rather than the flat Web Mercator plane used by the 2D/3D
// view modes.
//
// The conversion itself is delegated to GeographicLib::Geocentric, which is
// already vendored and linked into this project (see geo_metric_projection.h
// for the existing UTM use of GeographicLib). Hand-rolling the geodetic ->
// ECEF formulas here would just be a worse-tested reimplementation of what
// GeographicLib already provides, including the local east/north/up frame
// used by the globe orbit camera below.
class GeoWgs84Ellipsoid
{
public:
    GeoWgs84Ellipsoid() = delete;

    // WGS84 semi-major axis (equatorial radius), in meters. Only used here
    // for camera framing (near/far clip planes, default orbit distance);
    // the actual vertex geometry always goes through GeographicLib, which
    // carries the full ellipsoid (including flattening) itself.
    static constexpr double EquatorialRadiusM = 6378137.0;

    // A point on (or above) the ellipsoid together with the local
    // east/north/up unit basis at that point, all expressed in ECEF meters.
    // "position" is the ECEF coordinate itself; "east"/"north"/"up" are unit
    // directions, not positions.
    struct LocalFrame
    {
        QVector3D position;
        QVector3D east;
        QVector3D north;
        QVector3D up;
    };

    // Converts geodetic coordinates to ECEF meters. height_m is height above
    // the WGS84 ellipsoid (0 for a point on the surface).
    static QVector3D geodeticToEcef(double lon_deg, double lat_deg, double height_m);

    // Same conversion, but also returns the local east/north/up frame at
    // that point. Used by the globe orbit camera to build a yaw/pitch orbit
    // around an arbitrary lon/lat target without needing a separate
    // ellipsoid-normal derivation.
    static LocalFrame localFrameAtGeodetic(double lon_deg, double lat_deg, double height_m);
};

#endif // GEO_WGS84_ELLIPSOID_H
