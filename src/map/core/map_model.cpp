#include "map/core/map_model.h"

#include "map/render/map_render_cache_math.h"
#include "config/gui_configuration.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QVector3D>
#include <QtMath>

namespace
{
constexpr double CoordinateComparisonEpsilon = 1e-12;
constexpr double View3dFieldOfViewDeg = 45.0;

struct View3dCameraBasis
{
    QVector3D eye;
    QVector3D forward;
    QVector3D right;
    QVector3D up;
};

double normalizedYawDegrees(double yaw_deg)
{
    double normalized = std::fmod(yaw_deg, 360.0);
    if (normalized < 0.0)
        normalized += 360.0;
    return normalized;
}

View3dCameraBasis view3dCameraBasis(double yaw_deg, double pitch_deg,
                                    double vertical_offset_pixels,
                                    double camera_distance_pixels,
                                    double collision_lift_pixels,
                                    const QSize &viewport)
{
    const double safe_height = qMax(1, viewport.height());
    const double half_height = safe_height / 2.0;
    const double half_fov_rad = qDegreesToRadians(View3dFieldOfViewDeg / 2.0);
    const double native_distance = half_height / std::tan(half_fov_rad);
    const double distance = camera_distance_pixels > 0.0
        ? camera_distance_pixels : native_distance;
    const double pitch_rad = qDegreesToRadians(qBound(
        MapModel::MinView3dPitchDeg, pitch_deg, MapModel::MaxView3dPitchDeg));
    const double yaw_rad = qDegreesToRadians(normalizedYawDegrees(yaw_deg));
    const double horizontal_distance = distance * std::cos(pitch_rad);

    const QVector3D target(0.0f, 0.0f, float(vertical_offset_pixels));

    View3dCameraBasis basis;
    basis.eye = QVector3D(
        float(std::sin(yaw_rad) * horizontal_distance),
        float(std::cos(yaw_rad) * horizontal_distance),
        float(vertical_offset_pixels + distance * std::sin(pitch_rad) + collision_lift_pixels));
    basis.forward = (target - basis.eye).normalized();
    // Web Mercator X grows eastward, so north-up yaw 0 has +X on screen-right.
    basis.right = QVector3D(
        float(std::cos(yaw_rad)), float(-std::sin(yaw_rad)), 0.0f);
    basis.up = QVector3D::crossProduct(basis.forward, basis.right).normalized();
    return basis;
}

bool coordinatesEqual(double first, double second)
{
    return std::abs(first - second) <= CoordinateComparisonEpsilon;
}
}

MapModel::MapModel(QObject *parent)
    : QObject(parent)
{
}

int MapModel::zoom() const
{
    return this->m_zoom;
}

double MapModel::view2dContinuousScale() const
{
    return this->m_view_2d_continuous_scale;
}

double MapModel::view2dContinuousZoom() const
{
    return double(this->m_zoom) + std::log2(qMax(1e-9, this->m_view_2d_continuous_scale));
}

double MapModel::centerLon() const
{
    return this->m_centerLon;
}

double MapModel::centerLat() const
{
    return this->m_centerLat;
}

MapProvider MapModel::provider() const
{
    return this->m_provider;
}

MapViewMode MapModel::viewMode() const
{
    return this->m_view_mode;
}

double MapModel::view3dYawDeg() const
{
    return this->m_view_3d_yaw_deg;
}

double MapModel::view3dPitchDeg() const
{
    return this->m_view_3d_pitch_deg;
}

double MapModel::view3dCameraDistanceM() const
{
    return this->m_view_3d_camera_distance_m;
}

double MapModel::view3dNativeCameraDistanceM() const
{
    return qMax(MinView3dCameraDistanceM, this->m_view_3d_native_camera_distance_m);
}

double MapModel::view3dMaximumCameraDistanceM() const
{
    // Absolute, user-adjustable (Settings > Map Settings > Map Performance
    // > View distance) -- deliberately independent of the current zoom
    // level. This used to be added on top of view3dNativeCameraDistanceM(),
    // a zoom-dependent "native" distance, which made the effective maximum
    // silently grow or shrink as the zoom level changed even though the
    // setting itself hadn't; the extended-maximum term below is unrelated
    // to that and is preserved as-is (it tracks how far a continuous
    // scroll/drag has already taken the camera, so it never gets yanked
    // back mid-gesture -- see setView3dContinuousCameraDistanceM()).
    return qMax(
        guiConfiguration().map_performance.max_view_distance_m,
        this->m_view_3d_extended_camera_distance_maximum_m);
}

double MapModel::view3dCameraDistanceWorld() const
{
    return this->m_view_3d_camera_distance_world;
}

double MapModel::view3dCameraCollisionLiftWorld() const
{
    return this->m_view_3d_camera_collision_lift_world;
}

double MapModel::view3dVerticalOffsetWorld() const
{
    return this->m_view_3d_vertical_offset_world;
}

double MapModel::view3dNetworkGroundOffsetM() const
{
    return this->m_view_3d_network_ground_offset_m;
}

double MapModel::view3dVerticalExaggeration() const
{
    return this->m_view_3d_vertical_exaggeration;
}

MapView3dNavigationState MapModel::view3dNavigationState() const
{
    return this->m_view_3d_navigation_state;
}

double MapModel::viewGlobeYawDeg() const
{
    return this->m_view_globe_yaw_deg;
}

double MapModel::viewGlobePitchDeg() const
{
    return this->m_view_globe_pitch_deg;
}

double MapModel::viewGlobeDistanceM() const
{
    return this->m_view_globe_distance_m;
}

int MapModel::tileCount() const
{
    return 1 << this->m_zoom;
}

QPointF MapModel::centerTile() const
{
    return QPointF(
        GeoWebMercator::lonToTileX(this->m_centerLon, this->m_zoom),
        GeoWebMercator::latToTileY(this->m_centerLat, this->m_zoom)
    );
}

QString MapModel::tileCacheKey(int x, int y) const
{
    const int wrapped_x = GeoWebMercator::wrapTileX(x, this->m_zoom);
    return tileCachePrefix(this->m_zoom) + QString("%1/%2").arg(wrapped_x).arg(y);
}

