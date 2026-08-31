#include "map_rhi_widget.h"

#include "map_terrain_repository.h"
#include "map_terrain_tile.h"

#include "map_model.h"
#include "map_render_cache_math.h"
#include "map_tile_repository.h"
#include "../geo_web_mercator.h"
#include "../network_symbology_rendering.h"

#include <QColor>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QImage>
#include <QHash>
#include <QLineF>
#include <QMatrix4x4>
#include <QPalette>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QRectF>
#include <rhi/qshader.h>

#include <rhi/qrhi.h>

#include <array>
#include <cstddef>
#include <cmath>
#include <limits>

namespace
{
constexpr int CameraUniformBytes = 32 * int(sizeof(float));
#ifdef Q_OS_WASM
constexpr int RendererMsaaSamples = 1;
#else
constexpr int RendererMsaaSamples = 4;
#endif
constexpr int CameraTerrainMinimumZoom = 8;
constexpr int CameraTerrainMaximumZoom = 14;
constexpr double FallbackOriginRecenterThresholdWorld = MapModel::TileSize * 1024.0;

// Soft pink default fill for the SVG-derived reservoir/tank/pump/valve icons
// on the 2D map monitor in light mode, for better visibility against the
// basemap. Only applies when the icon is neither colorized (VisualNode /
// VisualLink symbology active) nor selected -- both of those continue to
// override this via the existing fill color branches in MapRhiScene.
constexpr QRgb MonitorLightThemeIconFillColor = qRgb(244, 174, 194);

bool isLightThemeWindowColor(const QColor &window_color)
{
    const double luminance = 0.2126 * window_color.red()
        + 0.7152 * window_color.green()
        + 0.0722 * window_color.blue();
    return luminance >= 128.0;
}

double metersToScreenPixels(double meters, double latitude_deg, int zoom)
{
    const double meters_per_pixel = GeoWebMercator::metersPerPixel(latitude_deg, zoom);
    if (!std::isfinite(meters_per_pixel) || meters_per_pixel <= 0.0)
        return 0.0;
    return meters / meters_per_pixel;
}

QString cameraTerrainDatasetId()
{
    return QStringLiteral("copernicus-glo30");
}

double terrainSample(const MapTerrainTile &terrain_tile, int row, int column)
{
    const int bounded_row = qBound(0, row, MapTerrainTileGridSize - 1);
    const int bounded_column = qBound(0, column, MapTerrainTileGridSize - 1);
    return double(terrain_tile.elevations_m.at(
        bounded_row * MapTerrainTileGridSize + bounded_column));
}

double nearestFiniteTerrainElevation(
    const MapTerrainTile &terrain_tile, int row, int column)
{
    const int bounded_row = qBound(0, row, MapTerrainTileGridSize - 1);
    const int bounded_column = qBound(0, column, MapTerrainTileGridSize - 1);
    const double direct = terrainSample(terrain_tile, bounded_row, bounded_column);
    if (std::isfinite(direct))
        return direct;

    for (int ring = 1; ring < MapTerrainTileGridSize; ++ring)
    {
        const int row_minimum = qMax(0, bounded_row - ring);
        const int row_maximum = qMin(MapTerrainTileGridSize - 1, bounded_row + ring);
        const int column_minimum = qMax(0, bounded_column - ring);
        const int column_maximum = qMin(MapTerrainTileGridSize - 1, bounded_column + ring);
        for (int sample_column = column_minimum; sample_column <= column_maximum; ++sample_column)
        {
            const double top = terrainSample(terrain_tile, row_minimum, sample_column);
            if (std::isfinite(top))
                return top;
            const double bottom = terrainSample(terrain_tile, row_maximum, sample_column);
            if (std::isfinite(bottom))
                return bottom;
        }
        for (int sample_row = row_minimum + 1; sample_row < row_maximum; ++sample_row)
        {
            const double left = terrainSample(terrain_tile, sample_row, column_minimum);
            if (std::isfinite(left))
                return left;
            const double right = terrainSample(terrain_tile, sample_row, column_maximum);
            if (std::isfinite(right))
                return right;
        }
    }

    return qQNaN();
}

double bilinearTerrainElevation(const MapTerrainTile &terrain_tile, double u, double v)
{
    if (terrain_tile.elevations_m.size() != MapTerrainTileSampleCount)
        return qQNaN();

    const double sample_x = qBound(0.0, u, 1.0) * MapTerrainTileCellCount;
    const double sample_y = qBound(0.0, v, 1.0) * MapTerrainTileCellCount;
    const int x0 = qBound(0, int(std::floor(sample_x)), MapTerrainTileGridSize - 1);
    const int y0 = qBound(0, int(std::floor(sample_y)), MapTerrainTileGridSize - 1);
    const int x1 = qMin(x0 + 1, MapTerrainTileGridSize - 1);
    const int y1 = qMin(y0 + 1, MapTerrainTileGridSize - 1);
    const double tx = sample_x - x0;
    const double ty = sample_y - y0;
    const double samples[4] = {
        terrainSample(terrain_tile, y0, x0),
        terrainSample(terrain_tile, y0, x1),
        terrainSample(terrain_tile, y1, x0),
        terrainSample(terrain_tile, y1, x1)
    };
    const double weights[4] = {
        (1.0 - tx) * (1.0 - ty),
        tx * (1.0 - ty),
        (1.0 - tx) * ty,
        tx * ty
    };

    double weighted_sum = 0.0;
    double weight_sum = 0.0;
    for (int index = 0; index < 4; ++index)
    {
        if (!std::isfinite(samples[index]))
            continue;
        weighted_sum += samples[index] * weights[index];
        weight_sum += weights[index];
    }
    if (weight_sum > 0.0)
        return weighted_sum / weight_sum;
    return nearestFiniteTerrainElevation(
        terrain_tile, int(std::lround(sample_y)), int(std::lround(sample_x)));
}

QRhiWidget::Api platformGraphicsApi()
{
#if defined(Q_OS_MACOS)
    return QRhiWidget::Api::Metal;
#elif defined(Q_OS_WIN)
    return QRhiWidget::Api::Direct3D11;
#else
    return QRhiWidget::Api::OpenGL;
#endif
}

QString graphicsApiDisplayName(QRhiWidget::Api api)
{
    switch (api)
    {
        case QRhiWidget::Api::OpenGL:
            return QStringLiteral("OpenGL");
        case QRhiWidget::Api::Metal:
            return QStringLiteral("Metal");
        case QRhiWidget::Api::Vulkan:
            return QStringLiteral("Vulkan");
        case QRhiWidget::Api::Direct3D11:
            return QStringLiteral("Direct3D 11");
        case QRhiWidget::Api::Direct3D12:
            return QStringLiteral("Direct3D 12");
        case QRhiWidget::Api::Null:
        default:
            return QStringLiteral("Null");
    }
}

QShader loadShader(const QString &resource_path)
{
    QFile file(resource_path);
    if (!file.open(QIODevice::ReadOnly))
        return QShader();
    return QShader::fromSerialized(file.readAll());
}

int boundedBufferSize(qsizetype vertex_count, qsizetype vertex_size)
{
    const qsizetype bytes = vertex_count * vertex_size;
    if (bytes <= 0)
        return 1;
    if (bytes > qsizetype(std::numeric_limits<int>::max()))
        return 0;
    return int(bytes);
}

double pointSegmentDistance(const QPointF &point, const QPointF &start, const QPointF &end)
{
    const QPointF segment = end - start;
    const double length_squared = segment.x() * segment.x() + segment.y() * segment.y();
    if (length_squared <= 1e-12)
        return QLineF(point, start).length();

    const QPointF relative = point - start;
    const double ratio = qBound(
        0.0,
        (relative.x() * segment.x() + relative.y() * segment.y()) / length_squared,
        1.0);
    return QLineF(
        point,
        QPointF(start.x() + segment.x() * ratio, start.y() + segment.y() * ratio)).length();
}

bool finiteScreenPoint(const QPointF &point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool rayTriangleIntersectionDistance(
    const QVector3D &ray_origin, const QVector3D &ray_direction,
    const QVector3D &a, const QVector3D &b, const QVector3D &c,
    double *distance)
{
    if (distance == nullptr)
        return false;

    const QVector3D edge1 = b - a;
    const QVector3D edge2 = c - a;
    const QVector3D cross = QVector3D::crossProduct(ray_direction, edge2);
    const double determinant = double(QVector3D::dotProduct(edge1, cross));
    if (std::abs(determinant) <= 1e-10)
        return false;

    const double inverse_determinant = 1.0 / determinant;
    const QVector3D from_a = ray_origin - a;
    const double u = double(QVector3D::dotProduct(from_a, cross)) * inverse_determinant;
    if (u < 0.0 || u > 1.0)
        return false;

    const QVector3D q = QVector3D::crossProduct(from_a, edge1);
    const double v = double(QVector3D::dotProduct(ray_direction, q)) * inverse_determinant;
    if (v < 0.0 || u + v > 1.0)
        return false;

    const double hit_distance = double(QVector3D::dotProduct(edge2, q))
        * inverse_determinant;
    if (hit_distance < 0.0 || !std::isfinite(hit_distance))
        return false;

    *distance = hit_distance;
    return true;
}

constexpr double Rhi2dMinimumNodeHitRadiusPx = 10.0;
constexpr double Rhi3dMinimumNodeHitRadiusPx = 16.0;
constexpr double Rhi2dNodeHitPaddingPx = 5.0;
constexpr double Rhi3dNodeHitPaddingPx = 9.0;
constexpr double Rhi2dMinimumLinkHitRadiusPx = 8.0;
constexpr double Rhi3dMinimumLinkHitRadiusPx = 12.0;
constexpr double Rhi2dLinkHitPaddingPx = 5.0;
constexpr double Rhi3dLinkHitPaddingPx = 8.0;
constexpr double Rhi2dMinimumIconHitRadiusPx = 12.0;
constexpr double Rhi3dMinimumIconHitRadiusPx = 18.0;
constexpr double Rhi2dIconHitPaddingPx = 6.0;
constexpr double Rhi3dIconHitPaddingPx = 10.0;
}

MapRhiWidget::MapRhiWidget(MapModel *map_model, const QString &surface_name, QWidget *parent)
    : QRhiWidget(parent),
      map_model(map_model),
      surface_name(surface_name)
{
    Q_ASSERT(this->map_model != nullptr);

    setApi(platformGraphicsApi());
    setSampleCount(RendererMsaaSamples);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);

    this->fallback_origin_world = GeoWebMercator::lonLatToWorldPixel(
        GeoWebMercator::normalizeLongitude(this->map_model->centerLon()),
        this->map_model->centerLat(), MapRenderCacheMath::ReferenceZoom);
    this->basemap_renderer = std::make_unique<MapRhiBasemapRenderer>(
        this->map_model, &this->scene, this->tile_repository, this->terrain_repository);
    this->scene.setNetworkGroundOffsetM(this->map_model->view3dNetworkGroundOffsetM());
    this->scene.setVerticalExaggeration(this->map_model->view3dVerticalExaggeration());
    syncViewState();

    connect(this->map_model, &MapModel::centerChangedWGS84, this, [this]
    {
        if (!this->scene.hasGeometry())
        {
            const QPointF center_world = GeoWebMercator::lonLatToWorldPixel(
                GeoWebMercator::normalizeLongitude(this->map_model->centerLon()),
                this->map_model->centerLat(), MapRenderCacheMath::ReferenceZoom);
            const double delta_x = center_world.x() - this->fallback_origin_world.x();
            const double delta_y = center_world.y() - this->fallback_origin_world.y();
            if (std::hypot(delta_x, delta_y) > FallbackOriginRecenterThresholdWorld)
            {
                this->fallback_origin_world = center_world;
                if (this->basemap_renderer)
                    this->basemap_renderer->invalidate();
            }
        }
        syncViewState();
        update();
    });
    connect(this->map_model, &MapModel::zoomChanged, this, [this]
    {
        syncViewState();
        markUndergroundGeometryDirty();
        this->heatmap_upload_pending = true;
        syncBasemapHeatmapOverlay();
        if (this->basemap_renderer)
            this->basemap_renderer->invalidate();
        update();
    });
    connect(this->map_model, &MapModel::view2dContinuousScaleChanged,
            this, [this](double)
    {
        syncViewState();
        update();
    });
    connect(this->map_model, &MapModel::viewModeChanged, this, [this](MapViewMode)
    {
        syncViewState();
        markUndergroundGeometryDirty();
        this->heatmap_upload_pending = true;
        syncBasemapHeatmapOverlay();
        if (this->basemap_renderer)
            this->basemap_renderer->invalidate();
        update();
    });
    connect(this->map_model, &MapModel::view3dCameraChanged, this, [this]
    {
        if (this->scene.setVerticalExaggeration(this->map_model->view3dVerticalExaggeration()))
        {
            this->geometry_upload_pending = true;
            this->highlight_upload_pending = true;
            this->flow_direction_upload_pending = true;
            this->icon_upload_pending = true;
            this->heatmap_upload_pending = true;
            this->tank_upload_pending = true;
            this->junction_instance_upload_pending = true;
            markUndergroundGeometryDirty();
            if (this->basemap_renderer)
                this->basemap_renderer->invalidate();
        }

        syncViewState();
        update();
    });
    connect(this->map_model, &MapModel::view3dNetworkGroundOffsetChanged,
            this, [this](double offset_m)
    {
        if (!this->scene.setNetworkGroundOffsetM(offset_m))
            return;

        this->geometry_upload_pending = true;
        this->highlight_upload_pending = true;
        this->flow_direction_upload_pending = true;
        this->icon_upload_pending = true;
        this->tank_upload_pending = true;
        this->junction_instance_upload_pending = true;
        markUndergroundGeometryDirty();
        update();
    });
    connect(this->map_model, &MapModel::view3dNavigationStateChanged,
            this, [this](MapView3dNavigationState state)
    {
        if (state == MapView3dNavigationState::Rotate)
            captureView3dFocusAnchor();
    });
    connect(this->map_model, &MapModel::providerChanged, this, [this](MapProvider)
    {
        if (this->basemap_renderer)
            this->basemap_renderer->invalidate();
        update();
    });
    connect(this, &QRhiWidget::renderFailed, this, [this]
    {
        reportFailure(QStringLiteral("QRhiWidget reported a rendering failure"));
    });
    connect(this, &QRhiWidget::frameSubmitted, this, [this]
    {
        if (this->ready_reported || this->failure_reported)
            return;

        this->ready_reported = true;
        qInfo().noquote()
            << QStringLiteral("Desktop map RHI surface '%1' submitted its first frame using %2 "
                              "(%3 link vertices, %4 node vertices).")
                   .arg(this->surface_name, graphicsApiName())
                   .arg(this->scene.linkVertices().size())
                   .arg(this->scene.nodeVertices().size());
        emit signalRendererReady();
    });
}

MapRhiWidget::~MapRhiWidget() = default;

QString MapRhiWidget::graphicsApiName() const
{
    return graphicsApiDisplayName(api());
}

