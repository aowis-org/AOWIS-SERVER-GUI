#include "geo/geo_wgs84_ellipsoid.h"

#include <GeographicLib/Geocentric.hpp>

#include <vector>

QVector3D GeoWgs84Ellipsoid::geodeticToEcef(double lon_deg, double lat_deg, double height_m)
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    GeographicLib::Geocentric::WGS84().Forward(lat_deg, lon_deg, height_m, x, y, z);
    return QVector3D(float(x), float(y), float(z));
}

GeoWgs84Ellipsoid::LocalFrame GeoWgs84Ellipsoid::localFrameAtGeodetic(
    double lon_deg, double lat_deg, double height_m)
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    std::vector<double> rotation(9, 0.0);
    GeographicLib::Geocentric::WGS84().Forward(
        lat_deg, lon_deg, height_m, x, y, z, rotation);

    LocalFrame frame;
    frame.position = QVector3D(float(x), float(y), float(z));

    // GeographicLib::Geocentric::Forward documents the rotation matrix M as
    // row-major with ecef = M * [east, north, up]^T, i.e. column j of M is
    // the ECEF direction of local unit axis j (0 = east, 1 = north, 2 = up).
    frame.east = QVector3D(float(rotation[0]), float(rotation[3]), float(rotation[6]));
    frame.north = QVector3D(float(rotation[1]), float(rotation[4]), float(rotation[7]));
    frame.up = QVector3D(float(rotation[2]), float(rotation[5]), float(rotation[8]));
    return frame;
}
