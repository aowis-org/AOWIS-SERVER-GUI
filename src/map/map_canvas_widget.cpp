#include "map_canvas_widget.h"

#include <QLinearGradient>

#include <cmath>

MapCanvasWidget::MapCanvasWidget(MapModel *map_model, MapWidget *map, HydraulicData *hydraulic_data, QWidget *parent)
    : QWidget{parent},
    map_model(map_model),
    map(map),
    map_canvas_entities(new MapCanvasEntities(map_model, hydraulic_data, this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);

    connect(this->map_model, &MapModel::zoomChanged, this, [this]
    {
        update();
    });
    connect(this->map_model, &MapModel::centerChangedWGS84, this, [this]
    {
        update();
    });
    
    //setFocusPolicy(Qt::StrongFocus);
    //setFocus(Qt::OtherFocusReason);
}

MapCanvasEntities *MapCanvasWidget::mapCanvasEntities()
{
    return this->map_canvas_entities;
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
    
    paintEventTileSelectionOverlay(paint);
    paintEventRectangle(paint);
    
    this->map_canvas_entities->paintMarkers(paint);
}

void MapCanvasWidget::startEntityPositioning(InfrastructureEntity tool)
{
    this->map_canvas_entities->startEntityPositioning(tool);
}
void MapCanvasWidget::stopEntityPositioning()
{
    this->map_canvas_entities->stopEntityPositioning();
}

void MapCanvasWidget::clearTileSelectionOverlay()
{
    if (!this->tile_selection_overlay.visible)
        return;

    this->tile_selection_overlay = TileSelectionOverlay();
    update();
}

MapCanvasWidget::TileSelectionRange MapCanvasWidget::tileSelectionRange(int zoom) const
{
    TileSelectionRange range;
    range.zoom = zoom;

    if (!this->tile_selection_overlay.visible || zoom < MapModel::MinZoom || zoom > MapModel::MaxZoom)
        return range;

    const double zoom_scale = std::ldexp(1.0, zoom - this->tile_selection_overlay.zoom);
    const double selected_west_tile = this->tile_selection_overlay.tile_x_min * zoom_scale;
    const double selected_east_tile = (this->tile_selection_overlay.tile_x_max + 1.0) * zoom_scale;
    const double selected_north_tile = this->tile_selection_overlay.tile_y_min * zoom_scale;
    const double selected_south_tile = (this->tile_selection_overlay.tile_y_max + 1.0) * zoom_scale;
    const int tile_count = 1 << zoom;

    range.tile_x_min = int(std::floor(selected_west_tile));
    range.tile_x_max = int(std::ceil(selected_east_tile)) - 1;
    range.tile_y_min = qBound(0, int(std::floor(selected_north_tile)), tile_count - 1);
    range.tile_y_max = qBound(0, int(std::ceil(selected_south_tile)) - 1, tile_count - 1);
    range.valid = range.tile_x_min <= range.tile_x_max && range.tile_y_min <= range.tile_y_max;
    return range;
}

