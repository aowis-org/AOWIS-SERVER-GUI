#include "map_canvas_widget.h"

MapCanvasWidget::MapCanvasWidget(MapModel *map_model, MapWidget *map, HydraulicData *hydraulic_data, CanvasMode mode, QWidget *parent)
    : QWidget{parent},
    map_model(map_model),
    map(map),
    hydraulic_data(hydraulic_data),
    mode(mode),
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
    
    /*
    //qDebug() << size();
    //example red dot
    CoordinateWGS84 wgs;
    wgs.lat = 11.98161;
    wgs.lon = 18.19329;
    const QPointF p = this->map_model->screenFromWgs84(wgs, size());
    //qDebug() << p;
    paint.setBrush(Qt::red);
    paint.drawEllipse(p, 5.0, 5.0);
    
    //example red line
    CoordinateWGS84 wgs_a;
    wgs_a.lat = 11.98300;
    wgs_a.lon = 18.19435;
    CoordinateWGS84 wgs_b;
    wgs_b.lat = 11.97945;
    wgs_b.lon = 18.19433;
    const QPointF a = this->map_model->screenFromWgs84(wgs_a, size());
    const QPointF b = this->map_model->screenFromWgs84(wgs_b, size());
    paint.drawLine(a, b);
    */
    
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
    if (onKeyPressEvent(event))
    {
        event->accept();
        return;
    }
    
    QWidget::keyPressEvent(event);
}
bool MapCanvasWidget::onKeyPressEvent(QKeyEvent *event)
{
    if (this->rectangle_selection_active && event->key() == Qt::Key_Escape)
    {
        cancelRectangleSelection();
        event->accept();
        return true;
    }
    
    const int key = event->key();
    const bool modifier_ctrl = (event->modifiers() & Qt::ControlModifier);
    
    if (modifier_ctrl)
    {
        
    }
    else if (this->key_space_pressed)
    {
        
    }
    else
    {
        if (key == Qt::Key_Space  && !event->isAutoRepeat())
            this->key_space_pressed = true;
        
        switch (key)
        {
        case Qt::Key_Left:
        case Qt::Key_U:
            this->map->panLeft();
            return true;
            
        case Qt::Key_Right:
        case Qt::Key_A:
            this->map->panRight();
            return true;
            
        case Qt::Key_Up:
        case Qt::Key_V:
            this->map->panUp();
            return true;
        
        case Qt::Key_Down:
        case Qt::Key_I:
            this->map->panDown();
            return true;    
        }
        
        switch (key)
        {
        case Qt::Key_L:
            this->map->zoomIn();
            return true;
        
        case Qt::Key_X:
            this->map->zoomOut();
            return true;
        }
        
        return false;
    }
    
    return false;
}
void MapCanvasWidget::keyReleaseEvent(QKeyEvent *event)
{
    const int key = event->key();
    
    if (key == Qt::Key_Space && !event->isAutoRepeat())
        this->key_space_pressed = false;
}
void MapCanvasWidget::focusOutEvent(QFocusEvent *event)
{
    this->key_space_pressed = false;
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
        bool positioned = this->map_canvas_entities->anchorMarker(event);
        if (positioned)
        {
            update();
            event->accept();
            return;
        }
    }
    
    if (this->rectangle_selection_active && event->button() == Qt::RightButton)
    {
        this->rectangle_start_wgs84 = this->map_model->wgs84FromScreen(
            event->position().toPoint(),
            size()
            );
        this->rectangle_current_pos = event->position().toPoint();
        this->rectangle_dragging = true;
        update();
        event->accept();
        return;
    }
    
    QWidget::mousePressEvent(event);
}
void MapCanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    this->map->onMouseMove(event);
    
    if (this->rectangle_selection_active && this->rectangle_dragging)
    {
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
    
    if (this->map_canvas_entities->isDeviceLinkAt(event->position()) ||
        this->map_canvas_entities->isPipeAt(event->position()))
    {
        this->setCursor(Qt::PointingHandCursor);
    }
    else
    {
        this->unsetCursor();
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
    
    QWidget::mouseReleaseEvent(event);
}
void MapCanvasWidget::wheelEvent(QWheelEvent *event)
{
    this->map->onMouseWheel(event);
    event->accept();
    return;
    
    QWidget::wheelEvent(event);
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
    
    setCursor(Qt::CrossCursor);
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

