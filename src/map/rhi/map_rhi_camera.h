#ifndef MAP_RHI_CAMERA_H
#define MAP_RHI_CAMERA_H

#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QVector3D>

#include "common/_enums_structs.h"
#include "geo/geo_wgs84_ellipsoid.h"

class MapModel;
class QRhi;

class MapRhiCamera
{
public:
    MapRhiCamera();

    void setSceneOriginWorld(const QPointF &origin_world);
    void setViewportSize(const QSize &viewport_size);
    void syncFromMapModel(const MapModel &map_model);

    QMatrix4x4 viewProjectionMatrix(const QRhi &rhi) const;
    // Standard Globe view/projection matrix, in raw (Earth-center-relative)
    // ECEF space -- unchanged in meaning from before origin-relative
    // rendering was introduced for the network renderer. This is what
    // MapRhiGlobeRenderer's terrain tiles are drawn with, and what tile
    // LOD/occlusion/picking code (which all still work in raw ECEF, via
    // GeoWgs84Ellipsoid::orbitCameraBasis()) implicitly assumes. Do NOT
    // repurpose this for network rendering -- see
    // globeNetworkViewProjectionMatrix() below for that, and
    // updateGlobeRenderOrigin()'s comment for why the two must not be
    // conflated.
    QMatrix4x4 globeViewProjectionMatrix(const QRhi &rhi) const;
    // Globe-network-only counterpart of globeViewProjectionMatrix(): the
    // same camera, but expressed relative to globeRenderOriginEcef()
    // instead of raw ECEF, so it stays numerically well-conditioned at
    // close-in Globe zoom where eye and target are only meters apart (see
    // the EcefPositionD/OrbitCameraBasisRelative comments in
    // geo_wgs84_ellipsoid.h). Must be called with the exact same
    // MapRhiGlobeNetworkScene that was fed globeRenderOriginEcef() this
    // frame (see MapRhiWidget::renderGlobe()) -- the two are only
    // consistent with each other, not with raw ECEF vertex data (i.e. not
    // with anything drawn using globeViewProjectionMatrix() above, such as
    // Globe terrain tiles).
    QMatrix4x4 globeNetworkViewProjectionMatrix(const QRhi &rhi) const;
    QPointF projectWorldToScreen(const QVector3D &world_position) const;
    QPointF cameraGroundWorldPixel() const;
    QPointF cameraGroundWorldPixelForDistance(double distance_world) const;
    double nativeOrbitDistanceWorld() const;
    double orbitDistanceWorld() const;
    // Globe counterpart of orbitDistanceWorld(): the current Globe orbit
    // distance in meters, clamped the same way globeViewProjectionMatrix()
    // clamps it. Lets other Globe-mode geometry (e.g. the network renderer's
    // icon perspective scaling) stay referenced to the same distance the GPU
    // camera actually used.
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
    // GlobeRenderOriginRebaseThresholdM's definition), so the origin only
    // needs to be "reasonably close" to the camera, not exactly on top of
    // it. This function keeps the origin fixed until the camera has
    // actually drifted past that generous margin, so ordinary
    // orbiting/panning around one local area triggers zero rebases (and
    // zero geometry rebuilds) after the first frame.
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
    double perspectiveDepthWorld(const QVector3D &world_position) const;
    bool screenRay(const QPointF &screen_position, QVector3D *eye_world,
                   QVector3D *direction_world) const;
    bool crosshairRay(QVector3D *eye_world, QVector3D *direction_world) const;

private:
    QPointF scene_origin_world;
    QPointF center_world;
    QSize viewport_size;
    int zoom = 0;
    double view_2d_continuous_scale = 1.0;
    MapViewMode view_mode = MapViewMode::TwoD;
    double view_3d_yaw_deg = 0.0;
    double view_3d_pitch_deg = 55.0;
    double view_3d_camera_distance_world = 0.0;
    double view_3d_camera_collision_lift_world = 0.0;
    double view_3d_vertical_offset_world = 0.0;

    // Globe view mode camera state, synced from MapModel like the ThreeD
    // fields above. Kept separate rather than reusing the ThreeD fields
    // because the two cameras operate in different spaces (flat Web
    // Mercator "world pixel" units above vs. WGS84 ECEF meters here) and
    // are never active at the same time.
    double globe_target_lon_deg = 0.0;
    double globe_target_lat_deg = 0.0;
    double view_globe_yaw_deg = 0.0;
    double view_globe_pitch_deg = 55.0;
    double view_globe_distance_m = 0.0;
    double view_globe_vertical_offset_m = 0.0;

    // Sticky Globe network render origin -- see updateGlobeRenderOrigin()
    // and globeRenderOriginEcef(). "valid" starts false purely so the very
    // first updateGlobeRenderOrigin() call always adopts the current target
    // unconditionally, rather than needing some plausible-but-arbitrary
    // ECEF value to compare a drift distance against.
    GeoWgs84Ellipsoid::EcefPositionD globe_render_origin_ecef;
    bool globe_render_origin_valid = false;
};

#endif // MAP_RHI_CAMERA_H
