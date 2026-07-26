#include "map_canvas_entities.h"
#include "map_canvas_devicelinks.h"
#include "map_canvas_markers.h"
#include "map_canvas_pipes.h"
#include "map_canvas_placement.h"
#include "map_canvas_selection.h"
#include "map_canvas_widget.h"

#include <QApplication>
#include <QCursor>

namespace
{
bool isHydraulicConnectionNode(InfrastructureEntity entity)
{
    return entity == InfrastructureEntity::Junction ||
           entity == InfrastructureEntity::Reservoir ||
           entity == InfrastructureEntity::Tank;
}

bool isHydraulicDeviceLink(InfrastructureEntity entity)
{
    return entity == InfrastructureEntity::Pump || entity == InfrastructureEntity::Valve;
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

MapCanvasEntities::MapCanvasEntities(MapModel *map_model, HydraulicData *hydraulic_data,
                                     MapCanvasWidget *map_canvas)
    : QObject(map_canvas), map_model(map_model), hydraulic_data(hydraulic_data),
    map_canvas(map_canvas)
{
    this->point_markers = new MapCanvasMarkers(this->map_model, this->map_canvas, this);
    this->device_links = new MapCanvasDeviceLinks(this->map_model, this->map_canvas, this);
    this->pipes = new MapCanvasPipes(this->map_model, this->map_canvas, this);
    this->selection = new MapCanvasSelection(this->map_model, this->map_canvas,
                                             this->point_markers, this->device_links,
                                             this->pipes, this);
    this->placement = new MapCanvasPlacement(this->map_canvas, this);
    
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
    
    connect(this->device_links, &MapCanvasDeviceLinks::markerDeleteRequested,
            this, &MapCanvasEntities::onMarkerDeleteRequested);
    connect(this->device_links, &MapCanvasDeviceLinks::markerMoveRequested,
            this, &MapCanvasEntities::onMarkerMoveRequested);
    connect(this->device_links, &MapCanvasDeviceLinks::markerMoveSelectedRequested,
            this, &MapCanvasEntities::onMarkerMoveSelectedRequested);
    connect(this->device_links, &MapCanvasDeviceLinks::markerClicked,
            this, &MapCanvasEntities::onMarkerClicked);
    connect(this->device_links, &MapCanvasDeviceLinks::markerContextMenuRequested,
            this, &MapCanvasEntities::onMarkerContextMenuRequested);
    
    connect(this->pipes, &MapCanvasPipes::pipeSelectionRequested,
            this, &MapCanvasEntities::selectPipe);
    connect(this->pipes, &MapCanvasPipes::pipeVertexMoveRequested,
            this, &MapCanvasEntities::startPipeVertexMove);
    connect(this->pipes, &MapCanvasPipes::pipeVertexConversionRequested,
            this, &MapCanvasEntities::convertPipeVertexToJunction);
    
    connect(this->map_model, &MapModel::zoomChanged, this, &MapCanvasEntities::scaleMarkers);
    connect(this->map_model, &MapModel::centerChangedWGS84, this,
            [this](const CoordinateWGS84 &)
            {
                positionMarkers();
            });
}

void MapCanvasEntities::startEntityPositioning(InfrastructureEntity entity)
{
    stopEntityPositioning();
    
    if (isHydraulicCanvasLink(entity))
        this->point_markers->setMouseTransparency(true);
    
    const int width = isHydraulicCanvasLink(entity) ? this->point_markers->entityWidth() : 150;
    this->placement->startCreate(entity, this->point_markers->pixmapPathForEntity(entity), width);
}

void MapCanvasEntities::stopEntityPositioning()
{
    if (this->placement->movingSelected())
        this->selection->setMouseTransparency(false);
    
    this->device_links->clearPlacement();
    this->pipes->clearPlacement();
    this->pipes->cancelPipeVertexMove();
    this->point_markers->setMouseTransparency(false);
    this->placement->stop();
    positionMarkers();
    updateCanvas();
}

void MapCanvasEntities::floatEntity(QMouseEvent *event)
{
    if (this->placement->isMoving())
        this->placement->setMoveCursor(true);
    
    this->placement->updateMousePosition(event->position());
    
    if (this->placement->movingSelected())
    {
        this->selection->moveSelected(this->placement->previousMousePosition(),
                                      this->placement->mousePosition());
        positionMarkers();
        updateCanvas();
        event->accept();
        return;
    }
    
    updateConnectionTarget(event->position());
    
    if (this->placement->isMoving() && this->pipes->isPipeVertexMoveActive())
    {
        if (!this->pipes->updatePipeVertexMove(event->position()))
            this->placement->completeMove();
        event->accept();
        return;
    }
    
    MapEntityMarkerLabel *floating_label = this->placement->floatingLabel();
    if (!floating_label || !this->placement->revealFloatingLabelIfReady())
        return;
    
    bool device_link_positioned = false;
    if (this->placement->isMoving())
    {
        device_link_positioned = this->device_links->updateMove(floating_label, event->position());
    }
    else if (this->placement->isCreating() &&
             isHydraulicDeviceLink(this->placement->entity()))
    {
        device_link_positioned = this->device_links->positionFloatingLabel(
            floating_label, event->position(), this->placement->connectionTarget(),
            this->point_markers->markers());
    }
    
    if (!device_link_positioned)
        this->placement->moveFloatingLabelTopLeft(event->position());
    
    if (this->placement->isMoving() && !device_link_positioned)
    {
        const CoordinateWGS84 coordinate = this->map_model->wgs84FromScreen(
            event->position().toPoint(), this->map_canvas->size());
        this->point_markers->setCoordinate(floating_label, coordinate);
    }
    
    updateCanvas();
    event->accept();
}

bool MapCanvasEntities::anchorMarker(QMouseEvent *event)
{
    if (anchorPipeVertexMove(event))
        return true;
    
    MapEntityMarkerLabel *floating_label = this->placement->floatingLabel();
    if (!floating_label)
        return false;
    
    if (this->placement->movingSelected())
    {
        this->selection->moveSelected(this->placement->mousePosition(), event->position());
        this->selection->setMouseTransparency(false);
        this->placement->completeMove();
        positionMarkers();
        updateCanvas();
        return true;
    }
    
    if (this->placement->isCreating())
    {
        if (isHydraulicDeviceLink(this->placement->entity()))
            return anchorDeviceLink(event);
        if (isHydraulicPipeGeometry(this->placement->entity()))
            return anchorPipe(event);
    }
    
    const CoordinateWGS84 coordinate = this->map_model->wgs84FromScreen(
        event->position().toPoint(), this->map_canvas->size());
    
    if (this->placement->isMoving())
    {
        const bool moved = this->point_markers->setCoordinate(floating_label, coordinate) ||
                           this->device_links->setCenterCoordinate(floating_label, coordinate);
        this->placement->completeMove();
        positionMarkers();
        updateCanvas();
        return moved;
    }
    
    if (!this->placement->isCreating())
        return false;
    
    InfrastructureEntityReference reference;
    reference.type = this->placement->entity();
    reference.uuid = QUuid::createUuid();
    MapEntityMarkerLabel *created_label = this->placement->takeCreatedLabel();
    if (!created_label)
        return false;
    
    this->point_markers->addMarker(
        reference, coordinate, this->point_markers->pixmapPathForEntity(reference.type),
        this->point_markers->entityWidth(), created_label);
    this->placement->setFloatingHiddenUntil(event->position().toPoint());
    positionMarkers();
    this->placement->rearmCreate(this->point_markers->pixmapPathForEntity(reference.type), 150);
    return true;
}

bool MapCanvasEntities::anchorDeviceLink(QMouseEvent *event)
{
    const InfrastructureEntity entity = this->placement->entity();
    const MapCanvasDeviceLinks::AnchorResult result = this->device_links->anchor(
        entity, this->placement->connectionTarget(), this->placement->floatingLabel(),
        this->point_markers->markers(), this->point_markers->pixmapPathForEntity(entity),
        this->point_markers->entityWidth());
    
    if (result.status == MapCanvasDeviceLinks::AnchorStatus::StartSet)
    {
        this->placement->clearConnectionTarget();
        updateConnectionTarget(event->position());
        updateCanvas();
        return true;
    }
    
    if (result.status != MapCanvasDeviceLinks::AnchorStatus::Completed)
        return true;
    
    this->placement->setFloatingHiddenUntil(event->position().toPoint());
    this->placement->takeCreatedLabel();
    this->placement->clearConnectionTarget();
    this->placement->rearmCreate(this->point_markers->pixmapPathForEntity(entity),
                                 this->point_markers->entityWidth());
    updateCanvas();
    return true;
}

bool MapCanvasEntities::anchorPipe(QMouseEvent *event)
{
    MapEntityMarkerLabel *connection_target = this->placement->connectionTarget();
    if (!this->pipes->hasStartLabel())
    {
        if (!connection_target)
            return true;
        
        const MapEntityMarker start_marker = markerByLabel(connection_target);
        if (!isHydraulicConnectionNode(start_marker.entity.type))
            return true;
        
        this->pipes->startPipe(connection_target);
        this->placement->clearConnectionTarget();
        return true;
    }
    
    if (connection_target)
    {
        if (connection_target == this->pipes->startLabel())
            return true;
        
        const MapEntityMarker start_marker = markerByLabel(this->pipes->startLabel());
        const MapEntityMarker end_marker = markerByLabel(connection_target);
        if (!isHydraulicConnectionNode(start_marker.entity.type) ||
            !isHydraulicConnectionNode(end_marker.entity.type))
        {
            return true;
        }
        
        if (!this->pipes->completePipe(start_marker.entity, end_marker.entity, connection_target))
            return true;
        
        this->placement->setFloatingHiddenUntil(event->position().toPoint());
        MapEntityMarkerLabel *placement_icon = this->placement->takeCreatedLabel();
        this->placement->clearConnectionTarget();
        if (placement_icon)
        {
            placement_icon->hide();
            placement_icon->deleteLater();
        }
        this->placement->rearmCreate(
            this->point_markers->pixmapPathForEntity(InfrastructureEntity::Pipe),
            this->point_markers->entityWidth());
        return true;
    }
    
    const CoordinateWGS84 intermediate_vertex = this->map_model->wgs84FromScreen(
        event->position().toPoint(), this->map_canvas->size());
    this->pipes->appendIntermediateVertex(intermediate_vertex);
    return true;
}

bool MapCanvasEntities::anchorPipeVertexMove(QMouseEvent *event)
{
    if (!this->placement->isMoving() || !this->pipes->isPipeVertexMoveActive())
        return false;
    
    this->pipes->finishPipeVertexMove(event->position());
    this->placement->completeMove();
    return true;
}

void MapCanvasEntities::scaleMarkers()
{
    const int width = this->point_markers->entityWidth();
    this->point_markers->scaleLabels(width);
    this->device_links->scaleLabels(width);
    
    if (this->placement->isCreating() &&
        isHydraulicCanvasLink(this->placement->entity()))
    {
        this->placement->scaleFloatingLabel(
            this->point_markers->pixmapPathForEntity(this->placement->entity()), width);
    }
    
    positionMarkers();
}

void MapCanvasEntities::positionMarkers()
{
    MapEntityMarkerLabel *label_to_skip = nullptr;
    if (this->placement->isMoving() && !this->placement->movingSelected())
        label_to_skip = this->placement->floatingLabel();
    
    this->point_markers->positionLabels(label_to_skip);
    this->device_links->positionLabels();
}

void MapCanvasEntities::paintMarkers(QPainter &paint)
{
    this->pipes->paint(
        paint, this->point_markers->markers(),
        this->placement->isCreating() && isHydraulicPipeGeometry(this->placement->entity()),
        this->placement->mousePosition(), this->placement->connectionTarget());
    this->device_links->paint(
        paint, this->point_markers->markers(), this->selection->selectedMarkers(),
        this->placement->isCreating() && isHydraulicDeviceLink(this->placement->entity()),
        this->placement->mousePosition(), this->placement->connectionTarget());
    
    const bool draw_moving_label_at_mouse = this->placement->isMoving() &&
                                            this->placement->floatingLabel() &&
                                            !this->placement->movingSelected();
    this->point_markers->paintConnectionPoints(
        paint, this->placement->connectionTarget(), this->placement->floatingLabel(),
        draw_moving_label_at_mouse, this->placement->mousePosition());
}

void MapCanvasEntities::onMarkerMoveRequested(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    const MapEntityMarker marker = markerByLabel(label);
    if (marker.entity.type == InfrastructureEntity::Unknown)
        return;
    
    stopEntityPositioning();
    const QPointF mouse_position = this->map_canvas->mapFromGlobal(QCursor::pos());
    this->placement->startMove(marker.entity.type, label, mouse_position);
    updateCanvas();
}

void MapCanvasEntities::onMarkerMoveSelectedRequested(MapEntityMarkerLabel *label)
{
    if (!label || !this->selection->isMarkerSelected(label) ||
        this->selection->selectedMarkerCount() < 2)
    {
        return;
    }
    
    onMarkerMoveRequested(label);
    if (!this->placement->isMoving() || this->placement->floatingLabel() != label)
        return;
    
    this->placement->setMovingSelected(true);
    this->selection->setMouseTransparency(true);
    positionMarkers();
    updateCanvas();
}

void MapCanvasEntities::onMarkerDeleteRequested(MapEntityMarkerLabel *label)
{
    deleteMarker(label);
    updateCanvas();
}

void MapCanvasEntities::onMarkerSelectedDeleteRequested()
{
    const QList<MapEntityMarkerLabel *> labels_to_delete = this->selection->selectedLabels();
    for (MapEntityMarkerLabel *label : labels_to_delete)
        deleteMarker(label);
    
    this->pipes->deleteSelected();
    this->selection->clear();
    updateCanvas();
    emit signalEntityMarkerSelected(false);
}

void MapCanvasEntities::deleteMarker(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    if (this->placement->connectionTarget() == label)
        this->placement->clearConnectionTarget();
    if (this->placement->floatingLabel() == label)
        this->placement->stop();
    if (this->pipes->startLabel() == label)
        this->pipes->clearPlacement();
    
    this->device_links->removeConnectedToLabel(label);
    this->pipes->removeConnectedToLabel(label);
    this->selection->removeMarker(label);
    
    if (!this->point_markers->removeMarker(label))
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
        updateCanvas();
        return;
    }
    
    this->selection->replaceWithMarker(marker);
    emit signalEntityMarkerSelected(true);
    
    if (this->hydraulic_data)
        this->hydraulic_data->setSelectedUuid(marker.entity.type, marker.entity.uuid);
    updateCanvas();
}

void MapCanvasEntities::onMarkerContextMenuRequested(MapEntityMarkerLabel *label,
                                                     const QPoint &global_position)
{
    if (!label)
        return;
    
    const bool multiple_entities_selected = this->selection->isMarkerSelected(label) &&
                                            this->selection->selectedMarkerCount() > 1;
    label->showContextMenu(global_position, multiple_entities_selected);
}

void MapCanvasEntities::onRectangleSelect(const CoordinateWGS84Rect &rect,
                                          RectangleSelectMode mode)
{
    this->selection->selectInRectangle(rect, this->point_markers->markers(),
                                       this->device_links->markers(),
                                       mode == RectangleSelectMode::Replace);
    emit signalEntityMarkerSelected(this->selection->hasSelection());
    updateCanvas();
}

void MapCanvasEntities::updateConnectionTarget(const QPointF &mouse_position)
{
    MapEntityMarkerLabel *nearest_label = nullptr;
    if (this->placement->isCreating() &&
        (isHydraulicDeviceLink(this->placement->entity()) ||
         isHydraulicPipeGeometry(this->placement->entity())))
    {
        MapEntityMarkerLabel *excluded_label = nullptr;
        if (isHydraulicDeviceLink(this->placement->entity()))
            excluded_label = this->device_links->startLabel();
        nearest_label = this->point_markers->nearestConnectionTarget(
            mouse_position, excluded_label);
    }
    
    if (this->placement->connectionTarget() == nearest_label)
        return;
    
    this->placement->setConnectionTarget(nearest_label);
    updateCanvas();
}

bool MapCanvasEntities::selectDeviceLinkAt(const QPointF &position)
{
    if (!this->placement->isIdle())
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
    return this->placement->isIdle() &&
           this->device_links->labelAt(position, this->point_markers->markers()) != nullptr;
}

bool MapCanvasEntities::showPipeContextMenuAt(const QPointF &position,
                                              const QPoint &global_position)
{
    return this->placement->isIdle() &&
           this->pipes->showContextMenuAt(position, global_position,
                                          this->point_markers->markers());
}

void MapCanvasEntities::selectPipe(const QUuid &pipe_uuid)
{
    const std::optional<InfrastructureEntityReference> selected_pipe =
        this->selection->replaceWithPipe(pipe_uuid);
    if (!selected_pipe.has_value())
        return;
    
    emit signalEntityMarkerSelected(true);
    updateCanvas();
}

void MapCanvasEntities::startPipeVertexMove(const QUuid &pipe_uuid, int vertex_index)
{
    stopEntityPositioning();
    if (!this->pipes->startPipeVertexMove(pipe_uuid, vertex_index))
        return;
    
    selectPipe(pipe_uuid);
    const QPointF mouse_position = this->map_canvas->mapFromGlobal(QCursor::pos());
    this->placement->startVirtualMove(InfrastructureEntity::Pipe, mouse_position);
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
        junction_reference, vertex_coordinate.value(),
        this->point_markers->pixmapPathForEntity(InfrastructureEntity::Junction),
        this->point_markers->entityWidth());
    
    if (!this->pipes->splitPipeAtVertex(pipe_uuid, vertex_index,
                                        junction_reference, junction_marker.label))
    {
        this->point_markers->removeMarker(junction_marker.label);
        return;
    }
    
    this->selection->replaceWithMarker(junction_marker);
    emit signalEntityMarkerSelected(true);
    positionMarkers();
    updateCanvas();
}

bool MapCanvasEntities::selectPipeAt(const QPointF &position)
{
    if (!this->placement->isIdle())
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
    return this->placement->isIdle() &&
           this->pipes->pipeAt(position, this->point_markers->markers()).has_value();
}

MapEntityMarker MapCanvasEntities::markerByLabel(MapEntityMarkerLabel *label)
{
    const std::optional<MapEntityMarker> point_marker = this->point_markers->markerByLabel(label);
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

void MapCanvasEntities::updateCanvas()
{
    if (this->map_canvas)
        this->map_canvas->update();
}
