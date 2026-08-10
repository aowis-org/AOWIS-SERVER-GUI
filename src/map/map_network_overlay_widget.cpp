#include "map_network_overlay_widget.h"

#include "../geo_web_mercator.h"
#include "../hydraulic_data.h"
#include "../infrastructure_entity_traits.h"
#include "../network_render_snapshot_builder.h"
#include "map_render_cache_math.h"
#include "map_retained_vector_renderer.h"
#include "map_vector_document.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QCoreApplication>
#include <QHideEvent>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPalette>
#include <QRadialGradient>
#include <QPointer>
#include <QPixmap>
#include <QRunnable>
#include <QSemaphore>
#include <QShowEvent>
#include <QThreadPool>
#include <QTimer>
#include <QTransform>
#include <QtMath>

#include <algorithm>
#include <array>
#include <functional>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace
{
constexpr qreal NetworkImagePadding = 8.0;
constexpr qreal LinkHitDistance = 7.0;
constexpr qreal SpatialCellSize = 128.0;
constexpr int SymbologyColorBucketCount = 256;
constexpr int HeatmapMaximumDimension = 2048;
constexpr qint64 HeatmapMaximumArea = 2LL * 1024LL * 1024LL;
constexpr qint64 HeatmapMaximumKernelCachePixels = 8LL * 1024LL * 1024LL;
constexpr qreal HeatmapMaximumKernelRadius = 256.0;
constexpr int HeatmapMaximumColorBuckets = 64;
constexpr int HeatmapTileSize = 256;
const QColor NetworkColor(Qt::black);
const QColor SymbologyValueUnavailableColor(Qt::black);

const std::array<QColor, 7> SymbologyRampColors = {{
    QColor(QStringLiteral("#440154")),
    QColor(QStringLiteral("#443983")),
    QColor(QStringLiteral("#31688e")),
    QColor(QStringLiteral("#21918c")),
    QColor(QStringLiteral("#35b779")),
    QColor(QStringLiteral("#90d743")),
    QColor(QStringLiteral("#fde725"))
}};

struct NetworkRenderWorkers
{
    NetworkRenderWorkers()
    {
        this->thread_count = MapRetainedVectorRenderer::idealThreadCount();
        this->heatmap_dispatch_pool.setMaxThreadCount(1);
        this->heatmap_dispatch_pool.setExpiryTimeout(-1);
        this->heatmap_pool.setMaxThreadCount(qMax(1, this->thread_count / 3));
        this->heatmap_pool.setExpiryTimeout(-1);
    }

    QThreadPool heatmap_dispatch_pool;
    QThreadPool heatmap_pool;
    int thread_count = 1;
};

NetworkRenderWorkers &networkRenderWorkers()
{
    static NetworkRenderWorkers workers;
    return workers;
}

struct NetworkPreparationWorkers
{
    NetworkPreparationWorkers()
    {
        this->pool.setMaxThreadCount(qMax(1, qMin(2, MapRetainedVectorRenderer::idealThreadCount())));
        this->pool.setExpiryTimeout(-1);
    }

    QThreadPool pool;
};

NetworkPreparationWorkers &networkPreparationWorkers()
{
    static NetworkPreparationWorkers workers;
    return workers;
}

quint64 entityRenderKey(InfrastructureEntity entity_type, quint32 render_id)
{
    return (quint64(quint32(int(entity_type))) << 32) | quint64(render_id);
}

bool isFiniteCoordinate(const CoordinateWGS84 &coordinate)
{
    return std::isfinite(coordinate.longitude_deg) && std::isfinite(coordinate.latitude_deg);
}

qreal nodeSizeScale(int node_size_percent)
{
    return qBound<qreal>(0.5, node_size_percent / 100.0, 2.5);
}

qreal baseMarkerSizeForZoom(int zoom)
{
    return qBound<qreal>(10.0, 10.0 + (zoom - 16) * 10.0, 40.0);
}

qreal markerSizeForZoom(int zoom, int node_size_percent)
{
    return qMax<qreal>(5.0, baseMarkerSizeForZoom(zoom) * nodeSizeScale(node_size_percent));
}

qreal junctionDotDiameterForZoom(int zoom, int node_size_percent)
{
    const qreal base_diameter = qBound<qreal>(8.0, baseMarkerSizeForZoom(zoom) * 0.3, 12.0);
    return qMax<qreal>(4.0, base_diameter * nodeSizeScale(node_size_percent));
}

struct NetworkIconAsset
{
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    qreal view_width = 0.0;
    qreal view_height = 0.0;
    qreal stroke_width = 10.0;
    QPainterPath stroke_path;
    QPainterPath fill_path;
};

QPainterPath reservoirStrokePath()
{
    QPainterPath path;
    path.moveTo(5.0, 5.0);
    path.lineTo(23.0, 97.0);
    path.cubicTo(27.0, 117.0, 41.0, 133.0, 61.0, 133.0);
    path.lineTo(125.0, 133.0);
    path.cubicTo(145.0, 133.0, 159.0, 117.0, 163.0, 97.0);
    path.lineTo(181.0, 5.0);

    QPointF current(13.640777, 19.11651);
    path.moveTo(current);
    for (int index = 0; index < 4; ++index)
    {
        path.cubicTo(current + QPointF(11.0, 10.0),
                     current + QPointF(index == 1 ? 29.000003 : 29.0, 10.0),
                     current + QPointF(index == 1 ? 40.000003 : 40.0, 0.0));
        current = path.currentPosition();
    }
    return path;
}

QPainterPath tankStrokePath()
{
    QPainterPath path;
    path.addRoundedRect(QRectF(11.0, 22.0, 116.0, 120.0), 10.0, 10.0);
    path.moveTo(17.0, 22.0);
    path.cubicTo(23.0, 8.0, 115.0, 8.0, 121.0, 22.0);
    path.moveTo(49.0, 14.0);
    path.lineTo(89.0, 14.0);
    path.moveTo(49.0, 14.0);
    path.cubicTo(53.0, 2.0, 85.0, 2.0, 89.0, 14.0);
    path.moveTo(24.0, 142.0);
    path.lineTo(24.0, 178.0);
    path.moveTo(46.0, 142.0);
    path.lineTo(46.0, 178.0);
    path.moveTo(92.0, 142.0);
    path.lineTo(92.0, 178.0);
    path.moveTo(114.0, 142.0);
    path.lineTo(114.0, 178.0);
    path.moveTo(5.0, 178.0);
    path.lineTo(133.0, 178.0);
    return path;
}

NetworkIconAsset pumpIconAsset()
{
    NetworkIconAsset asset;
    asset.entity_type = InfrastructureEntity::Pump;
    asset.view_width = 126.0;
    asset.view_height = 110.0;
    asset.stroke_path.addRoundedRect(QRectF(5.0, 5.0, 116.0, 100.0), 18.0, 18.0);

    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(72);
    font.setBold(true);
    const QString text = QStringLiteral("P");
    const QFontMetricsF metrics(font);
    const QRectF bounds = metrics.boundingRect(text);
    const qreal x = 64.242722 - bounds.width() / 2.0 - bounds.left();
    asset.fill_path.addText(QPointF(x, 80.077667), font, text);
    return asset;
}

QPainterPath valveStrokePath()
{
    QPainterPath path;
    path.addEllipse(QRectF(5.0, 5.0, 128.0, 128.0));
    path.moveTo(25.0, 33.0);
    path.lineTo(69.0, 69.0);
    path.lineTo(113.0, 33.0);
    path.moveTo(25.0, 105.0);
    path.lineTo(69.0, 69.0);
    path.lineTo(113.0, 105.0);
    return path;
}

const std::array<NetworkIconAsset, 4> &networkIconAssets()
{
    static const std::array<NetworkIconAsset, 4> assets = []
    {
        std::array<NetworkIconAsset, 4> result;

        result[0].entity_type = InfrastructureEntity::Reservoir;
        result[0].view_width = 186.0;
        result[0].view_height = 138.0;
        result[0].stroke_path = reservoirStrokePath();

        result[1].entity_type = InfrastructureEntity::Tank;
        result[1].view_width = 138.0;
        result[1].view_height = 183.0;
        result[1].stroke_path = tankStrokePath();

        result[2] = pumpIconAsset();

        result[3].entity_type = InfrastructureEntity::Valve;
        result[3].view_width = 138.0;
        result[3].view_height = 138.0;
        result[3].stroke_path = valveStrokePath();
        return result;
    }();
    return assets;
}

const NetworkIconAsset *iconAssetForEntity(InfrastructureEntity entity_type)
{
    const std::array<NetworkIconAsset, 4> &assets = networkIconAssets();
    for (const NetworkIconAsset &asset : assets)
    {
        if (asset.entity_type == entity_type)
            return &asset;
    }
    return nullptr;
}

QSizeF markerScreenSize(InfrastructureEntity entity_type, int zoom, int node_size_percent, int icon_size_percent)
{
    const NetworkIconAsset *asset = iconAssetForEntity(entity_type);
    const qreal marker_size = markerSizeForZoom(zoom, asset ? icon_size_percent : node_size_percent);
    if (!asset)
        return QSizeF(marker_size, marker_size);

    const qreal maximum_view_dimension = qMax(asset->view_width, asset->view_height);
    return QSizeF(
        marker_size * asset->view_width / maximum_view_dimension,
        marker_size * asset->view_height / maximum_view_dimension);
}

QPointF polylineMidpoint(const QList<QPointF> &vertices)
{
    if (vertices.isEmpty())
        return QPointF();
    if (vertices.size() == 1)
        return vertices.constFirst();

    qreal total_length = 0.0;
    for (qsizetype index = 1; index < vertices.size(); ++index)
        total_length += QLineF(vertices.at(index - 1), vertices.at(index)).length();
    if (total_length <= 0.0)
        return vertices.constFirst();

    const qreal target_length = total_length / 2.0;
    qreal accumulated_length = 0.0;
    for (qsizetype index = 1; index < vertices.size(); ++index)
    {
        const QPointF &start = vertices.at(index - 1);
        const QPointF &end = vertices.at(index);
        const qreal segment_length = QLineF(start, end).length();
        if (accumulated_length + segment_length < target_length || segment_length <= 0.0)
        {
            accumulated_length += segment_length;
            continue;
        }

        const qreal ratio = (target_length - accumulated_length) / segment_length;
        return QPointF(
            start.x() + (end.x() - start.x()) * ratio,
            start.y() + (end.y() - start.y()) * ratio);
    }
    return vertices.constLast();
}

QHash<QUuid, double> nodeValues(const NetworkHydraulic &network_hydraulic, VisualNode visual_node)
{
    QHash<QUuid, double> values;

    switch (visual_node)
    {
    case VisualNode::Elevation:
        values.reserve(network_hydraulic.nodes_junctions.size() +
                       network_hydraulic.nodes_reservoirs.size() +
                       network_hydraulic.nodes_tanks.size());
        for (const HydraulicNodeJunction &junction : network_hydraulic.nodes_junctions)
            values.insert(junction.uuid, junction.elevation_m);
        for (const HydraulicNodeReservoir &reservoir : network_hydraulic.nodes_reservoirs)
            values.insert(reservoir.uuid, reservoir.head_m);
        for (const HydraulicNodeTank &tank : network_hydraulic.nodes_tanks)
            values.insert(tank.uuid, tank.bottom_elevation_m);
        break;
    case VisualNode::BaseDemand:
        values.reserve(network_hydraulic.nodes_junctions.size());
        for (const HydraulicNodeJunction &junction : network_hydraulic.nodes_junctions)
        {
            double base_demand_m3_per_h = 0.0;
            for (const HydraulicNodeJunctionDemand &demand : junction.demands)
                base_demand_m3_per_h += demand.base_demand_m3_per_h;
            values.insert(junction.uuid, base_demand_m3_per_h);
        }
        break;
    case VisualNode::None:
    case VisualNode::TotalDemand:
    case VisualNode::DemandDeficit:
    case VisualNode::EmitterFlow:
    case VisualNode::Leakage:
    case VisualNode::Head:
    case VisualNode::Pressure:
    case VisualNode::Chlorine:
    case VisualNode::RiverWater:
    case VisualNode::LakeWater:
        break;
    }

    return values;
}

QHash<QUuid, double> linkValues(const NetworkHydraulic &network_hydraulic, VisualLink visual_link)
{
    QHash<QUuid, double> values;

    switch (visual_link)
    {
    case VisualLink::Diameter:
        values.reserve(network_hydraulic.links_pipes.size());
        for (const HydraulicLinkPipe &pipe : network_hydraulic.links_pipes)
            values.insert(pipe.uuid, pipe.diameter_mm);
        break;
    case VisualLink::Length:
        values.reserve(network_hydraulic.links_pipes.size());
        for (const HydraulicLinkPipe &pipe : network_hydraulic.links_pipes)
            values.insert(pipe.uuid, pipe.length_measured_m.value_or(pipe.length_calculated_m));
        break;
    case VisualLink::Roughness:
        values.reserve(network_hydraulic.links_pipes.size());
        for (const HydraulicLinkPipe &pipe : network_hydraulic.links_pipes)
            values.insert(pipe.uuid, pipe.roughness_hw);
        break;
    case VisualLink::None:
    case VisualLink::FlowRate:
    case VisualLink::Velocity:
    case VisualLink::HeadLoss:
    case VisualLink::Leakage:
    case VisualLink::Chlorine:
    case VisualLink::RiverWater:
    case VisualLink::LakeWater:
        break;
    }

    return values;
}

QHash<QUuid, double> heatmapValues(const NetworkHydraulic &network_hydraulic, VisualHeatmap visual_heatmap)
{
    switch (visual_heatmap)
    {
    case VisualHeatmap::Elevation:
        return nodeValues(network_hydraulic, VisualNode::Elevation);
    case VisualHeatmap::BaseDemand:
        return nodeValues(network_hydraulic, VisualNode::BaseDemand);
    case VisualHeatmap::TotalDemand:
        return nodeValues(network_hydraulic, VisualNode::TotalDemand);
    case VisualHeatmap::DemandDeficit:
        return nodeValues(network_hydraulic, VisualNode::DemandDeficit);
    case VisualHeatmap::EmitterFlow:
        return nodeValues(network_hydraulic, VisualNode::EmitterFlow);
    case VisualHeatmap::Leakage:
        return nodeValues(network_hydraulic, VisualNode::Leakage);
    case VisualHeatmap::Head:
        return nodeValues(network_hydraulic, VisualNode::Head);
    case VisualHeatmap::Pressure:
        return nodeValues(network_hydraulic, VisualNode::Pressure);
    case VisualHeatmap::Chlorine:
        return nodeValues(network_hydraulic, VisualNode::Chlorine);
    case VisualHeatmap::RiverWater:
        return nodeValues(network_hydraulic, VisualNode::RiverWater);
    case VisualHeatmap::LakeWater:
        return nodeValues(network_hydraulic, VisualNode::LakeWater);
    case VisualHeatmap::None:
        break;
    }

    return QHash<QUuid, double>();
}

QColor interpolatedRampColor(double fraction)
{
    const double limited_fraction = qBound(0.0, fraction, 1.0);
    const double scaled = limited_fraction * (SymbologyRampColors.size() - 1);
    const int left_index = qMin(int(SymbologyRampColors.size()) - 1, int(std::floor(scaled)));
    const int right_index = qMin(int(SymbologyRampColors.size()) - 1, left_index + 1);
    const double ratio = scaled - left_index;
    const QColor &left = SymbologyRampColors.at(left_index);
    const QColor &right = SymbologyRampColors.at(right_index);
    return QColor(
        qRound(left.red() + (right.red() - left.red()) * ratio),
        qRound(left.green() + (right.green() - left.green()) * ratio),
        qRound(left.blue() + (right.blue() - left.blue()) * ratio));
}

QColor rampColor(double fraction)
{
    const double limited_fraction = qBound(0.0, fraction, 1.0);
    const int bucket = qRound(limited_fraction * (SymbologyColorBucketCount - 1));
    return interpolatedRampColor(double(bucket) / double(SymbologyColorBucketCount - 1));
}

QRgb symbologyColor(double value, double minimum, double maximum)
{
    if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum))
        return SymbologyValueUnavailableColor.rgb();
    if (minimum == maximum)
        return rampColor(0.5).rgb();
    return rampColor((value - minimum) / (maximum - minimum)).rgb();
}

