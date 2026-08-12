#include "map_editor_renderer.h"

#include "map_model.h"
#include "map_render_cache_math.h"
#include "map_retained_vector_renderer.h"
#include "map_vector_document.h"

#include "../geo_web_mercator.h"
#include "../infrastructure_entity_traits.h"

#include <QBrush>
#include <QColor>
#include <QCoreApplication>
#include <QHash>
#include <QLinearGradient>
#include <QMetaObject>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPalette>
#include <QPen>
#include <QPointer>
#include <QRunnable>
#include <QRegion>
#include <QThreadPool>
#include <QWidget>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

namespace
{
constexpr double marker_dot_radius = 5.0;
constexpr double connection_target_radius = 9.0;
constexpr double pipe_vertex_radius = 4.0;
constexpr qreal static_cache_item_padding = 16.0;

bool isFiniteCoordinate(const CoordinateWGS84 &coordinate)
{
    return std::isfinite(coordinate.longitude_deg) && std::isfinite(coordinate.latitude_deg);
}

bool isSimulationErrorEntity(const MapEditorVisualState &visual_state, InfrastructureEntity entity_type, const QUuid &uuid)
{
    return visual_state.simulation_error_entities.value(uuid, InfrastructureEntity::Unknown) == entity_type;
}

bool isSimulationDiagnosticEntityStale(const MapEditorVisualState &visual_state, const QUuid &uuid)
{
    return visual_state.simulation_stale_diagnostic_entity_uuids.contains(uuid);
}

}

MapEditorRenderer::MapEditorRenderer(MapModel *map_model, QWidget *canvas)
    : QObject(nullptr), map_model(map_model), canvas(canvas)
{
}

MapEditorRenderer::~MapEditorRenderer()
{
    this->rendering_active = false;
    if (this->geometry_build_cancelled)
        this->geometry_build_cancelled->store(true, std::memory_order_relaxed);
    if (this->pending_render_cancelled)
        this->pending_render_cancelled->store(true, std::memory_order_relaxed);
}

