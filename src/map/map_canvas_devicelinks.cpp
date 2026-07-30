#include "map_canvas_devicelinks.h"
#include "map_canvas_widget.h"

#include "../geo_web_mercator.h"

#include <algorithm>

namespace
{
constexpr double link_hit_distance = 7.0;

bool isHydraulicConnectionNode(InfrastructureEntity entity)
{
    return entity == InfrastructureEntity::Junction ||
           entity == InfrastructureEntity::Reservoir ||
           entity == InfrastructureEntity::Tank;
}

QPointF nearestPointOnSegment(const QPointF &point,
                              const QPointF &segment_start,
                              const QPointF &segment_end)
{
    const double segment_x = segment_end.x() - segment_start.x();
    const double segment_y = segment_end.y() - segment_start.y();
    const double segment_length_squared = segment_x * segment_x + segment_y * segment_y;
    
    if (segment_length_squared <= 0.0)
        return segment_start;
    
    const double projection = ((point.x() - segment_start.x()) * segment_x +
                               (point.y() - segment_start.y()) * segment_y) /
                              segment_length_squared;
    const double bounded_projection = qBound(0.0, projection, 1.0);
    return QPointF(segment_start.x() + bounded_projection * segment_x,
                   segment_start.y() + bounded_projection * segment_y);
}

double distanceSquaredToSegment(const QPointF &point,
                                const QPointF &segment_start,
                                const QPointF &segment_end)
{
    const QPointF nearest_point = nearestPointOnSegment(point, segment_start, segment_end);
    const double distance_x = point.x() - nearest_point.x();
    const double distance_y = point.y() - nearest_point.y();
    return distance_x * distance_x + distance_y * distance_y;
}
}

MapCanvasDeviceLinks::MapCanvasDeviceLinks(MapModel *map_model,
                                           MapCanvasWidget *map_canvas,
                                           QObject *parent)
    : QObject(parent)
{
    this->map_model = map_model;
    this->map_canvas = map_canvas;
}

void MapCanvasDeviceLinks::setWrapReferenceLongitude(double longitude)
{
    this->wrap_reference_lon = GeoWebMercator::normalizeLongitude(longitude);
}

QPointF MapCanvasDeviceLinks::screenFromWgs84(const CoordinateWGS84 &coordinate) const
{
    return this->map_model->screenFromWgs84(
        coordinate, this->map_canvas->size(), this->wrap_reference_lon);
}

void MapCanvasDeviceLinks::clear()
{
    clearPlacement();

    for (const DeviceLinkCanvasItem &device_link : this->list_device_links)
    {
        if (!device_link.device_label)
            continue;

        device_link.device_label->hide();
        device_link.device_label->deleteLater();
    }

    this->list_device_links.clear();
    updateCanvas();
}

void MapCanvasDeviceLinks::clearPlacement()
{
    this->device_link_start_label = nullptr;
}

bool MapCanvasDeviceLinks::hasStartLabel() const
{
    return !this->device_link_start_label.isNull();
}

MapEntityMarkerLabel *MapCanvasDeviceLinks::startLabel() const
{
    return this->device_link_start_label.data();
}

