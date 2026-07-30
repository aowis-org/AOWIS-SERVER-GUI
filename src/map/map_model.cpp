#include "map_model.h"

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

QString MapModel::providerCacheKey() const
{
    return this->m_providerCacheKey;
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
    return this->m_providerCacheKey + QString("/%1/%2/%3").arg(this->m_zoom).arg(wrapped_x).arg(y);
}

QString MapModel::tileEndpoint(int x, int y) const
{
    const int wrapped_x = GeoWebMercator::wrapTileX(x, this->m_zoom);

    // Fallback: only OSM has zoom levels > 17 in the current backend.
    const QString path = (this->m_zoom > 17) ? QStringLiteral("osmcyclo") : this->providerPath();

    return QString("/%1/%2/%3/%4.png")
        .arg(path)
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

void MapModel::setCenter(double lon, double lat, const QSize &viewport)
{
    this->m_centerLon = GeoWebMercator::normalizeLongitude(lon);
    this->m_centerLat = std::clamp(lat, -GeoWebMercator::MaximumLatitude, GeoWebMercator::MaximumLatitude);

    if (viewport.isValid())
        this->clampCenter(viewport);

    this->emitCenterChanged();
}

void MapModel::setZoom(int zoomValue, const QSize &viewport)
{
    const int clamped_zoom = std::clamp(zoomValue, MinZoom, MaxZoom);

    if (clamped_zoom == this->m_zoom)
        return;

    this->m_zoom = clamped_zoom;

    if (viewport.isValid())
        this->clampCenter(viewport);

    emit zoomChanged(this->m_zoom);
    this->emitCenterChanged();
}

void MapModel::zoomIn(const QSize &viewport)
{
    this->setZoom(this->m_zoom + 1, viewport);
}

void MapModel::zoomOut(const QSize &viewport)
{
    this->setZoom(this->m_zoom - 1, viewport);
}

void MapModel::zoomByAt(int steps, const QPoint &anchorPos, const QSize &viewport)
{
    if (!viewport.isValid() || steps == 0)
        return;

    const int old_zoom = this->m_zoom;
    const int new_zoom = std::clamp(old_zoom + steps, MinZoom, MaxZoom);

    if (new_zoom == old_zoom)
        return;

    const QPointF old_center = this->centerTile();
    const double anchor_offset_x = (anchorPos.x() - viewport.width() / 2.0) / TileSize;
    const double anchor_offset_y = (anchorPos.y() - viewport.height() / 2.0) / TileSize;
    const double zoom_scale = std::ldexp(1.0, new_zoom - old_zoom);

    const double anchor_tile_x_new = (old_center.x() + anchor_offset_x) * zoom_scale;
    const double anchor_tile_y_new = (old_center.y() + anchor_offset_y) * zoom_scale;
    const double center_tile_x_new = anchor_tile_x_new - anchor_offset_x;
    const double center_tile_y_new = anchor_tile_y_new - anchor_offset_y;

    this->m_zoom = new_zoom;
    this->m_centerLon = GeoWebMercator::normalizeLongitude(GeoWebMercator::tileXToLon(center_tile_x_new, this->m_zoom));
    this->m_centerLat = GeoWebMercator::tileYToLat(center_tile_y_new, this->m_zoom);
    this->clampCenter(viewport);

    emit zoomChanged(this->m_zoom);
    this->emitCenterChanged();
}

void MapModel::panByPixels(const QPoint &delta, const QSize &viewport)
{
    QPointF center = this->centerTile();
    center.rx() -= double(delta.x()) / TileSize;
    center.ry() -= double(delta.y()) / TileSize;

    this->m_centerLon = GeoWebMercator::normalizeLongitude(GeoWebMercator::tileXToLon(center.x(), this->m_zoom));
    this->m_centerLat = GeoWebMercator::tileYToLat(center.y(), this->m_zoom);

    if (viewport.isValid())
        this->clampCenter(viewport);

    this->emitCenterChanged();
}

void MapModel::clampCenter(const QSize &viewport)
{
    if (!viewport.isValid())
        return;

    const double world_tile_count = double(this->tileCount());
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
    this->m_providerCacheKey = this->providerPath();

    emit providerChanged(this->m_provider);
}

CoordinateWGS84 MapModel::wgs84FromScreen(const QPoint &pos, const QSize &viewport) const
{
    const QPointF center = this->centerTile();
    const double tile_x = center.x() + (pos.x() - viewport.width() / 2.0) / TileSize;
    const double unclamped_tile_y = center.y() + (pos.y() - viewport.height() / 2.0) / TileSize;
    const double tile_y = std::clamp(unclamped_tile_y, 0.0, double(this->tileCount()));

    CoordinateWGS84 wgs;
    wgs.latitude_deg = GeoWebMercator::tileYToLat(tile_y, this->m_zoom);
    wgs.longitude_deg = GeoWebMercator::normalizeLongitude(GeoWebMercator::tileXToLon(tile_x, this->m_zoom));
    return wgs;
}

QPointF MapModel::screenFromWgs84(const CoordinateWGS84 &coord, const QSize &viewport) const
{
    return this->screenFromWgs84(coord.longitude_deg, coord.latitude_deg, viewport);
}

QPointF MapModel::screenFromWgs84(double lon, double lat, const QSize &viewport) const
{
    const QPointF center = this->centerTile();
    const double wrapped_lon = GeoWebMercator::normalizeLongitude(lon);
    const double base_tile_x = GeoWebMercator::lonToTileX(wrapped_lon, this->m_zoom);
    const double tile_x = GeoWebMercator::nearestWrappedTileX(base_tile_x, center.x(), this->m_zoom);
    const double tile_y = GeoWebMercator::latToTileY(lat, this->m_zoom);

    const double dx = (tile_x - center.x()) * TileSize;
    const double dy = (tile_y - center.y()) * TileSize;

    return QPointF(
        double(viewport.width()) / 2.0 + dx,
        double(viewport.height()) / 2.0 + dy
    );
}

QPointF MapModel::screenFromWgs84(const CoordinateWGS84 &coord, const QSize &viewport,
                                  double wrap_reference_lon) const
{
    return this->screenFromWgs84(coord.longitude_deg, coord.latitude_deg, viewport,
                                 wrap_reference_lon);
}

QPointF MapModel::screenFromWgs84(double lon, double lat, const QSize &viewport,
                                  double wrap_reference_lon) const
{
    const QPointF center = this->centerTile();
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

    const double dx = (tile_x - center.x()) * TileSize;
    const double dy = (tile_y - center.y()) * TileSize;

    return QPointF(
        double(viewport.width()) / 2.0 + dx,
        double(viewport.height()) / 2.0 + dy
    );
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
