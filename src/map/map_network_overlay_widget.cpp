#include "map_network_overlay_widget.h"

#include "../geo_web_mercator.h"
#include "../hydraulic_data.h"

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QHideEvent>
#include <QMetaObject>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPointer>
#include <QPixmap>
#include <QRunnable>
#include <QSemaphore>
#include <QShowEvent>
#include <QSvgRenderer>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QtMath>

#include <algorithm>
#include <functional>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
constexpr int ReferenceZoom = 18;
constexpr qreal NetworkImagePadding = 8.0;
constexpr qreal NetworkImageOverscanFactor = 3.0;
constexpr int NetworkImageMaximumPhysicalDimension = 4096;
constexpr qint64 NetworkImageMaximumPhysicalArea = 8LL * 1024LL * 1024LL;
constexpr qreal NetworkImageRebuildEdge = 256.0;
constexpr qreal NetworkLinkWidth = 3.0;
constexpr qreal NetworkNodeWidth = 8.0;
constexpr qreal LinkHitDistance = 7.0;
constexpr qreal SpatialCellSize = 128.0;
const QColor NetworkColor(QStringLiteral("#b000ff"));

struct NetworkRenderWorkers
{
    NetworkRenderWorkers()
    {
        this->thread_count = qMax(1, QThread::idealThreadCount());
        this->pool.setMaxThreadCount(this->thread_count);
        this->pool.setExpiryTimeout(-1);
    }

    QThreadPool pool;
    int thread_count = 1;
};

NetworkRenderWorkers &networkRenderWorkers()
{
    static NetworkRenderWorkers workers;
    return workers;
}

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

qreal scaleForZoom(int zoom)
{
    return std::ldexp(1.0, zoom - ReferenceZoom);
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

void appendSvgNumber(QByteArray &target, qreal value)
{
    target.append(QByteArray::number(value, 'f', 2));
}

void appendSvgMove(QByteArray &path, const QPointF &point)
{
    path.append('M');
    appendSvgNumber(path, point.x());
    path.append(' ');
    appendSvgNumber(path, point.y());
}

void appendSvgLine(QByteArray &path, const QPointF &point)
{
    path.append('L');
    appendSvgNumber(path, point.x());
    path.append(' ');
    appendSvgNumber(path, point.y());
}

void appendSvgCircle(QByteArray &path, const QPointF &center, qreal radius)
{
    path.append('M');
    appendSvgNumber(path, center.x() - radius);
    path.append(' ');
    appendSvgNumber(path, center.y());
    path.append('A');
    appendSvgNumber(path, radius);
    path.append(' ');
    appendSvgNumber(path, radius);
    path.append(" 0 1 0 ");
    appendSvgNumber(path, center.x() + radius);
    path.append(' ');
    appendSvgNumber(path, center.y());
    path.append('A');
    appendSvgNumber(path, radius);
    path.append(' ');
    appendSvgNumber(path, radius);
    path.append(" 0 1 0 ");
    appendSvgNumber(path, center.x() - radius);
    path.append(' ');
    appendSvgNumber(path, center.y());
    path.append('Z');
}


QSize boundedCacheLogicalSize(const QSize &viewport_size, qreal device_pixel_ratio)
{
    if (!viewport_size.isValid())
        return QSize();

    const qreal bounded_device_pixel_ratio = qMax<qreal>(1.0, device_pixel_ratio);
    const qreal viewport_physical_area = qMax<qreal>(1.0,
        viewport_size.width() * bounded_device_pixel_ratio *
        viewport_size.height() * bounded_device_pixel_ratio);
    const qreal maximum_factor = std::min({
        NetworkImageOverscanFactor,
        NetworkImageMaximumPhysicalDimension /
            qMax<qreal>(1.0, viewport_size.width() * bounded_device_pixel_ratio),
        NetworkImageMaximumPhysicalDimension /
            qMax<qreal>(1.0, viewport_size.height() * bounded_device_pixel_ratio),
        std::sqrt(NetworkImageMaximumPhysicalArea / viewport_physical_area)
    });
    const qreal factor = qMax<qreal>(1.0, maximum_factor);
    return QSize(
        qMax(1, qFloor(viewport_size.width() * factor)),
        qMax(1, qFloor(viewport_size.height() * factor)));
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
        requestRenderCache();
    });
    connect(this->map_model, &MapModel::zoomChanged, this, [this]
    {
        update();
        requestRenderCache(true);
    });

    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded,
        this, &MapNetworkOverlayWidget::syncSnapshot);
    connect(this->hydraulic_data, &HydraulicData::signalNetworkGeometryChanged,
        this, &MapNetworkOverlayWidget::syncSnapshot);

    QTimer::singleShot(0, this, &MapNetworkOverlayWidget::syncSnapshot);
}