void MapEditorRenderer::setRenderingActive(bool active)
{
    if (this->rendering_active == active)
        return;

    this->rendering_active = active;
    if (!active)
    {
        if (this->pending_render_cancelled)
            this->pending_render_cancelled->store(true, std::memory_order_relaxed);
        this->render_restart_requested = false;
        this->render_restart_force = false;
        return;
    }

    requestStaticCache(this->current_entity_width);
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

    this->current_entity_width = qMax(1, visual_state.entity_width);
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

QPointF MapEditorRenderer::screenFromReferenceWorld(const QPointF &world_position) const
{
    if (!this->canvas || !this->static_geometry)
        return QPointF();

    const qreal scale = referenceScaleForCurrentZoom();
    if (scale <= 0.0)
        return QPointF();

    const QPointF center = visibleReferenceWorldCenter();
    return QPointF(
        this->canvas->width() / 2.0 + (world_position.x() - center.x()) * scale,
        this->canvas->height() / 2.0 + (world_position.y() - center.y()) * scale);
}

QPointF MapEditorRenderer::visibleReferenceWorldCenter() const
{
    if (!this->map_model || !this->static_geometry)
        return QPointF();

    const QPointF raw_center = GeoWebMercator::lonLatToWorldPixel(
        GeoWebMercator::normalizeLongitude(this->map_model->centerLon()),
        this->map_model->centerLat(), MapRenderCacheMath::ReferenceZoom);
    return QPointF(
        GeoWebMercator::nearestWrappedWorldPixelX(
            raw_center.x(), this->static_geometry->world_origin.x(),
            MapRenderCacheMath::ReferenceZoom),
        raw_center.y());
}

QRectF MapEditorRenderer::visibleReferenceWorldRect() const
{
    if (!this->canvas || !this->static_geometry)
        return QRectF();

    const qreal scale = referenceScaleForCurrentZoom();
    if (scale <= 0.0)
        return QRectF();

    return MapRenderCacheMath::centeredWorldRect(
        visibleReferenceWorldCenter(), this->canvas->size(), scale);
}

qreal MapEditorRenderer::referenceScaleForCurrentZoom() const
{
    if (!this->map_model)
        return 0.0;
    return GeoWebMercator::zoomScale(
        this->map_model->zoom(), MapRenderCacheMath::ReferenceZoom);
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

void MapEditorRenderer::syncStaticGeometry(const NetworkRenderSnapshot &network_snapshot)
{
    this->current_geometry_revision = network_snapshot.geometry_revision;
    if (this->static_geometry &&
        this->static_geometry->geometry_revision == network_snapshot.geometry_revision)
    {
        return;
    }

    if (this->pending_geometry_snapshot &&
        this->pending_geometry_snapshot->geometry_revision == network_snapshot.geometry_revision)
    {
        return;
    }

    this->pending_geometry_snapshot = std::make_shared<NetworkRenderSnapshot>(network_snapshot);
    if (this->geometry_build_running)
    {
        if (this->geometry_build_cancelled)
            this->geometry_build_cancelled->store(true, std::memory_order_relaxed);
        this->geometry_build_restart_requested = true;
        return;
    }

    startStaticGeometryBuild();
}

void MapEditorRenderer::startStaticGeometryBuild()
{
    if (!this->pending_geometry_snapshot || this->geometry_build_running)
        return;

    const std::shared_ptr<const NetworkRenderSnapshot> snapshot =
        this->pending_geometry_snapshot;
    this->pending_geometry_snapshot.reset();
    const quint64 request_id = ++this->next_geometry_request_id;
    this->active_geometry_request_id = request_id;
    this->geometry_build_running = true;
    this->geometry_build_restart_requested = false;
    this->geometry_build_cancelled = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancelled = this->geometry_build_cancelled;
    const QPointer<MapEditorRenderer> renderer(this);

    QRunnable *runnable = QRunnable::create([renderer, snapshot, cancelled, request_id]
    {
        std::shared_ptr<StaticGeometry> geometry = buildStaticGeometry(*snapshot, cancelled);
        QCoreApplication *application = QCoreApplication::instance();
        if (!application)
            return;

        QMetaObject::invokeMethod(application,
            [renderer, request_id, geometry = std::move(geometry)]() mutable
        {
            if (!renderer)
                return;
            renderer->applyStaticGeometryBuild(request_id, std::move(geometry));
        }, Qt::QueuedConnection);
    });
    QThreadPool::globalInstance()->start(runnable);
}

std::shared_ptr<MapEditorRenderer::StaticGeometry> MapEditorRenderer::buildStaticGeometry(
    const NetworkRenderSnapshot &network_snapshot,
    const std::shared_ptr<std::atomic_bool> &cancelled)
{
    std::shared_ptr<StaticGeometry> geometry = std::make_shared<StaticGeometry>();
    geometry->geometry_revision = network_snapshot.geometry_revision;
    geometry->nodes.reserve(network_snapshot.nodes.size());
    geometry->links.reserve(network_snapshot.links.size());
    geometry->node_indices_by_uuid.reserve(network_snapshot.nodes.size());
    geometry->link_indices_by_uuid.reserve(network_snapshot.links.size());

    double anchor_x = std::numeric_limits<double>::quiet_NaN();
    double minimum_x = std::numeric_limits<double>::infinity();
    double minimum_y = std::numeric_limits<double>::infinity();
    double maximum_x = -std::numeric_limits<double>::infinity();
    double maximum_y = -std::numeric_limits<double>::infinity();

    int processed_nodes = 0;
    for (const NetworkRenderNode &node : network_snapshot.nodes)
    {
        if ((++processed_nodes & 255) == 0 && cancelled &&
            cancelled->load(std::memory_order_relaxed))
        {
            return std::shared_ptr<StaticGeometry>();
        }
        if (!isFiniteCoordinate(node.coordinate_wgs84))
            continue;

        const QPointF raw_world_position = GeoWebMercator::lonLatToWorldPixel(
            GeoWebMercator::normalizeLongitude(node.coordinate_wgs84.longitude_deg),
            node.coordinate_wgs84.latitude_deg,
            MapRenderCacheMath::ReferenceZoom);
        if (!std::isfinite(anchor_x))
            anchor_x = raw_world_position.x();

        StaticNode static_node;
        static_node.uuid = node.uuid;
        static_node.entity_type = node.entity_type;
        static_node.world_position = QPointF(
            GeoWebMercator::nearestWrappedWorldPixelX(
                raw_world_position.x(), anchor_x,
                MapRenderCacheMath::ReferenceZoom),
            raw_world_position.y());
        geometry->node_indices_by_uuid.insert(static_node.uuid, geometry->nodes.size());
        geometry->nodes.append(static_node);

        minimum_x = std::min(minimum_x, static_node.world_position.x());
        minimum_y = std::min(minimum_y, static_node.world_position.y());
        maximum_x = std::max(maximum_x, static_node.world_position.x());
        maximum_y = std::max(maximum_y, static_node.world_position.y());
    }

    int processed_links = 0;
    for (const NetworkRenderLink &link : network_snapshot.links)
    {
        if ((++processed_links & 127) == 0 && cancelled &&
            cancelled->load(std::memory_order_relaxed))
        {
            return std::shared_ptr<StaticGeometry>();
        }
        if (link.vertices_wgs84.size() < 2)
            continue;

        StaticLink static_link;
        static_link.uuid = link.uuid;
        static_link.entity_type = link.entity_type;
        static_link.world_vertices.reserve(link.vertices_wgs84.size());

        double previous_x = anchor_x;
        for (const CoordinateWGS84 &coordinate : link.vertices_wgs84)
        {
            if (!isFiniteCoordinate(coordinate))
                continue;

            const QPointF raw_world_position = GeoWebMercator::lonLatToWorldPixel(
                GeoWebMercator::normalizeLongitude(coordinate.longitude_deg),
                coordinate.latitude_deg, MapRenderCacheMath::ReferenceZoom);
            if (!std::isfinite(previous_x))
            {
                previous_x = raw_world_position.x();
                if (!std::isfinite(anchor_x))
                    anchor_x = previous_x;
            }

            const QPointF world_position(
                GeoWebMercator::nearestWrappedWorldPixelX(
                    raw_world_position.x(), previous_x,
                    MapRenderCacheMath::ReferenceZoom),
                raw_world_position.y());
            static_link.world_vertices.append(world_position);
            previous_x = world_position.x();

            minimum_x = std::min(minimum_x, world_position.x());
            minimum_y = std::min(minimum_y, world_position.y());
            maximum_x = std::max(maximum_x, world_position.x());
            maximum_y = std::max(maximum_y, world_position.y());
        }

        if (static_link.world_vertices.size() < 2)
            continue;

        if (InfrastructureEntityTraits::isHydraulicDeviceLink(link.entity_type))
        {
            if (link.vertices_wgs84.size() > 2 && static_link.world_vertices.size() > 1)
            {
                static_link.device_center_world_position = static_link.world_vertices.at(1);
            }
            else
            {
                CoordinateWGS84 center;
                const CoordinateWGS84 &start = link.vertices_wgs84.first();
                const CoordinateWGS84 &end = link.vertices_wgs84.last();
                center.latitude_deg = (start.latitude_deg + end.latitude_deg) / 2.0;
                const double longitude_delta = GeoWebMercator::normalizeLongitude(
                    end.longitude_deg - start.longitude_deg);
                center.longitude_deg = GeoWebMercator::normalizeLongitude(
                    start.longitude_deg + longitude_delta / 2.0);
                const QPointF raw_center = GeoWebMercator::lonLatToWorldPixel(
                    center.longitude_deg, center.latitude_deg,
                    MapRenderCacheMath::ReferenceZoom);
                const double center_reference_x =
                    (static_link.world_vertices.first().x() +
                     static_link.world_vertices.last().x()) / 2.0;
                static_link.device_center_world_position = QPointF(
                    GeoWebMercator::nearestWrappedWorldPixelX(
                        raw_center.x(), center_reference_x,
                        MapRenderCacheMath::ReferenceZoom),
                    raw_center.y());
            }
        }

        geometry->link_indices_by_uuid.insert(static_link.uuid, geometry->links.size());
        geometry->links.append(std::move(static_link));
    }

    if (cancelled && cancelled->load(std::memory_order_relaxed))
        return std::shared_ptr<StaticGeometry>();

    if (!std::isfinite(minimum_x) || !std::isfinite(minimum_y) ||
        !std::isfinite(maximum_x) || !std::isfinite(maximum_y))
    {
        geometry->world_bounds = QRectF();
        geometry->world_origin = QPointF();
        return geometry;
    }

    geometry->world_bounds = QRectF(
        minimum_x, minimum_y, maximum_x - minimum_x, maximum_y - minimum_y);
    geometry->world_origin = geometry->world_bounds.center();
    return geometry;
}

void MapEditorRenderer::applyStaticGeometryBuild(
    quint64 request_id, std::shared_ptr<StaticGeometry> geometry)
{
    if (request_id != this->active_geometry_request_id)
        return;

    this->geometry_build_running = false;
    this->active_geometry_request_id = 0;
    this->geometry_build_cancelled.reset();

    const bool restart_requested = this->geometry_build_restart_requested;
    this->geometry_build_restart_requested = false;

    if (geometry && geometry->geometry_revision == this->current_geometry_revision)
    {
        this->static_geometry = std::move(geometry);
        clearStaticRenderedCache();
        requestStaticCache(this->current_entity_width, true);
        if (this->canvas)
            this->canvas->update();
    }

    if (restart_requested || this->pending_geometry_snapshot)
        startStaticGeometryBuild();
}

void MapEditorRenderer::clearStaticRenderedCache()
{
    if (this->pending_render_cancelled)
        this->pending_render_cancelled->store(true, std::memory_order_relaxed);

    this->rendered_static_cache = QImage();
    this->rendered_static_cache_coverage_world_bounds = QRectF();
    this->rendered_static_cache_geometry_revision = 0;
    this->rendered_static_cache_zoom = -1;
    this->rendered_static_cache_entity_width = 10;
    this->rendered_static_cache_device_pixel_ratio = 0.0;
    this->pending_render_request_id = 0;
    this->pending_static_cache_coverage_world_bounds = QRectF();
    this->pending_static_cache_zoom = -1;
    this->pending_static_cache_entity_width = 10;
    this->pending_static_cache_device_pixel_ratio = 0.0;
    this->render_restart_requested = this->render_worker_running;
    this->render_restart_force = this->render_worker_running;
}

void MapEditorRenderer::requestStaticCache(int entity_width, bool force)
{
    if (!this->rendering_active || !this->canvas || !this->canvas->isVisible() ||
        !this->static_geometry ||
        this->static_geometry->geometry_revision != this->current_geometry_revision ||
        this->canvas->width() <= 0 || this->canvas->height() <= 0)
    {
        return;
    }

    this->current_entity_width = qMax(1, entity_width);
    if (!force && renderedStaticCacheCoversCurrentView(true))
        return;
    if (!force && pendingStaticCacheCoversCurrentView())
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
    StaticRenderRequest request = createStaticRenderRequest(
        request_id, this->current_entity_width);
    if (!request.geometry || !request.logical_size.isValid() ||
        request.coverage_world_bounds.isEmpty())
    {
        return;
    }

    request.cancelled = std::make_shared<std::atomic_bool>(false);
    this->pending_render_cancelled = request.cancelled;
    this->pending_render_request_id = request_id;
    this->active_render_request_id = request_id;
    this->render_worker_running = true;
    this->render_restart_requested = false;
    this->render_restart_force = false;
    this->pending_static_cache_coverage_world_bounds = request.coverage_world_bounds;
    this->pending_static_cache_zoom = request.zoom;
    this->pending_static_cache_entity_width = request.entity_width;
    this->pending_static_cache_device_pixel_ratio = request.device_pixel_ratio;

    const QPointer<MapEditorRenderer> renderer(this);
    QRunnable *runnable = QRunnable::create([renderer, request]
    {
        StaticRenderResult result = renderStaticCache(request);
        QCoreApplication *application = QCoreApplication::instance();
        if (!application)
            return;

        QMetaObject::invokeMethod(application,
            [renderer, result = std::move(result)]() mutable
        {
            if (!renderer)
                return;
            renderer->applyStaticRenderResult(std::move(result));
        }, Qt::QueuedConnection);
    });
    QThreadPool::globalInstance()->start(runnable);
}

MapEditorRenderer::StaticRenderRequest MapEditorRenderer::createStaticRenderRequest(
    quint64 request_id, int entity_width) const
{
    StaticRenderRequest request;
    if (!this->static_geometry || !this->map_model || !this->canvas)
        return request;

    request.request_id = request_id;
    request.geometry_revision = this->static_geometry->geometry_revision;
    request.zoom = this->map_model->zoom();
    request.entity_width = qMax(1, entity_width);
    request.device_pixel_ratio = qMax<qreal>(1.0, this->canvas->devicePixelRatioF());
    request.geometry = this->static_geometry;
    request.logical_size = MapRenderCacheMath::boundedCacheLogicalSize(
        this->canvas->size(), request.device_pixel_ratio);
    if (!request.logical_size.isValid())
        return StaticRenderRequest();

    const qreal scale = GeoWebMercator::zoomScale(
        request.zoom, MapRenderCacheMath::ReferenceZoom);
    if (scale <= 0.0)
        return StaticRenderRequest();

    request.coverage_world_bounds = MapRenderCacheMath::centeredWorldRect(
        visibleReferenceWorldCenter(), request.logical_size, scale);

    const std::function<void(InfrastructureEntity)> add_entity_image =
        [&request](InfrastructureEntity entity_type)
    {
        const int key = int(entity_type);
        if (request.entity_images.contains(key))
            return;

        const QString path = MapEntityPixmapRenderer::pixmapPathForEntity(entity_type);
        QImage image(path);
        if (!image.isNull() && request.entity_width > 0)
            image = image.scaledToWidth(request.entity_width, Qt::SmoothTransformation);
        request.entity_images.insert(key, image);
    };

    for (const StaticNode &node : request.geometry->nodes)
        add_entity_image(node.entity_type);
    add_entity_image(InfrastructureEntity::Pump);
    add_entity_image(InfrastructureEntity::Valve);
    return request;
}

MapEditorRenderer::StaticRenderResult MapEditorRenderer::renderStaticCache(
    const StaticRenderRequest &request)
{
    StaticRenderResult result;
    result.request_id = request.request_id;
    result.geometry_revision = request.geometry_revision;
    result.zoom = request.zoom;
    result.entity_width = request.entity_width;
    result.device_pixel_ratio = request.device_pixel_ratio;
    result.coverage_world_bounds = request.coverage_world_bounds;

    if (!request.geometry || !request.logical_size.isValid() ||
        request.coverage_world_bounds.isEmpty())
    {
        return result;
    }
    if (request.cancelled && request.cancelled->load(std::memory_order_relaxed))
        return result;

    const qreal scale = GeoWebMercator::zoomScale(
        request.zoom, MapRenderCacheMath::ReferenceZoom);
    if (scale <= 0.0)
        return result;

    const qreal image_left = request.coverage_world_bounds.left();
    const qreal image_top = request.coverage_world_bounds.top();
    const qreal logical_width = request.logical_size.width();
    const qreal logical_height = request.logical_size.height();

    const std::vector<MapRetainedVectorRenderer::HorizontalBand> bands =
        MapRetainedVectorRenderer::createHorizontalBands(
            request.logical_size, request.device_pixel_ratio);
    if (bands.empty())
        return result;

    struct BandContent
    {
        std::vector<int> pipe_indices;
        std::vector<int> device_indices;
        std::vector<int> node_indices;
    };

    std::vector<BandContent> band_contents(bands.size());
    const int band_count = int(bands.size());
    const std::function<int(qreal)> band_for_logical_y =
        [band_count, logical_height](qreal logical_y)
    {
        if (logical_height <= 0.0)
            return 0;
        return qBound(0, qFloor(logical_y * band_count / logical_height), band_count - 1);
    };

    for (int link_index = 0; link_index < request.geometry->links.size(); ++link_index)
    {
        if ((link_index & 511) == 0 && request.cancelled &&
            request.cancelled->load(std::memory_order_relaxed))
        {
            return result;
        }

        const StaticLink &link = request.geometry->links.at(link_index);
        if (link.world_vertices.size() < 2)
            continue;

        qreal minimum_x = std::numeric_limits<qreal>::infinity();
        qreal minimum_y = std::numeric_limits<qreal>::infinity();
        qreal maximum_x = -std::numeric_limits<qreal>::infinity();
        qreal maximum_y = -std::numeric_limits<qreal>::infinity();
        for (const QPointF &world_position : link.world_vertices)
        {
            const qreal x = (world_position.x() - image_left) * scale;
            const qreal y = (world_position.y() - image_top) * scale;
            minimum_x = std::min(minimum_x, x);
            minimum_y = std::min(minimum_y, y);
            maximum_x = std::max(maximum_x, x);
            maximum_y = std::max(maximum_y, y);
        }

        qreal padding_x = static_cache_item_padding;
        qreal padding_y = static_cache_item_padding;
        if (InfrastructureEntityTraits::isHydraulicDeviceLink(link.entity_type))
        {
            const QImage image = request.entity_images.value(int(link.entity_type));
            padding_x = qMax(padding_x, image.width() / 2.0 + static_cache_item_padding);
            padding_y = qMax(padding_y, image.height() / 2.0 + static_cache_item_padding);
            const qreal center_x = (link.device_center_world_position.x() - image_left) * scale;
            const qreal center_y = (link.device_center_world_position.y() - image_top) * scale;
            minimum_x = std::min(minimum_x, center_x);
            minimum_y = std::min(minimum_y, center_y);
            maximum_x = std::max(maximum_x, center_x);
            maximum_y = std::max(maximum_y, center_y);
        }

        minimum_x -= padding_x;
        maximum_x += padding_x;
        minimum_y -= padding_y;
        maximum_y += padding_y;
        if (maximum_x < 0.0 || minimum_x > logical_width ||
            maximum_y < 0.0 || minimum_y > logical_height)
        {
            continue;
        }

        const int first_band = band_for_logical_y(minimum_y);
        const int last_band = band_for_logical_y(maximum_y);
        for (int band_index = first_band; band_index <= last_band; ++band_index)
        {
            if (link.entity_type == InfrastructureEntity::Pipe)
                band_contents[band_index].pipe_indices.push_back(link_index);
            else if (InfrastructureEntityTraits::isHydraulicDeviceLink(link.entity_type))
                band_contents[band_index].device_indices.push_back(link_index);
        }
    }

    for (int node_index = 0; node_index < request.geometry->nodes.size(); ++node_index)
    {
        if ((node_index & 511) == 0 && request.cancelled &&
            request.cancelled->load(std::memory_order_relaxed))
        {
            return result;
        }

        const StaticNode &node = request.geometry->nodes.at(node_index);
        const qreal x = (node.world_position.x() - image_left) * scale;
        const qreal y = (node.world_position.y() - image_top) * scale;
        const QImage image = request.entity_images.value(int(node.entity_type));
        const qreal minimum_x = x - marker_dot_radius - static_cache_item_padding;
        const qreal maximum_x = x + qMax<qreal>(marker_dot_radius, image.width()) +
            static_cache_item_padding;
        const qreal minimum_y = y - qMax<qreal>(marker_dot_radius, image.height()) -
            static_cache_item_padding;
        const qreal maximum_y = y + marker_dot_radius + static_cache_item_padding;
        if (maximum_x < 0.0 || minimum_x > logical_width ||
            maximum_y < 0.0 || minimum_y > logical_height)
        {
            continue;
        }

        const int first_band = band_for_logical_y(minimum_y);
        const int last_band = band_for_logical_y(maximum_y);
        for (int band_index = first_band; band_index <= last_band; ++band_index)
            band_contents[band_index].node_indices.push_back(node_index);
    }

    if (request.cancelled && request.cancelled->load(std::memory_order_relaxed))
        return result;

    result.image = MapRetainedVectorRenderer::renderHorizontalBands(
        request.logical_size, request.device_pixel_ratio, bands, request.cancelled,
        [&request, &band_contents, scale, image_left, image_top](
            int band_index,
            const MapRetainedVectorRenderer::HorizontalBand &band,
            MapVectorDocument &document) -> bool
        {
            if (request.cancelled && request.cancelled->load(std::memory_order_relaxed))
                return false;

            const BandContent &content = band_contents.at(band_index);
            QPainterPath pipe_path;
            QPainterPath pipe_vertex_path;
            int processed_pipes = 0;
            for (int link_index : content.pipe_indices)
            {
                if ((++processed_pipes & 255) == 0 && request.cancelled &&
                    request.cancelled->load(std::memory_order_relaxed))
                {
                    return false;
                }

                const StaticLink &link = request.geometry->links.at(link_index);
                if (link.world_vertices.size() < 2)
                    continue;

                QPointF point(
                    (link.world_vertices.first().x() - image_left) * scale,
                    (link.world_vertices.first().y() - image_top) * scale - band.logical_top);
                pipe_path.moveTo(point);
                for (qsizetype index = 1; index < link.world_vertices.size(); ++index)
                {
                    point = QPointF(
                        (link.world_vertices.at(index).x() - image_left) * scale,
                        (link.world_vertices.at(index).y() - image_top) * scale - band.logical_top);
                    pipe_path.lineTo(point);
                    if (index + 1 < link.world_vertices.size())
                        pipe_vertex_path.addEllipse(point, pipe_vertex_radius, pipe_vertex_radius);
                }
            }

            QPen pipe_pen(Qt::black);
            pipe_pen.setWidthF(3.0);
            pipe_pen.setCapStyle(Qt::RoundCap);
            pipe_pen.setJoinStyle(Qt::RoundJoin);
            document.addStroke(std::move(pipe_path), pipe_pen);
            document.addFill(std::move(pipe_vertex_path), QBrush(Qt::black));

            QPainterPath device_path;
            int processed_devices = 0;
            for (int link_index : content.device_indices)
            {
                if ((++processed_devices & 255) == 0 && request.cancelled &&
                    request.cancelled->load(std::memory_order_relaxed))
                {
                    return false;
                }

                const StaticLink &link = request.geometry->links.at(link_index);
                if (link.world_vertices.size() < 2)
                    continue;

                const QPointF start_point(
                    (link.world_vertices.first().x() - image_left) * scale,
                    (link.world_vertices.first().y() - image_top) * scale - band.logical_top);
                const QPointF center_point(
                    (link.device_center_world_position.x() - image_left) * scale,
                    (link.device_center_world_position.y() - image_top) * scale - band.logical_top);
                const QPointF end_point(
                    (link.world_vertices.last().x() - image_left) * scale,
                    (link.world_vertices.last().y() - image_top) * scale - band.logical_top);
                device_path.moveTo(start_point);
                device_path.lineTo(center_point);
                device_path.moveTo(center_point);
                device_path.lineTo(end_point);
            }

            QPen device_pen(QColor(139, 90, 43));
            device_pen.setWidthF(3.0);
            device_pen.setCapStyle(Qt::RoundCap);
            device_pen.setJoinStyle(Qt::RoundJoin);
            document.addStroke(std::move(device_path), device_pen);

            for (int link_index : content.device_indices)
            {
                const StaticLink &link = request.geometry->links.at(link_index);
                const QImage image = request.entity_images.value(int(link.entity_type));
                if (image.isNull())
                    continue;
                const QPointF center_point(
                    (link.device_center_world_position.x() - image_left) * scale,
                    (link.device_center_world_position.y() - image_top) * scale - band.logical_top);
                const QRectF target_rect(
                    center_point.x() - image.width() / 2.0,
                    center_point.y() - image.height() / 2.0,
                    image.width(), image.height());
                document.addImage(image, target_rect);
            }

            QPainterPath node_path;
            for (int node_index : content.node_indices)
            {
                const StaticNode &node = request.geometry->nodes.at(node_index);
                const QPointF point(
                    (node.world_position.x() - image_left) * scale,
                    (node.world_position.y() - image_top) * scale - band.logical_top);
                node_path.addEllipse(point, marker_dot_radius, marker_dot_radius);
            }
            document.addFill(std::move(node_path), QBrush(Qt::black));

            for (int node_index : content.node_indices)
            {
                const StaticNode &node = request.geometry->nodes.at(node_index);
                const QImage image = request.entity_images.value(int(node.entity_type));
                if (image.isNull())
                    continue;

                const QPointF point(
                    (node.world_position.x() - image_left) * scale,
                    (node.world_position.y() - image_top) * scale - band.logical_top);
                const QPointF rounded_anchor(qRound(point.x()), qRound(point.y()));
                const QRectF target_rect(
                    rounded_anchor.x(), rounded_anchor.y() - image.height(),
                    image.width(), image.height());
                document.addImage(image, target_rect);
            }

            return !(request.cancelled && request.cancelled->load(std::memory_order_relaxed));
        },
        true);
    return result;
}

void MapEditorRenderer::applyStaticRenderResult(StaticRenderResult result)
{
    if (result.request_id != this->active_render_request_id)
        return;

    this->render_worker_running = false;
    this->active_render_request_id = 0;
    this->pending_render_request_id = 0;
    this->pending_render_cancelled.reset();
    this->pending_static_cache_coverage_world_bounds = QRectF();
    this->pending_static_cache_zoom = -1;
    this->pending_static_cache_entity_width = 10;
    this->pending_static_cache_device_pixel_ratio = 0.0;

    const bool restart_requested = this->render_restart_requested;
    const bool restart_force = this->render_restart_force;
    this->render_restart_requested = false;
    this->render_restart_force = false;

    const bool result_matches_current_view = this->static_geometry &&
        result.geometry_revision == this->current_geometry_revision &&
        result.geometry_revision == this->static_geometry->geometry_revision &&
        result.zoom == this->map_model->zoom() &&
        result.entity_width == this->current_entity_width &&
        qFuzzyCompare(result.device_pixel_ratio,
                      qMax<qreal>(1.0, this->canvas->devicePixelRatioF())) &&
        coverageCoversCurrentView(result.coverage_world_bounds, result.zoom, false);

    if (this->rendering_active && this->canvas && this->canvas->isVisible() &&
        result_matches_current_view && !result.image.isNull())
    {
        this->rendered_static_cache = std::move(result.image);
        this->rendered_static_cache_coverage_world_bounds =
            result.coverage_world_bounds;
        this->rendered_static_cache_geometry_revision = result.geometry_revision;
        this->rendered_static_cache_zoom = result.zoom;
        this->rendered_static_cache_entity_width = result.entity_width;
        this->rendered_static_cache_device_pixel_ratio = result.device_pixel_ratio;
        this->canvas->update();
    }

    if (!this->rendering_active || !this->canvas || !this->canvas->isVisible())
        return;

    if (restart_requested)
    {
        requestStaticCache(this->current_entity_width, restart_force);
        return;
    }

    requestStaticCache(this->current_entity_width);
}

bool MapEditorRenderer::renderedStaticCacheCoversCurrentView(
    bool include_rebuild_margin) const
{
    if (this->rendered_static_cache.isNull() || !this->static_geometry ||
        this->rendered_static_cache_geometry_revision != this->current_geometry_revision ||
        this->rendered_static_cache_geometry_revision != this->static_geometry->geometry_revision ||
        this->rendered_static_cache_zoom != this->map_model->zoom() ||
        this->rendered_static_cache_entity_width != this->current_entity_width ||
        !qFuzzyCompare(this->rendered_static_cache_device_pixel_ratio,
                       qMax<qreal>(1.0, this->canvas->devicePixelRatioF())))
    {
        return false;
    }

    return coverageCoversCurrentView(
        this->rendered_static_cache_coverage_world_bounds,
        this->rendered_static_cache_zoom, include_rebuild_margin);
}

bool MapEditorRenderer::pendingStaticCacheCoversCurrentView() const
{
    if (this->pending_render_request_id == 0 || !this->static_geometry ||
        this->static_geometry->geometry_revision != this->current_geometry_revision ||
        this->pending_static_cache_zoom != this->map_model->zoom() ||
        this->pending_static_cache_entity_width != this->current_entity_width ||
        !qFuzzyCompare(this->pending_static_cache_device_pixel_ratio,
                       qMax<qreal>(1.0, this->canvas->devicePixelRatioF())))
    {
        return false;
    }

    return coverageCoversCurrentView(
        this->pending_static_cache_coverage_world_bounds,
        this->pending_static_cache_zoom, true);
}

bool MapEditorRenderer::coverageCoversCurrentView(
    const QRectF &coverage_world_bounds, int zoom, bool include_rebuild_margin) const
{
    if (coverage_world_bounds.isEmpty() || !this->canvas || !this->static_geometry)
        return false;

    const qreal scale = GeoWebMercator::zoomScale(
        zoom, MapRenderCacheMath::ReferenceZoom);
    if (scale <= 0.0)
        return false;

    const QRectF view_bounds = visibleReferenceWorldRect();
    const qreal current_scale = referenceScaleForCurrentZoom();
    if (current_scale <= 0.0)
        return false;
    return MapRenderCacheMath::coverageCoversView(
        coverage_world_bounds, view_bounds, current_scale,
        include_rebuild_margin);
}

bool MapEditorRenderer::paintStaticCache(
    QPainter &painter, const MapEditorVisualState &visual_state)
{
    if (this->rendered_static_cache.isNull() || !this->static_geometry ||
        this->rendered_static_cache_geometry_revision != this->current_geometry_revision ||
        this->rendered_static_cache_geometry_revision != this->static_geometry->geometry_revision)
    {
        return false;
    }

    const qreal scale = referenceScaleForCurrentZoom();
    if (scale <= 0.0)
        return false;
    if (!this->rendered_static_cache_coverage_world_bounds.contains(
            visibleReferenceWorldRect()))
    {
        return false;
    }

    const QPointF center = visibleReferenceWorldCenter();
    const QRectF target_rect(
        this->canvas->width() / 2.0 +
            (this->rendered_static_cache_coverage_world_bounds.left() - center.x()) * scale,
        this->canvas->height() / 2.0 +
            (this->rendered_static_cache_coverage_world_bounds.top() - center.y()) * scale,
        this->rendered_static_cache_coverage_world_bounds.width() * scale,
        this->rendered_static_cache_coverage_world_bounds.height() * scale);

    painter.save();
    if (visual_state.move.active)
        painter.setClipPath(moveStaticVisibleClipPath(visual_state), Qt::IntersectClip);
    painter.setRenderHint(QPainter::SmoothPixmapTransform,
        this->rendered_static_cache_zoom != this->map_model->zoom() ||
        this->rendered_static_cache_entity_width != this->current_entity_width);
    painter.drawImage(target_rect, this->rendered_static_cache);
    painter.restore();
    return true;
}

QPainterPath MapEditorRenderer::moveStaticVisibleClipPath(
    const MapEditorVisualState &visual_state)
{
    if (!this->canvas || !this->static_geometry || !visual_state.move.active)
    {
        QPainterPath full_path;
        if (this->canvas)
            full_path.addRect(QRectF(this->canvas->rect()));
        return full_path;
    }

    const int current_zoom = this->map_model ? this->map_model->zoom() : -1;
    const QPointF current_center_tile = this->map_model ? this->map_model->centerTile() : QPointF();
    const QSize current_viewport_size = this->canvas->size();
    if (this->move_static_clip_session_id == visual_state.move.session_id &&
        this->move_static_clip_geometry_revision == this->static_geometry->geometry_revision &&
        this->move_static_clip_zoom == current_zoom &&
        this->move_static_clip_center_tile == current_center_tile &&
        this->move_static_clip_viewport_size == current_viewport_size &&
        this->move_static_clip_entity_width == visual_state.entity_width &&
        !this->move_static_visible_clip_path.isEmpty())
    {
        return this->move_static_visible_clip_path;
    }

    QPainterPath clear_path;
    QPainterPathStroker line_stroker;
    line_stroker.setWidth(9.0);
    line_stroker.setCapStyle(Qt::RoundCap);
    line_stroker.setJoinStyle(Qt::RoundJoin);

    for (const MapEditorDynamicLinkVisualState &dynamic_link : visual_state.move.links)
    {
        const auto link_iterator =
            this->static_geometry->link_indices_by_uuid.constFind(dynamic_link.uuid);
        if (link_iterator == this->static_geometry->link_indices_by_uuid.cend())
            continue;

        const StaticLink &link = this->static_geometry->links.at(link_iterator.value());
        if (link.world_vertices.size() >= 2)
        {
            QPainterPath link_path;
            QPointF point = screenFromReferenceWorld(link.world_vertices.first());
            link_path.moveTo(point);
            if (InfrastructureEntityTraits::isHydraulicDeviceLink(link.entity_type))
            {
                link_path.lineTo(screenFromReferenceWorld(link.device_center_world_position));
                link_path.lineTo(screenFromReferenceWorld(link.world_vertices.last()));
            }
            else
            {
                for (qsizetype i = 1; i < link.world_vertices.size(); ++i)
                    link_path.lineTo(screenFromReferenceWorld(link.world_vertices.at(i)));
                for (qsizetype i = 1; i + 1 < link.world_vertices.size(); ++i)
                {
                    clear_path.addEllipse(
                        screenFromReferenceWorld(link.world_vertices.at(i)),
                        pipe_vertex_radius + 2.0, pipe_vertex_radius + 2.0);
                }
            }
            clear_path.addPath(line_stroker.createStroke(link_path));
        }

        if (InfrastructureEntityTraits::isHydraulicDeviceLink(link.entity_type))
        {
            const QPointF center_point = screenFromReferenceWorld(
                link.device_center_world_position);
            const QString path = MapEntityPixmapRenderer::pixmapPathForEntity(link.entity_type);
            clear_path.addRect(this->pixmap_renderer.centeredRect(
                center_point, path, visual_state.entity_width).adjusted(-2.0, -2.0, 2.0, 2.0));
        }
    }

    for (const MapEditorDynamicMarkerVisualState &dynamic_marker : visual_state.move.markers)
    {
        if (!InfrastructureEntityTraits::isHydraulicConnectionNode(dynamic_marker.entity))
            continue;
        const auto node_iterator =
            this->static_geometry->node_indices_by_uuid.constFind(dynamic_marker.uuid);
        if (node_iterator == this->static_geometry->node_indices_by_uuid.cend())
            continue;

        const StaticNode &node = this->static_geometry->nodes.at(node_iterator.value());
        const QPointF point = screenFromReferenceWorld(node.world_position);
        clear_path.addEllipse(point, marker_dot_radius + 2.0, marker_dot_radius + 2.0);
        const QString path = MapEntityPixmapRenderer::pixmapPathForEntity(node.entity_type);
        const QPointF rounded_anchor(qRound(point.x()), qRound(point.y()));
        clear_path.addRect(this->pixmap_renderer.bottomAnchoredRect(
            rounded_anchor, path, visual_state.entity_width).adjusted(-2.0, -2.0, 2.0, 2.0));
    }

    QPainterPath full_path;
    full_path.addRect(QRectF(this->canvas->rect()));
    this->move_static_visible_clip_path = full_path.subtracted(clear_path);
    this->move_static_clip_session_id = visual_state.move.session_id;
    this->move_static_clip_geometry_revision = this->static_geometry->geometry_revision;
    this->move_static_clip_zoom = current_zoom;
    this->move_static_clip_center_tile = current_center_tile;
    this->move_static_clip_viewport_size = current_viewport_size;
    this->move_static_clip_entity_width = visual_state.entity_width;
    return this->move_static_visible_clip_path;
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

    syncStaticGeometry(network_snapshot);
    requestStaticCache(visual_state.entity_width);

    if (!paintStaticCache(painter, visual_state))
    {
        paintDirectNetwork(painter, network_snapshot, visual_state);
        if (visual_state.move.active)
            paintMovingNetwork(painter, visual_state);
        return;
    }

    if (visual_state.move.active)
    {
        paintMovingNetwork(painter, visual_state);
        return;
    }

    paintInteractiveNetwork(painter, network_snapshot, visual_state);
}

void MapEditorRenderer::paintDirectNetwork(
    QPainter &painter,
    const NetworkRenderSnapshot &network_snapshot,
    const MapEditorVisualState &visual_state)
{
    QHash<QUuid, const NetworkRenderNode *> nodes_by_uuid;
    const bool needs_node_lookup = visual_state.placement.creating &&
        InfrastructureEntityTraits::isHydraulicNetworkLink(visual_state.placement.entity);
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

void MapEditorRenderer::paintInteractiveNetwork(
    QPainter &painter,
    const NetworkRenderSnapshot &network_snapshot,
    const MapEditorVisualState &visual_state)
{
    if (!this->static_geometry ||
        this->static_geometry->geometry_revision != network_snapshot.geometry_revision)
    {
        return;
    }

    QHash<QUuid, const NetworkRenderNode *> nodes_by_uuid;
    const bool needs_node_lookup = visual_state.placement.creating &&
        InfrastructureEntityTraits::isHydraulicNetworkLink(visual_state.placement.entity);
    if (needs_node_lookup)
    {
        nodes_by_uuid.reserve(network_snapshot.nodes.size());
        for (const NetworkRenderNode &node : network_snapshot.nodes)
            nodes_by_uuid.insert(node.uuid, &node);
    }

    paintSelectedPipes(painter, visual_state);
    paintPipePlacement(painter, visual_state, nodes_by_uuid);
    paintDeviceLinkPlacement(painter, visual_state, nodes_by_uuid);
    paintSelectedMarkersAndDeviceLinks(painter, visual_state);
    paintSimulationError(painter, visual_state);
    paintPlacement(painter, visual_state, nodes_by_uuid);
}

void MapEditorRenderer::paintMovingNetwork(
    QPainter &painter, const MapEditorVisualState &visual_state)
{
    painter.save();

    for (const MapEditorDynamicLinkVisualState &link : visual_state.move.links)
    {
        if (link.vertices_wgs84.size() < 2)
            continue;

        if (link.entity == InfrastructureEntity::Pipe)
        {
            const bool selected = visual_state.selected_pipe_uuids.contains(link.uuid);
            QPen pen(selected ? QColor(0, 190, 255) : QColor(Qt::black));
            pen.setWidthF(3.0);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            QPointF previous = screenFromWgs84(
                link.vertices_wgs84.first(), visual_state.wrap_reference_longitude);
            for (qsizetype i = 1; i < link.vertices_wgs84.size(); ++i)
            {
                const QPointF point = screenFromWgs84(
                    link.vertices_wgs84.at(i), visual_state.wrap_reference_longitude);
                painter.drawLine(previous, point);
                previous = point;
            }
            painter.setPen(Qt::NoPen);
            painter.setBrush(selected ? QColor(0, 190, 255) : QColor(Qt::black));
            for (qsizetype i = 1; i + 1 < link.vertices_wgs84.size(); ++i)
            {
                painter.drawEllipse(
                    screenFromWgs84(link.vertices_wgs84.at(i),
                                    visual_state.wrap_reference_longitude),
                    pipe_vertex_radius, pipe_vertex_radius);
            }
            continue;
        }

        if (!InfrastructureEntityTraits::isHydraulicDeviceLink(link.entity) ||
            link.vertices_wgs84.size() < 3)
            continue;

        const bool selected = visual_state.selected_marker_uuids.contains(link.uuid);
        QPen pen(selected ? QColor(0, 190, 255) : QColor(139, 90, 43));
        pen.setWidthF(3.0);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        const QPointF start_point = screenFromWgs84(
            link.vertices_wgs84.first(), visual_state.wrap_reference_longitude);
        const QPointF center_point = screenFromWgs84(
            link.vertices_wgs84.at(1), visual_state.wrap_reference_longitude);
        const QPointF end_point = screenFromWgs84(
            link.vertices_wgs84.last(), visual_state.wrap_reference_longitude);
        painter.drawLine(start_point, center_point);
        painter.drawLine(center_point, end_point);

        const QString path = MapEntityPixmapRenderer::pixmapPathForEntity(link.entity);
        const QRectF target_rect = this->pixmap_renderer.centeredRect(
            center_point, path, visual_state.entity_width);
        this->pixmap_renderer.paint(
            painter, path, visual_state.entity_width, target_rect,
            selected ? MapEntityPixmapRenderer::Highlight::Selected
                     : MapEntityPixmapRenderer::Highlight::None);
    }

    painter.setPen(Qt::NoPen);
    for (const MapEditorDynamicMarkerVisualState &marker : visual_state.move.markers)
    {
        if (!InfrastructureEntityTraits::isHydraulicConnectionNode(marker.entity))
            continue;
        const QPointF point = screenFromWgs84(
            marker.coordinate_wgs84, visual_state.wrap_reference_longitude);
        painter.setBrush(Qt::black);
        painter.drawEllipse(point, marker_dot_radius, marker_dot_radius);
        const QPointF rounded_anchor(qRound(point.x()), qRound(point.y()));
        const QRectF target_rect = this->pixmap_renderer.bottomAnchoredRect(
            rounded_anchor, marker.pixmap_path, visual_state.entity_width);
        this->pixmap_renderer.paint(
            painter, marker.pixmap_path, visual_state.entity_width, target_rect,
            visual_state.selected_marker_uuids.contains(marker.uuid)
                ? MapEntityPixmapRenderer::Highlight::Selected
                : MapEntityPixmapRenderer::Highlight::None);
    }

    painter.restore();
}

void MapEditorRenderer::paintSelectedPipes(
    QPainter &painter, const MapEditorVisualState &visual_state) const
{
    if (!this->static_geometry || visual_state.selected_pipe_uuids.isEmpty())
        return;

    painter.save();
    QPen selected_pen(QColor(0, 190, 255));
    selected_pen.setWidthF(3.0);
    selected_pen.setCapStyle(Qt::RoundCap);
    selected_pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(selected_pen);

    for (const QUuid &uuid : visual_state.selected_pipe_uuids)
    {
        const QHash<QUuid, int>::const_iterator iterator =
            this->static_geometry->link_indices_by_uuid.constFind(uuid);
        if (iterator == this->static_geometry->link_indices_by_uuid.cend())
            continue;

        const StaticLink &link = this->static_geometry->links.at(iterator.value());
        if (link.entity_type != InfrastructureEntity::Pipe ||
            link.world_vertices.size() < 2)
        {
            continue;
        }

        QPointF previous_point = screenFromReferenceWorld(link.world_vertices.first());
        for (qsizetype index = 1; index < link.world_vertices.size(); ++index)
        {
            const QPointF point = screenFromReferenceWorld(link.world_vertices.at(index));
            painter.drawLine(previous_point, point);
            previous_point = point;
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 190, 255));
        for (qsizetype index = 1; index + 1 < link.world_vertices.size(); ++index)
        {
            painter.drawEllipse(
                screenFromReferenceWorld(link.world_vertices.at(index)),
                pipe_vertex_radius, pipe_vertex_radius);
        }
        painter.setPen(selected_pen);
        painter.setBrush(Qt::NoBrush);
    }

    painter.restore();
}

void MapEditorRenderer::paintSelectedMarkersAndDeviceLinks(
    QPainter &painter, const MapEditorVisualState &visual_state)
{
    if (!this->static_geometry)
        return;

    painter.save();
    QPen selected_pen(QColor(0, 190, 255));
    selected_pen.setWidthF(3.0);
    selected_pen.setCapStyle(Qt::RoundCap);
    selected_pen.setJoinStyle(Qt::RoundJoin);

    for (const QUuid &uuid : visual_state.selected_marker_uuids)
    {
        const QHash<QUuid, int>::const_iterator link_iterator =
            this->static_geometry->link_indices_by_uuid.constFind(uuid);
        if (link_iterator == this->static_geometry->link_indices_by_uuid.cend())
            continue;

        const StaticLink &link = this->static_geometry->links.at(link_iterator.value());
        if (!InfrastructureEntityTraits::isHydraulicDeviceLink(link.entity_type) ||
            link.world_vertices.size() < 2)
            continue;

        const QPointF start_point = screenFromReferenceWorld(link.world_vertices.first());
        const QPointF center_point = screenFromReferenceWorld(
            link.device_center_world_position);
        const QPointF end_point = screenFromReferenceWorld(link.world_vertices.last());
        painter.setPen(selected_pen);
        painter.drawLine(start_point, center_point);
        painter.drawLine(center_point, end_point);

        const QString path = MapEntityPixmapRenderer::pixmapPathForEntity(link.entity_type);
        const QRectF target_rect = this->pixmap_renderer.centeredRect(
            center_point, path, visual_state.entity_width);
        this->pixmap_renderer.paint(
            painter, path, visual_state.entity_width, target_rect,
            MapEntityPixmapRenderer::Highlight::Selected);
    }

    const QUuid connection_target_uuid = visual_state.placement.connection_target_uuid;
    for (const QUuid &uuid : visual_state.selected_marker_uuids)
    {
        const QHash<QUuid, int>::const_iterator node_iterator =
            this->static_geometry->node_indices_by_uuid.constFind(uuid);
        if (node_iterator == this->static_geometry->node_indices_by_uuid.cend())
            continue;

        const StaticNode &node = this->static_geometry->nodes.at(node_iterator.value());
        const QPointF point = screenFromReferenceWorld(node.world_position);
        const QString path = MapEntityPixmapRenderer::pixmapPathForEntity(node.entity_type);
        const QPointF rounded_anchor(qRound(point.x()), qRound(point.y()));
        const QRectF target_rect = this->pixmap_renderer.bottomAnchoredRect(
            rounded_anchor, path, visual_state.entity_width);
        this->pixmap_renderer.paint(
            painter, path, visual_state.entity_width, target_rect,
            MapEntityPixmapRenderer::Highlight::Selected);
    }

    if (!connection_target_uuid.isNull())
    {
        const QHash<QUuid, int>::const_iterator node_iterator =
            this->static_geometry->node_indices_by_uuid.constFind(connection_target_uuid);
        if (node_iterator != this->static_geometry->node_indices_by_uuid.cend())
        {
            const StaticNode &node = this->static_geometry->nodes.at(node_iterator.value());
            const QPointF point = screenFromReferenceWorld(node.world_position);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 140, 255));
            painter.drawEllipse(point, connection_target_radius, connection_target_radius);

            const QString path = MapEntityPixmapRenderer::pixmapPathForEntity(node.entity_type);
            const QPointF rounded_anchor(qRound(point.x()), qRound(point.y()));
            const QRectF target_rect = this->pixmap_renderer.bottomAnchoredRect(
                rounded_anchor, path, visual_state.entity_width);
            const MapEntityPixmapRenderer::Highlight highlight =
                visual_state.selected_marker_uuids.contains(connection_target_uuid)
                ? MapEntityPixmapRenderer::Highlight::Selected
                : MapEntityPixmapRenderer::Highlight::None;
            this->pixmap_renderer.paint(
                painter, path, visual_state.entity_width, target_rect, highlight);
        }
    }

    painter.restore();
}

