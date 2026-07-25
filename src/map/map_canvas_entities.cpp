#include "map_canvas_entities.h"
#include "map_canvas_pipes.h"
#include "map_canvas_devicelinks.h"
#include "map_canvas_markers.h"
#include "map_canvas_selection.h"
#include "map_canvas_widget.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMessageBox>

namespace
{
constexpr double connection_hover_distance = 18.0;

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

}

MapCanvasEntities::MapCanvasEntities(MapModel *map_model, HydraulicData *hydraulic_data, MapCanvasWidget *map_canvas)
    : QObject(map_canvas),
    map_model(map_model),
    hydraulic_data(hydraulic_data),
    map_canvas(map_canvas)
{
    this->point_markers = new MapCanvasMarkers(this->map_model, this->map_canvas, this);
    this->device_links = new MapCanvasDeviceLinks(this->map_model, this->map_canvas, this);
    this->pipes = new MapCanvasPipes(this->map_model, this->map_canvas, this);
    this->selection = new MapCanvasSelection(this->pipes, this);
    
    connect(this->point_markers, &MapCanvasMarkers::markerDeleteRequested,
            this, &MapCanvasEntities::onMarkerDeleteRequested);
    connect(this->point_markers, &MapCanvasMarkers::markerMoveRequested,
            this, &MapCanvasEntities::onMarkerMoveRequested);
    connect(this->point_markers, &MapCanvasMarkers::markerMoveSelectedRequested,
            this, &MapCanvasEntities::onMarkerMoveSelectedRequested);
    connect(this->point_markers, &MapCanvasMarkers::markerClicked,
            this, &MapCanvasEntities::onMarkerClicked);
    connect(this->point_markers, &MapCanvasMarkers::markerContextMenuRequested,
            this, &MapCanvasEntities::onMarkerContextMenuRequested);
    
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
        this->selection->setMouseTransparency(false);
        this->move_selected_entities = false;
    }
    
    clearConnectionTarget();
    this->device_links->clearPlacement();
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
        if (this->map_canvas)
            this->map_canvas->update();
        return;
    }
    
    if (previous_mode == MapEntityPlacementMode::MoveExisting)
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
    
    bool device_link_positioned = false;
    if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting)
    {
        device_link_positioned = this->device_links->updateMove(
            this->entity_floating, event->position());
    }
    else if (this->entity_placement_mode == MapEntityPlacementMode::CreateNew &&
             isHydraulicDeviceLink(this->entity_current))
    {
        device_link_positioned = this->device_links->positionFloatingLabel(
            this->entity_floating,
            event->position(),
            this->connection_target_label,
            this->point_markers->markers());
    }
    
    if (!device_link_positioned)
    {
        const QPoint marker_pos(qRound(event->position().x()),
                                qRound(event->position().y()) - this->entity_floating->height());
        this->entity_floating->move(marker_pos);
    }
    
    if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting &&
        !device_link_positioned)
    {
        const CoordinateWGS84 coordinate = this->map_model->wgs84FromScreen(
            event->position().toPoint(), this->map_canvas->size());
        this->point_markers->setCoordinate(this->entity_floating, coordinate);
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
        this->selection->setMouseTransparency(false);
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
        
        if (this->point_markers->setCoordinate(moved_label, coordinate))
        {
            setMoveCursor(false);
            moved_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            this->entity_floating = nullptr;
            this->entity_placement_mode = MapEntityPlacementMode::None;
            positionMarkers();
            
            if (this->map_canvas)
                this->map_canvas->update();
            
            return true;
        }
        
        if (this->device_links->setCenterCoordinate(moved_label, coordinate))
        {
            setMoveCursor(false);
            moved_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            this->entity_floating = nullptr;
            this->entity_placement_mode = MapEntityPlacementMode::None;
            positionMarkers();
            
            if (this->map_canvas)
                this->map_canvas->update();
            
            return true;
        }
        
        setMoveCursor(false);
        moved_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        this->entity_floating = nullptr;
        this->entity_placement_mode = MapEntityPlacementMode::None;
        positionMarkers();
        
        if (this->map_canvas)
            this->map_canvas->update();
        
        return false;
    }
    
    if (this->entity_placement_mode != MapEntityPlacementMode::CreateNew)
        return false;
    
    InfrastructureEntityReference reference;
    reference.type = this->entity_current;
    reference.uuid = QUuid::createUuid();
    
    this->point_markers->addMarker(
        reference,
        coordinate,
        pixmapPathForEntity(this->entity_current),
        calculateEntityWidth(),
        this->entity_floating);
    
    this->entity_floating_hide_until = event->position().toPoint();
    this->entity_floating = nullptr;
    positionMarkers();
    startEntityPositioningInternal();
    return true;
}