double heatmapValueFraction(double value, double minimum, double maximum)
{
    if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum))
        return std::numeric_limits<double>::quiet_NaN();
    if (minimum == maximum)
        return 0.5;
    return qBound(0.0, (value - minimum) / (maximum - minimum), 1.0);
}

qreal heatmapRadiusReferencePixels(const QRectF &geometry_bounds, int radius_m)
{
    if (geometry_bounds.isEmpty())
        return 0.0;

    const double center_latitude = GeoWebMercator::worldPixelToLonLat(
        0.0, geometry_bounds.center().y(),
        MapRenderCacheMath::ReferenceZoom).latitude_deg;
    const double meters_per_pixel = std::max(0.000001,
        GeoWebMercator::metersPerPixel(
            center_latitude, MapRenderCacheMath::ReferenceZoom));
    return qMax<qreal>(1.0, qBound(10, radius_m, 1000) / meters_per_pixel);
}

struct HeatmapRasterDimensions
{
    QSize size;
    qreal scale = 1.0;
};

HeatmapRasterDimensions boundedHeatmapRasterDimensions(const QSize &display_size)
{
    HeatmapRasterDimensions result;
    if (!display_size.isValid())
        return result;

    const qreal display_area = qMax<qreal>(1.0, qreal(display_size.width()) * display_size.height());
    const qreal factor = std::min({
        qreal(1.0),
        HeatmapMaximumDimension / qMax<qreal>(1.0, display_size.width()),
        HeatmapMaximumDimension / qMax<qreal>(1.0, display_size.height()),
        std::sqrt(HeatmapMaximumArea / display_area)
    });
    result.scale = qMax<qreal>(0.000001, factor);
    result.size = QSize(
        qMax(1, qFloor(display_size.width() * result.scale)),
        qMax(1, qFloor(display_size.height() * result.scale)));
    return result;
}

