#include "map_network_overlay_widget.h"

#include "../geo_web_mercator.h"
#include "../hydraulic_data.h"
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
#include <QThread>
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
constexpr int ReferenceZoom = 18;
constexpr qreal NetworkImagePadding = 8.0;
constexpr qreal NetworkImageOverscanFactor = 3.0;
constexpr int NetworkImageMaximumPhysicalDimension = 4096;
constexpr qint64 NetworkImageMaximumPhysicalArea = 8LL * 1024LL * 1024LL;
constexpr qreal NetworkImageRebuildEdge = 256.0;
constexpr qreal LinkHitDistance = 7.0;
constexpr qreal SpatialCellSize = 128.0;
constexpr int SymbologyColorBucketCount = 256;
constexpr int HeatmapMaximumDimension = 2048;
constexpr qint64 HeatmapMaximumArea = 2LL * 1024LL * 1024LL;
constexpr qint64 HeatmapMaximumKernelCachePixels = 8LL * 1024LL * 1024LL;
constexpr qreal HeatmapMaximumKernelRadius = 256.0;
constexpr int HeatmapMaximumColorBuckets = 64;
constexpr double WebMercatorMetersPerPixelAtZoomZero = 156543.03392804097;
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

QSizeF markerScreenSize(InfrastructureEntity entity_type, int zoom, int node_size_percent)
{
    const qreal marker_size = markerSizeForZoom(zoom, node_size_percent);
    const NetworkIconAsset *asset = iconAssetForEntity(entity_type);
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

QPair<double, double> nodeRange(const HydraulicData &hydraulic_data, VisualNode visual_node)
{
    switch (visual_node)
    {
    case VisualNode::Elevation:
        return qMakePair(hydraulic_data.nodeElevationMMinimum(), hydraulic_data.nodeElevationMMaximum());
    case VisualNode::BaseDemand:
        return qMakePair(hydraulic_data.nodeBaseDemandM3PerHMinimum(), hydraulic_data.nodeBaseDemandM3PerHMaximum());
    case VisualNode::TotalDemand:
        return qMakePair(hydraulic_data.nodeTotalDemandM3PerHMinimum(), hydraulic_data.nodeTotalDemandM3PerHMaximum());
    case VisualNode::DemandDeficit:
        return qMakePair(hydraulic_data.nodeDemandDeficitM3PerHMinimum(), hydraulic_data.nodeDemandDeficitM3PerHMaximum());
    case VisualNode::EmitterFlow:
        return qMakePair(hydraulic_data.nodeEmitterFlowM3PerHMinimum(), hydraulic_data.nodeEmitterFlowM3PerHMaximum());
    case VisualNode::Leakage:
        return qMakePair(hydraulic_data.nodeLeakageM3PerHMinimum(), hydraulic_data.nodeLeakageM3PerHMaximum());
    case VisualNode::Head:
        return qMakePair(hydraulic_data.nodeHeadMMinimum(), hydraulic_data.nodeHeadMMaximum());
    case VisualNode::Pressure:
        return qMakePair(hydraulic_data.nodePressureMMinimum(), hydraulic_data.nodePressureMMaximum());
    case VisualNode::Chlorine:
        return qMakePair(hydraulic_data.nodeChlorineMgPerLMinimum(), hydraulic_data.nodeChlorineMgPerLMaximum());
    case VisualNode::RiverWater:
        return qMakePair(hydraulic_data.nodeRiverWaterPercentMinimum(), hydraulic_data.nodeRiverWaterPercentMaximum());
    case VisualNode::LakeWater:
        return qMakePair(hydraulic_data.nodeLakeWaterPercentMinimum(), hydraulic_data.nodeLakeWaterPercentMaximum());
    case VisualNode::None:
        break;
    }

    return qMakePair(0.0, 0.0);
}

QPair<double, double> linkRange(const HydraulicData &hydraulic_data, VisualLink visual_link)
{
    switch (visual_link)
    {
    case VisualLink::Diameter:
        return qMakePair(hydraulic_data.linkDiameterMmMinimum(), hydraulic_data.linkDiameterMmMaximum());
    case VisualLink::Length:
        return qMakePair(hydraulic_data.linkLengthMMinimum(), hydraulic_data.linkLengthMMaximum());
    case VisualLink::Roughness:
        return qMakePair(hydraulic_data.linkRoughnessHwMinimum(), hydraulic_data.linkRoughnessHwMaximum());
    case VisualLink::FlowRate:
        return qMakePair(hydraulic_data.linkFlowRateM3PerHMinimum(), hydraulic_data.linkFlowRateM3PerHMaximum());
    case VisualLink::Velocity:
        return qMakePair(hydraulic_data.linkVelocityMPerSMinimum(), hydraulic_data.linkVelocityMPerSMaximum());
    case VisualLink::HeadLoss:
        return qMakePair(hydraulic_data.linkHeadLossMMinimum(), hydraulic_data.linkHeadLossMMaximum());
    case VisualLink::Leakage:
        return qMakePair(hydraulic_data.linkLeakageM3PerHMinimum(), hydraulic_data.linkLeakageM3PerHMaximum());
    case VisualLink::Chlorine:
        return qMakePair(hydraulic_data.linkChlorineMgPerLMinimum(), hydraulic_data.linkChlorineMgPerLMaximum());
    case VisualLink::RiverWater:
        return qMakePair(hydraulic_data.linkRiverWaterPercentMinimum(), hydraulic_data.linkRiverWaterPercentMaximum());
    case VisualLink::LakeWater:
        return qMakePair(hydraulic_data.linkLakeWaterPercentMinimum(), hydraulic_data.linkLakeWaterPercentMaximum());
    case VisualLink::None:
        break;
    }

    return qMakePair(0.0, 0.0);
}

QPair<double, double> heatmapRange(const HydraulicData &hydraulic_data, VisualHeatmap visual_heatmap)
{
    switch (visual_heatmap)
    {
    case VisualHeatmap::Elevation:
        return qMakePair(hydraulic_data.heatmapElevationMMinimum(), hydraulic_data.heatmapElevationMMaximum());
    case VisualHeatmap::BaseDemand:
        return qMakePair(hydraulic_data.nodeBaseDemandM3PerHMinimum(), hydraulic_data.nodeBaseDemandM3PerHMaximum());
    case VisualHeatmap::TotalDemand:
        return qMakePair(hydraulic_data.heatmapTotalDemandM3PerHMinimum(), hydraulic_data.heatmapTotalDemandM3PerHMaximum());
    case VisualHeatmap::DemandDeficit:
        return qMakePair(hydraulic_data.heatmapDemandDeficitM3PerHMinimum(), hydraulic_data.heatmapDemandDeficitM3PerHMaximum());
    case VisualHeatmap::EmitterFlow:
        return qMakePair(hydraulic_data.nodeEmitterFlowM3PerHMinimum(), hydraulic_data.nodeEmitterFlowM3PerHMaximum());
    case VisualHeatmap::Leakage:
        return qMakePair(hydraulic_data.heatmapLeakageM3PerHMinimum(), hydraulic_data.heatmapLeakageM3PerHMaximum());
    case VisualHeatmap::Head:
        return qMakePair(hydraulic_data.heatmapHeadMMinimum(), hydraulic_data.heatmapHeadMMaximum());
    case VisualHeatmap::Pressure:
        return qMakePair(hydraulic_data.heatmapPressureMMinimum(), hydraulic_data.heatmapPressureMMaximum());
    case VisualHeatmap::Chlorine:
        return qMakePair(hydraulic_data.heatmapChlorineMgPerLMinimum(), hydraulic_data.heatmapChlorineMgPerLMaximum());
    case VisualHeatmap::RiverWater:
        return qMakePair(hydraulic_data.heatmapRiverWaterPercentMinimum(), hydraulic_data.heatmapRiverWaterPercentMaximum());
    case VisualHeatmap::LakeWater:
        return qMakePair(hydraulic_data.heatmapLakeWaterPercentMinimum(), hydraulic_data.heatmapLakeWaterPercentMaximum());
    case VisualHeatmap::None:
        break;
    }

    return qMakePair(0.0, 0.0);
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
        0.0, geometry_bounds.center().y(), ReferenceZoom).latitude_deg;
    const double latitude_radians = qDegreesToRadians(
        qBound(-GeoWebMercator::MaximumLatitude, center_latitude, GeoWebMercator::MaximumLatitude));
    const double meters_per_pixel = std::max(0.000001,
        WebMercatorMetersPerPixelAtZoomZero * std::cos(latitude_radians) / std::ldexp(1.0, ReferenceZoom));
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
        const bool rebuild_node_colors = this->visual_node != VisualNode::None;
        const bool rebuild_heatmap = this->visual_heatmap != VisualHeatmap::None;
        if (!rebuild_node_colors && !rebuild_heatmap)
            return;

        rebuildSymbology(true, rebuild_node_colors, false, rebuild_heatmap);
        requestRenderCache(true);
        update();
    });
    connect(this->hydraulic_data, &HydraulicData::signalLinkChanged, this,
        [this](InfrastructureEntity, const QUuid &)
    {
        if (!this->rendering_active)
            return;
        if (this->visual_link == VisualLink::None)
            return;

        rebuildSymbology(true, false, true, false);
        requestRenderCache(true);
        update();
    });

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

