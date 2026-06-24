#include "map_canvas_entities.h"
#include "map_canvas_widget.h"


MapCanvasEntities::MapCanvasEntities(MapModel *map_model, MapCanvasWidget *map_canvas)
    : QObject(map_canvas),
    map_model(map_model),
    map_canvas(map_canvas)
{
    connect(this->map_model, &MapModel::zoomChanged, this, &MapCanvasEntities::scaleMarkersTank);
    
    connect(this->map_model, &MapModel::centerChangedWGS84, this, [this](const CoordinateWGS84 &)
    {
        positionMarkersTank();
    });
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
            // only draw new floating entity after a certain "rearming distance"
            if ((event->position().toPoint() - this->entity_floating_hide_until).manhattanLength() <= 10)
                return;
            
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
        
        int width = calculateEntityWidth();
        QPixmap pixmap = QPixmap(tank_marker.path_pixmap).scaledToWidth(width, Qt::SmoothTransformation);
        tank_marker.label->setPixmap(pixmap);
        tank_marker.label->resize(pixmap.size());
        
        this->list_tank_markers.append(tank_marker);
        
        positionMarkersTank();
        
        this->entity_floating_hide_until = event->position().toPoint();
        
        this->entity_floating = nullptr;
        startEntityPositioning(this->tool_current);
        return true;
    }
    else
        return false;
}

int MapCanvasEntities::calculateEntityWidth()
{
    int zoom = this->map_model->zoom();
    
    int width = 10;
    if (zoom == 19)
        width = 40;
    else if (zoom == 18)
        width = 30;
    else if (zoom == 17)
        width = 20;
    
    return width;
}

void MapCanvasEntities::scaleMarkersTank()
{
    int width = calculateEntityWidth();
    
    for (int i = 0; i < this->list_tank_markers.length(); i++)
    {
        EntityTankMarker &tank_marker = this->list_tank_markers[i];
        QLabel *label = tank_marker.label;
        
        QPixmap pixmap = QPixmap(tank_marker.path_pixmap).scaledToWidth(width, Qt::SmoothTransformation);
        label->setPixmap(pixmap);
        label->resize(pixmap.size());
    }
    
    positionMarkersTank();
}

void MapCanvasEntities::positionMarkersTank()
{
    for (int i = 0; i < this->list_tank_markers.length(); i++)
    {
        EntityTankMarker &tank_marker = this->list_tank_markers[i];
        QLabel *label = tank_marker.label;
        
        CoordinateWGS84 wgs = tank_marker.entity_tank.coord_wgs84;
        QPointF point = this->map_model->screenFromWgs84(wgs, this->map_canvas->size());
        
        QPoint marker_pos(
            int(point.x()),
            int(point.y()) - label->height()
            );
        
        if (label->pos() != marker_pos)
            label->move(marker_pos);
        
        if (!label->isVisible())
            label->show();
    }
}
void MapCanvasEntities::paintMarkersTank(QPainter &paint)
{
    paint.save();
    paint.setBrush(Qt::black);
    paint.setPen(Qt::NoPen);
    
    for (int i = 0; i < this->list_tank_markers.length(); i++)
    {
        EntityTankMarker &tank_marker = this->list_tank_markers[i];
        
        CoordinateWGS84 wgs = tank_marker.entity_tank.coord_wgs84;
        QPointF point = this->map_model->screenFromWgs84(wgs, this->map_canvas->size());
        
        paint.drawEllipse(point, 5.0, 5.0);
    }
    
    paint.restore();
}