void MapCanvasWidget::paintEventTileSelectionOverlay(QPainter &paint)
{
    if (!this->tile_selection_overlay.visible)
        return;

    const int current_zoom = this->map_model->zoom();
    const TileSelectionRange current_range = tileSelectionRange(current_zoom);
    if (!current_range.valid)
        return;

    const int world_tile_count = 1 << current_zoom;
    const int current_tile_x_min = current_range.tile_x_min;
    const int current_tile_x_max = current_range.tile_x_max;
    const int current_tile_y_min = current_range.tile_y_min;
    const int current_tile_y_max = current_range.tile_y_max;

    const QPointF center_tile = this->map_model->centerTile();
    double west_tile = current_tile_x_min;
    double east_tile = current_tile_x_max + 1.0;
    const double north_tile = current_tile_y_min;
    const double south_tile = current_tile_y_max + 1.0;

    const double selection_center_tile = (west_tile + east_tile) / 2.0;
    const double wrap_shift = std::round((center_tile.x() - selection_center_tile) / world_tile_count) * world_tile_count;
    west_tile += wrap_shift;
    east_tile += wrap_shift;

    const QPointF top_left(
        width() / 2.0 + (west_tile - center_tile.x()) * MapModel::TileSize,
        height() / 2.0 + (north_tile - center_tile.y()) * MapModel::TileSize);
    const QPointF bottom_right(
        width() / 2.0 + (east_tile - center_tile.x()) * MapModel::TileSize,
        height() / 2.0 + (south_tile - center_tile.y()) * MapModel::TileSize);
    const QRectF overlay_rect = QRectF(top_left, bottom_right).normalized();

    if (overlay_rect.isEmpty() || !overlay_rect.intersects(rect()))
        return;

    paint.save();
    paint.setRenderHint(QPainter::Antialiasing, false);

    QLinearGradient fill_gradient(overlay_rect.topLeft(), overlay_rect.bottomRight());
    fill_gradient.setColorAt(0.0, QColor(92, 255, 82, 54));
    fill_gradient.setColorAt(0.5, QColor(32, 224, 58, 66));
    fill_gradient.setColorAt(1.0, QColor(8, 132, 38, 76));
    paint.fillRect(overlay_rect, fill_gradient);

    QPen wide_glow_pen(QColor(60, 255, 78, 54));
    wide_glow_pen.setWidthF(12.0);
    wide_glow_pen.setJoinStyle(Qt::MiterJoin);
    paint.setPen(wide_glow_pen);
    paint.setBrush(Qt::NoBrush);
    paint.drawRect(overlay_rect);

    QPen glow_pen(QColor(92, 255, 96, 120));
    glow_pen.setWidthF(5.0);
    glow_pen.setJoinStyle(Qt::MiterJoin);
    paint.setPen(glow_pen);
    paint.drawRect(overlay_rect);

    QPen border_pen(QColor(155, 255, 145, 230));
    border_pen.setWidthF(1.5);
    border_pen.setJoinStyle(Qt::MiterJoin);
    paint.setPen(border_pen);
    paint.drawRect(overlay_rect);

    QPen grid_pen(QColor(104, 255, 104, 105));
    grid_pen.setWidthF(1.0);
    paint.setPen(grid_pen);

    const double viewport_west_tile = center_tile.x() - width() / 2.0 / MapModel::TileSize;
    const double viewport_east_tile = center_tile.x() + width() / 2.0 / MapModel::TileSize;
    const int first_visible_tile_x = qMax(current_tile_x_min + 1, int(std::ceil(viewport_west_tile - wrap_shift)));
    const int last_visible_tile_x = qMin(current_tile_x_max, int(std::floor(viewport_east_tile - wrap_shift)));
    for (int tile_x = first_visible_tile_x; tile_x <= last_visible_tile_x; ++tile_x)
    {
        const double screen_x = width() / 2.0 + (tile_x + wrap_shift - center_tile.x()) * MapModel::TileSize;
        paint.drawLine(QPointF(screen_x, overlay_rect.top()), QPointF(screen_x, overlay_rect.bottom()));
    }

    const double viewport_north_tile = center_tile.y() - height() / 2.0 / MapModel::TileSize;
    const double viewport_south_tile = center_tile.y() + height() / 2.0 / MapModel::TileSize;
    const int first_visible_tile_y = qMax(current_tile_y_min + 1, int(std::ceil(viewport_north_tile)));
    const int last_visible_tile_y = qMin(current_tile_y_max, int(std::floor(viewport_south_tile)));
    for (int tile_y = first_visible_tile_y; tile_y <= last_visible_tile_y; ++tile_y)
    {
        const double screen_y = height() / 2.0 + (tile_y - center_tile.y()) * MapModel::TileSize;
        paint.drawLine(QPointF(overlay_rect.left(), screen_y), QPointF(overlay_rect.right(), screen_y));
    }

    paint.restore();
}

