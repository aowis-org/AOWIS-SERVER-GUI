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
    
    if (this->entity_floating)
    {
        this->entity_floating->deleteLater();
        this->entity_floating = nullptr;
    }
    
    if (tool == MapEditTool::Tank)
    {
        this->entity_floating = new QLabel(this->map_canvas);
        this->entity_floating->setPixmap(QPixmap(":/icon/tower.png").scaledToWidth(150, Qt::SmoothTransformation));
        this->entity_floating->adjustSize();
        
        this->entity_floating->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        this->entity_floating->setFocusPolicy(Qt::NoFocus);
        
        this->entity_floating->hide();
        this->entity_floating->raise();
        
        this->map_canvas->setFocusPolicy(Qt::StrongFocus);
        this->map_canvas->setFocus(Qt::OtherFocusReason);
    }
}

void MapCanvasEntities::floatEntity(QMouseEvent *event)
{
    if (this->entity_floating)
    {
        if (!this->entity_floating->isVisible())
        {
            this->entity_floating->show();
        }
        this->entity_floating->move(event->position().toPoint());
        
        event->accept();
        return;
    }
}

bool MapCanvasEntities::anchorMarkerTank(QMouseEvent *event)
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

void MapCanvasEntities::updateMarkersTank(QPainter &paint)
{
    if (!this->map_canvas)
        return;
    
    for (int i = 0; i < this->list_tank_markers.length(); i++)
    {
        EntityTankMarker &tank_marker = this->list_tank_markers[i];
        
        QLabel *label = tank_marker.label;
        if (!label)
            continue;
        
        CoordinateWGS84 wgs = tank_marker.entity_tank.coord_wgs84;
        QPointF point = this->map_model->screenFromWgs84(wgs, this->map_canvas->size());
        
        int zoom = this->map_model->zoom();
        
        int width = 10;
        if (zoom == 19)
            width = 40;
        else if (zoom == 18)
            width = 30;
        else if (zoom == 17)
            width = 20;
        
        QPixmap pixmap = QPixmap(tank_marker.path_pixmap).scaledToWidth(width, Qt::SmoothTransformation);
        
        label->setPixmap(pixmap);
        label->resize(pixmap.size());
        
        QPoint marker_pos = point.toPoint();
        
        label->move(
            marker_pos.x(),
            marker_pos.y() - label->height()
            );
        
        label->show();
        
        paint.save();
        paint.setBrush(Qt::black);
        paint.setPen(Qt::NoPen);
        paint.drawEllipse(point, 5.0, 5.0);
        paint.restore();
    }
}
