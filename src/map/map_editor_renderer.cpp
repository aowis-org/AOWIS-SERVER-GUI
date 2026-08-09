#include "map_editor_renderer.h"

#include "map_model.h"

#include "../geo_web_mercator.h"

#include <QBrush>
#include <QColor>
#include <QHash>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QRegion>
#include <QWidget>

#include <cmath>

namespace
{
constexpr double marker_dot_radius = 5.0;
constexpr double connection_target_radius = 9.0;
constexpr double pipe_vertex_radius = 4.0;

bool isHydraulicDeviceLink(InfrastructureEntity entity)
{
    return entity == InfrastructureEntity::Pump || entity == InfrastructureEntity::Valve;
}
}

MapEditorRenderer::MapEditorRenderer(MapModel *map_model, QWidget *canvas)
    : map_model(map_model), canvas(canvas)
{
}

void MapEditorRenderer::paint(QPainter &painter, const QPaintEvent &event,
                              const NetworkRenderSnapshot &network_snapshot,
                              const MapEditorVisualState &visual_state,
                              const MapEditorViewportRenderState &viewport_state)
{
#ifdef Q_OS_WASM
    Q_UNUSED(network_snapshot)
    Q_UNUSED(visual_state)
    Q_UNUSED(viewport_state)

    painter.setCompositionMode(QPainter::CompositionMode_Source);
    const QRegion dirty_region = event.region();
    for (const QRect &dirty_rect : dirty_region)
        painter.fillRect(dirty_rect, Qt::transparent);
    return;
#else
    Q_UNUSED(event)
    painter.setRenderHint(QPainter::Antialiasing);
#endif

    paintBackground(painter, viewport_state);
    paintTileSelection(painter, viewport_state);
    paintRectangleSelection(painter, viewport_state);
    paintNetwork(painter, network_snapshot, visual_state);
}

void MapEditorRenderer::prepareProjection(double wrap_reference_longitude)
{
    if (!this->map_model || !this->canvas)
    {
        this->projection_ready = false;
        return;
    }

    this->projection_zoom = this->map_model->zoom();
    this->projection_center_tile = this->map_model->centerTile();
    this->projection_viewport_size = this->canvas->size();

    const double wrapped_reference_longitude =
        GeoWebMercator::normalizeLongitude(wrap_reference_longitude);
    this->projection_reference_base_tile_x = GeoWebMercator::lonToTileX(
        wrapped_reference_longitude, this->projection_zoom);
    this->projection_reference_tile_x = GeoWebMercator::nearestWrappedTileX(
        this->projection_reference_base_tile_x, this->projection_center_tile.x(),
        this->projection_zoom);
    this->projection_ready = true;
}

QPointF MapEditorRenderer::screenFromWgs84(const CoordinateWGS84 &coordinate,
                                           double wrap_reference_longitude) const
{
    Q_UNUSED(wrap_reference_longitude)

    if (!this->projection_ready)
        return QPointF();

    const double wrapped_longitude = GeoWebMercator::normalizeLongitude(
        coordinate.longitude_deg);
    const double base_tile_x = GeoWebMercator::lonToTileX(
        wrapped_longitude, this->projection_zoom);
    const double local_tile_x = GeoWebMercator::nearestWrappedTileX(
        base_tile_x, this->projection_reference_base_tile_x, this->projection_zoom);
    const double tile_x = local_tile_x + this->projection_reference_tile_x -
        this->projection_reference_base_tile_x;
    const double tile_y = GeoWebMercator::latToTileY(
        coordinate.latitude_deg, this->projection_zoom);

    return QPointF(
        double(this->projection_viewport_size.width()) / 2.0 +
            (tile_x - this->projection_center_tile.x()) * MapModel::TileSize,
        double(this->projection_viewport_size.height()) / 2.0 +
            (tile_y - this->projection_center_tile.y()) * MapModel::TileSize);
}

const NetworkRenderNode *MapEditorRenderer::nodeByUuid(
    const QHash<QUuid, const NetworkRenderNode *> &nodes_by_uuid,
    const QUuid &uuid) const
{
    const QHash<QUuid, const NetworkRenderNode *>::const_iterator iterator =
        nodes_by_uuid.constFind(uuid);
    if (iterator == nodes_by_uuid.cend())
        return nullptr;
    return iterator.value();
}

