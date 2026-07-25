#include "map_canvas_entities.h"
#include "map_canvas_pipes.h"
#include "map_canvas_widget.h"

#include <functional>
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMessageBox>

namespace
{
constexpr double marker_dot_radius = 5.0;
constexpr double connection_target_radius = 9.0;
constexpr double connection_hover_distance = 18.0;
constexpr double link_hit_distance = 7.0;

bool isHydraulicConnectionNode(InfrastructureEntity entity)
{
    return entity == InfrastructureEntity::Junction ||
           entity == InfrastructureEntity::Reservoir ||
           entity == InfrastructureEntity::Tank;
}

bool isHydraulicDeviceLink(InfrastructureEntity entity)
{
    return entity == InfrastructureEntity::Pump ||
           entity == InfrastructureEntity::Valve;
}

bool isHydraulicPipeGeometry(InfrastructureEntity entity)
{
    return entity == InfrastructureEntity::Pipe;
}

bool isHydraulicCanvasLink(InfrastructureEntity entity)
{
    return isHydraulicDeviceLink(entity) || isHydraulicPipeGeometry(entity);
}

QPointF nearestPointOnSegment(const QPointF &point, const QPointF &segment_start, const QPointF &segment_end)
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

double distanceSquaredToSegment(const QPointF &point, const QPointF &segment_start, const QPointF &segment_end)
{
    const QPointF nearest_point = nearestPointOnSegment(point, segment_start, segment_end);
    const double distance_x = point.x() - nearest_point.x();
    const double distance_y = point.y() - nearest_point.y();
    return distance_x * distance_x + distance_y * distance_y;
}
}

MapCanvasEntities::MapCanvasEntities(MapModel *map_model, HydraulicData *hydraulic_data, MapCanvasWidget *map_canvas)
    : QObject(map_canvas),
    map_model(map_model),
    hydraulic_data(hydraulic_data),
    map_canvas(map_canvas)
{
    this->pipes = new MapCanvasPipes(this->map_model, this->map_canvas, this);
    
    connect(this->map_model, &MapModel::zoomChanged, this, &MapCanvasEntities::scaleMarkers);
    connect(this->map_model, &MapModel::centerChangedWGS84, this, [this](const CoordinateWGS84 &)
            {
                positionMarkers();
            });
}

void MapCanvasEntities::startEntityPositioning(InfrastructureEntity entity)
{
    stopEntityPositioning();
    this->entity_current = entity;
    this->entity_draw_immediately = true;
    this->entity_placement_mode = MapEntityPlacementMode::CreateNew;
    
    if (isHydraulicCanvasLink(this->entity_current))
        setPointMarkerMouseTransparency(true);
    
    startEntityPositioningInternal();
}

void MapCanvasEntities::startEntityPositioningInternal()
{
    if (this->entity_placement_mode == MapEntityPlacementMode::CreateNew)
    {
        this->entity_floating = new MapEntityMarkerLabel(this->map_canvas);
        
        int width = 150;
        if (isHydraulicCanvasLink(this->entity_current))
            width = calculateEntityWidth();
        
        const QPixmap pixmap = QPixmap(pixmapPathForEntity(this->entity_current)).scaledToWidth(
            width, Qt::SmoothTransformation);
        this->entity_floating->setPixmap(pixmap);
        this->entity_floating->resize(pixmap.size());
    }
    else if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting)
    {
        if (!this->entity_floating && !this->pipes->isPipeVertexMoveActive())
        {
            this->entity_placement_mode = MapEntityPlacementMode::None;
            return;
        }
    }
    else
    {
        return;
    }
    
    if (this->entity_floating)
    {
        this->entity_floating->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        this->entity_floating->setFocusPolicy(Qt::NoFocus);
        this->entity_floating->hide();
        this->entity_floating->raise();
        
        if (this->entity_draw_immediately)
        {
            const QPoint marker_pos(qRound(this->mouse_pos_last.x()),
                                    qRound(this->mouse_pos_last.y()) - this->entity_floating->height());
            this->entity_floating->move(marker_pos);
            this->entity_floating->show();
        }
    }
    
    this->entity_draw_immediately = false;
    this->map_canvas->setFocusPolicy(Qt::StrongFocus);
    this->map_canvas->setFocus(Qt::OtherFocusReason);
    
    if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting)
        setMoveCursor(true);
}

void MapCanvasEntities::stopEntityPositioning()
{
    setMoveCursor(false);
    
    if (this->move_selected_entities)
    {
        setSelectedEntitiesMouseTransparency(false);
        this->move_selected_entities = false;
    }
    
    clearConnectionTarget();
    this->device_link_start_label = nullptr;
    this->pipes->clearPlacement();
    this->pipes->cancelPipeVertexMove();
    setPointMarkerMouseTransparency(false);
    
    if (!this->entity_floating)
    {
        this->entity_placement_mode = MapEntityPlacementMode::None;
        return;
    }
    
    const MapEntityPlacementMode previous_mode = this->entity_placement_mode;
    MapEntityMarkerLabel *label = this->entity_floating;
    this->entity_floating = nullptr;
    this->entity_placement_mode = MapEntityPlacementMode::None;
    
    if (previous_mode == MapEntityPlacementMode::CreateNew)
    {
        label->hide();
        label->deleteLater();
    }
    else if (previous_mode == MapEntityPlacementMode::MoveExisting)
    {
        label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        positionMarkers();
    }
    
    if (this->map_canvas)
        this->map_canvas->update();
}