QString MapModel::tileCachePrefix(int zoom) const
{
    const int bounded_zoom = std::clamp(zoom, MinZoom, MaxZoom);
    return tileSourcePath(bounded_zoom) + QString("/%1/").arg(bounded_zoom);
}

QString MapModel::tileEndpoint(int x, int y) const
{
    const int wrapped_x = GeoWebMercator::wrapTileX(x, this->m_zoom);

    return QString("/%1/%2/%3/%4.png")
        .arg(tileSourcePath(this->m_zoom))
        .arg(this->m_zoom)
        .arg(wrapped_x)
        .arg(y);
}

QString MapModel::tileCacheKeyAtZoom(int x, int y, int zoom) const
{
    const int wrapped_x = GeoWebMercator::wrapTileX(x, zoom);
    return tileCachePrefix(zoom) + QString("%1/%2").arg(wrapped_x).arg(y);
}

QString MapModel::tileEndpointAtZoom(int x, int y, int zoom) const
{
    const int wrapped_x = GeoWebMercator::wrapTileX(x, zoom);

    return QString("/%1/%2/%3/%4.png")
        .arg(tileSourcePath(zoom))
        .arg(zoom)
        .arg(wrapped_x)
        .arg(y);
}

QString MapModel::providerPath() const
{
    switch (this->m_provider)
    {
    case MapProvider::ArcGISSat:
        return QStringLiteral("arcgis");
    case MapProvider::OpenTopoMap:
        return QStringLiteral("opentopomap");
    case MapProvider::OpenStreetMap:
        return QStringLiteral("openstreetmap");
    case MapProvider::OSMCyclo:
        return QStringLiteral("osmcyclo");
    }

    return QStringLiteral("arcgis");
}

QString MapModel::tileSourcePath(int zoom) const
{
    // Only this source currently provides zoom levels above 17 in the map backend.
    if (zoom > 17)
        return QStringLiteral("osmcyclo");

    return providerPath();
}

void MapModel::setView(double lon, double lat, int zoom_value, const QSize &viewport)
{
    const double old_lon = this->m_centerLon;
    const double old_lat = this->m_centerLat;
    const int old_zoom = this->m_zoom;

    this->m_zoom = std::clamp(zoom_value, MinZoom, MaxZoom);
    this->m_centerLon = GeoWebMercator::normalizeLongitude(lon);
    this->m_centerLat = std::clamp(
        lat, -GeoWebMercator::MaximumLatitude, GeoWebMercator::MaximumLatitude);

    if (viewport.isValid())
        clampCenter(viewport);

    const bool zoom_changed = this->m_zoom != old_zoom;
    const bool center_changed = !coordinatesEqual(this->m_centerLon, old_lon) ||
                                !coordinatesEqual(this->m_centerLat, old_lat);

    if (!zoom_changed && !center_changed)
        return;

    if (zoom_changed)
        emit zoomChanged(this->m_zoom);
    if (center_changed)
        emitCenterChanged();
}

void MapModel::fitViewToBounds(
    const CoordinateWGS84 &minimum, const CoordinateWGS84 &maximum,
    const QSize &viewport, double elevation_minimum_m, double elevation_maximum_m,
    bool allow_continuous_2d_zoom)
{
    constexpr double FitMarginFraction = 0.10;
    constexpr double MinimumWorldExtent = 1.0;

    const QSize safe_viewport = viewport.isValid() ? viewport : QSize(1024, 768);
    const double available_width = qMax(
        1.0, double(safe_viewport.width()) * (1.0 - 2.0 * FitMarginFraction));
    const double available_height = qMax(
        1.0, double(safe_viewport.height()) * (1.0 - 2.0 * FitMarginFraction));

    const QPointF minimum_world = GeoWebMercator::lonLatToWorldPixel(
        GeoWebMercator::normalizeLongitude(minimum.longitude_deg),
        minimum.latitude_deg,
        MapRenderCacheMath::ReferenceZoom);
    const QPointF maximum_world_raw = GeoWebMercator::lonLatToWorldPixel(
        GeoWebMercator::normalizeLongitude(maximum.longitude_deg),
        maximum.latitude_deg,
        MapRenderCacheMath::ReferenceZoom);
    const double maximum_world_x = GeoWebMercator::nearestWrappedWorldPixelX(
        maximum_world_raw.x(), minimum_world.x(), MapRenderCacheMath::ReferenceZoom);

    const double left_world = qMin(minimum_world.x(), maximum_world_x);
    const double right_world = qMax(minimum_world.x(), maximum_world_x);
    const double top_world = qMin(minimum_world.y(), maximum_world_raw.y());
    const double bottom_world = qMax(minimum_world.y(), maximum_world_raw.y());
    const double width_world = right_world - left_world;
    const double height_world = bottom_world - top_world;

    const QPointF center_world(
        (left_world + right_world) * 0.5,
        (top_world + bottom_world) * 0.5);
    const CoordinateWGS84 center = GeoWebMercator::worldPixelToLonLat(
        center_world.x(), center_world.y(), MapRenderCacheMath::ReferenceZoom);

    double fit_scale = std::numeric_limits<double>::infinity();
    if (width_world > 1e-9)
        fit_scale = qMin(fit_scale, available_width / width_world);
    if (height_world > 1e-9)
        fit_scale = qMin(fit_scale, available_height / height_world);
    if (!std::isfinite(fit_scale) || fit_scale <= 0.0)
        fit_scale = GeoWebMercator::zoomScale(MaxZoom, MapRenderCacheMath::ReferenceZoom);

    const double continuous_zoom = qBound(
        double(MinZoom),
        double(MapRenderCacheMath::ReferenceZoom) + std::log2(fit_scale),
        double(MaxZoom));
    const int tile_zoom = allow_continuous_2d_zoom
        ? qBound(MinZoom, qRound(continuous_zoom), MaxZoom)
        : qBound(MinZoom, int(std::floor(continuous_zoom)), MaxZoom);

    setView(center.longitude_deg, center.latitude_deg, tile_zoom, safe_viewport);
    if (allow_continuous_2d_zoom)
        setView2dContinuousZoom(continuous_zoom, safe_viewport);
    else
        resetView2dContinuousZoom(safe_viewport);

    if (this->m_view_mode != MapViewMode::ThreeD)
        return;

    const double meters_per_world_pixel = GeoWebMercator::metersPerPixel(
        center.latitude_deg, MapRenderCacheMath::ReferenceZoom);
    if (!std::isfinite(meters_per_world_pixel) || meters_per_world_pixel <= 0.0)
        return;

    const double horizontal_radius_world = 0.5 * std::hypot(
        qMax(MinimumWorldExtent, width_world),
        qMax(MinimumWorldExtent, height_world));
    const double elevation_span_m =
        std::isfinite(elevation_minimum_m) && std::isfinite(elevation_maximum_m)
            ? std::abs(elevation_maximum_m - elevation_minimum_m)
            : 0.0;
    const double vertical_radius_world = elevation_span_m
        * this->m_view_3d_vertical_exaggeration / meters_per_world_pixel;
    const double bounding_radius_world = qMax(
        MinimumWorldExtent,
        std::hypot(horizontal_radius_world, vertical_radius_world));

    const double half_vertical_fov_rad = qDegreesToRadians(View3dFieldOfViewDeg / 2.0);
    const double aspect = qMax(
        1e-6, double(safe_viewport.width()) / double(qMax(1, safe_viewport.height())));
    const double usable_vertical_tangent = std::tan(half_vertical_fov_rad)
        * available_height / double(qMax(1, safe_viewport.height()));
    const double usable_horizontal_tangent = std::tan(half_vertical_fov_rad)
        * aspect * available_width / double(qMax(1, safe_viewport.width()));
    const double limiting_half_angle = qMin(
        std::atan(usable_vertical_tangent),
        std::atan(usable_horizontal_tangent));
    const double safe_half_angle = qMax(qDegreesToRadians(1.0), limiting_half_angle);
    const double desired_distance_world = bounding_radius_world / std::sin(safe_half_angle);

    const double world_units_per_meter =
        this->m_view_3d_vertical_exaggeration / meters_per_world_pixel;
    const double desired_distance_m = qMax(
        MinView3dCameraDistanceM,
        desired_distance_world / qMax(1e-12, world_units_per_meter));

    this->m_view_3d_preserve_camera_distance_on_next_native_sync = true;
    setView3dContinuousCameraDistanceM(desired_distance_m);
    setView3dCameraDistanceWorld(desired_distance_world);
    setView3dCameraCollisionLiftWorld(0.0);
}

