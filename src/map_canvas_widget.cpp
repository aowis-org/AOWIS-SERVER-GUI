#include "map_canvas_widget.h"

MapCanvasWidget::MapCanvasWidget(MapModel *map_model, MapWidget *map, CanvasMode mode, QWidget *parent)
    : QWidget{parent},
    map_model(map_model),
    map(map),
    mode(mode),
    map_canvas_entities(new MapCanvasEntities(map_model, this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    
    //setFocusPolicy(Qt::StrongFocus);
    //setFocus(Qt::OtherFocusReason);
    
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
    
    this->map_canvas_entities->paintMarkersTank(paint);
}

void MapCanvasWidget::startEntityPositioning(MapEditTool tool)
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
        cancelRectangleSelection();
        event->accept();
        return;
    }
    
    const int key = event->key();
    const bool modifier_ctrl = (event->modifiers() & Qt::ControlModifier);
    
    if (modifier_ctrl)
    {
        // F-Keys (and also shift-modifier) do not work reliably in WASM,
        // so we fallback to Ctrl + 1 - 4 for that
        if (key == Qt::Key_1)
            emit signalMapProviderChange(MapProvider::ArcGISSat);
        else if (key == Qt::Key_2)
            emit signalMapProviderChange(MapProvider::OpenTopoMap);
        else if (key == Qt::Key_3)
            emit signalMapProviderChange(MapProvider::OpenStreetMap);
        else if (key == Qt::Key_4)
            emit signalMapProviderChange(MapProvider::OSMCyclo);
    }
    else if (this->key_space_pressed)
    {
        if (key == Qt::Key_1)
            emit signalEditToolChangeSub(MapEditToolSub::Tool_1);
        else if (key == Qt::Key_2)
            emit signalEditToolChangeSub(MapEditToolSub::Tool_2);
        else if (key == Qt::Key_3)
            emit signalEditToolChangeSub(MapEditToolSub::Tool_3);
        else if (key == Qt::Key_4)
            emit signalEditToolChangeSub(MapEditToolSub::Tool_4);
        else if (key == Qt::Key_5)
            emit signalEditToolChangeSub(MapEditToolSub::Tool_5);
    }
    else
    {
        if (key == Qt::Key_Space)
            this->key_space_pressed = true;
        
        else if (key == Qt::Key_Left)
            this->map->panLeft();
        else if (key == Qt::Key_Right)
            this->map->panRight();
        else if (key == Qt::Key_Up)
            this->map->panUp();
        else if (key == Qt::Key_Down)
            this->map->panDown();
        
        else if (key == Qt::Key_U)
            this->map->panLeft();
        else if (key == Qt::Key_A)
            this->map->panRight();
        else if (key == Qt::Key_V)
            this->map->panUp();
        else if (key == Qt::Key_I)
            this->map->panDown();
        
        else if (key == Qt::Key_L)
            this->map->zoomIn();
        else if (key == Qt::Key_X)
            this->map->zoomOut();
        
        else if (key == Qt::Key_F1)
            emit signalMapProviderChange(MapProvider::ArcGISSat);
        else if (key == Qt::Key_F2)
            emit signalMapProviderChange(MapProvider::OpenTopoMap);
        else if (key == Qt::Key_F3)
            emit signalMapProviderChange(MapProvider::OpenStreetMap);
        else if (key == Qt::Key_F4)
            emit signalMapProviderChange(MapProvider::OSMCyclo);
        
        else if (this->mode == CanvasMode::Edit)
        {
            /*
            if (key == Qt::Key_1)
                emit signalEditToolChange(MapEditTool::Select);
            else if (key == Qt::Key_2)
                emit signalEditToolChange(MapEditTool::Reservoir);
            else if (key == Qt::Key_3)
                emit signalEditToolChange(MapEditTool::Tank);
            else if (key == Qt::Key_4)
                emit signalEditToolChange(MapEditTool::Pump);
            else if (key == Qt::Key_5)
                emit signalEditToolChange(MapEditTool::Valve);
            else if (key == Qt::Key_6)
                emit signalEditToolChange(MapEditTool::Junction);
            else if (key == Qt::Key_7)
                emit signalEditToolChange(MapEditTool::Pipe);
            else if (key == Qt::Key_8)
                emit signalEditToolChange(MapEditTool::Customer_Point);
            else if (key == Qt::Key_9)
                emit signalEditToolChange(MapEditTool::Power);
            else if (key == Qt::Key_0)
                emit signalEditToolChange(MapEditTool::Note);
            */
        }
        
        else
            QWidget::keyPressEvent(event);
    }
    
    event->accept();
    return;
}
void MapCanvasWidget::keyReleaseEvent(QKeyEvent *event)
{
    const int key = event->key();
    
    if (key == Qt::Key_Space)
        this->key_space_pressed = false;
}
void MapCanvasWidget::mousePressEvent(QMouseEvent *event)
{
    if (this->rectangle_selection_active && event->button() == Qt::LeftButton)
    {
        this->rectangle_start_pos = event->position().toPoint();
        this->rectangle_current_pos = this->rectangle_start_pos;
        this->rectangle_dragging = true;
        
        update();
        event->accept();
        return;
    }
    
    if (event->button() == Qt::RightButton)
    {
        bool positioned = this->map_canvas_entities->anchorMarkerTank(event);
        if (positioned)
        {
            update();
            event->accept();
            return;
        }
    }
    
    QWidget::mousePressEvent(event);
}
void MapCanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (this->rectangle_selection_active && this->rectangle_dragging)
    {
        this->rectangle_current_pos = event->position().toPoint();
        
        update();
        event->accept();
    }
    
    this->map->onMouseMove(event);
    
    // float entity with mouse cursor during placement
    this->map_canvas_entities->floatEntity(event);
    
    QWidget::mouseMoveEvent(event);
}
void MapCanvasWidget::mouseReleaseEvent(QMouseEvent *event)
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
void MapCanvasWidget::wheelEvent(QWheelEvent *event)
{
    this->map->onMouseWheel(event);
    
    QWidget::wheelEvent(event);
}

void MapCanvasWidget::resizeEvent(QResizeEvent *event)
{
    this->map_canvas_entities->positionMarkersTank();
    
    QWidget::resizeEvent(event);
}

void MapCanvasWidget::startRectangleSelection()
{
    this->rectangle_selection_active = true;
    this->rectangle_dragging = false;
    
    this->rectangle_start_pos = QPoint();
    this->rectangle_current_pos = QPoint();
    
    setCursor(Qt::CrossCursor);
    setFocus();
    
    update();
}
void MapCanvasWidget::cancelRectangleSelection()
{
    if (!rectangle_selection_active)
        return;
    
    this->rectangle_selection_active = false;
    this->rectangle_dragging = false;
    
    unsetCursor();
    
    update();
    
    emit rectangleSelectionCanceled();
}
QRect MapCanvasWidget::currentSelectionRect() const
{
    return QRect(rectangle_start_pos, rectangle_current_pos).normalized();
}