MapNetworkOverlayWidget::~MapNetworkOverlayWidget()
{
    this->rendering_active = false;
    if (this->pending_render_cancelled)
        this->pending_render_cancelled->store(true, std::memory_order_relaxed);
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
    if (!isVisible() || !this->reference_geometry_ready ||
        !std::isfinite(screen_position.x()) || !std::isfinite(screen_position.y()) ||
        screen_position.x() < 0.0 || screen_position.y() < 0.0 ||
        screen_position.x() > width() || screen_position.y() > height())
    {
        return no_hit;
    }

    const qreal scale = referenceScaleForCurrentZoom();
    if (scale <= 0.0)
        return no_hit;

    const QPointF world_position = geometryWorldPosition(screen_position);
    const NetworkOverlayHit marker_hit = nearestMarkerHit(
        world_position.x(), world_position.y(), markerWidthForZoom(this->map_model->zoom()) / (2.0 * scale));
    if (marker_hit.isValid())
        return marker_hit;

    const qreal link_hit_distance = LinkHitDistance / scale;
    const NetworkOverlayHit device_hit = nearestSegmentHit(
        world_position.x(), world_position.y(), link_hit_distance, HitCollection::DeviceSegments);
    if (device_hit.isValid())
        return device_hit;

    return nearestSegmentHit(
        world_position.x(), world_position.y(), link_hit_distance, HitCollection::PipeSegments);
}

void MapNetworkOverlayWidget::hideEvent(QHideEvent *event)
{
    this->rendering_active = false;
    this->render_restart_requested = false;
    this->render_restart_force = false;
    if (this->pending_render_cancelled)
        this->pending_render_cancelled->store(true, std::memory_order_relaxed);

    QWidget::hideEvent(event);
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

    if (this->reference_geometry_ready)
    {
        requestRenderCache();
        paintNetwork(painter);
    }

    static const QPixmap crosshair_pixmap = QPixmap(QStringLiteral(":/icon/crosshair.png")).scaled(
        QSize(40, 40), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!crosshair_pixmap.isNull())
    {
        const QPoint crosshair_position(
            (width() - crosshair_pixmap.width()) / 2,
            (height() - crosshair_pixmap.height()) / 2);
        painter.drawPixmap(crosshair_position, crosshair_pixmap);
    }
}

void MapNetworkOverlayWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    this->rendering_active = true;
    requestRenderCache();
}

void MapNetworkOverlayWidget::syncSnapshot()
{
    const quint64 current_geometry_revision = this->hydraulic_data->geometryRevision();
    if (this->snapshot_initialized && this->geometry_revision == current_geometry_revision)
        return;

    this->snapshot = this->hydraulic_data->networkRenderSnapshot();
    this->geometry_revision = this->snapshot.geometry_revision;
    this->snapshot_initialized = true;
    rebuildReferenceGeometry();
    clearRenderedCache();
    requestRenderCache(true);
    update();
}