qreal heatmapKernelRadius(qreal radius)
{
    return qBound<qreal>(1.0, radius, HeatmapMaximumKernelRadius);
}

int heatmapColorBucketCount(qreal radius)
{
    const qreal kernel_radius = heatmapKernelRadius(radius);
    const int diameter = qMax(3, qCeil(kernel_radius * 2.0) + 2);
    const int maximum_by_memory = qMax(1, int(HeatmapMaximumKernelCachePixels / (qint64(diameter) * diameter)));
    return qMax(int(SymbologyRampColors.size()), qMin(HeatmapMaximumColorBuckets, maximum_by_memory));
}

QImage createHeatmapKernel(qreal radius, const QColor &color, int solid_center_percent)
{
    const qreal kernel_radius = heatmapKernelRadius(qMax<qreal>(1.0, radius));
    const int diameter = qMax(3, qCeil(kernel_radius * 2.0) + 2);
    const qreal center = diameter / 2.0;
    QImage image(QSize(diameter, diameter), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return QImage();
    image.fill(Qt::transparent);

    const qreal solid_center_fraction = qBound<qreal>(0.0, solid_center_percent / 100.0, 0.9);
    const qreal half_opacity_fraction = solid_center_fraction +
        (1.0 - solid_center_fraction) * 0.4375;
    QRadialGradient gradient(QPointF(center, center), kernel_radius);
    QColor full_color = color;
    full_color.setAlpha(255);
    QColor half_color = color;
    half_color.setAlpha(128);
    QColor transparent_color = color;
    transparent_color.setAlpha(0);
    gradient.setColorAt(0.0, full_color);
    gradient.setColorAt(solid_center_fraction, full_color);
    gradient.setColorAt(half_opacity_fraction, half_color);
    gradient.setColorAt(1.0, transparent_color);

    QPainter painter(&image);
    painter.fillRect(image.rect(), QBrush(gradient));
    painter.end();
    return image;
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

    networkIconAssets();

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
    connect(this->hydraulic_data, &HydraulicData::signalNodeChanged, this,
        [this](InfrastructureEntity, const QUuid &)
    {
        if (!this->rendering_active)
            return;
        const bool rebuild_node_colors =
            this->symbology_settings.visual_node != VisualNode::None;
        const bool rebuild_heatmap =
            this->symbology_settings.visual_heatmap != VisualHeatmap::None;
        if (!rebuild_node_colors && !rebuild_heatmap)
            return;

        this->symbology_ranges =
            this->hydraulic_data->symbologyRanges(this->symbology_settings);
        requestSymbologyPreparation(true);
    });
    connect(this->hydraulic_data, &HydraulicData::signalLinkChanged, this,
        [this](InfrastructureEntity, const QUuid &)
    {
        if (!this->rendering_active)
            return;
        if (this->symbology_settings.visual_link == VisualLink::None)
            return;

        this->symbology_ranges =
            this->hydraulic_data->symbologyRanges(this->symbology_settings);
        requestSymbologyPreparation(true);
    });

    QTimer::singleShot(0, this, &MapNetworkOverlayWidget::syncSnapshot);
}

MapNetworkOverlayWidget::~MapNetworkOverlayWidget()
{
    this->rendering_active = false;
    if (this->pending_render_cancelled)
        this->pending_render_cancelled->store(true, std::memory_order_relaxed);
    if (this->geometry_prepare_cancelled)
        this->geometry_prepare_cancelled->store(true, std::memory_order_relaxed);
    if (this->symbology_prepare_cancelled)
        this->symbology_prepare_cancelled->store(true, std::memory_order_relaxed);
}

int MapNetworkOverlayWidget::backgroundOpacity() const
{
    return this->background_opacity;
}

void MapNetworkOverlayWidget::setSelectedEntity(const NetworkOverlayHit &hit)
{
    const NetworkOverlayHit selected = hit.isValid() ? hit : NetworkOverlayHit();
    if (this->selected_entity.render_id == selected.render_id &&
        this->selected_entity.entity_type == selected.entity_type)
    {
        return;
    }

    this->selected_entity = selected;
    update();
}

void MapNetworkOverlayWidget::clearSelectedEntity()
{
    setSelectedEntity(NetworkOverlayHit());
}

void MapNetworkOverlayWidget::setBackgroundOpacity(int opacity)
{
    const int bounded_opacity = qBound(0, opacity, 100);
    if (this->background_opacity == bounded_opacity)
        return;

    this->background_opacity = bounded_opacity;
    update();
}

void MapNetworkOverlayWidget::setSymbology(
    const NetworkSymbologySettings &settings, const NetworkSymbologyRanges &ranges)
{
    const NetworkSymbologySettings bounded_settings = settings.bounded();
    const bool node_visual_changed =
        this->symbology_settings.visual_node != bounded_settings.visual_node;
    const bool link_visual_changed =
        this->symbology_settings.visual_link != bounded_settings.visual_link;
    const bool heatmap_visual_changed =
        this->symbology_settings.visual_heatmap != bounded_settings.visual_heatmap;
    const bool node_size_changed =
        this->symbology_settings.node_size_percent != bounded_settings.node_size_percent;
    const bool icon_size_changed =
        this->symbology_settings.icon_size_percent != bounded_settings.icon_size_percent;
    const bool link_thickness_changed =
        this->symbology_settings.link_thickness_px != bounded_settings.link_thickness_px;
    const bool heatmap_opacity_changed =
        this->symbology_settings.heatmap_opacity != bounded_settings.heatmap_opacity;
    const bool heatmap_radius_changed =
        this->symbology_settings.heatmap_radius_m != bounded_settings.heatmap_radius_m;
    const bool heatmap_solid_center_changed =
        this->symbology_settings.heatmap_solid_center_percent !=
        bounded_settings.heatmap_solid_center_percent;
    const bool node_range_changed =
        this->symbology_ranges.node_minimum != ranges.node_minimum ||
        this->symbology_ranges.node_maximum != ranges.node_maximum;
    const bool link_range_changed =
        this->symbology_ranges.link_minimum != ranges.link_minimum ||
        this->symbology_ranges.link_maximum != ranges.link_maximum;
    const bool heatmap_range_changed =
        this->symbology_ranges.heatmap_minimum != ranges.heatmap_minimum ||
        this->symbology_ranges.heatmap_maximum != ranges.heatmap_maximum;

    const bool values_changed = node_visual_changed || link_visual_changed ||
        heatmap_visual_changed ||
        (bounded_settings.visual_node != VisualNode::None && node_range_changed) ||
        (bounded_settings.visual_link != VisualLink::None && link_range_changed) ||
        (bounded_settings.visual_heatmap != VisualHeatmap::None && heatmap_range_changed);
    const bool cached_render_changed = values_changed || node_size_changed ||
        icon_size_changed || link_thickness_changed ||
        (bounded_settings.visual_heatmap != VisualHeatmap::None &&
            (heatmap_radius_changed || heatmap_solid_center_changed));
    const bool any_change = cached_render_changed || heatmap_opacity_changed ||
        heatmap_radius_changed || heatmap_solid_center_changed ||
        node_range_changed || link_range_changed || heatmap_range_changed;

    if (!any_change)
        return;

    this->symbology_settings = bounded_settings;
    this->symbology_ranges = ranges;

    if (!cached_render_changed)
    {
        update();
        return;
    }

    if (bounded_settings.visual_heatmap == VisualHeatmap::None)
        this->rendered_heatmap_cache = QImage();
    requestSymbologyPreparation(values_changed || !this->render_symbology);
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
        world_position.x(), world_position.y(),
        markerSizeForZoom(this->map_model->zoom(),
            this->symbology_settings.node_size_percent) / (2.0 * scale),
        markerSizeForZoom(this->map_model->zoom(),
            this->symbology_settings.icon_size_percent) / (2.0 * scale));
    if (marker_hit.isValid())
        return marker_hit;

    const qreal link_hit_distance = qMax(
        LinkHitDistance, this->symbology_settings.link_thickness_px / 2.0 + 3.0) / scale;
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
        paintSelectedEntity(painter);
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
    if ((this->snapshot_initialized || this->active_geometry_prepare_request_id != 0) &&
        this->geometry_revision == current_geometry_revision)
    {
        return;
    }

    this->geometry_revision = current_geometry_revision;
    this->selected_entity = NetworkOverlayHit();
    this->snapshot_initialized = false;
    this->reference_geometry_ready = false;
    this->render_symbology.reset();

    if (this->pending_render_cancelled)
        this->pending_render_cancelled->store(true, std::memory_order_relaxed);

    requestGeometryPreparation();
    update();
}

