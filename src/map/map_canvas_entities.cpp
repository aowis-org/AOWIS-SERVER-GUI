#include "map_canvas_entities.h"
#include "map_canvas_widget.h"

namespace
{
    constexpr double marker_dot_radius = 5.0;
    constexpr double connection_target_radius = 9.0;
    constexpr double connection_hover_distance = 18.0;
    
    bool isHydraulicConnectionNode(InfrastructureEntity entity)
    {
        return entity == InfrastructureEntity::Junction ||
               entity == InfrastructureEntity::Reservoir ||
               entity == InfrastructureEntity::Tank;
    }
    
    bool isHydraulicDeviceLink(InfrastructureEntity entity)
    {
        return entity == InfrastructureEntity::Pump ||
               entity == InfrastructureEntity::Valve;
    }
    
    bool isHydraulicPipeGeometry(InfrastructureEntity entity)
    {
        return entity == InfrastructureEntity::Pipe;
    }
    
    constexpr double device_link_hit_distance = 7.0;
    
    double distanceSquaredToSegment(
        const QPointF &point,
        const QPointF &segment_start,
        const QPointF &segment_end
    )
    {
        const double segment_x = segment_end.x() - segment_start.x();
        const double segment_y = segment_end.y() - segment_start.y();
        const double segment_length_squared =
            segment_x * segment_x + segment_y * segment_y;
        
        if (segment_length_squared <= 0.0)
        {
            const double distance_x = point.x() - segment_start.x();
            const double distance_y = point.y() - segment_start.y();
            return distance_x * distance_x + distance_y * distance_y;
        }
        
        const double projection =
            ((point.x() - segment_start.x()) * segment_x +
             (point.y() - segment_start.y()) * segment_y) /
            segment_length_squared;
        
        const double bounded_projection = qBound(0.0, projection, 1.0);
        
        const QPointF nearest_point(
            segment_start.x() + bounded_projection * segment_x,
            segment_start.y() + bounded_projection * segment_y
            );
        
        const double distance_x = point.x() - nearest_point.x();
        const double distance_y = point.y() - nearest_point.y();
        
        return distance_x * distance_x + distance_y * distance_y;
    }
}

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

void MapCanvasEntities::startEntityPositioning(InfrastructureEntity entity)
{
    stopEntityPositioning();
    
    this->entity_current = entity;
    this->entity_draw_immediately = true;
    this->entity_placement_mode = MapEntityPlacementMode::CreateNew;
    
    if (isHydraulicDeviceLink(this->entity_current))
        setPointMarkerMouseTransparency(true);
    
    startEntityPositioningInternal();
}
void MapCanvasEntities::startEntityPositioningInternal()
{
    if (this->entity_placement_mode == MapEntityPlacementMode::CreateNew)
    {
        this->entity_floating = new MapEntityMarkerLabel(this->map_canvas);
        
        int width = 150;
        
        if (isHydraulicDeviceLink(this->entity_current))
            width = calculateEntityWidth();
        
        QPixmap pixmap = QPixmap(pixmapPathForEntity(this->entity_current))
                             .scaledToWidth(width, Qt::SmoothTransformation);
        
        this->entity_floating->setPixmap(pixmap);
        this->entity_floating->resize(pixmap.size());
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
    clearConnectionTarget();
    this->device_link_start_label = nullptr;
    setPointMarkerMouseTransparency(false);
    
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
    updateConnectionTarget(event->position());
    
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
    
    QPoint marker_pos;
    bool moving_device_link = false;
    
    if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting)
    {
        for (DeviceLinkCanvasItem &device_link : this->list_device_links)
        {
            if (device_link.device_label != this->entity_floating)
                continue;
            
            moving_device_link = true;
            
            device_link.geometry.center_coordinate = this->map_model->wgs84FromScreen(
                event->position().toPoint(),
                this->map_canvas->size()
                );
            
            marker_pos = QPoint(
                qRound(event->position().x() - this->entity_floating->width() / 2.0),
                qRound(event->position().y() - this->entity_floating->height() / 2.0)
                );
            
            break;
        }
    }
    
    if (!moving_device_link &&
        this->entity_placement_mode == MapEntityPlacementMode::CreateNew &&
        isHydraulicDeviceLink(this->entity_current) &&
        this->device_link_start_label)
    {
        MapEntityMarker start_marker = markerByLabel(this->device_link_start_label);
        
        QPointF start_point = this->map_model->screenFromWgs84(
            start_marker.coord_wgs84,
            this->map_canvas->size()
            );
        
        QPointF end_point = event->position();
        
        if (this->connection_target_label)
        {
            MapEntityMarker end_marker = markerByLabel(this->connection_target_label);
            
            if (isHydraulicConnectionNode(end_marker.entity.type))
            {
                end_point = this->map_model->screenFromWgs84(
                    end_marker.coord_wgs84,
                    this->map_canvas->size()
                    );
            }
        }
        
        QPointF center_point = (start_point + end_point) / 2.0;
        
        marker_pos = QPoint(
            qRound(center_point.x() - this->entity_floating->width() / 2.0),
            qRound(center_point.y() - this->entity_floating->height() / 2.0)
            );
    }
    else if (!moving_device_link)
    {
        marker_pos = QPoint(
            qRound(event->position().x()),
            qRound(event->position().y()) - this->entity_floating->height()
            );
    }
    
    this->entity_floating->move(marker_pos);
    
    if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting &&
        !moving_device_link)
    {
        for (MapEntityMarker &marker : this->list_entity_markers)
        {
            if (marker.label != this->entity_floating)
                continue;
            
            marker.coord_wgs84 = this->map_model->wgs84FromScreen(
                event->position().toPoint(),
                this->map_canvas->size()
                );
            
            break;
        }
    }
    
    if (this->map_canvas)
        this->map_canvas->update();
    
    event->accept();
}

