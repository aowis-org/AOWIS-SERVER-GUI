#include "map_network_overlay_widget.h"

#include "../geo_web_mercator.h"
#include "../hydraulic_data.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPalette>
#include <QPen>
#include <QSet>
#include <QTimer>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr qreal NetworkImagePadding = 8.0;
constexpr qreal NetworkLinkWidth = 3.0;
constexpr qreal NetworkNodeWidth = 8.0;
constexpr qreal LinkHitDistance = 7.0;
constexpr qreal SpatialCellSize = 128.0;
const QColor NetworkColor(QStringLiteral("#b000ff"));

bool isFiniteCoordinate(const CoordinateWGS84 &coordinate)
{
    return std::isfinite(coordinate.longitude_deg) && std::isfinite(coordinate.latitude_deg);
}

double nearestWrappedWorldPixel(double raw_pixel_x, double reference_pixel_x, int zoom)
{
    const double raw_tile_x = raw_pixel_x / MapModel::TileSize;
    const double reference_tile_x = reference_pixel_x / MapModel::TileSize;
    return GeoWebMercator::nearestWrappedTileX(raw_tile_x, reference_tile_x, zoom) * MapModel::TileSize;
}

qreal snapToPhysicalPixel(qreal value, qreal device_pixel_ratio)
{
    return std::round(value * device_pixel_ratio) / device_pixel_ratio;
}

qreal markerWidthForZoom(int zoom)
{
    if (zoom == 19)
        return 40.0;
    if (zoom == 18)
        return 30.0;
    if (zoom == 17)
        return 20.0;
    return 10.0;
}

int spatialCellCoordinate(qreal value)
{
    return qFloor(value / SpatialCellSize);
}

quint64 spatialCellKey(int cell_x, int cell_y)
{
    return (quint64(quint32(cell_x)) << 32) | quint32(cell_y);
}

qreal pointToSegmentDistanceSquared(qreal point_x, qreal point_y, const QPointF &start, const QPointF &end)
{
    const qreal segment_x = end.x() - start.x();
    const qreal segment_y = end.y() - start.y();
    const qreal length_squared = segment_x * segment_x + segment_y * segment_y;
    if (length_squared <= 0.0)
    {
        const qreal delta_x = point_x - start.x();
        const qreal delta_y = point_y - start.y();
        return delta_x * delta_x + delta_y * delta_y;
    }

    const qreal projection = ((point_x - start.x()) * segment_x +
        (point_y - start.y()) * segment_y) / length_squared;
    const qreal bounded_projection = qBound<qreal>(0.0, projection, 1.0);
    const qreal nearest_x = start.x() + bounded_projection * segment_x;
    const qreal nearest_y = start.y() + bounded_projection * segment_y;
    const qreal delta_x = point_x - nearest_x;
    const qreal delta_y = point_y - nearest_y;
    return delta_x * delta_x + delta_y * delta_y;
}
}

MapNetworkOverlayWidget::MapNetworkOverlayWidget(MapModel *map_model, HydraulicData *hydraulic_data, QWidget *parent)
    : QWidget(parent),
    map_model(map_model),
    hydraulic_data(hydraulic_data)
{
    Q_ASSERT(this->map_model);
    Q_ASSERT(this->hydraulic_data);

    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);

    connect(this->map_model, &MapModel::centerChangedWGS84, this, [this]
    {
        update();
    });
    connect(this->map_model, &MapModel::zoomChanged, this, [this]
    {
        invalidateCache();
        update();
    });

    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this, &MapNetworkOverlayWidget::syncSnapshot);
    connect(this->hydraulic_data, &HydraulicData::signalNetworkGeometryChanged,
        this, &MapNetworkOverlayWidget::syncSnapshot);

    QTimer::singleShot(0, this, &MapNetworkOverlayWidget::syncSnapshot);
}

int MapNetworkOverlayWidget::backgroundOpacity() const
{
    return this->background_opacity;
}

void MapNetworkOverlayWidget::setBackgroundOpacity(int opacity)
{
    const int bounded_opacity = qBound(0, opacity, 100);
    if (this->background_opacity == bounded_opacity)
        return;

    this->background_opacity = bounded_opacity;
    update();
}

