
#include <cmath>

#include <QtMath>
#include <QPointF>

namespace Mercator {
    double lonToTileX(double lon, int zoom);
    double latToTileY(double lat, int zoom);
    
    double tileXToLon(double x, int zoom);
    double tileYToLat(double y, int zoom);
    
    QPointF latLonToPixel(double lat, double lon, int zoom);
    QPointF pixelToLatLon(double px, double py, int zoom);
}
