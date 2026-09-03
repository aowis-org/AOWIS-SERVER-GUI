#include "map/rhi/map_rhi_camera.h"

#include "map/core/map_model.h"
#include "map/render/map_render_cache_math.h"
#include "geo/geo_web_mercator.h"
#include "geo/geo_wgs84_ellipsoid.h"

#include <rhi/qrhi.h>

#include <QVector3D>
#include <QtMath>

#include <cmath>

MapRhiCamera::MapRhiCamera() = default;

void MapRhiCamera::setSceneOriginWorld(const QPointF &origin_world)
{
    this->scene_origin_world = origin_world;
}

void MapRhiCamera::setViewportSize(const QSize &viewport_size)
{
    this->viewport_size = viewport_size;
}

void MapRhiCamera::syncFromMapModel(const MapModel &map_model)
{
    this->zoom = map_model.zoom();
    this->view_2d_continuous_scale = map_model.view2dContinuousScale();
    this->view_mode = map_model.viewMode();
    this->view_3d_yaw_deg = map_model.view3dYawDeg();
    this->view_3d_pitch_deg = map_model.view3dPitchDeg();
    this->view_3d_camera_distance_world = map_model.view3dCameraDistanceWorld();
    this->view_3d_camera_collision_lift_world =
        map_model.view3dCameraCollisionLiftWorld();
    this->view_3d_vertical_offset_world = map_model.view3dVerticalOffsetWorld();

    this->globe_target_lon_deg = map_model.centerLon();
    this->globe_target_lat_deg = map_model.centerLat();
    this->view_globe_yaw_deg = map_model.viewGlobeYawDeg();
    this->view_globe_pitch_deg = map_model.viewGlobePitchDeg();
    this->view_globe_distance_m = map_model.viewGlobeDistanceM();

    const QPointF raw_center_world = GeoWebMercator::lonLatToWorldPixel(
        GeoWebMercator::normalizeLongitude(map_model.centerLon()),
        map_model.centerLat(),
        MapRenderCacheMath::ReferenceZoom);
    const double center_x = GeoWebMercator::nearestWrappedWorldPixelX(
        raw_center_world.x(),
        this->scene_origin_world.x(),
        MapRenderCacheMath::ReferenceZoom);

    this->center_world = QPointF(
        center_x - this->scene_origin_world.x(),
        raw_center_world.y() - this->scene_origin_world.y());
}

