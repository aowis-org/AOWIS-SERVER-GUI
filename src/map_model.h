#ifndef MAP_MODEL_H
#define MAP_MODEL_H

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QString>

#include <algorithm>
#include <cmath>

#include <QtMath>

#include "geo_metric_projection.h"
#include "geo_web_mercator.h"
#include "_enums_structs.h"

class MapModel : public QObject
{
    Q_OBJECT
    
public:
    explicit MapModel(QObject *parent = nullptr);
    
    static constexpr int TileSize = 256;
    static constexpr int MinZoom = 1;
    static constexpr int MaxZoom = 19;
    
    int zoom() const;
    double centerLon() const;
    double centerLat() const;
    
    MapProvider provider() const;
    QString providerCacheKey() const;
    QString tileCacheKey(int x, int y) const;
    QString tileEndpoint(int x, int y) const;
    
    int tileCount() const;
    
    QPointF centerTile() const;
    Wgs84Coordinate wgs84FromScreen(const QPoint &pos, const QSize &viewport) const;
    QPointF screenFromWgs84(const Wgs84Coordinate &coord, const QSize &viewport) const;
    QPointF screenFromWgs84(double lon, double lat, const QSize &viewport) const;
    
    void setCenter(double lon, double lat, const QSize &viewport = QSize());
    void setZoom(int zoom, const QSize &viewport = QSize());
    void zoomIn(const QSize &viewport = QSize());
    void zoomOut(const QSize &viewport = QSize());
    
    void zoomByAt(int steps, const QPoint &anchorPos, const QSize &viewport);
    void panByPixels(const QPoint &delta, const QSize &viewport);
    void clampCenter(const QSize &viewport);
    
    void setProvider(MapProvider provider);
    
signals:
    void zoomChanged(int zoom);
    void centerChanged(Wgs84Coordinate wgs);
    void providerChanged(MapProvider provider);
    
private:
    int m_zoom = 16;
    double m_centerLon = 18.2063;
    double m_centerLat = 11.9792;
    
    MapProvider m_provider = MapProvider::ArcGISSat;
    QString m_providerCacheKey = "arcgis";
    
    QString providerPath() const;
};

#endif // MAP_MODEL_H