MapEntityMarkerLabel *MapCanvasDeviceLinks::addDeviceLink(
    const InfrastructureEntityReference &entity,
    const DeviceLinkGeometry &geometry,
    MapEntityMarkerLabel *start_label,
    MapEntityMarkerLabel *end_label,
    const QString &pixmap_path,
    int label_width,
    MapEntityMarkerLabel *device_label)
{
    if (!start_label || !end_label || start_label == end_label || entity.uuid.isNull() ||
        !isHydraulicConnectionNode(geometry.start_node.type) ||
        !isHydraulicConnectionNode(geometry.end_node.type) ||
        (entity.type != InfrastructureEntity::Pump && entity.type != InfrastructureEntity::Valve))
    {
        return nullptr;
    }

    if (!device_label)
        device_label = new MapEntityMarkerLabel(this->map_canvas);

    DeviceLinkCanvasItem device_link;
    device_link.entity = entity;
    device_link.geometry = geometry;
    device_link.start_label = start_label;
    device_link.end_label = end_label;
    device_link.device_label = device_label;
    device_link.path_pixmap = pixmap_path;

    const QPixmap pixmap = QPixmap(device_link.path_pixmap).scaledToWidth(
        label_width, Qt::SmoothTransformation);
    device_link.device_label->setPixmap(pixmap);
    device_link.device_label->resize(pixmap.size());
    device_link.device_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    configureLabel(device_link.device_label);

    this->list_device_links.append(device_link);
    positionLabels();
    updateCanvas();
    return device_link.device_label;
}

MapCanvasDeviceLinks::AnchorResult MapCanvasDeviceLinks::anchor(
    const InfrastructureEntityReference &entity,
    MapEntityMarkerLabel *connection_target_label,
    MapEntityMarkerLabel *floating_label,
    const QList<MapEntityMarker> &markers,
    const QString &pixmap_path,
    int label_width)
{
    AnchorResult result;
    if (!connection_target_label || !floating_label)
        return result;

    const MapEntityMarker target_marker = pointMarkerByLabel(connection_target_label, markers);
    if (!isHydraulicConnectionNode(target_marker.entity.type))
        return result;

    if (!this->device_link_start_label)
    {
        this->device_link_start_label = connection_target_label;
        result.status = AnchorStatus::StartSet;
        updateCanvas();
        return result;
    }

    const std::optional<DeviceLinkGeometry> geometry = completionGeometry(
        connection_target_label, markers);
    if (!geometry.has_value())
        return result;

    result.device_label = addDeviceLink(
        entity, geometry.value(), this->device_link_start_label,
        connection_target_label, pixmap_path, label_width, floating_label);
    if (!result.device_label)
        return result;

    result.status = AnchorStatus::Completed;
    clearPlacement();
    return result;
}

std::optional<DeviceLinkGeometry> MapCanvasDeviceLinks::completionGeometry(
    MapEntityMarkerLabel *connection_target_label,
    const QList<MapEntityMarker> &markers) const
{
    if (!this->device_link_start_label || !connection_target_label ||
        connection_target_label == this->device_link_start_label)
    {
        return std::nullopt;
    }

    const MapEntityMarker start_marker = pointMarkerByLabel(
        this->device_link_start_label.data(), markers);
    const MapEntityMarker end_marker = pointMarkerByLabel(connection_target_label, markers);
    if (!isHydraulicConnectionNode(start_marker.entity.type) ||
        !isHydraulicConnectionNode(end_marker.entity.type))
    {
        return std::nullopt;
    }

    const QPointF start_point = this->screenFromWgs84(start_marker.coord_wgs84);
    const QPointF end_point = this->screenFromWgs84(end_marker.coord_wgs84);

    DeviceLinkGeometry geometry;
    geometry.start_node = start_marker.entity;
    geometry.end_node = end_marker.entity;
    geometry.center_coordinate = this->map_model->wgs84FromScreen(
        ((start_point + end_point) / 2.0).toPoint(), this->map_canvas->size());
    return geometry;
}

bool MapCanvasDeviceLinks::updateMove(MapEntityMarkerLabel *label,
                                      const QPointF &screen_position)
{
    if (!label)
        return false;
    
    for (DeviceLinkCanvasItem &device_link : this->list_device_links)
    {
        if (device_link.device_label != label)
            continue;
        
        device_link.geometry.center_coordinate = this->map_model->wgs84FromScreen(
            screen_position.toPoint(), this->map_canvas->size());
        positionDeviceLabel(label, screen_position);
        updateCanvas();
        return true;
    }
    
    return false;
}