bool MapCanvasEntities::anchorDeviceLink(QMouseEvent *event)
{
    const MapCanvasDeviceLinks::AnchorResult result = this->device_links->anchor(
        this->entity_current,
        this->connection_target_label,
        this->entity_floating,
        this->point_markers->markers(),
        pixmapPathForEntity(this->entity_current),
        calculateEntityWidth());
    
    if (result.status == MapCanvasDeviceLinks::AnchorStatus::StartSet)
    {
        this->connection_target_label = nullptr;
        updateConnectionTarget(event->position());
        return true;
    }
    
    if (result.status != MapCanvasDeviceLinks::AnchorStatus::Completed ||
        !result.device_label)
    {
        return true;
    }
    
    MapEntityMarkerLabel *device_label = result.device_label.data();
    connect(device_label, &MapEntityMarkerLabel::signalDeleteRequested,
            this, &MapCanvasEntities::onMarkerDeleteRequested);
    connect(device_label, &MapEntityMarkerLabel::signalMoveRequested,
            this, &MapCanvasEntities::onMarkerMoveRequested);
    connect(device_label, &MapEntityMarkerLabel::signalMoveSelectedRequested,
            this, &MapCanvasEntities::onMarkerMoveSelectedRequested);
    connect(device_label, &MapEntityMarkerLabel::signalClicked,
            this, &MapCanvasEntities::onMarkerClicked);
    connect(device_label, &MapEntityMarkerLabel::signalContextMenuRequested,
            this, &MapCanvasEntities::onMarkerContextMenuRequested);
    
    this->entity_floating_hide_until = event->position().toPoint();
    this->entity_floating = nullptr;
    this->connection_target_label = nullptr;
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
    
    this->point_markers->scaleLabels(width);
    this->device_links->scaleLabels(width);
    
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
    MapEntityMarkerLabel *label_to_skip = nullptr;
    if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting &&
        !this->move_selected_entities)
    {
        label_to_skip = this->entity_floating;
    }
    
    this->point_markers->positionLabels(label_to_skip);
    this->device_links->positionLabels();
}

void MapCanvasEntities::setPointMarkerMouseTransparency(bool transparent)
{
    this->point_markers->setMouseTransparency(transparent);
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

void MapCanvasEntities::moveSelectedEntities(const QPointF &from_position,
                                             const QPointF &to_position)
{
    const CoordinateWGS84 from_coordinate = this->map_model->wgs84FromScreen(
        from_position.toPoint(), this->map_canvas->size());
    const CoordinateWGS84 to_coordinate = this->map_model->wgs84FromScreen(
        to_position.toPoint(), this->map_canvas->size());
    const double longitude_delta = to_coordinate.lon - from_coordinate.lon;
    const double latitude_delta = to_coordinate.lat - from_coordinate.lat;
    const QList<MapEntityMarker> &selected_markers = this->selection->selectedMarkers();
    
    for (const MapEntityMarker &selected_marker : selected_markers)
    {
        if (!selected_marker.label)
            continue;
        
        if (!this->point_markers->moveByDelta(
                selected_marker.label, longitude_delta, latitude_delta))
        {
            this->device_links->moveCenterByDelta(
                selected_marker.label, longitude_delta, latitude_delta);
        }
    }
    
    this->pipes->moveIntermediateVerticesWithSelectedEndpoints(
        selected_markers, longitude_delta, latitude_delta);
}

void MapCanvasEntities::paintMarkers(QPainter &paint)
{
    this->pipes->paint(
        paint,
        this->point_markers->markers(),
        this->entity_placement_mode == MapEntityPlacementMode::CreateNew &&
            isHydraulicPipeGeometry(this->entity_current),
        this->mouse_pos_last,
        this->connection_target_label);
    this->device_links->paint(
        paint,
        this->point_markers->markers(),
        this->selection->selectedMarkers(),
        this->entity_placement_mode == MapEntityPlacementMode::CreateNew &&
            isHydraulicDeviceLink(this->entity_current),
        this->mouse_pos_last,
        this->connection_target_label);
    
    const bool draw_moving_label_at_mouse =
        this->entity_placement_mode == MapEntityPlacementMode::MoveExisting &&
        this->entity_floating &&
        !this->move_selected_entities;
    this->point_markers->paintConnectionPoints(
        paint,
        this->connection_target_label,
        this->entity_floating,
        draw_moving_label_at_mouse,
        this->mouse_pos_last);
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
    if (!label || !this->selection->isMarkerSelected(label) ||
        this->selection->selectedMarkerCount() < 2)
        return;
    
    onMarkerMoveRequested(label);
    
    if (this->entity_placement_mode != MapEntityPlacementMode::MoveExisting ||
        this->entity_floating != label)
    {
        return;
    }
    
    this->move_selected_entities = true;
    this->selection->setMouseTransparency(true);
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
    for (const MapEntityMarker &marker : this->selection->selectedMarkers())
    {
        if (marker.label)
            labels_to_delete.append(marker.label);
    }
    
    for (MapEntityMarkerLabel *label : labels_to_delete)
        deleteMarker(label);
    
    this->pipes->deleteSelected();
    this->selection->clear();
    
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
    
    if (this->pipes->startLabel() == label)
        this->pipes->clearPlacement();
    
    this->device_links->removeConnectedToLabel(label);
    this->pipes->removeConnectedToLabel(label);
    
    this->selection->removeMarker(label);
    const bool point_marker_removed = this->point_markers->removeMarker(label);
    
    if (!point_marker_removed)
    {
        label->hide();
        label->deleteLater();
    }
}

void MapCanvasEntities::onMarkerClicked(MapEntityMarkerLabel *label)
{
    const MapEntityMarker marker = markerByLabel(label);
    if (marker.entity.type == InfrastructureEntity::Unknown)
        return;
    
    if (QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))
    {
        this->selection->toggleMarker(marker);
        emit signalEntityMarkerSelected(this->selection->hasSelection());
        
        if (this->map_canvas)
            this->map_canvas->update();
        return;
    }
    
    this->selection->replaceWithMarker(marker);
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
        this->selection->isMarkerSelected(label) &&
        this->selection->selectedMarkerCount() > 1;
    label->showContextMenu(global_position, multiple_entities_selected);
}