MapRhiHit MapRhiWidget::hitTest(const QPointF &screen_position) const
{
    MapRhiHit no_hit;
    if (this->map_model == nullptr
        || !finiteScreenPoint(screen_position)
        || screen_position.x() < 0.0 || screen_position.y() < 0.0
        || screen_position.x() > width() || screen_position.y() > height())
    {
        return no_hit;
    }

    const NetworkRenderSnapshot &snapshot = this->scene.networkSnapshot();

    const bool three_d = this->map_model->viewMode() == MapViewMode::ThreeD;

    // In 3D the tank mesh itself is authoritative. Cast the camera ray against
    // the exact triangles uploaded for the rendered tank model, including the
    // current model dimensions. This avoids the old padded projected rectangle
    // selecting empty space around the tank or missing visible roof/body parts.
    quint32 best_tank_render_id = 0;
    double best_tank_distance = std::numeric_limits<double>::max();
    if (three_d && !this->tank_model_vertices.isEmpty())
    {
        QVector3D tank_ray_origin;
        QVector3D tank_ray_direction;
        const QPointF tank_screen_position =
            screen_position - this->network_screen_translation;
        if (this->camera.screenRay(
                tank_screen_position, &tank_ray_origin, &tank_ray_direction))
        {
            for (qsizetype vertex_index = 0;
                 vertex_index + 2 < this->tank_model_vertices.size();
                 vertex_index += 3)
            {
                const MapRhiTankModelVertex &vertex_a =
                    this->tank_model_vertices.at(vertex_index);
                const MapRhiTankModelVertex &vertex_b =
                    this->tank_model_vertices.at(vertex_index + 1);
                const MapRhiTankModelVertex &vertex_c =
                    this->tank_model_vertices.at(vertex_index + 2);
                if (vertex_a.render_id == 0
                    || vertex_a.render_id != vertex_b.render_id
                    || vertex_a.render_id != vertex_c.render_id)
                {
                    continue;
                }

                const QVector3D a(
                    vertex_a.position_x, vertex_a.position_y, vertex_a.position_z);
                const QVector3D b(
                    vertex_b.position_x, vertex_b.position_y, vertex_b.position_z);
                const QVector3D c(
                    vertex_c.position_x, vertex_c.position_y, vertex_c.position_z);
                double hit_distance = 0.0;
                if (!rayTriangleIntersectionDistance(
                        tank_ray_origin, tank_ray_direction, a, b, c, &hit_distance)
                    || hit_distance >= best_tank_distance)
                {
                    continue;
                }

                best_tank_distance = hit_distance;
                best_tank_render_id = vertex_a.render_id;
            }
        }
    }

    if (best_tank_render_id != 0)
    {
        for (const NetworkRenderNode &node : snapshot.nodes)
        {
            if (node.entity_type != InfrastructureEntity::Tank
                || node.render_id != best_tank_render_id
                || this->scene.isEntityHidden(node.uuid))
            {
                continue;
            }

            MapRhiHit tank_hit;
            tank_hit.render_id = node.render_id;
            tank_hit.entity_type = node.entity_type;
            tank_hit.uuid = node.uuid;
            return tank_hit;
        }
    }

    // Keep 3D junction sizing independent from the discrete map/tile zoom,
    // but preserve real perspective. Junctions near the orbit focus retain the
    // familiar 2D marker size, while geometry farther from the camera projects
    // smaller and nearer geometry projects larger. Hit-testing follows the same
    // continuous depth ratio so there is no zoom-level size jump.
    if (three_d)
    {
        const QPointF junction_screen_position =
            screen_position - this->network_screen_translation;
        const double base_junction_radius_px = this->scene.nodeSizePx() * 0.5;
        const double reference_depth_world = qMax(
            this->camera.orbitDistanceWorld(), 0.000001);
        const double junction_radius_world = this->scene.nodeSizeM()
            * this->scene.worldUnitsPerMeter() * 0.5;
        double best_junction_screen_distance =
            std::numeric_limits<double>::infinity();
        quint32 best_junction_render_id = 0;
        const QVector<MapRhiJunctionInstance> &junction_instances =
            this->scene.junctionInstances();
        for (const MapRhiJunctionInstance &instance : junction_instances)
        {
            if (!(instance.alpha > 0.0f))
                continue;

            const QVector3D junction_world_position(
                instance.center_x, instance.center_y, instance.center_z);
            const double depth_world =
                this->camera.perspectiveDepthWorld(junction_world_position);
            if (!std::isfinite(depth_world) || depth_world <= 0.000001)
                continue;

            const QPointF projected =
                this->camera.projectWorldToScreen(junction_world_position);
            if (!finiteScreenPoint(projected))
                continue;

            double visual_radius_px =
                base_junction_radius_px * reference_depth_world / depth_world;
            if (this->scene.nodeSizeUnit() == NetworkSymbologySizeUnit::Meters
                && std::isfinite(junction_radius_world) && junction_radius_world > 0.0)
            {
                const QPointF projected_edge = this->camera.projectWorldToScreen(
                    junction_world_position + QVector3D(float(junction_radius_world), 0.0f, 0.0f));
                if (finiteScreenPoint(projected_edge))
                    visual_radius_px = QLineF(projected, projected_edge).length();
            }
            const double junction_hit_radius = qMax(
                Rhi3dMinimumNodeHitRadiusPx,
                visual_radius_px + Rhi3dNodeHitPaddingPx);
            const double screen_distance =
                QLineF(junction_screen_position, projected).length();
            if (screen_distance > junction_hit_radius
                || screen_distance >= best_junction_screen_distance)
            {
                continue;
            }

            best_junction_screen_distance = screen_distance;
            best_junction_render_id = instance.render_id;
        }

        if (best_junction_render_id != 0)
        {
            for (const NetworkRenderNode &node : snapshot.nodes)
            {
                if (node.entity_type != InfrastructureEntity::Junction
                    || node.render_id != best_junction_render_id
                    || this->scene.isEntityHidden(node.uuid))
                {
                    continue;
                }

                MapRhiHit junction_hit;
                junction_hit.render_id = node.render_id;
                junction_hit.entity_type = node.entity_type;
                junction_hit.uuid = node.uuid;
                return junction_hit;
            }
        }
    }

    // Pick visible icon billboards as icons, not merely by the much smaller
    // underlying node/link center geometry. This matters especially for pumps
    // and valves, whose icon can be substantially larger than the pipe itself.
    const double icon_padding = three_d
        ? Rhi3dIconHitPaddingPx : Rhi2dIconHitPaddingPx;
    const double minimum_icon_radius = three_d
        ? Rhi3dMinimumIconHitRadiusPx : Rhi2dMinimumIconHitRadiusPx;
    double best_icon_distance = std::numeric_limits<double>::infinity();
    quint32 best_icon_render_id = 0;
    InfrastructureEntity best_icon_entity_type = InfrastructureEntity::Unknown;
    const QVector<MapRhiScene::IconVertex> &icon_vertices = this->scene.iconVertices();
    for (qsizetype vertex_index = 0; vertex_index + 5 < icon_vertices.size();
         vertex_index += 6)
    {
        const MapRhiScene::IconVertex &icon = icon_vertices.at(vertex_index);
        const QPointF projected = this->camera.projectWorldToScreen(
            QVector3D(icon.center_x, icon.center_y, icon.center_z));
        if (!finiteScreenPoint(projected))
            continue;

        double icon_size_px = double(this->applied_symbology.icon_size_px);
        if (this->applied_symbology.icon_size_unit == NetworkSymbologySizeUnit::Meters)
        {
            if (three_d)
            {
                const double world_units_per_meter = this->scene.worldUnitsPerMeter();
                const double depth_world = this->camera.perspectiveDepthWorld(
                    QVector3D(icon.center_x, icon.center_y, icon.center_z));
                constexpr double tan_half_fov = 0.4142135623730950;
                if (std::isfinite(world_units_per_meter) && world_units_per_meter > 0.0
                    && std::isfinite(depth_world) && depth_world > 0.0)
                {
                    const double icon_size_world =
                        this->applied_symbology.icon_size_m * world_units_per_meter;
                    icon_size_px = icon_size_world * qMax(1, this->viewport_size.height())
                        / (2.0 * depth_world * tan_half_fov);
                }
            }
            else
            {
                icon_size_px = metersToScreenPixels(
                    this->applied_symbology.icon_size_m,
                    this->map_model->centerLat(), this->map_model->zoom());
            }
        }
        else if (three_d)
        {
            const double depth_world = this->camera.perspectiveDepthWorld(
                QVector3D(icon.center_x, icon.center_y, icon.center_z));
            const double reference_depth_world = this->camera.orbitDistanceWorld();
            if (std::isfinite(depth_world) && depth_world > 0.0
                && std::isfinite(reference_depth_world) && reference_depth_world > 0.0)
            {
                icon_size_px *= reference_depth_world / depth_world;
            }
        }
        const double icon_hit_radius = qMax(
            minimum_icon_radius, icon_size_px / 2.0 + icon_padding);
        const double distance = QLineF(screen_position, projected).length();
        if (distance > icon_hit_radius || distance >= best_icon_distance)
            continue;

        best_icon_distance = distance;
        best_icon_render_id = icon.render_id;
        best_icon_entity_type = icon.entity_type;
    }

    if (best_icon_render_id != 0)
    {
        for (const NetworkRenderNode &node : snapshot.nodes)
        {
            if (node.render_id != best_icon_render_id
                || node.entity_type != best_icon_entity_type
                || this->scene.isEntityHidden(node.uuid))
            {
                continue;
            }

            MapRhiHit icon_hit;
            icon_hit.render_id = node.render_id;
            icon_hit.entity_type = node.entity_type;
            icon_hit.uuid = node.uuid;
            return icon_hit;
        }
        for (const NetworkRenderLink &link : snapshot.links)
        {
            if (link.render_id != best_icon_render_id
                || link.entity_type != best_icon_entity_type
                || this->scene.isEntityHidden(link.uuid))
            {
                continue;
            }

            MapRhiHit icon_hit;
            icon_hit.render_id = link.render_id;
            icon_hit.entity_type = link.entity_type;
            icon_hit.uuid = link.uuid;
            return icon_hit;
        }
    }

    const double node_padding = three_d
        ? Rhi3dNodeHitPaddingPx : Rhi2dNodeHitPaddingPx;
    const double minimum_node_radius = three_d
        ? Rhi3dMinimumNodeHitRadiusPx : Rhi2dMinimumNodeHitRadiusPx;
    const double node_radius_px = this->scene.nodeSizeUnit() == NetworkSymbologySizeUnit::Meters
        ? metersToScreenPixels(this->scene.nodeSizeM(), this->map_model->centerLat(),
            this->map_model->zoom()) * 0.5
        : this->scene.nodeSizePx() * 0.5;
    const double node_hit_radius = qMax(
        minimum_node_radius, node_radius_px + node_padding);
    double best_node_distance = node_hit_radius;
    MapRhiHit best_node_hit;
    for (const NetworkRenderNode &node : snapshot.nodes)
    {
        if (this->scene.isEntityHidden(node.uuid)
            || (three_d
                && (node.entity_type == InfrastructureEntity::Junction
                    || node.entity_type == InfrastructureEntity::Tank)))
        {
            continue;
        }

        const QVector3D world_position = this->scene.worldPosition(
            node.coordinate_wgs84, node.elevation_m, this->scene.originWorld().x());
        const QPointF projected = this->camera.projectWorldToScreen(world_position);
        if (!finiteScreenPoint(projected))
            continue;

        const double distance = QLineF(screen_position, projected).length();
        if (distance > best_node_distance)
            continue;

        best_node_distance = distance;
        best_node_hit.render_id = node.render_id;
        best_node_hit.entity_type = node.entity_type;
        best_node_hit.uuid = node.uuid;
    }
    if (best_node_hit.isValid())
        return best_node_hit;

    const double link_padding = three_d
        ? Rhi3dLinkHitPaddingPx : Rhi2dLinkHitPaddingPx;
    const double minimum_link_radius = three_d
        ? Rhi3dMinimumLinkHitRadiusPx : Rhi2dMinimumLinkHitRadiusPx;
    const double fixed_link_half_width_px =
        this->scene.linkThicknessUnit() == NetworkSymbologySizeUnit::Meters
            ? metersToScreenPixels(this->scene.linkThicknessM(), this->map_model->centerLat(),
                this->map_model->zoom()) * 0.5
            : this->scene.linkThicknessPx() * 0.5;
    double best_link_distance = std::numeric_limits<double>::infinity();
    MapRhiHit best_link_hit;
    for (const NetworkRenderLink &link : snapshot.links)
    {
        if (this->scene.isEntityHidden(link.uuid) || link.vertices_wgs84.size() < 2)
            continue;

        bool have_previous = false;
        QPointF previous_screen;
        QVector3D previous_world_position;
        double wrap_reference_x = this->scene.originWorld().x();
        for (qsizetype vertex_index = 0; vertex_index < link.vertices_wgs84.size(); ++vertex_index)
        {
            const double elevation_m = vertex_index < link.elevations_m.size()
                ? link.elevations_m.at(vertex_index)
                : 0.0;
            double resolved_x = wrap_reference_x;
            const QVector3D world_position = this->scene.worldPosition(
                link.vertices_wgs84.at(vertex_index), elevation_m,
                wrap_reference_x, &resolved_x);
            wrap_reference_x = resolved_x;
            const QPointF current_screen = this->camera.projectWorldToScreen(world_position);
            if (!finiteScreenPoint(current_screen))
            {
                have_previous = false;
                continue;
            }

            if (have_previous)
            {
                double segment_hit_radius = qMax(
                    minimum_link_radius, fixed_link_half_width_px + link_padding);
                if (three_d
                    && this->scene.linkThicknessUnit() == NetworkSymbologySizeUnit::Meters)
                {
                    const QVector3D segment_direction = world_position - previous_world_position;
                    QVector3D width_direction(-segment_direction.y(), segment_direction.x(), 0.0f);
                    if (width_direction.lengthSquared() > 0.000001f)
                    {
                        width_direction.normalize();
                        const double half_width_world = this->scene.linkThicknessM()
                            * this->scene.worldUnitsPerMeter() * 0.5;
                        const QVector3D midpoint = (world_position + previous_world_position) * 0.5f;
                        const QPointF midpoint_screen = this->camera.projectWorldToScreen(midpoint);
                        const QPointF edge_screen = this->camera.projectWorldToScreen(
                            midpoint + width_direction * float(half_width_world));
                        if (finiteScreenPoint(midpoint_screen) && finiteScreenPoint(edge_screen))
                        {
                            segment_hit_radius = qMax(
                                minimum_link_radius,
                                QLineF(midpoint_screen, edge_screen).length() + link_padding);
                        }
                    }
                }

                const double distance = pointSegmentDistance(
                    screen_position, previous_screen, current_screen);
                if (distance <= segment_hit_radius && distance <= best_link_distance)
                {
                    best_link_distance = distance;
                    best_link_hit.render_id = link.render_id;
                    best_link_hit.entity_type = link.entity_type;
                    best_link_hit.uuid = link.uuid;
                }
            }

            previous_screen = current_screen;
            previous_world_position = world_position;
            have_previous = true;
        }
    }

    return best_link_hit;
}

void MapRhiWidget::setNetworkSnapshot(const NetworkRenderSnapshot &snapshot)
{
    if (this->scene.geometryRevision() == snapshot.geometry_revision &&
        this->scene.hasGeometry())
    {
        return;
    }

    this->scene.setNetworkSnapshot(snapshot);
    this->scene.setViewZoom(this->map_model->zoom());
    syncViewState();
    syncBasemapHeatmapOverlay();
    if (this->basemap_renderer)
        this->basemap_renderer->invalidate();
    this->geometry_upload_pending = true;
    this->highlight_upload_pending = true;
    this->flow_direction_upload_pending = true;
    this->icon_upload_pending = true;
    this->heatmap_upload_pending = true;
    this->tank_upload_pending = true;
    this->junction_instance_upload_pending = true;
    markUndergroundGeometryDirty();
    update();
}


void MapRhiWidget::setHiddenEntityUuids(const QSet<QUuid> &hidden_entity_uuids)
{
    if (!this->scene.setHiddenEntityUuids(hidden_entity_uuids))
        return;

    syncViewState();
    syncBasemapHeatmapOverlay();
    this->geometry_upload_pending = true;
    this->highlight_upload_pending = true;
    this->flow_direction_upload_pending = true;
    this->icon_upload_pending = true;
    this->heatmap_upload_pending = true;
    this->tank_upload_pending = true;
    this->junction_instance_upload_pending = true;
    markUndergroundGeometryDirty();
    update();
}


void MapRhiWidget::setNetworkScreenTranslation(const QPointF &translation_pixels)
{
    if (this->network_screen_translation == translation_pixels)
        return;

    this->network_screen_translation = translation_pixels;
    update();
}


