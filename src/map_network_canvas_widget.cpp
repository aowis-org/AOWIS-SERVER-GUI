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
    
    QWidget::keyPressEvent(event);
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