void MapNetworkOverlayWidget::rebuildReferenceGeometry()
{
    std::shared_ptr<RenderGeometry> geometry = std::make_shared<RenderGeometry>();
    QHash<quint32, QPointF> node_positions;
    double anchor_x = std::numeric_limits<double>::quiet_NaN();
    double minimum_x = std::numeric_limits<double>::infinity();
    double minimum_y = std::numeric_limits<double>::infinity();
    double maximum_x = -std::numeric_limits<double>::infinity();
    double maximum_y = -std::numeric_limits<double>::infinity();

    this->reference_geometry_ready = false;
    this->render_geometry.reset();
    this->hit_markers.clear();
    this->device_hit_segments.clear();
    this->pipe_hit_segments.clear();
    this->global_device_segment_indices.clear();
    this->global_pipe_segment_indices.clear();
    this->spatial_cells.clear();

    geometry->geometry_revision = this->geometry_revision;
    geometry->node_positions.reserve(this->snapshot.nodes.size());
    node_positions.reserve(this->snapshot.nodes.size());
    this->hit_markers.reserve(this->snapshot.nodes.size());

    for (const NetworkRenderNode &node : this->snapshot.nodes)
    {
        if (!isFiniteCoordinate(node.coordinate_wgs84))
            continue;

        const QPointF raw_world_position = GeoWebMercator::lonLatToWorldPixel(
            GeoWebMercator::normalizeLongitude(node.coordinate_wgs84.longitude_deg),
            node.coordinate_wgs84.latitude_deg,
            ReferenceZoom);
        if (!std::isfinite(anchor_x))
            anchor_x = raw_world_position.x();

        const QPointF world_position(
            nearestWrappedWorldPixel(raw_world_position.x(), anchor_x, ReferenceZoom),
            raw_world_position.y());
        geometry->node_positions.append(world_position);
        node_positions.insert(node.render_id, world_position);

        HitMarker marker;
        marker.render_id = node.render_id;
        marker.entity_type = node.entity_type;
        marker.world_position = world_position;
        this->hit_markers.append(marker);

        minimum_x = std::min(minimum_x, world_position.x());
        minimum_y = std::min(minimum_y, world_position.y());
        maximum_x = std::max(maximum_x, world_position.x());
        maximum_y = std::max(maximum_y, world_position.y());
    }

    for (const NetworkRenderLink &link : this->snapshot.links)
    {
        QList<QPointF> world_vertices;
        world_vertices.reserve(link.vertices_wgs84.size());
        const QPointF start_node_position = node_positions.value(
            link.start_node_render_id,
            QPointF(anchor_x, 0.0));
        double previous_x = start_node_position.x();

        for (const CoordinateWGS84 &coordinate : link.vertices_wgs84)
        {
            if (!isFiniteCoordinate(coordinate))
                continue;

            const QPointF raw_world_position = GeoWebMercator::lonLatToWorldPixel(
                GeoWebMercator::normalizeLongitude(coordinate.longitude_deg),
                coordinate.latitude_deg,
                ReferenceZoom);
            if (!std::isfinite(previous_x))
            {
                previous_x = raw_world_position.x();
                if (!std::isfinite(anchor_x))
                    anchor_x = previous_x;
            }

            const QPointF world_position(
                nearestWrappedWorldPixel(raw_world_position.x(), previous_x, ReferenceZoom),
                raw_world_position.y());
            world_vertices.append(world_position);
            minimum_x = std::min(minimum_x, world_position.x());
            minimum_y = std::min(minimum_y, world_position.y());
            maximum_x = std::max(maximum_x, world_position.x());
            maximum_y = std::max(maximum_y, world_position.y());
            previous_x = world_position.x();
        }

        if (world_vertices.size() < 2)
            continue;

        QList<HitSegment> *hit_segments = link.entity_type == InfrastructureEntity::Pipe
            ? &this->pipe_hit_segments : &this->device_hit_segments;
        for (qsizetype index = 1; index < world_vertices.size(); ++index)
        {
            const QPointF &start = world_vertices.at(index - 1);
            const QPointF &end = world_vertices.at(index);
            geometry->link_segments.append(QLineF(start, end));

            HitSegment segment;
            segment.render_id = link.render_id;
            segment.entity_type = link.entity_type;
            segment.start = start;
            segment.end = end;
            hit_segments->append(segment);
        }

        if (link.entity_type == InfrastructureEntity::Pump ||
            link.entity_type == InfrastructureEntity::Valve)
        {
            HitMarker marker;
            marker.render_id = link.render_id;
            marker.entity_type = link.entity_type;
            marker.world_position = world_vertices.at(world_vertices.size() / 2);
            this->hit_markers.append(marker);
        }
    }

    if ((geometry->node_positions.isEmpty() && geometry->link_segments.isEmpty()) ||
        !std::isfinite(minimum_x) || !std::isfinite(minimum_y) ||
        !std::isfinite(maximum_x) || !std::isfinite(maximum_y))
    {
        return;
    }

    geometry->world_bounds = QRectF(
        minimum_x,
        minimum_y,
        maximum_x - minimum_x,
        maximum_y - minimum_y);
    geometry->world_origin = geometry->world_bounds.center();
    this->render_geometry = geometry;
    this->reference_geometry_ready = true;
    rebuildSpatialIndex();
}

