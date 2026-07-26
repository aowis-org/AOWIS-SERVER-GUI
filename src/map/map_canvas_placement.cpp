#include "map_canvas_placement.h"
#include "map_canvas_widget.h"
#include "map_entity_marker_label.h"

#include <QApplication>
#include <QPixmap>

MapCanvasPlacement::MapCanvasPlacement(MapCanvasWidget *map_canvas, QObject *parent)
    : QObject(parent), map_canvas(map_canvas)
{}

void MapCanvasPlacement::startCreate(InfrastructureEntity entity, const QString &pixmap_path, int width)
{
    stop();
    this->current_entity = entity;
    this->placement_mode = MapEntityPlacementMode::CreateNew;
    this->draw_immediately = true;
    createFloatingLabel(pixmap_path, width);
}

bool MapCanvasPlacement::rearmCreate(const QString &pixmap_path, int width)
{
    if (this->placement_mode != MapEntityPlacementMode::CreateNew || this->floating_label)
        return false;
    
    createFloatingLabel(pixmap_path, width);
    return true;
}

bool MapCanvasPlacement::startMove(InfrastructureEntity entity, MapEntityMarkerLabel *label,
                                   const QPointF &mouse_position)
{
    if (!label)
        return false;
    
    stop();
    this->current_entity = entity;
    this->placement_mode = MapEntityPlacementMode::MoveExisting;
    this->floating_label = label;
    this->mouse_position = mouse_position;
    this->previous_mouse_position = mouse_position;
    this->draw_immediately = true;
    prepareFloatingLabel();
    return true;
}

void MapCanvasPlacement::startVirtualMove(InfrastructureEntity entity, const QPointF &mouse_position)
{
    stop();
    this->current_entity = entity;
    this->placement_mode = MapEntityPlacementMode::MoveExisting;
    this->mouse_position = mouse_position;
    this->previous_mouse_position = mouse_position;
    focusCanvas();
    setMoveCursor(true);
}

void MapCanvasPlacement::stop()
{
    setMoveCursor(false);
    clearConnectionTarget();
    this->moving_selected = false;
    
    if (this->floating_label)
    {
        MapEntityMarkerLabel *label = this->floating_label.data();
        if (this->placement_mode == MapEntityPlacementMode::CreateNew)
        {
            label->hide();
            label->deleteLater();
        }
        else if (this->placement_mode == MapEntityPlacementMode::MoveExisting)
        {
            label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        }
    }
    
    this->floating_label = nullptr;
    this->placement_mode = MapEntityPlacementMode::None;
    this->current_entity = InfrastructureEntity::Unknown;
}

MapEntityPlacementMode MapCanvasPlacement::mode() const
{
    return this->placement_mode;
}

InfrastructureEntity MapCanvasPlacement::entity() const
{
    return this->current_entity;
}

bool MapCanvasPlacement::isIdle() const
{
    return this->placement_mode == MapEntityPlacementMode::None;
}

bool MapCanvasPlacement::isCreating() const
{
    return this->placement_mode == MapEntityPlacementMode::CreateNew;
}

bool MapCanvasPlacement::isMoving() const
{
    return this->placement_mode == MapEntityPlacementMode::MoveExisting;
}

MapEntityMarkerLabel *MapCanvasPlacement::floatingLabel() const
{
    return this->floating_label.data();
}

MapEntityMarkerLabel *MapCanvasPlacement::takeCreatedLabel()
{
    if (!isCreating())
        return nullptr;
    
    MapEntityMarkerLabel *label = this->floating_label.data();
    this->floating_label = nullptr;
    return label;
}

void MapCanvasPlacement::completeMove()
{
    if (this->floating_label)
        this->floating_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    
    setMoveCursor(false);
    clearConnectionTarget();
    this->floating_label = nullptr;
    this->placement_mode = MapEntityPlacementMode::None;
    this->current_entity = InfrastructureEntity::Unknown;
    this->moving_selected = false;
}

void MapCanvasPlacement::updateMousePosition(const QPointF &position)
{
    this->previous_mouse_position = this->mouse_position;
    this->mouse_position = position;
}

QPointF MapCanvasPlacement::mousePosition() const
{
    return this->mouse_position;
}

QPointF MapCanvasPlacement::previousMousePosition() const
{
    return this->previous_mouse_position;
}

bool MapCanvasPlacement::revealFloatingLabelIfReady()
{
    if (!this->floating_label)
        return false;
    if (this->floating_label->isVisible())
        return true;
    
    if (isCreating() && !this->draw_immediately &&
        (this->mouse_position.toPoint() - this->floating_hide_until).manhattanLength() <= 10)
    {
        return false;
    }
    
    this->floating_label->show();
    return true;
}

void MapCanvasPlacement::moveFloatingLabelTopLeft(const QPointF &position)
{
    if (!this->floating_label)
        return;
    
    this->floating_label->move(qRound(position.x()),
                               qRound(position.y()) - this->floating_label->height());
}

void MapCanvasPlacement::setFloatingHiddenUntil(const QPoint &position)
{
    this->floating_hide_until = position;
}

void MapCanvasPlacement::scaleFloatingLabel(const QString &pixmap_path, int width)
{
    if (!this->floating_label)
        return;
    
    const QPixmap pixmap = QPixmap(pixmap_path).scaledToWidth(width, Qt::SmoothTransformation);
    this->floating_label->setPixmap(pixmap);
    this->floating_label->resize(pixmap.size());
}

void MapCanvasPlacement::setConnectionTarget(MapEntityMarkerLabel *label)
{
    this->connection_target_label = label;
}

MapEntityMarkerLabel *MapCanvasPlacement::connectionTarget() const
{
    return this->connection_target_label.data();
}

void MapCanvasPlacement::clearConnectionTarget()
{
    this->connection_target_label = nullptr;
}

void MapCanvasPlacement::setMovingSelected(bool moving_selected)
{
    this->moving_selected = moving_selected;
}

bool MapCanvasPlacement::movingSelected() const
{
    return this->moving_selected;
}

void MapCanvasPlacement::setMoveCursor(bool enabled)
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

void MapCanvasPlacement::createFloatingLabel(const QString &pixmap_path, int width)
{
    this->floating_label = new MapEntityMarkerLabel(this->map_canvas);
    scaleFloatingLabel(pixmap_path, width);
    prepareFloatingLabel();
}

void MapCanvasPlacement::prepareFloatingLabel()
{
    if (!this->floating_label)
        return;
    
    this->floating_label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    this->floating_label->setFocusPolicy(Qt::NoFocus);
    this->floating_label->hide();
    this->floating_label->raise();
    
    if (this->draw_immediately)
    {
        moveFloatingLabelTopLeft(this->mouse_position);
        this->floating_label->show();
    }
    
    this->draw_immediately = false;
    focusCanvas();
    if (isMoving())
        setMoveCursor(true);
}

void MapCanvasPlacement::focusCanvas()
{
    if (!this->map_canvas)
        return;
    
    this->map_canvas->setFocusPolicy(Qt::StrongFocus);
    this->map_canvas->setFocus(Qt::OtherFocusReason);
}
