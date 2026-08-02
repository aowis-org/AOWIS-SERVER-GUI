#include "map_network_overlay_widget.h"

#include "../hydraulic_data.h"
#include "../geo_web_mercator.h"

#include <QColor>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
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
        invalidateCachedImage();
        update();
    });

    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this, &MapNetworkOverlayWidget::syncSnapshot);
    connect(this->hydraulic_data, &HydraulicData::signalNodeChanged, this, &MapNetworkOverlayWidget::syncSnapshot);
    connect(this->hydraulic_data, &HydraulicData::signalLinkChanged, this, &MapNetworkOverlayWidget::syncSnapshot);

    QTimer::singleShot(0, this, &MapNetworkOverlayWidget::syncSnapshot);
}

void MapNetworkOverlayWidget::paintEvent(QPaintEvent *event)
{
    if (!this->snapshot_initialized ||
        (this->snapshot.nodes.isEmpty() && this->snapshot.links.isEmpty()))
        return;

    const qreal device_pixel_ratio = devicePixelRatioF();
    if (this->cached_image.isNull() || this->cached_zoom != this->map_model->zoom() ||
        !qFuzzyCompare(this->cached_device_pixel_ratio, device_pixel_ratio))
    {
        rebuildCachedImage();
    }

    if (this->cached_image.isNull())
        return;

    QPainter painter(this);
    painter.setClipRegion(event->region());
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
    invalidateCachedImage();
    update();
}

void MapNetworkOverlayWidget::invalidateCachedImage()
{
    this->cached_image = QImage();
    this->cached_image_world_top_left = QPointF();
    this->cached_geometry_world_origin = QPointF();
    this->cached_zoom = -1;
    this->cached_device_pixel_ratio = 0.0;
}

void MapNetworkOverlayWidget::rebuildCachedImage()
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

    const auto include_point = [&minimum_x, &minimum_y, &maximum_x, &maximum_y](const QPointF &point)
    {
        minimum_x = std::min(minimum_x, point.x());
        minimum_y = std::min(minimum_y, point.y());
        maximum_x = std::max(maximum_x, point.x());
        maximum_y = std::max(maximum_y, point.y());
    };

    projected_nodes.reserve(this->snapshot.nodes.size());
    node_positions.reserve(this->snapshot.nodes.size());
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
        projected_node.world_position = QPointF(
            nearestWrappedWorldPixel(raw_world_position.x(), anchor_x, zoom),
            raw_world_position.y());
        projected_nodes.append(projected_node);
        node_positions.insert(projected_node.render_id, projected_node.world_position);
        include_point(projected_node.world_position);
    }

    projected_links.reserve(this->snapshot.links.size());
    for (const NetworkRenderLink &link : this->snapshot.links)
    {
        ProjectedLink projected_link;
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

        if (projected_link.world_vertices.size() >= 2)
            projected_links.append(projected_link);
    }

    if ((projected_nodes.isEmpty() && projected_links.isEmpty()) ||
        !std::isfinite(minimum_x) || !std::isfinite(minimum_y) ||
        !std::isfinite(maximum_x) || !std::isfinite(maximum_y))
    {
        invalidateCachedImage();
        this->cached_zoom = zoom;
        this->cached_device_pixel_ratio = device_pixel_ratio;
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
    this->cached_zoom = zoom;
    this->cached_device_pixel_ratio = device_pixel_ratio;
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
