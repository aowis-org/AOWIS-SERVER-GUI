#include "map_rhi_camera.h"

#include "map_model.h"
#include "map_render_cache_math.h"
#include "../geo_web_mercator.h"

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
    this->view_mode = map_model.viewMode();
    this->view_3d_yaw_deg = map_model.view3dYawDeg();
    this->view_3d_pitch_deg = map_model.view3dPitchDeg();
    this->view_3d_camera_distance_world = map_model.view3dCameraDistanceWorld();
    this->view_3d_camera_collision_lift_world =
        map_model.view3dCameraCollisionLiftWorld();
    this->view_3d_vertical_offset_world = map_model.view3dVerticalOffsetWorld();

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
    const double scale = GeoWebMercator::zoomScale(
        this->zoom, MapRenderCacheMath::ReferenceZoom);
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
QPointF MapRhiCamera::projectWorldToScreen(const QVector3D &world_position) const
{
    const int viewport_width = qMax(1, this->viewport_size.width());
    const int viewport_height = qMax(1, this->viewport_size.height());
    const double scale = GeoWebMercator::zoomScale(
        this->zoom, MapRenderCacheMath::ReferenceZoom);
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