void MapCanvasEntities::onRectangleSelect(const CoordinateWGS84Rect &rect,
                                          RectangleSelectMode mode)
{
    this->selection->selectInRectangle(
        rect,
        this->point_markers->markers(),
        this->device_links->markers(),
        mode == RectangleSelectMode::Replace);
    emit signalEntityMarkerSelected(this->selection->hasSelection());
    
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
        for (const MapEntityMarker &marker : this->point_markers->markers())
        {
            if (!marker.label || !isHydraulicConnectionNode(marker.entity.type))
                continue;
            
            if (placing_device_link &&
                this->device_links->startLabel() &&
                marker.label == this->device_links->startLabel())
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

bool MapCanvasEntities::selectDeviceLinkAt(const QPointF &position)
{
    if (this->entity_placement_mode != MapEntityPlacementMode::None)
        return false;
    
    MapEntityMarkerLabel *device_label = this->device_links->labelAt(
        position, this->point_markers->markers());
    if (!device_label)
        return false;
    
    onMarkerClicked(device_label);
    return true;
}

bool MapCanvasEntities::isDeviceLinkAt(const QPointF &position)
{
    if (this->entity_placement_mode != MapEntityPlacementMode::None)
        return false;
    
    return this->device_links->labelAt(position, this->point_markers->markers()) != nullptr;
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
        this->pipes->pipeSegmentAt(position, this->point_markers->markers());
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
    const std::optional<InfrastructureEntityReference> selected_pipe =
        this->selection->replaceWithPipe(pipe_uuid);
    if (!selected_pipe.has_value())
        return;
    
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
    
    const MapEntityMarker junction_marker = this->point_markers->addMarker(
        junction_reference,
        vertex_coordinate.value(),
        pixmapPathForEntity(InfrastructureEntity::Junction),
        calculateEntityWidth());
    
    if (!this->pipes->splitPipeAtVertex(pipe_uuid, vertex_index,
                                        junction_reference, junction_marker.label))
    {
        this->point_markers->removeMarker(junction_marker.label);
        return;
    }
    
    this->selection->replaceWithMarker(junction_marker);
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
        this->pipes->pipeAt(position, this->point_markers->markers());
    if (!pipe.has_value())
        return false;
    
    selectPipe(pipe->uuid);
    return true;
}

bool MapCanvasEntities::isPipeAt(const QPointF &position)
{
    if (this->entity_placement_mode != MapEntityPlacementMode::None)
        return false;
    
    return this->pipes->pipeAt(position, this->point_markers->markers()).has_value();
}

MapEntityMarker MapCanvasEntities::markerByLabel(MapEntityMarkerLabel *label)
{
    const std::optional<MapEntityMarker> point_marker =
        this->point_markers->markerByLabel(label);
    if (point_marker.has_value())
        return point_marker.value();
    
    const std::optional<MapEntityMarker> device_link_marker =
        this->device_links->markerByLabel(label);
    if (device_link_marker.has_value())
        return device_link_marker.value();
    
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
