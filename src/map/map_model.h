#ifndef MAP_MODEL_H
#define MAP_MODEL_H

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QString>

#include "../_enums_structs.h"
#include "../geo_metric_projection.h"
#include "../geo_web_mercator.h"

class MapModel : public QObject
{
    Q_OBJECT

public:
    explicit MapModel(QObject *parent = nullptr);

    static constexpr int TileSize = GeoWebMercator::TileSize;
    static constexpr int MinZoom = 1;
    static constexpr int MaxZoom = 19;

    int zoom() const;
    double centerLon() const;
    double centerLat() const;

    MapProvider provider() const;
    MapViewMode viewMode() const;
    double view3dYawDeg() const;
    double view3dPitchDeg() const;
    QString tileCacheKey(int x, int y) const;
    QString tileCachePrefix(int zoom) const;
    QString tileEndpoint(int x, int y) const;
    QString tileSourcePath(int zoom) const;

    int tileCount() const;

    QPointF centerTile() const;
    CoordinateWGS84 wgs84FromScreen(const QPoint &pos, const QSize &viewport) const;
    QPointF screenFromWgs84(const CoordinateWGS84 &coord, const QSize &viewport) const;
    QPointF screenFromWgs84(double lon, double lat, const QSize &viewport) const;
    QPointF screenFromWgs84(const CoordinateWGS84 &coord, const QSize &viewport,
                            double wrap_reference_lon) const;
    QPointF screenFromWgs84(double lon, double lat, const QSize &viewport,
                            double wrap_reference_lon) const;

    void setView(double lon, double lat, int zoom, const QSize &viewport = QSize());
    void setCenter(double lon, double lat, const QSize &viewport = QSize());
    void setZoom(int zoom, const QSize &viewport = QSize());
    void zoomIn(const QSize &viewport = QSize());
    void zoomOut(const QSize &viewport = QSize());

    void zoomByAt(int steps, const QPoint &anchorPos, const QSize &viewport);
    void panByPixels(const QPoint &delta, const QSize &viewport);
    void panByPixels3d(const QPoint &delta, const QSize &viewport);

    void setProvider(MapProvider provider);
    void setViewMode(MapViewMode view_mode);
    void orbitView3d(double yaw_delta_deg, double pitch_delta_deg);
    void resetView3dCamera();

signals:
    void zoomChanged(int zoom);
    void centerChangedWGS84(CoordinateWGS84 wgs);
    void centerChangedUTM(CoordinateUTM utm);
    void providerChanged(MapProvider provider);
    void viewModeChanged(MapViewMode view_mode);
    void view3dCameraChanged();

private:
    void clampCenter(const QSize &viewport);
    void emitCenterChanged();
    QPointF groundOffsetFromScreen3d(const QPointF &position, const QSize &viewport) const;
    QPointF screenFromTileOffset3d(const QPointF &offset_pixels, const QSize &viewport) const;
    QString providerPath() const;

    int m_zoom = 18;
    double m_centerLon = 18.19331;
    double m_centerLat = 11.98119;
    /*
    int m_zoom = 16;
    double m_centerLon = 18.2063;
    double m_centerLat = 11.9792;
    */

    MapProvider m_provider = MapProvider::ArcGISSat;
    MapViewMode m_view_mode = MapViewMode::TwoD;
    double m_view_3d_yaw_deg = 0.0;
    double m_view_3d_pitch_deg = 55.0;
};

#endif // MAP_MODEL_H