void MapRhiWidget::setSymbology(const MapRhiSymbology &symbology)
{
    MapRhiSymbology themed_symbology = symbology;
    const QColor window_color = palette().color(QPalette::Window);
    themed_symbology.icon_default_fill_color =
        networkSymbologyIconDefaultFillColor(window_color);

    const bool is_monitor_surface = this->surface_name == QStringLiteral("monitor");
    const bool is_2d_view = this->map_model->viewMode() != MapViewMode::ThreeD;
    if (is_monitor_surface && is_2d_view && isLightThemeWindowColor(window_color))
        themed_symbology.icon_default_fill_color = MonitorLightThemeIconFillColor;

    const bool base_symbology_changed =
        !this->symbology_initialized
        || this->applied_symbology.node_colors != themed_symbology.node_colors
        || this->applied_symbology.link_colors != themed_symbology.link_colors;
    const bool junction_changed =
        !this->symbology_initialized
        || this->applied_symbology.node_size_unit != themed_symbology.node_size_unit
        || this->applied_symbology.node_size_px != themed_symbology.node_size_px
        || this->applied_symbology.node_size_m != themed_symbology.node_size_m
        || this->applied_symbology.node_colors != themed_symbology.node_colors;
    const bool icon_changed =
        !this->symbology_initialized
        || this->applied_symbology.icon_size_unit != themed_symbology.icon_size_unit
        || this->applied_symbology.icon_size_px != themed_symbology.icon_size_px
        || this->applied_symbology.icon_size_m != themed_symbology.icon_size_m
        || this->applied_symbology.show_icons != themed_symbology.show_icons
        || this->applied_symbology.icon_default_fill_color
            != themed_symbology.icon_default_fill_color
        || this->applied_symbology.visual_node != themed_symbology.visual_node
        || this->applied_symbology.visual_link != themed_symbology.visual_link
        || this->applied_symbology.node_colors != themed_symbology.node_colors
        || this->applied_symbology.link_colors != themed_symbology.link_colors;
    const bool heatmap_data_changed =
        !this->symbology_initialized
        || this->applied_symbology.visual_heatmap != themed_symbology.visual_heatmap
        || this->applied_symbology.heatmap_fractions != themed_symbology.heatmap_fractions
        || this->applied_symbology.heatmap_palette != themed_symbology.heatmap_palette
        || this->applied_symbology.heatmap_palette_flipped
            != themed_symbology.heatmap_palette_flipped;
    const bool heatmap_style_changed =
        !this->symbology_initialized
        || this->applied_symbology.heatmap_radius_unit != themed_symbology.heatmap_radius_unit
        || this->applied_symbology.heatmap_radius_m != themed_symbology.heatmap_radius_m
        || this->applied_symbology.heatmap_radius_px != themed_symbology.heatmap_radius_px
        || this->applied_symbology.heatmap_solid_center_percent
            != themed_symbology.heatmap_solid_center_percent;
    const bool flow_direction_changed =
        !this->symbology_initialized
        || this->applied_symbology.show_flow_direction != themed_symbology.show_flow_direction
        || this->applied_symbology.flow_direction_size_px
            != themed_symbology.flow_direction_size_px
        || this->applied_symbology.flow_directions != themed_symbology.flow_directions
        || this->applied_symbology.link_thickness_unit != themed_symbology.link_thickness_unit
        || this->applied_symbology.link_thickness_px != themed_symbology.link_thickness_px
        || this->applied_symbology.link_thickness_m != themed_symbology.link_thickness_m
        || this->applied_symbology.link_colors != themed_symbology.link_colors;

    this->scene.setViewZoom(this->map_model->zoom());
    this->scene.setSymbology(themed_symbology);
    this->applied_symbology = themed_symbology;
    this->symbology_initialized = true;

    if (base_symbology_changed)
    {
        this->geometry_upload_pending = true;
        this->highlight_upload_pending = true;
        markUndergroundGeometryDirty();
    }
    if (junction_changed)
    {
        this->junction_instance_upload_pending = true;
        markUndergroundGeometryDirty();
    }
    if (flow_direction_changed)
        this->flow_direction_upload_pending = true;
    if (icon_changed)
    {
        this->icon_upload_pending = true;
        this->tank_upload_pending = true;
    }
    if (heatmap_data_changed)
    {
        this->heatmap_upload_pending = true;
        syncBasemapHeatmapOverlay();
    }
    else if (heatmap_style_changed)
    {
        syncBasemapHeatmapStyle();
    }

    update();
}

void MapRhiWidget::setVisualControlSettings(
    const NetworkSymbologySettings &settings)
{
    if (!this->symbology_initialized)
        return;

    const NetworkSymbologySettings bounded_settings = settings.bounded();
    MapRhiSymbology symbology = this->applied_symbology;
    symbology.node_size_unit = bounded_settings.node_size_unit;
    symbology.node_size_px = bounded_settings.node_size_px;
    symbology.node_size_m = bounded_settings.node_size_m;
    symbology.icon_size_unit = bounded_settings.icon_size_unit;
    symbology.icon_size_px = bounded_settings.icon_size_px;
    symbology.icon_size_m = bounded_settings.icon_size_m;
    symbology.link_thickness_unit = bounded_settings.link_thickness_unit;
    symbology.link_thickness_px = bounded_settings.link_thickness_px;
    symbology.link_thickness_m = bounded_settings.link_thickness_m;
    symbology.show_flow_direction = bounded_settings.show_flow_direction;
    symbology.flow_direction_size_px = bounded_settings.flow_direction_size_px;
    symbology.heatmap_opacity = bounded_settings.heatmap_opacity;
    symbology.heatmap_radius_unit = bounded_settings.heatmap_radius_unit;
    symbology.heatmap_radius_m = bounded_settings.heatmap_radius_m;
    symbology.heatmap_radius_px = bounded_settings.heatmap_radius_px;
    symbology.heatmap_solid_center_percent =
        bounded_settings.heatmap_solid_center_percent;
    setSymbology(symbology);
}

void MapRhiWidget::setTileRepository(MapTileRepository *tile_repository)
{
    if (this->tile_repository == tile_repository)
        return;

    if (this->tile_repository != nullptr)
        disconnect(this->tile_repository, nullptr, this, nullptr);

    this->tile_repository = tile_repository;
    if (this->basemap_renderer)
        this->basemap_renderer->setTileRepository(this->tile_repository);

    if (this->tile_repository != nullptr)
    {
        connect(this->tile_repository, &MapTileRepository::signalTileAvailable,
                this, [this](const QString &)
        {
            update();
        });
        connect(this->tile_repository, &MapTileRepository::signalTileRetryReady,
                this, [this](const QString &)
        {
            update();
        });
        connect(this->tile_repository, &MapTileRepository::signalTilesDeleted,
                this, [this]
        {
            if (this->basemap_renderer)
                this->basemap_renderer->invalidate();
            update();
        });
    }

    update();
}

void MapRhiWidget::setTerrainRepository(MapTerrainRepository *terrain_repository)
{
    if (this->terrain_repository == terrain_repository)
        return;

    if (this->terrain_repository != nullptr)
        disconnect(this->terrain_repository, nullptr, this, nullptr);

    this->terrain_repository = terrain_repository;
    if (this->basemap_renderer)
        this->basemap_renderer->setTerrainRepository(this->terrain_repository);

    if (this->terrain_repository != nullptr)
    {
        connect(this->terrain_repository, &MapTerrainRepository::signalTerrainTileAvailable,
                this, [this](const QString &key)
        {
            if (this->basemap_renderer)
                this->basemap_renderer->notifyTerrainTileAvailable(key);
            syncViewState();
            markUndergroundGeometryDirty();
            update();
        });
        connect(this->terrain_repository, &MapTerrainRepository::signalTerrainTileRetryReady,
                this, [this](const QString &)
        {
            update();
        });
    }

    markUndergroundGeometryDirty();
    this->heatmap_upload_pending = true;
    syncBasemapHeatmapOverlay();
    update();
}

void MapRhiWidget::setUndergroundMode(MapRhiUndergroundMode mode)
{
    if (this->underground_mode == mode)
        return;

    this->underground_mode = mode;
    if (mode == MapRhiUndergroundMode::XRay)
        markUndergroundGeometryDirty();
    update();
}

MapRhiUndergroundMode MapRhiWidget::undergroundMode() const
{
    return this->underground_mode;
}

void MapRhiWidget::setBackgroundOpacity(int opacity)
{
    const int bounded_opacity = qBound(0, opacity, 100);
    if (this->background_opacity == bounded_opacity)
        return;

    this->background_opacity = bounded_opacity;
    update();
}

void MapRhiWidget::setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid)
{
    this->scene.setSelectedEntity(entity_type, uuid);
    this->highlight_upload_pending = true;
    this->icon_upload_pending = true;
    this->tank_upload_pending = true;
    this->junction_instance_upload_pending = true;
    markUndergroundGeometryDirty();
    update();
}

void MapRhiWidget::setSimulationErrorEntities(
    const QHash<QUuid, InfrastructureEntity> &error_entities,
    const QSet<QUuid> &stale_entity_uuids)
{
    this->scene.setSimulationErrorEntities(error_entities, stale_entity_uuids);
    this->highlight_upload_pending = true;
    update();
}

void MapRhiWidget::initialize(QRhiCommandBuffer *command_buffer)
{
    Q_UNUSED(command_buffer);

    QRhi *current_rhi = rhi();
    if (current_rhi == nullptr)
    {
        reportFailure(QStringLiteral("QRhiWidget did not provide a QRhi instance"));
        return;
    }

    QRhiRenderTarget *target = renderTarget();
    if (target == nullptr || target->renderPassDescriptor() == nullptr)
    {
        reportFailure(QStringLiteral("QRhiWidget did not provide a render target"));
        return;
    }

    const bool rhi_changed = this->active_rhi != current_rhi;
    const bool render_pass_changed =
        this->render_pass_descriptor != target->renderPassDescriptor();
    if (rhi_changed)
    {
        resetGpuResources();
        this->active_rhi = current_rhi;
        this->ready_reported = false;
        this->failure_reported = false;
    }
    else if (render_pass_changed)
    {
        this->link_pipeline.reset();
        this->selected_link_pipeline.reset();
        this->node_pipeline.reset();
        this->node_overlay_pipeline.reset();
        this->icon_pipeline.reset();
        this->icon_overlay_pipeline.reset();
        this->heatmap_pipeline.reset();
        this->tank_pipeline.reset();
        this->junction_pipeline.reset();
        this->link_xray_pipeline.reset();
        this->junction_xray_pipeline.reset();
        this->link_no_depth_pipeline.reset();
        this->junction_no_depth_pipeline.reset();
    }

    this->render_pass_descriptor = target->renderPassDescriptor();

    syncViewState();
    if (this->scene.setViewZoom(this->map_model->zoom()))
    {
        this->flow_direction_upload_pending = true;
        this->icon_upload_pending = true;
        this->tank_upload_pending = true;
        this->junction_instance_upload_pending = true;
        markUndergroundGeometryDirty();
    }

    if (!createPersistentResources() || !ensureGeometryBuffers() || !createPipelines())
        return;
    if (this->basemap_renderer
        && !this->basemap_renderer->initialize(
            this->active_rhi, this->render_pass_descriptor,
            this->uniform_buffer.get(), sampleCount()))
    {
        reportFailure(QStringLiteral("Failed to initialize RHI basemap renderer"));
        return;
    }

    qInfo().noquote()
        << QStringLiteral("Desktop map RHI surface '%1' initialized using %2 with %3x MSAA.")
               .arg(this->surface_name, graphicsApiName())
               .arg(sampleCount());
}