void MapCanvasEntities::floatEntity(QMouseEvent *event)
{
    if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting)
        setMoveCursor(true);
    
    const QPointF previous_mouse_position = this->mouse_pos_last;
    this->mouse_pos_last = event->position();
    
    if (this->move_selected_entities)
    {
        moveSelectedEntities(previous_mouse_position, event->position());
        positionMarkers();
        
        if (this->map_canvas)
            this->map_canvas->update();
        
        event->accept();
        return;
    }
    
    updateConnectionTarget(event->position());
    
    if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting &&
        this->pipes->isPipeVertexMoveActive())
    {
        if (!this->pipes->updatePipeVertexMove(event->position()))
        {
            setMoveCursor(false);
            this->entity_placement_mode = MapEntityPlacementMode::None;
        }
        
        event->accept();
        return;
    }
    
    if (!this->entity_floating)
        return;
    
    if (!this->entity_floating->isVisible())
    {
        if (this->entity_placement_mode == MapEntityPlacementMode::CreateNew &&
            !this->entity_draw_immediately &&
            (event->position().toPoint() - this->entity_floating_hide_until).manhattanLength() <= 10)
        {
            return;
        }
        this->entity_floating->show();
    }
    
    QPoint marker_pos;
    bool moving_device_link = false;
    
    if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting)
    {
        for (DeviceLinkCanvasItem &device_link : this->list_device_links)
        {
            if (device_link.device_label != this->entity_floating)
                continue;
            
            moving_device_link = true;
            device_link.geometry.center_coordinate = this->map_model->wgs84FromScreen(
                event->position().toPoint(), this->map_canvas->size());
            marker_pos = QPoint(
                qRound(event->position().x() - this->entity_floating->width() / 2.0),
                qRound(event->position().y() - this->entity_floating->height() / 2.0));
            break;
        }
    }
    
    if (!moving_device_link &&
        this->entity_placement_mode == MapEntityPlacementMode::CreateNew &&
        isHydraulicDeviceLink(this->entity_current) &&
        this->device_link_start_label)
    {
        const MapEntityMarker start_marker = markerByLabel(this->device_link_start_label);
        const QPointF start_point = this->map_model->screenFromWgs84(
            start_marker.coord_wgs84, this->map_canvas->size());
        QPointF end_point = event->position();
        
        if (this->connection_target_label)
        {
            const MapEntityMarker end_marker = markerByLabel(this->connection_target_label);
            if (isHydraulicConnectionNode(end_marker.entity.type))
            {
                end_point = this->map_model->screenFromWgs84(
                    end_marker.coord_wgs84, this->map_canvas->size());
            }
        }
        
        const QPointF center_point = (start_point + end_point) / 2.0;
        marker_pos = QPoint(qRound(center_point.x() - this->entity_floating->width() / 2.0),
                            qRound(center_point.y() - this->entity_floating->height() / 2.0));
    }
    else if (!moving_device_link)
    {
        marker_pos = QPoint(qRound(event->position().x()),
                            qRound(event->position().y()) - this->entity_floating->height());
    }
    
    this->entity_floating->move(marker_pos);
    
    if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting && !moving_device_link)
    {
        for (MapEntityMarker &marker : this->list_entity_markers)
        {
            if (marker.label != this->entity_floating)
                continue;
            
            marker.coord_wgs84 = this->map_model->wgs84FromScreen(
                event->position().toPoint(), this->map_canvas->size());
            break;
        }
    }
    
    if (this->map_canvas)
        this->map_canvas->update();
    
    event->accept();
}

bool MapCanvasEntities::anchorMarker(QMouseEvent *event)
{
    if (anchorPipeVertexMove(event))
        return true;
    
    if (!this->entity_floating)
        return false;
    
    if (this->move_selected_entities)
    {
        moveSelectedEntities(this->mouse_pos_last, event->position());
        setMoveCursor(false);
        setSelectedEntitiesMouseTransparency(false);
        this->move_selected_entities = false;
        this->entity_floating = nullptr;
        this->entity_placement_mode = MapEntityPlacementMode::None;
        positionMarkers();
        
        if (this->map_canvas)
            this->map_canvas->update();
        
        return true;
    }
    
    if (this->entity_placement_mode == MapEntityPlacementMode::CreateNew)
    {
        if (isHydraulicDeviceLink(this->entity_current))
            return anchorDeviceLink(event);
        if (isHydraulicPipeGeometry(this->entity_current))
            return anchorPipe(event);
    }
    
    const CoordinateWGS84 coordinate = this->map_model->wgs84FromScreen(
        event->position().toPoint(), this->map_canvas->size());
    
    if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting)
    {
        MapEntityMarkerLabel *moved_label = this->entity_floating;
        
        for (MapEntityMarker &marker : this->list_entity_markers)
        {
            if (marker.label != moved_label)
                continue;
            
            marker.coord_wgs84 = coordinate;
            setMoveCursor(false);
            moved_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            this->entity_floating = nullptr;
            this->entity_placement_mode = MapEntityPlacementMode::None;
            positionMarkers();
            positionDeviceLinks();
            
            if (this->map_canvas)
                this->map_canvas->update();
            
            return true;
        }
        
        for (DeviceLinkCanvasItem &device_link : this->list_device_links)
        {
            if (device_link.device_label != moved_label)
                continue;
            
            device_link.geometry.center_coordinate = coordinate;
            setMoveCursor(false);
            moved_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            this->entity_floating = nullptr;
            this->entity_placement_mode = MapEntityPlacementMode::None;
            positionDeviceLinks();
            
            if (this->map_canvas)
                this->map_canvas->update();
            
            return true;
        }
        
        setMoveCursor(false);
        moved_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        this->entity_floating = nullptr;
        this->entity_placement_mode = MapEntityPlacementMode::None;
        positionMarkers();
        positionDeviceLinks();
        
        if (this->map_canvas)
            this->map_canvas->update();
        
        return false;
    }
    
    if (this->entity_placement_mode != MapEntityPlacementMode::CreateNew)
        return false;
    
    InfrastructureEntityReference reference;
    reference.type = this->entity_current;
    reference.uuid = QUuid::createUuid();
    
    MapEntityMarker marker;
    marker.coord_wgs84 = coordinate;
    marker.entity = reference;
    marker.label = this->entity_floating;
    marker.path_pixmap = pixmapPathForEntity(this->entity_current);
    marker.label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    
    connect(marker.label, &MapEntityMarkerLabel::signalDeleteRequested,
            this, &MapCanvasEntities::onMarkerDeleteRequested);
    connect(marker.label, &MapEntityMarkerLabel::signalMoveRequested,
            this, &MapCanvasEntities::onMarkerMoveRequested);
    connect(marker.label, &MapEntityMarkerLabel::signalMoveSelectedRequested,
            this, &MapCanvasEntities::onMarkerMoveSelectedRequested);
    connect(marker.label, &MapEntityMarkerLabel::signalClicked,
            this, &MapCanvasEntities::onMarkerClicked);
    connect(marker.label, &MapEntityMarkerLabel::signalContextMenuRequested,
            this, &MapCanvasEntities::onMarkerContextMenuRequested);
    
    const int width = calculateEntityWidth();
    const QPixmap pixmap = QPixmap(marker.path_pixmap).scaledToWidth(width, Qt::SmoothTransformation);
    marker.label->setPixmap(pixmap);
    marker.label->resize(pixmap.size());
    this->list_entity_markers.append(marker);
    
    this->entity_floating_hide_until = event->position().toPoint();
    this->entity_floating = nullptr;
    positionMarkers();
    startEntityPositioningInternal();
    return true;
}