void MapNetworkOverlayWidget::clearRenderedCache()
{
    if (this->pending_render_cancelled)
        this->pending_render_cancelled->store(true, std::memory_order_relaxed);
    this->pending_render_request_id = 0;
    this->pending_cache_coverage_world_bounds = QRectF();
    this->pending_cache_zoom = -1;
    this->pending_cache_device_pixel_ratio = 0.0;
    this->render_restart_requested = this->render_worker_running;
    this->render_restart_force = this->render_worker_running;
    this->rendered_network_cache = QImage();
    this->rendered_cache_coverage_world_bounds = QRectF();
    this->rendered_cache_image_world_bounds = QRectF();
    this->rendered_cache_zoom = -1;
    this->rendered_cache_device_pixel_ratio = 0.0;
}

void MapNetworkOverlayWidget::requestRenderCache(bool force)
{
    if (!this->rendering_active || !isVisible() ||
        !this->reference_geometry_ready || !this->render_geometry || width() <= 0 || height() <= 0)
    {
        return;
    }

    if (!force && renderedCacheCoversCurrentView())
        return;
    if (!force && pendingCacheCoversCurrentView())
        return;

    if (this->render_worker_running)
    {
        if (this->pending_render_cancelled)
            this->pending_render_cancelled->store(true, std::memory_order_relaxed);
        this->render_restart_requested = true;
        this->render_restart_force = this->render_restart_force || force;
        return;
    }

    const quint64 request_id = ++this->next_render_request_id;
    RenderRequest request = createRenderRequest(request_id);
    if (!request.geometry || !request.logical_size.isValid() || request.coverage_world_bounds.isEmpty())
        return;

    request.cancelled = std::make_shared<std::atomic_bool>(false);
    this->pending_render_cancelled = request.cancelled;
    this->pending_render_request_id = request_id;
    this->active_render_request_id = request_id;
    this->render_worker_running = true;
    this->render_restart_requested = false;
    this->render_restart_force = false;
    this->pending_cache_coverage_world_bounds = request.coverage_world_bounds;
    this->pending_cache_zoom = request.zoom;
    this->pending_cache_device_pixel_ratio = request.device_pixel_ratio;

    const QPointer<MapNetworkOverlayWidget> widget(this);
    QRunnable *runnable = QRunnable::create([widget, request]
    {
        RenderResult result = MapNetworkOverlayWidget::renderRequest(request);
        QCoreApplication *application = QCoreApplication::instance();
        if (!application)
            return;

        QMetaObject::invokeMethod(application, [widget, result = std::move(result)]() mutable
        {
            if (!widget)
                return;
            widget->applyRenderResult(std::move(result));
        }, Qt::QueuedConnection);
    });
    QThreadPool::globalInstance()->start(runnable);
}

MapNetworkOverlayWidget::RenderRequest MapNetworkOverlayWidget::createRenderRequest(quint64 request_id) const
{
    RenderRequest request;
    if (!this->render_geometry || !this->reference_geometry_ready)
        return request;

    request.request_id = request_id;
    request.geometry_revision = this->geometry_revision;
    request.zoom = this->map_model->zoom();
    request.device_pixel_ratio = qMax<qreal>(1.0, devicePixelRatioF());
    request.geometry = this->render_geometry;

    const QSize cache_size = boundedCacheLogicalSize(size(), request.device_pixel_ratio);
    if (!cache_size.isValid())
        return RenderRequest();

    const qreal scale = scaleForZoom(request.zoom);
    if (scale <= 0.0)
        return RenderRequest();

    const QPointF center = visibleReferenceWorldCenter();
    request.coverage_world_bounds = QRectF(
        center.x() - cache_size.width() / (2.0 * scale),
        center.y() - cache_size.height() / (2.0 * scale),
        cache_size.width() / scale,
        cache_size.height() / scale);

    const qreal geometry_padding = (NetworkNodeWidth / 2.0 + NetworkImagePadding) / scale;
    const QRectF padded_geometry_bounds = this->render_geometry->world_bounds.adjusted(
        -geometry_padding, -geometry_padding, geometry_padding, geometry_padding);
    const QRectF visible_geometry_bounds = request.coverage_world_bounds.intersected(padded_geometry_bounds);

    if (visible_geometry_bounds.isEmpty())
    {
        request.logical_size = QSize(1, 1);
        request.image_world_bounds = QRectF(
            request.coverage_world_bounds.topLeft(),
            QSizeF(1.0 / scale, 1.0 / scale));
        return request;
    }

    const int logical_width = qMin(cache_size.width(),
        qMax(1, qCeil(visible_geometry_bounds.width() * scale)));
    const int logical_height = qMin(cache_size.height(),
        qMax(1, qCeil(visible_geometry_bounds.height() * scale)));
    request.logical_size = QSize(logical_width, logical_height);
    request.image_world_bounds = QRectF(
        visible_geometry_bounds.topLeft(),
        QSizeF(logical_width / scale, logical_height / scale));
    return request;
}