void MapRhiWidget::render(QRhiCommandBuffer *command_buffer)
{
    if (this->active_rhi == nullptr || command_buffer == nullptr)
    {
        reportFailure(QStringLiteral("QRhi render called without an initialized QRhi context"));
        return;
    }

    QRhiRenderTarget *target = renderTarget();
    if (target == nullptr)
    {
        reportFailure(QStringLiteral("QRhiWidget did not provide a render target"));
        return;
    }

    syncViewState();
    if (this->scene.setViewZoom(this->map_model->zoom()))
    {
        this->flow_direction_upload_pending = true;
        this->icon_upload_pending = true;
        this->tank_upload_pending = true;
        this->junction_instance_upload_pending = true;
    }

    if (this->heatmap_upload_pending)
        rebuildHeatmapRenderVertices();

    if (!createPersistentResources() || !ensureGeometryBuffers() || !createPipelines())
        return;
    if (this->basemap_renderer
        && !this->basemap_renderer->initialize(
            this->active_rhi, this->render_pass_descriptor,
            this->uniform_buffer.get(), sampleCount()))
    {
        reportFailure(QStringLiteral("Failed to initialize RHI basemap renderer"));
        return;
    }

    const QMatrix4x4 view_projection = this->camera.viewProjectionMatrix(*this->active_rhi);
    std::array<float, 32> uniform_data{};
    const float *matrix_data = view_projection.constData();
    for (int index = 0; index < 16; ++index)
        uniform_data[size_t(index)] = matrix_data[index];
    uniform_data[16] = float(qMax(1, this->viewport_size.width()));
    uniform_data[17] = float(qMax(1, this->viewport_size.height()));
    const double horizontal_world_units_per_meter = this->scene.worldUnitsPerMeter();
    uniform_data[18] = this->scene.linkThicknessUnit() == NetworkSymbologySizeUnit::Meters
        && std::isfinite(horizontal_world_units_per_meter)
        && horizontal_world_units_per_meter > 0.0
        ? -float(this->scene.linkThicknessM() * horizontal_world_units_per_meter * 0.5)
        : float(this->scene.linkThicknessPx()) * 0.5f;
    uniform_data[19] = this->scene.nodeSizeUnit() == NetworkSymbologySizeUnit::Meters
        && std::isfinite(horizontal_world_units_per_meter)
        && horizontal_world_units_per_meter > 0.0
        ? -float(this->scene.nodeSizeM() * horizontal_world_units_per_meter * 0.5)
        : float(this->scene.nodeSizePx()) * 0.5f;
    uniform_data[20] = heatmapRadiusPixels();
    uniform_data[21] = qBound(0.0f, this->applied_symbology.heatmap_opacity / 100.0f, 1.0f);
    uniform_data[22] = qBound(0.0f,
        this->applied_symbology.heatmap_solid_center_percent / 100.0f, 0.9f);
    const double heatmap_scale = GeoWebMercator::zoomScale(
        this->map_model->zoom(), MapRenderCacheMath::ReferenceZoom);
    uniform_data[23] = float(heatmapRadiusPixels()
        / qMax(heatmap_scale, 0.000001));

    const QColor background_color = palette().color(QPalette::Window);
    uniform_data[24] = background_color.redF();
    uniform_data[25] = background_color.greenF();
    uniform_data[26] = background_color.blueF();
    uniform_data[27] = qBound(0.0f, this->background_opacity / 100.0f, 1.0f);
    uniform_data[28] = float(this->network_screen_translation.x());
    uniform_data[29] = -float(this->network_screen_translation.y());
    // Z carries the continuous 3D orbit depth used only by the junction shader.
    // It is deliberately independent from the discrete map/tile zoom so sphere
    // size cannot jump when the renderer changes tile LOD.
    uniform_data[30] = float(this->camera.orbitDistanceWorld());
    uniform_data[31] = this->scene.iconSizeUnit() == NetworkSymbologySizeUnit::Meters
        && std::isfinite(horizontal_world_units_per_meter)
        && horizontal_world_units_per_meter > 0.0
        ? -float(this->scene.iconSizeM() * horizontal_world_units_per_meter)
        : float(this->scene.iconSizePx());

    QRhiResourceUpdateBatch *resource_updates = this->active_rhi->nextResourceUpdateBatch();
    resource_updates->updateDynamicBuffer(
        this->uniform_buffer.get(), 0, CameraUniformBytes, uniform_data.data());

    if (this->icon_atlas_upload_pending)
    {
        resource_updates->uploadTexture(
            this->icon_atlas_texture.get(), mapRhiIconAtlasImage());
        this->icon_atlas_upload_pending = false;
    }

    if (this->tank_texture_upload_pending)
    {
        resource_updates->uploadTexture(
            this->tank_texture.get(), mapRhiTankAlbedoImage());
        resource_updates->generateMips(this->tank_texture.get());
        this->tank_texture_upload_pending = false;
    }

    if (this->geometry_upload_pending)
    {
        const QVector<MapRhiScene::LinkVertex> &link_vertices = this->scene.linkVertices();
        const QVector<MapRhiScene::NodeVertex> &node_vertices = this->scene.nodeVertices();
        if (!link_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->link_vertex_buffer.get(), 0,
                int(link_vertices.size() * qsizetype(sizeof(MapRhiScene::LinkVertex))),
                link_vertices.constData());
        }
        if (!node_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->node_vertex_buffer.get(), 0,
                int(node_vertices.size() * qsizetype(sizeof(MapRhiScene::NodeVertex))),
                node_vertices.constData());
        }
        this->geometry_upload_pending = false;
    }

    if (this->highlight_upload_pending)
    {
        const QVector<MapRhiScene::LinkVertex> &selected_link_vertices =
            this->scene.selectedLinkVertices();
        const QVector<MapRhiScene::NodeVertex> &selected_node_vertices =
            this->scene.selectedNodeVertices();
        const QVector<MapRhiScene::LinkVertex> &diagnostic_link_vertices =
            this->scene.diagnosticLinkVertices();
        const QVector<MapRhiScene::NodeVertex> &diagnostic_node_vertices =
            this->scene.diagnosticNodeVertices();

        if (!selected_link_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->selected_link_vertex_buffer.get(), 0,
                int(selected_link_vertices.size() * qsizetype(sizeof(MapRhiScene::LinkVertex))),
                selected_link_vertices.constData());
        }
        if (!selected_node_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->selected_node_vertex_buffer.get(), 0,
                int(selected_node_vertices.size() * qsizetype(sizeof(MapRhiScene::NodeVertex))),
                selected_node_vertices.constData());
        }
        if (!diagnostic_link_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->diagnostic_link_vertex_buffer.get(), 0,
                int(diagnostic_link_vertices.size() * qsizetype(sizeof(MapRhiScene::LinkVertex))),
                diagnostic_link_vertices.constData());
        }
        if (!diagnostic_node_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->diagnostic_node_vertex_buffer.get(), 0,
                int(diagnostic_node_vertices.size() * qsizetype(sizeof(MapRhiScene::NodeVertex))),
                diagnostic_node_vertices.constData());
        }
        this->highlight_upload_pending = false;
    }

    if (this->flow_direction_upload_pending)
    {
        const QVector<MapRhiScene::LinkVertex> &flow_direction_vertices =
            this->scene.flowDirectionVertices();
        if (!flow_direction_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->flow_direction_vertex_buffer.get(), 0,
                int(flow_direction_vertices.size() * qsizetype(sizeof(MapRhiScene::LinkVertex))),
                flow_direction_vertices.constData());
        }
        this->flow_direction_upload_pending = false;
    }

    if (this->icon_upload_pending)
    {
        const QVector<MapRhiScene::IconVertex> &icon_vertices = this->scene.iconVertices();
        if (!icon_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->icon_vertex_buffer.get(), 0,
                int(icon_vertices.size() * qsizetype(sizeof(MapRhiScene::IconVertex))),
                icon_vertices.constData());
        }
        this->icon_upload_pending = false;
    }

    if (this->heatmap_upload_pending)
    {
        if (!this->heatmap_render_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->heatmap_vertex_buffer.get(), 0,
                int(this->heatmap_render_vertices.size()
                    * qsizetype(sizeof(MapRhiScene::HeatmapVertex))),
                this->heatmap_render_vertices.constData());
        }
        this->heatmap_upload_pending = false;
    }

    if (this->tank_upload_pending)
    {
        if (!this->tank_model_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->tank_vertex_buffer.get(), 0,
                int(this->tank_model_vertices.size() * qsizetype(sizeof(MapRhiTankModelVertex))),
                this->tank_model_vertices.constData());
        }
        this->tank_upload_pending = false;
    }

    if (this->junction_mesh_upload_pending)
    {
        const QVector<MapRhiJunctionMeshVertex> &junction_mesh =
            mapRhiJunctionSphereMeshVertices();
        if (!junction_mesh.isEmpty())
            resource_updates->uploadStaticBuffer(
                this->junction_mesh_vertex_buffer.get(), junction_mesh.constData());
        this->junction_mesh_upload_pending = false;
    }

    if (this->junction_instance_upload_pending)
    {
        const QVector<MapRhiJunctionInstance> &junction_instances =
            this->scene.junctionInstances();
        if (!junction_instances.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->junction_instance_buffer.get(), 0,
                int(junction_instances.size() * qsizetype(sizeof(MapRhiJunctionInstance))),
                junction_instances.constData());
        }
        this->junction_instance_upload_pending = false;
    }

    if (this->underground_geometry_upload_pending)
    {
        if (!this->underground_link_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->underground_link_vertex_buffer.get(), 0,
                int(this->underground_link_vertices.size()
                    * qsizetype(sizeof(MapRhiScene::LinkVertex))),
                this->underground_link_vertices.constData());
        }
        if (!this->underground_junction_instances.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->underground_junction_instance_buffer.get(), 0,
                int(this->underground_junction_instances.size()
                    * qsizetype(sizeof(MapRhiJunctionInstance))),
                this->underground_junction_instances.constData());
        }
        this->underground_geometry_upload_pending = false;
    }

    if (this->basemap_renderer
        && !this->basemap_renderer->prepare(
            resource_updates, renderOriginWorld(), this->viewport_size))
    {
        resource_updates->release();
        reportFailure(QStringLiteral("Failed to prepare RHI basemap tiles"));
        return;
    }

    command_buffer->beginPass(target, background_color, {1.0f, 0}, resource_updates);

    const QSize output_size = target->pixelSize();
    command_buffer->setViewport(QRhiViewport(
        0.0f, 0.0f, float(output_size.width()), float(output_size.height())));

    if (this->basemap_renderer)
        this->basemap_renderer->draw(command_buffer);

    const bool is_2d_view = this->map_model->viewMode() != MapViewMode::ThreeD;

    QRhiGraphicsPipeline *node_render_pipeline = is_2d_view
        ? this->node_overlay_pipeline.get()
        : this->node_pipeline.get();

    if (is_2d_view
        && !this->heatmap_render_vertices.isEmpty()
        && this->applied_symbology.heatmap_opacity > 0)
    {
        command_buffer->setGraphicsPipeline(this->heatmap_pipeline.get());
        command_buffer->setShaderResources(this->heatmap_shader_resource_bindings.get());
        const QRhiCommandBuffer::VertexInput heatmap_binding(
            this->heatmap_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &heatmap_binding);
        command_buffer->draw(quint32(this->heatmap_render_vertices.size()));
    }

    const QVector<MapRhiScene::LinkVertex> &link_vertices = this->scene.linkVertices();
    const QVector<MapRhiJunctionInstance> &junction_instances =
        this->scene.junctionInstances();
    const QVector<MapRhiJunctionMeshVertex> &junction_mesh =
        mapRhiJunctionSphereMeshVertices();

    // Underground visualization is a terrain-occlusion override only. Draw its
    // no-depth pass before every regular network component so arrows, tanks,
    // icons and other network geometry remain authoritative and can cover it.
    if (!is_2d_view && this->underground_mode == MapRhiUndergroundMode::Solid)
    {
        if (!link_vertices.isEmpty())
        {
            command_buffer->setGraphicsPipeline(this->link_no_depth_pipeline.get());
            command_buffer->setShaderResources();
            const QRhiCommandBuffer::VertexInput link_binding(
                this->link_vertex_buffer.get(), 0);
            command_buffer->setVertexInput(0, 1, &link_binding);
            command_buffer->draw(quint32(link_vertices.size()));
        }

        if (!junction_instances.isEmpty() && !junction_mesh.isEmpty())
        {
            command_buffer->setGraphicsPipeline(this->junction_no_depth_pipeline.get());
            command_buffer->setShaderResources();
            const QRhiCommandBuffer::VertexInput junction_bindings[] = {
                {this->junction_mesh_vertex_buffer.get(), 0},
                {this->junction_instance_buffer.get(), 0}
            };
            command_buffer->setVertexInput(0, 2, junction_bindings);
            command_buffer->draw(
                quint32(junction_mesh.size()), quint32(junction_instances.size()));
        }
    }
    else if (!is_2d_view && this->underground_mode == MapRhiUndergroundMode::XRay)
    {
        if (!this->underground_link_vertices.isEmpty())
        {
            command_buffer->setGraphicsPipeline(this->link_xray_pipeline.get());
            command_buffer->setShaderResources();
            const QRhiCommandBuffer::VertexInput underground_link_binding(
                this->underground_link_vertex_buffer.get(), 0);
            command_buffer->setVertexInput(0, 1, &underground_link_binding);
            command_buffer->draw(quint32(this->underground_link_vertices.size()));
        }

        if (!this->underground_junction_instances.isEmpty() && !junction_mesh.isEmpty())
        {
            command_buffer->setGraphicsPipeline(this->junction_xray_pipeline.get());
            command_buffer->setShaderResources();
            const QRhiCommandBuffer::VertexInput underground_junction_bindings[] = {
                {this->junction_mesh_vertex_buffer.get(), 0},
                {this->underground_junction_instance_buffer.get(), 0}
            };
            command_buffer->setVertexInput(0, 2, underground_junction_bindings);
            command_buffer->draw(
                quint32(junction_mesh.size()),
                quint32(this->underground_junction_instances.size()));
        }
    }

    if (!link_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->link_pipeline.get());
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput link_binding(this->link_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &link_binding);
        command_buffer->draw(quint32(link_vertices.size()));
    }

    const QVector<MapRhiScene::LinkVertex> &flow_direction_vertices =
        this->scene.flowDirectionVertices();
    if (!flow_direction_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->link_pipeline.get());
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(
            this->flow_direction_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(flow_direction_vertices.size()));
    }

    const QVector<MapRhiScene::LinkVertex> &selected_link_vertices =
        this->scene.selectedLinkVertices();
    if (is_2d_view && !selected_link_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->selected_link_pipeline.get());
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(this->selected_link_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(selected_link_vertices.size()));
    }

    const QVector<MapRhiScene::LinkVertex> &diagnostic_link_vertices =
        this->scene.diagnosticLinkVertices();
    if (is_2d_view && !diagnostic_link_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->link_pipeline.get());
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(this->diagnostic_link_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(diagnostic_link_vertices.size()));
    }

    const QVector<MapRhiScene::NodeVertex> &node_vertices = this->scene.nodeVertices();
    if (!node_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(node_render_pipeline);
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput node_binding(this->node_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &node_binding);
        command_buffer->draw(quint32(node_vertices.size()));
    }

    if (!junction_instances.isEmpty() && !junction_mesh.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->junction_pipeline.get());
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput junction_bindings[] = {
            {this->junction_mesh_vertex_buffer.get(), 0},
            {this->junction_instance_buffer.get(), 0}
        };
        command_buffer->setVertexInput(0, 2, junction_bindings);
        command_buffer->draw(
            quint32(junction_mesh.size()), quint32(junction_instances.size()));
    }

    if (!this->tank_model_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->tank_pipeline.get());
        command_buffer->setShaderResources(this->tank_shader_resource_bindings.get());
        const QRhiCommandBuffer::VertexInput tank_binding(this->tank_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &tank_binding);
        command_buffer->draw(quint32(this->tank_model_vertices.size()));
    }

    if (!is_2d_view && !selected_link_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->selected_link_pipeline.get());
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(this->selected_link_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(selected_link_vertices.size()));
    }

    const QVector<MapRhiScene::NodeVertex> &selected_node_vertices =
        this->scene.selectedNodeVertices();
    if (!selected_node_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(node_render_pipeline);
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(this->selected_node_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(selected_node_vertices.size()));
    }

    if (!is_2d_view && !diagnostic_link_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->link_pipeline.get());
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(this->diagnostic_link_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(diagnostic_link_vertices.size()));
    }

    const QVector<MapRhiScene::NodeVertex> &diagnostic_node_vertices =
        this->scene.diagnosticNodeVertices();
    if (!diagnostic_node_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(node_render_pipeline);
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(this->diagnostic_node_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(diagnostic_node_vertices.size()));
    }

    // Icons are semantic markers and must remain visually authoritative over
    // every network stroke/highlight. In 2D the overlay pipeline also ignores
    // network depth so a pump or valve cannot be cut through by its link.
    const QVector<MapRhiScene::IconVertex> &icon_vertices = this->scene.iconVertices();
    if (!icon_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(
            is_2d_view ? this->icon_overlay_pipeline.get() : this->icon_pipeline.get());
        command_buffer->setShaderResources(this->icon_shader_resource_bindings.get());
        const QRhiCommandBuffer::VertexInput icon_binding(this->icon_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &icon_binding);
        command_buffer->draw(quint32(icon_vertices.size()));
    }

    command_buffer->endPass();
}

void MapRhiWidget::releaseResources()
{
    resetGpuResources();
    this->active_rhi = nullptr;
    this->render_pass_descriptor = nullptr;
    this->ready_reported = false;
}

void MapRhiWidget::changeEvent(QEvent *event)
{
    if (event != nullptr
        && this->symbology_initialized
        && (event->type() == QEvent::ApplicationPaletteChange
            || event->type() == QEvent::PaletteChange
            || event->type() == QEvent::StyleChange))
    {
        setSymbology(this->applied_symbology);
    }

    QRhiWidget::changeEvent(event);
}

void MapRhiWidget::resizeEvent(QResizeEvent *event)
{
    this->viewport_size = event->size();
    this->camera.setViewportSize(this->viewport_size);
    QRhiWidget::resizeEvent(event);
}