void MapCanvasWidget::paintEventRectangle(QPainter &paint)
{
    if (!this->rectangle_selection_active || !this->rectangle_dragging)
        return;

    const QRect selection = currentSelectionRect();
    if (selection.isEmpty())
        return;

    const QRectF outer_rect = QRectF(selection).adjusted(2.5, 2.5, -2.5, -2.5);
    if (outer_rect.width() <= 0.0 || outer_rect.height() <= 0.0)
        return;

    const qreal corner_radius = qMin<qreal>(2.0, qMin(outer_rect.width(), outer_rect.height()) / 8.0);

    paint.save();
    paint.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient fill_gradient(outer_rect.topLeft(), outer_rect.bottomLeft());
    fill_gradient.setColorAt(0.0, QColor(35, 151, 211, 24));
    fill_gradient.setColorAt(0.45, QColor(0, 145, 215, 32));
    fill_gradient.setColorAt(1.0, QColor(0, 65, 110, 38));
    paint.setPen(Qt::NoPen);
    paint.setBrush(fill_gradient);
    paint.drawRoundedRect(outer_rect, corner_radius, corner_radius);

    paint.setBrush(Qt::NoBrush);

    QPen wide_glow_pen(QColor(0, 149, 230, 52));
    wide_glow_pen.setWidthF(18.0);
    wide_glow_pen.setJoinStyle(Qt::RoundJoin);
    paint.setPen(wide_glow_pen);
    paint.drawRoundedRect(outer_rect, corner_radius, corner_radius);

    QPen glow_pen(QColor(23, 190, 255, 112));
    glow_pen.setWidthF(9.0);
    glow_pen.setJoinStyle(Qt::RoundJoin);
    paint.setPen(glow_pen);
    paint.drawRoundedRect(outer_rect, corner_radius, corner_radius);

    QPen shadow_pen(QColor(10, 15, 18, 205));
    shadow_pen.setWidthF(5.0);
    shadow_pen.setJoinStyle(Qt::MiterJoin);
    paint.setPen(shadow_pen);
    paint.drawRoundedRect(outer_rect, corner_radius, corner_radius);

    QLinearGradient steel_gradient(outer_rect.topLeft(), outer_rect.bottomLeft());
    steel_gradient.setColorAt(0.0, QColor(245, 250, 252, 245));
    steel_gradient.setColorAt(0.24, QColor(129, 147, 153, 240));
    steel_gradient.setColorAt(0.52, QColor(48, 61, 66, 245));
    steel_gradient.setColorAt(0.78, QColor(177, 190, 194, 240));
    steel_gradient.setColorAt(1.0, QColor(31, 42, 46, 245));

    QPen steel_pen(QBrush(steel_gradient), 3.0);
    steel_pen.setJoinStyle(Qt::MiterJoin);
    paint.setPen(steel_pen);
    paint.drawRoundedRect(outer_rect, corner_radius, corner_radius);

    const QRectF edge_glow_rect = outer_rect.adjusted(1.5, 1.5, -1.5, -1.5);
    if (edge_glow_rect.width() > 0.0 && edge_glow_rect.height() > 0.0)
    {
        QPen edge_glow_pen(QColor(86, 215, 255, 180));
        edge_glow_pen.setWidthF(1.0);
        edge_glow_pen.setJoinStyle(Qt::MiterJoin);
        paint.setPen(edge_glow_pen);
        paint.drawRoundedRect(edge_glow_rect, qMax<qreal>(0.0, corner_radius - 1.0), qMax<qreal>(0.0, corner_radius - 1.0));
    }

    const QRectF blue_frame_rect = outer_rect.adjusted(3.0, 3.0, -3.0, -3.0);
    if (blue_frame_rect.width() > 0.0 && blue_frame_rect.height() > 0.0)
    {
        QPen blue_frame_glow_pen(QColor(36, 196, 255, 96));
        blue_frame_glow_pen.setWidthF(5.0);
        blue_frame_glow_pen.setStyle(Qt::DashLine);
        blue_frame_glow_pen.setDashOffset(1.5);
        blue_frame_glow_pen.setJoinStyle(Qt::MiterJoin);
        paint.setPen(blue_frame_glow_pen);
        paint.drawRoundedRect(blue_frame_rect, qMax<qreal>(0.0, corner_radius - 2.0), qMax<qreal>(0.0, corner_radius - 2.0));

        QPen blue_frame_pen(QColor(102, 224, 255, 245));
        blue_frame_pen.setWidthF(1.5);
        blue_frame_pen.setStyle(Qt::DashLine);
        blue_frame_pen.setDashOffset(1.5);
        blue_frame_pen.setJoinStyle(Qt::MiterJoin);
        paint.setPen(blue_frame_pen);
        paint.drawRoundedRect(blue_frame_rect, qMax<qreal>(0.0, corner_radius - 2.0), qMax<qreal>(0.0, corner_radius - 2.0));
    }

    paint.restore();
}

void MapCanvasWidget::keyPressEvent(QKeyEvent *event)
{
    if (this->rectangle_selection_active && event->key() == Qt::Key_Escape)
    {
        this->cancelRectangleSelection();
        event->accept();
        return;
    }

    if (this->map->handleKeyPressEvent(event))
        return;

    QWidget::keyPressEvent(event);
}

void MapCanvasWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (this->map->handleKeyReleaseEvent(event))
        return;

    QWidget::keyReleaseEvent(event);
}

void MapCanvasWidget::focusOutEvent(QFocusEvent *event)
{
    this->map->clearKeyboardPanInput();
    QWidget::focusOutEvent(event);
}