MapNetworkOverlayWidget::RenderResult MapNetworkOverlayWidget::renderRequest(const RenderRequest &request)
{
    RenderResult result;
    result.request_id = request.request_id;
    result.geometry_revision = request.geometry_revision;
    result.zoom = request.zoom;
    result.device_pixel_ratio = request.device_pixel_ratio;
    result.coverage_world_bounds = request.coverage_world_bounds;
    result.image_world_bounds = request.image_world_bounds;

    if (!request.geometry || !request.logical_size.isValid() || request.image_world_bounds.isEmpty())
        return result;
    if (request.cancelled && request.cancelled->load(std::memory_order_relaxed))
        return result;

    const qreal scale = scaleForZoom(request.zoom);
    const qreal image_left = request.image_world_bounds.left();
    const qreal image_top = request.image_world_bounds.top();
    const QSize physical_size(
        qMax(1, qCeil(request.logical_size.width() * request.device_pixel_ratio)),
        qMax(1, qCeil(request.logical_size.height() * request.device_pixel_ratio)));
    if (physical_size.isEmpty())
        return result;

    struct StripeDefinition
    {
        int physical_top = 0;
        int physical_height = 0;
        qreal logical_top = 0.0;
        qreal logical_height = 0.0;
        std::vector<int> segment_indices;
        std::vector<int> node_indices;
    };

    NetworkRenderWorkers &workers = networkRenderWorkers();
    const int stripe_count = qMin(workers.thread_count, physical_size.height());
    std::vector<StripeDefinition> stripes(stripe_count);
    for (int stripe_index = 0; stripe_index < stripe_count; ++stripe_index)
    {
        StripeDefinition &stripe = stripes[stripe_index];
        const int physical_bottom = (stripe_index + 1) * physical_size.height() / stripe_count;
        stripe.physical_top = stripe_index * physical_size.height() / stripe_count;
        stripe.physical_height = physical_bottom - stripe.physical_top;
        stripe.logical_top = stripe.physical_top / request.device_pixel_ratio;
        stripe.logical_height = stripe.physical_height / request.device_pixel_ratio;
    }

    const qreal logical_width = request.logical_size.width();
    const qreal logical_height = request.logical_size.height();
    const qreal link_padding = NetworkLinkWidth / 2.0 + NetworkImagePadding;
    const qreal node_padding = NetworkNodeWidth / 2.0 + NetworkImagePadding;

    const std::function<int(qreal)> stripe_for_logical_y =
        [stripe_count, logical_height](qreal logical_y)
    {
        if (logical_height <= 0.0)
            return 0;
        const int stripe_index = qFloor(logical_y * stripe_count / logical_height);
        return qBound(0, stripe_index, stripe_count - 1);
    };

    for (int segment_index = 0; segment_index < request.geometry->link_segments.size(); ++segment_index)
    {
        if ((segment_index & 1023) == 0 && request.cancelled &&
            request.cancelled->load(std::memory_order_relaxed))
        {
            return result;
        }

        const QLineF &segment = request.geometry->link_segments.at(segment_index);
        const qreal x1 = (segment.x1() - image_left) * scale;
        const qreal y1 = (segment.y1() - image_top) * scale;
        const qreal x2 = (segment.x2() - image_left) * scale;
        const qreal y2 = (segment.y2() - image_top) * scale;
        const qreal minimum_x = std::min(x1, x2) - link_padding;
        const qreal maximum_x = std::max(x1, x2) + link_padding;
        const qreal minimum_y = std::min(y1, y2) - link_padding;
        const qreal maximum_y = std::max(y1, y2) + link_padding;
        if (maximum_x < 0.0 || minimum_x > logical_width ||
            maximum_y < 0.0 || minimum_y > logical_height)
        {
            continue;
        }

        const int first_stripe = stripe_for_logical_y(minimum_y);
        const int last_stripe = stripe_for_logical_y(maximum_y);
        for (int stripe_index = first_stripe; stripe_index <= last_stripe; ++stripe_index)
            stripes[stripe_index].segment_indices.push_back(segment_index);
    }

    for (int node_index = 0; node_index < request.geometry->node_positions.size(); ++node_index)
    {
        if ((node_index & 1023) == 0 && request.cancelled &&
            request.cancelled->load(std::memory_order_relaxed))
        {
            return result;
        }

        const QPointF &world_position = request.geometry->node_positions.at(node_index);
        const qreal x = (world_position.x() - image_left) * scale;
        const qreal y = (world_position.y() - image_top) * scale;
        if (x + node_padding < 0.0 || x - node_padding > logical_width ||
            y + node_padding < 0.0 || y - node_padding > logical_height)
        {
            continue;
        }

        const int first_stripe = stripe_for_logical_y(y - node_padding);
        const int last_stripe = stripe_for_logical_y(y + node_padding);
        for (int stripe_index = first_stripe; stripe_index <= last_stripe; ++stripe_index)
            stripes[stripe_index].node_indices.push_back(node_index);
    }

    if (request.cancelled && request.cancelled->load(std::memory_order_relaxed))
        return result;

    std::vector<QImage> stripe_images(stripe_count);
    QSemaphore completed_stripes;
    const std::function<QImage(const StripeDefinition &)> render_stripe =
        [&request, scale, image_left, image_top, logical_width](const StripeDefinition &stripe) -> QImage
    {
        if (request.cancelled && request.cancelled->load(std::memory_order_relaxed))
            return QImage();

        QByteArray link_path;
        QByteArray node_path;
        link_path.reserve(qsizetype(stripe.segment_indices.size()) * 40);
        node_path.reserve(qsizetype(stripe.node_indices.size()) * 50);

        int processed_segments = 0;
        for (int segment_index : stripe.segment_indices)
        {
            if ((++processed_segments & 255) == 0 && request.cancelled &&
                request.cancelled->load(std::memory_order_relaxed))
            {
                return QImage();
            }

            const QLineF &segment = request.geometry->link_segments.at(segment_index);
            appendSvgMove(link_path, QPointF(
                (segment.x1() - image_left) * scale,
                (segment.y1() - image_top) * scale - stripe.logical_top));
            appendSvgLine(link_path, QPointF(
                (segment.x2() - image_left) * scale,
                (segment.y2() - image_top) * scale - stripe.logical_top));
        }

        const qreal node_radius = NetworkNodeWidth / 2.0;
        int processed_nodes = 0;
        for (int node_index : stripe.node_indices)
        {
            if ((++processed_nodes & 255) == 0 && request.cancelled &&
                request.cancelled->load(std::memory_order_relaxed))
            {
                return QImage();
            }

            const QPointF &world_position = request.geometry->node_positions.at(node_index);
            appendSvgCircle(node_path, QPointF(
                (world_position.x() - image_left) * scale,
                (world_position.y() - image_top) * scale - stripe.logical_top), node_radius);
        }

        if (request.cancelled && request.cancelled->load(std::memory_order_relaxed))
            return QImage();

        QByteArray svg;
        svg.reserve(link_path.size() + node_path.size() + 512);
        svg.append("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"");
        appendSvgNumber(svg, logical_width);
        svg.append("\" height=\"");
        appendSvgNumber(svg, stripe.logical_height);
        svg.append("\" viewBox=\"0 0 ");
        appendSvgNumber(svg, logical_width);
        svg.append(' ');
        appendSvgNumber(svg, stripe.logical_height);
        svg.append("\">");
        if (!link_path.isEmpty())
        {
            svg.append("<path fill=\"none\" stroke=\"");
            svg.append(NetworkColor.name().toUtf8());
            svg.append("\" stroke-width=\"");
            appendSvgNumber(svg, NetworkLinkWidth);
            svg.append("\" stroke-linecap=\"round\" stroke-linejoin=\"round\" d=\"");
            svg.append(link_path);
            svg.append("\"/>");
        }
        if (!node_path.isEmpty())
        {
            svg.append("<path fill=\"");
            svg.append(NetworkColor.name().toUtf8());
            svg.append("\" stroke=\"none\" d=\"");
            svg.append(node_path);
            svg.append("\"/>");
        }
        svg.append("</svg>");

        QSvgRenderer renderer;
        if (!renderer.load(svg) || !renderer.isValid())
            return QImage();
        if (request.cancelled && request.cancelled->load(std::memory_order_relaxed))
            return QImage();

        QImage stripe_image(
            QSize(qMax(1, qCeil(logical_width * request.device_pixel_ratio)), stripe.physical_height),
            QImage::Format_ARGB32_Premultiplied);
        if (stripe_image.isNull())
            return QImage();
        stripe_image.setDevicePixelRatio(request.device_pixel_ratio);
        stripe_image.fill(Qt::transparent);

        QPainter painter(&stripe_image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderer.render(&painter, QRectF(0.0, 0.0, logical_width, stripe.logical_height));
        painter.end();
        return stripe_image;
    };

    for (int stripe_index = 0; stripe_index < stripe_count; ++stripe_index)
    {
        QRunnable *runnable = QRunnable::create([&stripes, &stripe_images, &completed_stripes,
            &render_stripe, stripe_index]
        {
            stripe_images[stripe_index] = render_stripe(stripes[stripe_index]);
            completed_stripes.release();
        });
        workers.pool.start(runnable);
    }

    for (int stripe_index = 0; stripe_index < stripe_count; ++stripe_index)
        completed_stripes.acquire();

    if (request.cancelled && request.cancelled->load(std::memory_order_relaxed))
        return result;

    QImage image(physical_size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return result;
    image.setDevicePixelRatio(request.device_pixel_ratio);
    image.fill(Qt::transparent);

    QPainter composition_painter(&image);
    composition_painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    for (int stripe_index = 0; stripe_index < stripe_count; ++stripe_index)
    {
        const QImage &stripe_image = stripe_images[stripe_index];
        if (stripe_image.isNull())
            continue;
        composition_painter.drawImage(
            QPointF(0.0, stripes[stripe_index].logical_top), stripe_image);
    }
    composition_painter.end();

    result.image = std::move(image);
    return result;
}

void MapNetworkOverlayWidget::applyRenderResult(RenderResult result)
{
    if (result.request_id != this->active_render_request_id)
        return;

    this->render_worker_running = false;
    this->active_render_request_id = 0;
    this->pending_render_request_id = 0;
    this->pending_render_cancelled.reset();
    this->pending_cache_coverage_world_bounds = QRectF();
    this->pending_cache_zoom = -1;
    this->pending_cache_device_pixel_ratio = 0.0;

    const bool restart_requested = this->render_restart_requested;
    const bool restart_force = this->render_restart_force;
    this->render_restart_requested = false;
    this->render_restart_force = false;

    const bool result_matches_current_view =
        result.geometry_revision == this->geometry_revision &&
        result.zoom == this->map_model->zoom() &&
        qFuzzyCompare(result.device_pixel_ratio, qMax<qreal>(1.0, devicePixelRatioF())) &&
        coverageCoversCurrentView(result.coverage_world_bounds, result.zoom);

    if (this->rendering_active && isVisible() && result_matches_current_view && !result.image.isNull())
    {
        this->rendered_network_cache = std::move(result.image);
        this->rendered_cache_coverage_world_bounds = result.coverage_world_bounds;
        this->rendered_cache_image_world_bounds = result.image_world_bounds;
        this->rendered_cache_zoom = result.zoom;
        this->rendered_cache_device_pixel_ratio = result.device_pixel_ratio;
        update();
    }

    if (!this->rendering_active || !isVisible())
        return;

    if (restart_requested)
    {
        QTimer::singleShot(0, this, [this, restart_force]
        {
            requestRenderCache(restart_force);
        });
        return;
    }

    requestRenderCache();
}

bool MapNetworkOverlayWidget::renderedCacheCoversCurrentView() const
{
    if (this->rendered_network_cache.isNull() ||
        this->rendered_cache_zoom != this->map_model->zoom() ||
        !qFuzzyCompare(this->rendered_cache_device_pixel_ratio, qMax<qreal>(1.0, devicePixelRatioF())))
    {
        return false;
    }

    return coverageCoversCurrentView(
        this->rendered_cache_coverage_world_bounds,
        this->rendered_cache_zoom);
}

bool MapNetworkOverlayWidget::pendingCacheCoversCurrentView() const
{
    if (this->pending_render_request_id == 0 ||
        this->pending_cache_zoom != this->map_model->zoom() ||
        !qFuzzyCompare(this->pending_cache_device_pixel_ratio, qMax<qreal>(1.0, devicePixelRatioF())))
    {
        return false;
    }

    return coverageCoversCurrentView(
        this->pending_cache_coverage_world_bounds,
        this->pending_cache_zoom);
}

bool MapNetworkOverlayWidget::coverageCoversCurrentView(
    const QRectF &coverage_world_bounds, int zoom) const
{
    if (coverage_world_bounds.isEmpty() || zoom != this->map_model->zoom())
        return false;

    const qreal scale = scaleForZoom(zoom);
    if (scale <= 0.0)
        return false;

    const QRectF view_bounds = visibleReferenceWorldRect();
    const qreal horizontal_overscan = qMax<qreal>(0.0,
        (coverage_world_bounds.width() - view_bounds.width()) / 2.0);
    const qreal vertical_overscan = qMax<qreal>(0.0,
        (coverage_world_bounds.height() - view_bounds.height()) / 2.0);
    const qreal horizontal_safety = std::min(NetworkImageRebuildEdge / scale, horizontal_overscan / 2.0);
    const qreal vertical_safety = std::min(NetworkImageRebuildEdge / scale, vertical_overscan / 2.0);
    const QRectF required_bounds = view_bounds.adjusted(
        -horizontal_safety,
        -vertical_safety,
        horizontal_safety,
        vertical_safety);
    return coverage_world_bounds.contains(required_bounds);
}

void MapNetworkOverlayWidget::paintNetwork(QPainter &painter)
{
    if (this->rendered_network_cache.isNull() || this->rendered_cache_image_world_bounds.isEmpty())
        return;

    const qreal scale = referenceScaleForCurrentZoom();
    if (scale <= 0.0)
        return;

    const QPointF center = visibleReferenceWorldCenter();
    const QRectF target_rect(
        width() / 2.0 + (this->rendered_cache_image_world_bounds.left() - center.x()) * scale,
        height() / 2.0 + (this->rendered_cache_image_world_bounds.top() - center.y()) * scale,
        this->rendered_cache_image_world_bounds.width() * scale,
        this->rendered_cache_image_world_bounds.height() * scale);

    painter.setRenderHint(QPainter::SmoothPixmapTransform,
        this->rendered_cache_zoom != this->map_model->zoom());
    painter.drawImage(target_rect, this->rendered_network_cache);
}

QPointF MapNetworkOverlayWidget::visibleReferenceWorldCenter() const
{
    if (!this->render_geometry)
        return QPointF();

    const QPointF raw_center = GeoWebMercator::lonLatToWorldPixel(
        GeoWebMercator::normalizeLongitude(this->map_model->centerLon()),
        this->map_model->centerLat(),
        ReferenceZoom);
    return QPointF(
        nearestWrappedWorldPixel(
            raw_center.x(),
            this->render_geometry->world_origin.x(),
            ReferenceZoom),
        raw_center.y());
}

QRectF MapNetworkOverlayWidget::visibleReferenceWorldRect() const
{
    const qreal scale = referenceScaleForCurrentZoom();
    if (scale <= 0.0)
        return QRectF();

    const QPointF center = visibleReferenceWorldCenter();
    return QRectF(
        center.x() - width() / (2.0 * scale),
        center.y() - height() / (2.0 * scale),
        width() / scale,
        height() / scale);
}

qreal MapNetworkOverlayWidget::referenceScaleForCurrentZoom() const
{
    return scaleForZoom(this->map_model->zoom());
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

    const std::function<void(const QList<HitSegment> &, HitCollection)> add_segments =
        [this](const QList<HitSegment> &segments, HitCollection collection)
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
    qreal point_x, qreal point_y, qreal hit_distance, HitCollection collection) const
{
    NetworkOverlayHit best_hit;
    const QList<HitSegment> &segments = collection == HitCollection::DeviceSegments
        ? this->device_hit_segments : this->pipe_hit_segments;
    qreal best_distance_squared = hit_distance * hit_distance;
    const QList<int> candidates = candidateIndices(point_x, point_y, hit_distance, collection);

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

QPointF MapNetworkOverlayWidget::geometryWorldPosition(const QPointF &screen_position) const
{
    const qreal scale = referenceScaleForCurrentZoom();
    const QPointF center = visibleReferenceWorldCenter();
    return QPointF(
        center.x() + (screen_position.x() - width() / 2.0) / scale,
        center.y() + (screen_position.y() - height() / 2.0) / scale);
}