bool MapCanvasEntities::anchorDeviceLink(QMouseEvent *event)
{
    if (!this->connection_target_label)
        return true;
    
    const MapEntityMarker target_marker = markerByLabel(this->connection_target_label);
    if (!isHydraulicConnectionNode(target_marker.entity.type))
        return true;
    
    if (!this->device_link_start_label)
    {
        this->device_link_start_label = this->connection_target_label;
        this->connection_target_label = nullptr;
        updateConnectionTarget(event->position());
        
        if (this->map_canvas)
            this->map_canvas->update();
        
        return true;
    }
    
    if (this->connection_target_label == this->device_link_start_label)
        return true;
    
    const MapEntityMarker start_marker = markerByLabel(this->device_link_start_label);
    if (!isHydraulicConnectionNode(start_marker.entity.type))
    {
        this->device_link_start_label = nullptr;
        return true;
    }
    
    const QPointF start_point = this->map_model->screenFromWgs84(
        start_marker.coord_wgs84, this->map_canvas->size());
    const QPointF end_point = this->map_model->screenFromWgs84(
        target_marker.coord_wgs84, this->map_canvas->size());
    const QPointF center_point = (start_point + end_point) / 2.0;
    
    DeviceLinkCanvasItem device_link;
    device_link.entity.type = this->entity_current;
    device_link.entity.uuid = QUuid::createUuid();
    device_link.geometry.start_node = start_marker.entity;
    device_link.geometry.end_node = target_marker.entity;
    device_link.geometry.center_coordinate = this->map_model->wgs84FromScreen(
        center_point.toPoint(), this->map_canvas->size());
    device_link.start_label = this->device_link_start_label;
    device_link.end_label = this->connection_target_label;
    device_link.device_label = this->entity_floating;
    device_link.path_pixmap = pixmapPathForEntity(this->entity_current);
    
    const int width = calculateEntityWidth();
    const QPixmap pixmap = QPixmap(device_link.path_pixmap).scaledToWidth(width, Qt::SmoothTransformation);
    device_link.device_label->setPixmap(pixmap);
    device_link.device_label->resize(pixmap.size());
    device_link.device_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    
    connect(device_link.device_label, &MapEntityMarkerLabel::signalDeleteRequested,
            this, &MapCanvasEntities::onMarkerDeleteRequested);
    connect(device_link.device_label, &MapEntityMarkerLabel::signalMoveRequested,
            this, &MapCanvasEntities::onMarkerMoveRequested);
    connect(device_link.device_label, &MapEntityMarkerLabel::signalMoveSelectedRequested,
            this, &MapCanvasEntities::onMarkerMoveSelectedRequested);
    connect(device_link.device_label, &MapEntityMarkerLabel::signalClicked,
            this, &MapCanvasEntities::onMarkerClicked);
    connect(device_link.device_label, &MapEntityMarkerLabel::signalContextMenuRequested,
            this, &MapCanvasEntities::onMarkerContextMenuRequested);
    
    this->list_device_links.append(device_link);
    this->entity_floating_hide_until = event->position().toPoint();
    this->entity_floating = nullptr;
    this->device_link_start_label = nullptr;
    this->connection_target_label = nullptr;
    positionDeviceLinks();
    startEntityPositioningInternal();
    
    if (this->map_canvas)
        this->map_canvas->update();
    
    return true;
}

bool MapCanvasEntities::anchorPipe(QMouseEvent *event)
{
    if (!this->pipes->hasStartLabel())
    {
        if (!this->connection_target_label)
            return true;
        
        const MapEntityMarker start_marker = markerByLabel(this->connection_target_label);
        if (!isHydraulicConnectionNode(start_marker.entity.type))
            return true;
        
        this->pipes->startPipe(this->connection_target_label);
        this->connection_target_label = nullptr;
        return true;
    }
    
    if (this->connection_target_label)
    {
        if (this->connection_target_label == this->pipes->startLabel())
            return true;
        
        const MapEntityMarker start_marker = markerByLabel(this->pipes->startLabel());
        const MapEntityMarker end_marker = markerByLabel(this->connection_target_label);
        if (!isHydraulicConnectionNode(start_marker.entity.type) ||
            !isHydraulicConnectionNode(end_marker.entity.type))
        {
            return true;
        }
        
        if (!this->pipes->completePipe(start_marker.entity, end_marker.entity,
                                       this->connection_target_label))
        {
            return true;
        }
        
        this->entity_floating_hide_until = event->position().toPoint();
        MapEntityMarkerLabel *placement_icon = this->entity_floating;
        this->entity_floating = nullptr;
        this->connection_target_label = nullptr;
        placement_icon->hide();
        placement_icon->deleteLater();
        startEntityPositioningInternal();
        return true;
    }
    
    const CoordinateWGS84 intermediate_vertex = this->map_model->wgs84FromScreen(
        event->position().toPoint(), this->map_canvas->size());
    this->pipes->appendIntermediateVertex(intermediate_vertex);
    return true;
}

