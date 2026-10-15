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
    // QRhi adapter for the retained-network ECEF frame. Terrain uses the
    // independently rebased globeRenderOriginEcef() instead. QRhi-specific
    // clip-space correction is applied after the neutral camera matrix.
    QMatrix4x4 globeNetworkViewProjectionMatrix(
        const QRhi &rhi,
        const GeoWgs84Ellipsoid::EcefPositionD &network_origin_ecef,
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
    double continuous_2d_zoom = 0.0;
    MapGlobeCamera globe_camera;
};

#endif // MAP_RHI_CAMERA_H