bool MapCanvasDeviceLinks::setCenterCoordinate(MapEntityMarkerLabel *label,
                                               const CoordinateWGS84 &coordinate)
{
    if (!label)
        return false;
    
    for (DeviceLinkCanvasItem &device_link : this->list_device_links)
    {
        if (device_link.device_label != label)
            continue;
        
        device_link.geometry.center_coordinate = coordinate;
        positionLabels();
        updateCanvas();
        return true;
    }
    
    return false;
}

bool MapCanvasDeviceLinks::positionFloatingLabel(
    MapEntityMarkerLabel *floating_label,
    const QPointF &mouse_position,
    MapEntityMarkerLabel *connection_target_label,
    const QList<MapEntityMarker> &markers) const
{
    if (!floating_label || !this->device_link_start_label)
        return false;
    
    const MapEntityMarker start_marker = pointMarkerByLabel(
        this->device_link_start_label.data(), markers);
    if (!isHydraulicConnectionNode(start_marker.entity.type))
        return false;
    
    const QPointF start_point = this->screenFromWgs84(start_marker.coord_wgs84);
    QPointF end_point = mouse_position;
    
    if (connection_target_label)
    {
        const MapEntityMarker end_marker = pointMarkerByLabel(connection_target_label, markers);
        if (isHydraulicConnectionNode(end_marker.entity.type))
        {
            end_point = this->screenFromWgs84(end_marker.coord_wgs84);
        }
    }
    
    const QPointF center_point = (start_point + end_point) / 2.0;
    positionDeviceLabel(floating_label, center_point);
    return true;
}

void MapCanvasDeviceLinks::scaleLabels(int width)
{
    for (DeviceLinkCanvasItem &device_link : this->list_device_links)
    {
        if (!device_link.device_label)
            continue;
        
        const QPixmap pixmap = QPixmap(device_link.path_pixmap).scaledToWidth(
            width, Qt::SmoothTransformation);
        device_link.device_label->setPixmap(pixmap);
        device_link.device_label->resize(pixmap.size());
    }
    
    positionLabels();
}

void MapCanvasDeviceLinks::positionLabels()
{
    for (DeviceLinkCanvasItem &device_link : this->list_device_links)
    {
        if (!device_link.device_label)
            continue;
        
        const QPointF center_point = this->screenFromWgs84(device_link.geometry.center_coordinate);
        positionDeviceLabel(device_link.device_label.data(), center_point);
    }
}

void MapCanvasDeviceLinks::paint(QPainter &paint,
                                 const QList<MapEntityMarker> &markers,
                                 const QList<MapEntityMarker> &selected_markers,
                                 bool placing_device_link,
                                 const QPointF &mouse_position,
                                 MapEntityMarkerLabel *connection_target_label) const
{
    paint.save();
    
    for (const DeviceLinkCanvasItem &device_link : this->list_device_links)
    {
        if (!device_link.start_label || !device_link.end_label || !device_link.device_label)
            continue;
        
        const MapEntityMarker start_marker = pointMarkerByLabel(
            device_link.start_label.data(), markers);
        const MapEntityMarker end_marker = pointMarkerByLabel(
            device_link.end_label.data(), markers);
        if (!isHydraulicConnectionNode(start_marker.entity.type) ||
            !isHydraulicConnectionNode(end_marker.entity.type))
        {
            continue;
        }
        
        const QPointF start_point = this->screenFromWgs84(start_marker.coord_wgs84);
        const QPointF center_point = this->screenFromWgs84(device_link.geometry.center_coordinate);
        const QPointF end_point = this->screenFromWgs84(end_marker.coord_wgs84);
        
        QPen placed_pen;
        placed_pen.setColor(markerIsSelected(device_link.device_label.data(), selected_markers) ?
                                QColor(0, 190, 255) : QColor(139, 90, 43));
        placed_pen.setWidthF(3.0);
        placed_pen.setCapStyle(Qt::RoundCap);
        placed_pen.setJoinStyle(Qt::RoundJoin);
        paint.setPen(placed_pen);
        paint.drawLine(start_point, center_point);
        paint.drawLine(center_point, end_point);
    }
    
    if (placing_device_link && this->device_link_start_label)
    {
        const MapEntityMarker start_marker = pointMarkerByLabel(
            this->device_link_start_label.data(), markers);
        if (isHydraulicConnectionNode(start_marker.entity.type))
        {
            const QPointF start_point = this->screenFromWgs84(start_marker.coord_wgs84);
            QPointF end_point = mouse_position;
            
            if (connection_target_label)
            {
                const MapEntityMarker end_marker = pointMarkerByLabel(
                    connection_target_label, markers);
                if (isHydraulicConnectionNode(end_marker.entity.type))
                {
                    end_point = this->screenFromWgs84(end_marker.coord_wgs84);
                }
            }
            
            const QPointF center_point = (start_point + end_point) / 2.0;
            QPen preview_pen(QColor(0, 140, 255));
            preview_pen.setWidthF(3.0);
            preview_pen.setCapStyle(Qt::RoundCap);
            preview_pen.setJoinStyle(Qt::RoundJoin);
            paint.setPen(preview_pen);
            paint.drawLine(start_point, center_point);
            paint.drawLine(center_point, end_point);
        }
    }
    
    paint.restore();
}

