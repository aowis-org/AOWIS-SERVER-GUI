#include "map_network_canvas_widget.h"

MapNetworkCanvasWidget::MapNetworkCanvasWidget(MapModel *map_model, MapWidget *map, CanvasMode mode, QWidget *parent)
    : QWidget{parent},
    map_model( map_model ),
    map( map ),
    mode( mode )
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    
    setFocusPolicy(Qt::StrongFocus);
    
    
}

int MapNetworkCanvasWidget::backgroundOpacity() const
{
    return this->map_background_opacity;
}
void MapNetworkCanvasWidget::setBackgroundOpacity(int opacity)
{
    opacity = qBound(0, opacity, 100);
    
    if (this->map_background_opacity == opacity)
    {
        return;
    }
    
    this->map_background_opacity = opacity;
    update();
}

void MapNetworkCanvasWidget::paintEvent(QPaintEvent *)
{
    QPainter paint(this);
    paint.setRenderHint(QPainter::Antialiasing);
    
    if (this->map_background_opacity > 0)
    {
        QColor background = palette().color(QPalette::Window);
        background.setAlphaF(this->map_background_opacity / 100.0);
        
        paint.fillRect(rect(), background);
    }
    
    paintEventRectangle(paint);
    
    
    
    Wgs84Coordinate wgs;
    wgs.lat = 11.98161;
    wgs.lon = 18.19329;
    const QPointF p = this->map_model->screenFromWgs84(wgs, size());
    
    paint.setBrush(Qt::red);
    paint.drawEllipse(p, 5.0, 5.0);
    
    
    
    Wgs84Coordinate wgs_a;
    wgs_a.lat = 11.98300;
    wgs_a.lon = 18.19435;
    Wgs84Coordinate wgs_b;
    wgs_b.lat = 11.97945;
    wgs_b.lon = 18.19433;
    const QPointF a = this->map_model->screenFromWgs84(wgs_a, size());
    const QPointF b = this->map_model->screenFromWgs84(wgs_b, size());
    
    paint.drawLine(a, b);
}
void MapNetworkCanvasWidget::paintEventRectangle(QPainter &paint)
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

void MapNetworkCanvasWidget::keyPressEvent(QKeyEvent *event)
{
    if (this->rectangle_selection_active && event->key() == Qt::Key_Escape)
    {
        cancelRectangleSelection();
        event->accept();
        return;
    }
    
    if (event->key() == Qt::Key_Left)
        this->map->addPanVelocity(1, 0);
    else if (event->key() == Qt::Key_Right)
        this->map->addPanVelocity(-1, 0);
    else if (event->key() == Qt::Key_Up)
        this->map->addPanVelocity(0, 1);
    else if (event->key() == Qt::Key_Down)
        this->map->addPanVelocity(0, -1);
    
    else if (event->key() == Qt::Key_U)
        this->map->addPanVelocity(1, 0);
    else if (event->key() == Qt::Key_A)
        this->map->addPanVelocity(-1, 0);
    else if (event->key() == Qt::Key_V)
        this->map->addPanVelocity(0, 1);
    else if (event->key() == Qt::Key_I)
        this->map->addPanVelocity(0, -1);
    
    else if (event->key() == Qt::Key_Shift)
        this->map->zoomIn();
    else if (event->key() == Qt::Key_Space)
        this->map->zoomOut();
    
    else
        QWidget::keyPressEvent(event);
    
    event->accept();
    return;
}
void MapNetworkCanvasWidget::mousePressEvent(QMouseEvent *event)
{
    if (this->rectangle_selection_active && event->button() == Qt::LeftButton)
    {
        this->rectangle_start_pos = event->position().toPoint();
        this->rectangle_current_pos = this->rectangle_start_pos;
        this->rectangle_dragging = true;
        
        update();
        event->accept();
    }
    
    QWidget::mousePressEvent(event);
}
void MapNetworkCanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (this->rectangle_selection_active && this->rectangle_dragging)
    {
        this->rectangle_current_pos = event->position().toPoint();
        
        update();
        event->accept();
    }
    
    QWidget::mouseMoveEvent(event);
}
void MapNetworkCanvasWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (this->rectangle_selection_active && this->rectangle_dragging)
    {
        this->rectangle_current_pos = event->position().toPoint();
        QRect selected_rect = currentSelectionRect();
        
        this->rectangle_selection_active = false;
        this->rectangle_dragging = false;
        
        unsetCursor();
        update();
        
        if (selected_rect.width() > 3 && selected_rect.height() > 3)
        {
            emit rectangleSelected(selected_rect);
        }
        else
        {
            emit rectangleSelectionCanceled();
        }
    }
    
    QWidget::mouseReleaseEvent(event);
}
void MapNetworkCanvasWidget::wheelEvent(QWheelEvent *event)
{
    
    QWidget::wheelEvent(event);
}

void MapNetworkCanvasWidget::startRectangleSelection()
{
    this->rectangle_selection_active = true;
    this->rectangle_dragging = false;
    
    this->rectangle_start_pos = QPoint();
    this->rectangle_current_pos = QPoint();
    
    setCursor(Qt::CrossCursor);
    setFocus();
    
    update();
}
void MapNetworkCanvasWidget::cancelRectangleSelection()
{
    if (!rectangle_selection_active)
        return;
    
    this->rectangle_selection_active = false;
    this->rectangle_dragging = false;
    
    unsetCursor();
    
    update();
    
    emit rectangleSelectionCanceled();
}
QRect MapNetworkCanvasWidget::currentSelectionRect() const
{
    return QRect(rectangle_start_pos, rectangle_current_pos).normalized();
}

