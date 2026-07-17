#include "map_canvas_entities.h"
#include "map_canvas_widget.h"


MapCanvasEntities::MapCanvasEntities(MapModel *map_model, NetworkData *network_data, MapCanvasWidget *map_canvas)
    : QObject(map_canvas),
    map_model(map_model),
    network_data(network_data),
    map_canvas(map_canvas)
{
    this->network_hydraulic = this->network_data->networkHydraulic();
    
    connect(this->map_model, &MapModel::zoomChanged, this, &MapCanvasEntities::scaleMarkersTank);
    
    connect(this->map_model, &MapModel::centerChangedWGS84, this, [this](const CoordinateWGS84 &)
    {
        positionMarkersTank();
    });
}

void MapCanvasEntities::startEntityPositioning(MapEditTool tool)
{
    stopEntityPositioning();
    
    this->tool_current = tool;
    this->entity_draw_immediately = true;
    this->entity_placement_mode = MapEntityPlacementMode::CreateNew;
    
    startEntityPositioningInternal();
}
void MapCanvasEntities::startEntityPositioningInternal()
{
    if (this->entity_placement_mode == MapEntityPlacementMode::CreateNew)
    {
        this->entity_floating = new MapEntityMarkerLabel(this->map_canvas);
        this->entity_floating->setPixmap(
            QPixmap(QStringLiteral(":/icon/tower.png"))
                .scaledToWidth(150, Qt::SmoothTransformation)
            );
        this->entity_floating->adjustSize();
    }
    else if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting)
    {
        if (!this->entity_floating)
        {
            this->entity_placement_mode = MapEntityPlacementMode::None;
            return;
        }
    }
    else
    {
        return;
    }
    
    this->entity_floating->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    this->entity_floating->setFocusPolicy(Qt::NoFocus);
    this->entity_floating->hide();
    this->entity_floating->raise();
    
    if (this->entity_draw_immediately)
    {
        QPoint marker_pos(
            qRound(this->mouse_pos_last.x()),
            qRound(this->mouse_pos_last.y()) - this->entity_floating->height()
            );
        
        this->entity_floating->move(marker_pos);
        this->entity_floating->show();
    }
    
    this->entity_draw_immediately = false;
    
    this->map_canvas->setFocusPolicy(Qt::StrongFocus);
    this->map_canvas->setFocus(Qt::OtherFocusReason);
}

void MapCanvasEntities::stopEntityPositioning()
{
    if (!this->entity_floating)
    {
        this->entity_placement_mode = MapEntityPlacementMode::None;
        return;
    }
    
    MapEntityPlacementMode previous_mode = this->entity_placement_mode;
    MapEntityMarkerLabel *label = this->entity_floating;
    
    this->entity_floating = nullptr;
    this->entity_placement_mode = MapEntityPlacementMode::None;
    
    if (previous_mode == MapEntityPlacementMode::CreateNew)
    {
        label->hide();
        label->deleteLater();
        return;
    }
    
    if (previous_mode == MapEntityPlacementMode::MoveExisting)
    {
        label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        
        positionMarkersTank();
        
        if (this->map_canvas)
            this->map_canvas->update();
    }
}

void MapCanvasEntities::floatEntity(QMouseEvent *event)
{
    this->mouse_pos_last = event->position();
    
    if (!this->entity_floating)
        return;
    
    if (!this->entity_floating->isVisible())
    {
        if (this->entity_placement_mode == MapEntityPlacementMode::CreateNew &&
            !this->entity_draw_immediately &&
            (event->position().toPoint() - this->entity_floating_hide_until).manhattanLength() <= 10)
        {
            return;
        }
        
        this->entity_floating->show();
    }
    
    QPoint marker_pos(
        qRound(event->position().x()),
        qRound(event->position().y()) - this->entity_floating->height()
        );
    
    this->entity_floating->move(marker_pos);
    
    if (this->map_canvas)
        this->map_canvas->update();
    
    event->accept();
}