bool MapCanvasDeviceLinks::moveCenterByDelta(MapEntityMarkerLabel *label,
                                             double longitude_delta,
                                             double latitude_delta)
{
    if (!label)
        return false;
    
    for (DeviceLinkCanvasItem &device_link : this->list_device_links)
    {
        if (device_link.device_label != label)
            continue;
        
        device_link.geometry.center_coordinate.latitude_deg = std::clamp(
            device_link.geometry.center_coordinate.latitude_deg + latitude_delta,
            -GeoWebMercator::MaximumLatitude, GeoWebMercator::MaximumLatitude);
        device_link.geometry.center_coordinate.longitude_deg =
            GeoWebMercator::normalizeLongitude(
                device_link.geometry.center_coordinate.longitude_deg + longitude_delta);
        return true;
    }
    
    return false;
}

void MapCanvasDeviceLinks::removeConnectedToLabel(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    if (this->device_link_start_label == label)
        clearPlacement();
    
    for (int i = this->list_device_links.size() - 1; i >= 0; i--)
    {
        DeviceLinkCanvasItem &device_link = this->list_device_links[i];
        if (device_link.start_label != label &&
            device_link.end_label != label &&
            device_link.device_label != label)
        {
            continue;
        }
        
        if (device_link.device_label && device_link.device_label != label)
        {
            device_link.device_label->hide();
            device_link.device_label->deleteLater();
        }
        
        this->list_device_links.removeAt(i);
    }
    
    updateCanvas();
}

QList<MapEntityMarker> MapCanvasDeviceLinks::markers() const
{
    QList<MapEntityMarker> result;
    result.reserve(this->list_device_links.size());
    
    for (const DeviceLinkCanvasItem &device_link : this->list_device_links)
    {
        if (!device_link.device_label)
            continue;
        
        MapEntityMarker marker;
        marker.entity = device_link.entity;
        marker.coord_wgs84 = device_link.geometry.center_coordinate;
        marker.label = device_link.device_label;
        marker.path_pixmap = device_link.path_pixmap;
        result.append(marker);
    }
    
    return result;
}

std::optional<MapEntityMarker> MapCanvasDeviceLinks::markerByLabel(
    MapEntityMarkerLabel *label) const
{
    if (!label)
        return std::nullopt;
    
    for (const DeviceLinkCanvasItem &device_link : this->list_device_links)
    {
        if (device_link.device_label != label)
            continue;
        
        MapEntityMarker marker;
        marker.entity = device_link.entity;
        marker.coord_wgs84 = device_link.geometry.center_coordinate;
        marker.label = device_link.device_label;
        marker.path_pixmap = device_link.path_pixmap;
        return marker;
    }
    
    return std::nullopt;
}