bool MapRhiWidget::createPersistentResources()
{
    if (this->active_rhi == nullptr)
        return false;

    if (!this->uniform_buffer)
    {
        this->uniform_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, CameraUniformBytes));
        if (!this->uniform_buffer || !this->uniform_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI camera uniform buffer"));
            return false;
        }
    }

    if (!this->shader_resource_bindings)
    {
        this->shader_resource_bindings.reset(this->active_rhi->newShaderResourceBindings());
        if (!this->shader_resource_bindings)
        {
            reportFailure(QStringLiteral("Failed to allocate RHI shader resource bindings"));
            return false;
        }
        this->shader_resource_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, this->uniform_buffer.get())
        });
        if (!this->shader_resource_bindings->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI shader resource bindings"));
            return false;
        }
    }

    if (!this->heatmap_shader_resource_bindings)
    {
        this->heatmap_shader_resource_bindings.reset(
            this->active_rhi->newShaderResourceBindings());
        if (!this->heatmap_shader_resource_bindings)
        {
            reportFailure(QStringLiteral("Failed to allocate RHI heatmap shader bindings"));
            return false;
        }
        this->heatmap_shader_resource_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage
                    | QRhiShaderResourceBinding::FragmentStage,
                this->uniform_buffer.get())
        });
        if (!this->heatmap_shader_resource_bindings->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI heatmap shader bindings"));
            return false;
        }
    }

    if (!this->icon_atlas_texture)
    {
        const QImage atlas_image = mapRhiIconAtlasImage();
        this->icon_atlas_texture.reset(this->active_rhi->newTexture(
            QRhiTexture::RGBA8, atlas_image.size()));
        if (!this->icon_atlas_texture || !this->icon_atlas_texture->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI icon atlas texture"));
            return false;
        }
        this->icon_atlas_upload_pending = true;
    }

    if (!this->icon_sampler)
    {
        this->icon_sampler.reset(this->active_rhi->newSampler(
            QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
            QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!this->icon_sampler || !this->icon_sampler->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI icon atlas sampler"));
            return false;
        }
    }

    if (!this->icon_shader_resource_bindings)
    {
        this->icon_shader_resource_bindings.reset(
            this->active_rhi->newShaderResourceBindings());
        if (!this->icon_shader_resource_bindings)
        {
            reportFailure(QStringLiteral("Failed to allocate RHI icon shader bindings"));
            return false;
        }
        this->icon_shader_resource_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, this->uniform_buffer.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                this->icon_atlas_texture.get(), this->icon_sampler.get())
        });
        if (!this->icon_shader_resource_bindings->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI icon shader bindings"));
            return false;
        }
    }

    if (!this->tank_texture)
    {
        const QImage tank_image = mapRhiTankAlbedoImage();
        if (tank_image.isNull())
        {
            reportFailure(QStringLiteral("Failed to load RHI tank model texture"));
            return false;
        }
        this->tank_texture.reset(this->active_rhi->newTexture(
            QRhiTexture::RGBA8, tank_image.size(), 1,
            QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips));
        if (!this->tank_texture || !this->tank_texture->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI tank model texture"));
            return false;
        }
        this->tank_texture_upload_pending = true;
    }

    if (!this->tank_sampler)
    {
        this->tank_sampler.reset(this->active_rhi->newSampler(
            QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
            QRhiSampler::Repeat, QRhiSampler::ClampToEdge));
        if (!this->tank_sampler || !this->tank_sampler->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI tank model sampler"));
            return false;
        }
    }

    if (!this->tank_shader_resource_bindings)
    {
        this->tank_shader_resource_bindings.reset(
            this->active_rhi->newShaderResourceBindings());
        if (!this->tank_shader_resource_bindings)
        {
            reportFailure(QStringLiteral("Failed to allocate RHI tank shader bindings"));
            return false;
        }
        this->tank_shader_resource_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, this->uniform_buffer.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                this->tank_texture.get(), this->tank_sampler.get())
        });
        if (!this->tank_shader_resource_bindings->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI tank shader bindings"));
            return false;
        }
    }

    return true;
}

bool MapRhiWidget::createPipelines()
{
    if (this->active_rhi == nullptr || this->render_pass_descriptor == nullptr
        || !this->shader_resource_bindings || !this->heatmap_shader_resource_bindings
        || !this->icon_shader_resource_bindings || !this->tank_shader_resource_bindings)
    {
        return false;
    }

    if (!this->link_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_link.vert.qsb"));
        const QShader fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_link.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI link shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiScene::LinkVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::LinkVertex, start_x))},
            {0, 1, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::LinkVertex, end_x))},
            {0, 2, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiScene::LinkVertex, along))},
            {0, 3, QRhiVertexInputAttribute::Float4,
             quint32(offsetof(MapRhiScene::LinkVertex, red))},
            {0, 4, QRhiVertexInputAttribute::Float,
             quint32(offsetof(MapRhiScene::LinkVertex, size_adjust_px))}
        });

        this->link_pipeline.reset(this->active_rhi->newGraphicsPipeline());
        this->link_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->link_pipeline->setVertexInputLayout(input_layout);
        this->link_pipeline->setShaderResourceBindings(this->shader_resource_bindings.get());
        this->link_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->link_pipeline->setSampleCount(sampleCount());
        this->link_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->link_pipeline->setDepthTest(true);
        this->link_pipeline->setDepthWrite(true);
        this->link_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        QRhiGraphicsPipeline::TargetBlend link_blend;
        link_blend.enable = true;
        this->link_pipeline->setTargetBlends({link_blend});
        if (!this->link_pipeline->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI link graphics pipeline"));
            return false;
        }
    }

    if (!this->selected_link_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_link.vert.qsb"));
        const QShader fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_link.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI selected-link shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiScene::LinkVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::LinkVertex, start_x))},
            {0, 1, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::LinkVertex, end_x))},
            {0, 2, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiScene::LinkVertex, along))},
            {0, 3, QRhiVertexInputAttribute::Float4,
             quint32(offsetof(MapRhiScene::LinkVertex, red))},
            {0, 4, QRhiVertexInputAttribute::Float,
             quint32(offsetof(MapRhiScene::LinkVertex, size_adjust_px))}
        });

        this->selected_link_pipeline.reset(this->active_rhi->newGraphicsPipeline());
        this->selected_link_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->selected_link_pipeline->setVertexInputLayout(input_layout);
        this->selected_link_pipeline->setShaderResourceBindings(
            this->shader_resource_bindings.get());
        this->selected_link_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->selected_link_pipeline->setSampleCount(sampleCount());
        this->selected_link_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);

        // Selection is a UI overlay. Do not depth-test it against the pipe it is
        // highlighting, otherwise equal-depth segment joins can make the cyan stroke
        // appear to weave in and out of the selected pipe in perspective view.
        this->selected_link_pipeline->setDepthTest(false);
        this->selected_link_pipeline->setDepthWrite(false);
        QRhiGraphicsPipeline::TargetBlend selected_blend;
        selected_blend.enable = true;
        this->selected_link_pipeline->setTargetBlends({selected_blend});
        if (!this->selected_link_pipeline->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI selected-link pipeline"));
            return false;
        }
    }

    if (!this->node_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_node.vert.qsb"));
        const QShader fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_node.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI node shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiScene::NodeVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::NodeVertex, center_x))},
            {0, 1, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiScene::NodeVertex, corner_x))},
            {0, 2, QRhiVertexInputAttribute::Float4,
             quint32(offsetof(MapRhiScene::NodeVertex, red))},
            {0, 3, QRhiVertexInputAttribute::Float,
             quint32(offsetof(MapRhiScene::NodeVertex, size_adjust_px))}
        });

        this->node_pipeline.reset(this->active_rhi->newGraphicsPipeline());
        this->node_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->node_pipeline->setVertexInputLayout(input_layout);
        this->node_pipeline->setShaderResourceBindings(this->shader_resource_bindings.get());
        this->node_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->node_pipeline->setSampleCount(sampleCount());
        this->node_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->node_pipeline->setDepthTest(true);
        this->node_pipeline->setDepthWrite(true);
        this->node_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        QRhiGraphicsPipeline::TargetBlend node_blend;
        node_blend.enable = true;
        this->node_pipeline->setTargetBlends({node_blend});
        if (!this->node_pipeline->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI node graphics pipeline"));
            return false;
        }
    }

    if (!this->node_overlay_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_node.vert.qsb"));
        const QShader fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_node.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI 2D node overlay shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiScene::NodeVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::NodeVertex, center_x))},
            {0, 1, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiScene::NodeVertex, corner_x))},
            {0, 2, QRhiVertexInputAttribute::Float4,
             quint32(offsetof(MapRhiScene::NodeVertex, red))},
            {0, 3, QRhiVertexInputAttribute::Float,
             quint32(offsetof(MapRhiScene::NodeVertex, size_adjust_px))}
        });

        this->node_overlay_pipeline.reset(this->active_rhi->newGraphicsPipeline());
        this->node_overlay_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->node_overlay_pipeline->setVertexInputLayout(input_layout);
        this->node_overlay_pipeline->setShaderResourceBindings(
            this->shader_resource_bindings.get());
        this->node_overlay_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->node_overlay_pipeline->setSampleCount(sampleCount());
        this->node_overlay_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->node_overlay_pipeline->setDepthTest(false);
        this->node_overlay_pipeline->setDepthWrite(false);
        QRhiGraphicsPipeline::TargetBlend node_overlay_blend;
        node_overlay_blend.enable = true;
        this->node_overlay_pipeline->setTargetBlends({node_overlay_blend});
        if (!this->node_overlay_pipeline->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI 2D node overlay pipeline"));
            return false;
        }
    }

    if (!this->icon_pipeline || !this->icon_overlay_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_icon.vert.qsb"));
        const QShader fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_icon.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI icon shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiScene::IconVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::IconVertex, center_x))},
            {0, 1, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiScene::IconVertex, offset_x_ratio))},
            {0, 2, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiScene::IconVertex, u))},
            {0, 3, QRhiVertexInputAttribute::Float4,
             quint32(offsetof(MapRhiScene::IconVertex, red))}
        });

        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;

        this->icon_pipeline.reset(this->active_rhi->newGraphicsPipeline());
        this->icon_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->icon_pipeline->setVertexInputLayout(input_layout);
        this->icon_pipeline->setShaderResourceBindings(
            this->icon_shader_resource_bindings.get());
        this->icon_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->icon_pipeline->setSampleCount(sampleCount());
        this->icon_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->icon_pipeline->setDepthTest(true);
        this->icon_pipeline->setDepthWrite(false);
        this->icon_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        this->icon_pipeline->setTargetBlends({blend});
        if (!this->icon_pipeline->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI icon graphics pipeline"));
            return false;
        }

        this->icon_overlay_pipeline.reset(this->active_rhi->newGraphicsPipeline());
        this->icon_overlay_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->icon_overlay_pipeline->setVertexInputLayout(input_layout);
        this->icon_overlay_pipeline->setShaderResourceBindings(
            this->icon_shader_resource_bindings.get());
        this->icon_overlay_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->icon_overlay_pipeline->setSampleCount(sampleCount());
        this->icon_overlay_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->icon_overlay_pipeline->setDepthTest(false);
        this->icon_overlay_pipeline->setDepthWrite(false);
        this->icon_overlay_pipeline->setTargetBlends({blend});
        if (!this->icon_overlay_pipeline->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI 2D icon overlay pipeline"));
            return false;
        }
    }

    if (!this->heatmap_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_heatmap.vert.qsb"));
        const QShader fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_heatmap.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI heatmap shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiScene::HeatmapVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::HeatmapVertex, center_x))},
            {0, 1, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiScene::HeatmapVertex, corner_x))},
            {0, 2, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::HeatmapVertex, red))}
        });

        this->heatmap_pipeline.reset(this->active_rhi->newGraphicsPipeline());
        this->heatmap_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->heatmap_pipeline->setVertexInputLayout(input_layout);
        this->heatmap_pipeline->setShaderResourceBindings(
            this->heatmap_shader_resource_bindings.get());
        this->heatmap_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->heatmap_pipeline->setSampleCount(sampleCount());
        this->heatmap_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->heatmap_pipeline->setDepthTest(true);
        this->heatmap_pipeline->setDepthWrite(false);
        this->heatmap_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        QRhiGraphicsPipeline::TargetBlend heatmap_blend;
        heatmap_blend.enable = true;
        this->heatmap_pipeline->setTargetBlends({heatmap_blend});
        if (!this->heatmap_pipeline->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI heatmap graphics pipeline"));
            return false;
        }
    }

    if (!this->tank_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_tank.vert.qsb"));
        const QShader fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_tank.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI tank model shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiTankModelVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiTankModelVertex, position_x))},
            {0, 1, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiTankModelVertex, normal_x))},
            {0, 2, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiTankModelVertex, u))},
            {0, 3, QRhiVertexInputAttribute::Float,
             quint32(offsetof(MapRhiTankModelVertex, selected))}
        });

        this->tank_pipeline.reset(this->active_rhi->newGraphicsPipeline());
        this->tank_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->tank_pipeline->setVertexInputLayout(input_layout);
        this->tank_pipeline->setShaderResourceBindings(
            this->tank_shader_resource_bindings.get());
        this->tank_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->tank_pipeline->setSampleCount(sampleCount());
        this->tank_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->tank_pipeline->setDepthTest(true);
        this->tank_pipeline->setDepthWrite(true);
        this->tank_pipeline->setCullMode(QRhiGraphicsPipeline::Back);
        this->tank_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        QRhiGraphicsPipeline::TargetBlend tank_blend;
        tank_blend.enable = true;
        this->tank_pipeline->setTargetBlends({tank_blend});
        if (!this->tank_pipeline->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI tank graphics pipeline"));
            return false;
        }
    }


    if (!this->junction_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_junction.vert.qsb"));
        const QShader fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_junction.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI junction sphere shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiJunctionMeshVertex))},
            {quint32(sizeof(MapRhiJunctionInstance)), QRhiVertexInputBinding::PerInstance}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiJunctionMeshVertex, position_x))},
            {0, 1, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiJunctionMeshVertex, normal_x))},
            {1, 2, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiJunctionInstance, center_x))},
            {1, 3, QRhiVertexInputAttribute::Float,
             quint32(offsetof(MapRhiJunctionInstance, radius_world))},
            {1, 4, QRhiVertexInputAttribute::Float4,
             quint32(offsetof(MapRhiJunctionInstance, red))},
            {1, 5, QRhiVertexInputAttribute::Float,
             quint32(offsetof(MapRhiJunctionInstance, selected))}
        });

        this->junction_pipeline.reset(this->active_rhi->newGraphicsPipeline());
        this->junction_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->junction_pipeline->setVertexInputLayout(input_layout);
        this->junction_pipeline->setShaderResourceBindings(this->shader_resource_bindings.get());
        this->junction_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->junction_pipeline->setSampleCount(sampleCount());
        this->junction_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->junction_pipeline->setDepthTest(true);
        this->junction_pipeline->setDepthWrite(true);
        this->junction_pipeline->setCullMode(QRhiGraphicsPipeline::Back);
        this->junction_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        if (!this->junction_pipeline->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI junction sphere pipeline"));
            return false;
        }
    }


    if (!this->link_xray_pipeline || !this->link_no_depth_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_link.vert.qsb"));
        const QShader normal_fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_link.frag.qsb"));
        const QShader xray_fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_link_xray.frag.qsb"));
        if (!vertex_shader.isValid() || !normal_fragment_shader.isValid()
            || !xray_fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI underground-link shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiScene::LinkVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::LinkVertex, start_x))},
            {0, 1, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::LinkVertex, end_x))},
            {0, 2, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiScene::LinkVertex, along))},
            {0, 3, QRhiVertexInputAttribute::Float4,
             quint32(offsetof(MapRhiScene::LinkVertex, red))},
            {0, 4, QRhiVertexInputAttribute::Float,
             quint32(offsetof(MapRhiScene::LinkVertex, size_adjust_px))}
        });

        if (!this->link_xray_pipeline)
        {
            this->link_xray_pipeline.reset(this->active_rhi->newGraphicsPipeline());
            this->link_xray_pipeline->setShaderStages({
                {QRhiShaderStage::Vertex, vertex_shader},
                {QRhiShaderStage::Fragment, xray_fragment_shader}
            });
            this->link_xray_pipeline->setVertexInputLayout(input_layout);
            this->link_xray_pipeline->setShaderResourceBindings(
                this->shader_resource_bindings.get());
            this->link_xray_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
            this->link_xray_pipeline->setSampleCount(sampleCount());
            this->link_xray_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
            this->link_xray_pipeline->setDepthTest(false);
            this->link_xray_pipeline->setDepthWrite(false);
            QRhiGraphicsPipeline::TargetBlend xray_blend;
            xray_blend.enable = true;
            this->link_xray_pipeline->setTargetBlends({xray_blend});
            if (!this->link_xray_pipeline->create())
            {
                reportFailure(QStringLiteral("Failed to create RHI underground-link X-ray pipeline"));
                return false;
            }
        }

        if (!this->link_no_depth_pipeline)
        {
            this->link_no_depth_pipeline.reset(this->active_rhi->newGraphicsPipeline());
            this->link_no_depth_pipeline->setShaderStages({
                {QRhiShaderStage::Vertex, vertex_shader},
                {QRhiShaderStage::Fragment, normal_fragment_shader}
            });
            this->link_no_depth_pipeline->setVertexInputLayout(input_layout);
            this->link_no_depth_pipeline->setShaderResourceBindings(
                this->shader_resource_bindings.get());
            this->link_no_depth_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
            this->link_no_depth_pipeline->setSampleCount(sampleCount());
            this->link_no_depth_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
            this->link_no_depth_pipeline->setDepthTest(false);
            this->link_no_depth_pipeline->setDepthWrite(false);
            QRhiGraphicsPipeline::TargetBlend solid_blend;
            solid_blend.enable = true;
            this->link_no_depth_pipeline->setTargetBlends({solid_blend});
            if (!this->link_no_depth_pipeline->create())
            {
                reportFailure(QStringLiteral("Failed to create RHI no-depth link pipeline"));
                return false;
            }
        }
    }

    if (!this->junction_xray_pipeline || !this->junction_no_depth_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_junction.vert.qsb"));
        const QShader normal_fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_junction.frag.qsb"));
        const QShader xray_fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_junction_xray.frag.qsb"));
        if (!vertex_shader.isValid() || !normal_fragment_shader.isValid()
            || !xray_fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI underground-junction shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiJunctionMeshVertex))},
            {quint32(sizeof(MapRhiJunctionInstance)), QRhiVertexInputBinding::PerInstance}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiJunctionMeshVertex, position_x))},
            {0, 1, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiJunctionMeshVertex, normal_x))},
            {1, 2, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiJunctionInstance, center_x))},
            {1, 3, QRhiVertexInputAttribute::Float,
             quint32(offsetof(MapRhiJunctionInstance, radius_world))},
            {1, 4, QRhiVertexInputAttribute::Float4,
             quint32(offsetof(MapRhiJunctionInstance, red))},
            {1, 5, QRhiVertexInputAttribute::Float,
             quint32(offsetof(MapRhiJunctionInstance, selected))}
        });

        if (!this->junction_xray_pipeline)
        {
            this->junction_xray_pipeline.reset(this->active_rhi->newGraphicsPipeline());
            this->junction_xray_pipeline->setShaderStages({
                {QRhiShaderStage::Vertex, vertex_shader},
                {QRhiShaderStage::Fragment, xray_fragment_shader}
            });
            this->junction_xray_pipeline->setVertexInputLayout(input_layout);
            this->junction_xray_pipeline->setShaderResourceBindings(
                this->shader_resource_bindings.get());
            this->junction_xray_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
            this->junction_xray_pipeline->setSampleCount(sampleCount());
            this->junction_xray_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
            this->junction_xray_pipeline->setDepthTest(false);
            this->junction_xray_pipeline->setDepthWrite(false);
            this->junction_xray_pipeline->setCullMode(QRhiGraphicsPipeline::Back);
            QRhiGraphicsPipeline::TargetBlend xray_blend;
            xray_blend.enable = true;
            this->junction_xray_pipeline->setTargetBlends({xray_blend});
            if (!this->junction_xray_pipeline->create())
            {
                reportFailure(QStringLiteral(
                    "Failed to create RHI underground-junction X-ray pipeline"));
                return false;
            }
        }

        if (!this->junction_no_depth_pipeline)
        {
            this->junction_no_depth_pipeline.reset(this->active_rhi->newGraphicsPipeline());
            this->junction_no_depth_pipeline->setShaderStages({
                {QRhiShaderStage::Vertex, vertex_shader},
                {QRhiShaderStage::Fragment, normal_fragment_shader}
            });
            this->junction_no_depth_pipeline->setVertexInputLayout(input_layout);
            this->junction_no_depth_pipeline->setShaderResourceBindings(
                this->shader_resource_bindings.get());
            this->junction_no_depth_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
            this->junction_no_depth_pipeline->setSampleCount(sampleCount());
            this->junction_no_depth_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
            this->junction_no_depth_pipeline->setDepthTest(false);
            this->junction_no_depth_pipeline->setDepthWrite(false);
            this->junction_no_depth_pipeline->setCullMode(QRhiGraphicsPipeline::Back);
            QRhiGraphicsPipeline::TargetBlend solid_blend;
            solid_blend.enable = true;
            this->junction_no_depth_pipeline->setTargetBlends({solid_blend});
            if (!this->junction_no_depth_pipeline->create())
            {
                reportFailure(QStringLiteral("Failed to create RHI no-depth junction pipeline"));
                return false;
            }
        }
    }

    return true;
}