CoordinateWGS84 MapEditorRenderer::deviceLinkCenterCoordinate(
    const NetworkRenderLink &link) const
{
    if (link.vertices_wgs84.size() > 2)
        return link.vertices_wgs84.at(1);

    CoordinateWGS84 center;
    if (link.vertices_wgs84.size() < 2)
        return center;

    const CoordinateWGS84 &start = link.vertices_wgs84.first();
    const CoordinateWGS84 &end = link.vertices_wgs84.last();
    center.latitude_deg = (start.latitude_deg + end.latitude_deg) / 2.0;
    const double longitude_delta = GeoWebMercator::normalizeLongitude(
        end.longitude_deg - start.longitude_deg);
    center.longitude_deg = GeoWebMercator::normalizeLongitude(
        start.longitude_deg + longitude_delta / 2.0);
    return center;
}

void MapEditorRenderer::paintBackground(
    QPainter &painter, const MapEditorViewportRenderState &viewport_state) const
{
    if (!this->canvas || viewport_state.background_opacity <= 0)
        return;

    QColor background = this->canvas->palette().color(QPalette::Window);
    background.setAlphaF(viewport_state.background_opacity / 100.0);
    painter.fillRect(this->canvas->rect(), background);
}

void MapEditorRenderer::paintTileSelection(
    QPainter &painter, const MapEditorViewportRenderState &viewport_state) const
{
    if (!this->map_model || !this->canvas || !viewport_state.tile_selection_visible ||
        viewport_state.tile_x_max < viewport_state.tile_x_min ||
        viewport_state.tile_y_max < viewport_state.tile_y_min)
    {
        return;
    }

    const int current_zoom = this->map_model->zoom();
    const int world_tile_count = 1 << current_zoom;
    const QPointF center_tile = this->map_model->centerTile();
    double west_tile = viewport_state.tile_x_min;
    double east_tile = viewport_state.tile_x_max + 1.0;
    const double north_tile = viewport_state.tile_y_min;
    const double south_tile = viewport_state.tile_y_max + 1.0;

    const double selection_center_tile = (west_tile + east_tile) / 2.0;
    const double wrap_shift = std::round(
        (center_tile.x() - selection_center_tile) / world_tile_count) * world_tile_count;
    west_tile += wrap_shift;
    east_tile += wrap_shift;

    const QPointF top_left(
        this->canvas->width() / 2.0 + (west_tile - center_tile.x()) * MapModel::TileSize,
        this->canvas->height() / 2.0 + (north_tile - center_tile.y()) * MapModel::TileSize);
    const QPointF bottom_right(
        this->canvas->width() / 2.0 + (east_tile - center_tile.x()) * MapModel::TileSize,
        this->canvas->height() / 2.0 + (south_tile - center_tile.y()) * MapModel::TileSize);
    const QRectF overlay_rect = QRectF(top_left, bottom_right).normalized();

    if (overlay_rect.isEmpty() || !overlay_rect.intersects(this->canvas->rect()))
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);

    QLinearGradient fill_gradient(overlay_rect.topLeft(), overlay_rect.bottomRight());
    fill_gradient.setColorAt(0.0, QColor(92, 255, 82, 54));
    fill_gradient.setColorAt(0.5, QColor(32, 224, 58, 66));
    fill_gradient.setColorAt(1.0, QColor(8, 132, 38, 76));
    painter.fillRect(overlay_rect, fill_gradient);

    QPen wide_glow_pen(QColor(60, 255, 78, 54));
    wide_glow_pen.setWidthF(12.0);
    wide_glow_pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(wide_glow_pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(overlay_rect);

    QPen glow_pen(QColor(92, 255, 96, 120));
    glow_pen.setWidthF(5.0);
    glow_pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(glow_pen);
    painter.drawRect(overlay_rect);

    QPen border_pen(QColor(155, 255, 145, 230));
    border_pen.setWidthF(1.5);
    border_pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(border_pen);
    painter.drawRect(overlay_rect);

    QPen grid_pen(QColor(104, 255, 104, 105));
    grid_pen.setWidthF(1.0);
    painter.setPen(grid_pen);

    const double viewport_west_tile = center_tile.x() -
        this->canvas->width() / 2.0 / MapModel::TileSize;
    const double viewport_east_tile = center_tile.x() +
        this->canvas->width() / 2.0 / MapModel::TileSize;
    const int first_visible_tile_x = qMax(
        viewport_state.tile_x_min + 1,
        int(std::ceil(viewport_west_tile - wrap_shift)));
    const int last_visible_tile_x = qMin(
        viewport_state.tile_x_max,
        int(std::floor(viewport_east_tile - wrap_shift)));
    for (int tile_x = first_visible_tile_x; tile_x <= last_visible_tile_x; ++tile_x)
    {
        const double screen_x = this->canvas->width() / 2.0 +
            (tile_x + wrap_shift - center_tile.x()) * MapModel::TileSize;
        painter.drawLine(QPointF(screen_x, overlay_rect.top()),
                         QPointF(screen_x, overlay_rect.bottom()));
    }

    const double viewport_north_tile = center_tile.y() -
        this->canvas->height() / 2.0 / MapModel::TileSize;
    const double viewport_south_tile = center_tile.y() +
        this->canvas->height() / 2.0 / MapModel::TileSize;
    const int first_visible_tile_y = qMax(
        viewport_state.tile_y_min + 1, int(std::ceil(viewport_north_tile)));
    const int last_visible_tile_y = qMin(
        viewport_state.tile_y_max, int(std::floor(viewport_south_tile)));
    for (int tile_y = first_visible_tile_y; tile_y <= last_visible_tile_y; ++tile_y)
    {
        const double screen_y = this->canvas->height() / 2.0 +
            (tile_y - center_tile.y()) * MapModel::TileSize;
        painter.drawLine(QPointF(overlay_rect.left(), screen_y),
                         QPointF(overlay_rect.right(), screen_y));
    }

    painter.restore();
}