void MapModel::setCenter(double lon, double lat, const QSize &viewport)
{
    setView(lon, lat, this->m_zoom, viewport);
}

void MapModel::setZoom(int zoom_value, const QSize &viewport)
{
    const int bounded_zoom = std::clamp(zoom_value, MinZoom, MaxZoom);
    if (bounded_zoom != this->m_zoom)
        resetView2dContinuousZoom(viewport);
    setView(this->m_centerLon, this->m_centerLat, bounded_zoom, viewport);
}

void MapModel::zoomIn(const QSize &viewport)
{
    setZoom(this->m_zoom + 1, viewport);
}

void MapModel::zoomOut(const QSize &viewport)
{
    setZoom(this->m_zoom - 1, viewport);
}


void MapModel::setView2dContinuousZoom(double continuous_zoom, const QSize &viewport)
{
    if (!std::isfinite(continuous_zoom))
        return;

    const double bounded_zoom = qBound(
        double(MinZoom), continuous_zoom, double(MaxZoom));
    const int next_tile_zoom = qBound(
        MinZoom, qRound(bounded_zoom), MaxZoom);
    const double next_scale = std::exp2(bounded_zoom - double(next_tile_zoom));

    const int old_zoom = this->m_zoom;
    const double old_scale = this->m_view_2d_continuous_scale;
    const double old_lon = this->m_centerLon;
    const double old_lat = this->m_centerLat;

    this->m_zoom = next_tile_zoom;
    this->m_view_2d_continuous_scale = next_scale;
    if (viewport.isValid())
        clampCenter(viewport);

    if (this->m_zoom != old_zoom)
        emit zoomChanged(this->m_zoom);
    if (!coordinatesEqual(this->m_view_2d_continuous_scale, old_scale))
        emit view2dContinuousScaleChanged(this->m_view_2d_continuous_scale);
    if (!coordinatesEqual(this->m_centerLon, old_lon)
        || !coordinatesEqual(this->m_centerLat, old_lat))
    {
        emitCenterChanged();
    }
}

void MapModel::resetView2dContinuousZoom(const QSize &viewport)
{
    if (coordinatesEqual(this->m_view_2d_continuous_scale, 1.0))
        return;

    const double old_lon = this->m_centerLon;
    const double old_lat = this->m_centerLat;
    this->m_view_2d_continuous_scale = 1.0;
    if (viewport.isValid())
        clampCenter(viewport);
    emit view2dContinuousScaleChanged(this->m_view_2d_continuous_scale);
    if (!coordinatesEqual(this->m_centerLon, old_lon)
        || !coordinatesEqual(this->m_centerLat, old_lat))
    {
        emitCenterChanged();
    }
}

