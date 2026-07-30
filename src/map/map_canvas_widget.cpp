#include "map_canvas_widget.h"

MapCanvasWidget::MapCanvasWidget(MapModel *map_model, MapWidget *map, HydraulicData *hydraulic_data, QWidget *parent)
    : QWidget{parent},
    map_model(map_model),
    map(map),
    map_canvas_entities(new MapCanvasEntities(map_model, hydraulic_data, this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    
    //setFocusPolicy(Qt::StrongFocus);
    //setFocus(Qt::OtherFocusReason);
}

MapCanvasEntities *MapCanvasWidget::mapCanvasEntities()
{
    return this->map_canvas_entities;
}

int MapCanvasWidget::backgroundOpacity() const
{
    return this->map_background_opacity;
}
void MapCanvasWidget::setBackgroundOpacity(int opacity)
{
    opacity = qBound(0, opacity, 100);
    
    if (this->map_background_opacity == opacity)
    {
        return;
    }
    
    this->map_background_opacity = opacity;
    update();
}

void MapCanvasWidget::paintEvent(QPaintEvent *)
{
    QPainter paint(this);
#ifndef Q_OS_WASM
    paint.setRenderHint(QPainter::Antialiasing);
#endif
    
    if (this->map_background_opacity > 0)
    {
        QColor background = palette().color(QPalette::Window);
        background.setAlphaF(this->map_background_opacity / 100.0);
        
        paint.fillRect(rect(), background);
    }
    
    paintEventRectangle(paint);
    
    this->map_canvas_entities->paintMarkers(paint);
}

void MapCanvasWidget::startEntityPositioning(InfrastructureEntity tool)
{
    this->map_canvas_entities->startEntityPositioning(tool);
}
void MapCanvasWidget::stopEntityPositioning()
{
    this->map_canvas_entities->stopEntityPositioning();
}

void MapCanvasWidget::paintEventRectangle(QPainter &paint)
{
    if (this->rectangle_selection_active && this->rectangle_dragging)
    {
        QRect selection = currentSelectionRect();
        
        QColor fillColor(80, 140, 255, 40);
        QColor borderColor(80, 140, 255, 220);
        
        paint.fillRect(selection, fillColor);
        
        QPen pen(borderColor);
        pen.setWidth(1);
        pen.setStyle(Qt::DashLine);
        
        paint.setPen(pen);
        paint.drawRect(selection);
    }
}

void MapCanvasWidget::keyPressEvent(QKeyEvent *event)
{
    if (this->rectangle_selection_active && event->key() == Qt::Key_Escape)
    {
        this->cancelRectangleSelection();
        event->accept();
        return;
    }

    if (this->map->handleKeyPressEvent(event))
        return;

    QWidget::keyPressEvent(event);
}

void MapCanvasWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (this->map->handleKeyReleaseEvent(event))
        return;

    QWidget::keyReleaseEvent(event);
}

void MapCanvasWidget::focusOutEvent(QFocusEvent *event)
{
    this->map->clearKeyboardPanInput();
    QWidget::focusOutEvent(event);
}

void MapCanvasWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton &&
        (this->map_canvas_entities->selectDeviceLinkAt(event->position()) ||
         this->map_canvas_entities->selectPipeAt(event->position())))
    {
        update();
        event->accept();
        return;
    }
    
    if (event->button() == Qt::RightButton)
    {
        if (this->map_canvas_entities->anchorMarker(event))
        {
            update();
            event->accept();
            return;
        }
        
        // Pipe vertices and segments must be checked before starting
        // rectangle selection, including while the selection tool is active.
        if (this->map_canvas_entities->showPipeContextMenuAt(
                event->position(),
                event->globalPosition().toPoint()
                ))
        {
            update();
            event->accept();
            return;
        }
        
        if (this->rectangle_selection_active)
        {
            this->rectangle_start_wgs84 = this->map_model->wgs84FromScreen(
                event->position().toPoint(),
                size()
                );
            this->rectangle_current_pos = event->position().toPoint();
            this->rectangle_dragging = true;
            setCursor(Qt::CrossCursor);
            update();
            event->accept();
            return;
        }
    }
    
    if (this->map->handleMousePressEvent(event))
        return;

    QWidget::mousePressEvent(event);
}

void MapCanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (this->map->handleMouseMoveEvent(event))
        return;

    if (this->rectangle_selection_active && this->rectangle_dragging)
    {
        setCursor(Qt::CrossCursor);
        this->rectangle_current_pos = event->position().toPoint();
        
        const QRect selected_rect = currentSelectionRect();
        const CoordinateWGS84Rect rect = getSelectionRect(selected_rect);
        
        this->map_canvas_entities->onRectangleSelect(
            rect,
            RectangleSelectMode::Replace
            );
        
        update();
        event->accept();
        return;
    }
    
    this->map_canvas_entities->floatEntity(event);
    
    if (this->rectangle_selection_active)
    {
        unsetCursor();
    }
    else if (this->map_canvas_entities->isDeviceLinkAt(event->position()) ||
             this->map_canvas_entities->isPipeAt(event->position()))
    {
        setCursor(Qt::PointingHandCursor);
    }
    else
    {
        unsetCursor();
    }
    
    QWidget::mouseMoveEvent(event);
}
void MapCanvasWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (this->rectangle_selection_active && this->rectangle_dragging && event->button() == Qt::RightButton)
    {
        this->rectangle_current_pos = event->position().toPoint();
        const QRect selected_rect = currentSelectionRect();
        
        if (this->is_rectangle_selection_oneshot)
            this->rectangle_selection_active = false;
        
        this->rectangle_dragging = false;
        
        unsetCursor();
        update();
        
        int distance_min = 0; // 3
        if (selected_rect.width() > distance_min && selected_rect.height() > distance_min)
        {
            const CoordinateWGS84Rect rect = getSelectionRect(selected_rect);
            this->map_canvas_entities->onRectangleSelect(rect, RectangleSelectMode::Replace);
            emit signalRectangleSelected(rect);
        }
        else
        {
            emit signalRectangleSelectionCanceled();
        }
        
        event->accept();
        return;
    }
    
    if (this->map->handleMouseReleaseEvent(event))
        return;

    QWidget::mouseReleaseEvent(event);
}

void MapCanvasWidget::wheelEvent(QWheelEvent *event)
{
    this->map->handleWheelEvent(event);
}

void MapCanvasWidget::resizeEvent(QResizeEvent *event)
{
    this->map_canvas_entities->positionMarkers();
    
    QWidget::resizeEvent(event);
}

void MapCanvasWidget::startRectangleSelection(bool oneshot)
{
    this->is_rectangle_selection_oneshot = oneshot;
    
    this->rectangle_selection_active = true;
    this->rectangle_dragging = false;
    
    this->rectangle_current_pos = QPoint();
    
    unsetCursor();
    setFocus();
    
    update();
}
void MapCanvasWidget::cancelRectangleSelection()
{
    if (!rectangle_selection_active)
        return;
    
    if (this->is_rectangle_selection_oneshot)
        this->rectangle_selection_active = false;
    
    this->rectangle_dragging = false;
    
    unsetCursor();
    
    update();
    
    emit signalRectangleSelectionCanceled();
}
QRect MapCanvasWidget::currentSelectionRect() const
{
    const QPoint rectangle_start_pos = this->map_model->screenFromWgs84(
        this->rectangle_start_wgs84,
        size()
    ).toPoint();
    
    return QRect(rectangle_start_pos, this->rectangle_current_pos).normalized();
}
CoordinateWGS84Rect MapCanvasWidget::getSelectionRect(const QRect &selected_rect) const
{
    CoordinateWGS84Rect rect;
    
    rect.north_west = this->map_model->wgs84FromScreen(
        selected_rect.topLeft(),
        size()
        );
    rect.south_east = this->map_model->wgs84FromScreen(
        selected_rect.bottomRight(),
        size()
        );
    
    return rect;
}