bool MapCanvasEntities::anchorMarker(QMouseEvent *event)
{
    if (!this->entity_floating)
        return false;
    
    if (this->entity_placement_mode == MapEntityPlacementMode::CreateNew &&
        isHydraulicDeviceLink(this->entity_current))
    {
        return anchorDeviceLink(event);
    }
    
    CoordinateWGS84 wgs = this->map_model->wgs84FromScreen(
        event->position().toPoint(),
        this->map_canvas->size()
        );
    
    if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting)
    {
        MapEntityMarkerLabel *moved_label = this->entity_floating;
        
        for (MapEntityMarker &marker : this->list_entity_markers)
        {
            if (marker.label != moved_label)
                continue;
            
            marker.coord_wgs84 = wgs;
            moved_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            
            this->entity_floating = nullptr;
            this->entity_placement_mode = MapEntityPlacementMode::None;
            
            positionMarkers();
            positionDeviceLinks();
            
            if (this->map_canvas)
                this->map_canvas->update();
            
            return true;
        }
        
        for (DeviceLinkCanvasItem &device_link : this->list_device_links)
        {
            if (device_link.device_label != moved_label)
                continue;
            
            device_link.geometry.center_coordinate = wgs;
            moved_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            
            this->entity_floating = nullptr;
            this->entity_placement_mode = MapEntityPlacementMode::None;
            
            positionDeviceLinks();
            
            if (this->map_canvas)
                this->map_canvas->update();
            
            return true;
        }
        
        moved_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        
        this->entity_floating = nullptr;
        this->entity_placement_mode = MapEntityPlacementMode::None;
        
        positionMarkers();
        positionDeviceLinks();
        
        if (this->map_canvas)
            this->map_canvas->update();
        
        return false;
    }
    
    if (this->entity_placement_mode != MapEntityPlacementMode::CreateNew)
        return false;
    
    InfrastructureEntityReference reference;
    reference.type = this->entity_current;
    reference.uuid = QUuid::createUuid();
    
    MapEntityMarker marker;
    marker.coord_wgs84 = wgs;
    marker.entity = reference;
    marker.label = this->entity_floating;
    marker.path_pixmap = pixmapPathForEntity(this->entity_current);
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
    QPixmap pixmap = QPixmap(marker.path_pixmap).scaledToWidth(
        width,
        Qt::SmoothTransformation
        );
    
    marker.label->setPixmap(pixmap);
    marker.label->resize(pixmap.size());
    
    this->list_entity_markers.append(marker);
    
    this->entity_floating_hide_until = event->position().toPoint();
    this->entity_floating = nullptr;
    
    positionMarkers();
    startEntityPositioningInternal();
    
    return true;
}