bool MapCanvasEntities::anchorPipeVertexMove(QMouseEvent *event)
{
    if (this->entity_placement_mode != MapEntityPlacementMode::MoveExisting ||
        !this->pipes->isPipeVertexMoveActive())
    {
        return false;
    }
    
    this->pipes->finishPipeVertexMove(event->position());
    setMoveCursor(false);
    this->entity_placement_mode = MapEntityPlacementMode::None;
    return true;
}

int MapCanvasEntities::calculateEntityWidth()
{
    const int zoom = this->map_model->zoom();
    int width = 10;
    if (zoom == 19)
        width = 40;
    else if (zoom == 18)
        width = 30;
    else if (zoom == 17)
        width = 20;
    return width;
}

void MapCanvasEntities::scaleMarkers()
{
    const int width = calculateEntityWidth();
    
    for (MapEntityMarker &marker : this->list_entity_markers)
    {
        MapEntityMarkerLabel *label = marker.label;
        if (!label)
            continue;
        
        const QPixmap pixmap = QPixmap(marker.path_pixmap).scaledToWidth(width, Qt::SmoothTransformation);
        label->setPixmap(pixmap);
        label->resize(pixmap.size());
    }
    
    for (DeviceLinkCanvasItem &device_link : this->list_device_links)
    {
        if (!device_link.device_label)
            continue;
        
        const QPixmap pixmap = QPixmap(device_link.path_pixmap).scaledToWidth(
            width, Qt::SmoothTransformation);
        device_link.device_label->setPixmap(pixmap);
        device_link.device_label->resize(pixmap.size());
    }
    
    if (this->entity_floating &&
        this->entity_placement_mode == MapEntityPlacementMode::CreateNew &&
        isHydraulicCanvasLink(this->entity_current))
    {
        const QPixmap pixmap = QPixmap(pixmapPathForEntity(this->entity_current)).scaledToWidth(
            width, Qt::SmoothTransformation);
        this->entity_floating->setPixmap(pixmap);
        this->entity_floating->resize(pixmap.size());
    }
    
    positionMarkers();
}

void MapCanvasEntities::positionMarkers()
{
    for (MapEntityMarker &marker : this->list_entity_markers)
    {
        MapEntityMarkerLabel *label = marker.label;
        if (!label)
            continue;
        
        if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting &&
            label == this->entity_floating &&
            !this->move_selected_entities)
        {
            continue;
        }
        
        const QPointF point = this->map_model->screenFromWgs84(
            marker.coord_wgs84, this->map_canvas->size());
        const QPoint marker_pos(qRound(point.x()), qRound(point.y()) - label->height());
        
        if (label->pos() != marker_pos)
            label->move(marker_pos);
        if (!label->isVisible())
            label->show();
    }
    
    positionDeviceLinks();
}

void MapCanvasEntities::positionDeviceLinks()
{
    for (DeviceLinkCanvasItem &device_link : this->list_device_links)
    {
        if (!device_link.device_label)
            continue;
        
        const QPointF center_point = this->map_model->screenFromWgs84(
            device_link.geometry.center_coordinate, this->map_canvas->size());
        positionDeviceLabel(device_link.device_label, center_point);
    }
}

void MapCanvasEntities::positionDeviceLabel(MapEntityMarkerLabel *label, const QPointF &center)
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

void MapCanvasEntities::setPointMarkerMouseTransparency(bool transparent)
{
    for (MapEntityMarker &marker : this->list_entity_markers)
    {
        if (marker.label)
            marker.label->setAttribute(Qt::WA_TransparentForMouseEvents, transparent);
    }
}

void MapCanvasEntities::setMoveCursor(bool enabled)
{
    if (enabled)
    {
        if (this->move_cursor_active)
            QApplication::changeOverrideCursor(Qt::SizeAllCursor);
        else
        {
            QApplication::setOverrideCursor(Qt::SizeAllCursor);
            this->move_cursor_active = true;
        }
        return;
    }
    
    if (!this->move_cursor_active)
        return;
    
    QApplication::restoreOverrideCursor();
    this->move_cursor_active = false;
}

void MapCanvasEntities::moveSelectedEntities(const QPointF &from_position, const QPointF &to_position)
{
    const CoordinateWGS84 from_coordinate = this->map_model->wgs84FromScreen(
        from_position.toPoint(), this->map_canvas->size());
    const CoordinateWGS84 to_coordinate = this->map_model->wgs84FromScreen(
        to_position.toPoint(), this->map_canvas->size());
    const double longitude_delta = to_coordinate.lon - from_coordinate.lon;
    const double latitude_delta = to_coordinate.lat - from_coordinate.lat;
    
    for (const MapEntityMarker &selected_marker : this->list_entity_markers_selected)
    {
        if (!selected_marker.label)
            continue;
        
        bool marker_moved = false;
        for (MapEntityMarker &marker : this->list_entity_markers)
        {
            if (marker.label != selected_marker.label)
                continue;
            
            marker.coord_wgs84.lon += longitude_delta;
            marker.coord_wgs84.lat += latitude_delta;
            marker_moved = true;
            break;
        }
        
        if (marker_moved)
            continue;
        
        for (DeviceLinkCanvasItem &device_link : this->list_device_links)
        {
            if (device_link.device_label != selected_marker.label)
                continue;
            
            device_link.geometry.center_coordinate.lon += longitude_delta;
            device_link.geometry.center_coordinate.lat += latitude_delta;
            break;
        }
    }
    
    this->pipes->moveIntermediateVerticesWithSelectedEndpoints(
        this->list_entity_markers_selected, longitude_delta, latitude_delta);
}