void MapCanvasWidget::mousePressEvent(QMouseEvent *event)
{
    const bool entity_interaction_enabled = !this->rectangle_selection_active || this->rectangle_selection_interacts_with_entities;

    if (entity_interaction_enabled &&
        event->button() == Qt::LeftButton &&
        !this->rectangle_dragging &&
        (this->map_canvas_entities->selectDeviceLinkAt(event->position()) ||
         this->map_canvas_entities->selectPipeAt(event->position())))
    {
        update();
        event->accept();
        return;
    }
    
    if (event->button() == Qt::RightButton)
    {
        if (this->rectangle_selection_active && !this->rectangle_selection_interacts_with_entities)
        {
            clearTileSelectionOverlay();
            this->rectangle_start_wgs84 = this->map_model->wgs84FromScreen(event->position().toPoint(), size());
            this->rectangle_current_wgs84 = this->rectangle_start_wgs84;
            this->rectangle_dragging = true;
            setCursor(Qt::CrossCursor);
            update();
            event->accept();
            return;
        }

        if (this->map_canvas_entities->anchorMarker(event))
        {
            update();
            event->accept();
            return;
        }
        
        // Pipe vertices and segments must be checked before starting
        // rectangle selection, including while the selection tool is active.
        if (this->map_canvas_entities->showPipeContextMenuAt(
                event->position(),
                event->globalPosition().toPoint()
                ))
        {
            update();
            event->accept();
            return;
        }
        
        if (this->rectangle_selection_active)
        {
            this->rectangle_start_wgs84 = this->map_model->wgs84FromScreen(event->position().toPoint(), size());
            this->rectangle_current_wgs84 = this->rectangle_start_wgs84;
            this->rectangle_dragging = true;
            setCursor(Qt::CrossCursor);
            update();
            event->accept();
            return;
        }
    }
    
    if (this->map->handleMousePressEvent(event))
        return;

    QWidget::mousePressEvent(event);
}

void MapCanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    const bool map_handled_event = this->map->handleMouseMoveEvent(event);
    const bool entity_interaction_enabled = !this->rectangle_selection_active || this->rectangle_selection_interacts_with_entities;

    if (this->rectangle_selection_active && this->rectangle_dragging)
    {
        setCursor(Qt::CrossCursor);
        this->rectangle_current_wgs84 = this->map_model->wgs84FromScreen(event->position().toPoint(), size());

        const QRect selected_rect = currentSelectionRect();
        if (this->rectangle_selection_interacts_with_entities)
            this->map_canvas_entities->onRectangleSelect(selected_rect, RectangleSelectMode::Replace);
        else
            updateTileSelectionOverlay(selected_rect);

        update();
        event->accept();
        return;
    }

    if (map_handled_event)
        return;
    
    if (!entity_interaction_enabled)
    {
        unsetCursor();
        QWidget::mouseMoveEvent(event);
        return;
    }

    this->map_canvas_entities->floatEntity(event);
    
    if (this->map_canvas_entities->isDeviceLinkAt(event->position()) ||
        this->map_canvas_entities->isPipeAt(event->position()))
    {
        setCursor(Qt::PointingHandCursor);
    }
    else
    {
        unsetCursor();
    }
    
    QWidget::mouseMoveEvent(event);
}
void MapCanvasWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (this->rectangle_selection_active && this->rectangle_dragging && event->button() == Qt::RightButton)
    {
        this->rectangle_current_wgs84 = this->map_model->wgs84FromScreen(event->position().toPoint(), size());
        const QRect selected_rect = currentSelectionRect();
        
        if (this->is_rectangle_selection_oneshot)
            this->rectangle_selection_active = false;
        
        this->rectangle_dragging = false;
        
        unsetCursor();
        update();
        
        int distance_min = 0; // 3
        if (selected_rect.width() > distance_min && selected_rect.height() > distance_min)
        {
            if (this->rectangle_selection_interacts_with_entities)
            {
                this->map_canvas_entities->onRectangleSelect(selected_rect, RectangleSelectMode::Replace);
                emit signalRectangleSelected(getSelectionRect(selected_rect));
            }
            else
            {
                updateTileSelectionOverlay(selected_rect);
                emit signalRectangleSelected(getTileSelectionRect());
            }
        }
        else
        {
            emit signalRectangleSelectionCanceled();
        }
        
        event->accept();
        return;
    }
    
    if (this->map->handleMouseReleaseEvent(event))
        return;

    QWidget::mouseReleaseEvent(event);
}

void MapCanvasWidget::wheelEvent(QWheelEvent *event)
{
    this->map->handleWheelEvent(event);
}

void MapCanvasWidget::resizeEvent(QResizeEvent *event)
{
    this->map_canvas_entities->positionMarkers();
    
    QWidget::resizeEvent(event);
}

