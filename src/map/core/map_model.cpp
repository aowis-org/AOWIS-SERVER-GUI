#include "map/core/map_model.h"

#include "map/render/map_render_cache_math.h"
#include "config/gui_configuration.h"
#include "geo/geo_wgs84_ellipsoid.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QQuaternion>
#include <QVector3D>
#include <QtMath>

namespace
{
constexpr double CoordinateComparisonEpsilon = 1e-12;
double normalizedYawDegrees(double yaw_deg)
{
    double normalized = std::fmod(yaw_deg, 360.0);
    if (normalized < 0.0)
        normalized += 360.0;
    return normalized;
}

QVector3D normalizedEcefDirection(const GeoWgs84Ellipsoid::EcefPositionD &position)
{
    const double length = std::sqrt(
        position.x * position.x
        + position.y * position.y
        + position.z * position.z);
    if (!std::isfinite(length) || length <= 1e-9)
        return QVector3D();

    return QVector3D(
        float(position.x / length),
        float(position.y / length),
        float(position.z / length));
}

double quaternionRotationAngleRadians(const QQuaternion &rotation)
{
    const QQuaternion normalized_rotation = rotation.normalized();
    const double scalar = qBound(
        -1.0, std::abs(double(normalized_rotation.scalar())), 1.0);
    return 2.0 * std::acos(scalar);
}

QQuaternion limitedRotation(
    const QQuaternion &rotation, double maximum_angle_rad)
{
    const double angle_rad = quaternionRotationAngleRadians(rotation);
    if (!(angle_rad > maximum_angle_rad) || angle_rad <= 1e-12)
        return rotation.normalized();

    const double fraction = qBound(0.0, maximum_angle_rad / angle_rad, 1.0);
    return QQuaternion::slerp(
        QQuaternion(1.0f, 0.0f, 0.0f, 0.0f),
        rotation.normalized(), float(fraction));
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

double MapModel::viewGlobeVerticalOffsetM() const
{
    return this->m_view_globe_vertical_offset_m;
}

double MapModel::viewGlobeCameraCollisionLiftM() const
{
    return this->m_view_globe_camera_collision_lift_m;
}

double MapModel::viewGlobeDistanceMForZoomLevel(
    double zoom_level, double latitude_deg, int viewport_height_px)
{
    const double latitude_clamped = qBound(
        -GlobeZoomFormulaMaxLatitudeDeg, latitude_deg, GlobeZoomFormulaMaxLatitudeDeg);
    const double circumference_m = 2.0 * M_PI * GeoWgs84Ellipsoid::EquatorialRadiusM;
    const double meters_per_pixel = circumference_m * std::cos(qDegreesToRadians(latitude_clamped))
        / (double(TileSize) * std::exp2(zoom_level));
    const double tan_half_fov = std::tan(qDegreesToRadians(GlobeFieldOfViewDeg / 2.0));
    const double safe_viewport_height = double(qMax(1, viewport_height_px));
    return safe_viewport_height * meters_per_pixel / (2.0 * tan_half_fov);
}

double MapModel::viewGlobeZoomLevelForDistanceM(
    double distance_m, double latitude_deg, int viewport_height_px)
{
    const double latitude_clamped = qBound(
        -GlobeZoomFormulaMaxLatitudeDeg, latitude_deg, GlobeZoomFormulaMaxLatitudeDeg);
    const double circumference_m = 2.0 * M_PI * GeoWgs84Ellipsoid::EquatorialRadiusM;
    const double tan_half_fov = std::tan(qDegreesToRadians(GlobeFieldOfViewDeg / 2.0));
    const double safe_viewport_height = double(qMax(1, viewport_height_px));
    const double meters_per_pixel_target =
        qMax(1.0, distance_m) * 2.0 * tan_half_fov / safe_viewport_height;
    return std::log2(
        circumference_m * std::cos(qDegreesToRadians(latitude_clamped))
        / (double(TileSize) * meters_per_pixel_target));
}

double MapModel::viewGlobeZoomLevel(const QSize &viewport) const
{
    return viewGlobeZoomLevelForDistanceM(
        this->m_view_globe_distance_m, this->m_centerLat,
        viewport.isValid() ? viewport.height() : GlobeZoomReferenceViewportHeightPx);
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

    // The flat 2D view is backed by Web Mercator and therefore cannot
    // represent the polar caps above +-85.05112878 degrees. Globe view is
    // different: its target is geodetic WGS84 converted directly to ECEF,
    // so the full physical latitude range through the true poles is valid.
    //
    // clampCenter() already deliberately skips the Web Mercator extent
    // clamp in Globe mode, but setCenter() comes through setView() first.
    // Keeping the unconditional Mercator clamp here therefore made the
    // globe's quaternion/ECEF pan math *look* pole-limited even though the
    // rotation itself is gimbal-lock safe.
    const double maximum_center_latitude = this->m_view_mode == MapViewMode::Globe
        ? 90.0
        : GeoWebMercator::MaximumLatitude;
    this->m_centerLat = std::clamp(
        lat, -maximum_center_latitude, maximum_center_latitude);

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

    if (this->m_view_mode == MapViewMode::Globe)
    {
        const double meters_per_world_pixel = GeoWebMercator::metersPerPixel(
            center.latitude_deg, MapRenderCacheMath::ReferenceZoom);
        if (!std::isfinite(meters_per_world_pixel) || meters_per_world_pixel <= 0.0)
            return;

        // Fit the same network bounds used by the 2D view into the
        // Globe perspective camera. A bounding sphere keeps the complete
        // network visible at any current yaw/pitch instead of only matching
        // the top-down Mercator zoom.
        const double horizontal_radius_m = 0.5 * meters_per_world_pixel * std::hypot(
            qMax(MinimumWorldExtent, width_world),
            qMax(MinimumWorldExtent, height_world));
        const double elevation_span_m =
            std::isfinite(elevation_minimum_m) && std::isfinite(elevation_maximum_m)
                ? std::abs(elevation_maximum_m - elevation_minimum_m)
                : 0.0;
        const double vertical_radius_m =
            elevation_span_m * this->m_view_3d_vertical_exaggeration;
        const double bounding_radius_m = qMax(
            meters_per_world_pixel * MinimumWorldExtent,
            std::hypot(horizontal_radius_m, vertical_radius_m));

        const double half_vertical_fov_rad =
            qDegreesToRadians(GlobeFieldOfViewDeg / 2.0);
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
        const double desired_distance_m =
            bounding_radius_m / std::sin(safe_half_angle);

        setViewGlobeDistanceM(desired_distance_m);
        return;
    }


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

void MapModel::zoomByAt(double steps, const QPoint &anchorPos, const QSize &viewport)
{
    if (!viewport.isValid() || !std::isfinite(steps) || coordinatesEqual(steps, 0.0))
        return;

    const int old_zoom = this->m_zoom;
    const double old_scale = qMax(1e-9, this->m_view_2d_continuous_scale);
    const double old_continuous_zoom =
        double(old_zoom) + std::log2(old_scale);
    const double new_continuous_zoom = qBound(
        double(MinZoom), old_continuous_zoom + steps, double(MaxZoom));
    if (coordinatesEqual(new_continuous_zoom, old_continuous_zoom))
        return;

    const int new_zoom = qBound(
        MinZoom, qRound(new_continuous_zoom), MaxZoom);
    const double new_scale = std::exp2(new_continuous_zoom - double(new_zoom));

    const double old_lon = this->m_centerLon;
    const double old_lat = this->m_centerLat;
    const QPointF old_center = centerTile();
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
    const double center_tile_x_new =
        anchor_tile_x_new - screen_offset_x_tiles / new_scale;
    const double center_tile_y_new =
        anchor_tile_y_new - screen_offset_y_tiles / new_scale;

    this->m_zoom = new_zoom;
    this->m_view_2d_continuous_scale = new_scale;
    this->m_centerLon = GeoWebMercator::normalizeLongitude(
        GeoWebMercator::tileXToLon(center_tile_x_new, this->m_zoom));
    this->m_centerLat = GeoWebMercator::tileYToLat(center_tile_y_new, this->m_zoom);
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

void MapModel::panByPixels(const QPoint &delta, const QSize &viewport)
{
    if (delta.isNull())
        return;

    QPointF center = centerTile();
    const double scale = qMax(1e-9, this->m_view_2d_continuous_scale);
    center.rx() -= double(delta.x()) / (TileSize * scale);
    center.ry() -= double(delta.y()) / (TileSize * scale);

    setCenter(
        GeoWebMercator::tileXToLon(center.x(), this->m_zoom),
        GeoWebMercator::tileYToLat(center.y(), this->m_zoom),
        viewport);
}

bool MapModel::globeScreenRay(
    const QPoint &screen_position, const QSize &viewport,
    QVector3D *eye, QVector3D *direction) const
{
    const double pitch_deg = qBound(
        MinViewGlobePitchDeg, this->m_view_globe_pitch_deg, MaxViewGlobePitchDeg);
    const double distance_m = qMax(MinViewGlobeDistanceM, this->m_view_globe_distance_m);
    const GeoWgs84Ellipsoid::OrbitCameraBasis basis = GeoWgs84Ellipsoid::orbitCameraBasis(
        this->m_centerLon, this->m_centerLat,
        this->m_view_globe_yaw_deg, pitch_deg, distance_m,
        this->m_view_globe_vertical_offset_m,
        this->m_view_globe_camera_collision_lift_m);

    // Same FOV as the Globe GPU projection, using the shared NDC -> ray
    // formula. This has to stay in lockstep with the rendered camera so the
    // ray corresponds to the pixel the person is actually looking at.
    return GeoWgs84Ellipsoid::screenRay(
        basis, QPointF(screen_position), viewport, GlobeFieldOfViewDeg, eye, direction);
}

bool MapModel::globeCoordinateAtScreen(
    const QPoint &screen_position, const QSize &viewport, CoordinateWGS84 *coordinate) const
{
    if (coordinate == nullptr)
        return false;

    QVector3D eye;
    QVector3D direction;
    if (!globeScreenRay(screen_position, viewport, &eye, &direction))
        return false;

    QVector3D intersection;
    if (!GeoWgs84Ellipsoid::rayIntersection(eye, direction, &intersection))
        return false;

    double lon_deg = 0.0;
    double lat_deg = 0.0;
    if (!GeoWgs84Ellipsoid::ecefToGeodetic(intersection, &lon_deg, &lat_deg))
        return false;

    coordinate->longitude_deg = lon_deg;
    coordinate->latitude_deg = lat_deg;
    return true;
}

void MapModel::panGlobeByPointerDrag(
    const QPoint &previous_screen_position, const QPoint &new_screen_position,
    const QSize &viewport)
{
    if (previous_screen_position == new_screen_position)
        return;

    QVector3D eye;
    QVector3D direction;
    if (!globeScreenRay(previous_screen_position, viewport, &eye, &direction))
        return;
    QVector3D previous_point;
    if (!GeoWgs84Ellipsoid::rayIntersection(eye, direction, &previous_point))
        return;

    if (!globeScreenRay(new_screen_position, viewport, &eye, &direction))
        return;
    QVector3D new_point;
    if (!GeoWgs84Ellipsoid::rayIntersection(eye, direction, &new_point))
        return;

    const QQuaternion rotation = QQuaternion::rotationTo(
        new_point.normalized(), previous_point.normalized());
    applyGlobePanRotation(rotation);
}

bool MapModel::panGlobeByTerrainPointerDrag(
    const QPoint &previous_screen_position, const QPoint &new_screen_position,
    const QSize &viewport,
    const GeoWgs84Ellipsoid::EcefPositionD &previous_terrain_ecef,
    const GeoWgs84Ellipsoid::EcefPositionD &new_terrain_ecef,
    double terrain_weight)
{
    if (previous_screen_position == new_screen_position
        || !viewport.isValid())
    {
        return false;
    }

    QVector3D eye;
    QVector3D direction;
    if (!globeScreenRay(previous_screen_position, viewport, &eye, &direction))
        return false;
    QVector3D previous_ellipsoid_point;
    if (!GeoWgs84Ellipsoid::rayIntersection(
            eye, direction, &previous_ellipsoid_point))
    {
        return false;
    }

    if (!globeScreenRay(new_screen_position, viewport, &eye, &direction))
        return false;
    QVector3D new_ellipsoid_point;
    if (!GeoWgs84Ellipsoid::rayIntersection(
            eye, direction, &new_ellipsoid_point))
    {
        return false;
    }

    const QQuaternion ellipsoid_rotation = QQuaternion::rotationTo(
        new_ellipsoid_point.normalized(), previous_ellipsoid_point.normalized());

    const QVector3D previous_terrain_direction =
        normalizedEcefDirection(previous_terrain_ecef);
    const QVector3D new_terrain_direction =
        normalizedEcefDirection(new_terrain_ecef);
    if (previous_terrain_direction.lengthSquared() <= 1e-12f
        || new_terrain_direction.lengthSquared() <= 1e-12f)
    {
        return false;
    }

    QQuaternion terrain_rotation = QQuaternion::rotationTo(
        new_terrain_direction, previous_terrain_direction);

    // Relief viewed almost tangentially can make two adjacent screen rays hit
    // terrain points that are very far apart. Keep the existing ellipsoid pan
    // as the hard speed reference: even with terrain_weight == 1, the DEM
    // rotation may be at most slightly larger than the baseline requested by
    // the same pixel delta. This prevents cliffs/ridges from creating a sudden
    // high-speed pan while still allowing nearby relief to influence the drag.
    const double ellipsoid_angle_rad =
        quaternionRotationAngleRadians(ellipsoid_rotation);
    const double maximum_terrain_angle_rad = qMax(
        ellipsoid_angle_rad * 1.35,
        qDegreesToRadians(0.002));
    terrain_rotation = limitedRotation(
        terrain_rotation, maximum_terrain_angle_rad);

    const double blend = qBound(0.0, terrain_weight, 1.0);
    const QQuaternion blended_rotation = QQuaternion::slerp(
        ellipsoid_rotation.normalized(), terrain_rotation.normalized(),
        float(blend));
    applyGlobePanRotation(blended_rotation);
    return true;
}

void MapModel::applyGlobePanRotation(const QQuaternion &rotation)
{
    if (rotation.isNull())
        return;

    const GeoWgs84Ellipsoid::OrbitCameraBasis old_camera_basis =
        GeoWgs84Ellipsoid::orbitCameraBasis(
            this->m_centerLon, this->m_centerLat,
            this->m_view_globe_yaw_deg,
            qBound(MinViewGlobePitchDeg, this->m_view_globe_pitch_deg, MaxViewGlobePitchDeg),
            qMax(MinViewGlobeDistanceM, this->m_view_globe_distance_m),
            this->m_view_globe_vertical_offset_m,
            this->m_view_globe_camera_collision_lift_m);
    const QVector3D target_ecef = old_camera_basis.target;

    // Rotating the camera's target by the rotation that maps the new surface
    // point back onto the previous one is equivalent to rotating the globe
    // itself so the grabbed ground point follows the requested pixel motion.
    const QVector3D rotated_target = rotation.rotatedVector(target_ecef);

    double lon_deg = 0.0;
    double lat_deg = 0.0;
    if (!GeoWgs84Ellipsoid::ecefToGeodetic(rotated_target, &lon_deg, &lat_deg))
        return;

    const double next_lon_deg = GeoWebMercator::normalizeLongitude(lon_deg);
    const double next_lat_deg = std::clamp(lat_deg, -90.0, 90.0);

    QVector3D transported_right = rotation.rotatedVector(old_camera_basis.right);
    const GeoWgs84Ellipsoid::LocalFrame next_frame =
        GeoWgs84Ellipsoid::localFrameAtGeodetic(next_lon_deg, next_lat_deg, 0.0);
    transported_right -= next_frame.up
        * QVector3D::dotProduct(transported_right, next_frame.up);
    if (transported_right.lengthSquared() <= 1e-12f)
        return;
    transported_right.normalize();

    double next_yaw_deg = this->m_view_globe_yaw_deg;
    if (this->m_view_globe_north_up_locked)
    {
        // North-up lock must suppress the small parallel-transport heading
        // drift produced by ordinary movement over a curved surface, but it
        // must NOT force yaw back to zero at a pole. Latitude/longitude has an
        // unavoidable tangent-frame discontinuity there: after crossing a
        // pole the canonical longitude can jump by 180 degrees, which flips
        // east/north even though the physical camera orientation is smooth.
        //
        // Keep the locked yaw unchanged during normal panning. At each step,
        // compare that candidate screen-right direction with the physically
        // transported screen-right vector. If the canonical tangent frame has
        // flipped, the two directions become opposite; adding exactly 180
        // degrees cancels the coordinate-frame flip without reintroducing the
        // ordinary curved-surface wobble that the lock is meant to suppress.
        const double yaw_rad = qDegreesToRadians(next_yaw_deg);
        const QVector3D locked_right =
            next_frame.east * float(std::cos(yaw_rad))
            + next_frame.north * float(std::sin(yaw_rad));
        if (QVector3D::dotProduct(locked_right, transported_right) < 0.0f)
            next_yaw_deg = normalizedYawDegrees(next_yaw_deg + 180.0);
    }
    else
    {
        // For a freely rotated globe, transport the actual ECEF screen-right
        // direction through the same drag rotation, then re-express that
        // physical direction as yaw in the new local tangent frame. Thus a
        // longitude jump is cancelled by the corresponding yaw change and the
        // camera orientation stays continuous through and across either pole.
        const double right_east =
            double(QVector3D::dotProduct(transported_right, next_frame.east));
        const double right_north =
            double(QVector3D::dotProduct(transported_right, next_frame.north));
        next_yaw_deg = normalizedYawDegrees(
            qRadiansToDegrees(std::atan2(right_north, right_east)));
    }

    // Update center and yaw as one state change before emitting either signal.
    // Emitting centerChanged first and fixing yaw afterwards would still expose
    // one transient frame in the arbitrary polar tangent basis.
    const bool center_changed =
        !coordinatesEqual(next_lon_deg, this->m_centerLon)
        || !coordinatesEqual(next_lat_deg, this->m_centerLat);
    const bool yaw_changed =
        !coordinatesEqual(next_yaw_deg, this->m_view_globe_yaw_deg);
    if (!center_changed && !yaw_changed)
        return;

    this->m_centerLon = next_lon_deg;
    this->m_centerLat = next_lat_deg;
    this->m_view_globe_yaw_deg = next_yaw_deg;

    if (center_changed)
        emitCenterChanged();
    if (yaw_changed)
        emit viewGlobeCameraChanged();
}

void MapModel::panByPixelsGlobe(const QPoint &delta, const QSize &viewport)
{
    if (delta.isNull() || !viewport.isValid())
        return;

    // Same "grab the point at screen center and drag it by delta pixels"
    // shape as a direct screen-space drag, but via the ellipsoid ray-cast
    // instead of the flat-plane ground offset. The screen center is always
    // a safe ray to cast -- the camera's forward vector is, by
    // construction, aimed exactly at the current target, which is always a
    // point on the ellipsoid.
    const QPoint viewport_center(viewport.width() / 2, viewport.height() / 2);
    panGlobeByPointerDrag(viewport_center - delta, viewport_center, viewport);
}

void MapModel::panByPixelsGlobeKeyboard(
    const QPoint &delta, const QSize &viewport)
{
    if (delta.isNull() || !viewport.isValid())
        return;

    // Keyboard movement must not inherit the grazing-angle amplification of
    // a ground/terrain ray cast. Derive a pitch-independent physical scale
    // from the orbit distance and vertical FOV instead, then rotate the
    // requested screen movement by yaw into the local east/north tangent
    // plane. Keyboard Globe panning deliberately stays pitch-independent.
    const double safe_height = qMax(1, viewport.height());
    const double half_fov_rad = qDegreesToRadians(GlobeFieldOfViewDeg * 0.5);
    const double distance_m = qMax(
        MinViewGlobeDistanceM, this->m_view_globe_distance_m);
    const double meters_per_pixel =
        2.0 * distance_m * std::tan(half_fov_rad) / safe_height;
    if (!std::isfinite(meters_per_pixel) || meters_per_pixel <= 0.0)
        return;

    const double yaw_rad = qDegreesToRadians(
        normalizedYawDegrees(this->m_view_globe_yaw_deg));
    const double screen_right_east = std::cos(yaw_rad);
    const double screen_right_north = std::sin(yaw_rad);
    const double screen_up_east = -std::sin(yaw_rad);
    const double screen_up_north = std::cos(yaw_rad);

    const double east_m = meters_per_pixel * (
        -screen_right_east * double(delta.x())
        + screen_up_east * double(delta.y()));
    const double north_m = meters_per_pixel * (
        -screen_right_north * double(delta.x())
        + screen_up_north * double(delta.y()));

    double next_lon_deg = 0.0;
    double next_lat_deg = 0.0;
    if (!GeoWgs84Ellipsoid::offsetGeodetic(
            this->m_centerLon, this->m_centerLat,
            east_m, north_m, &next_lon_deg, &next_lat_deg))
    {
        return;
    }

    setCenter(next_lon_deg, next_lat_deg, viewport);
}

void MapModel::clampCenter(const QSize &viewport)
{
    if (!viewport.isValid())
        return;

    // This clamp exists to keep the flat 2D map's on-screen extent within
    // Web Mercator's own representable range (it round-trips centerLat
    // through latToTileY()/tileYToLat(), both of which clamp internally to
    // +-GeoWebMercator::MaximumLatitude, i.e. ~85.05 degrees). The Globe
    // camera has no such limit -- it looks directly at the WGS84 ellipsoid
    // via ECEF math, which is perfectly well-defined all the way to the
    // true poles (90 degrees) -- so applying this here would silently stop
    // any attempt to pan the globe to a real polar latitude a few degrees
    // short of the actual pole, for no reason that applies to Globe at all.
    if (this->m_view_mode == MapViewMode::Globe)
        return;

    const double world_tile_count = double(tileCount());
    const double scale = qMax(1e-9, this->m_view_2d_continuous_scale);
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

void MapModel::setViewMode(MapViewMode view_mode, const QSize &viewport)
{
    if (this->m_view_mode == view_mode)
        return;

    const MapViewMode previous_view_mode = this->m_view_mode;
    const double old_latitude = this->m_centerLat;
    const int viewport_height_px = viewport.isValid()
        ? qMax(1, viewport.height())
        : GlobeZoomReferenceViewportHeightPx;

    bool globe_distance_changed = false;
    double transferred_2d_zoom = std::numeric_limits<double>::quiet_NaN();

    // 2D and Globe use different native camera representations, but a view
    // mode switch must not look like an arbitrary zoom jump. Transfer the
    // current screen scale before changing mode so the first Globe frame is
    // already at the same 2D-equivalent zoom level.
    if (previous_view_mode == MapViewMode::TwoD && view_mode == MapViewMode::Globe)
    {
        const double next_distance_m = qBound(
            MinViewGlobeDistanceM,
            viewGlobeDistanceMForZoomLevel(
                view2dContinuousZoom(), this->m_centerLat, viewport_height_px),
            MaxViewGlobeDistanceM);
        globe_distance_changed = !coordinatesEqual(
            next_distance_m, this->m_view_globe_distance_m);
        this->m_view_globe_distance_m = next_distance_m;
    }
    else if (previous_view_mode == MapViewMode::Globe
             && view_mode == MapViewMode::TwoD)
    {
        transferred_2d_zoom = viewGlobeZoomLevelForDistanceM(
            this->m_view_globe_distance_m, this->m_centerLat, viewport_height_px);
    }

    this->m_view_mode = view_mode;

    // A Globe center may legitimately sit anywhere up to the true poles.
    // When returning to a flat Web Mercator view, bring that shared center
    // back into the projection's representable latitude range immediately
    // instead of leaving a +-90 degree value in flat-map state.
    if (this->m_view_mode != MapViewMode::Globe)
    {
        this->m_centerLat = std::clamp(
            this->m_centerLat,
            -GeoWebMercator::MaximumLatitude,
            GeoWebMercator::MaximumLatitude);
    }

    if (this->m_view_mode != MapViewMode::Globe
        && this->m_view_3d_navigation_state == MapView3dNavigationState::Rotate)
    {
        this->m_view_3d_rotate_interaction_depth = 0;
        this->m_view_3d_navigation_state = MapView3dNavigationState::Pan;
        emit view3dNavigationStateChanged(this->m_view_3d_navigation_state);
    }

    emit viewModeChanged(this->m_view_mode);

    if (std::isfinite(transferred_2d_zoom))
        setView2dContinuousZoom(transferred_2d_zoom, viewport);

    if (globe_distance_changed)
        emit viewGlobeCameraChanged();

    if (!coordinatesEqual(this->m_centerLat, old_latitude))
        emitCenterChanged();
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
    emit view3dVerticalExaggerationChanged(bounded_exaggeration);
}

void MapModel::beginView3dRotateInteraction()
{
    // This generic 3D interaction state belongs exclusively to Globe now.
    if (this->m_view_mode != MapViewMode::Globe)
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

void MapModel::setViewGlobeYawDeg(double yaw_deg)
{
    const double next_yaw = normalizedYawDegrees(yaw_deg);

    // Setting an exact north-up heading is also the explicit north-up lock
    // used by the globe compass. Any other absolute heading releases it.
    // Update the lock even when the numeric yaw is already unchanged so a
    // compass click at 0 degrees can re-enable north-up panning.
    this->m_view_globe_north_up_locked = coordinatesEqual(next_yaw, 0.0);

    if (coordinatesEqual(next_yaw, this->m_view_globe_yaw_deg))
    {
        // Keep the stored north heading exact as well; the comparison epsilon
        // should not leave a tiny residual yaw behind after a compass reset.
        this->m_view_globe_yaw_deg = next_yaw;
        return;
    }

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

void MapModel::setViewGlobeCameraCollisionLiftM(double lift_m)
{
    setViewGlobeTerrainHeightOffsetsM(
        this->m_view_globe_vertical_offset_m, lift_m);
}

void MapModel::setViewGlobeTerrainHeightOffsetsM(
    double vertical_offset_m, double camera_collision_lift_m)
{
    if (!std::isfinite(vertical_offset_m)
        || !std::isfinite(camera_collision_lift_m))
    {
        return;
    }

    const double next_collision_lift_m = qMax(
        0.0, camera_collision_lift_m);
    const bool vertical_offset_changed = !coordinatesEqual(
        vertical_offset_m, this->m_view_globe_vertical_offset_m);
    const bool collision_lift_changed = !coordinatesEqual(
        next_collision_lift_m,
        this->m_view_globe_camera_collision_lift_m);
    if (!vertical_offset_changed && !collision_lift_changed)
        return;

    this->m_view_globe_vertical_offset_m = vertical_offset_m;
    this->m_view_globe_camera_collision_lift_m =
        next_collision_lift_m;
    emit viewGlobeTerrainHeightChanged();
}

void MapModel::setViewGlobeZoomLevel(double zoom_level, const QSize &viewport)
{
    if (!std::isfinite(zoom_level))
        return;

    const double distance_m = viewGlobeDistanceMForZoomLevel(
        zoom_level, this->m_centerLat,
        viewport.isValid() ? viewport.height() : GlobeZoomReferenceViewportHeightPx);
    setViewGlobeDistanceM(distance_m);
}

void MapModel::setViewGlobeFocusAnchor(
    double lon, double lat, double vertical_offset_m,
    double yaw_deg, double pitch_deg, double distance_m,
    double camera_collision_lift_m, const QSize &viewport)
{
    if (!std::isfinite(lon) || !std::isfinite(lat)
        || !std::isfinite(vertical_offset_m) || !std::isfinite(yaw_deg)
        || !std::isfinite(pitch_deg) || !std::isfinite(distance_m)
        || !std::isfinite(camera_collision_lift_m))
    {
        return;
    }

    const double old_lon = this->m_centerLon;
    const double old_lat = this->m_centerLat;
    const double old_vertical_offset_m = this->m_view_globe_vertical_offset_m;
    const double old_yaw_deg = this->m_view_globe_yaw_deg;
    const double old_pitch_deg = this->m_view_globe_pitch_deg;
    const double old_distance_m = this->m_view_globe_distance_m;
    const double old_collision_lift_m =
        this->m_view_globe_camera_collision_lift_m;

    this->m_centerLon = GeoWebMercator::normalizeLongitude(lon);
    this->m_centerLat = std::clamp(lat, -90.0, 90.0);
    if (viewport.isValid())
        clampCenter(viewport);

    this->m_view_globe_vertical_offset_m = vertical_offset_m;
    this->m_view_globe_yaw_deg = normalizedYawDegrees(yaw_deg);
    this->m_view_globe_pitch_deg = qBound(
        MinViewGlobePitchDeg, pitch_deg, MaxViewGlobePitchDeg);
    this->m_view_globe_distance_m = qBound(
        MinViewGlobeDistanceM, distance_m, MaxViewGlobeDistanceM);
    this->m_view_globe_camera_collision_lift_m = qMax(
        0.0, camera_collision_lift_m);

    const bool center_changed = !coordinatesEqual(this->m_centerLon, old_lon)
        || !coordinatesEqual(this->m_centerLat, old_lat);
    const bool camera_changed = !coordinatesEqual(
        this->m_view_globe_vertical_offset_m, old_vertical_offset_m)
        || !coordinatesEqual(this->m_view_globe_yaw_deg, old_yaw_deg)
        || !coordinatesEqual(this->m_view_globe_pitch_deg, old_pitch_deg)
        || !coordinatesEqual(this->m_view_globe_distance_m, old_distance_m)
        || !coordinatesEqual(
            this->m_view_globe_camera_collision_lift_m,
            old_collision_lift_m);

    if (center_changed)
        emitCenterChanged();
    if (camera_changed)
        emit viewGlobeCameraChanged();
}

void MapModel::orbitViewGlobe(double yaw_delta_deg, double pitch_delta_deg)
{
    const double next_yaw = normalizedYawDegrees(this->m_view_globe_yaw_deg + yaw_delta_deg);
    const double next_pitch = qBound(
        MinViewGlobePitchDeg,
        this->m_view_globe_pitch_deg + pitch_delta_deg,
        MaxViewGlobePitchDeg);

    // Any deliberate yaw rotation leaves north-up mode. Pitch-only orbiting
    // does not, so the user can tilt the globe while keeping north locked.
    if (!coordinatesEqual(yaw_delta_deg, 0.0))
        this->m_view_globe_north_up_locked = false;

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
    const double sensitivity = guiConfiguration().map_navigation.orbit_3d_sensitivity;
    const double yaw_delta_deg =
        double(delta_pixels.x()) * ViewGlobeOrbitYawDegreesPerPixel * sensitivity;
    const double pitch_delta_deg = include_pitch
        ? double(-delta_pixels.y()) * ViewGlobeOrbitPitchDegreesPerPixel * sensitivity
        : 0.0;
    orbitViewGlobe(yaw_delta_deg, pitch_delta_deg);
}

CoordinateWGS84 MapModel::wgs84FromScreen(const QPoint &pos, const QSize &viewport) const
{
    const QPointF center = centerTile();

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
    const double view_scale = qMax(1e-9, this->m_view_2d_continuous_scale);
    return QPointF(
        double(viewport.width()) / 2.0 + offset_pixels.x() * view_scale,
        double(viewport.height()) / 2.0 + offset_pixels.y() * view_scale);
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