bool MapRhiWidget::ensureGeometryBuffers()
{
    if (this->active_rhi == nullptr)
        return false;

    if (this->underground_geometry_dirty)
        rebuildUndergroundGeometry();

    if (this->tank_upload_pending)
        rebuildTankModelGeometry();

    const int required_link_bytes = boundedBufferSize(
        this->scene.linkVertices().size(), qsizetype(sizeof(MapRhiScene::LinkVertex)));
    const int required_node_bytes = boundedBufferSize(
        this->scene.nodeVertices().size(), qsizetype(sizeof(MapRhiScene::NodeVertex)));
    const int required_selected_link_bytes = boundedBufferSize(
        this->scene.selectedLinkVertices().size(), qsizetype(sizeof(MapRhiScene::LinkVertex)));
    const int required_selected_node_bytes = boundedBufferSize(
        this->scene.selectedNodeVertices().size(), qsizetype(sizeof(MapRhiScene::NodeVertex)));
    const int required_diagnostic_link_bytes = boundedBufferSize(
        this->scene.diagnosticLinkVertices().size(), qsizetype(sizeof(MapRhiScene::LinkVertex)));
    const int required_diagnostic_node_bytes = boundedBufferSize(
        this->scene.diagnosticNodeVertices().size(), qsizetype(sizeof(MapRhiScene::NodeVertex)));
    const int required_flow_direction_bytes = boundedBufferSize(
        this->scene.flowDirectionVertices().size(), qsizetype(sizeof(MapRhiScene::LinkVertex)));
    const int required_icon_bytes = boundedBufferSize(
        this->scene.iconVertices().size(), qsizetype(sizeof(MapRhiScene::IconVertex)));
    const int required_heatmap_bytes = boundedBufferSize(
        this->heatmap_render_vertices.size(), qsizetype(sizeof(MapRhiScene::HeatmapVertex)));
    const int required_tank_bytes = boundedBufferSize(
        this->tank_model_vertices.size(), qsizetype(sizeof(MapRhiTankModelVertex)));
    const QVector<MapRhiJunctionMeshVertex> &junction_mesh =
        mapRhiJunctionSphereMeshVertices();
    const int required_junction_mesh_bytes = boundedBufferSize(
        junction_mesh.size(), qsizetype(sizeof(MapRhiJunctionMeshVertex)));
    const int required_junction_instance_bytes = boundedBufferSize(
        this->scene.junctionInstances().size(), qsizetype(sizeof(MapRhiJunctionInstance)));
    const int required_underground_link_bytes = boundedBufferSize(
        this->underground_link_vertices.size(), qsizetype(sizeof(MapRhiScene::LinkVertex)));
    const int required_underground_junction_instance_bytes = boundedBufferSize(
        this->underground_junction_instances.size(), qsizetype(sizeof(MapRhiJunctionInstance)));
    if (required_link_bytes == 0 || required_node_bytes == 0
        || required_selected_link_bytes == 0 || required_selected_node_bytes == 0
        || required_diagnostic_link_bytes == 0 || required_diagnostic_node_bytes == 0
        || required_flow_direction_bytes == 0 || required_icon_bytes == 0
        || required_heatmap_bytes == 0 || required_tank_bytes == 0
        || required_junction_mesh_bytes == 0 || required_junction_instance_bytes == 0
        || required_underground_link_bytes == 0
        || required_underground_junction_instance_bytes == 0)
    {
        reportFailure(QStringLiteral("RHI network geometry exceeds supported buffer size"));
        return false;
    }

    if (!this->link_vertex_buffer || this->link_vertex_buffer_size != required_link_bytes)
    {
        this->link_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_link_bytes));
        if (!this->link_vertex_buffer || !this->link_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI link vertex buffer"));
            return false;
        }
        this->link_vertex_buffer_size = required_link_bytes;
        this->geometry_upload_pending = true;
    }

    if (!this->node_vertex_buffer || this->node_vertex_buffer_size != required_node_bytes)
    {
        this->node_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_node_bytes));
        if (!this->node_vertex_buffer || !this->node_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI node vertex buffer"));
            return false;
        }
        this->node_vertex_buffer_size = required_node_bytes;
        this->geometry_upload_pending = true;
    }

    if (!this->selected_link_vertex_buffer
        || this->selected_link_vertex_buffer_size != required_selected_link_bytes)
    {
        this->selected_link_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_selected_link_bytes));
        if (!this->selected_link_vertex_buffer || !this->selected_link_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI selected-link vertex buffer"));
            return false;
        }
        this->selected_link_vertex_buffer_size = required_selected_link_bytes;
        this->highlight_upload_pending = true;
    }

    if (!this->selected_node_vertex_buffer
        || this->selected_node_vertex_buffer_size != required_selected_node_bytes)
    {
        this->selected_node_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_selected_node_bytes));
        if (!this->selected_node_vertex_buffer || !this->selected_node_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI selected-node vertex buffer"));
            return false;
        }
        this->selected_node_vertex_buffer_size = required_selected_node_bytes;
        this->highlight_upload_pending = true;
    }

    if (!this->diagnostic_link_vertex_buffer
        || this->diagnostic_link_vertex_buffer_size != required_diagnostic_link_bytes)
    {
        this->diagnostic_link_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_diagnostic_link_bytes));
        if (!this->diagnostic_link_vertex_buffer || !this->diagnostic_link_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI diagnostic-link vertex buffer"));
            return false;
        }
        this->diagnostic_link_vertex_buffer_size = required_diagnostic_link_bytes;
        this->highlight_upload_pending = true;
    }

    if (!this->diagnostic_node_vertex_buffer
        || this->diagnostic_node_vertex_buffer_size != required_diagnostic_node_bytes)
    {
        this->diagnostic_node_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_diagnostic_node_bytes));
        if (!this->diagnostic_node_vertex_buffer || !this->diagnostic_node_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI diagnostic-node vertex buffer"));
            return false;
        }
        this->diagnostic_node_vertex_buffer_size = required_diagnostic_node_bytes;
        this->highlight_upload_pending = true;
    }

    if (!this->flow_direction_vertex_buffer
        || this->flow_direction_vertex_buffer_size != required_flow_direction_bytes)
    {
        this->flow_direction_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_flow_direction_bytes));
        if (!this->flow_direction_vertex_buffer || !this->flow_direction_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI flow-direction vertex buffer"));
            return false;
        }
        this->flow_direction_vertex_buffer_size = required_flow_direction_bytes;
        this->flow_direction_upload_pending = true;
    }

    if (!this->icon_vertex_buffer || this->icon_vertex_buffer_size != required_icon_bytes)
    {
        this->icon_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_icon_bytes));
        if (!this->icon_vertex_buffer || !this->icon_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI icon vertex buffer"));
            return false;
        }
        this->icon_vertex_buffer_size = required_icon_bytes;
        this->icon_upload_pending = true;
    }

    if (!this->heatmap_vertex_buffer
        || this->heatmap_vertex_buffer_size != required_heatmap_bytes)
    {
        this->heatmap_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_heatmap_bytes));
        if (!this->heatmap_vertex_buffer || !this->heatmap_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI heatmap vertex buffer"));
            return false;
        }
        this->heatmap_vertex_buffer_size = required_heatmap_bytes;
        this->heatmap_upload_pending = true;
    }

    if (!this->tank_vertex_buffer || this->tank_vertex_buffer_size != required_tank_bytes)
    {
        this->tank_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_tank_bytes));
        if (!this->tank_vertex_buffer || !this->tank_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI tank vertex buffer"));
            return false;
        }
        this->tank_vertex_buffer_size = required_tank_bytes;
        this->tank_upload_pending = true;
    }

    if (!this->junction_mesh_vertex_buffer
        || this->junction_mesh_vertex_buffer_size != required_junction_mesh_bytes)
    {
        this->junction_mesh_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, required_junction_mesh_bytes));
        if (!this->junction_mesh_vertex_buffer || !this->junction_mesh_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI junction sphere mesh buffer"));
            return false;
        }
        this->junction_mesh_vertex_buffer_size = required_junction_mesh_bytes;
        this->junction_mesh_upload_pending = true;
    }

    if (!this->junction_instance_buffer
        || this->junction_instance_buffer_size != required_junction_instance_bytes)
    {
        this->junction_instance_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_junction_instance_bytes));
        if (!this->junction_instance_buffer || !this->junction_instance_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI junction sphere instance buffer"));
            return false;
        }
        this->junction_instance_buffer_size = required_junction_instance_bytes;
        this->junction_instance_upload_pending = true;
    }


    if (!this->underground_link_vertex_buffer
        || this->underground_link_vertex_buffer_size != required_underground_link_bytes)
    {
        this->underground_link_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_underground_link_bytes));
        if (!this->underground_link_vertex_buffer
            || !this->underground_link_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI underground-link vertex buffer"));
            return false;
        }
        this->underground_link_vertex_buffer_size = required_underground_link_bytes;
        this->underground_geometry_upload_pending = true;
    }

    if (!this->underground_junction_instance_buffer
        || this->underground_junction_instance_buffer_size
            != required_underground_junction_instance_bytes)
    {
        this->underground_junction_instance_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
            required_underground_junction_instance_bytes));
        if (!this->underground_junction_instance_buffer
            || !this->underground_junction_instance_buffer->create())
        {
            reportFailure(QStringLiteral(
                "Failed to create RHI underground-junction instance buffer"));
            return false;
        }
        this->underground_junction_instance_buffer_size =
            required_underground_junction_instance_bytes;
        this->underground_geometry_upload_pending = true;
    }

    return true;
}

void MapRhiWidget::resetGpuResources()
{
    if (this->basemap_renderer)
        this->basemap_renderer->releaseResources();
    this->junction_xray_pipeline.reset();
    this->junction_no_depth_pipeline.reset();
    this->link_xray_pipeline.reset();
    this->link_no_depth_pipeline.reset();
    this->junction_pipeline.reset();
    this->tank_pipeline.reset();
    this->heatmap_pipeline.reset();
    this->icon_overlay_pipeline.reset();
    this->icon_pipeline.reset();
    this->node_overlay_pipeline.reset();
    this->node_pipeline.reset();
    this->selected_link_pipeline.reset();
    this->link_pipeline.reset();
    this->tank_shader_resource_bindings.reset();
    this->icon_shader_resource_bindings.reset();
    this->heatmap_shader_resource_bindings.reset();
    this->shader_resource_bindings.reset();
    this->tank_sampler.reset();
    this->icon_sampler.reset();
    this->tank_texture.reset();
    this->icon_atlas_texture.reset();
    this->underground_junction_instance_buffer.reset();
    this->underground_link_vertex_buffer.reset();
    this->junction_instance_buffer.reset();
    this->junction_mesh_vertex_buffer.reset();
    this->tank_vertex_buffer.reset();
    this->heatmap_vertex_buffer.reset();
    this->icon_vertex_buffer.reset();
    this->flow_direction_vertex_buffer.reset();
    this->diagnostic_node_vertex_buffer.reset();
    this->diagnostic_link_vertex_buffer.reset();
    this->selected_node_vertex_buffer.reset();
    this->selected_link_vertex_buffer.reset();
    this->node_vertex_buffer.reset();
    this->link_vertex_buffer.reset();
    this->uniform_buffer.reset();
    this->underground_junction_instance_buffer_size = 0;
    this->underground_link_vertex_buffer_size = 0;
    this->junction_instance_buffer_size = 0;
    this->junction_mesh_vertex_buffer_size = 0;
    this->tank_vertex_buffer_size = 0;
    this->heatmap_vertex_buffer_size = 0;
    this->icon_vertex_buffer_size = 0;
    this->flow_direction_vertex_buffer_size = 0;
    this->diagnostic_node_vertex_buffer_size = 0;
    this->diagnostic_link_vertex_buffer_size = 0;
    this->selected_node_vertex_buffer_size = 0;
    this->selected_link_vertex_buffer_size = 0;
    this->node_vertex_buffer_size = 0;
    this->link_vertex_buffer_size = 0;
    this->geometry_upload_pending = true;
    this->highlight_upload_pending = true;
    this->flow_direction_upload_pending = true;
    this->icon_upload_pending = true;
    this->heatmap_upload_pending = true;
    this->tank_upload_pending = true;
    this->junction_mesh_upload_pending = true;
    this->junction_instance_upload_pending = true;
    this->underground_geometry_upload_pending = true;
    this->underground_geometry_dirty = true;
    this->icon_atlas_upload_pending = true;
    this->tank_texture_upload_pending = true;
    this->heatmap_render_vertices.clear();
    this->tank_model_vertices.clear();
    this->underground_link_vertices.clear();
    this->underground_junction_instances.clear();
}

