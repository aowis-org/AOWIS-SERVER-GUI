#include "map_model.h"

MapModel::MapModel(QObject *parent)
    : QObject(parent)
{
}

int MapModel::zoom() const
{
    return m_zoom;
}

double MapModel::centerLon() const
{
    return m_centerLon;
}

double MapModel::centerLat() const
{
    return m_centerLat;
}

MapProvider MapModel::provider() const
{
    return m_provider;
}

QString MapModel::providerCacheKey() const
{
    return m_providerCacheKey;
}

int MapModel::tileCount() const
{
    return 1 << m_zoom;
}

QPointF MapModel::centerTile() const
{
    return QPointF(lonToTileX(m_centerLon, m_zoom),
                   latToTileY(m_centerLat, m_zoom));
}

QString MapModel::tileCacheKey(int x, int y) const
{
    return m_providerCacheKey + QString("/%1/%2/%3").arg(m_zoom).arg(x).arg(y);
}

QString MapModel::tileEndpoint(int x, int y) const
{
    // Fallback: only OSM has zoom levels > 17 in the current backend.
    const QString path = (m_zoom > 17) ? QStringLiteral("openstreetmap") : providerPath();
    
    return QString("/%1/%2/%3/%4.png")
        .arg(path)
        .arg(m_zoom)
        .arg(x)
        .arg(y);
}

QString MapModel::providerPath() const
{
    switch (m_provider)
    {
    case MapProvider::ArcGISSat:
        return QStringLiteral("arcgis");
    case MapProvider::OpenTopoMap:
        return QStringLiteral("opentopomap");
    case MapProvider::OpenStreetMap:
        return QStringLiteral("openstreetmap");
    }
    
    return QStringLiteral("arcgis");
}

void MapModel::setCenter(double lon, double lat, const QSize &viewport)
{
    m_centerLon = lon;
    m_centerLat = lat;
    
    if (viewport.isValid())
        clampCenter(viewport);
    
    emit centerChanged(m_centerLon, m_centerLat);
}

void MapModel::setZoom(int zoomValue, const QSize &viewport)
{
    const int clampedZoom = std::clamp(zoomValue, MinZoom, MaxZoom);
    
    if (clampedZoom == m_zoom)
        return;
    
    m_zoom = clampedZoom;
    
    if (viewport.isValid())
        clampCenter(viewport);
    
    emit zoomChanged(m_zoom);
    emit centerChanged(m_centerLon, m_centerLat);
}

void MapModel::zoomIn(const QSize &viewport)
{
    setZoom(m_zoom + 1, viewport);
}

void MapModel::zoomOut(const QSize &viewport)
{
    setZoom(m_zoom - 1, viewport);
}

void MapModel::zoomByAt(int steps, const QPoint &anchorPos, const QSize &viewport)
{
    if (!viewport.isValid() || steps == 0)
        return;
    
    const int newZoom = std::clamp(m_zoom + steps, MinZoom, MaxZoom);
    
    if (newZoom == m_zoom)
        return;
    
    const double cx = lonToTileX(m_centerLon, m_zoom);
    const double cy = latToTileY(m_centerLat, m_zoom);
    
    const double dx = anchorPos.x() - viewport.width() / 2.0;
    const double dy = anchorPos.y() - viewport.height() / 2.0;
    
    const double mouseTileX = cx + dx / TileSize;
    const double mouseTileY = cy + dy / TileSize;
    
    const double mouseLon = tileXToLon(mouseTileX, m_zoom);
    const double mouseLat = tileYToLat(mouseTileY, m_zoom);
    
    m_zoom = newZoom;
    
    const double mouseTileXNew = lonToTileX(mouseLon, m_zoom);
    const double mouseTileYNew = latToTileY(mouseLat, m_zoom);
    
    const double centerTileXNew = mouseTileXNew - dx / TileSize;
    const double centerTileYNew = mouseTileYNew - dy / TileSize;
    
    m_centerLon = tileXToLon(centerTileXNew, m_zoom);
    m_centerLat = tileYToLat(centerTileYNew, m_zoom);
    
    clampCenter(viewport);
    
    emit zoomChanged(m_zoom);
    emit centerChanged(m_centerLon, m_centerLat);
}

void MapModel::panByPixels(const QPoint &delta, const QSize &viewport)
{
    double cx = lonToTileX(m_centerLon, m_zoom);
    double cy = latToTileY(m_centerLat, m_zoom);
    
    cx -= double(delta.x()) / TileSize;
    cy -= double(delta.y()) / TileSize;
    
    m_centerLon = tileXToLon(cx, m_zoom);
    m_centerLat = tileYToLat(cy, m_zoom);
    
    if (viewport.isValid())
        clampCenter(viewport);
    
    emit centerChanged(m_centerLon, m_centerLat);
}

void MapModel::clampCenter(const QSize &viewport)
{
    if (!viewport.isValid())
        return;
    
    double cx = lonToTileX(m_centerLon, m_zoom);
    double cy = latToTileY(m_centerLat, m_zoom);
    
    const double maxTile = double((1 << m_zoom) - 1);
    
    const double halfW = (viewport.width() / double(TileSize)) / 2.0;
    const double halfH = (viewport.height() / double(TileSize)) / 2.0;
    
    const double minCx = halfW;
    const double maxCx = maxTile - halfW;
    
    const double minCy = halfH;
    const double maxCy = maxTile - halfH;
    
    if (minCx > maxCx)
        cx = maxTile / 2.0;
    else
        cx = std::clamp(cx, minCx, maxCx);
    
    if (minCy > maxCy)
        cy = maxTile / 2.0;
    else
        cy = std::clamp(cy, minCy, maxCy);
    
    m_centerLon = tileXToLon(cx, m_zoom);
    m_centerLat = tileYToLat(cy, m_zoom);
}

void MapModel::setProvider(MapProvider provider)
{
    if (m_provider == provider)
        return;
    
    m_provider = provider;
    m_providerCacheKey = providerPath();
    
    emit providerChanged(m_provider);
}

QPointF MapModel::latLonAt(const QPoint &pos, const QSize &viewport) const
{
    const double cx = lonToTileX(m_centerLon, m_zoom);
    const double cy = latToTileY(m_centerLat, m_zoom);
    
    const double dx = pos.x() - viewport.width() / 2.0;
    const double dy = pos.y() - viewport.height() / 2.0;
    
    const double tx = cx + dx / TileSize;
    const double ty = cy + dy / TileSize;
    
    const double lon = tileXToLon(tx, m_zoom);
    const double lat = tileYToLat(ty, m_zoom);
    
    return QPointF(lon, lat);
}

double MapModel::lonToTileX(double lon, int zoomValue) const
{
    return (lon + 180.0) / 360.0 * (1 << zoomValue);
}

double MapModel::latToTileY(double lat, int zoomValue) const
{
    const double rad = qDegreesToRadians(lat);
    return (1.0 - std::log(std::tan(rad) + 1.0 / std::cos(rad)) / M_PI) / 2.0 * (1 << zoomValue);
}

double MapModel::tileXToLon(double x, int zoomValue) const
{
    return x / (1 << zoomValue) * 360.0 - 180.0;
}

double MapModel::tileYToLat(double y, int zoomValue) const
{
    const double n = M_PI - 2.0 * M_PI * y / (1 << zoomValue);
    return qRadiansToDegrees(std::atan(0.5 * (std::exp(n) - std::exp(-n))));
}