void MapEditorRenderer::paintRectangleSelection(
    QPainter &painter, const MapEditorViewportRenderState &viewport_state) const
{
    if (!viewport_state.rectangle_selection_visible ||
        viewport_state.rectangle_selection.isEmpty())
    {
        return;
    }

    const QRectF outer_rect = QRectF(viewport_state.rectangle_selection)
        .adjusted(2.5, 2.5, -2.5, -2.5);
    if (outer_rect.width() <= 0.0 || outer_rect.height() <= 0.0)
        return;

    const qreal corner_radius = qMin<qreal>(
        2.0, qMin(outer_rect.width(), outer_rect.height()) / 8.0);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient fill_gradient(outer_rect.topLeft(), outer_rect.bottomLeft());
    fill_gradient.setColorAt(0.0, QColor(35, 151, 211, 24));
    fill_gradient.setColorAt(0.45, QColor(0, 145, 215, 32));
    fill_gradient.setColorAt(1.0, QColor(0, 65, 110, 38));
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill_gradient);
    painter.drawRoundedRect(outer_rect, corner_radius, corner_radius);

    painter.setBrush(Qt::NoBrush);

    QPen wide_glow_pen(QColor(0, 149, 230, 52));
    wide_glow_pen.setWidthF(18.0);
    wide_glow_pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(wide_glow_pen);
    painter.drawRoundedRect(outer_rect, corner_radius, corner_radius);

    QPen glow_pen(QColor(23, 190, 255, 112));
    glow_pen.setWidthF(9.0);
    glow_pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(glow_pen);
    painter.drawRoundedRect(outer_rect, corner_radius, corner_radius);

    QPen shadow_pen(QColor(10, 15, 18, 205));
    shadow_pen.setWidthF(5.0);
    shadow_pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(shadow_pen);
    painter.drawRoundedRect(outer_rect, corner_radius, corner_radius);

    QLinearGradient steel_gradient(outer_rect.topLeft(), outer_rect.bottomLeft());
    steel_gradient.setColorAt(0.0, QColor(245, 250, 252, 245));
    steel_gradient.setColorAt(0.24, QColor(129, 147, 153, 240));
    steel_gradient.setColorAt(0.52, QColor(48, 61, 66, 245));
    steel_gradient.setColorAt(0.78, QColor(177, 190, 194, 240));
    steel_gradient.setColorAt(1.0, QColor(31, 42, 46, 245));

    QPen steel_pen(QBrush(steel_gradient), 3.0);
    steel_pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(steel_pen);
    painter.drawRoundedRect(outer_rect, corner_radius, corner_radius);

    const QRectF edge_glow_rect = outer_rect.adjusted(1.5, 1.5, -1.5, -1.5);
    if (edge_glow_rect.width() > 0.0 && edge_glow_rect.height() > 0.0)
    {
        QPen edge_glow_pen(QColor(86, 215, 255, 180));
        edge_glow_pen.setWidthF(1.0);
        edge_glow_pen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(edge_glow_pen);
        painter.drawRoundedRect(
            edge_glow_rect, qMax<qreal>(0.0, corner_radius - 1.0),
            qMax<qreal>(0.0, corner_radius - 1.0));
    }

    const QRectF blue_frame_rect = outer_rect.adjusted(3.0, 3.0, -3.0, -3.0);
    if (blue_frame_rect.width() > 0.0 && blue_frame_rect.height() > 0.0)
    {
        QPen blue_frame_glow_pen(QColor(36, 196, 255, 96));
        blue_frame_glow_pen.setWidthF(5.0);
        blue_frame_glow_pen.setStyle(Qt::DashLine);
        blue_frame_glow_pen.setDashOffset(1.5);
        blue_frame_glow_pen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(blue_frame_glow_pen);
        painter.drawRoundedRect(
            blue_frame_rect, qMax<qreal>(0.0, corner_radius - 2.0),
            qMax<qreal>(0.0, corner_radius - 2.0));

        QPen blue_frame_pen(QColor(102, 224, 255, 245));
        blue_frame_pen.setWidthF(1.5);
        blue_frame_pen.setStyle(Qt::DashLine);
        blue_frame_pen.setDashOffset(1.5);
        blue_frame_pen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(blue_frame_pen);
        painter.drawRoundedRect(
            blue_frame_rect, qMax<qreal>(0.0, corner_radius - 2.0),
            qMax<qreal>(0.0, corner_radius - 2.0));
    }

    painter.restore();
}