QMatrix4x4 MapRhiCamera::viewProjectionMatrix(const QRhi &rhi) const
{
    const int viewport_width = qMax(1, this->viewport_size.width());
    const int viewport_height = qMax(1, this->viewport_size.height());
    const double base_scale = GeoWebMercator::zoomScale(
        this->zoom, MapRenderCacheMath::ReferenceZoom);
    const double scale = this->view_mode == MapViewMode::TwoD
        ? base_scale * qMax(1e-9, this->view_2d_continuous_scale)
        : base_scale;
    const double safe_scale = scale > 0.0 ? scale : 1.0;
    const double half_width_world = double(viewport_width) / (2.0 * safe_scale);
    const double half_height_world = double(viewport_height) / (2.0 * safe_scale);

    if (this->view_mode == MapViewMode::ThreeD)
    {
        constexpr float FieldOfViewDeg = 45.0f;
        const double pitch_rad = qDegreesToRadians(qBound(
            MapModel::MinView3dPitchDeg,
            this->view_3d_pitch_deg,
            MapModel::MaxView3dPitchDeg));
        const double yaw_rad = qDegreesToRadians(this->view_3d_yaw_deg);
        const double native_distance = half_height_world
            / std::tan(qDegreesToRadians(double(FieldOfViewDeg) / 2.0));
        const double distance = this->view_3d_camera_distance_world > 0.0
            ? this->view_3d_camera_distance_world : native_distance;
        const double horizontal_distance = distance * std::cos(pitch_rad);
        const QVector3D target(
            float(this->center_world.x()), float(this->center_world.y()),
            float(this->view_3d_vertical_offset_world));
        const QVector3D eye(
            target.x() + float(std::sin(yaw_rad) * horizontal_distance),
            target.y() + float(std::cos(yaw_rad) * horizontal_distance),
            float(this->view_3d_vertical_offset_world
                + distance * std::sin(pitch_rad)
                + this->view_3d_camera_collision_lift_world));

        QMatrix4x4 projection;
        projection.perspective(
            FieldOfViewDeg, float(viewport_width) / float(viewport_height),
            float(qMax(0.001, qMin(0.25, distance * 0.001))),
            float(qMax(10.0, distance * 20.0)));

        // Web Mercator uses +X east and +Y south. Together with +Z up this
        // gives the map plane the opposite handedness from QMatrix4x4::lookAt().
        // lookAt() therefore places geographic east on the left even though the
        // logical 3D camera/picking basis correctly uses +X as screen-right.
        // Flip only the perspective X projection so rendering matches that basis;
        // do not invert yaw, panning, picking, or map coordinates to compensate.
        projection(0, 0) = -projection(0, 0);

        const QVector3D forward = (target - eye).normalized();
        // Web Mercator X grows eastward. At yaw 0, screen-right must therefore
        // point east (+X), otherwise the entire perspective map is mirrored.
        const QVector3D right(
            float(std::cos(yaw_rad)), float(-std::sin(yaw_rad)), 0.0f);
        const QVector3D camera_up = QVector3D::crossProduct(forward, right).normalized();
        QMatrix4x4 view;
        view.lookAt(eye, target, camera_up);

        QMatrix4x4 result = rhi.clipSpaceCorrMatrix();
        result *= projection;
        result *= view;
        return result;
    }

    const float left = float(this->center_world.x() - half_width_world);
    const float right = float(this->center_world.x() + half_width_world);
    const float top = float(this->center_world.y() - half_height_world);
    const float bottom = float(this->center_world.y() + half_height_world);

    QMatrix4x4 result = rhi.clipSpaceCorrMatrix();
    result.ortho(left, right, bottom, top, -1000000.0f, 1000000.0f);
    return result;
}
QMatrix4x4 MapRhiCamera::globeViewProjectionMatrix(const QRhi &rhi) const
{
    const int viewport_width = qMax(1, this->viewport_size.width());
    const int viewport_height = qMax(1, this->viewport_size.height());

    const GeoWgs84Ellipsoid::LocalFrame target_frame = GeoWgs84Ellipsoid::localFrameAtGeodetic(
        this->globe_target_lon_deg, this->globe_target_lat_deg, 0.0);

    const double pitch_rad = qDegreesToRadians(qBound(
        MapModel::MinViewGlobePitchDeg, this->view_globe_pitch_deg,
        MapModel::MaxViewGlobePitchDeg));
    const double yaw_rad = qDegreesToRadians(this->view_globe_yaw_deg);
    const double distance = qMax(MapModel::MinViewGlobeDistanceM, this->view_globe_distance_m);
    const double horizontal_distance = distance * std::cos(pitch_rad);

    // Both "horizontal_direction" (where the camera sits around the target,
    // by yaw) and "right" (the yaw-only tangent-plane direction 90 degrees
    // from it) are unit vectors, since east/north are themselves orthonormal
    // and each is a sin/cos combination of the two.
    const QVector3D horizontal_direction =
        target_frame.east * float(std::sin(yaw_rad))
        + target_frame.north * float(std::cos(yaw_rad));
    const QVector3D right =
        target_frame.east * float(std::cos(yaw_rad))
        - target_frame.north * float(std::sin(yaw_rad));

    const QVector3D eye = target_frame.position
        + target_frame.up * float(distance * std::sin(pitch_rad))
        + horizontal_direction * float(horizontal_distance);
    const QVector3D forward = (target_frame.position - eye).normalized();

    // "right" is constructed purely from yaw, in the target's tangent
    // plane, so it is always perpendicular to "forward" (which only ever
    // combines "up" and "horizontal_direction", both of which "right" is
    // perpendicular to by construction) -- including at pitch 90 degrees,
    // where a naive up-vector-based lookAt() would degenerate. Passing
    // cross(right, forward) as the up hint (rather than cross(forward,
    // right)) is deliberate: lookAt() derives its screen-right axis as
    // cross(forward, up_hint), and cross(forward, cross(right, forward))
    // reduces to +right (by the vector triple product identity, since
    // right and forward are perpendicular and forward is unit length),
    // which keeps geographic east on screen-right. The other cross order
    // would silently mirror the globe left/right, the same failure mode
    // the ThreeD camera above works around with a projection-matrix flip.
    const QVector3D camera_up = QVector3D::crossProduct(right, forward).normalized();

    constexpr float FieldOfViewDeg = 45.0f;
    const double near_plane = qMax(1000.0, distance * 0.001);
    const double far_plane = (distance + GeoWgs84Ellipsoid::EquatorialRadiusM) * 3.0;
    QMatrix4x4 projection;
    projection.perspective(
        FieldOfViewDeg, float(viewport_width) / float(viewport_height),
        float(near_plane), float(far_plane));

    QMatrix4x4 view;
    view.lookAt(eye, target_frame.position, camera_up);

    QMatrix4x4 result = rhi.clipSpaceCorrMatrix();
    result *= projection;
    result *= view;
    return result;
}