void MapModel::zoomByAt(int steps, const QPoint &anchorPos, const QSize &viewport)
{
    if (!viewport.isValid() || steps == 0)
        return;

    const int old_zoom = this->m_zoom;
    const int new_zoom = std::clamp(old_zoom + steps, MinZoom, MaxZoom);

    if (new_zoom == old_zoom)
        return;

    const double old_lon = this->m_centerLon;
    const double old_lat = this->m_centerLat;
    const QPointF old_center = centerTile();
    const double old_scale = qMax(1e-9, this->m_view_2d_continuous_scale);
    const double screen_offset_x_tiles =
        (anchorPos.x() - viewport.width() / 2.0) / TileSize;
    const double screen_offset_y_tiles =
        (anchorPos.y() - viewport.height() / 2.0) / TileSize;
    const double anchor_offset_x_old = screen_offset_x_tiles / old_scale;
    const double anchor_offset_y_old = screen_offset_y_tiles / old_scale;
    const double zoom_scale = GeoWebMercator::zoomScale(new_zoom, old_zoom);

    const double anchor_tile_x_new =
        (old_center.x() + anchor_offset_x_old) * zoom_scale;
    const double anchor_tile_y_new =
        (old_center.y() + anchor_offset_y_old) * zoom_scale;
    const double center_tile_x_new = anchor_tile_x_new - screen_offset_x_tiles;
    const double center_tile_y_new = anchor_tile_y_new - screen_offset_y_tiles;

    this->m_zoom = new_zoom;
    const bool scale_changed = !coordinatesEqual(this->m_view_2d_continuous_scale, 1.0);
    this->m_view_2d_continuous_scale = 1.0;
    this->m_centerLon = GeoWebMercator::normalizeLongitude(
        GeoWebMercator::tileXToLon(center_tile_x_new, this->m_zoom));
    this->m_centerLat = GeoWebMercator::tileYToLat(center_tile_y_new, this->m_zoom);
    clampCenter(viewport);

    emit zoomChanged(this->m_zoom);
    if (scale_changed)
        emit view2dContinuousScaleChanged(this->m_view_2d_continuous_scale);

    if (!coordinatesEqual(this->m_centerLon, old_lon) ||
        !coordinatesEqual(this->m_centerLat, old_lat))
    {
        emitCenterChanged();
    }
}

void MapModel::panByPixels(const QPoint &delta, const QSize &viewport)
{
    if (delta.isNull())
        return;

    QPointF center = centerTile();
    const double scale = this->m_view_mode == MapViewMode::TwoD
        ? qMax(1e-9, this->m_view_2d_continuous_scale)
        : 1.0;
    center.rx() -= double(delta.x()) / (TileSize * scale);
    center.ry() -= double(delta.y()) / (TileSize * scale);

    setCenter(
        GeoWebMercator::tileXToLon(center.x(), this->m_zoom),
        GeoWebMercator::tileYToLat(center.y(), this->m_zoom),
        viewport);
}

void MapModel::panByPixels3d(const QPoint &delta, const QSize &viewport)
{
    if (delta.isNull() || !viewport.isValid())
        return;

    const QPointF viewport_center(
        viewport.width() / 2.0, viewport.height() / 2.0);
    const QPointF center_ground = groundOffsetFromScreen3d(viewport_center, viewport);
    const QPointF dragged_ground = groundOffsetFromScreen3d(
        viewport_center - QPointF(delta), viewport);
    if (!std::isfinite(center_ground.x()) || !std::isfinite(center_ground.y())
        || !std::isfinite(dragged_ground.x()) || !std::isfinite(dragged_ground.y()))
    {
        return;
    }

    QPointF center = centerTile();
    const QPointF offset = dragged_ground - center_ground;
    center.rx() += offset.x() / TileSize;
    center.ry() += offset.y() / TileSize;

    setCenter(
        GeoWebMercator::tileXToLon(center.x(), this->m_zoom),
        GeoWebMercator::tileYToLat(center.y(), this->m_zoom),
        viewport);
}

void MapModel::panByPixels3dKeyboard(const QPoint &delta, const QSize &viewport)
{
    if (delta.isNull())
        return;

    // Keyboard movement uses the same map-distance-per-pixel as the 2D map.
    // Only yaw rotates the movement into the current screen orientation; pitch
    // deliberately does not participate, so movement speed cannot explode as
    // the camera approaches the horizon.
    const double yaw_rad = qDegreesToRadians(normalizedYawDegrees(this->m_view_3d_yaw_deg));
    const QPointF screen_right(std::cos(yaw_rad), -std::sin(yaw_rad));
    const QPointF screen_up(-std::sin(yaw_rad), -std::cos(yaw_rad));
    const QPointF map_delta =
        -screen_right * double(delta.x())
        + screen_up * double(delta.y());

    QPointF center = centerTile();
    center.rx() += map_delta.x() / TileSize;
    center.ry() += map_delta.y() / TileSize;

    setCenter(
        GeoWebMercator::tileXToLon(center.x(), this->m_zoom),
        GeoWebMercator::tileYToLat(center.y(), this->m_zoom),
        viewport);
}

void MapModel::clampCenter(const QSize &viewport)
{
    if (!viewport.isValid())
        return;

    const double world_tile_count = double(tileCount());
    const double scale = this->m_view_mode == MapViewMode::TwoD
        ? qMax(1e-9, this->m_view_2d_continuous_scale)
        : 1.0;
    const double half_viewport_height_tiles =
        viewport.height() / (double(TileSize) * scale) / 2.0;
    double center_tile_y = GeoWebMercator::latToTileY(this->m_centerLat, this->m_zoom);

    if (half_viewport_height_tiles >= world_tile_count / 2.0)
    {
        center_tile_y = world_tile_count / 2.0;
    }
    else
    {
        center_tile_y = std::clamp(
            center_tile_y,
            half_viewport_height_tiles,
            world_tile_count - half_viewport_height_tiles
        );
    }

    this->m_centerLat = GeoWebMercator::tileYToLat(center_tile_y, this->m_zoom);
}

void MapModel::setProvider(MapProvider provider)
{
    if (this->m_provider == provider)
        return;

    this->m_provider = provider;
    emit providerChanged(this->m_provider);
}

void MapModel::setViewMode(MapViewMode view_mode)
{
    if (this->m_view_mode == view_mode)
        return;

    this->m_view_mode = view_mode;
    if (this->m_view_mode != MapViewMode::ThreeD
        && this->m_view_3d_navigation_state == MapView3dNavigationState::Rotate)
    {
        this->m_view_3d_rotate_interaction_depth = 0;
        this->m_view_3d_navigation_state = MapView3dNavigationState::Pan;
        emit view3dNavigationStateChanged(this->m_view_3d_navigation_state);
    }
    emit viewModeChanged(this->m_view_mode);
}