void MapEditorRenderer::paintNetwork(
    QPainter &painter,
    const NetworkRenderSnapshot &network_snapshot,
    const MapEditorVisualState &visual_state)
{
    prepareProjection(visual_state.wrap_reference_longitude);
    if (!this->projection_ready)
        return;

    QHash<QUuid, const NetworkRenderNode *> nodes_by_uuid;
    const bool needs_node_lookup = visual_state.placement.creating &&
        (visual_state.placement.entity == InfrastructureEntity::Pipe ||
         isHydraulicDeviceLink(visual_state.placement.entity));
    if (needs_node_lookup)
    {
        nodes_by_uuid.reserve(network_snapshot.nodes.size());
        for (const NetworkRenderNode &node : network_snapshot.nodes)
            nodes_by_uuid.insert(node.uuid, &node);
    }

    paintPipes(painter, network_snapshot, visual_state, nodes_by_uuid);
    paintDeviceLinks(painter, network_snapshot, visual_state, nodes_by_uuid);
    paintMarkers(painter, network_snapshot, visual_state);
    paintPlacement(painter, visual_state, nodes_by_uuid);
}

void MapEditorRenderer::paintPipes(
    QPainter &painter,
    const NetworkRenderSnapshot &network_snapshot,
    const MapEditorVisualState &visual_state,
    const QHash<QUuid, const NetworkRenderNode *> &nodes_by_uuid) const
{
    painter.save();

    for (const NetworkRenderLink &link : network_snapshot.links)
    {
        if (link.entity_type != InfrastructureEntity::Pipe ||
            link.vertices_wgs84.size() < 2)
        {
            continue;
        }

        const bool selected = visual_state.selected_pipe_uuids.contains(link.uuid);
        QPen pipe_pen(selected ? QColor(0, 190, 255) : QColor(Qt::black));
        pipe_pen.setWidthF(3.0);
        pipe_pen.setCapStyle(Qt::RoundCap);
        pipe_pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pipe_pen);

        QPointF previous_point = screenFromWgs84(
            link.vertices_wgs84.first(), visual_state.wrap_reference_longitude);
        for (qsizetype index = 1; index < link.vertices_wgs84.size(); ++index)
        {
            const QPointF point = screenFromWgs84(
                link.vertices_wgs84.at(index), visual_state.wrap_reference_longitude);
            painter.drawLine(previous_point, point);
            previous_point = point;
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(selected ? QColor(0, 190, 255) : QColor(Qt::black));
        for (qsizetype index = 1; index + 1 < link.vertices_wgs84.size(); ++index)
        {
            painter.drawEllipse(
                screenFromWgs84(link.vertices_wgs84.at(index),
                                visual_state.wrap_reference_longitude),
                pipe_vertex_radius, pipe_vertex_radius);
        }
    }

    if (visual_state.placement.creating &&
        visual_state.placement.entity == InfrastructureEntity::Pipe &&
        !visual_state.placement.pipe_start_node_uuid.isNull())
    {
        const NetworkRenderNode *start_node = nodeByUuid(
            nodes_by_uuid, visual_state.placement.pipe_start_node_uuid);
        if (start_node)
        {
            QPen preview_pen(QColor(0, 140, 255));
            preview_pen.setWidthF(3.0);
            preview_pen.setCapStyle(Qt::RoundCap);
            preview_pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(preview_pen);

            QPointF previous_point = screenFromWgs84(
                start_node->coordinate_wgs84, visual_state.wrap_reference_longitude);
            for (const CoordinateWGS84 &vertex :
                 visual_state.placement.pipe_intermediate_vertices)
            {
                const QPointF vertex_point = screenFromWgs84(
                    vertex, visual_state.wrap_reference_longitude);
                painter.drawLine(previous_point, vertex_point);
                previous_point = vertex_point;
            }

            QPointF preview_end = visual_state.placement.mouse_position;
            const NetworkRenderNode *end_node = nodeByUuid(
                nodes_by_uuid, visual_state.placement.connection_target_uuid);
            if (end_node)
            {
                preview_end = screenFromWgs84(
                    end_node->coordinate_wgs84,
                    visual_state.wrap_reference_longitude);
            }
            painter.drawLine(previous_point, preview_end);
        }
    }

    painter.restore();
}

