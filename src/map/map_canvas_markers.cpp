#include "map_canvas_markers.h"
#include "map_canvas_widget.h"

#include "../geo_web_mercator.h"

#include <algorithm>

namespace
{
constexpr double marker_dot_radius = 5.0;
constexpr double marker_dot_hit_radius = marker_dot_radius * 3.0;

bool isHydraulicConnectionNode(InfrastructureEntity entity)
{
    return entity == InfrastructureEntity::Junction ||
           entity == InfrastructureEntity::Reservoir ||
           entity == InfrastructureEntity::Tank;
}
}

MapCanvasMarkers::MapCanvasMarkers(MapModel *map_model, MapCanvasWidget *map_canvas,
                                   MapEntityPixmapRenderer *pixmap_renderer,
                                   QObject *parent)
    : QObject(parent)
{
    this->map_model = map_model;
    this->map_canvas = map_canvas;
    this->pixmap_renderer = pixmap_renderer;
    this->marker_width = entityWidth();
}

void MapCanvasMarkers::setWrapReferenceLongitude(double longitude)
{
    this->wrap_reference_lon = GeoWebMercator::normalizeLongitude(longitude);
}

QPointF MapCanvasMarkers::screenFromWgs84(const CoordinateWGS84 &coordinate) const
{
    return this->map_model->screenFromWgs84(
        coordinate, this->map_canvas->size(), this->wrap_reference_lon);
}

const QList<MapEntityMarker> &MapCanvasMarkers::markers() const
{
    return this->list_markers;
}

void MapCanvasMarkers::clear()
{
    this->list_markers.clear();
}

int MapCanvasMarkers::entityWidth() const
{
    const int zoom = this->map_model->zoom();
    if (zoom == 19)
        return 40;
    if (zoom == 18)
        return 30;
    if (zoom == 17)
        return 20;
    return 10;
}

QString MapCanvasMarkers::pixmapPathForEntity(InfrastructureEntity entity) const
{
    return MapEntityPixmapRenderer::pixmapPathForEntity(entity);
}

std::optional<InfrastructureEntityReference> MapCanvasMarkers::nearestConnectionTarget(
    const QPointF &mouse_position, const QUuid &excluded_uuid, double max_distance) const
{
    std::optional<InfrastructureEntityReference> nearest_entity;
    double nearest_distance_squared = max_distance * max_distance;

    for (const MapEntityMarker &marker : this->list_markers)
    {
        if (marker.entity.uuid == excluded_uuid || !isHydraulicConnectionNode(marker.entity.type))
            continue;

        const QPointF point = markerAnchorPosition(marker);
        const double distance_x = point.x() - mouse_position.x();
        const double distance_y = point.y() - mouse_position.y();
        const double distance_squared = distance_x * distance_x + distance_y * distance_y;
        if (distance_squared > nearest_distance_squared)
            continue;

        nearest_distance_squared = distance_squared;
        nearest_entity = marker.entity;
    }

    return nearest_entity;
}

std::optional<MapEntityMarker> MapCanvasMarkers::markerByUuid(const QUuid &uuid) const
{
    if (uuid.isNull())
        return std::nullopt;

    for (const MapEntityMarker &marker : this->list_markers)
    {
        if (marker.entity.uuid == uuid)
            return marker;
    }

    return std::nullopt;
}

MapEntityMarker MapCanvasMarkers::addMarker(const InfrastructureEntityReference &entity,
                                             const CoordinateWGS84 &coordinate,
                                             const QString &pixmap_path,
                                             int width)
{
    this->marker_width = width;

    MapEntityMarker marker;
    marker.entity = entity;
    marker.coord_wgs84 = coordinate;
    marker.path_pixmap = pixmap_path;
    this->list_markers.append(marker);
    return marker;
}

bool MapCanvasMarkers::removeMarker(const QUuid &uuid)
{
    for (int i = 0; i < this->list_markers.size(); i++)
    {
        if (this->list_markers[i].entity.uuid != uuid)
            continue;

        this->list_markers.removeAt(i);
        return true;
    }

    return false;
}

bool MapCanvasMarkers::setCoordinate(const QUuid &uuid, const CoordinateWGS84 &coordinate)
{
    if (uuid.isNull())
        return false;

    for (MapEntityMarker &marker : this->list_markers)
    {
        if (marker.entity.uuid != uuid)
            continue;

        marker.coord_wgs84 = coordinate;
        return true;
    }

    return false;
}

bool MapCanvasMarkers::moveByDelta(const QUuid &uuid,
                                   double longitude_delta,
                                   double latitude_delta)
{
    if (uuid.isNull())
        return false;

    for (MapEntityMarker &marker : this->list_markers)
    {
        if (marker.entity.uuid != uuid)
            continue;

        marker.coord_wgs84.latitude_deg = std::clamp(
            marker.coord_wgs84.latitude_deg + latitude_delta,
            -GeoWebMercator::MaximumLatitude, GeoWebMercator::MaximumLatitude);
        marker.coord_wgs84.longitude_deg = GeoWebMercator::normalizeLongitude(
            marker.coord_wgs84.longitude_deg + longitude_delta);
        return true;
    }

    return false;
}

void MapCanvasMarkers::scaleMarkers(int width)
{
    this->marker_width = width;
}

std::optional<InfrastructureEntityReference> MapCanvasMarkers::markerAt(
    const QPointF &position) const
{
    if (!this->pixmap_renderer)
        return std::nullopt;

    for (int i = this->list_markers.size() - 1; i >= 0; i--)
    {
        const MapEntityMarker &marker = this->list_markers[i];
        const QPointF dot_center = markerAnchorPosition(marker);
        if (dotHit(position, dot_center) ||
            this->pixmap_renderer->hitTest(marker.path_pixmap, this->marker_width,
                                           markerRect(marker), position))
        {
            return marker.entity;
        }
    }

    return std::nullopt;
}

bool MapCanvasMarkers::isMarkerAt(const QPointF &position) const
{
    return markerAt(position).has_value();
}

QPointF MapCanvasMarkers::markerAnchorPosition(const MapEntityMarker &marker) const
{
    return screenFromWgs84(marker.coord_wgs84);
}

QRectF MapCanvasMarkers::markerRect(const MapEntityMarker &marker) const
{
    if (!this->pixmap_renderer)
        return QRectF();

    const QPointF screen_position = screenFromWgs84(marker.coord_wgs84);
    const QPointF rounded_anchor(qRound(screen_position.x()), qRound(screen_position.y()));
    return this->pixmap_renderer->bottomAnchoredRect(
        rounded_anchor, marker.path_pixmap, this->marker_width);
}

bool MapCanvasMarkers::dotHit(const QPointF &position, const QPointF &dot_center) const
{
    const double distance_x = position.x() - dot_center.x();
    const double distance_y = position.y() - dot_center.y();
    return distance_x * distance_x + distance_y * distance_y <=
           marker_dot_hit_radius * marker_dot_hit_radius;
}