void MapModel::setView3dYawDeg(double yaw_deg)
{
    const double next_yaw = normalizedYawDegrees(yaw_deg);
    if (coordinatesEqual(next_yaw, this->m_view_3d_yaw_deg))
        return;

    this->m_view_3d_yaw_deg = next_yaw;
    emit view3dCameraChanged();
}

void MapModel::setView3dPitchDeg(double pitch_deg)
{
    const double next_pitch = qBound(
        MinView3dPitchDeg, pitch_deg, MaxView3dPitchDeg);
    if (coordinatesEqual(next_pitch, this->m_view_3d_pitch_deg))
        return;

    this->m_view_3d_pitch_deg = next_pitch;
    emit view3dCameraChanged();
}

void MapModel::setView3dCameraDistanceM(double distance_m)
{
    if (!std::isfinite(distance_m))
        return;

    const double next_distance = qBound(
        MinView3dCameraDistanceM, distance_m, view3dMaximumCameraDistanceM());
    if (coordinatesEqual(next_distance, this->m_view_3d_camera_distance_m))
        return;

    this->m_view_3d_camera_distance_m = next_distance;
    emit view3dCameraChanged();
}

void MapModel::setView3dContinuousCameraDistanceM(double distance_m)
{
    if (!std::isfinite(distance_m))
        return;

    const double next_distance = qMax(MinView3dCameraDistanceM, distance_m);
    const double old_maximum_distance = view3dMaximumCameraDistanceM();
    this->m_view_3d_extended_camera_distance_maximum_m = qMax(
        this->m_view_3d_extended_camera_distance_maximum_m, next_distance);

    if (coordinatesEqual(next_distance, this->m_view_3d_camera_distance_m)
        && coordinatesEqual(old_maximum_distance, view3dMaximumCameraDistanceM()))
    {
        return;
    }

    this->m_view_3d_camera_distance_m = next_distance;
    emit view3dCameraChanged();
}

void MapModel::setView3dTileZoomPreservingCameraDistance(
    int zoom_value, const QSize &viewport)
{
    const int next_zoom = std::clamp(zoom_value, MinZoom, MaxZoom);
    if (next_zoom == this->m_zoom)
        return;

    const int old_zoom = this->m_zoom;
    const double old_lon = this->m_centerLon;
    const double old_lat = this->m_centerLat;
    const double old_native_distance = this->m_view_3d_native_camera_distance_m;
    const double old_maximum_distance = view3dMaximumCameraDistanceM();

    this->m_zoom = next_zoom;
    if (viewport.isValid())
        clampCenter(viewport);

    if (this->m_view_3d_native_camera_distance_initialized)
    {
        const double native_scale = std::pow(2.0, double(old_zoom - next_zoom));
        this->m_view_3d_native_camera_distance_m = qMax(
            MinView3dCameraDistanceM, old_native_distance * native_scale);
        this->m_view_3d_extended_camera_distance_maximum_m = qMax(
            this->m_view_3d_extended_camera_distance_maximum_m,
            this->m_view_3d_camera_distance_m);
        this->m_view_3d_preserve_camera_distance_on_next_native_sync = true;
    }

    emit zoomChanged(this->m_zoom);

    if (!coordinatesEqual(this->m_centerLon, old_lon)
        || !coordinatesEqual(this->m_centerLat, old_lat))
    {
        emitCenterChanged();
    }

    if (!coordinatesEqual(old_native_distance, this->m_view_3d_native_camera_distance_m)
        || !coordinatesEqual(old_maximum_distance, view3dMaximumCameraDistanceM()))
    {
        emit view3dCameraChanged();
    }
}

void MapModel::syncView3dNativeCameraDistanceM(double distance_m)
{
    if (!std::isfinite(distance_m))
        return;

    const double next_native_distance = qMax(MinView3dCameraDistanceM, distance_m);
    if (!this->m_view_3d_native_camera_distance_initialized)
    {
        this->m_view_3d_native_camera_distance_initialized = true;
        this->m_view_3d_native_camera_distance_m = next_native_distance;
        this->m_view_3d_camera_distance_m = next_native_distance;
        emit view3dCameraChanged();
        return;
    }

    if (this->m_view_3d_preserve_camera_distance_on_next_native_sync)
    {
        this->m_view_3d_preserve_camera_distance_on_next_native_sync = false;
        const double old_native_distance = this->m_view_3d_native_camera_distance_m;
        const double old_maximum_distance = view3dMaximumCameraDistanceM();
        this->m_view_3d_native_camera_distance_m = next_native_distance;
        this->m_view_3d_extended_camera_distance_maximum_m = qMax(
            this->m_view_3d_extended_camera_distance_maximum_m,
            this->m_view_3d_camera_distance_m);
        if (!coordinatesEqual(old_native_distance, this->m_view_3d_native_camera_distance_m)
            || !coordinatesEqual(old_maximum_distance, view3dMaximumCameraDistanceM()))
        {
            emit view3dCameraChanged();
        }
        return;
    }

    if (coordinatesEqual(
            next_native_distance, this->m_view_3d_native_camera_distance_m))
    {
        return;
    }

    const double native_delta_m =
        next_native_distance - this->m_view_3d_native_camera_distance_m;
    this->m_view_3d_native_camera_distance_m = next_native_distance;

    const double next_camera_distance_m = qMax(
        MinView3dCameraDistanceM,
        this->m_view_3d_camera_distance_m + native_delta_m);
    this->m_view_3d_extended_camera_distance_maximum_m = qMax(
        this->m_view_3d_extended_camera_distance_maximum_m,
        next_camera_distance_m);
    this->m_view_3d_camera_distance_m = qBound(
        MinView3dCameraDistanceM,
        next_camera_distance_m,
        view3dMaximumCameraDistanceM());
    emit view3dCameraChanged();
}

void MapModel::setView3dCameraDistanceWorld(double distance_world)
{
    if (!std::isfinite(distance_world) || distance_world <= 0.0
        || coordinatesEqual(distance_world, this->m_view_3d_camera_distance_world))
    {
        return;
    }

    this->m_view_3d_camera_distance_world = distance_world;
    emit view3dCameraChanged();
}