bool MapCanvasEntities::anchorDeviceLink(QMouseEvent *event)
{
    if (!this->connection_target_label)
        return true;
    
    MapEntityMarker target_marker = markerByLabel(this->connection_target_label);
    if (!isHydraulicConnectionNode(target_marker.entity.type))
        return true;
    
    if (!this->device_link_start_label)
    {
        this->device_link_start_label = this->connection_target_label;
        this->connection_target_label = nullptr;
        updateConnectionTarget(event->position());
        
        if (this->map_canvas)
            this->map_canvas->update();
        
        return true;
    }
    
    if (this->connection_target_label == this->device_link_start_label)
        return true;
    
    MapEntityMarker start_marker = markerByLabel(this->device_link_start_label);
    if (!isHydraulicConnectionNode(start_marker.entity.type))
    {
        this->device_link_start_label = nullptr;
        return true;
    }
    
    QPointF start_point = this->map_model->screenFromWgs84(start_marker.coord_wgs84, this->map_canvas->size());
    QPointF end_point = this->map_model->screenFromWgs84(target_marker.coord_wgs84, this->map_canvas->size());
    QPointF center_point = (start_point + end_point) / 2.0;
    
    DeviceLinkCanvasItem device_link;
    device_link.entity.type = this->entity_current;
    device_link.entity.uuid = QUuid::createUuid();
    device_link.geometry.start_node = start_marker.entity;
    device_link.geometry.end_node = target_marker.entity;
    device_link.geometry.center_coordinate = this->map_model->wgs84FromScreen(center_point.toPoint(), this->map_canvas->size());
    device_link.start_label = this->device_link_start_label;
    device_link.end_label = this->connection_target_label;
    device_link.device_label = this->entity_floating;
    device_link.path_pixmap = pixmapPathForEntity(this->entity_current);
    
    int width = calculateEntityWidth();
    QPixmap pixmap = QPixmap(device_link.path_pixmap)
                         .scaledToWidth(width, Qt::SmoothTransformation);
    
    device_link.device_label->setPixmap(pixmap);
    device_link.device_label->resize(pixmap.size());
    device_link.device_label->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    
    connect(
        device_link.device_label,
        &MapEntityMarkerLabel::signalDeleteRequested,
        this,
        &MapCanvasEntities::onMarkerDeleteRequested
    );
    connect(
        device_link.device_label,
        &MapEntityMarkerLabel::signalMoveRequested,
        this,
        &MapCanvasEntities::onMarkerMoveRequested
    );
    connect(
        device_link.device_label,
        &MapEntityMarkerLabel::signalClicked,
        this,
        &MapCanvasEntities::onMarkerClicked
    );
    
    this->list_device_links.append(device_link);
    
    this->entity_floating_hide_until = event->position().toPoint();
    this->entity_floating = nullptr;
    this->device_link_start_label = nullptr;
    this->connection_target_label = nullptr;
    
    positionDeviceLinks();
    startEntityPositioningInternal();
    
    if (this->map_canvas)
        this->map_canvas->update();
    
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
    
    for (int i = 0; i < this->list_device_links.length(); i++)
    {
        DeviceLinkCanvasItem &device_link = this->list_device_links[i];
        if (!device_link.device_label)
            continue;
        
        QPixmap pixmap = QPixmap(device_link.path_pixmap).scaledToWidth(width, Qt::SmoothTransformation);
        device_link.device_label->setPixmap(pixmap);
        device_link.device_label->resize(pixmap.size());
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
    
    positionDeviceLinks();
}

void MapCanvasEntities::positionDeviceLinks()
{
    for (int i = 0; i < this->list_device_links.length(); i++)
    {
        DeviceLinkCanvasItem &device_link = this->list_device_links[i];
        
        if (!device_link.device_label)
            continue;
        
        QPointF center_point = this->map_model->screenFromWgs84(
            device_link.geometry.center_coordinate,
            this->map_canvas->size()
            );
        
        positionDeviceLabel(
            device_link.device_label,
            center_point
            );
    }
}

void MapCanvasEntities::positionDeviceLabel(MapEntityMarkerLabel *label, const QPointF &center)
{
    if (!label)
        return;
    
    QPoint label_position(
        qRound(center.x() - label->width() / 2.0),
        qRound(center.y() - label->height() / 2.0)
        );
    
    if (label->pos() != label_position)
        label->move(label_position);
    
    if (!label->isVisible())
        label->show();
}

void MapCanvasEntities::setPointMarkerMouseTransparency(bool transparent)
{
    for (int i = 0; i < this->list_entity_markers.length(); i++)
    {
        MapEntityMarkerLabel *label = this->list_entity_markers[i].label;
        if (label)
            label->setAttribute(Qt::WA_TransparentForMouseEvents, transparent);
    }
}
void MapCanvasEntities::paintMarkers(QPainter &paint)
{
    paintDeviceLinks(paint);
    
    paint.save();
    paint.setPen(Qt::NoPen);
    
    for (int i = 0; i < this->list_entity_markers.length(); i++) {
        MapEntityMarker &marker = this->list_entity_markers[i];
        
        if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting &&
            marker.label == this->entity_floating) {
            continue;
        }
        
        const QPointF point = this->map_model->screenFromWgs84(
            marker.coord_wgs84,
            this->map_canvas->size()
            );
        
        const bool is_connection_target =
            marker.label &&
            marker.label == this->connection_target_label;
        
        if (is_connection_target) {
            paint.setBrush(QColor(0, 140, 255));
            paint.drawEllipse(point, connection_target_radius, connection_target_radius);
        } else {
            paint.setBrush(Qt::black);
            paint.drawEllipse(point, marker_dot_radius, marker_dot_radius);
        }
    }
    
    if (this->entity_placement_mode == MapEntityPlacementMode::MoveExisting &&
        this->entity_floating) {
        paint.setBrush(Qt::black);
        paint.drawEllipse(this->mouse_pos_last, marker_dot_radius, marker_dot_radius);
    }
    
    paint.restore();
}

