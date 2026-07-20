#include "map_canvas_entities.h"
#include "map_canvas_widget.h"


MapCanvasEntities::MapCanvasEntities(MapModel *map_model, HydraulicData *hydraulic_data, MapCanvasWidget *map_canvas)
    : QObject(map_canvas),
    map_model(map_model),
    hydraulic_data(hydraulic_data),
    map_canvas(map_canvas)
{
    //this->network_hydraulic = this->network_data->networkHydraulic();
    
    connect(this->map_model, &MapModel::zoomChanged, this, &MapCanvasEntities::scaleMarkers);
    
    connect(this->map_model, &MapModel::centerChangedWGS84, this, [this](const CoordinateWGS84 &)
    {
        positionMarkers();
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
        
        positionMarkers();
        
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

bool MapCanvasEntities::anchorMarker(QMouseEvent *event)
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
        
        for (int i = 0; i < this->list_entity_markers.length(); i++)
        {
            MapEntityMarker &marker = this->list_entity_markers[i];
            
            if (marker.label != moved_label)
                continue;
            
            marker.coord_wgs84 = wgs;
            moved_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            
            this->entity_floating = nullptr;
            this->entity_placement_mode = MapEntityPlacementMode::None;
            
            positionMarkers();
            
            if (this->map_canvas)
                this->map_canvas->update();
            
            return true;
        }
        
        moved_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        
        this->entity_floating = nullptr;
        this->entity_placement_mode = MapEntityPlacementMode::None;
        
        positionMarkers();
        
        if (this->map_canvas)
            this->map_canvas->update();
        
        return false;
    }
    
    if (this->entity_placement_mode != MapEntityPlacementMode::CreateNew)
        return false;
    
    //EntityTank tank;
    //tank.coord_wgs84 = wgs;
    MapEntityMarker marker;
    marker.coord_wgs84 = wgs;
    
    //EntityTankMarker tank_marker;
    //tank_marker.entity_tank = tank;
    //tank_marker.label = this->entity_floating;
    //tank_marker.path_pixmap = QStringLiteral(":/icon/tower.png");
    
    //tank_marker.label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    
    marker.label = this->entity_floating;
    marker.path_pixmap = QStringLiteral(":/icon/tower.png");
    marker.label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    
    connect(
        marker.label,
        &MapEntityMarkerLabel::signalDeleteRequested,
        this,
        &MapCanvasEntities::onMarkerDeleteRequested
    );
    
    connect(
        marker.label,
        &MapEntityMarkerLabel::signalMoveRequested,
        this,
        &MapCanvasEntities::onMarkerMoveRequested
    );
    
    connect(
        marker.label,
        &MapEntityMarkerLabel::signalClicked,
        this,
        &MapCanvasEntities::onMarkerClicked
    );
    
    int width = calculateEntityWidth();
    QPixmap pixmap = QPixmap(marker.path_pixmap).scaledToWidth(width, Qt::SmoothTransformation);
    
    marker.label->setPixmap(pixmap);
    marker.label->resize(pixmap.size());
    
    this->list_entity_markers.append(marker);
    
    this->entity_floating_hide_until = event->position().toPoint();
    this->entity_floating = nullptr;
    
    positionMarkers();
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

void MapCanvasEntities::scaleMarkers()
{
    int width = calculateEntityWidth();
    
    for (int i = 0; i < this->list_entity_markers.length(); i++)
    {
        MapEntityMarker &marker = this->list_entity_markers[i];
        MapEntityMarkerLabel *label = marker.label;
        
        QPixmap pixmap = QPixmap(marker.path_pixmap).scaledToWidth(width, Qt::SmoothTransformation);
        label->setPixmap(pixmap);
        label->resize(pixmap.size());
    }
    
    positionMarkers();
}

void MapCanvasEntities::positionMarkers()
{
    for (int i = 0; i < this->list_entity_markers.length(); i++)
    {
        MapEntityMarker &marker = this->list_entity_markers[i];
        MapEntityMarkerLabel *label = marker.label;
        
        if (!label)
            continue;
        
        if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting &&
            label == this->entity_floating)
        {
            continue;
        }
        
        CoordinateWGS84 wgs = marker.coord_wgs84;
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
void MapCanvasEntities::paintMarkers(QPainter &paint)
{
    paint.save();
    paint.setBrush(Qt::black);
    paint.setPen(Qt::NoPen);
    
    for (int i = 0; i < this->list_entity_markers.length(); i++)
    {
        MapEntityMarker &marker = this->list_entity_markers[i];
        
        if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting &&
            marker.label == this->entity_floating)
        {
            continue;
        }
        
        CoordinateWGS84 wgs = marker.coord_wgs84;
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

void MapCanvasEntities::onMarkerDeleteRequested(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    for (int i = 0; i < this->list_entity_markers.length(); i++)
    {
        MapEntityMarker &marker = this->list_entity_markers[i];
        
        if (marker.label == label)
        {
            MapEntityMarkerLabel *label_to_delete = marker.label;
            
            this->list_entity_markers.removeAt(i);
            
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
    
    for (int i = 0; i < this->list_entity_markers.length(); i++)
    {
        if (this->list_entity_markers[i].label == label)
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

void MapCanvasEntities::onMarkerClicked(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    bool selection_changed = false;
    
    for (int i = 0; i < this->list_entity_markers.length(); i++)
    {
        MapEntityMarker &marker = this->list_entity_markers[i];
        bool selected = marker.label == label;
        
        if (marker.selected == selected)
            continue;
        
        marker.selected = selected;
        selection_changed = true;
    }
    
    if (selection_changed && this->map_canvas)
        this->map_canvas->update();
}

QString MapCanvasEntities::pixmapPathForSymbol(const QString &symbol_id) const
{
    if (symbol_id == QStringLiteral("tank.water_tower"))
        return QStringLiteral(":/icon/tower.png");
    
    if (symbol_id == QStringLiteral("tank.ground"))
        return QStringLiteral(":/icon/tank_ground.png");
    
    return QStringLiteral(":/icon/entity_unknown.png");
}

