#ifndef GEO_WEB_MERCATOR_H
#define GEO_WEB_MERCATOR_H

#include <QPointF>

#include "_enums_structs.h"

class GeoWebMercator
{
public:
    static constexpr int TileSize = 256;
    
    GeoWebMercator() = delete;
    
    static double lonToTileX(double lon, int zoom);
    static double latToTileY(double lat, int zoom);
    static double tileXToLon(double x, int zoom);
    static double tileYToLat(double y, int zoom);
    
    static QPointF lonLatToWorldPixel(double lon, double lat, int zoom);
    static CoordinateWGS84 worldPixelToLonLat(double pixelX, double pixelY, int zoom);
};

#endif // GEO_WEB_MERCATOR_H