void MapCanvasEntities::paintDeviceLinks(QPainter &paint)
{
    paint.save();
    
    for (int i = 0; i < this->list_device_links.length(); i++)
    {
        const DeviceLinkCanvasItem &device_link =
            this->list_device_links[i];
        
        if (!device_link.start_label ||
            !device_link.end_label ||
            !device_link.device_label)
        {
            continue;
        }
        
        MapEntityMarker start_marker =
            markerByLabel(device_link.start_label);
        
        MapEntityMarker end_marker =
            markerByLabel(device_link.end_label);
        
        if (!isHydraulicConnectionNode(start_marker.entity.type) ||
            !isHydraulicConnectionNode(end_marker.entity.type))
        {
            continue;
        }
        
        const QPointF start_point = this->map_model->screenFromWgs84(
            start_marker.coord_wgs84,
            this->map_canvas->size()
            );
        
        const QPointF center_point = this->map_model->screenFromWgs84(
            device_link.geometry.center_coordinate,
            this->map_canvas->size()
            );
        
        const QPointF end_point = this->map_model->screenFromWgs84(
            end_marker.coord_wgs84,
            this->map_canvas->size()
            );
        
        QPen placed_pen;
        
        if (isMarkerSelected(device_link.device_label))
            placed_pen.setColor(QColor(0, 190, 255));
        else
            placed_pen.setColor(Qt::black);
        
        placed_pen.setWidthF(3.0);
        placed_pen.setCapStyle(Qt::RoundCap);
        paint.setPen(placed_pen);
        
        paint.drawLine(start_point, center_point);
        paint.drawLine(center_point, end_point);
    }
    
    if (this->entity_placement_mode == MapEntityPlacementMode::CreateNew &&
        isHydraulicDeviceLink(this->entity_current) &&
        this->device_link_start_label)
    {
        MapEntityMarker start_marker =
            markerByLabel(this->device_link_start_label);
        
        if (isHydraulicConnectionNode(start_marker.entity.type))
        {
            const QPointF start_point =
                this->map_model->screenFromWgs84(
                    start_marker.coord_wgs84,
                    this->map_canvas->size()
                    );
            
            QPointF end_point = this->mouse_pos_last;
            
            if (this->connection_target_label)
            {
                MapEntityMarker end_marker =
                    markerByLabel(this->connection_target_label);
                
                if (isHydraulicConnectionNode(end_marker.entity.type))
                {
                    end_point = this->map_model->screenFromWgs84(
                        end_marker.coord_wgs84,
                        this->map_canvas->size()
                        );
                }
            }
            
            const QPointF center_point =
                (start_point + end_point) / 2.0;
            
            QPen preview_pen(QColor(0, 140, 255));
            preview_pen.setWidthF(3.0);
            preview_pen.setCapStyle(Qt::RoundCap);
            paint.setPen(preview_pen);
            
            paint.drawLine(start_point, center_point);
            paint.drawLine(center_point, end_point);
        }
    }
    
    paint.restore();
}