QPointF MapRhiCamera::projectWorldToScreen(const QVector3D &world_position) const
{
    const int viewport_width = qMax(1, this->viewport_size.width());
    const int viewport_height = qMax(1, this->viewport_size.height());
    const double base_scale = GeoWebMercator::zoomScale(
        this->zoom, MapRenderCacheMath::ReferenceZoom);
    const double scale = this->view_mode == MapViewMode::TwoD
        ? base_scale * qMax(1e-9, this->view_2d_continuous_scale)
        : base_scale;
    const double safe_scale = scale > 0.0 ? scale : 1.0;

    if (this->view_mode != MapViewMode::ThreeD)
    {
        return QPointF(
            viewport_width / 2.0
                + (double(world_position.x()) - this->center_world.x()) * safe_scale,
            viewport_height / 2.0
                + (double(world_position.y()) - this->center_world.y()) * safe_scale);
    }

    constexpr double FieldOfViewDeg = 45.0;
    const double half_height_world = double(viewport_height) / (2.0 * safe_scale);
    const double pitch_rad = qDegreesToRadians(qBound(
        MapModel::MinView3dPitchDeg,
        this->view_3d_pitch_deg,
        MapModel::MaxView3dPitchDeg));
    const double yaw_rad = qDegreesToRadians(this->view_3d_yaw_deg);
    const double native_distance = half_height_world
        / std::tan(qDegreesToRadians(FieldOfViewDeg / 2.0));
    const double distance = this->view_3d_camera_distance_world > 0.0
        ? this->view_3d_camera_distance_world : native_distance;
    const double horizontal_distance = distance * std::cos(pitch_rad);
    const QVector3D target(
        float(this->center_world.x()), float(this->center_world.y()),
        float(this->view_3d_vertical_offset_world));
    const QVector3D eye(
        target.x() + float(std::sin(yaw_rad) * horizontal_distance),
        target.y() + float(std::cos(yaw_rad) * horizontal_distance),
        float(this->view_3d_vertical_offset_world
            + distance * std::sin(pitch_rad)
            + this->view_3d_camera_collision_lift_world));
    const QVector3D forward = (target - eye).normalized();
    // Keep CPU projection in the same east-right basis as the GPU view matrix.
    const QVector3D right(
        float(std::cos(yaw_rad)), float(-std::sin(yaw_rad)), 0.0f);
    const QVector3D up = QVector3D::crossProduct(forward, right).normalized();
    const QVector3D relative = world_position - eye;
    const double depth = QVector3D::dotProduct(relative, forward);
    if (depth <= 1e-6)
        return QPointF(qQNaN(), qQNaN());

    const double aspect = double(viewport_width) / double(viewport_height);
    const double tan_half_fov = std::tan(qDegreesToRadians(FieldOfViewDeg / 2.0));
    const double camera_x = QVector3D::dotProduct(relative, right);
    const double camera_y = QVector3D::dotProduct(relative, up);
    const double ndc_x = camera_x / (depth * tan_half_fov * aspect);
    const double ndc_y = camera_y / (depth * tan_half_fov);
    return QPointF(
        (ndc_x + 1.0) * viewport_width / 2.0,
        (1.0 - ndc_y) * viewport_height / 2.0);
}



double MapRhiCamera::nativeOrbitDistanceWorld() const
{
    const int viewport_height = qMax(1, this->viewport_size.height());
    const double scale = GeoWebMercator::zoomScale(
        this->zoom, MapRenderCacheMath::ReferenceZoom);
    const double safe_scale = scale > 0.0 ? scale : 1.0;
    const double half_height_world = double(viewport_height) / (2.0 * safe_scale);
    return half_height_world / std::tan(qDegreesToRadians(45.0 / 2.0));
}

double MapRhiCamera::orbitDistanceWorld() const
{
    return this->view_3d_camera_distance_world > 0.0
        ? this->view_3d_camera_distance_world
        : nativeOrbitDistanceWorld();
}

double MapRhiCamera::perspectiveDepthWorld(const QVector3D &world_position) const
{
    if (this->view_mode != MapViewMode::ThreeD)
        return qQNaN();

    const double pitch_rad = qDegreesToRadians(qBound(
        MapModel::MinView3dPitchDeg,
        this->view_3d_pitch_deg,
        MapModel::MaxView3dPitchDeg));
    const double yaw_rad = qDegreesToRadians(this->view_3d_yaw_deg);
    const double distance = orbitDistanceWorld();
    const double horizontal_distance = distance * std::cos(pitch_rad);
    const QVector3D target(
        float(this->center_world.x()),
        float(this->center_world.y()),
        float(this->view_3d_vertical_offset_world));
    const QVector3D eye(
        target.x() + float(std::sin(yaw_rad) * horizontal_distance),
        target.y() + float(std::cos(yaw_rad) * horizontal_distance),
        float(this->view_3d_vertical_offset_world
            + distance * std::sin(pitch_rad)
            + this->view_3d_camera_collision_lift_world));
    const QVector3D forward = (target - eye).normalized();
    if (forward.lengthSquared() <= 1e-12f)
        return qQNaN();

    return QVector3D::dotProduct(world_position - eye, forward);
}

