#include "map_canvas_widget.h"

#include <QLinearGradient>

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
    if (!this->rectangle_selection_active || !this->rectangle_dragging)
        return;

    const QRect selection = currentSelectionRect();
    if (selection.isEmpty())
        return;

    const QRectF outer_rect = QRectF(selection).adjusted(2.5, 2.5, -2.5, -2.5);
    if (outer_rect.width() <= 0.0 || outer_rect.height() <= 0.0)
        return;

    const qreal corner_radius = qMin<qreal>(2.0, qMin(outer_rect.width(), outer_rect.height()) / 8.0);

    paint.save();
    paint.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient fill_gradient(outer_rect.topLeft(), outer_rect.bottomLeft());
    fill_gradient.setColorAt(0.0, QColor(35, 151, 211, 24));
    fill_gradient.setColorAt(0.45, QColor(0, 145, 215, 32));
    fill_gradient.setColorAt(1.0, QColor(0, 65, 110, 38));
    paint.setPen(Qt::NoPen);
    paint.setBrush(fill_gradient);
    paint.drawRoundedRect(outer_rect, corner_radius, corner_radius);

    paint.setBrush(Qt::NoBrush);

    QPen wide_glow_pen(QColor(0, 149, 230, 52));
    wide_glow_pen.setWidthF(18.0);
    wide_glow_pen.setJoinStyle(Qt::RoundJoin);
    paint.setPen(wide_glow_pen);
    paint.drawRoundedRect(outer_rect, corner_radius, corner_radius);

    QPen glow_pen(QColor(23, 190, 255, 112));
    glow_pen.setWidthF(9.0);
    glow_pen.setJoinStyle(Qt::RoundJoin);
    paint.setPen(glow_pen);
    paint.drawRoundedRect(outer_rect, corner_radius, corner_radius);

    QPen shadow_pen(QColor(10, 15, 18, 205));
    shadow_pen.setWidthF(5.0);
    shadow_pen.setJoinStyle(Qt::MiterJoin);
    paint.setPen(shadow_pen);
    paint.drawRoundedRect(outer_rect, corner_radius, corner_radius);

    QLinearGradient steel_gradient(outer_rect.topLeft(), outer_rect.bottomLeft());
    steel_gradient.setColorAt(0.0, QColor(245, 250, 252, 245));
    steel_gradient.setColorAt(0.24, QColor(129, 147, 153, 240));
    steel_gradient.setColorAt(0.52, QColor(48, 61, 66, 245));
    steel_gradient.setColorAt(0.78, QColor(177, 190, 194, 240));
    steel_gradient.setColorAt(1.0, QColor(31, 42, 46, 245));

    QPen steel_pen(QBrush(steel_gradient), 3.0);
    steel_pen.setJoinStyle(Qt::MiterJoin);
    paint.setPen(steel_pen);
    paint.drawRoundedRect(outer_rect, corner_radius, corner_radius);

    const QRectF edge_glow_rect = outer_rect.adjusted(1.5, 1.5, -1.5, -1.5);
    if (edge_glow_rect.width() > 0.0 && edge_glow_rect.height() > 0.0)
    {
        QPen edge_glow_pen(QColor(86, 215, 255, 180));
        edge_glow_pen.setWidthF(1.0);
        edge_glow_pen.setJoinStyle(Qt::MiterJoin);
        paint.setPen(edge_glow_pen);
        paint.drawRoundedRect(edge_glow_rect, qMax<qreal>(0.0, corner_radius - 1.0), qMax<qreal>(0.0, corner_radius - 1.0));
    }

    const QRectF blue_frame_rect = outer_rect.adjusted(3.0, 3.0, -3.0, -3.0);
    if (blue_frame_rect.width() > 0.0 && blue_frame_rect.height() > 0.0)
    {
        QPen blue_frame_glow_pen(QColor(36, 196, 255, 96));
        blue_frame_glow_pen.setWidthF(5.0);
        blue_frame_glow_pen.setStyle(Qt::DashLine);
        blue_frame_glow_pen.setDashOffset(1.5);
        blue_frame_glow_pen.setJoinStyle(Qt::MiterJoin);
        paint.setPen(blue_frame_glow_pen);
        paint.drawRoundedRect(blue_frame_rect, qMax<qreal>(0.0, corner_radius - 2.0), qMax<qreal>(0.0, corner_radius - 2.0));

        QPen blue_frame_pen(QColor(102, 224, 255, 245));
        blue_frame_pen.setWidthF(1.5);
        blue_frame_pen.setStyle(Qt::DashLine);
        blue_frame_pen.setDashOffset(1.5);
        blue_frame_pen.setJoinStyle(Qt::MiterJoin);
        paint.setPen(blue_frame_pen);
        paint.drawRoundedRect(blue_frame_rect, qMax<qreal>(0.0, corner_radius - 2.0), qMax<qreal>(0.0, corner_radius - 2.0));
    }

    paint.restore();
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
        this->map_canvas_entities->onRectangleSelect(
            selected_rect, RectangleSelectMode::Replace);
        
        update();
        event->accept();
        return;
    }
    
    this->map_canvas_entities->floatEntity(event);
    
    if (this->map_canvas_entities->isDeviceLinkAt(event->position()) ||
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
            this->map_canvas_entities->onRectangleSelect(
                selected_rect, RectangleSelectMode::Replace);
            emit signalRectangleSelected(getSelectionRect(selected_rect));
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