void MapEditorRenderer::paintSimulationError(
    QPainter &painter, const MapEditorVisualState &visual_state)
{
    if (!this->static_geometry || visual_state.simulation_error_entities.isEmpty())
        return;

    painter.save();

    for (QHash<QUuid, InfrastructureEntity>::const_iterator error_iterator =
             visual_state.simulation_error_entities.cbegin();
         error_iterator != visual_state.simulation_error_entities.cend(); ++error_iterator)
    {
        const QUuid &uuid = error_iterator.key();
        const InfrastructureEntity entity_type = error_iterator.value();
        const bool stale = isSimulationDiagnosticEntityStale(visual_state, uuid);
        const QColor error_color = stale ? QColor(128, 128, 128) : QColor(255, 0, 0);
        const QHash<QUuid, int>::const_iterator link_iterator =
            this->static_geometry->link_indices_by_uuid.constFind(uuid);
        if (link_iterator != this->static_geometry->link_indices_by_uuid.cend())
        {
            const StaticLink &link = this->static_geometry->links.at(link_iterator.value());
            if (link.entity_type != entity_type || link.world_vertices.size() < 2)
                continue;

            const bool selected = link.entity_type == InfrastructureEntity::Pipe
                ? visual_state.selected_pipe_uuids.contains(link.uuid)
                : visual_state.selected_marker_uuids.contains(link.uuid);
            if (selected)
            {
                QPen selected_outer_pen(QColor(0, 190, 255));
                selected_outer_pen.setWidthF(7.0);
                selected_outer_pen.setCapStyle(Qt::RoundCap);
                selected_outer_pen.setJoinStyle(Qt::RoundJoin);
                painter.setPen(selected_outer_pen);

                if (link.entity_type == InfrastructureEntity::Pipe)
                {
                    QPointF previous_point = screenFromReferenceWorld(link.world_vertices.first());
                    for (qsizetype index = 1; index < link.world_vertices.size(); ++index)
                    {
                        const QPointF point = screenFromReferenceWorld(link.world_vertices.at(index));
                        painter.drawLine(previous_point, point);
                        previous_point = point;
                    }
                }
                else if (InfrastructureEntityTraits::isHydraulicDeviceLink(link.entity_type))
                {
                    const QPointF start_point = screenFromReferenceWorld(link.world_vertices.first());
                    const QPointF center_point = screenFromReferenceWorld(link.device_center_world_position);
                    const QPointF end_point = screenFromReferenceWorld(link.world_vertices.last());
                    painter.drawLine(start_point, center_point);
                    painter.drawLine(center_point, end_point);
                }
            }

            QPen error_pen(error_color);
            error_pen.setWidthF(3.0);
            error_pen.setCapStyle(Qt::RoundCap);
            error_pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(error_pen);
            if (link.entity_type == InfrastructureEntity::Pipe)
            {
                QPointF previous_point = screenFromReferenceWorld(link.world_vertices.first());
                for (qsizetype index = 1; index < link.world_vertices.size(); ++index)
                {
                    const QPointF point = screenFromReferenceWorld(link.world_vertices.at(index));
                    painter.drawLine(previous_point, point);
                    previous_point = point;
                }
            }
            else if (InfrastructureEntityTraits::isHydraulicDeviceLink(link.entity_type))
            {
                const QPointF start_point = screenFromReferenceWorld(link.world_vertices.first());
                const QPointF center_point = screenFromReferenceWorld(link.device_center_world_position);
                const QPointF end_point = screenFromReferenceWorld(link.world_vertices.last());
                painter.drawLine(start_point, center_point);
                painter.drawLine(center_point, end_point);

                const QString path = MapEntityPixmapRenderer::pixmapPathForEntity(link.entity_type);
                const QRectF target_rect = this->pixmap_renderer.centeredRect(center_point, path, visual_state.entity_width);
                this->pixmap_renderer.paint(painter, path, visual_state.entity_width, target_rect,
                    selected
                        ? (stale
                            ? MapEntityPixmapRenderer::Highlight::SelectedStale
                            : MapEntityPixmapRenderer::Highlight::SelectedError)
                        : (stale
                            ? MapEntityPixmapRenderer::Highlight::Stale
                            : MapEntityPixmapRenderer::Highlight::Error));
            }
            continue;
        }

        const QHash<QUuid, int>::const_iterator node_iterator =
            this->static_geometry->node_indices_by_uuid.constFind(uuid);
        if (node_iterator == this->static_geometry->node_indices_by_uuid.cend())
            continue;

        const StaticNode &node = this->static_geometry->nodes.at(node_iterator.value());
        if (node.entity_type != entity_type)
            continue;

        const bool selected = visual_state.selected_marker_uuids.contains(node.uuid);
        const QPointF point = screenFromReferenceWorld(node.world_position);
        const QString path = MapEntityPixmapRenderer::pixmapPathForEntity(node.entity_type);
        const QPointF rounded_anchor(qRound(point.x()), qRound(point.y()));
        const QRectF target_rect = this->pixmap_renderer.bottomAnchoredRect(rounded_anchor, path, visual_state.entity_width);
        this->pixmap_renderer.paint(painter, path, visual_state.entity_width, target_rect,
            selected
                ? (stale
                    ? MapEntityPixmapRenderer::Highlight::SelectedStale
                    : MapEntityPixmapRenderer::Highlight::SelectedError)
                : (stale
                    ? MapEntityPixmapRenderer::Highlight::Stale
                    : MapEntityPixmapRenderer::Highlight::Error));
    }

    painter.restore();
}