bool MapCanvasEntities::anchorMarkerTank(QMouseEvent *event)
{
    if (!this->entity_floating)
        return false;
    
    CoordinateWGS84 wgs = this->map_model->wgs84FromScreen(
        event->position().toPoint(),
        this->map_canvas->size()
        );
    
    if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting)
    {
        MapEntityMarkerLabel *moved_label = this->entity_floating;
        
        for (int i = 0; i < this->list_tank_markers.length(); i++)
        {
            EntityTankMarker &tank_marker = this->list_tank_markers[i];
            
            if (tank_marker.label != moved_label)
                continue;
            
            tank_marker.entity_tank.coord_wgs84 = wgs;
            moved_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            
            this->entity_floating = nullptr;
            this->entity_placement_mode = MapEntityPlacementMode::None;
            
            positionMarkersTank();
            
            if (this->map_canvas)
                this->map_canvas->update();
            
            return true;
        }
        
        moved_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        
        this->entity_floating = nullptr;
        this->entity_placement_mode = MapEntityPlacementMode::None;
        
        positionMarkersTank();
        
        if (this->map_canvas)
            this->map_canvas->update();
        
        return false;
    }
    
    if (this->entity_placement_mode != MapEntityPlacementMode::CreateNew)
        return false;
    
    EntityTank tank;
    tank.coord_wgs84 = wgs;
    
    EntityTankMarker tank_marker;
    tank_marker.entity_tank = tank;
    tank_marker.label = this->entity_floating;
    tank_marker.path_pixmap = QStringLiteral(":/icon/tower.png");
    
    tank_marker.label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    
    connect(
        tank_marker.label,
        &MapEntityMarkerLabel::signalDeleteRequested,
        this,
        &MapCanvasEntities::onTankMarkerDeleteRequested
        );
    
    connect(
        tank_marker.label,
        &MapEntityMarkerLabel::signalMoveRequested,
        this,
        &MapCanvasEntities::onMarkerMoveRequested
        );
    
    connect(
        tank_marker.label,
        &MapEntityMarkerLabel::signalClicked,
        this,
        &MapCanvasEntities::onTankMarkerClicked
        );
    
    int width = calculateEntityWidth();
    QPixmap pixmap = QPixmap(tank_marker.path_pixmap)
                         .scaledToWidth(width, Qt::SmoothTransformation);
    
    tank_marker.label->setPixmap(pixmap);
    tank_marker.label->resize(pixmap.size());
    
    this->list_tank_markers.append(tank_marker);
    
    this->entity_floating_hide_until = event->position().toPoint();
    this->entity_floating = nullptr;
    
    positionMarkersTank();
    startEntityPositioningInternal();
    
    return true;
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
        MapEntityMarkerLabel *label = tank_marker.label;
        
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
        MapEntityMarkerLabel *label = tank_marker.label;
        
        if (!label)
            continue;
        
        if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting &&
            label == this->entity_floating)
        {
            continue;
        }
        
        CoordinateWGS84 wgs = tank_marker.entity_tank.coord_wgs84;
        QPointF point = this->map_model->screenFromWgs84(
            wgs,
            this->map_canvas->size()
            );
        
        QPoint marker_pos(
            qRound(point.x()),
            qRound(point.y()) - label->height()
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
        
        if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting &&
            tank_marker.label == this->entity_floating)
        {
            continue;
        }
        
        CoordinateWGS84 wgs = tank_marker.entity_tank.coord_wgs84;
        QPointF point = this->map_model->screenFromWgs84(
            wgs,
            this->map_canvas->size()
            );
        
        paint.drawEllipse(point, 5.0, 5.0);
    }
    
    if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting &&
        this->entity_floating)
    {
        paint.drawEllipse(this->mouse_pos_last, 5.0, 5.0);
    }
    
    paint.restore();
}

void MapCanvasEntities::onTankMarkerDeleteRequested(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    for (int i = 0; i < this->list_tank_markers.length(); i++)
    {
        EntityTankMarker &tank_marker = this->list_tank_markers[i];
        
        if (tank_marker.label == label)
        {
            MapEntityMarkerLabel *label_to_delete = tank_marker.label;
            
            this->list_tank_markers.removeAt(i);
            
            label_to_delete->hide();
            label_to_delete->deleteLater();
            
            this->map_canvas->update();
            return;
        }
    }
}
void MapCanvasEntities::onMarkerMoveRequested(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    bool marker_found = false;
    
    for (int i = 0; i < this->list_tank_markers.length(); i++)
    {
        if (this->list_tank_markers[i].label == label)
        {
            marker_found = true;
            break;
        }
    }
    
    if (!marker_found)
        return;
    
    stopEntityPositioning();
    
    this->entity_placement_mode = MapEntityPlacementMode::MoveExisting;
    this->entity_floating = label;
    this->entity_draw_immediately = true;
    this->mouse_pos_last = this->map_canvas->mapFromGlobal(QCursor::pos());
    
    startEntityPositioningInternal();
    
    if (this->map_canvas)
        this->map_canvas->update();
}

void MapCanvasEntities::onTankMarkerClicked(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    bool selection_changed = false;
    
    for (int i = 0; i < this->list_tank_markers.length(); i++)
    {
        EntityTankMarker &tank_marker = this->list_tank_markers[i];
        bool selected = tank_marker.label == label;
        
        if (tank_marker.selected == selected)
            continue;
        
        tank_marker.selected = selected;
        selection_changed = true;
    }
    
    if (selection_changed && this->map_canvas)
        this->map_canvas->update();
}