void MapRhiWidget::rebuildTankModelGeometry()
{
    this->tank_model_vertices = mapRhiBuildTankModelVertices(this->scene.tankInstances());
}

void MapRhiWidget::markUndergroundGeometryDirty()
{
    this->underground_geometry_dirty = true;
    this->underground_geometry_upload_pending = true;
}

double MapRhiWidget::terrainCellWorldSize() const
{
    if (this->map_model == nullptr)
        return 0.0;

    const int terrain_zoom = qBound(
        CameraTerrainMinimumZoom,
        this->map_model->zoom(),
        CameraTerrainMaximumZoom);
    const double tile_reference_size = MapModel::TileSize
        * std::pow(2.0, MapRenderCacheMath::ReferenceZoom - terrain_zoom);
    return tile_reference_size / MapTerrainTileCellCount;
}

bool MapRhiWidget::isUndergroundAtCoordinate(
    const CoordinateWGS84 &coordinate, double elevation_m)
{
    double terrain_elevation_m = 0.0;
    if (!terrainElevationAtCoordinate(coordinate, &terrain_elevation_m, false))
        return false;

    const double network_world_z = double(this->scene.elevationToWorldZ(elevation_m));
    const double terrain_world_z = double(this->scene.terrainElevationToWorldZ(
        terrain_elevation_m));
    const double world_units_per_meter = terrainWorldUnitsPerMeter();
    const double tolerance_world = std::isfinite(world_units_per_meter)
        ? qMax(0.0001, world_units_per_meter * 0.05)
        : 0.0001;
    return network_world_z < terrain_world_z - tolerance_world;
}

void MapRhiWidget::appendUndergroundLinkSegment(
    InfrastructureEntity entity_type, quint32 render_id,
    const QVector3D &start, const QVector3D &end)
{
    const QRgb color = this->applied_symbology.link_colors.value(
        render_id, networkSymbologyDefaultColor());
    const float corners[6][2] = {
        {0.0f, -1.0f},
        {1.0f, -1.0f},
        {1.0f, 1.0f},
        {0.0f, -1.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f}
    };

    for (int index = 0; index < 6; ++index)
    {
        MapRhiScene::LinkVertex vertex;
        vertex.start_x = start.x();
        vertex.start_y = start.y();
        vertex.start_z = start.z();
        vertex.end_x = end.x();
        vertex.end_y = end.y();
        vertex.end_z = end.z();
        vertex.along = corners[index][0];
        vertex.side = corners[index][1];
        vertex.red = qRed(color) / 255.0f;
        vertex.green = qGreen(color) / 255.0f;
        vertex.blue = qBlue(color) / 255.0f;
        vertex.alpha = qAlpha(color) / 255.0f;
        vertex.render_id = render_id;
        vertex.entity_type = entity_type;
        this->underground_link_vertices.append(vertex);
    }
}

void MapRhiWidget::rebuildUndergroundGeometry()
{
    this->underground_geometry_dirty = false;
    this->underground_link_vertices.clear();
    this->underground_junction_instances.clear();
    this->underground_geometry_upload_pending = true;

    if (this->underground_mode != MapRhiUndergroundMode::XRay
        || this->map_model == nullptr
        || this->map_model->viewMode() != MapViewMode::ThreeD
        || this->terrain_repository == nullptr
        || !this->scene.hasGeometry())
    {
        return;
    }

    const NetworkRenderSnapshot &snapshot = this->scene.networkSnapshot();
    const QPointF origin_world = this->scene.originWorld();
    const double terrain_cell_world = terrainCellWorldSize();
    const double target_segment_world = terrain_cell_world > 0.0
        ? qMax(terrain_cell_world * 0.5, 0.25)
        : 1.0;

    for (const NetworkRenderLink &link : snapshot.links)
    {
        if (this->scene.isEntityHidden(link.uuid) || link.vertices_wgs84.size() < 2)
            continue;

        bool have_previous = false;
        QVector3D previous_position;
        double previous_elevation_m = 0.0;
        double wrap_reference_x = origin_world.x();
        for (qsizetype vertex_index = 0;
             vertex_index < link.vertices_wgs84.size(); ++vertex_index)
        {
            const CoordinateWGS84 &coordinate = link.vertices_wgs84.at(vertex_index);
            if (!std::isfinite(coordinate.longitude_deg)
                || !std::isfinite(coordinate.latitude_deg))
            {
                have_previous = false;
                wrap_reference_x = origin_world.x();
                continue;
            }

            if (vertex_index >= link.elevations_m.size()
                || !std::isfinite(link.elevations_m.at(vertex_index)))
            {
                have_previous = false;
                wrap_reference_x = origin_world.x();
                continue;
            }

            const double elevation_m = link.elevations_m.at(vertex_index);
            double resolved_world_x = wrap_reference_x;
            const QVector3D current_position = this->scene.worldPosition(
                coordinate, elevation_m, wrap_reference_x, &resolved_world_x);
            wrap_reference_x = resolved_world_x;

            if (have_previous)
            {
                const double dx = double(current_position.x() - previous_position.x());
                const double dy = double(current_position.y() - previous_position.y());
                const double horizontal_length_world = std::hypot(dx, dy);
                const int subdivision_count = qBound(
                    1,
                    int(std::ceil(horizontal_length_world / target_segment_world)),
                    128);

                bool buried_run_active = false;
                double buried_run_start_ratio = 0.0;
                for (int subdivision = 0; subdivision < subdivision_count; ++subdivision)
                {
                    const double start_ratio = double(subdivision) / subdivision_count;
                    const double end_ratio = double(subdivision + 1) / subdivision_count;
                    const double middle_ratio = (start_ratio + end_ratio) * 0.5;
                    const double local_middle_x = double(previous_position.x())
                        + dx * middle_ratio;
                    const double local_middle_y = double(previous_position.y())
                        + dy * middle_ratio;
                    const CoordinateWGS84 middle_coordinate =
                        GeoWebMercator::worldPixelToLonLat(
                            origin_world.x() + local_middle_x,
                            origin_world.y() + local_middle_y,
                            MapRenderCacheMath::ReferenceZoom);
                    const double middle_elevation_m = previous_elevation_m
                        + (elevation_m - previous_elevation_m) * middle_ratio;
                    const bool buried = isUndergroundAtCoordinate(
                        middle_coordinate, middle_elevation_m);

                    if (buried && !buried_run_active)
                    {
                        buried_run_active = true;
                        buried_run_start_ratio = start_ratio;
                    }
                    else if (!buried && buried_run_active)
                    {
                        const QVector3D segment_start = previous_position
                            + (current_position - previous_position)
                                * float(buried_run_start_ratio);
                        const QVector3D segment_end = previous_position
                            + (current_position - previous_position) * float(start_ratio);
                        appendUndergroundLinkSegment(
                            link.entity_type, link.render_id, segment_start, segment_end);
                        buried_run_active = false;
                    }
                }

                if (buried_run_active)
                {
                    const QVector3D segment_start = previous_position
                        + (current_position - previous_position)
                            * float(buried_run_start_ratio);
                    appendUndergroundLinkSegment(
                        link.entity_type, link.render_id, segment_start, current_position);
                }
            }

            previous_position = current_position;
            previous_elevation_m = elevation_m;
            have_previous = true;
        }
    }

    QHash<quint32, MapRhiJunctionInstance> junction_instances_by_render_id;
    const QVector<MapRhiJunctionInstance> &junction_instances =
        this->scene.junctionInstances();
    junction_instances_by_render_id.reserve(junction_instances.size());
    for (const MapRhiJunctionInstance &instance : junction_instances)
        junction_instances_by_render_id.insert(instance.render_id, instance);

    for (const NetworkRenderNode &node : snapshot.nodes)
    {
        if (node.entity_type != InfrastructureEntity::Junction
            || this->scene.isEntityHidden(node.uuid)
            || !isUndergroundAtCoordinate(node.coordinate_wgs84, node.elevation_m))
        {
            continue;
        }

        const QHash<quint32, MapRhiJunctionInstance>::const_iterator instance_iterator =
            junction_instances_by_render_id.constFind(node.render_id);
        if (instance_iterator != junction_instances_by_render_id.cend())
            this->underground_junction_instances.append(instance_iterator.value());
    }
}

void MapRhiWidget::syncViewState()
{
    this->viewport_size = size();
    this->camera.setViewportSize(this->viewport_size);
    this->camera.setSceneOriginWorld(renderOriginWorld());
    this->camera.syncFromMapModel(*this->map_model);
    syncTerrainAwareCameraDistance();
    this->camera.syncFromMapModel(*this->map_model);

    const bool use_3d_models = this->map_model->viewMode() == MapViewMode::ThreeD;
    if (this->scene.setUse3dTankModels(use_3d_models))
    {
        this->icon_upload_pending = true;
        this->tank_upload_pending = true;
    }
    if (this->scene.setUse3dJunctionModels(use_3d_models))
    {
        this->geometry_upload_pending = true;
        this->junction_instance_upload_pending = true;
    }
}

bool MapRhiWidget::terrainElevationAtCoordinate(
    const CoordinateWGS84 &coordinate, double *elevation_m, bool request_missing_tile)
{
    if (elevation_m == nullptr || this->terrain_repository == nullptr
        || this->map_model == nullptr
        || !std::isfinite(coordinate.longitude_deg)
        || !std::isfinite(coordinate.latitude_deg)
        || this->map_model->zoom() < CameraTerrainMinimumZoom)
    {
        return false;
    }

    // Match the exact terrain LOD and triangle split used by
    // MapRhiBasemapRenderer. The old fixed-z14 bilinear sampler described a
    // different surface from the visible GPU mesh, which allowed an otherwise
    // correct ray hit to appear inside a steep mountain.
    const int terrain_zoom = qBound(
        CameraTerrainMinimumZoom,
        this->map_model->zoom(),
        CameraTerrainMaximumZoom);
    const double terrain_tile_x = GeoWebMercator::lonToTileX(
        coordinate.longitude_deg, terrain_zoom);
    const double terrain_tile_y = GeoWebMercator::latToTileY(
        coordinate.latitude_deg, terrain_zoom);
    const int terrain_tile_count = 1 << terrain_zoom;
    const int tile_x_unwrapped = int(std::floor(terrain_tile_x));
    const int tile_y = qBound(
        0, int(std::floor(terrain_tile_y)), terrain_tile_count - 1);
    const int tile_x = GeoWebMercator::wrapTileX(
        tile_x_unwrapped, terrain_zoom);
    const QString dataset = cameraTerrainDatasetId();

    const MapTerrainTile *terrain_tile = this->terrain_repository->tile(
        dataset, terrain_zoom, quint32(tile_x), quint32(tile_y));
    if (terrain_tile == nullptr)
    {
        if (request_missing_tile)
        {
            this->terrain_repository->requestTile(
                dataset, terrain_zoom, quint32(tile_x), quint32(tile_y));
        }
        return false;
    }

    const double local_u = terrain_tile_x - std::floor(terrain_tile_x);
    const double local_v = terrain_tile_y - std::floor(terrain_tile_y);
    const double sample_x = qBound(0.0, local_u, 1.0) * MapTerrainTileCellCount;
    const double sample_y = qBound(0.0, local_v, 1.0) * MapTerrainTileCellCount;
    const int cell_x = qBound(
        0, int(std::floor(sample_x)), MapTerrainTileCellCount - 1);
    const int cell_y = qBound(
        0, int(std::floor(sample_y)), MapTerrainTileCellCount - 1);
    const double tx = qBound(0.0, sample_x - cell_x, 1.0);
    const double ty = qBound(0.0, sample_y - cell_y, 1.0);
    const double u0 = double(cell_x) / MapTerrainTileCellCount;
    const double u1 = double(cell_x + 1) / MapTerrainTileCellCount;
    const double v0 = double(cell_y) / MapTerrainTileCellCount;
    const double v1 = double(cell_y + 1) / MapTerrainTileCellCount;
    const double z00 = bilinearTerrainElevation(*terrain_tile, u0, v0);
    const double z10 = bilinearTerrainElevation(*terrain_tile, u1, v0);
    const double z11 = bilinearTerrainElevation(*terrain_tile, u1, v1);
    const double z01 = bilinearTerrainElevation(*terrain_tile, u0, v1);
    if (!std::isfinite(z00) || !std::isfinite(z10)
        || !std::isfinite(z11) || !std::isfinite(z01))
    {
        return false;
    }

    // The renderer splits every terrain cell along z00 -> z11. Interpolate
    // on those same two triangles rather than across the bilinear quad.
    double sampled_elevation_m = 0.0;
    if (ty <= tx)
    {
        sampled_elevation_m = z00 * (1.0 - tx)
            + z10 * (tx - ty)
            + z11 * ty;
    }
    else
    {
        sampled_elevation_m = z00 * (1.0 - ty)
            + z11 * tx
            + z01 * (ty - tx);
    }

    if (!std::isfinite(sampled_elevation_m))
        return false;

    *elevation_m = sampled_elevation_m;
    return true;
}

void MapRhiWidget::rebuildHeatmapRenderVertices()
{
    this->heatmap_render_vertices.clear();
    if (this->map_model == nullptr
        || this->map_model->viewMode() == MapViewMode::ThreeD)
    {
        return;
    }

    this->heatmap_render_vertices = this->scene.heatmapVertices();
}

double MapRhiWidget::terrainWorldUnitsPerMeter() const
{
    if (this->map_model == nullptr)
        return 0.0;

    if (this->scene.hasGeometry())
    {
        const double terrain_zero_z = double(
            this->scene.terrainElevationToWorldZ(0.0));
        const double terrain_one_meter_z = double(
            this->scene.terrainElevationToWorldZ(1.0));
        const double world_units_per_meter = terrain_one_meter_z - terrain_zero_z;
        return std::isfinite(world_units_per_meter) && world_units_per_meter > 0.0
            ? world_units_per_meter : 0.0;
    }

    const double meters_per_world_pixel = GeoWebMercator::metersPerPixel(
        this->map_model->centerLat(), MapRenderCacheMath::ReferenceZoom);
    if (!std::isfinite(meters_per_world_pixel) || meters_per_world_pixel <= 0.0)
        return 0.0;

    return this->map_model->view3dVerticalExaggeration() / meters_per_world_pixel;
}

double MapRhiWidget::terrainWorldZ(
    double elevation_m, double world_units_per_meter) const
{
    if (!std::isfinite(elevation_m))
        return 0.0;

    if (this->scene.hasGeometry())
    {
        return double(this->scene.terrainElevationToWorldZ(elevation_m)) - 1.0;
    }

    return elevation_m * world_units_per_meter;
}

bool MapRhiWidget::terrainCoordinateAtScreen(
    const QPointF &screen_position, CoordinateWGS84 *coordinate,
    bool request_missing_tile)
{
    return terrainRayHitAtScreen(
        screen_position, coordinate, nullptr, nullptr, request_missing_tile);
}