void MapCanvasEntities::onMarkerMoveRequested(
    MapEntityMarkerLabel *label
    )
{
    if (!label)
        return;
    
    MapEntityMarker marker = markerByLabel(label);
    
    if (marker.entity.type == InfrastructureEntity::Unknown)
        return;
    
    stopEntityPositioning();
    
    this->entity_current = marker.entity.type;
    this->entity_placement_mode =
        MapEntityPlacementMode::MoveExisting;
    this->entity_floating = label;
    this->entity_draw_immediately = true;
    this->mouse_pos_last =
        this->map_canvas->mapFromGlobal(QCursor::pos());
    
    startEntityPositioningInternal();
    
    if (this->map_canvas)
        this->map_canvas->update();
}

void MapCanvasEntities::onMarkerDeleteRequested(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    deleteMarker(label);
    
    if (this->map_canvas)
        this->map_canvas->update();
}
void MapCanvasEntities::onMarkerSelectedDeleteRequested()
{
    QList<MapEntityMarkerLabel *> labels_to_delete;
    
    for (int i = 0; i < this->list_entity_markers_selected.length(); i++)
    {
        MapEntityMarkerLabel *label = this->list_entity_markers_selected[i].label;
        
        if (label)
            labels_to_delete.append(label);
    }
    
    for (int i = 0; i < labels_to_delete.length(); i++)
        deleteMarker(labels_to_delete[i]);
    
    if (this->map_canvas)
        this->map_canvas->update();
    
    emit signalEntityMarkerSelected(false);
}
void MapCanvasEntities::deleteMarker(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    if (this->connection_target_label == label) {
        this->connection_target_label = nullptr;
    }
    
    if (this->entity_floating == label)
    {
        this->entity_floating = nullptr;
        this->entity_placement_mode = MapEntityPlacementMode::None;
    }
    
    if (this->device_link_start_label == label)
        this->device_link_start_label = nullptr;
    
    for (int i = this->list_device_links.length() - 1; i >= 0; i--)
    {
        DeviceLinkCanvasItem &device_link = this->list_device_links[i];
        if (device_link.start_label != label && device_link.end_label != label && device_link.device_label != label)
            continue;
        
        if (device_link.device_label && device_link.device_label != label)
        {
            device_link.device_label->hide();
            device_link.device_label->deleteLater();
        }
        
        this->list_device_links.removeAt(i);
    }
    
    for (int i = this->list_entity_markers_selected.length() - 1; i >= 0; i--)
    {
        if (this->list_entity_markers_selected[i].label == label)
            this->list_entity_markers_selected.removeAt(i);
    }
    
    for (int i = 0; i < this->list_entity_markers.length(); i++)
    {
        if (this->list_entity_markers[i].label != label)
            continue;
        
        this->list_entity_markers.removeAt(i);
        break;
    }
    
    if (this->list_entity_markers_selected.isEmpty())
        this->selected_entity.reset();
    
    label->hide();
    label->deleteLater();
}

void MapCanvasEntities::onMarkerClicked(MapEntityMarkerLabel *label)
{
    MapEntityMarker marker = markerByLabel(label);
    
    if (marker.entity.type == InfrastructureEntity::Unknown)
        return;
    
    for (int i=0; i < this->list_entity_markers_selected.length(); i++)
    {
        MapEntityMarker mark = this->list_entity_markers_selected[i];
        mark.label->clearHighlight();
    }
    this->list_entity_markers_selected.clear();
    
    this->selected_entity = marker.entity;
    marker.label->setHighlightSelected();
    
    this->list_entity_markers_selected.append(marker);
    emit signalEntityMarkerSelected(true);
    
    // dummy
    QUuid uuid = this->hydraulic_data->networkHydraulic().tanks.at(0).uuid;
    qDebug() << uuid;
    InfrastructureEntity type = marker.entity.type;
    //QUuid uuid = marker.entity.uuid;
    this->hydraulic_data->setSelectedUuid(type, uuid);
    
    if (this->map_canvas)
        this->map_canvas->update();
}

