#include "mercator.h"

namespace Mercator
{
    // Convert longitude to tile X index at a given zoom level
    double lonToTileX(double lon, int zoom)
    {
        return (lon + 180.0) / 360.0 * (1 << zoom);
    }
    
    // Convert latitude to tile Y index at a given zoom level
    double latToTileY(double lat, int zoom)
    {
        // Clamp latitude to Mercator limits
        if (lat > 85.05112878)
            lat = 85.05112878;
        if (lat < -85.05112878)
            lat = -85.05112878;
        
        double rad = qDegreesToRadians(lat);
        double n = qLn(qTan(M_PI / 4.0 + rad / 2.0));
        
        return (1.0 - n / M_PI) / 2.0 * (1 << zoom);
    }
    
    // Convert tile X index back to longitude
    double tileXToLon(double x, int zoom)
    {
        return x / (1 << zoom) * 360.0 - 180.0;
    }
    
    // Convert tile Y index back to latitude
    double tileYToLat(double y, int zoom)
    {
        double n = M_PI - 2.0 * M_PI * y / (1 << zoom);
        return qRadiansToDegrees(qAtan(std::sinh(n)));
    }
    
    // Convert lat/lon to pixel coordinates inside the global map
    QPointF latLonToPixel(double lat, double lon, int zoom)
    {
        double x = lonToTileX(lon, zoom) * 256.0;
        double y = latToTileY(lat, zoom) * 256.0;
        return QPointF(x, y);
    }
    
    // Convert pixel coordinates back to lat/lon
    QPointF pixelToLatLon(double px, double py, int zoom)
    {
        double tileX = px / 256.0;
        double tileY = py / 256.0;
        
        double lon = tileXToLon(tileX, zoom);
        double lat = tileYToLat(tileY, zoom);
        
        return QPointF(lat, lon);
    }
}