void MapCanvasEntities::setSelectedEntitiesMouseTransparency(bool transparent)
{
    for (const MapEntityMarker &selected_marker : this->list_entity_markers_selected)
    {
        if (selected_marker.label)
            selected_marker.label->setAttribute(Qt::WA_TransparentForMouseEvents, transparent);
    }
}

void MapCanvasEntities::paintMarkers(QPainter &paint)
{
    this->pipes->paint(
        paint,
        this->list_entity_markers,
        this->entity_placement_mode == MapEntityPlacementMode::CreateNew &&
            isHydraulicPipeGeometry(this->entity_current),
        this->mouse_pos_last,
        this->connection_target_label);
    paintDeviceLinks(paint);
    
    paint.save();
    paint.setPen(Qt::NoPen);
    
    for (MapEntityMarker &marker : this->list_entity_markers)
    {
        if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting &&
            marker.label == this->entity_floating &&
            !this->move_selected_entities)
        {
            continue;
        }
        
        const QPointF point = this->map_model->screenFromWgs84(
            marker.coord_wgs84, this->map_canvas->size());
        const bool is_connection_target = marker.label && marker.label == this->connection_target_label;
        
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
    
    if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting &&
        this->entity_floating &&
        !this->move_selected_entities)
    {
        paint.setBrush(Qt::black);
        paint.drawEllipse(this->mouse_pos_last, marker_dot_radius, marker_dot_radius);
    }
    
    paint.restore();
}