void MapCanvasWidget::startRectangleSelection(bool oneshot, bool interact_with_entities)
{
    this->is_rectangle_selection_oneshot = oneshot;
    this->rectangle_selection_interacts_with_entities = interact_with_entities;
    
    this->rectangle_selection_active = true;
    this->rectangle_dragging = false;

    if (this->rectangle_selection_interacts_with_entities)
        clearTileSelectionOverlay();
    
    this->rectangle_start_wgs84 = CoordinateWGS84();
    this->rectangle_current_wgs84 = CoordinateWGS84();
    
    unsetCursor();
    setFocus();
    
    update();
}
void MapCanvasWidget::cancelRectangleSelection()
{
    if (!this->rectangle_selection_active)
        return;
    
    if (this->is_rectangle_selection_oneshot)
        this->rectangle_selection_active = false;
    
    this->rectangle_dragging = false;

    if (!this->rectangle_selection_interacts_with_entities)
        clearTileSelectionOverlay();
    
    unsetCursor();
    
    update();
    
    emit signalRectangleSelectionCanceled();
}
QRect MapCanvasWidget::currentSelectionRect() const
{
    const QPoint rectangle_start_pos = this->map_model->screenFromWgs84(this->rectangle_start_wgs84, size()).toPoint();
    const QPoint rectangle_current_pos = this->map_model->screenFromWgs84(this->rectangle_current_wgs84, size()).toPoint();

    return QRect(rectangle_start_pos, rectangle_current_pos).normalized();
}
CoordinateWGS84Rect MapCanvasWidget::getSelectionRect(const QRect &selected_rect) const
{
    CoordinateWGS84Rect rect;
    
    rect.north_west = this->map_model->wgs84FromScreen(
        selected_rect.topLeft(),
        size()
        );
    rect.south_east = this->map_model->wgs84FromScreen(
        selected_rect.bottomRight(),
        size()
        );
    
    return rect;
}

void MapCanvasWidget::updateTileSelectionOverlay(const QRect &selected_rect)
{
    if (selected_rect.isEmpty())
    {
        clearTileSelectionOverlay();
        return;
    }

    const int zoom = this->map_model->zoom();
    const int tile_count = 1 << zoom;
    const QPointF center_tile = this->map_model->centerTile();
    const double tile_x_first = center_tile.x() + (selected_rect.left() - width() / 2.0) / MapModel::TileSize;
    const double tile_x_second = center_tile.x() + (selected_rect.right() - width() / 2.0) / MapModel::TileSize;
    const double tile_y_first = center_tile.y() + (selected_rect.top() - height() / 2.0) / MapModel::TileSize;
    const double tile_y_second = center_tile.y() + (selected_rect.bottom() - height() / 2.0) / MapModel::TileSize;

    const int tile_y_min = int(std::floor(qMin(tile_y_first, tile_y_second)));
    const int tile_y_max = int(std::floor(qMax(tile_y_first, tile_y_second)));
    if (tile_y_max < 0 || tile_y_min >= tile_count)
    {
        clearTileSelectionOverlay();
        return;
    }

    this->tile_selection_overlay.zoom = zoom;
    this->tile_selection_overlay.tile_x_min = int(std::floor(qMin(tile_x_first, tile_x_second)));
    this->tile_selection_overlay.tile_x_max = int(std::floor(qMax(tile_x_first, tile_x_second)));
    this->tile_selection_overlay.tile_y_min = qBound(0, tile_y_min, tile_count - 1);
    this->tile_selection_overlay.tile_y_max = qBound(0, tile_y_max, tile_count - 1);
    this->tile_selection_overlay.visible = this->tile_selection_overlay.tile_x_min <= this->tile_selection_overlay.tile_x_max &&
                                           this->tile_selection_overlay.tile_y_min <= this->tile_selection_overlay.tile_y_max;

    update();
}

CoordinateWGS84Rect MapCanvasWidget::getTileSelectionRect() const
{
    CoordinateWGS84Rect rect;
    rect.north_west.latitude_deg = GeoWebMercator::tileYToLat(
        this->tile_selection_overlay.tile_y_min,
        this->tile_selection_overlay.zoom);
    rect.north_west.longitude_deg = GeoWebMercator::normalizeLongitude(
        GeoWebMercator::tileXToLon(
            this->tile_selection_overlay.tile_x_min,
            this->tile_selection_overlay.zoom));
    rect.south_east.latitude_deg = GeoWebMercator::tileYToLat(
        this->tile_selection_overlay.tile_y_max + 1.0,
        this->tile_selection_overlay.zoom);
    rect.south_east.longitude_deg = GeoWebMercator::normalizeLongitude(
        GeoWebMercator::tileXToLon(
            this->tile_selection_overlay.tile_x_max + 1.0,
            this->tile_selection_overlay.zoom));
    return rect;
}
