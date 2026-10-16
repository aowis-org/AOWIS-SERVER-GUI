#ifndef MAP_CANVAS_HIT_BOX_H
#define MAP_CANVAS_HIT_BOX_H

#include "geo/geo_web_mercator.h"
#include "map/core/map_model.h"

#include <QPoint>
#include <QPointF>
#include <QSize>

#include <algorithm>
#include <cmath>

// Cheap rejection for editor hit tests: the map area within reach_px of a
// screen position as a latitude range and a longitude window (centre +
// half width, wrap aware). Hit tests run on every pointer move over every
// marker and pipe; projecting each of them to the screen first (with the
// pixmap lookups behind a marker rect) cost tens of milliseconds per move
// with thousands of demand points. Entities outside the box are skipped
// with two comparisons; the exact screen-space test runs only for the few
// inside it.
struct MapCanvasHitBox
{
    double latitude_min = -90.0;
    double latitude_max = 90.0;
    double longitude_center = 0.0;
    // 180: every longitude (box wider than the world).
    double longitude_half = 180.0;

    static MapCanvasHitBox around(const MapModel &model, const QPointF &position,
                                  double reach_px, const QSize &viewport)
    {
        MapCanvasHitBox box;
        const int left = int(std::floor(position.x() - reach_px));
        const int right = int(std::ceil(position.x() + reach_px));
        const int top = int(std::floor(position.y() - reach_px));
        const int bottom = int(std::ceil(position.y() + reach_px));
        const CoordinateWGS84 north_west = model.wgs84FromScreen(QPoint(left, top), viewport);
        const CoordinateWGS84 south_east = model.wgs84FromScreen(QPoint(right, bottom), viewport);
        box.latitude_min = std::min(north_west.latitude_deg, south_east.latitude_deg);
        box.latitude_max = std::max(north_west.latitude_deg, south_east.latitude_deg);
        const double span = GeoWebMercator::normalizeLongitude(
            south_east.longitude_deg - north_west.longitude_deg);
        if (span > 0.0)
        {
            box.longitude_half = 0.5 * span;
            box.longitude_center = GeoWebMercator::normalizeLongitude(
                north_west.longitude_deg + box.longitude_half);
        }
        return box;
    }

    // Longitude relative to the window centre, in (-180, 180].
    double relativeLongitude(double longitude) const
    {
        return GeoWebMercator::normalizeLongitude(longitude - this->longitude_center);
    }

    bool contains(const CoordinateWGS84 &coordinate) const
    {
        if (coordinate.latitude_deg < this->latitude_min
            || coordinate.latitude_deg > this->latitude_max)
            return false;
        return this->longitude_half >= 180.0
            || std::fabs(relativeLongitude(coordinate.longitude_deg)) <= this->longitude_half;
    }
};

#endif // MAP_CANVAS_HIT_BOX_H