void MapCanvasEntities::paintDeviceLinks(QPainter &paint)
{
    paint.save();
    
    for (const DeviceLinkCanvasItem &device_link : this->list_device_links)
    {
        if (!device_link.start_label || !device_link.end_label || !device_link.device_label)
            continue;
        
        const MapEntityMarker start_marker = markerByLabel(device_link.start_label);
        const MapEntityMarker end_marker = markerByLabel(device_link.end_label);
        if (!isHydraulicConnectionNode(start_marker.entity.type) ||
            !isHydraulicConnectionNode(end_marker.entity.type))
        {
            continue;
        }
        
        const QPointF start_point = this->map_model->screenFromWgs84(
            start_marker.coord_wgs84, this->map_canvas->size());
        const QPointF center_point = this->map_model->screenFromWgs84(
            device_link.geometry.center_coordinate, this->map_canvas->size());
        const QPointF end_point = this->map_model->screenFromWgs84(
            end_marker.coord_wgs84, this->map_canvas->size());
        
        QPen placed_pen;
        placed_pen.setColor(isMarkerSelected(device_link.device_label) ?
                                QColor(0, 190, 255) : QColor(139, 90, 43));
        placed_pen.setWidthF(3.0);
        placed_pen.setCapStyle(Qt::RoundCap);
        placed_pen.setJoinStyle(Qt::RoundJoin);
        paint.setPen(placed_pen);
        paint.drawLine(start_point, center_point);
        paint.drawLine(center_point, end_point);
    }
    
    if (this->entity_placement_mode == MapEntityPlacementMode::CreateNew &&
        isHydraulicDeviceLink(this->entity_current) &&
        this->device_link_start_label)
    {
        const MapEntityMarker start_marker = markerByLabel(this->device_link_start_label);
        if (isHydraulicConnectionNode(start_marker.entity.type))
        {
            const QPointF start_point = this->map_model->screenFromWgs84(
                start_marker.coord_wgs84, this->map_canvas->size());
            QPointF end_point = this->mouse_pos_last;
            
            if (this->connection_target_label)
            {
                const MapEntityMarker end_marker = markerByLabel(this->connection_target_label);
                if (isHydraulicConnectionNode(end_marker.entity.type))
                {
                    end_point = this->map_model->screenFromWgs84(
                        end_marker.coord_wgs84, this->map_canvas->size());
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

void MapCanvasEntities::onMarkerMoveRequested(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    const MapEntityMarker marker = markerByLabel(label);
    if (marker.entity.type == InfrastructureEntity::Unknown)
        return;
    
    stopEntityPositioning();
    this->entity_current = marker.entity.type;
    this->entity_placement_mode = MapEntityPlacementMode::MoveExisting;
    this->entity_floating = label;
    this->entity_draw_immediately = true;
    this->mouse_pos_last = this->map_canvas->mapFromGlobal(QCursor::pos());
    startEntityPositioningInternal();
    
    if (this->map_canvas)
        this->map_canvas->update();
}

void MapCanvasEntities::onMarkerMoveSelectedRequested(MapEntityMarkerLabel *label)
{
    if (!label || !isMarkerSelected(label) || this->list_entity_markers_selected.size() < 2)
        return;
    
    onMarkerMoveRequested(label);
    
    if (this->entity_placement_mode != MapEntityPlacementMode::MoveExisting ||
        this->entity_floating != label)
    {
        return;
    }
    
    this->move_selected_entities = true;
    setSelectedEntitiesMouseTransparency(true);
    positionMarkers();
    
    if (this->map_canvas)
        this->map_canvas->update();
}

void MapCanvasEntities::onMarkerDeleteRequested(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    deleteMarker(label);
    if (this->map_canvas)
        this->map_canvas->update();
}

void MapCanvasEntities::onMarkerSelectedDeleteRequested()
{
    QList<MapEntityMarkerLabel *> labels_to_delete;
    for (const MapEntityMarker &marker : this->list_entity_markers_selected)
    {
        if (marker.label)
            labels_to_delete.append(marker.label);
    }
    
    for (MapEntityMarkerLabel *label : labels_to_delete)
        deleteMarker(label);
    
    this->pipes->deleteSelected();
    clearSelection();
    
    if (this->map_canvas)
        this->map_canvas->update();
    
    emit signalEntityMarkerSelected(false);
}

void MapCanvasEntities::deleteMarker(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    if (this->connection_target_label == label)
        this->connection_target_label = nullptr;
    
    if (this->entity_floating == label)
    {
        setMoveCursor(false);
        this->entity_floating = nullptr;
        this->entity_placement_mode = MapEntityPlacementMode::None;
    }
    
    if (this->device_link_start_label == label)
        this->device_link_start_label = nullptr;
    
    if (this->pipes->startLabel() == label)
        this->pipes->clearPlacement();
    
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
    
    this->pipes->removeConnectedToLabel(label);
    
    for (int i = this->list_entity_markers_selected.size() - 1; i >= 0; i--)
    {
        if (this->list_entity_markers_selected[i].label == label)
            this->list_entity_markers_selected.removeAt(i);
    }
    
    for (int i = 0; i < this->list_entity_markers.size(); i++)
    {
        if (this->list_entity_markers[i].label != label)
            continue;
        
        this->list_entity_markers.removeAt(i);
        break;
    }
    
    if (!hasSelection())
        this->selected_entity.reset();
    
    label->hide();
    label->deleteLater();
}

void MapCanvasEntities::onMarkerClicked(MapEntityMarkerLabel *label)
{
    const MapEntityMarker marker = markerByLabel(label);
    if (marker.entity.type == InfrastructureEntity::Unknown)
        return;
    
    if (QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))
    {
        if (isMarkerSelected(label))
        {
            for (int i = 0; i < this->list_entity_markers_selected.size(); i++)
            {
                if (this->list_entity_markers_selected[i].label != label)
                    continue;
                
                this->list_entity_markers_selected.removeAt(i);
                break;
            }
            label->clearHighlight();
        }
        else
        {
            label->setHighlightSelected();
            this->list_entity_markers_selected.append(marker);
        }
        
        emit signalEntityMarkerSelected(hasSelection());
        if (this->map_canvas)
            this->map_canvas->update();
        return;
    }
    
    clearSelection();
    this->selected_entity = marker.entity;
    label->setHighlightSelected();
    this->list_entity_markers_selected.append(marker);
    emit signalEntityMarkerSelected(true);
    
    if (this->hydraulic_data)
        this->hydraulic_data->setSelectedUuid(marker.entity.type, marker.entity.uuid);
    
    if (this->map_canvas)
        this->map_canvas->update();
}

void MapCanvasEntities::onMarkerContextMenuRequested(MapEntityMarkerLabel *label,
                                                     const QPoint &global_position)
{
    if (!label)
        return;
    
    const bool multiple_entities_selected =
        isMarkerSelected(label) && this->list_entity_markers_selected.size() > 1;
    label->showContextMenu(global_position, multiple_entities_selected);
}

void MapCanvasEntities::onRectangleSelect(const CoordinateWGS84Rect &rect, RectangleSelectMode mode)
{
    const double north = rect.north_west.lat;
    const double west = rect.north_west.lon;
    const double south = rect.south_east.lat;
    const double east = rect.south_east.lon;
    
    if (mode == RectangleSelectMode::Replace)
        clearSelection();
    
    const std::function<void(const MapEntityMarker &)> select_marker =
        [this, north, west, south, east](const MapEntityMarker &marker)
    {
        if (!marker.label)
            return;
        
        const CoordinateWGS84 &coordinate = marker.coord_wgs84;
        if (coordinate.lat < south || coordinate.lat > north ||
            coordinate.lon < west || coordinate.lon > east)
        {
            return;
        }
        
        for (const MapEntityMarker &selected_marker : this->list_entity_markers_selected)
        {
            if (selected_marker.label == marker.label)
                return;
        }
        
        this->list_entity_markers_selected.append(marker);
        marker.label->setHighlightSelected();
    };
    
    for (const MapEntityMarker &marker : this->list_entity_markers)
        select_marker(marker);
    
    for (const DeviceLinkCanvasItem &device_link : this->list_device_links)
    {
        if (!device_link.device_label)
            continue;
        
        MapEntityMarker marker;
        marker.entity = device_link.entity;
        marker.coord_wgs84 = device_link.geometry.center_coordinate;
        marker.label = device_link.device_label;
        marker.path_pixmap = device_link.path_pixmap;
        select_marker(marker);
    }
    
    this->pipes->selectPipesWithSelectedEndpoints(this->list_entity_markers_selected);
    emit signalEntityMarkerSelected(hasSelection());
    
    if (this->map_canvas)
        this->map_canvas->update();
}

void MapCanvasEntities::updateConnectionTarget(const QPointF &mouse_pos)
{
    QPointer<MapEntityMarkerLabel> nearest_label = nullptr;
    double nearest_distance_squared = connection_hover_distance * connection_hover_distance;
    const bool placing_device_link =
        this->entity_floating &&
        this->entity_placement_mode == MapEntityPlacementMode::CreateNew &&
        isHydraulicDeviceLink(this->entity_current);
    const bool placing_pipe =
        this->entity_floating &&
        this->entity_placement_mode == MapEntityPlacementMode::CreateNew &&
        isHydraulicPipeGeometry(this->entity_current);
    
    if (placing_device_link || placing_pipe)
    {
        for (const MapEntityMarker &marker : this->list_entity_markers)
        {
            if (!marker.label || !isHydraulicConnectionNode(marker.entity.type))
                continue;
            
            if (placing_device_link &&
                this->device_link_start_label &&
                marker.label == this->device_link_start_label)
            {
                continue;
            }
            
            const QPointF point = this->map_model->screenFromWgs84(
                marker.coord_wgs84, this->map_canvas->size());
            const double distance_x = point.x() - mouse_pos.x();
            const double distance_y = point.y() - mouse_pos.y();
            const double distance_squared = distance_x * distance_x + distance_y * distance_y;
            
            if (distance_squared > nearest_distance_squared)
                continue;
            
            nearest_distance_squared = distance_squared;
            nearest_label = marker.label;
        }
    }
    
    if (this->connection_target_label == nearest_label)
        return;
    
    this->connection_target_label = nearest_label;
    if (this->map_canvas)
        this->map_canvas->update();
}

void MapCanvasEntities::clearConnectionTarget()
{
    if (this->connection_target_label.isNull())
        return;
    
    this->connection_target_label = nullptr;
    if (this->map_canvas)
        this->map_canvas->update();
}

bool MapCanvasEntities::isMarkerSelected(MapEntityMarkerLabel *label) const
{
    if (!label)
        return false;
    
    for (const MapEntityMarker &marker : this->list_entity_markers_selected)
    {
        if (marker.label == label)
            return true;
    }
    
    return false;
}

bool MapCanvasEntities::hasSelection() const
{
    return !this->list_entity_markers_selected.isEmpty() || this->pipes->hasSelection();
}

void MapCanvasEntities::clearSelection()
{
    for (MapEntityMarker &marker : this->list_entity_markers_selected)
    {
        if (marker.label)
            marker.label->clearHighlight();
    }
    
    this->list_entity_markers_selected.clear();
    this->pipes->clearSelection();
    this->selected_entity.reset();
}

bool MapCanvasEntities::selectDeviceLinkAt(const QPointF &position)
{
    MapEntityMarkerLabel *device_label = deviceLinkLabelAt(position);
    if (!device_label)
        return false;
    
    onMarkerClicked(device_label);
    return true;
}

MapEntityMarkerLabel *MapCanvasEntities::deviceLinkLabelAt(const QPointF &position)
{
    if (this->entity_placement_mode != MapEntityPlacementMode::None)
        return nullptr;
    
    double nearest_distance_squared = link_hit_distance * link_hit_distance;
    MapEntityMarkerLabel *nearest_device_label = nullptr;
    
    for (DeviceLinkCanvasItem &device_link : this->list_device_links)
    {
        if (!device_link.start_label || !device_link.end_label || !device_link.device_label)
            continue;
        
        const MapEntityMarker start_marker = markerByLabel(device_link.start_label);
        const MapEntityMarker end_marker = markerByLabel(device_link.end_label);
        if (!isHydraulicConnectionNode(start_marker.entity.type) ||
            !isHydraulicConnectionNode(end_marker.entity.type))
        {
            continue;
        }
        
        const QPointF start_point = this->map_model->screenFromWgs84(
            start_marker.coord_wgs84, this->map_canvas->size());
        const QPointF center_point = this->map_model->screenFromWgs84(
            device_link.geometry.center_coordinate, this->map_canvas->size());
        const QPointF end_point = this->map_model->screenFromWgs84(
            end_marker.coord_wgs84, this->map_canvas->size());
        const double distance_squared = qMin(
            distanceSquaredToSegment(position, start_point, center_point),
            distanceSquaredToSegment(position, center_point, end_point));
        
        if (distance_squared > nearest_distance_squared)
            continue;
        
        nearest_distance_squared = distance_squared;
        nearest_device_label = device_link.device_label;
    }
    
    return nearest_device_label;
}

bool MapCanvasEntities::isDeviceLinkAt(const QPointF &position)
{
    return deviceLinkLabelAt(position) != nullptr;
}

bool MapCanvasEntities::showPipeContextMenuAt(const QPointF &position,
                                              const QPoint &global_position)
{
    if (this->entity_placement_mode != MapEntityPlacementMode::None)
        return false;
    
    const MapCanvasPipes::PipeVertexHit vertex_hit = this->pipes->pipeVertexAt(position);
    if (vertex_hit.isValid())
    {
        const QUuid pipe_uuid = vertex_hit.pipe_uuid;
        const int vertex_index = vertex_hit.vertex_index;
        
        selectPipe(pipe_uuid);
        QMenu *menu = new QMenu(this->map_canvas);
        QAction *action_move = menu->addAction("Move");
        QAction *action_delete = menu->addAction("Delete");
        QAction *action_convert_to_junction = menu->addAction("Convert to junction");
        
        connect(action_move, &QAction::triggered, this, [this, pipe_uuid, vertex_index]()
                {
                    startPipeVertexMove(pipe_uuid, vertex_index);
                });
        connect(action_delete, &QAction::triggered, this, [this, pipe_uuid, vertex_index]()
                {
                    this->pipes->deletePipeVertex(pipe_uuid, vertex_index);
                });
        connect(action_convert_to_junction, &QAction::triggered, this,
                [this, pipe_uuid, vertex_index]()
                {
                    QMessageBox *message_box = new QMessageBox(
                        QMessageBox::Question,
                        "Convert pipe vertex",
                        "Do you really want to convert this pipe vertex to a junction?",
                        QMessageBox::Yes | QMessageBox::No,
                        this->map_canvas);
                    message_box->setDefaultButton(QMessageBox::No);
                    
                    connect(message_box, &QMessageBox::finished, this,
                            [this, pipe_uuid, vertex_index](int result)
                            {
                                if (result == QMessageBox::Yes)
                                    convertPipeVertexToJunction(pipe_uuid, vertex_index);
                            });
                    connect(message_box, &QMessageBox::finished, message_box, &QObject::deleteLater);
                    message_box->open();
                });
        
        connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
        menu->popup(global_position);
        return true;
    }
    
    const MapCanvasPipes::PipeSegmentHit segment_hit =
        this->pipes->pipeSegmentAt(position, this->list_entity_markers);
    if (!segment_hit.isValid())
        return false;
    
    const CoordinateWGS84 coordinate = this->map_model->wgs84FromScreen(
        segment_hit.nearest_point.toPoint(), this->map_canvas->size());
    
    selectPipe(segment_hit.pipe_uuid);
    this->pipes->addPipeVertex(segment_hit.pipe_uuid, segment_hit.insert_index, coordinate);
    return true;
}

void MapCanvasEntities::selectPipe(const QUuid &pipe_uuid)
{
    clearSelection();
    
    const std::optional<InfrastructureEntityReference> selected_pipe =
        this->pipes->selectPipe(pipe_uuid);
    if (!selected_pipe.has_value())
        return;
    
    this->selected_entity = selected_pipe.value();
    emit signalEntityMarkerSelected(true);
    
    if (this->map_canvas)
        this->map_canvas->update();
}

void MapCanvasEntities::startPipeVertexMove(const QUuid &pipe_uuid, int vertex_index)
{
    stopEntityPositioning();
    
    if (!this->pipes->startPipeVertexMove(pipe_uuid, vertex_index))
        return;
    
    selectPipe(pipe_uuid);
    this->entity_current = InfrastructureEntity::Pipe;
    this->entity_placement_mode = MapEntityPlacementMode::MoveExisting;
    this->mouse_pos_last = this->map_canvas->mapFromGlobal(QCursor::pos());
    this->map_canvas->setFocusPolicy(Qt::StrongFocus);
    this->map_canvas->setFocus(Qt::OtherFocusReason);
    setMoveCursor(true);
}

void MapCanvasEntities::convertPipeVertexToJunction(const QUuid &pipe_uuid, int vertex_index)
{
    const std::optional<CoordinateWGS84> vertex_coordinate =
        this->pipes->pipeVertexCoordinate(pipe_uuid, vertex_index);
    if (!vertex_coordinate.has_value())
        return;
    
    InfrastructureEntityReference junction_reference;
    junction_reference.type = InfrastructureEntity::Junction;
    junction_reference.uuid = QUuid::createUuid();
    
    MapEntityMarker junction_marker;
    junction_marker.entity = junction_reference;
    junction_marker.coord_wgs84 = vertex_coordinate.value();
    junction_marker.path_pixmap = pixmapPathForEntity(InfrastructureEntity::Junction);
    junction_marker.label = new MapEntityMarkerLabel(this->map_canvas);
    junction_marker.label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    
    connect(junction_marker.label, &MapEntityMarkerLabel::signalDeleteRequested,
            this, &MapCanvasEntities::onMarkerDeleteRequested);
    connect(junction_marker.label, &MapEntityMarkerLabel::signalMoveRequested,
            this, &MapCanvasEntities::onMarkerMoveRequested);
    connect(junction_marker.label, &MapEntityMarkerLabel::signalMoveSelectedRequested,
            this, &MapCanvasEntities::onMarkerMoveSelectedRequested);
    connect(junction_marker.label, &MapEntityMarkerLabel::signalClicked,
            this, &MapCanvasEntities::onMarkerClicked);
    connect(junction_marker.label, &MapEntityMarkerLabel::signalContextMenuRequested,
            this, &MapCanvasEntities::onMarkerContextMenuRequested);
    
    const int width = calculateEntityWidth();
    const QPixmap pixmap = QPixmap(junction_marker.path_pixmap).scaledToWidth(
        width, Qt::SmoothTransformation);
    junction_marker.label->setPixmap(pixmap);
    junction_marker.label->resize(pixmap.size());
    
    if (!this->pipes->splitPipeAtVertex(pipe_uuid, vertex_index,
                                        junction_reference, junction_marker.label))
    {
        junction_marker.label->deleteLater();
        return;
    }
    
    this->list_entity_markers.append(junction_marker);
    
    clearSelection();
    junction_marker.label->setHighlightSelected();
    this->list_entity_markers_selected.append(junction_marker);
    this->selected_entity = junction_reference;
    emit signalEntityMarkerSelected(true);
    
    positionMarkers();
    if (this->map_canvas)
        this->map_canvas->update();
}

bool MapCanvasEntities::selectPipeAt(const QPointF &position)
{
    if (this->entity_placement_mode != MapEntityPlacementMode::None)
        return false;
    
    const std::optional<InfrastructureEntityReference> pipe =
        this->pipes->pipeAt(position, this->list_entity_markers);
    if (!pipe.has_value())
        return false;
    
    selectPipe(pipe->uuid);
    return true;
}

bool MapCanvasEntities::isPipeAt(const QPointF &position)
{
    if (this->entity_placement_mode != MapEntityPlacementMode::None)
        return false;
    
    return this->pipes->pipeAt(position, this->list_entity_markers).has_value();
}

MapEntityMarker MapCanvasEntities::markerByLabel(MapEntityMarkerLabel *label)
{
    for (MapEntityMarker &marker : this->list_entity_markers)
    {
        if (marker.label == label)
            return marker;
    }
    
    for (DeviceLinkCanvasItem &device_link : this->list_device_links)
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
    
    MapEntityMarker marker;
    marker.entity.type = InfrastructureEntity::Unknown;
    return marker;
}

QString MapCanvasEntities::pixmapPathForEntity(InfrastructureEntity entity) const
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
