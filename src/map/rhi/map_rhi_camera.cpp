#include "map/rhi/map_rhi_camera.h"

#include "map/core/map_model.h"
#include "map/render/map_render_cache_math.h"
#include "geo/geo_web_mercator.h"
#include "geo/geo_wgs84_ellipsoid.h"

#include <rhi/qrhi.h>

#include <QVector3D>
#include <QtMath>

#include <cmath>

namespace
{
// A rebase rebuilds Globe terrain and every CPU-built network vertex. The
// minimum keeps close-range coordinates comfortably sub-centimetre, while
// updateGlobeRenderOrigin() expands it at distant zooms according to the
// current meters-per-pixel. A fixed 50 km threshold was needlessly strict
// when viewing whole regions: a small drag could move the target hundreds
// of kilometres and force several full KY4-sized network rebuilds during a
// single gesture even though a metre of float error would still be far
// below one screen pixel there.
constexpr double GlobeRenderOriginMinimumRebaseThresholdM = 50000.0;
// Maximum tolerated float32 coordinate quantisation attributable to origin
// drift, expressed in logical screen pixels. With float32's 23 explicit
// fraction bits, multiplying meters-per-pixel by this budget and 2^23 gives
// the largest origin-relative magnitude whose ULP remains below the budget.
// 0.05 px is deliberately conservative and visually unresolvable.
constexpr double GlobeRenderOriginPrecisionBudgetPx = 0.05;
constexpr double Float32FractionScale = 8388608.0; // 2^23

// How far past the geometric horizon (see globeNearFarPlanesM() below) the
// far clip plane should reach. Generous on purpose: it exists only to keep
// terrain relief that pokes up above the theoretical horizon line (distant
// mountains, etc.) from getting clipped, and erring generous here is cheap
// -- it costs a little depth-buffer precision at distances nothing
// network-related ever renders at anyway. What it must NOT do is scale
// with planet radius regardless of zoom, which is what the far plane this
// replaces did.
constexpr double GlobeHorizonMarginM = 50000.0;

// Computes the Globe camera's near/far clip distances. Replaces a far
// plane that was previously a flat (distance_m + EquatorialRadiusM) * 3.0
// -- correct in the sense that it never clipped anything, but at typical
// close-in Globe zoom (distance_m of a few meters to a few hundred) that
// made the far plane at least ~1.9e7 m regardless, an near:far ratio north
// of 1:100,000,000. Standard (non-logarithmic) depth buffers concentrate
// almost all of their representable precision close to the near plane and
// have essentially none left at that kind of ratio -- meaning two
// surfaces sitting a couple of meters apart (Globe network geometry lifted
// just above the terrain mesh beneath it, see
// MapRhiGlobeNetworkScene::ecefPosition()'s MinimumAntiZFightingLiftM)
// could easily fall within a single depth-buffer step of each other, so
// which one the depth test resolves as "in front" becomes essentially
// arbitrary sub-pixel/sub-ULP noise -- flickering between the two answers
// as the view matrix's own floating-point rounding shifts by even one bit
// from frame to frame. This is a materially worse problem for Globe than
// for the flat ThreeD camera, whose near/far range is naturally tight
// without any special-casing, which is why the same network/junction
// rendering code visibly flickers here and does not there.
//
// The fix is to stop wasting the depth buffer's precision on distances
// nothing will ever be drawn at: the far plane only needs to reach the
// true geometric horizon as seen from the camera's actual altitude above
// the ellipsoid (plus a margin for relief above that line), not the
// far side of the planet. eye_altitude_m is derived with a flat local
// tangent-plane approximation (target_height_m + distance_m*sin(pitch)) --
// exact only at the target's own tangent plane, not true ellipsoid
// geometry at large distance_m, but that is fine here: the quantity built
// from it only needs to be a generous upper bound on the far plane, not
// an exact value, and this keeps the calculation cheap and dependency-free
// (no need for the caller to already have eye/target as ECEF vectors).
// The horizon-distance formula itself (sqrt(2*R*h + h^2)) is exact, not a
// small-h approximation, so it stays correct from close-in zoom all the
// way out to viewing the whole planet from space.
void globeNearFarPlanesM(
    double distance_m, double pitch_deg, double target_height_m,
    double *near_plane_m, double *far_plane_m)
{
    const double pitch_rad = qDegreesToRadians(pitch_deg);
    const double eye_altitude_m = qMax(
        1.0, target_height_m + distance_m * std::sin(pitch_rad));
    const double horizon_distance_m = std::sqrt(
        eye_altitude_m * (2.0 * GeoWgs84Ellipsoid::EquatorialRadiusM + eye_altitude_m));

    // Keep the near plane proportional to the actual camera distance. A
    // fixed 1000 m floor clips the target ellipsoid completely once globe
    // zoom brings the camera below 1000 m (roughly zoom 17.4 for a typical
    // viewport), making the whole scene turn black even though the camera
    // and tile geometry are otherwise valid. The small absolute floor only
    // protects perspective() from a zero/denormal near value.
    *near_plane_m = qMax(0.1, distance_m * 0.001);
    // The qMax(horizon_distance_m, distance_m) guards a case the tangent-
    // plane altitude approximation above handles poorly: at low pitch
    // (looking near-horizontally) combined with a large orbit distance,
    // the eye's true position (offset "horizontal_distance" from the
    // target along the target's *local* tangent plane, not a great-circle
    // path -- see orbitCameraBasis()/orbitCameraBasisRelativeToOrigin())
    // ends up farther from the ellipsoid's surface than
    // target_height_m + distance_m*sin(pitch) alone would suggest, which
    // could otherwise make the far plane tighter than the camera's own
    // orbit radius -- clipping the very thing the camera is orbiting
    // around. distance_m is always a cheap, correct lower bound on how far
    // the far plane needs to reach regardless of that approximation error.
    *far_plane_m = qMax(
        *near_plane_m * 10.0, qMax(horizon_distance_m, distance_m) + GlobeHorizonMarginM);
}
}