void MapEditorRenderer::paintPipePlacement(
    QPainter &painter,
    const MapEditorVisualState &visual_state,
    const QHash<QUuid, const NetworkRenderNode *> &nodes_by_uuid) const
{
    if (!visual_state.placement.creating ||
        visual_state.placement.entity != InfrastructureEntity::Pipe ||
        visual_state.placement.pipe_start_node_uuid.isNull())
    {
        return;
    }

    const NetworkRenderNode *start_node = nodeByUuid(
        nodes_by_uuid, visual_state.placement.pipe_start_node_uuid);
    if (!start_node)
        return;

    painter.save();
    QPen preview_pen(QColor(0, 140, 255));
    preview_pen.setWidthF(3.0);
    preview_pen.setCapStyle(Qt::RoundCap);
    preview_pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(preview_pen);

    QPointF previous_point = screenFromWgs84(
        start_node->coordinate_wgs84, visual_state.wrap_reference_longitude);
    for (const CoordinateWGS84 &vertex : visual_state.placement.pipe_intermediate_vertices)
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
            end_node->coordinate_wgs84, visual_state.wrap_reference_longitude);
    }
    painter.drawLine(previous_point, preview_end);
    painter.restore();
}

void MapEditorRenderer::paintDeviceLinkPlacement(
    QPainter &painter,
    const MapEditorVisualState &visual_state,
    const QHash<QUuid, const NetworkRenderNode *> &nodes_by_uuid)
{
    if (!visual_state.placement.creating ||
        !InfrastructureEntityTraits::isHydraulicDeviceLink(visual_state.placement.entity) ||
        visual_state.placement.device_link_start_node_uuid.isNull())
    {
        return;
    }

    const NetworkRenderNode *start_node = nodeByUuid(
        nodes_by_uuid, visual_state.placement.device_link_start_node_uuid);
    if (!start_node)
        return;

    const QPointF start_point = screenFromWgs84(
        start_node->coordinate_wgs84, visual_state.wrap_reference_longitude);
    QPointF end_point = visual_state.placement.mouse_position;
    const NetworkRenderNode *end_node = nodeByUuid(
        nodes_by_uuid, visual_state.placement.connection_target_uuid);
    if (end_node)
    {
        end_point = screenFromWgs84(
            end_node->coordinate_wgs84, visual_state.wrap_reference_longitude);
    }

    const QPointF center_point = (start_point + end_point) / 2.0;
    painter.save();
    QPen preview_pen(QColor(0, 140, 255));
    preview_pen.setWidthF(3.0);
    preview_pen.setCapStyle(Qt::RoundCap);
    preview_pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(preview_pen);
    painter.drawLine(start_point, center_point);
    painter.drawLine(center_point, end_point);
    painter.restore();
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
        const bool error = isSimulationErrorEntity(visual_state, InfrastructureEntity::Pipe, link.uuid);
        const bool stale = isSimulationDiagnosticEntityStale(visual_state, link.uuid);
        const QColor pipe_color = error
            ? (stale ? QColor(128, 128, 128) : QColor(255, 0, 0))
            : selected ? QColor(0, 190, 255) : QColor(Qt::black);
        QPen pipe_pen(pipe_color);
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
        painter.setBrush(pipe_color);
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
        if (!InfrastructureEntityTraits::isHydraulicDeviceLink(link.entity_type) ||
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
        const bool error = isSimulationErrorEntity(visual_state, link.entity_type, link.uuid);
        if (selected && error)
        {
            QPen selected_outer_pen(QColor(0, 190, 255));
            selected_outer_pen.setWidthF(7.0);
            selected_outer_pen.setCapStyle(Qt::RoundCap);
            selected_outer_pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(selected_outer_pen);
            painter.drawLine(start_point, center_point);
            painter.drawLine(center_point, end_point);
        }

        const bool stale = isSimulationDiagnosticEntityStale(visual_state, link.uuid);
        const QColor placed_color = error
            ? (stale ? QColor(128, 128, 128) : QColor(255, 0, 0))
            : selected ? QColor(0, 190, 255) : QColor(139, 90, 43);
        QPen placed_pen(placed_color);
        placed_pen.setWidthF(3.0);
        placed_pen.setCapStyle(Qt::RoundCap);
        placed_pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(placed_pen);
        painter.drawLine(start_point, center_point);
        painter.drawLine(center_point, end_point);
    }

    if (visual_state.placement.creating &&
        InfrastructureEntityTraits::isHydraulicDeviceLink(visual_state.placement.entity) &&
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
        if (!InfrastructureEntityTraits::isHydraulicDeviceLink(link.entity_type) ||
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
        const bool error = isSimulationErrorEntity(visual_state, link.entity_type, link.uuid);
        const bool stale = isSimulationDiagnosticEntityStale(visual_state, link.uuid);
        const bool selected = visual_state.selected_marker_uuids.contains(link.uuid);
        const MapEntityPixmapRenderer::Highlight highlight = error && selected
            ? (stale
                ? MapEntityPixmapRenderer::Highlight::SelectedStale
                : MapEntityPixmapRenderer::Highlight::SelectedError)
            : error
                ? (stale
                    ? MapEntityPixmapRenderer::Highlight::Stale
                    : MapEntityPixmapRenderer::Highlight::Error)
            : selected ? MapEntityPixmapRenderer::Highlight::Selected
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
        const bool error = isSimulationErrorEntity(visual_state, node.entity_type, node.uuid);
        const bool stale = isSimulationDiagnosticEntityStale(visual_state, node.uuid);
        const bool selected = visual_state.selected_marker_uuids.contains(node.uuid);
        const MapEntityPixmapRenderer::Highlight highlight = error && selected
            ? (stale
                ? MapEntityPixmapRenderer::Highlight::SelectedStale
                : MapEntityPixmapRenderer::Highlight::SelectedError)
            : error
                ? (stale
                    ? MapEntityPixmapRenderer::Highlight::Stale
                    : MapEntityPixmapRenderer::Highlight::Error)
            : selected ? MapEntityPixmapRenderer::Highlight::Selected
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

    if (InfrastructureEntityTraits::isHydraulicDeviceLink(visual_state.placement.entity) &&
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