void MapNetworkOverlayWidget::requestGeometryPreparation()
{
    if (this->geometry_prepare_cancelled)
        this->geometry_prepare_cancelled->store(true, std::memory_order_relaxed);

    const quint64 request_id = ++this->next_geometry_prepare_request_id;
    this->active_geometry_prepare_request_id = request_id;
    this->geometry_prepare_cancelled = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancelled = this->geometry_prepare_cancelled;
    const NetworkHydraulic network_copy = this->hydraulic_data->networkHydraulic();
    const quint64 geometry_revision = this->geometry_revision;
    const quint64 visual_revision = this->hydraulic_data->visualRevision();
    const QPointer<MapNetworkOverlayWidget> widget(this);

    QRunnable *runnable = QRunnable::create([widget, request_id, network_copy, geometry_revision,
        visual_revision, cancelled]
    {
        NetworkRenderSnapshot snapshot_copy = buildNetworkRenderSnapshot(
            network_copy, geometry_revision, visual_revision);
        if (cancelled->load(std::memory_order_relaxed))
            return;
        PreparedGeometry result = MapNetworkOverlayWidget::prepareGeometry(snapshot_copy, cancelled);
        result.snapshot = std::move(snapshot_copy);
        if (cancelled->load(std::memory_order_relaxed))
            return;

        QCoreApplication *application = QCoreApplication::instance();
        if (!application)
            return;

        QMetaObject::invokeMethod(application, [widget, request_id, result = std::move(result)]() mutable
        {
            if (!widget)
                return;
            widget->applyPreparedGeometry(request_id, std::move(result));
        }, Qt::QueuedConnection);
    });
    networkPreparationWorkers().pool.start(runnable);
}

MapNetworkOverlayWidget::PreparedGeometry MapNetworkOverlayWidget::prepareGeometry(
    const NetworkRenderSnapshot &snapshot,
    const std::shared_ptr<std::atomic_bool> &cancelled)
{
    PreparedGeometry result;
    result.geometry_revision = snapshot.geometry_revision;
    std::shared_ptr<RenderGeometry> geometry = std::make_shared<RenderGeometry>();
    QHash<quint32, QPointF> node_positions;
    double anchor_x = std::numeric_limits<double>::quiet_NaN();
    double minimum_x = std::numeric_limits<double>::infinity();
    double minimum_y = std::numeric_limits<double>::infinity();
    double maximum_x = -std::numeric_limits<double>::infinity();
    double maximum_y = -std::numeric_limits<double>::infinity();

    geometry->geometry_revision = snapshot.geometry_revision;
    geometry->markers.reserve(snapshot.nodes.size() + snapshot.links.size());
    node_positions.reserve(snapshot.nodes.size());
    result.hit_markers.reserve(snapshot.nodes.size());

    int processed_items = 0;
    for (const NetworkRenderNode &node : snapshot.nodes)
    {
        if ((++processed_items & 255) == 0 && cancelled &&
            cancelled->load(std::memory_order_relaxed))
        {
            return PreparedGeometry();
        }
        if (!isFiniteCoordinate(node.coordinate_wgs84))
            continue;

        const QPointF raw_world_position = GeoWebMercator::lonLatToWorldPixel(
            GeoWebMercator::normalizeLongitude(node.coordinate_wgs84.longitude_deg),
            node.coordinate_wgs84.latitude_deg,
            MapRenderCacheMath::ReferenceZoom);
        if (!std::isfinite(anchor_x))
            anchor_x = raw_world_position.x();

        const QPointF world_position(
            GeoWebMercator::nearestWrappedWorldPixelX(
                raw_world_position.x(), anchor_x,
                MapRenderCacheMath::ReferenceZoom),
            raw_world_position.y());
        RenderGeometry::Marker render_marker;
        render_marker.render_id = node.render_id;
        render_marker.entity_type = node.entity_type;
        render_marker.world_position = world_position;
        geometry->markers.append(render_marker);
        geometry->marker_indices_by_entity.insert(
            entityRenderKey(node.entity_type, node.render_id), geometry->markers.size() - 1);
        node_positions.insert(node.render_id, world_position);

        HitMarker marker;
        marker.render_id = node.render_id;
        marker.entity_type = node.entity_type;
        marker.uuid = node.uuid;
        marker.world_position = world_position;
        result.hit_markers.append(marker);

        minimum_x = std::min(minimum_x, world_position.x());
        minimum_y = std::min(minimum_y, world_position.y());
        maximum_x = std::max(maximum_x, world_position.x());
        maximum_y = std::max(maximum_y, world_position.y());
    }

    for (const NetworkRenderLink &link : snapshot.links)
    {
        if ((++processed_items & 255) == 0 && cancelled &&
            cancelled->load(std::memory_order_relaxed))
        {
            return PreparedGeometry();
        }

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
                MapRenderCacheMath::ReferenceZoom);
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
            ? &result.pipe_hit_segments : &result.device_hit_segments;
        for (qsizetype index = 1; index < world_vertices.size(); ++index)
        {
            const QPointF &start = world_vertices.at(index - 1);
            const QPointF &end = world_vertices.at(index);
            RenderGeometry::Segment render_segment;
            render_segment.render_id = link.render_id;
            render_segment.entity_type = link.entity_type;
            render_segment.line = QLineF(start, end);
            geometry->link_segments.append(render_segment);
            geometry->segment_indices_by_entity[
                entityRenderKey(link.entity_type, link.render_id)].append(
                    geometry->link_segments.size() - 1);

            HitSegment segment;
            segment.render_id = link.render_id;
            segment.entity_type = link.entity_type;
            segment.uuid = link.uuid;
            segment.start = start;
            segment.end = end;
            hit_segments->append(segment);
        }

        if (InfrastructureEntityTraits::isHydraulicDeviceLink(link.entity_type))
        {
            const QPointF center = polylineMidpoint(world_vertices);

            RenderGeometry::Marker render_marker;
            render_marker.render_id = link.render_id;
            render_marker.entity_type = link.entity_type;
            render_marker.world_position = center;
            geometry->markers.append(render_marker);
            geometry->marker_indices_by_entity.insert(
                entityRenderKey(link.entity_type, link.render_id), geometry->markers.size() - 1);

            HitMarker marker;
            marker.render_id = link.render_id;
            marker.entity_type = link.entity_type;
            marker.uuid = link.uuid;
            marker.world_position = center;
            result.hit_markers.append(marker);
        }
    }

    if ((geometry->markers.isEmpty() && geometry->link_segments.isEmpty()) ||
        !std::isfinite(minimum_x) || !std::isfinite(minimum_y) ||
        !std::isfinite(maximum_x) || !std::isfinite(maximum_y))
    {
        return result;
    }

    geometry->world_bounds = QRectF(
        minimum_x,
        minimum_y,
        maximum_x - minimum_x,
        maximum_y - minimum_y);
    geometry->world_origin = geometry->world_bounds.center();
    result.geometry = geometry;

    for (int index = 0; index < result.hit_markers.size(); ++index)
    {
        const HitMarker &marker = result.hit_markers.at(index);
        const quint64 key = spatialCellKey(
            spatialCellCoordinate(marker.world_position.x()),
            spatialCellCoordinate(marker.world_position.y()));
        result.spatial_cells[key].marker_indices.append(index);
    }

    const std::function<void(const QList<HitSegment> &, HitCollection)> add_segments =
        [&result](const QList<HitSegment> &segments, HitCollection collection)
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
                    result.global_device_segment_indices.append(index);
                else
                    result.global_pipe_segment_indices.append(index);
                continue;
            }

            for (int cell_y = minimum_cell_y; cell_y <= maximum_cell_y; ++cell_y)
            {
                for (int cell_x = minimum_cell_x; cell_x <= maximum_cell_x; ++cell_x)
                {
                    SpatialCell &cell = result.spatial_cells[spatialCellKey(cell_x, cell_y)];
                    if (collection == HitCollection::DeviceSegments)
                        cell.device_segment_indices.append(index);
                    else
                        cell.pipe_segment_indices.append(index);
                }
            }
        }
    };

    add_segments(result.device_hit_segments, HitCollection::DeviceSegments);
    add_segments(result.pipe_hit_segments, HitCollection::PipeSegments);
    return result;
}

void MapNetworkOverlayWidget::applyPreparedGeometry(quint64 request_id, PreparedGeometry result)
{
    if (request_id != this->active_geometry_prepare_request_id ||
        result.geometry_revision != this->geometry_revision)
    {
        return;
    }

    this->active_geometry_prepare_request_id = 0;
    this->geometry_prepare_cancelled.reset();
    this->snapshot = std::move(result.snapshot);
    this->snapshot_initialized = true;
    if (!result.geometry)
    {
        this->render_geometry.reset();
        this->hit_markers.clear();
        this->device_hit_segments.clear();
        this->pipe_hit_segments.clear();
        this->global_device_segment_indices.clear();
        this->global_pipe_segment_indices.clear();
        this->spatial_cells.clear();
        this->reference_geometry_ready = false;
        clearRenderedCache();
        update();
        return;
    }

    this->render_geometry = std::move(result.geometry);
    this->hit_markers = std::move(result.hit_markers);
    this->device_hit_segments = std::move(result.device_hit_segments);
    this->pipe_hit_segments = std::move(result.pipe_hit_segments);
    this->global_device_segment_indices = std::move(result.global_device_segment_indices);
    this->global_pipe_segment_indices = std::move(result.global_pipe_segment_indices);
    this->spatial_cells = std::move(result.spatial_cells);
    this->reference_geometry_ready = true;

    this->symbology_ranges =
        this->hydraulic_data->symbologyRanges(this->symbology_settings);
    requestSymbologyPreparation(true);
    update();
}