void MapEditorRenderer::paintDeviceLinks(
    QPainter &painter,
    const NetworkRenderSnapshot &network_snapshot,
    const MapEditorVisualState &visual_state,
    const QHash<QUuid, const NetworkRenderNode *> &nodes_by_uuid)
{
    painter.save();

    for (const NetworkRenderLink &link : network_snapshot.links)
    {
        if (!isHydraulicDeviceLink(link.entity_type) ||
            link.vertices_wgs84.size() < 2)
        {
            continue;
        }

        const QPointF start_point = screenFromWgs84(
            link.vertices_wgs84.first(), visual_state.wrap_reference_longitude);
        const QPointF center_point = screenFromWgs84(
            deviceLinkCenterCoordinate(link), visual_state.wrap_reference_longitude);
        const QPointF end_point = screenFromWgs84(
            link.vertices_wgs84.last(), visual_state.wrap_reference_longitude);
        const bool selected = visual_state.selected_marker_uuids.contains(link.uuid);

        QPen placed_pen(selected ? QColor(0, 190, 255) : QColor(139, 90, 43));
        placed_pen.setWidthF(3.0);
        placed_pen.setCapStyle(Qt::RoundCap);
        placed_pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(placed_pen);
        painter.drawLine(start_point, center_point);
        painter.drawLine(center_point, end_point);
    }

    if (visual_state.placement.creating &&
        isHydraulicDeviceLink(visual_state.placement.entity) &&
        !visual_state.placement.device_link_start_node_uuid.isNull())
    {
        const NetworkRenderNode *start_node = nodeByUuid(
            nodes_by_uuid, visual_state.placement.device_link_start_node_uuid);
        if (start_node)
        {
            const QPointF start_point = screenFromWgs84(
                start_node->coordinate_wgs84,
                visual_state.wrap_reference_longitude);
            QPointF end_point = visual_state.placement.mouse_position;
            const NetworkRenderNode *end_node = nodeByUuid(
                nodes_by_uuid, visual_state.placement.connection_target_uuid);
            if (end_node)
            {
                end_point = screenFromWgs84(
                    end_node->coordinate_wgs84,
                    visual_state.wrap_reference_longitude);
            }

            const QPointF center_point = (start_point + end_point) / 2.0;
            QPen preview_pen(QColor(0, 140, 255));
            preview_pen.setWidthF(3.0);
            preview_pen.setCapStyle(Qt::RoundCap);
            preview_pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(preview_pen);
            painter.drawLine(start_point, center_point);
            painter.drawLine(center_point, end_point);
        }
    }

    for (const NetworkRenderLink &link : network_snapshot.links)
    {
        if (!isHydraulicDeviceLink(link.entity_type) ||
            link.vertices_wgs84.size() < 2)
        {
            continue;
        }

        const QString path = MapEntityPixmapRenderer::pixmapPathForEntity(
            link.entity_type);
        const QRectF target_rect = this->pixmap_renderer.centeredRect(
            screenFromWgs84(deviceLinkCenterCoordinate(link),
                            visual_state.wrap_reference_longitude),
            path, visual_state.entity_width);
        const MapEntityPixmapRenderer::Highlight highlight =
            visual_state.selected_marker_uuids.contains(link.uuid)
            ? MapEntityPixmapRenderer::Highlight::Selected
            : MapEntityPixmapRenderer::Highlight::None;
        this->pixmap_renderer.paint(
            painter, path, visual_state.entity_width, target_rect, highlight);
    }

    painter.restore();
}