NetworkOverlayHit MapNetworkOverlayWidget::hitTest(const QPointF &screen_position)
{
    NetworkOverlayHit no_hit;
    if (!isVisible() || !std::isfinite(screen_position.x()) || !std::isfinite(screen_position.y()) ||
        screen_position.x() < 0.0 || screen_position.y() < 0.0 ||
        screen_position.x() > width() || screen_position.y() > height())
    {
        return no_hit;
    }

    ensureCache();
    if (!this->cached_geometry_ready)
        return no_hit;

    const QPointF world_position = geometryWorldPosition(screen_position);
    const NetworkOverlayHit marker_hit = nearestMarkerHit(
        world_position.x(), world_position.y(), markerWidthForZoom(this->cached_zoom) / 2.0);
    if (marker_hit.isValid())
        return marker_hit;

    const NetworkOverlayHit device_hit = nearestSegmentHit(
        world_position.x(), world_position.y(), HitCollection::DeviceSegments);
    if (device_hit.isValid())
        return device_hit;

    return nearestSegmentHit(world_position.x(), world_position.y(), HitCollection::PipeSegments);
}

void MapNetworkOverlayWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setClipRegion(event->region());

    if (this->background_opacity > 0)
    {
        QColor background = palette().color(QPalette::Window);
        background.setAlphaF(this->background_opacity / 100.0);
        painter.fillRect(rect(), background);
    }

    if (!this->snapshot_initialized ||
        (this->snapshot.nodes.isEmpty() && this->snapshot.links.isEmpty()))
    {
        return;
    }

    ensureCache();
    if (this->cached_image.isNull())
        return;

    painter.drawImage(cachedImageScreenPosition(), this->cached_image);
}

void MapNetworkOverlayWidget::syncSnapshot()
{
    const quint64 current_geometry_revision = this->hydraulic_data->geometryRevision();
    if (this->snapshot_initialized && this->geometry_revision == current_geometry_revision)
        return;

    this->snapshot = this->hydraulic_data->networkRenderSnapshot();
    this->geometry_revision = this->snapshot.geometry_revision;
    this->snapshot_initialized = true;
    invalidateCache();
    update();
}

void MapNetworkOverlayWidget::invalidateCache()
{
    this->cached_image = QImage();
    this->cached_image_world_top_left = QPointF();
    this->cached_geometry_world_origin = QPointF();
    this->cached_zoom = -1;
    this->cached_device_pixel_ratio = 0.0;
    this->cache_initialized = false;
    this->cached_geometry_ready = false;
    this->hit_markers.clear();
    this->device_hit_segments.clear();
    this->pipe_hit_segments.clear();
    this->global_device_segment_indices.clear();
    this->global_pipe_segment_indices.clear();
    this->spatial_cells.clear();
}

void MapNetworkOverlayWidget::ensureCache()
{
    if (!this->snapshot_initialized)
        return;

    const int zoom = this->map_model->zoom();
    const qreal device_pixel_ratio = devicePixelRatioF();
    if (!this->cache_initialized || this->cached_zoom != zoom ||
        !qFuzzyCompare(this->cached_device_pixel_ratio, device_pixel_ratio))
    {
        rebuildCache();
    }
}