void MapModel::setView3dCameraCollisionLiftWorld(double lift_world)
{
    if (!std::isfinite(lift_world))
        return;

    const double next_lift = qMax(0.0, lift_world);
    if (coordinatesEqual(next_lift, this->m_view_3d_camera_collision_lift_world))
        return;

    this->m_view_3d_camera_collision_lift_world = next_lift;
    emit view3dCameraChanged();
}

void MapModel::setView3dVerticalOffsetWorld(double offset_world)
{
    if (!std::isfinite(offset_world)
        || coordinatesEqual(offset_world, this->m_view_3d_vertical_offset_world))
    {
        return;
    }

    this->m_view_3d_vertical_offset_world = offset_world;
    emit view3dCameraChanged();
}

void MapModel::setView3dNetworkGroundOffsetM(double offset_m)
{
    if (!std::isfinite(offset_m))
        return;

    const double bounded_offset_m = qBound(
        MinView3dNetworkGroundOffsetM,
        offset_m,
        MaxView3dNetworkGroundOffsetM);
    if (coordinatesEqual(bounded_offset_m, this->m_view_3d_network_ground_offset_m))
        return;

    this->m_view_3d_network_ground_offset_m = bounded_offset_m;
    emit view3dNetworkGroundOffsetChanged(bounded_offset_m);
}

void MapModel::setView3dVerticalExaggeration(double exaggeration)
{
    if (!std::isfinite(exaggeration))
        return;

    const double bounded_exaggeration = qBound(
        MinView3dVerticalExaggeration,
        exaggeration,
        MaxView3dVerticalExaggeration);
    if (coordinatesEqual(bounded_exaggeration, this->m_view_3d_vertical_exaggeration))
        return;

    this->m_view_3d_vertical_exaggeration = bounded_exaggeration;
    emit view3dCameraChanged();
}

void MapModel::setView3dFocusAnchor(
    double lon, double lat, double offset_world, double distance_m, const QSize &viewport)
{
    if (!std::isfinite(lon) || !std::isfinite(lat) || !std::isfinite(offset_world)
        || !std::isfinite(distance_m))
    {
        return;
    }

    const double old_lon = this->m_centerLon;
    const double old_lat = this->m_centerLat;
    const double old_offset_world = this->m_view_3d_vertical_offset_world;
    const double old_distance_m = this->m_view_3d_camera_distance_m;
    const double old_maximum_distance_m = view3dMaximumCameraDistanceM();

    this->m_centerLon = GeoWebMercator::normalizeLongitude(lon);
    this->m_centerLat = std::clamp(
        lat, -GeoWebMercator::MaximumLatitude, GeoWebMercator::MaximumLatitude);
    if (viewport.isValid())
        clampCenter(viewport);

    this->m_view_3d_vertical_offset_world = offset_world;
    const double captured_distance_m = qMax(MinView3dCameraDistanceM, distance_m);
    this->m_view_3d_extended_camera_distance_maximum_m = qMax(
        this->m_view_3d_extended_camera_distance_maximum_m,
        captured_distance_m);
    this->m_view_3d_camera_distance_m = captured_distance_m;

    const bool center_changed = !coordinatesEqual(this->m_centerLon, old_lon)
        || !coordinatesEqual(this->m_centerLat, old_lat);
    const bool camera_changed = !coordinatesEqual(
        this->m_view_3d_vertical_offset_world, old_offset_world)
        || !coordinatesEqual(this->m_view_3d_camera_distance_m, old_distance_m)
        || !coordinatesEqual(view3dMaximumCameraDistanceM(), old_maximum_distance_m);

    if (center_changed)
        emitCenterChanged();
    if (camera_changed)
        emit view3dCameraChanged();
}

void MapModel::beginView3dRotateInteraction()
{
    if (this->m_view_mode != MapViewMode::ThreeD)
        return;

    ++this->m_view_3d_rotate_interaction_depth;
    if (this->m_view_3d_navigation_state == MapView3dNavigationState::Rotate)
        return;

    this->m_view_3d_navigation_state = MapView3dNavigationState::Rotate;
    emit view3dNavigationStateChanged(this->m_view_3d_navigation_state);
}

void MapModel::endView3dRotateInteraction()
{
    if (this->m_view_3d_rotate_interaction_depth <= 0)
        return;

    --this->m_view_3d_rotate_interaction_depth;
    if (this->m_view_3d_rotate_interaction_depth > 0)
        return;

    if (this->m_view_3d_navigation_state == MapView3dNavigationState::Pan)
        return;

    this->m_view_3d_navigation_state = MapView3dNavigationState::Pan;
    emit view3dNavigationStateChanged(this->m_view_3d_navigation_state);
}

void MapModel::orbitView3d(double yaw_delta_deg, double pitch_delta_deg)
{
    const double next_yaw = normalizedYawDegrees(this->m_view_3d_yaw_deg + yaw_delta_deg);
    const double next_pitch = qBound(
        MinView3dPitchDeg,
        this->m_view_3d_pitch_deg + pitch_delta_deg,
        MaxView3dPitchDeg);
    if (coordinatesEqual(next_yaw, this->m_view_3d_yaw_deg)
        && coordinatesEqual(next_pitch, this->m_view_3d_pitch_deg))
    {
        return;
    }

    this->m_view_3d_yaw_deg = next_yaw;
    this->m_view_3d_pitch_deg = next_pitch;
    emit view3dCameraChanged();
}

void MapModel::orbitView3dByPointerDelta(const QPoint &delta_pixels, bool include_pitch)
{
    const double yaw_delta_deg =
        double(delta_pixels.x()) * View3dOrbitYawDegreesPerPixel;
    const double pitch_delta_deg = include_pitch
        ? double(-delta_pixels.y()) * View3dOrbitPitchDegreesPerPixel
        : 0.0;
    orbitView3d(yaw_delta_deg, pitch_delta_deg);
}