MapEntityMarkerLabel *MapCanvasDeviceLinks::labelAt(
    const QPointF &position,
    const QList<MapEntityMarker> &markers) const
{
    double nearest_distance_squared = link_hit_distance * link_hit_distance;
    MapEntityMarkerLabel *nearest_device_label = nullptr;
    
    for (const DeviceLinkCanvasItem &device_link : this->list_device_links)
    {
        if (!device_link.start_label || !device_link.end_label || !device_link.device_label)
            continue;
        
        const MapEntityMarker start_marker = pointMarkerByLabel(
            device_link.start_label.data(), markers);
        const MapEntityMarker end_marker = pointMarkerByLabel(
            device_link.end_label.data(), markers);
        if (!isHydraulicConnectionNode(start_marker.entity.type) ||
            !isHydraulicConnectionNode(end_marker.entity.type))
        {
            continue;
        }
        
        const QPointF start_point = this->screenFromWgs84(start_marker.coord_wgs84);
        const QPointF center_point = this->screenFromWgs84(device_link.geometry.center_coordinate);
        const QPointF end_point = this->screenFromWgs84(end_marker.coord_wgs84);
        const double distance_squared = qMin(
            distanceSquaredToSegment(position, start_point, center_point),
            distanceSquaredToSegment(position, center_point, end_point));
        
        if (distance_squared > nearest_distance_squared)
            continue;
        
        nearest_distance_squared = distance_squared;
        nearest_device_label = device_link.device_label.data();
    }
    
    return nearest_device_label;
}

MapEntityMarker MapCanvasDeviceLinks::pointMarkerByLabel(
    MapEntityMarkerLabel *label,
    const QList<MapEntityMarker> &markers) const
{
    for (const MapEntityMarker &marker : markers)
    {
        if (marker.label == label)
            return marker;
    }
    
    MapEntityMarker marker;
    marker.entity.type = InfrastructureEntity::Unknown;
    return marker;
}

bool MapCanvasDeviceLinks::markerIsSelected(
    MapEntityMarkerLabel *label,
    const QList<MapEntityMarker> &selected_markers) const
{
    if (!label)
        return false;
    
    for (const MapEntityMarker &marker : selected_markers)
    {
        if (marker.label == label)
            return true;
    }
    
    return false;
}

void MapCanvasDeviceLinks::positionDeviceLabel(MapEntityMarkerLabel *label,
                                               const QPointF &center) const
{
    if (!label)
        return;
    
    const QPoint label_position(qRound(center.x() - label->width() / 2.0),
                                qRound(center.y() - label->height() / 2.0));
    
    if (label->pos() != label_position)
        label->move(label_position);
    if (!label->isVisible())
        label->show();
}

void MapCanvasDeviceLinks::configureLabel(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    connect(label, &MapEntityMarkerLabel::signalDeleteRequested,
            this, &MapCanvasDeviceLinks::markerDeleteRequested);
    connect(label, &MapEntityMarkerLabel::signalMoveRequested,
            this, &MapCanvasDeviceLinks::markerMoveRequested);
    connect(label, &MapEntityMarkerLabel::signalMoveSelectedRequested,
            this, &MapCanvasDeviceLinks::markerMoveSelectedRequested);
    connect(label, &MapEntityMarkerLabel::signalClicked,
            this, &MapCanvasDeviceLinks::markerClicked);
    connect(label, &MapEntityMarkerLabel::signalContextMenuRequested,
            this, &MapCanvasDeviceLinks::markerContextMenuRequested);
}

void MapCanvasDeviceLinks::updateCanvas()
{
    if (this->map_canvas)
        this->map_canvas->update();
}
