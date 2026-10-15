#include "map/rhi/map_rhi_camera.h"

#include "geo/geo_web_mercator.h"
#include "map/core/map_model.h"
#include "map/render/map_render_cache_math.h"

#include <rhi/qrhi.h>

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
    this->globe_camera.setViewportSize(viewport_size);
}

void MapRhiCamera::syncFromMapModel(const MapModel &map_model)
{
    const MapCameraViewState view = map_model.cameraViewState();
    this->continuous_2d_zoom = view.continuous_2d_zoom;
    this->globe_camera.syncFromMapModel(map_model);

    const QPointF raw_center_world = GeoWebMercator::lonLatToWorldPixel(
        GeoWebMercator::normalizeLongitude(view.center_lon_deg),
        view.center_lat_deg,
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
    // GPU view scaling is continuous: selecting a different imagery tile
    // zoom level must not change this matrix or any network vertex data.
    const double safe_scale = std::exp2(
        this->continuous_2d_zoom - MapRenderCacheMath::ReferenceZoom);
    const double half_width_world =
        double(viewport_width) / (2.0 * safe_scale);
    const double half_height_world =
        double(viewport_height) / (2.0 * safe_scale);

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
    return this->globe_camera.renderOriginEcef();
}

void MapRhiCamera::updateGlobeRenderOrigin()
{
    this->globe_camera.updateRenderOrigin();
}

QMatrix4x4 MapRhiCamera::globeNetworkViewProjectionMatrix(
    const QRhi &rhi,
    const GeoWgs84Ellipsoid::EcefPositionD &network_origin_ecef,
    MapRhiImpostorCameraBasis *impostor_camera_basis) const
{
    QMatrix4x4 result = rhi.clipSpaceCorrMatrix();
    result *= this->globe_camera.viewProjectionMatrixForOrigin(
        network_origin_ecef, impostor_camera_basis);
    return result;
}

QPointF MapRhiCamera::projectWorldToScreen(
    const QVector3D &world_position) const
{
    const int viewport_width = qMax(1, this->viewport_size.width());
    const int viewport_height = qMax(1, this->viewport_size.height());
    // GPU view scaling is continuous: selecting a different imagery tile
    // zoom level must not change this matrix or any network vertex data.
    const double safe_scale = std::exp2(
        this->continuous_2d_zoom - MapRenderCacheMath::ReferenceZoom);

    return QPointF(
        viewport_width / 2.0
            + (double(world_position.x()) - this->center_world.x())
                * safe_scale,
        viewport_height / 2.0
            + (double(world_position.y()) - this->center_world.y())
                * safe_scale);
}

double MapRhiCamera::globeOrbitDistanceM() const
{
    return this->globe_camera.orbitDistanceM();
}

bool MapRhiCamera::globeScreenRay(
    const QPointF &screen_position,
    MapGlobeScreenRay *ray) const
{
    return this->globe_camera.screenRay(screen_position, ray);
}

const MapGlobeCamera &MapRhiCamera::globeCamera() const
{
    return this->globe_camera;
}
