#include "map_canvas_markers.h"
#include "map_canvas_widget.h"

#include "../geo_web_mercator.h"

#include <QColor>

#include <algorithm>

namespace
{
constexpr double marker_dot_radius = 5.0;
constexpr double connection_target_radius = 9.0;

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
    switch (entity)
    {
    case InfrastructureEntity::Junction:
        return QStringLiteral(":/icon/junction.png");
    case InfrastructureEntity::Reservoir:
        return QStringLiteral(":/icon/reservoir.png");
    case InfrastructureEntity::Tank:
        return QStringLiteral(":/icon/tower.png");
    case InfrastructureEntity::Pipe:
        return QStringLiteral(":/icon/pipe.png");
    case InfrastructureEntity::Pump:
        return QStringLiteral(":/icon/pump.png");
    case InfrastructureEntity::Valve:
        return QStringLiteral(":/icon/valve.png");
    case InfrastructureEntity::CustomerPoint:
        return QStringLiteral(":/icon/customer.png");
    case InfrastructureEntity::ElectricJunction:
    case InfrastructureEntity::Cable:
    case InfrastructureEntity::Switch:
    case InfrastructureEntity::Fuse:
    case InfrastructureEntity::CircuitBreaker:
        return QStringLiteral(":/icon/electricity.png");
    case InfrastructureEntity::Battery:
    case InfrastructureEntity::Generator:
    case InfrastructureEntity::SolarPanel:
    case InfrastructureEntity::Inverter:
    case InfrastructureEntity::Transformer:
        return QStringLiteral(":/icon/energy.png");
    case InfrastructureEntity::Note:
    case InfrastructureEntity::Unknown:
        return QStringLiteral(":/icon/geomarker.png");
    }

    return QStringLiteral(":/icon/geomarker.png");
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

        const QPointF point = screenFromWgs84(marker.coord_wgs84);
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

void MapCanvasMarkers::paint(QPainter &painter, const QList<QUuid> &selected_uuids,
                             const QUuid &connection_target_uuid) const
{
    if (!this->pixmap_renderer)
        return;

    painter.save();
    painter.setPen(Qt::NoPen);

    for (const MapEntityMarker &marker : this->list_markers)
    {
        const QPointF point = screenFromWgs84(marker.coord_wgs84);
        if (marker.entity.uuid == connection_target_uuid)
        {
            painter.setBrush(QColor(0, 140, 255));
            painter.drawEllipse(point, connection_target_radius, connection_target_radius);
        }
        else
        {
            painter.setBrush(Qt::black);
            painter.drawEllipse(point, marker_dot_radius, marker_dot_radius);
        }
    }

    for (const MapEntityMarker &marker : this->list_markers)
    {
        const MapEntityPixmapRenderer::Highlight highlight =
            isSelected(marker.entity.uuid, selected_uuids)
                ? MapEntityPixmapRenderer::Highlight::Selected
                : MapEntityPixmapRenderer::Highlight::None;
        this->pixmap_renderer->paint(painter, marker.path_pixmap, this->marker_width,
                                     markerRect(marker), highlight);
    }

    painter.restore();
}

void MapCanvasMarkers::paintFloating(QPainter &painter, InfrastructureEntity entity,
                                     const QString &pixmap_path, int width,
                                     const QPointF &anchor_position) const
{
    if (!this->pixmap_renderer || entity == InfrastructureEntity::Unknown)
        return;

    const QRectF target_rect = this->pixmap_renderer->bottomAnchoredRect(
        anchor_position, pixmap_path, width);
    this->pixmap_renderer->paint(painter, pixmap_path, width, target_rect);
}

std::optional<InfrastructureEntityReference> MapCanvasMarkers::markerAt(
    const QPointF &position) const
{
    if (!this->pixmap_renderer)
        return std::nullopt;

    for (int i = this->list_markers.size() - 1; i >= 0; i--)
    {
        const MapEntityMarker &marker = this->list_markers[i];
        const QPointF dot_center = screenFromWgs84(marker.coord_wgs84);
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

QRectF MapCanvasMarkers::markerRect(const MapEntityMarker &marker) const
{
    if (!this->pixmap_renderer)
        return QRectF();

    const QPointF screen_position = screenFromWgs84(marker.coord_wgs84);
    const QPointF rounded_anchor(qRound(screen_position.x()), qRound(screen_position.y()));
    return this->pixmap_renderer->bottomAnchoredRect(
        rounded_anchor, marker.path_pixmap, this->marker_width);
}

bool MapCanvasMarkers::isSelected(const QUuid &uuid, const QList<QUuid> &selected_uuids) const
{
    return selected_uuids.contains(uuid);
}

bool MapCanvasMarkers::dotHit(const QPointF &position, const QPointF &dot_center) const
{
    const double distance_x = position.x() - dot_center.x();
    const double distance_y = position.y() - dot_center.y();
    return distance_x * distance_x + distance_y * distance_y <=
           marker_dot_radius * marker_dot_radius;
}