void MapModel::resetView3dCamera()
{
    const bool changed = !coordinatesEqual(this->m_view_3d_yaw_deg, 0.0)
        || !coordinatesEqual(this->m_view_3d_pitch_deg, DefaultView3dPitchDeg);
    this->m_view_3d_yaw_deg = 0.0;
    this->m_view_3d_pitch_deg = DefaultView3dPitchDeg;
    if (changed)
        emit view3dCameraChanged();
}

void MapModel::setViewGlobeYawDeg(double yaw_deg)
{
    const double next_yaw = normalizedYawDegrees(yaw_deg);
    if (coordinatesEqual(next_yaw, this->m_view_globe_yaw_deg))
        return;

    this->m_view_globe_yaw_deg = next_yaw;
    emit viewGlobeCameraChanged();
}

void MapModel::setViewGlobePitchDeg(double pitch_deg)
{
    const double next_pitch = qBound(
        MinViewGlobePitchDeg, pitch_deg, MaxViewGlobePitchDeg);
    if (coordinatesEqual(next_pitch, this->m_view_globe_pitch_deg))
        return;

    this->m_view_globe_pitch_deg = next_pitch;
    emit viewGlobeCameraChanged();
}

void MapModel::setViewGlobeDistanceM(double distance_m)
{
    if (!std::isfinite(distance_m))
        return;

    const double next_distance = qBound(
        MinViewGlobeDistanceM, distance_m, MaxViewGlobeDistanceM);
    if (coordinatesEqual(next_distance, this->m_view_globe_distance_m))
        return;

    this->m_view_globe_distance_m = next_distance;
    emit viewGlobeCameraChanged();
}

void MapModel::orbitViewGlobe(double yaw_delta_deg, double pitch_delta_deg)
{
    const double next_yaw = normalizedYawDegrees(this->m_view_globe_yaw_deg + yaw_delta_deg);
    const double next_pitch = qBound(
        MinViewGlobePitchDeg,
        this->m_view_globe_pitch_deg + pitch_delta_deg,
        MaxViewGlobePitchDeg);
    if (coordinatesEqual(next_yaw, this->m_view_globe_yaw_deg)
        && coordinatesEqual(next_pitch, this->m_view_globe_pitch_deg))
    {
        return;
    }

    this->m_view_globe_yaw_deg = next_yaw;
    this->m_view_globe_pitch_deg = next_pitch;
    emit viewGlobeCameraChanged();
}

void MapModel::orbitViewGlobeByPointerDelta(const QPoint &delta_pixels, bool include_pitch)
{
    const double yaw_delta_deg =
        double(delta_pixels.x()) * ViewGlobeOrbitYawDegreesPerPixel;
    const double pitch_delta_deg = include_pitch
        ? double(-delta_pixels.y()) * ViewGlobeOrbitPitchDegreesPerPixel
        : 0.0;
    orbitViewGlobe(yaw_delta_deg, pitch_delta_deg);
}

CoordinateWGS84 MapModel::wgs84FromScreen(const QPoint &pos, const QSize &viewport) const
{
    const QPointF center = centerTile();
    if (this->m_view_mode == MapViewMode::ThreeD)
    {
        const QPointF ground_offset = groundOffsetFromScreen3d(pos, viewport);
        if (std::isfinite(ground_offset.x()) && std::isfinite(ground_offset.y()))
        {
            const double tile_x = center.x() + ground_offset.x() / TileSize;
            const double tile_y = std::clamp(
                center.y() + ground_offset.y() / TileSize, 0.0, double(tileCount()));
            CoordinateWGS84 wgs;
            wgs.latitude_deg = GeoWebMercator::tileYToLat(tile_y, this->m_zoom);
            wgs.longitude_deg = GeoWebMercator::normalizeLongitude(
                GeoWebMercator::tileXToLon(tile_x, this->m_zoom));
            return wgs;
        }
    }

    const double view_scale = qMax(1e-9, this->m_view_2d_continuous_scale);
    const double tile_x = center.x()
        + (pos.x() - viewport.width() / 2.0) / (TileSize * view_scale);
    const double unclamped_tile_y = center.y()
        + (pos.y() - viewport.height() / 2.0) / (TileSize * view_scale);
    const double tile_y = std::clamp(unclamped_tile_y, 0.0, double(tileCount()));

    CoordinateWGS84 wgs;
    wgs.latitude_deg = GeoWebMercator::tileYToLat(tile_y, this->m_zoom);
    wgs.longitude_deg = GeoWebMercator::normalizeLongitude(
        GeoWebMercator::tileXToLon(tile_x, this->m_zoom));
    return wgs;
}

QPointF MapModel::screenFromWgs84(const CoordinateWGS84 &coord, const QSize &viewport) const
{
    return screenFromWgs84(coord.longitude_deg, coord.latitude_deg, viewport);
}

QPointF MapModel::screenFromWgs84(double lon, double lat, const QSize &viewport) const
{
    const QPointF center = centerTile();
    const double wrapped_lon = GeoWebMercator::normalizeLongitude(lon);
    const double base_tile_x = GeoWebMercator::lonToTileX(wrapped_lon, this->m_zoom);
    const double tile_x = GeoWebMercator::nearestWrappedTileX(
        base_tile_x, center.x(), this->m_zoom);
    const double tile_y = GeoWebMercator::latToTileY(lat, this->m_zoom);

    const QPointF offset_pixels(
        (tile_x - center.x()) * TileSize,
        (tile_y - center.y()) * TileSize);
    if (this->m_view_mode == MapViewMode::ThreeD)
        return screenFromTileOffset3d(offset_pixels, viewport);

    const double view_scale = qMax(1e-9, this->m_view_2d_continuous_scale);
    return QPointF(
        double(viewport.width()) / 2.0 + offset_pixels.x() * view_scale,
        double(viewport.height()) / 2.0 + offset_pixels.y() * view_scale);
}

QPointF MapModel::screenFromWgs84(const CoordinateWGS84 &coord, const QSize &viewport,
                                  double wrap_reference_lon) const
{
    return screenFromWgs84(
        coord.longitude_deg, coord.latitude_deg, viewport, wrap_reference_lon);
}

