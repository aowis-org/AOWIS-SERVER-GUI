#ifndef MAP_RHI_CAMERA_H
#define MAP_RHI_CAMERA_H

#include "map/render/map_globe_camera.h"

#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QVector3D>

class MapModel;
class QRhi;

using MapRhiImpostorCameraBasis = MapGlobeCameraBasis;

class MapRhiCamera
{
public:
    MapRhiCamera();

    void setSceneOriginWorld(const QPointF &origin_world);
    void setViewportSize(const QSize &viewport_size);
    void syncFromMapModel(const MapModel &map_model);

    QMatrix4x4 viewProjectionMatrix(const QRhi &rhi) const;
    // QRhi adapter around the backend-neutral Globe camera matrix. Terrain
    // and network vertices must both be built relative to
    // globeRenderOriginEcef(). QRhi-specific clip-space correction is applied
    // only here, after MapGlobeCamera has produced the canonical matrix.
    QMatrix4x4 globeNetworkViewProjectionMatrix(
        const QRhi &rhi,
        MapRhiImpostorCameraBasis *impostor_camera_basis = nullptr) const;
    QPointF projectWorldToScreen(const QVector3D &world_position) const;
    double globeOrbitDistanceM() const;
    bool globeScreenRay(
        const QPointF &screen_position,
        MapGlobeScreenRay *ray) const;
    const MapGlobeCamera &globeCamera() const;

    // Maintains the sticky backend-neutral Globe render origin. Must be called
    // once per frame before using globeNetworkViewProjectionMatrix() or
    // globeRenderOriginEcef().
    void updateGlobeRenderOrigin();
    GeoWgs84Ellipsoid::EcefPositionD globeRenderOriginEcef() const;

private:
    QPointF scene_origin_world;
    QPointF center_world;
    QSize viewport_size;
    int zoom = 0;
    double view_2d_continuous_scale = 1.0;
    MapGlobeCamera globe_camera;
};

#endif // MAP_RHI_CAMERA_H
