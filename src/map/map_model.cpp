#include "map_model.h"

#include <algorithm>
#include <cmath>

#include <QVector3D>
#include <QtMath>

namespace
{
constexpr double CoordinateComparisonEpsilon = 1e-12;
constexpr double View3dFieldOfViewDeg = 45.0;
constexpr double View3dMinimumPitchDeg = 20.0;
constexpr double View3dMaximumPitchDeg = 80.0;
constexpr double View3dDefaultPitchDeg = 55.0;

struct View3dCameraBasis
{
    QVector3D eye;
    QVector3D forward;
    QVector3D right;
    QVector3D up;
};

double normalizedYawDegrees(double yaw_deg)
{
    double normalized = std::fmod(yaw_deg, 360.0);
    if (normalized < 0.0)
        normalized += 360.0;
    return normalized;
}

View3dCameraBasis view3dCameraBasis(double yaw_deg, double pitch_deg, const QSize &viewport)
{
    const double safe_height = qMax(1, viewport.height());
    const double half_height = safe_height / 2.0;
    const double half_fov_rad = qDegreesToRadians(View3dFieldOfViewDeg / 2.0);
    const double distance = half_height / std::tan(half_fov_rad);
    const double pitch_rad = qDegreesToRadians(
        qBound(View3dMinimumPitchDeg, pitch_deg, View3dMaximumPitchDeg));
    const double yaw_rad = qDegreesToRadians(normalizedYawDegrees(yaw_deg));
    const double horizontal_distance = distance * std::cos(pitch_rad);

    View3dCameraBasis basis;
    basis.eye = QVector3D(
        float(std::sin(yaw_rad) * horizontal_distance),
        float(std::cos(yaw_rad) * horizontal_distance),
        float(distance * std::sin(pitch_rad)));
    basis.forward = (-basis.eye).normalized();
    const QVector3D world_up(0.0f, 0.0f, 1.0f);
    basis.right = QVector3D::crossProduct(basis.forward, world_up).normalized();
    basis.up = QVector3D::crossProduct(basis.right, basis.forward).normalized();
    return basis;
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

double MapModel::view3dYawDeg() const
{
    return this->m_view_3d_yaw_deg;
}

double MapModel::view3dPitchDeg() const
{
    return this->m_view_3d_pitch_deg;
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
    this->m_centerLat = std::clamp(
        lat, -GeoWebMercator::MaximumLatitude, GeoWebMercator::MaximumLatitude);

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

void MapModel::setCenter(double lon, double lat, const QSize &viewport)
{
    setView(lon, lat, this->m_zoom, viewport);
}

void MapModel::setZoom(int zoom_value, const QSize &viewport)
{
    setView(this->m_centerLon, this->m_centerLat, zoom_value, viewport);
}

void MapModel::zoomIn(const QSize &viewport)
{
    setZoom(this->m_zoom + 1, viewport);
}

void MapModel::zoomOut(const QSize &viewport)
{
    setZoom(this->m_zoom - 1, viewport);
}

void MapModel::zoomByAt(int steps, const QPoint &anchorPos, const QSize &viewport)
{
    if (!viewport.isValid() || steps == 0)
        return;

    const int old_zoom = this->m_zoom;
    const int new_zoom = std::clamp(old_zoom + steps, MinZoom, MaxZoom);

    if (new_zoom == old_zoom)
        return;

    const double old_lon = this->m_centerLon;
    const double old_lat = this->m_centerLat;
    const QPointF old_center = centerTile();
    const double anchor_offset_x = (anchorPos.x() - viewport.width() / 2.0) / TileSize;
    const double anchor_offset_y = (anchorPos.y() - viewport.height() / 2.0) / TileSize;
    const double zoom_scale = GeoWebMercator::zoomScale(new_zoom, old_zoom);

    const double anchor_tile_x_new = (old_center.x() + anchor_offset_x) * zoom_scale;
    const double anchor_tile_y_new = (old_center.y() + anchor_offset_y) * zoom_scale;
    const double center_tile_x_new = anchor_tile_x_new - anchor_offset_x;
    const double center_tile_y_new = anchor_tile_y_new - anchor_offset_y;

    this->m_zoom = new_zoom;
    this->m_centerLon = GeoWebMercator::normalizeLongitude(
        GeoWebMercator::tileXToLon(center_tile_x_new, this->m_zoom));
    this->m_centerLat = GeoWebMercator::tileYToLat(center_tile_y_new, this->m_zoom);
    clampCenter(viewport);

    emit zoomChanged(this->m_zoom);

    if (!coordinatesEqual(this->m_centerLon, old_lon) ||
        !coordinatesEqual(this->m_centerLat, old_lat))
    {
        emitCenterChanged();
    }
}

void MapModel::panByPixels(const QPoint &delta, const QSize &viewport)
{
    if (delta.isNull())
        return;

    QPointF center = centerTile();
    center.rx() -= double(delta.x()) / TileSize;
    center.ry() -= double(delta.y()) / TileSize;

    setCenter(
        GeoWebMercator::tileXToLon(center.x(), this->m_zoom),
        GeoWebMercator::tileYToLat(center.y(), this->m_zoom),
        viewport);
}

void MapModel::panByPixels3d(const QPoint &delta, const QSize &viewport)
{
    if (delta.isNull() || !viewport.isValid())
        return;

    const QPointF viewport_center(
        viewport.width() / 2.0, viewport.height() / 2.0);
    const QPointF center_ground = groundOffsetFromScreen3d(viewport_center, viewport);
    const QPointF dragged_ground = groundOffsetFromScreen3d(
        viewport_center - QPointF(delta), viewport);
    if (!std::isfinite(center_ground.x()) || !std::isfinite(center_ground.y())
        || !std::isfinite(dragged_ground.x()) || !std::isfinite(dragged_ground.y()))
    {
        return;
    }

    QPointF center = centerTile();
    const QPointF offset = dragged_ground - center_ground;
    center.rx() += offset.x() / TileSize;
    center.ry() += offset.y() / TileSize;

    setCenter(
        GeoWebMercator::tileXToLon(center.x(), this->m_zoom),
        GeoWebMercator::tileYToLat(center.y(), this->m_zoom),
        viewport);
}

void MapModel::clampCenter(const QSize &viewport)
{
    if (!viewport.isValid())
        return;

    const double world_tile_count = double(tileCount());
    const double half_viewport_height_tiles = viewport.height() / double(TileSize) / 2.0;
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

void MapModel::setViewMode(MapViewMode view_mode)
{
    if (this->m_view_mode == view_mode)
        return;

    this->m_view_mode = view_mode;
    emit viewModeChanged(this->m_view_mode);
}

void MapModel::orbitView3d(double yaw_delta_deg, double pitch_delta_deg)
{
    const double next_yaw = normalizedYawDegrees(this->m_view_3d_yaw_deg + yaw_delta_deg);
    const double next_pitch = qBound(
        View3dMinimumPitchDeg,
        this->m_view_3d_pitch_deg + pitch_delta_deg,
        View3dMaximumPitchDeg);
    if (coordinatesEqual(next_yaw, this->m_view_3d_yaw_deg)
        && coordinatesEqual(next_pitch, this->m_view_3d_pitch_deg))
    {
        return;
    }

    this->m_view_3d_yaw_deg = next_yaw;
    this->m_view_3d_pitch_deg = next_pitch;
    emit view3dCameraChanged();
}

void MapModel::resetView3dCamera()
{
    const bool changed = !coordinatesEqual(this->m_view_3d_yaw_deg, 0.0)
        || !coordinatesEqual(this->m_view_3d_pitch_deg, View3dDefaultPitchDeg);
    this->m_view_3d_yaw_deg = 0.0;
    this->m_view_3d_pitch_deg = View3dDefaultPitchDeg;
    if (changed)
        emit view3dCameraChanged();
}

CoordinateWGS84 MapModel::wgs84FromScreen(const QPoint &pos, const QSize &viewport) const
{
    const QPointF center = centerTile();
    if (this->m_view_mode == MapViewMode::ThreeD)
    {
        const QPointF ground_offset = groundOffsetFromScreen3d(pos, viewport);
        if (std::isfinite(ground_offset.x()) && std::isfinite(ground_offset.y()))
        {
            const double tile_x = center.x() + ground_offset.x() / TileSize;
            const double tile_y = std::clamp(
                center.y() + ground_offset.y() / TileSize, 0.0, double(tileCount()));
            CoordinateWGS84 wgs;
            wgs.latitude_deg = GeoWebMercator::tileYToLat(tile_y, this->m_zoom);
            wgs.longitude_deg = GeoWebMercator::normalizeLongitude(
                GeoWebMercator::tileXToLon(tile_x, this->m_zoom));
            return wgs;
        }
    }

    const double tile_x = center.x() + (pos.x() - viewport.width() / 2.0) / TileSize;
    const double unclamped_tile_y = center.y() + (pos.y() - viewport.height() / 2.0) / TileSize;
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
    if (this->m_view_mode == MapViewMode::ThreeD)
        return screenFromTileOffset3d(offset_pixels, viewport);

    return QPointF(
        double(viewport.width()) / 2.0 + offset_pixels.x(),
        double(viewport.height()) / 2.0 + offset_pixels.y());
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
    if (this->m_view_mode == MapViewMode::ThreeD)
        return screenFromTileOffset3d(offset_pixels, viewport);

    return QPointF(
        double(viewport.width()) / 2.0 + offset_pixels.x(),
        double(viewport.height()) / 2.0 + offset_pixels.y());
}

QPointF MapModel::groundOffsetFromScreen3d(
    const QPointF &position, const QSize &viewport) const
{
    if (!viewport.isValid())
        return QPointF(qQNaN(), qQNaN());

    const View3dCameraBasis basis = view3dCameraBasis(
        this->m_view_3d_yaw_deg, this->m_view_3d_pitch_deg, viewport);
    const double width = qMax(1, viewport.width());
    const double height = qMax(1, viewport.height());
    const double aspect = width / height;
    const double tan_half_fov = std::tan(qDegreesToRadians(View3dFieldOfViewDeg / 2.0));
    const double ndc_x = position.x() * 2.0 / width - 1.0;
    const double ndc_y = 1.0 - position.y() * 2.0 / height;

    QVector3D direction = basis.forward
        + basis.right * float(ndc_x * tan_half_fov * aspect)
        + basis.up * float(ndc_y * tan_half_fov);
    direction.normalize();
    if (std::abs(direction.z()) < 1e-6f)
        return QPointF(qQNaN(), qQNaN());

    const double distance = -double(basis.eye.z()) / double(direction.z());
    if (!std::isfinite(distance) || distance <= 0.0)
        return QPointF(qQNaN(), qQNaN());

    const QVector3D ground = basis.eye + direction * float(distance);
    return QPointF(ground.x(), ground.y());
}

QPointF MapModel::screenFromTileOffset3d(
    const QPointF &offset_pixels, const QSize &viewport) const
{
    if (!viewport.isValid())
        return QPointF(qQNaN(), qQNaN());

    const View3dCameraBasis basis = view3dCameraBasis(
        this->m_view_3d_yaw_deg, this->m_view_3d_pitch_deg, viewport);
    const QVector3D point(
        float(offset_pixels.x()), float(offset_pixels.y()), 0.0f);
    const QVector3D relative = point - basis.eye;
    const double depth = QVector3D::dotProduct(relative, basis.forward);
    if (depth <= 1e-6)
        return QPointF(qQNaN(), qQNaN());

    const double width = qMax(1, viewport.width());
    const double height = qMax(1, viewport.height());
    const double aspect = width / height;
    const double tan_half_fov = std::tan(qDegreesToRadians(View3dFieldOfViewDeg / 2.0));
    const double camera_x = QVector3D::dotProduct(relative, basis.right);
    const double camera_y = QVector3D::dotProduct(relative, basis.up);
    const double ndc_x = camera_x / (depth * tan_half_fov * aspect);
    const double ndc_y = camera_y / (depth * tan_half_fov);

    return QPointF(
        (ndc_x + 1.0) * width / 2.0,
        (1.0 - ndc_y) * height / 2.0);
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