MapRhiCamera::MapRhiCamera() = default;

MapRhiImpostorCameraBasis MapRhiCamera::junctionImpostorCameraBasis() const
{
    MapRhiImpostorCameraBasis result;

    if (this->view_mode == MapViewMode::Globe)
    {
        const double pitch_deg = qBound(
            MapModel::MinViewGlobePitchDeg, this->view_globe_pitch_deg,
            MapModel::MaxViewGlobePitchDeg);
        const double distance = globeOrbitDistanceM();
        const GeoWgs84Ellipsoid::OrbitCameraBasisRelative basis =
            GeoWgs84Ellipsoid::orbitCameraBasisRelativeToOrigin(
                this->globe_target_lon_deg, this->globe_target_lat_deg,
                this->view_globe_yaw_deg, pitch_deg, distance,
                this->view_globe_vertical_offset_m, this->globe_render_origin_ecef);
        result.right = basis.right;
        result.up = basis.up;
        result.eye = basis.eye;
        return result;
    }

    const double pitch_rad = qDegreesToRadians(qBound(
        MapModel::MinView3dPitchDeg,
        this->view_3d_pitch_deg,
        MapModel::MaxView3dPitchDeg));
    const double yaw_rad = qDegreesToRadians(this->view_3d_yaw_deg);
    const double distance = orbitDistanceWorld();
    const double horizontal_distance = distance * std::cos(pitch_rad);
    const QVector3D target(
        float(this->center_world.x()), float(this->center_world.y()),
        float(this->view_3d_vertical_offset_world));
    result.eye = QVector3D(
        target.x() + float(std::sin(yaw_rad) * horizontal_distance),
        target.y() + float(std::cos(yaw_rad) * horizontal_distance),
        float(this->view_3d_vertical_offset_world
            + distance * std::sin(pitch_rad)
            + this->view_3d_camera_collision_lift_world));
    const QVector3D forward = (target - result.eye).normalized();
    result.right = QVector3D(
        float(std::cos(yaw_rad)), float(-std::sin(yaw_rad)), 0.0f);
    result.up = QVector3D::crossProduct(forward, result.right).normalized();
    return result;
}

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
    this->view_globe_vertical_offset_m = map_model.viewGlobeVerticalOffsetM();

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

GeoWgs84Ellipsoid::EcefPositionD MapRhiCamera::globeRenderOriginEcef() const
{
    // Just a getter for the sticky value updateGlobeRenderOrigin() manages
    // -- see that function's comment for why this is not itself a fresh
    // per-call computation from the current target.
    return this->globe_render_origin_ecef;
}

void MapRhiCamera::updateGlobeRenderOrigin()
{
    const GeoWgs84Ellipsoid::EcefPositionD candidate = GeoWgs84Ellipsoid::geodeticToEcefD(
        this->globe_target_lon_deg, this->globe_target_lat_deg,
        this->view_globe_vertical_offset_m);

    if (!this->globe_render_origin_valid)
    {
        this->globe_render_origin_ecef = candidate;
        this->globe_render_origin_valid = true;
        return;
    }

    const double delta_x = candidate.x - this->globe_render_origin_ecef.x;
    const double delta_y = candidate.y - this->globe_render_origin_ecef.y;
    const double delta_z = candidate.z - this->globe_render_origin_ecef.z;
    const double drift_squared_m = delta_x * delta_x + delta_y * delta_y + delta_z * delta_z;

    // Precision requirements are screen-relative. At close zoom the 50 km
    // floor still applies; farther out, one logical pixel covers many metres
    // and the origin may remain fixed across correspondingly larger globe
    // rotations without losing even 0.05 px of vertex precision. As the user
    // zooms back in this threshold contracts automatically, causing one rebase
    // before the old origin could become visible as jitter.
    const double distance_m = qMax(
        MapModel::MinViewGlobeDistanceM, this->view_globe_distance_m);
    const double viewport_height_px = double(qMax(1, this->viewport_size.height()));
    const double meters_per_pixel =
        2.0 * distance_m
        * std::tan(qDegreesToRadians(MapModel::GlobeFieldOfViewDeg * 0.5))
        / viewport_height_px;
    const double precision_scaled_threshold_m =
        meters_per_pixel * GlobeRenderOriginPrecisionBudgetPx * Float32FractionScale;
    const double rebase_threshold_m = qMax(
        GlobeRenderOriginMinimumRebaseThresholdM,
        precision_scaled_threshold_m);
    const double threshold_squared_m =
        rebase_threshold_m * rebase_threshold_m;
    if (drift_squared_m > threshold_squared_m)
        this->globe_render_origin_ecef = candidate;
}

