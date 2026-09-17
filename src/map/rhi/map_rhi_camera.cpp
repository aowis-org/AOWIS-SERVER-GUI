#include "map/rhi/map_rhi_camera.h"

#include "geo/geo_web_mercator.h"
#include "map/core/map_model.h"
#include "map/render/map_render_cache_math.h"

#include <rhi/qrhi.h>

#include <QtMath>

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
    this->zoom = map_model.zoom();
    this->view_2d_continuous_scale = map_model.view2dContinuousScale();
    this->globe_camera.syncFromMapModel(map_model);

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
    const double scale =
        base_scale * qMax(1e-9, this->view_2d_continuous_scale);
    const double safe_scale = scale > 0.0 ? scale : 1.0;
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
    const QRhi &rhi, MapRhiImpostorCameraBasis *impostor_camera_basis) const
{
    QMatrix4x4 result = rhi.clipSpaceCorrMatrix();
    result *= this->globe_camera.viewProjectionMatrix(impostor_camera_basis);
    return result;
}

QPointF MapRhiCamera::projectWorldToScreen(
    const QVector3D &world_position) const
{
    const int viewport_width = qMax(1, this->viewport_size.width());
    const int viewport_height = qMax(1, this->viewport_size.height());
    const double base_scale = GeoWebMercator::zoomScale(
        this->zoom, MapRenderCacheMath::ReferenceZoom);
    const double scale =
        base_scale * qMax(1e-9, this->view_2d_continuous_scale);
    const double safe_scale = scale > 0.0 ? scale : 1.0;

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