void MapNetworkOverlayWidget::requestSymbologyPreparation(bool force_values)
{
    if (!this->snapshot_initialized)
        return;

    const bool values_current = this->render_symbology &&
        this->render_symbology->visual_node == this->symbology_settings.visual_node &&
        this->render_symbology->visual_link == this->symbology_settings.visual_link &&
        this->render_symbology->visual_heatmap == this->symbology_settings.visual_heatmap;

    if (!force_values && values_current && this->active_symbology_prepare_request_id == 0)
    {
        if (this->symbology_prepare_cancelled)
            this->symbology_prepare_cancelled->store(true, std::memory_order_relaxed);
        this->active_symbology_prepare_request_id = 0;
        this->symbology_prepare_cancelled.reset();

        std::shared_ptr<RenderSymbology> symbology =
            std::make_shared<RenderSymbology>(*this->render_symbology);
        symbology->revision = ++this->symbology_revision;
        symbology->node_size_percent = this->symbology_settings.node_size_percent;
        symbology->icon_size_percent = this->symbology_settings.icon_size_percent;
        symbology->link_width = qreal(this->symbology_settings.link_thickness_px);
        symbology->heatmap_radius_m = this->symbology_settings.heatmap_radius_m;
        symbology->heatmap_solid_center_percent =
            this->symbology_settings.heatmap_solid_center_percent;
        this->render_symbology = symbology;
        requestRenderCache(true);
        update();
        return;
    }

    if (this->symbology_prepare_cancelled)
        this->symbology_prepare_cancelled->store(true, std::memory_order_relaxed);

    const quint64 request_id = ++this->next_symbology_prepare_request_id;
    const quint64 geometry_revision = this->geometry_revision;
    const quint64 symbology_revision = ++this->symbology_revision;
    this->active_symbology_prepare_request_id = request_id;
    this->symbology_prepare_cancelled = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancelled = this->symbology_prepare_cancelled;

    const NetworkRenderSnapshot snapshot_copy = this->snapshot;
    const NetworkHydraulic network_copy = this->hydraulic_data->networkHydraulic();
    const NetworkSymbologySettings settings = this->symbology_settings;
    const NetworkSymbologyRanges ranges = this->symbology_ranges;
    const QPointer<MapNetworkOverlayWidget> widget(this);

    QRunnable *runnable = QRunnable::create([widget, request_id, geometry_revision,
        symbology_revision, snapshot_copy, network_copy, settings, ranges, cancelled]
    {
        if (cancelled->load(std::memory_order_relaxed))
            return;

        std::shared_ptr<RenderSymbology> symbology = std::make_shared<RenderSymbology>();
        symbology->revision = symbology_revision;
        symbology->visual_node = settings.visual_node;
        symbology->visual_link = settings.visual_link;
        symbology->visual_heatmap = settings.visual_heatmap;
        symbology->node_size_percent = settings.node_size_percent;
        symbology->icon_size_percent = settings.icon_size_percent;
        symbology->link_width = qreal(settings.link_thickness_px);
        symbology->heatmap_radius_m = settings.heatmap_radius_m;
        symbology->heatmap_solid_center_percent = settings.heatmap_solid_center_percent;

        symbology->node_colors.reserve(snapshot_copy.nodes.size());
        if (settings.visual_node == VisualNode::None)
        {
            for (const NetworkRenderNode &node : snapshot_copy.nodes)
                symbology->node_colors.insert(node.render_id, NetworkColor.rgb());
        }
        else
        {
            const QHash<QUuid, double> values =
                nodeValues(network_copy, settings.visual_node);
            for (const NetworkRenderNode &node : snapshot_copy.nodes)
            {
                const QHash<QUuid, double>::const_iterator iterator = values.constFind(node.uuid);
                const QRgb color = iterator == values.cend()
                    ? SymbologyValueUnavailableColor.rgb()
                    : symbologyColor(
                        iterator.value(), ranges.node_minimum, ranges.node_maximum);
                symbology->node_colors.insert(node.render_id, color);
            }
        }

        if (cancelled->load(std::memory_order_relaxed))
            return;

        symbology->link_colors.reserve(snapshot_copy.links.size());
        if (settings.visual_link == VisualLink::None)
        {
            for (const NetworkRenderLink &link : snapshot_copy.links)
                symbology->link_colors.insert(link.render_id, NetworkColor.rgb());
        }
        else
        {
            const QHash<QUuid, double> values =
                linkValues(network_copy, settings.visual_link);
            for (const NetworkRenderLink &link : snapshot_copy.links)
            {
                const QHash<QUuid, double>::const_iterator iterator = values.constFind(link.uuid);
                const QRgb color = iterator == values.cend()
                    ? SymbologyValueUnavailableColor.rgb()
                    : symbologyColor(
                        iterator.value(), ranges.link_minimum, ranges.link_maximum);
                symbology->link_colors.insert(link.render_id, color);
            }
        }

        if (cancelled->load(std::memory_order_relaxed))
            return;

        if (settings.visual_heatmap != VisualHeatmap::None)
        {
            const QHash<QUuid, double> values =
                heatmapValues(network_copy, settings.visual_heatmap);
            symbology->heatmap_fractions.reserve(snapshot_copy.nodes.size());
            for (const NetworkRenderNode &node : snapshot_copy.nodes)
            {
                const QHash<QUuid, double>::const_iterator iterator = values.constFind(node.uuid);
                if (iterator == values.cend())
                    continue;
                const double fraction = heatmapValueFraction(
                    iterator.value(), ranges.heatmap_minimum, ranges.heatmap_maximum);
                if (std::isfinite(fraction))
                    symbology->heatmap_fractions.insert(node.render_id, fraction);
            }
        }

        if (cancelled->load(std::memory_order_relaxed))
            return;

        QCoreApplication *application = QCoreApplication::instance();
        if (!application)
            return;
        QMetaObject::invokeMethod(application, [widget, request_id, geometry_revision, symbology]()
        {
            if (!widget || request_id != widget->active_symbology_prepare_request_id ||
                geometry_revision != widget->geometry_revision)
            {
                return;
            }
            widget->applyPreparedSymbology(request_id, geometry_revision, symbology);
        }, Qt::QueuedConnection);
    });
    networkPreparationWorkers().pool.start(runnable);
}

void MapNetworkOverlayWidget::applyPreparedSymbology(
    quint64 request_id, quint64 geometry_revision,
    std::shared_ptr<RenderSymbology> symbology)
{
    if (request_id != this->active_symbology_prepare_request_id ||
        geometry_revision != this->geometry_revision || !symbology ||
        symbology->visual_node != this->symbology_settings.visual_node ||
        symbology->visual_link != this->symbology_settings.visual_link ||
        symbology->visual_heatmap != this->symbology_settings.visual_heatmap)
    {
        return;
    }

    this->active_symbology_prepare_request_id = 0;
    this->symbology_prepare_cancelled.reset();
    this->render_symbology = std::move(symbology);
    if (this->symbology_settings.visual_heatmap == VisualHeatmap::None)
        this->rendered_heatmap_cache = QImage();
    requestRenderCache(true);
    update();
}

void MapNetworkOverlayWidget::clearRenderedCache()
{
    if (this->pending_render_cancelled)
        this->pending_render_cancelled->store(true, std::memory_order_relaxed);
    this->pending_render_request_id = 0;
    this->pending_cache_coverage_world_bounds = QRectF();
    this->pending_cache_zoom = -1;
    this->pending_cache_device_pixel_ratio = 0.0;
    this->pending_cache_symbology_revision = 0;
    this->render_restart_requested = this->render_worker_running;
    this->render_restart_force = this->render_worker_running;
    this->rendered_network_cache = QImage();
    this->rendered_heatmap_cache = QImage();
    this->rendered_cache_coverage_world_bounds = QRectF();
    this->rendered_cache_image_world_bounds = QRectF();
    this->rendered_cache_zoom = -1;
    this->rendered_cache_device_pixel_ratio = 0.0;
    this->rendered_cache_symbology_revision = 0;
}