QPointF MapModel::screenFromWgs84(double lon, double lat, const QSize &viewport,
                                  double wrap_reference_lon) const
{
    const QPointF center = centerTile();
    const double wrapped_reference_lon = GeoWebMercator::normalizeLongitude(wrap_reference_lon);
    const double reference_base_tile_x = GeoWebMercator::lonToTileX(
        wrapped_reference_lon, this->m_zoom);
    const double reference_tile_x = GeoWebMercator::nearestWrappedTileX(
        reference_base_tile_x, center.x(), this->m_zoom);

    const double wrapped_lon = GeoWebMercator::normalizeLongitude(lon);
    const double base_tile_x = GeoWebMercator::lonToTileX(wrapped_lon, this->m_zoom);
    const double local_tile_x = GeoWebMercator::nearestWrappedTileX(
        base_tile_x, reference_base_tile_x, this->m_zoom);
    const double tile_x = local_tile_x + reference_tile_x - reference_base_tile_x;
    const double tile_y = GeoWebMercator::latToTileY(lat, this->m_zoom);

    const QPointF offset_pixels(
        (tile_x - center.x()) * TileSize,
        (tile_y - center.y()) * TileSize);
    if (this->m_view_mode == MapViewMode::ThreeD)
        return screenFromTileOffset3d(offset_pixels, viewport);

    const double view_scale = qMax(1e-9, this->m_view_2d_continuous_scale);
    return QPointF(
        double(viewport.width()) / 2.0 + offset_pixels.x() * view_scale,
        double(viewport.height()) / 2.0 + offset_pixels.y() * view_scale);
}

QPointF MapModel::groundOffsetFromScreen3d(
    const QPointF &position, const QSize &viewport) const
{
    if (!viewport.isValid())
        return QPointF(qQNaN(), qQNaN());

    const double vertical_offset_pixels = this->m_view_3d_vertical_offset_world
        * GeoWebMercator::zoomScale(this->m_zoom, MapRenderCacheMath::ReferenceZoom);
    const double camera_distance_pixels = this->m_view_3d_camera_distance_world
        * GeoWebMercator::zoomScale(this->m_zoom, MapRenderCacheMath::ReferenceZoom);
    const double collision_lift_pixels = this->m_view_3d_camera_collision_lift_world
        * GeoWebMercator::zoomScale(this->m_zoom, MapRenderCacheMath::ReferenceZoom);
    const View3dCameraBasis basis = view3dCameraBasis(
        this->m_view_3d_yaw_deg, this->m_view_3d_pitch_deg,
        vertical_offset_pixels, camera_distance_pixels, collision_lift_pixels, viewport);
    const double width = qMax(1, viewport.width());
    const double height = qMax(1, viewport.height());
    const double aspect = width / height;
    const double tan_half_fov = std::tan(qDegreesToRadians(View3dFieldOfViewDeg / 2.0));
    const double ndc_x = position.x() * 2.0 / width - 1.0;
    const double ndc_y = 1.0 - position.y() * 2.0 / height;

    QVector3D direction = basis.forward
        + basis.right * float(ndc_x * tan_half_fov * aspect)
        + basis.up * float(ndc_y * tan_half_fov);
    direction.normalize();
    if (std::abs(direction.z()) < 1e-6f)
        return QPointF(qQNaN(), qQNaN());

    const double distance = -double(basis.eye.z()) / double(direction.z());
    if (!std::isfinite(distance) || distance <= 0.0)
        return QPointF(qQNaN(), qQNaN());

    const QVector3D ground = basis.eye + direction * float(distance);
    return QPointF(ground.x(), ground.y());
}

QPointF MapModel::screenFromTileOffset3d(
    const QPointF &offset_pixels, const QSize &viewport) const
{
    if (!viewport.isValid())
        return QPointF(qQNaN(), qQNaN());

    const double vertical_offset_pixels = this->m_view_3d_vertical_offset_world
        * GeoWebMercator::zoomScale(this->m_zoom, MapRenderCacheMath::ReferenceZoom);
    const double camera_distance_pixels = this->m_view_3d_camera_distance_world
        * GeoWebMercator::zoomScale(this->m_zoom, MapRenderCacheMath::ReferenceZoom);
    const double collision_lift_pixels = this->m_view_3d_camera_collision_lift_world
        * GeoWebMercator::zoomScale(this->m_zoom, MapRenderCacheMath::ReferenceZoom);
    const View3dCameraBasis basis = view3dCameraBasis(
        this->m_view_3d_yaw_deg, this->m_view_3d_pitch_deg,
        vertical_offset_pixels, camera_distance_pixels, collision_lift_pixels, viewport);
    const QVector3D point(
        float(offset_pixels.x()), float(offset_pixels.y()), 0.0f);
    const QVector3D relative = point - basis.eye;
    const double depth = QVector3D::dotProduct(relative, basis.forward);
    if (depth <= 1e-6)
        return QPointF(qQNaN(), qQNaN());

    const double width = qMax(1, viewport.width());
    const double height = qMax(1, viewport.height());
    const double aspect = width / height;
    const double tan_half_fov = std::tan(qDegreesToRadians(View3dFieldOfViewDeg / 2.0));
    const double camera_x = QVector3D::dotProduct(relative, basis.right);
    const double camera_y = QVector3D::dotProduct(relative, basis.up);
    const double ndc_x = camera_x / (depth * tan_half_fov * aspect);
    const double ndc_y = camera_y / (depth * tan_half_fov);

    return QPointF(
        (ndc_x + 1.0) * width / 2.0,
        (1.0 - ndc_y) * height / 2.0);
}

void MapModel::emitCenterChanged()
{
    CoordinateWGS84 wgs;
    wgs.latitude_deg = this->m_centerLat;
    wgs.longitude_deg = this->m_centerLon;
    emit centerChangedWGS84(wgs);

    const CoordinateUTM utm = GeoMetricProjection::wgs84ToUtm(wgs);
    emit centerChangedUTM(utm);
}