bool MapRhiCamera::screenRay(
    const QPointF &screen_position, QVector3D *eye_world, QVector3D *direction_world) const
{
    if (eye_world == nullptr || direction_world == nullptr
        || this->view_mode != MapViewMode::ThreeD
        || !std::isfinite(screen_position.x()) || !std::isfinite(screen_position.y()))
    {
        return false;
    }

    const int viewport_width = qMax(1, this->viewport_size.width());
    const int viewport_height = qMax(1, this->viewport_size.height());
    const double scale = GeoWebMercator::zoomScale(
        this->zoom, MapRenderCacheMath::ReferenceZoom);
    const double safe_scale = scale > 0.0 ? scale : 1.0;
    const double half_height_world = double(viewport_height) / (2.0 * safe_scale);
    const double pitch_rad = qDegreesToRadians(qBound(
        MapModel::MinView3dPitchDeg,
        this->view_3d_pitch_deg,
        MapModel::MaxView3dPitchDeg));
    const double yaw_rad = qDegreesToRadians(this->view_3d_yaw_deg);
    const double native_distance = half_height_world
        / std::tan(qDegreesToRadians(45.0 / 2.0));
    const double distance = this->view_3d_camera_distance_world > 0.0
        ? this->view_3d_camera_distance_world : native_distance;
    const double horizontal_distance = distance * std::cos(pitch_rad);

    const QVector3D target(
        float(this->center_world.x()),
        float(this->center_world.y()),
        float(this->view_3d_vertical_offset_world));
    const QVector3D eye(
        target.x() + float(std::sin(yaw_rad) * horizontal_distance),
        target.y() + float(std::cos(yaw_rad) * horizontal_distance),
        float(this->view_3d_vertical_offset_world
            + distance * std::sin(pitch_rad)
            + this->view_3d_camera_collision_lift_world));
    const QVector3D forward = (target - eye).normalized();
    if (forward.lengthSquared() <= 1e-12f)
        return false;

    const QVector3D right(
        float(std::cos(yaw_rad)), float(-std::sin(yaw_rad)), 0.0f);
    const QVector3D up = QVector3D::crossProduct(forward, right).normalized();
    if (up.lengthSquared() <= 1e-12f)
        return false;

    constexpr double FieldOfViewDeg = 45.0;
    const double aspect = double(viewport_width) / double(viewport_height);
    const double tan_half_fov = std::tan(qDegreesToRadians(FieldOfViewDeg / 2.0));
    const double ndc_x = 2.0 * screen_position.x() / double(viewport_width) - 1.0;
    const double ndc_y = 1.0 - 2.0 * screen_position.y() / double(viewport_height);

    QVector3D direction = forward
        + right * float(ndc_x * tan_half_fov * aspect)
        + up * float(ndc_y * tan_half_fov);
    if (direction.lengthSquared() <= 1e-12f)
        return false;

    direction.normalize();
    *eye_world = eye;
    *direction_world = direction;
    return true;
}

bool MapRhiCamera::crosshairRay(QVector3D *eye_world, QVector3D *direction_world) const
{
    return screenRay(
        QPointF(this->viewport_size.width() / 2.0, this->viewport_size.height() / 2.0),
        eye_world, direction_world);
}

QPointF MapRhiCamera::cameraGroundWorldPixelForDistance(double distance_world) const
{
    const double pitch_rad = qDegreesToRadians(qBound(
        MapModel::MinView3dPitchDeg,
        this->view_3d_pitch_deg,
        MapModel::MaxView3dPitchDeg));
    const double yaw_rad = qDegreesToRadians(this->view_3d_yaw_deg);
    const double horizontal_distance = qMax(0.0, distance_world) * std::cos(pitch_rad);

    return QPointF(
        this->scene_origin_world.x() + this->center_world.x()
            + std::sin(yaw_rad) * horizontal_distance,
        this->scene_origin_world.y() + this->center_world.y()
            + std::cos(yaw_rad) * horizontal_distance);
}

QPointF MapRhiCamera::cameraGroundWorldPixel() const
{
    return cameraGroundWorldPixelForDistance(orbitDistanceWorld());
}
