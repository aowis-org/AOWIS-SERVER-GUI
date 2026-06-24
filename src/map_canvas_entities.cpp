#include "map_canvas_entities.h"
#include "map_canvas_widget.h"


MapCanvasEntities::MapCanvasEntities(MapModel *map_model, MapCanvasWidget *map_canvas)
    : QObject(map_canvas),
    map_model(map_model),
    map_canvas(map_canvas)
{
    
}

void MapCanvasEntities::startEntityPositioning(MapEditTool tool)
{
    this->tool_current = tool;
    
    if (tool == MapEditTool::Tank)
    {
        if (this->entity_floating)   
        {
            this->entity_floating->deleteLater();
            this->entity_floating = nullptr;
        }
        else
        {
            this->entity_floating = new QLabel(this->map_canvas);
            this->entity_floating->setPixmap(QPixmap(":/icon/tower.png"));
            this->entity_floating->adjustSize();
            
            this->entity_floating->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            this->entity_floating->setFocusPolicy(Qt::NoFocus);
            
            this->entity_floating->hide();
            this->entity_floating->raise();
        }
    }
}

void MapCanvasEntities::floatEntity(QMouseEvent *event)
{
    if (this->entity_floating)
    {
        this->map_canvas->setFocusPolicy(Qt::StrongFocus);
        this->map_canvas->setFocus(Qt::OtherFocusReason);
        
        if (!this->entity_floating->isVisible())
        {
            this->entity_floating->show();
        }
        this->entity_floating->move(event->position().toPoint());
        
        event->accept();
        return;
    }
}

bool MapCanvasEntities::positionMarkerTank(QMouseEvent *event)
{
    if (this->entity_floating)
    {
        CoordinateWGS84 wgs = this->map_model->wgs84FromScreen(event->position().toPoint(), this->map_canvas->size());
        EntityTank tank;
        tank.coord_wgs84 = wgs;
        EntityTankMarker tank_marker;
        tank_marker.entity_tank = tank;
        tank_marker.label = this->entity_floating;
        tank_marker.path_pixmap = ":/icon/tower.png";
        
        this->list_tank_markers.append(tank_marker);
        
        this->entity_floating = nullptr;
        
        startEntityPositioning(this->tool_current);
        
        return true;
    }
    else
        return false;
}

void MapCanvasEntities::paintMarkersTank(QPainter &paint)
{
    for (int i=0; i < this->list_tank_markers.length(); i++)
    {
        EntityTankMarker tank_marker = this->list_tank_markers.at(i);
        //EntityTank tank = tank_marker.entity_tank;
        CoordinateWGS84 wgs = tank_marker.entity_tank.coord_wgs84;
        QLabel *label = tank_marker.label;
        QPointF point = this->map_model->screenFromWgs84(wgs, this->map_canvas->size());
        
        label->move(point.x(), point.y());
        
        int zoom = this->map_model->zoom();
        if (zoom == 19)
            label->setPixmap(QPixmap(tank_marker.path_pixmap).scaledToWidth(40, Qt::SmoothTransformation));
        else if (zoom == 18)
            label->setPixmap(QPixmap(tank_marker.path_pixmap).scaledToWidth(30, Qt::SmoothTransformation));
        else if (zoom == 17)
            label->setPixmap(QPixmap(tank_marker.path_pixmap).scaledToWidth(20, Qt::SmoothTransformation));
        else
            label->setPixmap(QPixmap(tank_marker.path_pixmap).scaledToWidth(10, Qt::SmoothTransformation));
    }
}
