#include "map_canvas_markers.h"
#include "map_canvas_widget.h"

#include <QColor>
#include <QPixmap>

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

MapCanvasMarkers::MapCanvasMarkers(MapModel *map_model,
                                   MapCanvasWidget *map_canvas,
                                   QObject *parent)
    : QObject(parent)
{
    this->map_model = map_model;
    this->map_canvas = map_canvas;
}

const QList<MapEntityMarker> &MapCanvasMarkers::markers() const
{
    return this->list_markers;
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

MapEntityMarkerLabel *MapCanvasMarkers::nearestConnectionTarget(
    const QPointF &mouse_position, MapEntityMarkerLabel *excluded_label, double max_distance) const
{
    MapEntityMarkerLabel *nearest_label = nullptr;
    double nearest_distance_squared = max_distance * max_distance;
    
    for (const MapEntityMarker &marker : this->list_markers)
    {
        if (!marker.label || marker.label == excluded_label ||
            !isHydraulicConnectionNode(marker.entity.type))
        {
            continue;
        }
        
        const QPointF point = this->map_model->screenFromWgs84(
            marker.coord_wgs84, this->map_canvas->size());
        const double distance_x = point.x() - mouse_position.x();
        const double distance_y = point.y() - mouse_position.y();
        const double distance_squared = distance_x * distance_x + distance_y * distance_y;
        if (distance_squared > nearest_distance_squared)
            continue;
        
        nearest_distance_squared = distance_squared;
        nearest_label = marker.label;
    }
    
    return nearest_label;
}

std::optional<MapEntityMarker> MapCanvasMarkers::markerByLabel(
    MapEntityMarkerLabel *label) const
{
    if (!label)
        return std::nullopt;
    
    for (const MapEntityMarker &marker : this->list_markers)
    {
        if (marker.label == label)
            return marker;
    }
    
    return std::nullopt;
}

MapEntityMarker MapCanvasMarkers::addMarker(
    const InfrastructureEntityReference &entity,
    const CoordinateWGS84 &coordinate,
    const QString &pixmap_path,
    int width,
    MapEntityMarkerLabel *label)
{
    if (!label)
        label = new MapEntityMarkerLabel(this->map_canvas);
    
    configureLabel(label, pixmap_path, width);
    
    MapEntityMarker marker;
    marker.entity = entity;
    marker.coord_wgs84 = coordinate;
    marker.path_pixmap = pixmap_path;
    marker.label = label;
    this->list_markers.append(marker);
    return marker;
}

bool MapCanvasMarkers::removeMarker(MapEntityMarkerLabel *label)
{
    if (!label)
        return false;
    
    for (int i = 0; i < this->list_markers.size(); i++)
    {
        if (this->list_markers[i].label != label)
            continue;
        
        this->list_markers.removeAt(i);
        label->hide();
        label->deleteLater();
        return true;
    }
    
    return false;
}

bool MapCanvasMarkers::setCoordinate(MapEntityMarkerLabel *label,
                                     const CoordinateWGS84 &coordinate)
{
    if (!label)
        return false;
    
    for (MapEntityMarker &marker : this->list_markers)
    {
        if (marker.label != label)
            continue;
        
        marker.coord_wgs84 = coordinate;
        return true;
    }
    
    return false;
}

bool MapCanvasMarkers::moveByDelta(MapEntityMarkerLabel *label,
                                   double longitude_delta,
                                   double latitude_delta)
{
    if (!label)
        return false;
    
    for (MapEntityMarker &marker : this->list_markers)
    {
        if (marker.label != label)
            continue;
        
        marker.coord_wgs84.latitude_deg += latitude_delta;
        marker.coord_wgs84.longitude_deg += longitude_delta;
        return true;
    }
    
    return false;
}

void MapCanvasMarkers::scaleLabels(int width)
{
    for (MapEntityMarker &marker : this->list_markers)
    {
        if (!marker.label)
            continue;
        
        const QPixmap pixmap = QPixmap(marker.path_pixmap).scaledToWidth(
            width, Qt::SmoothTransformation);
        marker.label->setPixmap(pixmap);
        marker.label->resize(pixmap.size());
    }
}

void MapCanvasMarkers::positionLabels(MapEntityMarkerLabel *label_to_skip)
{
    for (MapEntityMarker &marker : this->list_markers)
    {
        MapEntityMarkerLabel *label = marker.label;
        if (!label || label == label_to_skip)
            continue;
        
        const QPointF point = this->map_model->screenFromWgs84(
            marker.coord_wgs84, this->map_canvas->size());
        const QPoint marker_position(qRound(point.x()),
                                     qRound(point.y()) - label->height());
        
        if (label->pos() != marker_position)
            label->move(marker_position);
        if (!label->isVisible())
            label->show();
    }
}

void MapCanvasMarkers::setMouseTransparency(bool transparent)
{
    for (MapEntityMarker &marker : this->list_markers)
    {
        if (marker.label)
            marker.label->setAttribute(Qt::WA_TransparentForMouseEvents, transparent);
    }
}

void MapCanvasMarkers::paintConnectionPoints(
    QPainter &paint,
    MapEntityMarkerLabel *connection_target_label,
    MapEntityMarkerLabel *moving_label,
    bool draw_moving_label_at_mouse,
    const QPointF &mouse_position) const
{
    paint.save();
    paint.setPen(Qt::NoPen);
    
    for (const MapEntityMarker &marker : this->list_markers)
    {
        if (draw_moving_label_at_mouse && marker.label == moving_label)
            continue;
        
        const QPointF point = this->map_model->screenFromWgs84(
            marker.coord_wgs84, this->map_canvas->size());
        const bool is_connection_target = marker.label &&
                                          marker.label == connection_target_label;
        
        if (is_connection_target)
        {
            paint.setBrush(QColor(0, 140, 255));
            paint.drawEllipse(point, connection_target_radius, connection_target_radius);
        }
        else
        {
            paint.setBrush(Qt::black);
            paint.drawEllipse(point, marker_dot_radius, marker_dot_radius);
        }
    }
    
    if (draw_moving_label_at_mouse && moving_label)
    {
        paint.setBrush(Qt::black);
        paint.drawEllipse(mouse_position, marker_dot_radius, marker_dot_radius);
    }
    
    paint.restore();
}

void MapCanvasMarkers::configureLabel(MapEntityMarkerLabel *label,
                                      const QString &pixmap_path,
                                      int width)
{
    label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    
    const QPixmap pixmap = QPixmap(pixmap_path).scaledToWidth(
        width, Qt::SmoothTransformation);
    label->setPixmap(pixmap);
    label->resize(pixmap.size());
    
    connect(label, &MapEntityMarkerLabel::signalDeleteRequested,
            this, &MapCanvasMarkers::markerDeleteRequested);
    connect(label, &MapEntityMarkerLabel::signalMoveRequested,
            this, &MapCanvasMarkers::markerMoveRequested);
    connect(label, &MapEntityMarkerLabel::signalMoveSelectedRequested,
            this, &MapCanvasMarkers::markerMoveSelectedRequested);
    connect(label, &MapEntityMarkerLabel::signalClicked,
            this, &MapCanvasMarkers::markerClicked);
    connect(label, &MapEntityMarkerLabel::signalContextMenuRequested,
            this, &MapCanvasMarkers::markerContextMenuRequested);
}