void MapNetworkOverlayWidget::requestRenderCache(bool force)
{
    if (!this->rendering_active || !isVisible() ||
        !this->reference_geometry_ready || !this->render_geometry || !this->render_symbology ||
        width() <= 0 || height() <= 0)
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
    this->pending_cache_symbology_revision = request.symbology_revision;

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
    if (!this->render_geometry || !this->render_symbology || !this->reference_geometry_ready)
        return request;

    request.request_id = request_id;
    request.geometry_revision = this->geometry_revision;
    request.symbology_revision = this->render_symbology->revision;
    request.zoom = this->map_model->zoom();
    request.device_pixel_ratio = qMax<qreal>(1.0, devicePixelRatioF());
    request.geometry = this->render_geometry;
    request.symbology = this->render_symbology;

    const QSize cache_size = MapRenderCacheMath::boundedCacheLogicalSize(
        size(), request.device_pixel_ratio);
    if (!cache_size.isValid())
        return RenderRequest();

    const qreal scale = GeoWebMercator::zoomScale(
        request.zoom, MapRenderCacheMath::ReferenceZoom);
    if (scale <= 0.0)
        return RenderRequest();

    request.coverage_world_bounds = MapRenderCacheMath::centeredWorldRect(
        visibleReferenceWorldCenter(), cache_size, scale);

    const qreal render_half_width = qMax(
        qMax(markerSizeForZoom(request.zoom, request.symbology->node_size_percent) / 2.0,
             markerSizeForZoom(request.zoom, request.symbology->icon_size_percent) / 2.0),
        request.symbology->link_width / 2.0);
    const qreal heatmap_padding = request.symbology->heatmap_fractions.isEmpty()
        ? 0.0 : heatmapRadiusReferencePixels(
            this->render_geometry->world_bounds, request.symbology->heatmap_radius_m) + NetworkImagePadding / scale;
    const qreal geometry_padding = qMax(
        (render_half_width + NetworkImagePadding) / scale, heatmap_padding);
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

QImage MapNetworkOverlayWidget::renderHeatmap(const RenderRequest &request, qreal scale, qreal image_left, qreal image_top)
{
    if (!request.geometry || !request.symbology || request.symbology->heatmap_fractions.isEmpty())
        return QImage();
    if (request.cancelled && request.cancelled->load(std::memory_order_relaxed))
        return QImage();

    const HeatmapRasterDimensions raster = boundedHeatmapRasterDimensions(request.logical_size);
    if (!raster.size.isValid() || raster.scale <= 0.0)
        return QImage();

    const qreal radius_reference_pixels = heatmapRadiusReferencePixels(
        request.geometry->world_bounds, request.symbology->heatmap_radius_m);
    const qreal radius = qMax<qreal>(1.0, radius_reference_pixels * scale * raster.scale);
    const int color_bucket_count = heatmapColorBucketCount(radius);
    const qreal display_diameter = qMax<qreal>(3.0, qCeil(radius * 2.0) + 2.0);
    const qreal display_half = display_diameter / 2.0;

    struct HeatmapPoint
    {
        qreal x = 0.0;
        qreal y = 0.0;
        int bucket = 0;
    };

    std::vector<HeatmapPoint> points;
    points.reserve(request.geometry->markers.size());
    std::vector<bool> used_buckets(color_bucket_count, false);
    int processed_markers = 0;
    for (const RenderGeometry::Marker &marker : request.geometry->markers)
    {
        if ((++processed_markers & 255) == 0 && request.cancelled &&
            request.cancelled->load(std::memory_order_relaxed))
        {
            return QImage();
        }

        if (!InfrastructureEntityTraits::isHydraulicConnectionNode(marker.entity_type))
        {
            continue;
        }

        const QHash<quint32, double>::const_iterator fraction_iterator =
            request.symbology->heatmap_fractions.constFind(marker.render_id);
        if (fraction_iterator == request.symbology->heatmap_fractions.cend())
            continue;

        const qreal x = (marker.world_position.x() - image_left) * scale * raster.scale;
        const qreal y = (marker.world_position.y() - image_top) * scale * raster.scale;
        if (x + radius < 0.0 || y + radius < 0.0 ||
            x - radius > raster.size.width() || y - radius > raster.size.height())
        {
            continue;
        }

        HeatmapPoint point;
        point.x = x;
        point.y = y;
        point.bucket = qBound(0,
            qRound(fraction_iterator.value() * (color_bucket_count - 1)), color_bucket_count - 1);
        points.push_back(point);
        used_buckets[point.bucket] = true;
    }

    if (points.empty())
        return QImage();

    std::vector<QImage> kernels(color_bucket_count);
    for (int bucket = 0; bucket < color_bucket_count; ++bucket)
    {
        if (!used_buckets[bucket])
            continue;
        const double bucket_fraction = color_bucket_count <= 1
            ? 0.5 : double(bucket) / double(color_bucket_count - 1);
        kernels[bucket] = createHeatmapKernel(
            radius, interpolatedRampColor(bucket_fraction),
            request.symbology->heatmap_solid_center_percent);
        if (kernels[bucket].isNull())
            return QImage();
    }

    struct HeatmapTile
    {
        QRect rect;
        std::vector<int> point_indices;
    };

    const int tile_columns = qMax(1, (raster.size.width() + HeatmapTileSize - 1) / HeatmapTileSize);
    const int tile_rows = qMax(1, (raster.size.height() + HeatmapTileSize - 1) / HeatmapTileSize);
    const int tile_count = tile_columns * tile_rows;
    std::vector<HeatmapTile> tiles(tile_count);
    for (int tile_y = 0; tile_y < tile_rows; ++tile_y)
    {
        for (int tile_x = 0; tile_x < tile_columns; ++tile_x)
        {
            HeatmapTile &tile = tiles[tile_y * tile_columns + tile_x];
            const int left = tile_x * HeatmapTileSize;
            const int top = tile_y * HeatmapTileSize;
            tile.rect = QRect(left, top,
                qMin(HeatmapTileSize, raster.size.width() - left),
                qMin(HeatmapTileSize, raster.size.height() - top));
        }
    }

    for (int point_index = 0; point_index < int(points.size()); ++point_index)
    {
        const HeatmapPoint &point = points[point_index];
        const int minimum_tile_x = qBound(0,
            qFloor((point.x - display_half) / HeatmapTileSize), tile_columns - 1);
        const int maximum_tile_x = qBound(0,
            qFloor((point.x + display_half) / HeatmapTileSize), tile_columns - 1);
        const int minimum_tile_y = qBound(0,
            qFloor((point.y - display_half) / HeatmapTileSize), tile_rows - 1);
        const int maximum_tile_y = qBound(0,
            qFloor((point.y + display_half) / HeatmapTileSize), tile_rows - 1);

        for (int tile_y = minimum_tile_y; tile_y <= maximum_tile_y; ++tile_y)
        {
            for (int tile_x = minimum_tile_x; tile_x <= maximum_tile_x; ++tile_x)
                tiles[tile_y * tile_columns + tile_x].point_indices.push_back(point_index);
        }
    }

    NetworkRenderWorkers &workers = networkRenderWorkers();
    std::vector<QImage> tile_images(tile_count);
    QSemaphore completed_tiles;
    for (int tile_index = 0; tile_index < tile_count; ++tile_index)
    {
        QRunnable *runnable = QRunnable::create([&request, &tiles, &points, &kernels,
            &tile_images, &completed_tiles, display_diameter, tile_index]
        {
            const HeatmapTile &tile = tiles[tile_index];
            if (request.cancelled && request.cancelled->load(std::memory_order_relaxed))
            {
                completed_tiles.release();
                return;
            }

            QImage image(tile.rect.size(), QImage::Format_ARGB32_Premultiplied);
            if (!image.isNull())
            {
                image.fill(Qt::transparent);
                QPainter painter(&image);
                painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                for (int point_index : tile.point_indices)
                {
                    const HeatmapPoint &point = points[point_index];
                    const QImage &kernel = kernels[point.bucket];
                    painter.drawImage(QRectF(
                        point.x - display_diameter / 2.0 - tile.rect.left(),
                        point.y - display_diameter / 2.0 - tile.rect.top(),
                        display_diameter, display_diameter),
                        kernel, QRectF(kernel.rect()));
                }
                painter.end();
            }
            tile_images[tile_index] = std::move(image);
            completed_tiles.release();
        });
        workers.heatmap_pool.start(runnable);
    }

    for (int tile_index = 0; tile_index < tile_count; ++tile_index)
        completed_tiles.acquire();

    if (request.cancelled && request.cancelled->load(std::memory_order_relaxed))
        return QImage();

    QImage heatmap(raster.size, QImage::Format_ARGB32_Premultiplied);
    if (heatmap.isNull())
        return QImage();
    heatmap.fill(Qt::transparent);
    QPainter composition_painter(&heatmap);
    composition_painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    for (int tile_index = 0; tile_index < tile_count; ++tile_index)
    {
        if (!tile_images[tile_index].isNull())
            composition_painter.drawImage(tiles[tile_index].rect.topLeft(), tile_images[tile_index]);
    }
    composition_painter.end();
    return heatmap;
}

MapNetworkOverlayWidget::RenderResult MapNetworkOverlayWidget::renderRequest(const RenderRequest &request)
{
    RenderResult result;
    result.request_id = request.request_id;
    result.geometry_revision = request.geometry_revision;
    result.symbology_revision = request.symbology_revision;
    result.zoom = request.zoom;
    result.device_pixel_ratio = request.device_pixel_ratio;
    result.coverage_world_bounds = request.coverage_world_bounds;
    result.image_world_bounds = request.image_world_bounds;

    if (!request.geometry || !request.symbology || !request.logical_size.isValid() ||
        request.image_world_bounds.isEmpty())
    {
        return result;
    }
    if (request.cancelled && request.cancelled->load(std::memory_order_relaxed))
        return result;

    const qreal scale = GeoWebMercator::zoomScale(
        request.zoom, MapRenderCacheMath::ReferenceZoom);
    const qreal image_left = request.image_world_bounds.left();
    const qreal image_top = request.image_world_bounds.top();
    const qreal logical_width = request.logical_size.width();
    const qreal logical_height = request.logical_size.height();
    if (scale <= 0.0)
        return result;

    NetworkRenderWorkers &workers = networkRenderWorkers();
    const bool heatmap_active = !request.symbology->heatmap_fractions.isEmpty();
    const int heatmap_thread_count = heatmap_active && workers.thread_count > 1
        ? qMax(1, workers.thread_count / 3) : 0;
    const int vector_thread_count = qMax(1, workers.thread_count - heatmap_thread_count);
    workers.heatmap_pool.setMaxThreadCount(qMax(1, heatmap_thread_count));

    const std::vector<MapRetainedVectorRenderer::HorizontalBand> bands =
        MapRetainedVectorRenderer::createHorizontalBands(
            request.logical_size, request.device_pixel_ratio, vector_thread_count);
    if (bands.empty())
        return result;

    struct BandContent
    {
        std::vector<int> segment_indices;
        std::vector<int> marker_indices;
    };

    std::vector<BandContent> band_contents(bands.size());
    const int band_count = int(bands.size());
    const qreal link_padding = request.symbology->link_width / 2.0 + NetworkImagePadding;
    const std::function<int(qreal)> band_for_logical_y =
        [band_count, logical_height](qreal logical_y)
    {
        if (logical_height <= 0.0)
            return 0;
        const int band_index = qFloor(logical_y * band_count / logical_height);
        return qBound(0, band_index, band_count - 1);
    };

    for (int segment_index = 0; segment_index < request.geometry->link_segments.size(); ++segment_index)
    {
        if ((segment_index & 1023) == 0 && request.cancelled &&
            request.cancelled->load(std::memory_order_relaxed))
        {
            return result;
        }

        const RenderGeometry::Segment &render_segment = request.geometry->link_segments.at(segment_index);
        const QLineF &segment = render_segment.line;
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

        const int first_band = band_for_logical_y(minimum_y);
        const int last_band = band_for_logical_y(maximum_y);
        for (int band_index = first_band; band_index <= last_band; ++band_index)
            band_contents[band_index].segment_indices.push_back(segment_index);
    }

    for (int marker_index = 0; marker_index < request.geometry->markers.size(); ++marker_index)
    {
        if ((marker_index & 1023) == 0 && request.cancelled &&
            request.cancelled->load(std::memory_order_relaxed))
        {
            return result;
        }

        const RenderGeometry::Marker &marker = request.geometry->markers.at(marker_index);
        const QPointF &world_position = marker.world_position;
        const QSizeF marker_size = markerScreenSize(
            marker.entity_type, request.zoom, request.symbology->node_size_percent,
            request.symbology->icon_size_percent);
        const qreal marker_padding_x = marker_size.width() / 2.0 + NetworkImagePadding;
        const qreal marker_padding_y = marker_size.height() / 2.0 + NetworkImagePadding;
        const qreal x = (world_position.x() - image_left) * scale;
        const qreal y = (world_position.y() - image_top) * scale;
        if (x + marker_padding_x < 0.0 || x - marker_padding_x > logical_width ||
            y + marker_padding_y < 0.0 || y - marker_padding_y > logical_height)
        {
            continue;
        }

        const int first_band = band_for_logical_y(y - marker_padding_y);
        const int last_band = band_for_logical_y(y + marker_padding_y);
        for (int band_index = first_band; band_index <= last_band; ++band_index)
            band_contents[band_index].marker_indices.push_back(marker_index);
    }

    if (request.cancelled && request.cancelled->load(std::memory_order_relaxed))
        return result;

    QImage heatmap_image;
    QSemaphore heatmap_completed;
    if (heatmap_active)
    {
        QRunnable *heatmap_runnable = QRunnable::create(
            [&request, &heatmap_image, &heatmap_completed, scale, image_left, image_top]
        {
            heatmap_image = renderHeatmap(request, scale, image_left, image_top);
            heatmap_completed.release();
        });
        workers.heatmap_dispatch_pool.start(heatmap_runnable);
    }

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
            QHash<QRgb, QPainterPath> link_paths;
            QHash<QRgb, QPainterPath> junction_paths;
            QHash<quint64, QPainterPath> icon_stroke_paths;
            QHash<QRgb, QPainterPath> icon_fill_paths;
            link_paths.reserve(qMin(SymbologyColorBucketCount + 1, int(content.segment_indices.size())));
            junction_paths.reserve(qMin(SymbologyColorBucketCount + 1, int(content.marker_indices.size())));

            int processed_segments = 0;
            for (int segment_index : content.segment_indices)
            {
                if ((++processed_segments & 255) == 0 && request.cancelled &&
                    request.cancelled->load(std::memory_order_relaxed))
                {
                    return false;
                }

                const RenderGeometry::Segment &render_segment = request.geometry->link_segments.at(segment_index);
                const QLineF &segment = render_segment.line;
                const QRgb color = request.symbology->link_colors.value(
                    render_segment.render_id, NetworkColor.rgb());
                QPainterPath &link_path = link_paths[color];
                link_path.moveTo(QPointF(
                    (segment.x1() - image_left) * scale,
                    (segment.y1() - image_top) * scale - band.logical_top));
                link_path.lineTo(QPointF(
                    (segment.x2() - image_left) * scale,
                    (segment.y2() - image_top) * scale - band.logical_top));
            }

            const qreal junction_radius = junctionDotDiameterForZoom(
                request.zoom, request.symbology->node_size_percent) / 2.0;
            int processed_markers = 0;
            for (int marker_index : content.marker_indices)
            {
                if ((++processed_markers & 255) == 0 && request.cancelled &&
                    request.cancelled->load(std::memory_order_relaxed))
                {
                    return false;
                }

                const RenderGeometry::Marker &marker = request.geometry->markers.at(marker_index);
                const QPointF &world_position = marker.world_position;
                const bool node_entity =
                    InfrastructureEntityTraits::isHydraulicConnectionNode(marker.entity_type);
                const QRgb color = node_entity
                    ? request.symbology->node_colors.value(marker.render_id, NetworkColor.rgb())
                    : request.symbology->link_colors.value(marker.render_id, NetworkColor.rgb());
                const QPointF center(
                    (world_position.x() - image_left) * scale,
                    (world_position.y() - image_top) * scale - band.logical_top);
                const NetworkIconAsset *asset = iconAssetForEntity(marker.entity_type);
                if (!asset)
                {
                    junction_paths[color].addEllipse(center, junction_radius, junction_radius);
                    continue;
                }

                const qreal marker_size = markerSizeForZoom(
                    request.zoom, request.symbology->icon_size_percent);
                const qreal icon_scale = marker_size / qMax(asset->view_width, asset->view_height);
                const qreal icon_x = center.x() - asset->view_width * icon_scale / 2.0;
                const qreal icon_y = center.y() - asset->view_height * icon_scale / 2.0;
                const QTransform icon_transform(
                    icon_scale, 0.0, 0.0, icon_scale, icon_x, icon_y);
                const quint64 icon_key = (quint64(color) << 32) |
                    quint32(int(marker.entity_type));
                if (!asset->stroke_path.isEmpty())
                    icon_stroke_paths[icon_key].addPath(icon_transform.map(asset->stroke_path));
                if (!asset->fill_path.isEmpty())
                    icon_fill_paths[color].addPath(icon_transform.map(asset->fill_path));
            }

            for (QHash<QRgb, QPainterPath>::iterator iterator = link_paths.begin();
                 iterator != link_paths.end(); ++iterator)
            {
                QPen pen(QColor::fromRgb(iterator.key()));
                pen.setWidthF(request.symbology->link_width);
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);
                document.addStroke(std::move(iterator.value()), pen);
            }
            for (QHash<QRgb, QPainterPath>::iterator iterator = junction_paths.begin();
                 iterator != junction_paths.end(); ++iterator)
            {
                document.addFill(std::move(iterator.value()), QBrush(QColor::fromRgb(iterator.key())));
            }
            for (QHash<quint64, QPainterPath>::iterator iterator = icon_stroke_paths.begin();
                 iterator != icon_stroke_paths.end(); ++iterator)
            {
                const QRgb color = QRgb(iterator.key() >> 32);
                const InfrastructureEntity entity_type = static_cast<InfrastructureEntity>(
                    quint32(iterator.key()));
                const NetworkIconAsset *asset = iconAssetForEntity(entity_type);
                if (!asset)
                    continue;
                const qreal marker_size = markerSizeForZoom(
                    request.zoom, request.symbology->icon_size_percent);
                const qreal icon_scale = marker_size / qMax(asset->view_width, asset->view_height);
                QPen pen(QColor::fromRgb(color));
                pen.setWidthF(asset->stroke_width * icon_scale);
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);
                document.addStroke(std::move(iterator.value()), pen);
            }
            for (QHash<QRgb, QPainterPath>::iterator iterator = icon_fill_paths.begin();
                 iterator != icon_fill_paths.end(); ++iterator)
            {
                document.addFill(std::move(iterator.value()), QBrush(QColor::fromRgb(iterator.key())));
            }

            return !(request.cancelled && request.cancelled->load(std::memory_order_relaxed));
        },
        false,
        vector_thread_count);

    if (heatmap_active)
    {
        heatmap_completed.acquire();
        result.heatmap_image = std::move(heatmap_image);
    }

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
    this->pending_cache_symbology_revision = 0;

    const bool restart_requested = this->render_restart_requested;
    const bool restart_force = this->render_restart_force;
    this->render_restart_requested = false;
    this->render_restart_force = false;

    const bool result_matches_current_view =
        result.geometry_revision == this->geometry_revision &&
        result.symbology_revision == this->symbology_revision &&
        result.zoom == this->map_model->zoom() &&
        qFuzzyCompare(result.device_pixel_ratio, qMax<qreal>(1.0, devicePixelRatioF())) &&
        coverageCoversCurrentView(result.coverage_world_bounds, result.zoom);

    if (this->rendering_active && isVisible() && result_matches_current_view && !result.image.isNull())
    {
        this->rendered_network_cache = std::move(result.image);
        this->rendered_heatmap_cache = std::move(result.heatmap_image);
        this->rendered_cache_coverage_world_bounds = result.coverage_world_bounds;
        this->rendered_cache_image_world_bounds = result.image_world_bounds;
        this->rendered_cache_zoom = result.zoom;
        this->rendered_cache_device_pixel_ratio = result.device_pixel_ratio;
        this->rendered_cache_symbology_revision = result.symbology_revision;
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
        this->rendered_cache_symbology_revision != this->symbology_revision ||
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
        this->pending_cache_symbology_revision != this->symbology_revision ||
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

    const qreal scale = GeoWebMercator::zoomScale(
        zoom, MapRenderCacheMath::ReferenceZoom);
    if (scale <= 0.0)
        return false;

    return MapRenderCacheMath::coverageCoversView(
        coverage_world_bounds, visibleReferenceWorldRect(), scale, true);
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

    if (!this->rendered_heatmap_cache.isNull() &&
        this->symbology_settings.visual_heatmap != VisualHeatmap::None &&
        this->symbology_settings.heatmap_opacity > 0)
    {
        painter.save();
        painter.setOpacity(this->symbology_settings.heatmap_opacity / 100.0);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(target_rect, this->rendered_heatmap_cache);
        painter.restore();
    }

    painter.setRenderHint(QPainter::SmoothPixmapTransform,
        this->rendered_cache_zoom != this->map_model->zoom());
    painter.drawImage(target_rect, this->rendered_network_cache);
}