void MapNetworkOverlayWidget::rebuildCache()
{
    const int zoom = this->map_model->zoom();
    const qreal device_pixel_ratio = devicePixelRatioF();
    QList<ProjectedNode> projected_nodes;
    QList<ProjectedLink> projected_links;
    QHash<quint32, QPointF> node_positions;
    double anchor_x = std::numeric_limits<double>::quiet_NaN();
    double minimum_x = std::numeric_limits<double>::infinity();
    double minimum_y = std::numeric_limits<double>::infinity();
    double maximum_x = -std::numeric_limits<double>::infinity();
    double maximum_y = -std::numeric_limits<double>::infinity();

    this->cached_image = QImage();
    this->cached_image_world_top_left = QPointF();
    this->cached_geometry_world_origin = QPointF();
    this->cached_geometry_ready = false;
    this->hit_markers.clear();
    this->device_hit_segments.clear();
    this->pipe_hit_segments.clear();
    this->global_device_segment_indices.clear();
    this->global_pipe_segment_indices.clear();
    this->spatial_cells.clear();

    const auto include_point = [&minimum_x, &minimum_y, &maximum_x, &maximum_y](const QPointF &point)
    {
        minimum_x = std::min(minimum_x, point.x());
        minimum_y = std::min(minimum_y, point.y());
        maximum_x = std::max(maximum_x, point.x());
        maximum_y = std::max(maximum_y, point.y());
    };

    projected_nodes.reserve(this->snapshot.nodes.size());
    node_positions.reserve(this->snapshot.nodes.size());
    this->hit_markers.reserve(this->snapshot.nodes.size());
    for (const NetworkRenderNode &node : this->snapshot.nodes)
    {
        if (!isFiniteCoordinate(node.coordinate_wgs84))
            continue;

        const QPointF raw_world_position = GeoWebMercator::lonLatToWorldPixel(
            GeoWebMercator::normalizeLongitude(node.coordinate_wgs84.longitude_deg),
            node.coordinate_wgs84.latitude_deg,
            zoom);
        if (!std::isfinite(anchor_x))
            anchor_x = raw_world_position.x();

        ProjectedNode projected_node;
        projected_node.render_id = node.render_id;
        projected_node.entity_type = node.entity_type;
        projected_node.world_position = QPointF(
            nearestWrappedWorldPixel(raw_world_position.x(), anchor_x, zoom),
            raw_world_position.y());
        projected_nodes.append(projected_node);
        node_positions.insert(projected_node.render_id, projected_node.world_position);

        HitMarker marker;
        marker.render_id = projected_node.render_id;
        marker.entity_type = projected_node.entity_type;
        marker.world_position = projected_node.world_position;
        this->hit_markers.append(marker);
        include_point(projected_node.world_position);
    }

    projected_links.reserve(this->snapshot.links.size());
    for (const NetworkRenderLink &link : this->snapshot.links)
    {
        ProjectedLink projected_link;
        projected_link.render_id = link.render_id;
        projected_link.entity_type = link.entity_type;
        projected_link.world_vertices.reserve(link.vertices_wgs84.size());

        double previous_x = node_positions.value(
            link.start_node_render_id,
            QPointF(anchor_x, 0.0)).x();

        for (const CoordinateWGS84 &coordinate : link.vertices_wgs84)
        {
            if (!isFiniteCoordinate(coordinate))
                continue;

            const QPointF raw_world_position = GeoWebMercator::lonLatToWorldPixel(
                GeoWebMercator::normalizeLongitude(coordinate.longitude_deg),
                coordinate.latitude_deg,
                zoom);
            if (!std::isfinite(previous_x))
                previous_x = raw_world_position.x();

            const QPointF world_position(
                nearestWrappedWorldPixel(raw_world_position.x(), previous_x, zoom),
                raw_world_position.y());
            projected_link.world_vertices.append(world_position);
            include_point(world_position);
            previous_x = world_position.x();
        }

        if (projected_link.world_vertices.size() < 2)
            continue;

        projected_links.append(projected_link);
        QList<HitSegment> *segments = projected_link.entity_type == InfrastructureEntity::Pipe
            ? &this->pipe_hit_segments : &this->device_hit_segments;
        for (qsizetype index = 1; index < projected_link.world_vertices.size(); ++index)
        {
            HitSegment segment;
            segment.render_id = projected_link.render_id;
            segment.entity_type = projected_link.entity_type;
            segment.start = projected_link.world_vertices.at(index - 1);
            segment.end = projected_link.world_vertices.at(index);
            segments->append(segment);
        }

        if ((projected_link.entity_type == InfrastructureEntity::Pump ||
             projected_link.entity_type == InfrastructureEntity::Valve) &&
            projected_link.world_vertices.size() >= 3)
        {
            HitMarker marker;
            marker.render_id = projected_link.render_id;
            marker.entity_type = projected_link.entity_type;
            marker.world_position = projected_link.world_vertices.at(projected_link.world_vertices.size() / 2);
            this->hit_markers.append(marker);
        }
    }

    this->cached_zoom = zoom;
    this->cached_device_pixel_ratio = device_pixel_ratio;
    this->cache_initialized = true;

    if ((projected_nodes.isEmpty() && projected_links.isEmpty()) ||
        !std::isfinite(minimum_x) || !std::isfinite(minimum_y) ||
        !std::isfinite(maximum_x) || !std::isfinite(maximum_y))
    {
        return;
    }

    const QPointF geometry_origin(
        (minimum_x + maximum_x) / 2.0,
        (minimum_y + maximum_y) / 2.0);
    const QPointF image_world_top_left(
        minimum_x - NetworkImagePadding,
        minimum_y - NetworkImagePadding);
    const QSize logical_image_size(
        qMax(1, int(std::ceil(maximum_x - minimum_x + NetworkImagePadding * 2.0))),
        qMax(1, int(std::ceil(maximum_y - minimum_y + NetworkImagePadding * 2.0))));
    const QSize physical_image_size(
        qMax(1, qCeil(logical_image_size.width() * device_pixel_ratio)),
        qMax(1, qCeil(logical_image_size.height() * device_pixel_ratio)));

    QImage image(physical_image_size, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(device_pixel_ratio);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath link_path;
    for (const ProjectedLink &link : projected_links)
    {
        link_path.moveTo(link.world_vertices.first() - image_world_top_left);
        for (qsizetype index = 1; index < link.world_vertices.size(); ++index)
            link_path.lineTo(link.world_vertices.at(index) - image_world_top_left);
    }

    QPen link_pen(NetworkColor, NetworkLinkWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(link_pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(link_path);

    painter.setPen(Qt::NoPen);
    painter.setBrush(NetworkColor);
    for (const ProjectedNode &node : projected_nodes)
    {
        const QPointF local_position = node.world_position - image_world_top_left;
        painter.drawEllipse(local_position, NetworkNodeWidth / 2.0, NetworkNodeWidth / 2.0);
    }

    painter.end();

    this->cached_image = image;
    this->cached_image_world_top_left = image_world_top_left;
    this->cached_geometry_world_origin = geometry_origin;
    this->cached_geometry_ready = true;
    rebuildSpatialIndex();
}

void MapNetworkOverlayWidget::rebuildSpatialIndex()
{
    this->spatial_cells.clear();
    this->global_device_segment_indices.clear();
    this->global_pipe_segment_indices.clear();

    for (int index = 0; index < this->hit_markers.size(); ++index)
    {
        const HitMarker &marker = this->hit_markers.at(index);
        const quint64 key = spatialCellKey(
            spatialCellCoordinate(marker.world_position.x()),
            spatialCellCoordinate(marker.world_position.y()));
        this->spatial_cells[key].marker_indices.append(index);
    }

    const auto add_segments = [this](const QList<HitSegment> &segments, HitCollection collection)
    {
        for (int index = 0; index < segments.size(); ++index)
        {
            const HitSegment &segment = segments.at(index);
            const int minimum_cell_x = spatialCellCoordinate(std::min(segment.start.x(), segment.end.x()));
            const int maximum_cell_x = spatialCellCoordinate(std::max(segment.start.x(), segment.end.x()));
            const int minimum_cell_y = spatialCellCoordinate(std::min(segment.start.y(), segment.end.y()));
            const int maximum_cell_y = spatialCellCoordinate(std::max(segment.start.y(), segment.end.y()));
            const qint64 cell_count = qint64(maximum_cell_x - minimum_cell_x + 1) *
                qint64(maximum_cell_y - minimum_cell_y + 1);

            if (cell_count > 4096)
            {
                if (collection == HitCollection::DeviceSegments)
                    this->global_device_segment_indices.append(index);
                else
                    this->global_pipe_segment_indices.append(index);
                continue;
            }

            for (int cell_y = minimum_cell_y; cell_y <= maximum_cell_y; ++cell_y)
            {
                for (int cell_x = minimum_cell_x; cell_x <= maximum_cell_x; ++cell_x)
                {
                    SpatialCell &cell = this->spatial_cells[spatialCellKey(cell_x, cell_y)];
                    if (collection == HitCollection::DeviceSegments)
                        cell.device_segment_indices.append(index);
                    else
                        cell.pipe_segment_indices.append(index);
                }
            }
        }
    };

    add_segments(this->device_hit_segments, HitCollection::DeviceSegments);
    add_segments(this->pipe_hit_segments, HitCollection::PipeSegments);
}

QList<int> MapNetworkOverlayWidget::candidateIndices(
    qreal point_x, qreal point_y, qreal radius, HitCollection collection) const
{
    const int minimum_cell_x = spatialCellCoordinate(point_x - radius);
    const int maximum_cell_x = spatialCellCoordinate(point_x + radius);
    const int minimum_cell_y = spatialCellCoordinate(point_y - radius);
    const int maximum_cell_y = spatialCellCoordinate(point_y + radius);
    const qint64 cell_count = qint64(maximum_cell_x - minimum_cell_x + 1) *
        qint64(maximum_cell_y - minimum_cell_y + 1);
    QSet<int> result;

    if (cell_count > 256)
    {
        int collection_size = 0;
        if (collection == HitCollection::Markers)
            collection_size = this->hit_markers.size();
        else if (collection == HitCollection::DeviceSegments)
            collection_size = this->device_hit_segments.size();
        else
            collection_size = this->pipe_hit_segments.size();

        for (int index = 0; index < collection_size; ++index)
            result.insert(index);
        return result.values();
    }

    for (int cell_y = minimum_cell_y; cell_y <= maximum_cell_y; ++cell_y)
    {
        for (int cell_x = minimum_cell_x; cell_x <= maximum_cell_x; ++cell_x)
        {
            const QHash<quint64, SpatialCell>::const_iterator cell_iterator =
                this->spatial_cells.constFind(spatialCellKey(cell_x, cell_y));
            if (cell_iterator == this->spatial_cells.constEnd())
                continue;

            const QList<int> *indices = nullptr;
            if (collection == HitCollection::Markers)
                indices = &cell_iterator->marker_indices;
            else if (collection == HitCollection::DeviceSegments)
                indices = &cell_iterator->device_segment_indices;
            else
                indices = &cell_iterator->pipe_segment_indices;

            for (int index : *indices)
                result.insert(index);
        }
    }

    if (collection == HitCollection::DeviceSegments)
    {
        for (int index : this->global_device_segment_indices)
            result.insert(index);
    }
    else if (collection == HitCollection::PipeSegments)
    {
        for (int index : this->global_pipe_segment_indices)
            result.insert(index);
    }

    return result.values();
}

NetworkOverlayHit MapNetworkOverlayWidget::nearestMarkerHit(
    qreal point_x, qreal point_y, qreal marker_half_width) const
{
    NetworkOverlayHit best_hit;
    qreal best_distance_squared = std::numeric_limits<qreal>::infinity();
    const QList<int> candidates = candidateIndices(
        point_x, point_y, marker_half_width, HitCollection::Markers);

    for (int index : candidates)
    {
        const HitMarker &marker = this->hit_markers.at(index);
        const qreal delta_x = point_x - marker.world_position.x();
        const qreal delta_y = point_y - marker.world_position.y();
        if (qAbs(delta_x) > marker_half_width || qAbs(delta_y) > marker_half_width)
            continue;

        const qreal distance_squared = delta_x * delta_x + delta_y * delta_y;
        if (distance_squared >= best_distance_squared)
            continue;

        best_distance_squared = distance_squared;
        best_hit.render_id = marker.render_id;
        best_hit.entity_type = marker.entity_type;
    }

    return best_hit;
}

NetworkOverlayHit MapNetworkOverlayWidget::nearestSegmentHit(
    qreal point_x, qreal point_y, HitCollection collection) const
{
    NetworkOverlayHit best_hit;
    const QList<HitSegment> &segments = collection == HitCollection::DeviceSegments
        ? this->device_hit_segments : this->pipe_hit_segments;
    qreal best_distance_squared = LinkHitDistance * LinkHitDistance;
    const QList<int> candidates = candidateIndices(point_x, point_y, LinkHitDistance, collection);

    for (int index : candidates)
    {
        const HitSegment &segment = segments.at(index);
        const qreal distance_squared = pointToSegmentDistanceSquared(
            point_x, point_y, segment.start, segment.end);
        if (distance_squared > best_distance_squared)
            continue;

        best_distance_squared = distance_squared;
        best_hit.render_id = segment.render_id;
        best_hit.entity_type = segment.entity_type;
    }

    return best_hit;
}

QPointF MapNetworkOverlayWidget::cachedImageScreenPosition() const
{
    const QPointF center_world = this->map_model->centerTile() * MapModel::TileSize;
    const double wrapped_origin_x = nearestWrappedWorldPixel(
        this->cached_geometry_world_origin.x(), center_world.x(), this->cached_zoom);
    const double wrapped_image_left = wrapped_origin_x +
        this->cached_image_world_top_left.x() - this->cached_geometry_world_origin.x();

    const qreal screen_x = width() / 2.0 + wrapped_image_left - center_world.x();
    const qreal screen_y = height() / 2.0 + this->cached_image_world_top_left.y() - center_world.y();
    return QPointF(
        snapToPhysicalPixel(screen_x, this->cached_device_pixel_ratio),
        snapToPhysicalPixel(screen_y, this->cached_device_pixel_ratio));
}

QPointF MapNetworkOverlayWidget::geometryWorldPosition(const QPointF &screen_position) const
{
    const QPointF center_world = this->map_model->centerTile() * MapModel::TileSize;
    const qreal visible_world_x = center_world.x() + screen_position.x() - width() / 2.0;
    return QPointF(
        nearestWrappedWorldPixel(visible_world_x, this->cached_geometry_world_origin.x(), this->cached_zoom),
        center_world.y() + screen_position.y() - height() / 2.0);
}
