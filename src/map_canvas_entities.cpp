#include "map_canvas_entities.h"
#include "map_canvas_widget.h"


MapCanvasEntities::MapCanvasEntities(MapModel *map_model, MapCanvasWidget *map_canvas)
    : QObject(map_canvas),
    map_model(map_model),
    map_canvas(map_canvas)
{
    
}

void MapCanvasEntities::positionMarkerTank(QMouseEvent *event, QLabel *entity)
{
    CoordinateWGS84 wgs = this->map_model->wgs84FromScreen(event->position().toPoint(), this->map_canvas->size());
    EntityTank tank;
    tank.coord_wgs84 = wgs;
    EntityTankMarker tank_marker;
    tank_marker.entity_tank = tank;
    tank_marker.label = entity;
    tank_marker.path_pixmap = ":/icon/tower.png";
    
    this->list_tank_markers.append(tank_marker);
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