void MapNetworkOverlayWidget::paintSelectedEntity(QPainter &painter)
{
    if (!this->selected_entity.isValid() || !this->render_geometry)
        return;

    const qreal scale = referenceScaleForCurrentZoom();
    if (scale <= 0.0)
        return;

    const QPointF world_center = visibleReferenceWorldCenter();
    const quint64 key = entityRenderKey(
        this->selected_entity.entity_type, this->selected_entity.render_id);
    const QColor selected_color(0, 190, 255);

    const QList<int> segment_indices = this->render_geometry->segment_indices_by_entity.value(key);
    if (!segment_indices.isEmpty())
    {
        QPainterPath selected_path;
        for (int segment_index : segment_indices)
        {
            if (segment_index < 0 || segment_index >= this->render_geometry->link_segments.size())
                continue;
            const RenderGeometry::Segment &segment = this->render_geometry->link_segments.at(segment_index);
            const QPointF start(
                width() / 2.0 + (segment.line.x1() - world_center.x()) * scale,
                height() / 2.0 + (segment.line.y1() - world_center.y()) * scale);
            const QPointF end(
                width() / 2.0 + (segment.line.x2() - world_center.x()) * scale,
                height() / 2.0 + (segment.line.y2() - world_center.y()) * scale);
            selected_path.moveTo(start);
            selected_path.lineTo(end);
        }

        const qreal base_width = this->render_symbology
            ? this->render_symbology->link_width
            : qreal(this->symbology_settings.link_thickness_px);
        QPen selected_pen(selected_color);
        selected_pen.setWidthF(qMax<qreal>(3.0, base_width + 2.0));
        selected_pen.setCapStyle(Qt::RoundCap);
        selected_pen.setJoinStyle(Qt::RoundJoin);
        painter.strokePath(selected_path, selected_pen);
    }

    const QHash<quint64, int>::const_iterator marker_iterator =
        this->render_geometry->marker_indices_by_entity.constFind(key);
    if (marker_iterator == this->render_geometry->marker_indices_by_entity.cend())
        return;

    const int marker_index = marker_iterator.value();
    if (marker_index < 0 || marker_index >= this->render_geometry->markers.size())
        return;

    const RenderGeometry::Marker &marker = this->render_geometry->markers.at(marker_index);
    const QPointF center(
        width() / 2.0 + (marker.world_position.x() - world_center.x()) * scale,
        height() / 2.0 + (marker.world_position.y() - world_center.y()) * scale);
    const NetworkIconAsset *asset = iconAssetForEntity(marker.entity_type);
    if (!asset)
    {
        const qreal radius = junctionDotDiameterForZoom(
            this->map_model->zoom(), this->symbology_settings.node_size_percent) / 2.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(selected_color);
        painter.drawEllipse(center, radius + 2.0, radius + 2.0);
        return;
    }

    const qreal marker_size = markerSizeForZoom(
        this->map_model->zoom(), this->symbology_settings.icon_size_percent);
    const qreal icon_scale = marker_size / qMax(asset->view_width, asset->view_height);
    const qreal icon_x = center.x() - asset->view_width * icon_scale / 2.0;
    const qreal icon_y = center.y() - asset->view_height * icon_scale / 2.0;
    const QTransform transform(icon_scale, 0.0, 0.0, icon_scale, icon_x, icon_y);

    if (!asset->fill_path.isEmpty())
        painter.fillPath(transform.map(asset->fill_path), QBrush(selected_color));
    if (!asset->stroke_path.isEmpty())
    {
        QPen selected_pen(selected_color);
        selected_pen.setWidthF(qMax<qreal>(2.0, asset->stroke_width * icon_scale + 1.5));
        selected_pen.setCapStyle(Qt::RoundCap);
        selected_pen.setJoinStyle(Qt::RoundJoin);
        painter.strokePath(transform.map(asset->stroke_path), selected_pen);
    }
}