void MapEditorRenderer::paintMarkers(
    QPainter &painter,
    const NetworkRenderSnapshot &network_snapshot,
    const MapEditorVisualState &visual_state)
{
    painter.save();
    painter.setPen(Qt::NoPen);

    QList<QPointF> screen_positions;
    screen_positions.reserve(network_snapshot.nodes.size());
    for (const NetworkRenderNode &node : network_snapshot.nodes)
    {
        screen_positions.append(screenFromWgs84(
            node.coordinate_wgs84, visual_state.wrap_reference_longitude));
    }

    for (qsizetype index = 0; index < network_snapshot.nodes.size(); ++index)
    {
        const NetworkRenderNode &node = network_snapshot.nodes.at(index);
        const QPointF &point = screen_positions.at(index);
        if (node.uuid == visual_state.placement.connection_target_uuid)
        {
            painter.setBrush(QColor(0, 140, 255));
            painter.drawEllipse(point, connection_target_radius, connection_target_radius);
        }
        else
        {
            painter.setBrush(Qt::black);
            painter.drawEllipse(point, marker_dot_radius, marker_dot_radius);
        }
    }

    for (qsizetype index = 0; index < network_snapshot.nodes.size(); ++index)
    {
        const NetworkRenderNode &node = network_snapshot.nodes.at(index);
        const QString path = MapEntityPixmapRenderer::pixmapPathForEntity(
            node.entity_type);
        const QPointF &screen_position = screen_positions.at(index);
        const QPointF rounded_anchor(
            qRound(screen_position.x()), qRound(screen_position.y()));
        const QRectF target_rect = this->pixmap_renderer.bottomAnchoredRect(
            rounded_anchor, path, visual_state.entity_width);
        const MapEntityPixmapRenderer::Highlight highlight =
            visual_state.selected_marker_uuids.contains(node.uuid)
            ? MapEntityPixmapRenderer::Highlight::Selected
            : MapEntityPixmapRenderer::Highlight::None;
        this->pixmap_renderer.paint(
            painter, path, visual_state.entity_width, target_rect, highlight);
    }

    painter.restore();
}

void MapEditorRenderer::paintPlacement(
    QPainter &painter,
    const MapEditorVisualState &visual_state,
    const QHash<QUuid, const NetworkRenderNode *> &nodes_by_uuid)
{
    if (!visual_state.placement.creating ||
        !visual_state.placement.floating_marker_visible ||
        visual_state.placement.entity == InfrastructureEntity::Unknown ||
        visual_state.placement.floating_width <= 0)
    {
        return;
    }

    const QString path = MapEntityPixmapRenderer::pixmapPathForEntity(
        visual_state.placement.entity);
    const QPointF mouse_position = visual_state.placement.mouse_position;

    if (isHydraulicDeviceLink(visual_state.placement.entity) &&
        !visual_state.placement.device_link_start_node_uuid.isNull())
    {
        const NetworkRenderNode *start_node = nodeByUuid(
            nodes_by_uuid, visual_state.placement.device_link_start_node_uuid);
        if (start_node)
        {
            const QPointF start_point = screenFromWgs84(
                start_node->coordinate_wgs84,
                visual_state.wrap_reference_longitude);
            QPointF end_point = mouse_position;
            const NetworkRenderNode *end_node = nodeByUuid(
                nodes_by_uuid, visual_state.placement.connection_target_uuid);
            if (end_node)
            {
                end_point = screenFromWgs84(
                    end_node->coordinate_wgs84,
                    visual_state.wrap_reference_longitude);
            }

            const QRectF target_rect = this->pixmap_renderer.centeredRect(
                (start_point + end_point) / 2.0, path,
                visual_state.placement.floating_width);
            this->pixmap_renderer.paint(
                painter, path, visual_state.placement.floating_width, target_rect);
            return;
        }
    }

    const QRectF target_rect = this->pixmap_renderer.bottomAnchoredRect(
        mouse_position, path, visual_state.placement.floating_width);
    this->pixmap_renderer.paint(
        painter, path, visual_state.placement.floating_width, target_rect);
}