void MapNetworkOverlayWidget::setSymbology(VisualNode visual_node, int node_size_percent, VisualLink visual_link, int link_thickness_px)
{
    const int bounded_node_size_percent = qBound(50, node_size_percent, 250);
    const int bounded_link_thickness_px = qBound(1, link_thickness_px, 12);
    const bool node_visual_changed = this->visual_node != visual_node;
    const bool link_visual_changed = this->visual_link != visual_link;
    const bool node_size_changed = this->node_size_percent != bounded_node_size_percent;
    const bool link_thickness_changed = this->link_thickness_px != bounded_link_thickness_px;

    if (!node_visual_changed && !link_visual_changed && !node_size_changed && !link_thickness_changed)
        return;

    this->visual_node = visual_node;
    this->node_size_percent = bounded_node_size_percent;
    this->visual_link = visual_link;
    this->link_thickness_px = bounded_link_thickness_px;
    rebuildSymbology(false, node_visual_changed || !this->render_symbology,
        link_visual_changed || !this->render_symbology, false);
    requestRenderCache(true);
    update();
}

void MapNetworkOverlayWidget::setHeatmap(VisualHeatmap visual_heatmap, int opacity, int radius_m, int solid_center_percent)
{
    const int bounded_opacity = qBound(0, opacity, 100);
    const int bounded_radius_m = qBound(10, radius_m, 1000);
    const int bounded_solid_center_percent = qBound(0, solid_center_percent, 100);
    const bool visual_changed = this->visual_heatmap != visual_heatmap;
    const bool opacity_changed = this->heatmap_opacity != bounded_opacity;
    const bool radius_changed = this->heatmap_radius_m != bounded_radius_m;
    const bool solid_center_changed = this->heatmap_solid_center_percent != bounded_solid_center_percent;

    if (!visual_changed && !opacity_changed && !radius_changed && !solid_center_changed)
        return;

    this->visual_heatmap = visual_heatmap;
    this->heatmap_opacity = bounded_opacity;
    this->heatmap_radius_m = bounded_radius_m;
    this->heatmap_solid_center_percent = bounded_solid_center_percent;

    const bool heatmap_render_changed = visual_changed ||
        (visual_heatmap != VisualHeatmap::None && (radius_changed || solid_center_changed));
    if (heatmap_render_changed)
    {
        rebuildSymbology(false, false, false, visual_changed || !this->render_symbology);
        if (visual_heatmap == VisualHeatmap::None)
            this->rendered_heatmap_cache = QImage();
        requestRenderCache(true);
    }

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
        world_position.x(), world_position.y(),
        markerSizeForZoom(this->map_model->zoom(), this->node_size_percent) / (2.0 * scale));
    if (marker_hit.isValid())
        return marker_hit;

    const qreal link_hit_distance = qMax(LinkHitDistance, this->link_thickness_px / 2.0 + 3.0) / scale;
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
    rebuildSymbology(false);
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
    geometry->markers.reserve(this->snapshot.nodes.size() + this->snapshot.links.size());
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
        RenderGeometry::Marker render_marker;
        render_marker.render_id = node.render_id;
        render_marker.entity_type = node.entity_type;
        render_marker.world_position = world_position;
        geometry->markers.append(render_marker);
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
            RenderGeometry::Segment render_segment;
            render_segment.render_id = link.render_id;
            render_segment.line = QLineF(start, end);
            geometry->link_segments.append(render_segment);

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
            const QPointF center = polylineMidpoint(world_vertices);

            RenderGeometry::Marker render_marker;
            render_marker.render_id = link.render_id;
            render_marker.entity_type = link.entity_type;
            render_marker.world_position = center;
            geometry->markers.append(render_marker);

            HitMarker marker;
            marker.render_id = link.render_id;
            marker.entity_type = link.entity_type;
            marker.world_position = center;
            this->hit_markers.append(marker);
        }
    }

    if ((geometry->markers.isEmpty() && geometry->link_segments.isEmpty()) ||
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

void MapNetworkOverlayWidget::rebuildSymbology(bool rebuild_ranges, bool rebuild_node_colors,
                                               bool rebuild_link_colors, bool rebuild_heatmap)
{
    if (rebuild_ranges)
        this->hydraulic_data->rebuildSymbologyMinMaxValues();

    std::shared_ptr<RenderSymbology> symbology = std::make_shared<RenderSymbology>();
    symbology->revision = ++this->symbology_revision;
    symbology->node_size_percent = qBound(50, this->node_size_percent, 250);
    symbology->link_width = qBound<qreal>(1.0, this->link_thickness_px, 12.0);
    symbology->heatmap_radius_m = qBound(10, this->heatmap_radius_m, 1000);
    symbology->heatmap_solid_center_percent = qBound(0, this->heatmap_solid_center_percent, 100);

    if (this->render_symbology && !rebuild_node_colors)
        symbology->node_colors = this->render_symbology->node_colors;
    if (this->render_symbology && !rebuild_link_colors)
        symbology->link_colors = this->render_symbology->link_colors;
    if (this->render_symbology && !rebuild_heatmap)
        symbology->heatmap_fractions = this->render_symbology->heatmap_fractions;

    const NetworkHydraulic &network_hydraulic = this->hydraulic_data->networkHydraulic();
    if (rebuild_node_colors)
    {
        symbology->node_colors.reserve(this->snapshot.nodes.size());
        if (this->visual_node == VisualNode::None)
        {
            for (const NetworkRenderNode &node : this->snapshot.nodes)
                symbology->node_colors.insert(node.render_id, NetworkColor.rgb());
        }
        else
        {
            const QPair<double, double> range = nodeRange(*this->hydraulic_data, this->visual_node);
            const QHash<QUuid, double> values = nodeValues(network_hydraulic, this->visual_node);
            for (const NetworkRenderNode &node : this->snapshot.nodes)
            {
                const QHash<QUuid, double>::const_iterator iterator = values.constFind(node.uuid);
                const QRgb color = iterator == values.cend()
                    ? SymbologyValueUnavailableColor.rgb()
                    : symbologyColor(iterator.value(), range.first, range.second);
                symbology->node_colors.insert(node.render_id, color);
            }
        }
    }

    if (rebuild_link_colors)
    {
        symbology->link_colors.reserve(this->snapshot.links.size());
        if (this->visual_link == VisualLink::None)
        {
            for (const NetworkRenderLink &link : this->snapshot.links)
                symbology->link_colors.insert(link.render_id, NetworkColor.rgb());
        }
        else
        {
            const QPair<double, double> range = linkRange(*this->hydraulic_data, this->visual_link);
            const QHash<QUuid, double> values = linkValues(network_hydraulic, this->visual_link);
            for (const NetworkRenderLink &link : this->snapshot.links)
            {
                const QHash<QUuid, double>::const_iterator iterator = values.constFind(link.uuid);
                const QRgb color = iterator == values.cend()
                    ? SymbologyValueUnavailableColor.rgb()
                    : symbologyColor(iterator.value(), range.first, range.second);
                symbology->link_colors.insert(link.render_id, color);
            }
        }
    }

    if (rebuild_heatmap)
    {
        symbology->heatmap_fractions.clear();
        if (this->visual_heatmap != VisualHeatmap::None)
        {
            const QPair<double, double> range = heatmapRange(*this->hydraulic_data, this->visual_heatmap);
            const QHash<QUuid, double> values = heatmapValues(network_hydraulic, this->visual_heatmap);
            symbology->heatmap_fractions.reserve(this->snapshot.nodes.size());
            for (const NetworkRenderNode &node : this->snapshot.nodes)
            {
                const QHash<QUuid, double>::const_iterator iterator = values.constFind(node.uuid);
                if (iterator == values.cend())
                    continue;
                const double fraction = heatmapValueFraction(iterator.value(), range.first, range.second);
                if (std::isfinite(fraction))
                    symbology->heatmap_fractions.insert(node.render_id, fraction);
            }
        }
    }

    this->render_symbology = symbology;
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

    const qreal render_half_width = qMax(
        markerSizeForZoom(request.zoom, request.symbology->node_size_percent) / 2.0,
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

    QImage heatmap(raster.size, QImage::Format_ARGB32_Premultiplied);
    if (heatmap.isNull())
        return QImage();
    heatmap.fill(Qt::transparent);

    QHash<int, QImage> kernel_cache;
    kernel_cache.reserve(color_bucket_count);
    QPainter painter(&heatmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    int processed_markers = 0;
    for (const RenderGeometry::Marker &marker : request.geometry->markers)
    {
        if ((++processed_markers & 255) == 0 && request.cancelled &&
            request.cancelled->load(std::memory_order_relaxed))
        {
            painter.end();
            return QImage();
        }

        if (marker.entity_type != InfrastructureEntity::Junction &&
            marker.entity_type != InfrastructureEntity::Reservoir &&
            marker.entity_type != InfrastructureEntity::Tank)
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

        const int bucket = qBound(0,
            qRound(fraction_iterator.value() * (color_bucket_count - 1)), color_bucket_count - 1);
        QHash<int, QImage>::iterator kernel_iterator = kernel_cache.find(bucket);
        if (kernel_iterator == kernel_cache.end())
        {
            const double bucket_fraction = color_bucket_count <= 1
                ? 0.5 : double(bucket) / double(color_bucket_count - 1);
            QImage kernel = createHeatmapKernel(
                radius, interpolatedRampColor(bucket_fraction),
                request.symbology->heatmap_solid_center_percent);
            if (kernel.isNull())
            {
                painter.end();
                return QImage();
            }
            kernel_iterator = kernel_cache.insert(bucket, kernel);
        }

        const QImage &kernel = kernel_iterator.value();
        painter.drawImage(QRectF(
            x - display_diameter / 2.0, y - display_diameter / 2.0,
            display_diameter, display_diameter), kernel, QRectF(kernel.rect()));
    }

    painter.end();
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
        std::vector<int> marker_indices;
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
    const qreal link_padding = request.symbology->link_width / 2.0 + NetworkImagePadding;

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

        const int first_stripe = stripe_for_logical_y(minimum_y);
        const int last_stripe = stripe_for_logical_y(maximum_y);
        for (int stripe_index = first_stripe; stripe_index <= last_stripe; ++stripe_index)
            stripes[stripe_index].segment_indices.push_back(segment_index);
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
            marker.entity_type, request.zoom, request.symbology->node_size_percent);
        const qreal marker_padding_x = marker_size.width() / 2.0 + NetworkImagePadding;
        const qreal marker_padding_y = marker_size.height() / 2.0 + NetworkImagePadding;
        const qreal x = (world_position.x() - image_left) * scale;
        const qreal y = (world_position.y() - image_top) * scale;
        if (x + marker_padding_x < 0.0 || x - marker_padding_x > logical_width ||
            y + marker_padding_y < 0.0 || y - marker_padding_y > logical_height)
        {
            continue;
        }

        const int first_stripe = stripe_for_logical_y(y - marker_padding_y);
        const int last_stripe = stripe_for_logical_y(y + marker_padding_y);
        for (int stripe_index = first_stripe; stripe_index <= last_stripe; ++stripe_index)
            stripes[stripe_index].marker_indices.push_back(marker_index);
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

        QHash<QRgb, QPainterPath> link_paths;
        QHash<QRgb, QPainterPath> junction_paths;
        QHash<quint64, QPainterPath> icon_stroke_paths;
        QHash<QRgb, QPainterPath> icon_fill_paths;
        link_paths.reserve(qMin(SymbologyColorBucketCount + 1, int(stripe.segment_indices.size())));
        junction_paths.reserve(qMin(SymbologyColorBucketCount + 1, int(stripe.marker_indices.size())));

        int processed_segments = 0;
        for (int segment_index : stripe.segment_indices)
        {
            if ((++processed_segments & 255) == 0 && request.cancelled &&
                request.cancelled->load(std::memory_order_relaxed))
            {
                return QImage();
            }

            const RenderGeometry::Segment &render_segment = request.geometry->link_segments.at(segment_index);
            const QLineF &segment = render_segment.line;
            const QRgb color = request.symbology->link_colors.value(
                render_segment.render_id, NetworkColor.rgb());
            QPainterPath &link_path = link_paths[color];
            link_path.moveTo(QPointF(
                (segment.x1() - image_left) * scale,
                (segment.y1() - image_top) * scale - stripe.logical_top));
            link_path.lineTo(QPointF(
                (segment.x2() - image_left) * scale,
                (segment.y2() - image_top) * scale - stripe.logical_top));
        }

        const qreal junction_radius = junctionDotDiameterForZoom(
            request.zoom, request.symbology->node_size_percent) / 2.0;
        int processed_markers = 0;
        for (int marker_index : stripe.marker_indices)
        {
            if ((++processed_markers & 255) == 0 && request.cancelled &&
                request.cancelled->load(std::memory_order_relaxed))
            {
                return QImage();
            }

            const RenderGeometry::Marker &marker = request.geometry->markers.at(marker_index);
            const QPointF &world_position = marker.world_position;
            const bool node_entity = marker.entity_type == InfrastructureEntity::Junction ||
                marker.entity_type == InfrastructureEntity::Reservoir ||
                marker.entity_type == InfrastructureEntity::Tank;
            const QRgb color = node_entity
                ? request.symbology->node_colors.value(marker.render_id, NetworkColor.rgb())
                : request.symbology->link_colors.value(marker.render_id, NetworkColor.rgb());
            const QPointF center(
                (world_position.x() - image_left) * scale,
                (world_position.y() - image_top) * scale - stripe.logical_top);
            const NetworkIconAsset *asset = iconAssetForEntity(marker.entity_type);
            if (!asset)
            {
                junction_paths[color].addEllipse(center, junction_radius, junction_radius);
                continue;
            }

            const qreal marker_size = markerSizeForZoom(
                request.zoom, request.symbology->node_size_percent);
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

        if (request.cancelled && request.cancelled->load(std::memory_order_relaxed))
            return QImage();

        MapVectorDocument document;
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
                request.zoom, request.symbology->node_size_percent);
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

        QImage stripe_image(
            QSize(qMax(1, qCeil(logical_width * request.device_pixel_ratio)), stripe.physical_height),
            QImage::Format_ARGB32_Premultiplied);
        if (stripe_image.isNull())
            return QImage();
        stripe_image.setDevicePixelRatio(request.device_pixel_ratio);
        stripe_image.fill(Qt::transparent);

        QPainter painter(&stripe_image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        document.paint(painter);
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

    result.heatmap_image = renderHeatmap(request, scale, image_left, image_top);

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

    if (!this->rendered_heatmap_cache.isNull() && this->visual_heatmap != VisualHeatmap::None &&
        this->heatmap_opacity > 0)
    {
        painter.save();
        painter.setOpacity(this->heatmap_opacity / 100.0);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(target_rect, this->rendered_heatmap_cache);
        painter.restore();
    }

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
