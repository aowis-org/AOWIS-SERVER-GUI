#include "geo_web_mercator.h"


#include <algorithm>
#include <cmath>

#include <QtMath>

double GeoWebMercator::lonToTileX(double lon, int zoom)
{
    return (lon + 180.0) / 360.0 * double(1 << zoom);
}

double GeoWebMercator::latToTileY(double lat, int zoom)
{
    const double latClamped = std::clamp(lat, -85.05112878, 85.05112878);
    const double rad = qDegreesToRadians(latClamped);
    
    return (1.0 - std::log(std::tan(rad) + 1.0 / std::cos(rad)) / M_PI)
           / 2.0 * double(1 << zoom);
}

double GeoWebMercator::tileXToLon(double x, int zoom)
{
    return x / double(1 << zoom) * 360.0 - 180.0;
}

double GeoWebMercator::tileYToLat(double y, int zoom)
{
    const double n = M_PI - 2.0 * M_PI * y / double(1 << zoom);
    return qRadiansToDegrees(std::atan(std::sinh(n)));
}

QPointF GeoWebMercator::lonLatToWorldPixel(double lon, double lat, int zoom)
{
    return QPointF(
        lonToTileX(lon, zoom) * TileSize,
        latToTileY(lat, zoom) * TileSize
    );
}

QPointF GeoWebMercator::worldPixelToLonLat(double pixelX, double pixelY, int zoom)
{
    return QPointF(
        tileXToLon(pixelX / TileSize, zoom),
        tileYToLat(pixelY / TileSize, zoom)
    );
}