QPointF MapNetworkOverlayWidget::visibleReferenceWorldCenter() const
{
    if (!this->render_geometry)
        return QPointF();

    const QPointF raw_center = GeoWebMercator::lonLatToWorldPixel(
        GeoWebMercator::normalizeLongitude(this->map_model->centerLon()),
        this->map_model->centerLat(),
        MapRenderCacheMath::ReferenceZoom);
    return QPointF(
        GeoWebMercator::nearestWrappedWorldPixelX(
            raw_center.x(),
            this->render_geometry->world_origin.x(),
            MapRenderCacheMath::ReferenceZoom),
        raw_center.y());
}

QRectF MapNetworkOverlayWidget::visibleReferenceWorldRect() const
{
    const qreal scale = referenceScaleForCurrentZoom();
    if (scale <= 0.0)
        return QRectF();

    return MapRenderCacheMath::centeredWorldRect(
        visibleReferenceWorldCenter(), size(), scale);
}

qreal MapNetworkOverlayWidget::referenceScaleForCurrentZoom() const
{
    return GeoWebMercator::zoomScale(
        this->map_model->zoom(), MapRenderCacheMath::ReferenceZoom);
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
    qreal point_x, qreal point_y, qreal node_half_width, qreal icon_half_width) const
{
    NetworkOverlayHit best_hit;
    qreal best_distance_squared = std::numeric_limits<qreal>::infinity();
    const qreal maximum_half_width = qMax(node_half_width, icon_half_width);
    const QList<int> candidates = candidateIndices(
        point_x, point_y, maximum_half_width, HitCollection::Markers);

    for (int index : candidates)
    {
        const HitMarker &marker = this->hit_markers.at(index);
        const qreal marker_half_width = iconAssetForEntity(marker.entity_type)
            ? icon_half_width : node_half_width;
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
        best_hit.uuid = marker.uuid;
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
        best_hit.uuid = segment.uuid;
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
