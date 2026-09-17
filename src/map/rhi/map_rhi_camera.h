#ifndef MAP_RHI_CAMERA_H
#define MAP_RHI_CAMERA_H

#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QVector3D>

#include "geo/geo_wgs84_ellipsoid.h"

class MapModel;
class QRhi;

struct MapRhiImpostorCameraBasis
{
    QVector3D right;
    QVector3D up;
    QVector3D eye;
};

class MapRhiCamera
{
public:
    MapRhiCamera();

    void setSceneOriginWorld(const QPointF &origin_world);
    void setViewportSize(const QSize &viewport_size);
    void syncFromMapModel(const MapModel &map_model);

    QMatrix4x4 viewProjectionMatrix(const QRhi &rhi) const;
    // Origin-relative Globe GPU view/projection matrix. Terrain and network
    // vertices must both be built relative to globeRenderOriginEcef() before
    // using it. That keeps close-in camera arithmetic well-conditioned and
    // prevents the two passes from drifting against one another in depth.
    QMatrix4x4 globeNetworkViewProjectionMatrix(
        const QRhi &rhi,
        MapRhiImpostorCameraBasis *impostor_camera_basis = nullptr) const;
    QPointF projectWorldToScreen(const QVector3D &world_position) const;
    // Current Globe orbit distance in meters, clamped the same way the
    // Globe view/projection matrices clamp it.
    double globeOrbitDistanceM() const;
    // Recomputes the sticky Globe render origin (see globeRenderOriginEcef()
    // below) from the current orbit target, *if* the target has drifted far
    // enough from the origin currently in use to warrant it. Must be called
    // once per frame, before globeNetworkViewProjectionMatrix() and before
    // feeding globeRenderOriginEcef() to MapRhiGlobeNetworkScene -- see
    // MapRhiWidget::renderGlobe().
    //
    // Deliberately NOT "recompute the origin fresh every frame from the
    // current target": the target moves on *every* frame of a pan gesture
    // (unlike a pure orbit, where it is stationary), and
    // MapRhiGlobeNetworkScene::setRenderOriginEcef() rebuilds that scene's
    // entire vertex buffers whenever the origin changes -- doing that every
    // panning frame is a severe, easily-hit performance regression, not a
    // hypothetical one. Precision does not actually demand a fresh origin
    // every frame either: float32 comfortably holds sub-centimeter
    // precision out to tens of kilometers from the origin (see
    // GlobeRenderOriginMinimumRebaseThresholdM's definition), so the origin only
    // needs to be "reasonably close" to the camera, not exactly on top of
    // it. This function keeps the origin fixed until the camera has
    // actually drifted past a screen-precision-derived margin, so distant
    // Globe rotation does not rebuild a large network merely because one
    // mouse step represents many kilometres. The margin contracts again
    // while zooming in, before origin drift can become visible.
    void updateGlobeRenderOrigin();
    // The ECEF point globeNetworkViewProjectionMatrix() currently renders
    // *relative to*, and the point every CPU-built Globe-mode vertex buffer
    // (currently only MapRhiGlobeNetworkScene's) must place its own
    // vertices relative to, in double precision, before ever narrowing to
    // float32 -- see globeNetworkViewProjectionMatrix()'s and
    // EcefPositionD's comments for why. Sticky: only updateGlobeRenderOrigin()
    // changes it, and only when the camera has drifted far enough to
    // warrant a rebase -- see that function's comment. Callers should
    // compare the returned value for equality against what they last saw to
    // detect "did the origin actually change" (MapRhiGlobeNetworkScene::
    // setRenderOriginEcef() already does this) rather than assuming it
    // changes every frame.
    GeoWgs84Ellipsoid::EcefPositionD globeRenderOriginEcef() const;

private:
    QPointF scene_origin_world;
    QPointF center_world;
    QSize viewport_size;
    int zoom = 0;
    double view_2d_continuous_scale = 1.0;
    // Globe camera state in WGS84/ECEF meters.
    double globe_target_lon_deg = 0.0;
    double globe_target_lat_deg = 0.0;
    double view_globe_yaw_deg = 0.0;
    double view_globe_pitch_deg = 55.0;
    double view_globe_distance_m = 0.0;
    double view_globe_vertical_offset_m = 0.0;
    double view_globe_camera_collision_lift_m = 0.0;

    // Sticky Globe GPU render origin -- see updateGlobeRenderOrigin()
    // and globeRenderOriginEcef(). "valid" starts false purely so the very
    // first updateGlobeRenderOrigin() call always adopts the current target
    // unconditionally, rather than needing some plausible-but-arbitrary
    // ECEF value to compare a drift distance against.
    GeoWgs84Ellipsoid::EcefPositionD globe_render_origin_ecef;
    bool globe_render_origin_valid = false;
};

#endif // MAP_RHI_CAMERA_H