bool MapRhiWidget::terrainRayHitAtScreen(
    const QPointF &screen_position, CoordinateWGS84 *coordinate,
    double *world_z, double *distance_m, bool request_missing_tile)
{
    if (coordinate == nullptr || this->map_model == nullptr
        || this->terrain_repository == nullptr
        || this->map_model->viewMode() != MapViewMode::ThreeD
        || !this->viewport_size.isValid())
    {
        return false;
    }

    const double world_units_per_meter = terrainWorldUnitsPerMeter();
    if (!std::isfinite(world_units_per_meter) || world_units_per_meter <= 0.0)
        return false;

    this->camera.setViewportSize(this->viewport_size);
    this->camera.setSceneOriginWorld(renderOriginWorld());
    this->camera.syncFromMapModel(*this->map_model);

    QVector3D eye_local;
    QVector3D direction_local;
    if (!this->camera.screenRay(screen_position, &eye_local, &direction_local)
        || direction_local.z() >= -1e-6f)
    {
        return false;
    }

    const QPointF origin_world = renderOriginWorld();
    const double terrain_tile_reference_size = MapModel::TileSize
        * std::pow(2.0, MapRenderCacheMath::ReferenceZoom - qBound(
            CameraTerrainMinimumZoom,
            this->map_model->zoom(),
            CameraTerrainMaximumZoom));
    const double terrain_cell_world = terrain_tile_reference_size
        / MapTerrainTileCellCount;
    const double horizontal_ray_speed = std::hypot(
        double(direction_local.x()), double(direction_local.y()));

    // Search strictly front-to-back so the coordinate always belongs to the
    // first visible terrain surface below the cursor, never a second surface
    // behind a ridge or mountain.
    double march_step_world = terrain_cell_world * 0.25;
    if (horizontal_ray_speed > 1e-9)
        march_step_world /= horizontal_ray_speed;
    else
        march_step_world = qMax(1.0, 5.0 * world_units_per_meter);
    march_step_world = qMax(0.25, march_step_world);

    const double native_search_world = qMax(
        this->camera.orbitDistanceWorld() * 8.0,
        terrain_cell_world * 256.0);
    const double maximum_search_world = qMin(
        native_search_world,
        qMax(terrain_cell_world * 256.0, 100000.0 * world_units_per_meter));
    if (!std::isfinite(maximum_search_world) || maximum_search_world <= 0.0)
        return false;

    const double surface_tolerance_world = qMax(
        0.01 * world_units_per_meter, 0.0005);

    double previous_distance_world = 0.0;
    double previous_clearance_world = 0.0;
    bool previous_sample_available = false;
    bool bracket_found = false;
    double bracket_near_world = 0.0;
    double bracket_far_world = 0.0;

    const int maximum_march_steps = 20000;
    double ray_distance_world = 0.0;
    for (int step = 0; step <= maximum_march_steps; ++step)
    {
        if (step == 0)
            ray_distance_world = 0.0;
        else
            ray_distance_world = qMin(
                maximum_search_world, ray_distance_world + march_step_world);

        const QVector3D point =
            eye_local + direction_local * float(ray_distance_world);
        const QPointF absolute_world(
            origin_world.x() + double(point.x()),
            origin_world.y() + double(point.y()));
        const CoordinateWGS84 sample_coordinate =
            GeoWebMercator::worldPixelToLonLat(
                absolute_world.x(), absolute_world.y(),
                MapRenderCacheMath::ReferenceZoom);

        double terrain_elevation_m = 0.0;
        if (!terrainElevationAtCoordinate(
                sample_coordinate, &terrain_elevation_m, request_missing_tile))
        {
            return false;
        }

        const double sampled_world_z = terrainWorldZ(
            terrain_elevation_m, world_units_per_meter);
        const double clearance_world = double(point.z()) - sampled_world_z;

        if (previous_sample_available
            && previous_clearance_world > 0.0
            && clearance_world <= 0.0)
        {
            bracket_found = true;
            bracket_near_world = previous_distance_world;
            bracket_far_world = ray_distance_world;
            break;
        }

        previous_distance_world = ray_distance_world;
        previous_clearance_world = clearance_world;
        previous_sample_available = true;

        if (ray_distance_world >= maximum_search_world)
            break;
    }

    if (!bracket_found)
        return false;

    for (int iteration = 0; iteration < 24; ++iteration)
    {
        const double midpoint_world =
            (bracket_near_world + bracket_far_world) * 0.5;
        const QVector3D point =
            eye_local + direction_local * float(midpoint_world);
        const QPointF absolute_world(
            origin_world.x() + double(point.x()),
            origin_world.y() + double(point.y()));
        const CoordinateWGS84 sample_coordinate =
            GeoWebMercator::worldPixelToLonLat(
                absolute_world.x(), absolute_world.y(),
                MapRenderCacheMath::ReferenceZoom);

        double terrain_elevation_m = 0.0;
        if (!terrainElevationAtCoordinate(
                sample_coordinate, &terrain_elevation_m, request_missing_tile))
        {
            return false;
        }

        const double sampled_world_z = terrainWorldZ(
            terrain_elevation_m, world_units_per_meter);
        const double clearance_world = double(point.z()) - sampled_world_z;

        if (clearance_world > 0.0)
            bracket_near_world = midpoint_world;
        else
            bracket_far_world = midpoint_world;

        if (bracket_far_world - bracket_near_world <= surface_tolerance_world)
            break;
    }

    const double hit_distance_world = bracket_far_world;
    const QVector3D hit_point =
        eye_local + direction_local * float(hit_distance_world);
    const QPointF hit_absolute_world(
        origin_world.x() + double(hit_point.x()),
        origin_world.y() + double(hit_point.y()));
    const CoordinateWGS84 hit_coordinate =
        GeoWebMercator::worldPixelToLonLat(
            hit_absolute_world.x(), hit_absolute_world.y(),
            MapRenderCacheMath::ReferenceZoom);

    double hit_terrain_elevation_m = 0.0;
    if (!terrainElevationAtCoordinate(
            hit_coordinate, &hit_terrain_elevation_m, request_missing_tile))
    {
        return false;
    }

    *coordinate = hit_coordinate;
    if (world_z != nullptr)
    {
        *world_z = terrainWorldZ(
            hit_terrain_elevation_m, world_units_per_meter);
    }
    if (distance_m != nullptr)
        *distance_m = hit_distance_world / world_units_per_meter;

    return true;
}

void MapRhiWidget::captureView3dFocusAnchor()
{
    if (this->map_model == nullptr || !this->viewport_size.isValid())
        return;

    CoordinateWGS84 hit_coordinate;
    double hit_world_z = 0.0;
    double distance_m = 0.0;
    if (!terrainRayHitAtScreen(
            QPointF(
                this->viewport_size.width() / 2.0,
                this->viewport_size.height() / 2.0),
            &hit_coordinate, &hit_world_z, &distance_m, true))
    {
        return;
    }

    this->map_model->setView3dFocusAnchor(
        hit_coordinate.longitude_deg,
        hit_coordinate.latitude_deg,
        hit_world_z,
        distance_m,
        this->viewport_size);
}

void MapRhiWidget::syncTerrainAwareCameraDistance()
{
    if (this->map_model == nullptr || this->terrain_repository == nullptr
        || this->map_model->viewMode() != MapViewMode::ThreeD
        || this->terrain_camera_distance_sync_active)
    {
        return;
    }

    QScopedValueRollback<bool> sync_guard(
        this->terrain_camera_distance_sync_active, true);

    const double world_units_per_meter = terrainWorldUnitsPerMeter();
    if (!std::isfinite(world_units_per_meter) || world_units_per_meter <= 0.0)
        return;

    const double native_distance_m = qMax(
        MapModel::MinView3dCameraDistanceM,
        this->camera.nativeOrbitDistanceWorld() / world_units_per_meter);
    this->map_model->syncView3dNativeCameraDistanceM(native_distance_m);

    const double requested_distance_m = qMax(
        MapModel::MinView3dCameraDistanceM,
        this->map_model->view3dCameraDistanceM());
    const double effective_distance_world = requested_distance_m * world_units_per_meter;

    const double pitch_rad = qDegreesToRadians(qBound(
        MapModel::MinView3dPitchDeg,
        this->map_model->view3dPitchDeg(),
        MapModel::MaxView3dPitchDeg));
    const double vertical_orbit_world = effective_distance_world * std::sin(pitch_rad);
    const double minimum_clearance_world =
        MapModel::MinView3dCameraGroundClearanceM * world_units_per_meter;

    const QPointF camera_world =
        this->camera.cameraGroundWorldPixelForDistance(effective_distance_world);
    const CoordinateWGS84 camera_coordinate = GeoWebMercator::worldPixelToLonLat(
        camera_world.x(), camera_world.y(), MapRenderCacheMath::ReferenceZoom);

    double camera_terrain_elevation_m = 0.0;
    const bool camera_terrain_available = terrainElevationAtCoordinate(
        camera_coordinate, &camera_terrain_elevation_m);
    const double camera_terrain_world_z = camera_terrain_available
        ? terrainWorldZ(camera_terrain_elevation_m, world_units_per_meter)
        : this->map_model->view3dVerticalOffsetWorld();

    if (this->map_model->view3dNavigationState() == MapView3dNavigationState::Pan)
    {
        // Keep terrain following feedback-free in X/Y, but ease the vertical
        // orbit-rig translation instead of snapping to every DEM sample.  This
        // removes most of the bumpy ride over rough or exaggerated terrain.
        // A hard eye-clearance floor remains active, so smoothing can never put
        // the camera below the required terrain clearance.
        if (camera_terrain_available)
        {
            constexpr double RiseTimeConstantSeconds = 0.14;
            constexpr double FallTimeConstantSeconds = 0.30;
            constexpr double MaximumSmoothingStepSeconds = 1.0 / 30.0;
            constexpr double MinimumSmoothingStepSeconds = 1.0 / 240.0;
            constexpr double SettleDistanceM = 0.05;

            const double safety_lift_world = qMax(
                0.0, minimum_clearance_world - vertical_orbit_world);
            const double target_offset_world =
                camera_terrain_world_z + safety_lift_world;
            const double current_offset_world =
                this->map_model->view3dVerticalOffsetWorld();
            const double hard_floor_world =
                camera_terrain_world_z + minimum_clearance_world - vertical_orbit_world;

            double elapsed_seconds = 1.0 / 60.0;
            if (this->terrain_pan_smoothing_clock.isValid())
            {
                const qint64 elapsed_ms = this->terrain_pan_smoothing_clock.restart();
                elapsed_seconds = qBound(
                    MinimumSmoothingStepSeconds,
                    double(elapsed_ms) / 1000.0,
                    MaximumSmoothingStepSeconds);
            }
            else
            {
                this->terrain_pan_smoothing_clock.start();
            }

            const double time_constant_seconds =
                target_offset_world >= current_offset_world
                ? RiseTimeConstantSeconds
                : FallTimeConstantSeconds;
            const double blend = 1.0 - std::exp(
                -elapsed_seconds / time_constant_seconds);
            double next_offset_world = current_offset_world
                + (target_offset_world - current_offset_world) * blend;

            next_offset_world = qMax(next_offset_world, hard_floor_world);

            if (std::abs(target_offset_world - next_offset_world)
                <= SettleDistanceM * world_units_per_meter)
            {
                next_offset_world = target_offset_world;
                this->terrain_pan_smoothing_clock.invalidate();
            }

            this->map_model->setView3dVerticalOffsetWorld(next_offset_world);
        }
        else
        {
            this->terrain_pan_smoothing_clock.invalidate();
        }

        this->map_model->setView3dCameraDistanceWorld(effective_distance_world);
        this->map_model->setView3dCameraCollisionLiftWorld(0.0);
        return;
    }

    this->terrain_pan_smoothing_clock.invalidate();

    // ROTATE state keeps the terrain point captured under the crosshair as an
    // immutable orbit target. Camera-ground collision may move only the eye
    // vertically; it must never rebase or move that target while rotating.
    const double focus_world_z = this->map_model->view3dVerticalOffsetWorld();
    double collision_lift_world = 0.0;
    if (camera_terrain_available)
    {
        const double eye_world_z = focus_world_z + vertical_orbit_world;
        collision_lift_world = qMax(
            0.0,
            camera_terrain_world_z + minimum_clearance_world - eye_world_z);
    }

    this->map_model->setView3dCameraDistanceWorld(effective_distance_world);
    this->map_model->setView3dCameraCollisionLiftWorld(collision_lift_world);
}

void MapRhiWidget::syncBasemapHeatmapOverlay()
{
    if (!this->basemap_renderer || this->map_model == nullptr)
        return;

    QVector<MapRhiBasemapRenderer::HeatmapMarker> markers;
    if (this->map_model->viewMode() == MapViewMode::ThreeD
        && this->applied_symbology.visual_heatmap != VisualHeatmap::None)
    {
        const QVector<MapRhiScene::HeatmapVertex> &vertices =
            this->scene.heatmapVertices();
        if (vertices.size() % 6 == 0)
        {
            markers.reserve(vertices.size() / 6);
            for (qsizetype index = 0; index < vertices.size(); index += 6)
            {
                const MapRhiScene::HeatmapVertex &vertex = vertices.at(index);
                MapRhiBasemapRenderer::HeatmapMarker marker;
                marker.center = QPointF(vertex.center_x, vertex.center_y);
                marker.color = QColor::fromRgbF(
                    vertex.red, vertex.green, vertex.blue, 1.0f);
                markers.append(marker);
            }
        }
    }

    const double heatmap_scale = GeoWebMercator::zoomScale(
        this->map_model->zoom(), MapRenderCacheMath::ReferenceZoom);
    const double radius_world = markers.isEmpty()
        ? 0.0
        : double(heatmapRadiusPixels()) / qMax(heatmap_scale, 0.000001);
    const double solid_fraction = qBound(
        0.0,
        double(this->applied_symbology.heatmap_solid_center_percent) / 100.0,
        0.9);
    this->basemap_renderer->setHeatmapOverlay(
        markers, radius_world, solid_fraction);
}

void MapRhiWidget::syncBasemapHeatmapStyle()
{
    if (!this->basemap_renderer || this->map_model == nullptr)
        return;

    double radius_world = 0.0;
    if (this->map_model->viewMode() == MapViewMode::ThreeD
        && this->applied_symbology.visual_heatmap != VisualHeatmap::None)
    {
        const double heatmap_scale = GeoWebMercator::zoomScale(
            this->map_model->zoom(), MapRenderCacheMath::ReferenceZoom);
        radius_world = double(heatmapRadiusPixels())
            / qMax(heatmap_scale, 0.000001);
    }

    const double solid_fraction = qBound(
        0.0,
        double(this->applied_symbology.heatmap_solid_center_percent) / 100.0,
        0.9);
    this->basemap_renderer->setHeatmapStyle(radius_world, solid_fraction);
}

QPointF MapRhiWidget::renderOriginWorld() const
{
    return this->scene.hasGeometry()
        ? this->scene.originWorld()
        : this->fallback_origin_world;
}

float MapRhiWidget::heatmapRadiusPixels() const
{
    if (this->applied_symbology.visual_heatmap == VisualHeatmap::None)
        return 1.0f;

    if (this->applied_symbology.heatmap_radius_unit == HeatmapRadiusUnit::Pixels)
        return float(qMax(1, this->applied_symbology.heatmap_radius_px));

    const double meters_per_pixel = GeoWebMercator::metersPerPixel(
        this->map_model->centerLat(), this->map_model->zoom());
    if (!std::isfinite(meters_per_pixel) || meters_per_pixel <= 0.0)
        return 1.0f;

    return float(qMax(1.0,
        this->applied_symbology.heatmap_radius_m / meters_per_pixel));
}

void MapRhiWidget::reportFailure(const QString &reason)
{
    if (this->failure_reported)
        return;

    this->failure_reported = true;
    qWarning().noquote()
        << QStringLiteral("Desktop map RHI surface '%1' failed: %2.")
               .arg(this->surface_name, reason);
    emit signalRendererFailed(reason);
}