void MapCanvasEntities::onRectangleSelect(const CoordinateWGS84Rect &rect, RectangleSelectMode mode)
{
    const double north = rect.north_west.lat;
    const double west = rect.north_west.lon;
    const double south = rect.south_east.lat;
    const double east = rect.south_east.lon;
    
    if (mode == RectangleSelectMode::Replace)
    {
        for (MapEntityMarker &marker : this->list_entity_markers_selected)
        {
            if (marker.label)
                marker.label->clearHighlight();
        }
        
        this->list_entity_markers_selected.clear();
    }
    
    for (MapEntityMarker &marker : this->list_entity_markers)
    {
        const CoordinateWGS84 &coord = marker.coord_wgs84;
        
        if (coord.lat < south || coord.lat > north ||
            coord.lon < west || coord.lon > east)
        {
            continue;
        }
        
        bool already_selected = false;
        
        for (const MapEntityMarker &selected_marker : this->list_entity_markers_selected)
        {
            if (selected_marker.label == marker.label)
            {
                already_selected = true;
                break;
            }
        }
        
        if (already_selected)
            continue;
        
        this->list_entity_markers_selected.append(marker);
        
        if (marker.label)
            marker.label->setHighlightSelected();
    }
    
    emit signalEntityMarkerSelected(!this->list_entity_markers_selected.isEmpty());
    
    if (this->map_canvas)
        this->map_canvas->update();
}

void MapCanvasEntities::updateConnectionTarget(const QPointF &mouse_pos)
{
    QPointer<MapEntityMarkerLabel> nearest_label = nullptr;
    double nearest_distance_squared = connection_hover_distance * connection_hover_distance;
    
    if (this->entity_floating &&
        this->entity_placement_mode == MapEntityPlacementMode::CreateNew &&
        isHydraulicDeviceLink(this->entity_current)) {
        for (const MapEntityMarker &marker : this->list_entity_markers) {
            if (!marker.label || !isHydraulicConnectionNode(marker.entity.type)) continue;
            if (this->device_link_start_label && marker.label == this->device_link_start_label) continue;
            
            const QPointF point = this->map_model->screenFromWgs84(
                marker.coord_wgs84,
                this->map_canvas->size()
                );
            
            const double distance_x = point.x() - mouse_pos.x();
            const double distance_y = point.y() - mouse_pos.y();
            const double distance_squared =
                distance_x * distance_x +
                distance_y * distance_y;
            
            if (distance_squared > nearest_distance_squared) continue;
            
            nearest_distance_squared = distance_squared;
            nearest_label = marker.label;
        }
    }
    
    if (this->connection_target_label == nearest_label) return;
    
    this->connection_target_label = nearest_label;
    
    if (this->map_canvas) this->map_canvas->update();
}

void MapCanvasEntities::clearConnectionTarget()
{
    if (this->connection_target_label.isNull()) return;
    
    this->connection_target_label = nullptr;
    
    if (this->map_canvas) this->map_canvas->update();
}

bool MapCanvasEntities::isMarkerSelected(MapEntityMarkerLabel *label) const
{
    if (!label)
        return false;
    
    for (const MapEntityMarker &marker : this->list_entity_markers_selected)
    {
        if (marker.label == label)
            return true;
    }
    
    return false;
}

bool MapCanvasEntities::selectDeviceLinkAt(const QPointF &position)
{
    MapEntityMarkerLabel *device_label =
        deviceLinkLabelAt(position);
    
    if (!device_label)
        return false;
    
    onMarkerClicked(device_label);
    return true;
}