QMatrix4x4 MapRhiCamera::globeViewProjectionMatrix(const QRhi &rhi) const
{
    const int viewport_width = qMax(1, this->viewport_size.width());
    const int viewport_height = qMax(1, this->viewport_size.height());

    const double pitch_deg = qBound(
        MapModel::MinViewGlobePitchDeg, this->view_globe_pitch_deg,
        MapModel::MaxViewGlobePitchDeg);
    const double distance = qMax(MapModel::MinViewGlobeDistanceM, this->view_globe_distance_m);
    const GeoWgs84Ellipsoid::OrbitCameraBasis basis = GeoWgs84Ellipsoid::orbitCameraBasis(
        this->globe_target_lon_deg, this->globe_target_lat_deg,
        this->view_globe_yaw_deg, pitch_deg, distance, this->view_globe_vertical_offset_m);

    constexpr float FieldOfViewDeg = float(MapModel::GlobeFieldOfViewDeg);
    // See globeNearFarPlanesM()'s comment for why this is not simply
    // (distance + EquatorialRadiusM) * 3.0 any more -- that wasted almost
    // all of the depth buffer's precision on distances nothing is ever
    // drawn at, which is directly responsible for Globe-specific z-fighting
    // (flickering) that the equivalent ThreeD rendering code doesn't show.
    double near_plane = 0.0;
    double far_plane = 0.0;
    globeNearFarPlanesM(
        distance, pitch_deg, this->view_globe_vertical_offset_m,
        &near_plane, &far_plane);
    QMatrix4x4 projection;
    projection.perspective(
        FieldOfViewDeg, float(viewport_width) / float(viewport_height),
        float(near_plane), float(far_plane));

    QMatrix4x4 view;
    view.lookAt(basis.eye, basis.target, basis.up);

    QMatrix4x4 result = rhi.clipSpaceCorrMatrix();
    result *= projection;
    result *= view;
    return result;
}

QMatrix4x4 MapRhiCamera::globeNetworkViewProjectionMatrix(
    const QRhi &rhi, MapRhiImpostorCameraBasis *impostor_camera_basis) const
{
    const int viewport_width = qMax(1, this->viewport_size.width());
    const int viewport_height = qMax(1, this->viewport_size.height());

    const double pitch_deg = qBound(
        MapModel::MinViewGlobePitchDeg, this->view_globe_pitch_deg,
        MapModel::MaxViewGlobePitchDeg);
    const double distance = qMax(MapModel::MinViewGlobeDistanceM, this->view_globe_distance_m);
    // Same projection (FOV/near/far) as globeViewProjectionMatrix() above,
    // with a view built relative to globeRenderOriginEcef() via
    // orbitCameraBasisRelativeToOrigin(). Both GPU terrain and network use
    // this matrix and build their vertices relative to the same origin.
    const GeoWgs84Ellipsoid::OrbitCameraBasisRelative basis =
        GeoWgs84Ellipsoid::orbitCameraBasisRelativeToOrigin(
            this->globe_target_lon_deg, this->globe_target_lat_deg,
            this->view_globe_yaw_deg, pitch_deg, distance,
            this->view_globe_vertical_offset_m, this->globe_render_origin_ecef);
    if (impostor_camera_basis != nullptr)
    {
        impostor_camera_basis->right = basis.right;
        impostor_camera_basis->up = basis.up;
        impostor_camera_basis->eye = basis.eye;
    }

    constexpr float FieldOfViewDeg = float(MapModel::GlobeFieldOfViewDeg);
    double near_plane = 0.0;
    double far_plane = 0.0;
    globeNearFarPlanesM(
        distance, pitch_deg, this->view_globe_vertical_offset_m,
        &near_plane, &far_plane);
    QMatrix4x4 projection;
    projection.perspective(
        FieldOfViewDeg, float(viewport_width) / float(viewport_height),
        float(near_plane), float(far_plane));

    QMatrix4x4 view;
    view.lookAt(basis.eye, basis.target, basis.up);

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

double MapRhiCamera::globeOrbitDistanceM() const
{
    return qMax(MapModel::MinViewGlobeDistanceM, this->view_globe_distance_m);
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
