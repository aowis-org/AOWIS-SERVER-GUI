#include "geo_metric_projection.h"

#include <GeographicLib/UTMUPS.hpp>
#include <GeographicLib/Geodesic.hpp>

#include <stdexcept>

GeoMetricProjection::GeoMetricProjection(QObject *parent)
    : QObject{parent}
{
    
}

void GeoMetricProjection::setOrigin(const CoordinateWGS84 &origin)
{
    m_originWgs84 = origin;
    m_originUtm = wgs84ToUtm(origin);
    m_hasOrigin = true;
    
    emit originChanged();
}

bool GeoMetricProjection::hasOrigin() const
{
    return m_hasOrigin;
}

CoordinateWGS84 GeoMetricProjection::originWgs84() const
{
    return m_originWgs84;
}

CoordinateUTM GeoMetricProjection::originUtm() const
{
    return m_originUtm;
}

CoordinateUTM GeoMetricProjection::wgs84ToUtm(const CoordinateWGS84 &coordinate)
{
    int zone = GeographicLib::UTMUPS::INVALID;
    bool northp = true;
    double easting = 0.0;
    double northing = 0.0;
    
    GeographicLib::UTMUPS::Forward(
        coordinate.lat,
        coordinate.lon,
        zone,
        northp,
        easting,
        northing
        );
    
    CoordinateUTM result;
    result.easting = easting;
    result.northing = northing;
    result.zone = zone;
    result.hemisphere_northern = northp;
    
    return result;
}

CoordinateWGS84 GeoMetricProjection::utmToWgs84(const CoordinateUTM &coordinate)
{
    double lat = 0.0;
    double lon = 0.0;
    
    GeographicLib::UTMUPS::Reverse(
        coordinate.zone,
        coordinate.hemisphere_northern,
        coordinate.easting,
        coordinate.northing,
        lat,
        lon
        );
    
    CoordinateWGS84 result;
    result.lon = lon;
    result.lat = lat;
    
    return result;
}

CoordinateLocal GeoMetricProjection::wgs84ToLocal(const CoordinateWGS84 &coordinate) const
{
    if (!m_hasOrigin)
    {
        throw std::logic_error("GeoMetricProjection origin has not been set.");
    }
    
    const CoordinateUTM utm = wgs84ToOriginZoneUtm(coordinate);
    
    CoordinateLocal result;
    result.x = utm.easting - m_originUtm.easting;
    result.y = utm.northing - m_originUtm.northing;
    
    return result;
}

CoordinateWGS84 GeoMetricProjection::localToWgs84(const CoordinateLocal &coordinate) const
{
    if (!m_hasOrigin)
    {
        throw std::logic_error("GeoMetricProjection origin has not been set.");
    }
    
    CoordinateUTM utm;
    utm.easting = m_originUtm.easting + coordinate.x;
    utm.northing = m_originUtm.northing + coordinate.y;
    utm.zone = m_originUtm.zone;
    utm.hemisphere_northern = m_originUtm.hemisphere_northern;
    
    return utmToWgs84(utm);
}

CoordinateUTM GeoMetricProjection::wgs84ToOriginZoneUtm(const CoordinateWGS84 &coordinate) const
{
    if (!m_hasOrigin)
    {
        throw std::logic_error("GeoMetricProjection origin has not been set.");
    }
    
    int zone = GeographicLib::UTMUPS::INVALID;
    bool northp = true;
    double easting = 0.0;
    double northing = 0.0;
    
    GeographicLib::UTMUPS::Forward(
        coordinate.lat,
        coordinate.lon,
        zone,
        northp,
        easting,
        northing,
        m_originUtm.zone
        );
    
    if (northp != m_originUtm.hemisphere_northern)
    {
        throw std::runtime_error(
            "Coordinate is in a different UTM hemisphere than the local origin."
            );
    }
    
    CoordinateUTM result;
    result.easting = easting;
    result.northing = northing;
    result.zone = zone;
    result.hemisphere_northern = northp;
    
    return result;
}

double GeoMetricProjection::distanceMeters(const CoordinateWGS84 &a,
                                           const CoordinateWGS84 &b)
{
    double distance = 0.0;
    
    GeographicLib::Geodesic::WGS84().Inverse(
        a.lat,
        a.lon,
        b.lat,
        b.lon,
        distance
        );
    
    return distance;
}

double GeoMetricProjection::azimuthDegrees(const CoordinateWGS84 &a,
                                           const CoordinateWGS84 &b)
{
    double distance = 0.0;
    double azimuth1 = 0.0;
    double azimuth2 = 0.0;
    
    GeographicLib::Geodesic::WGS84().Inverse(
        a.lat,
        a.lon,
        b.lat,
        b.lon,
        distance,
        azimuth1,
        azimuth2
        );
    
    return azimuth1;
}