MapEntityMarkerLabel *MapCanvasEntities::deviceLinkLabelAt(const QPointF &position)
{
    if (this->entity_placement_mode != MapEntityPlacementMode::None)
        return nullptr;
    
    const double maximum_distance_squared =
        device_link_hit_distance * device_link_hit_distance;
    
    double nearest_distance_squared = maximum_distance_squared;
    MapEntityMarkerLabel *nearest_device_label = nullptr;
    
    for (DeviceLinkCanvasItem &device_link : this->list_device_links)
    {
        if (!device_link.start_label ||
            !device_link.end_label ||
            !device_link.device_label)
        {
            continue;
        }
        
        MapEntityMarker start_marker =
            markerByLabel(device_link.start_label);
        
        MapEntityMarker end_marker =
            markerByLabel(device_link.end_label);
        
        if (!isHydraulicConnectionNode(start_marker.entity.type) ||
            !isHydraulicConnectionNode(end_marker.entity.type))
        {
            continue;
        }
        
        const QPointF start_point =
            this->map_model->screenFromWgs84(
                start_marker.coord_wgs84,
                this->map_canvas->size()
                );
        
        const QPointF center_point =
            this->map_model->screenFromWgs84(
                device_link.geometry.center_coordinate,
                this->map_canvas->size()
                );
        
        const QPointF end_point =
            this->map_model->screenFromWgs84(
                end_marker.coord_wgs84,
                this->map_canvas->size()
                );
        
        const double first_distance_squared =
            distanceSquaredToSegment(
                position,
                start_point,
                center_point
                );
        
        const double second_distance_squared =
            distanceSquaredToSegment(
                position,
                center_point,
                end_point
                );
        
        const double distance_squared =
            qMin(first_distance_squared, second_distance_squared);
        
        if (distance_squared > nearest_distance_squared)
            continue;
        
        nearest_distance_squared = distance_squared;
        nearest_device_label = device_link.device_label;
    }
    
    return nearest_device_label;
}

bool MapCanvasEntities::isDeviceLinkAt(const QPointF &position)
{
    return deviceLinkLabelAt(position) != nullptr;
}

MapEntityMarker MapCanvasEntities::markerByLabel(
    MapEntityMarkerLabel *label
    )
{
    for (int i = 0; i < this->list_entity_markers.length(); i++)
    {
        MapEntityMarker &marker =
            this->list_entity_markers[i];
        
        if (marker.label == label)
            return marker;
    }
    
    for (int i = 0; i < this->list_device_links.length(); i++)
    {
        DeviceLinkCanvasItem &device_link = this->list_device_links[i];
        
        if (device_link.device_label != label)
            continue;
        
        MapEntityMarker marker;
        marker.entity = device_link.entity;
        marker.coord_wgs84 = device_link.geometry.center_coordinate;
        marker.label = device_link.device_label;
        marker.path_pixmap = device_link.path_pixmap;
        
        return marker;
    }
    
    InfrastructureEntityReference reference;
    reference.type = InfrastructureEntity::Unknown;
    
    MapEntityMarker marker;
    marker.entity = reference;
    
    return marker;
}

QString MapCanvasEntities::pixmapPathForEntity(InfrastructureEntity entity) const
{
    switch (entity)
    {
    case InfrastructureEntity::Junction:
        return QStringLiteral(":/icon/junction.png");
        
    case InfrastructureEntity::Reservoir:
        return QStringLiteral(":/icon/reservoir.png");
        
    case InfrastructureEntity::Tank:
        return QStringLiteral(":/icon/tower.png");
        
    case InfrastructureEntity::Pipe:
        return QStringLiteral(":/icon/pipe.png");
        
    case InfrastructureEntity::Pump:
        return QStringLiteral(":/icon/pump.png");
        
    case InfrastructureEntity::Valve:
        return QStringLiteral(":/icon/valve.png");
        
    case InfrastructureEntity::CustomerPoint:
        return QStringLiteral(":/icon/customer.png");
        
    case InfrastructureEntity::ElectricJunction:
    case InfrastructureEntity::Cable:
    case InfrastructureEntity::Switch:
    case InfrastructureEntity::Fuse:
    case InfrastructureEntity::CircuitBreaker:
        return QStringLiteral(":/icon/electricity.png");
        
    case InfrastructureEntity::Battery:
    case InfrastructureEntity::Generator:
    case InfrastructureEntity::SolarPanel:
    case InfrastructureEntity::Inverter:
    case InfrastructureEntity::Transformer:
        return QStringLiteral(":/icon/energy.png");
        
    case InfrastructureEntity::Note:
    case InfrastructureEntity::Unknown:
        return QStringLiteral(":/icon/geomarker.png");
    }
    
    return QStringLiteral(":/icon/geomarker.png");
}
