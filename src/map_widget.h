#ifndef MAP_WIDGET_H
#define MAP_WIDGET_H

#include <QObject>
#include <QWidget>
#include <QPainter>
#include <QCache>
#include <QImage>
#include <QPoint>
#include <QSet>

#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>

#include <QTimer>
#include <QDateTime>

#include <QDebug>

#include "enums_structs.h"
#include "rest_client.h"

class MapWidget : public QWidget
{
    Q_OBJECT
    
public:
    explicit MapWidget(QWidget *parent = nullptr);
    
    void zoomIn();
    void zoomOut();
    void changeMapProvider(MapProvider provider);
    
protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    
    void keyPressEvent(QKeyEvent *event) override;
    
    void paintEvent(QPaintEvent *event) override;
    
private:
    const int TILE_SIZE = 256;
    int zoom;
    double center_lat;
    double center_lon;
    
    RESTClient *rest;
    QSet<QString> pending;
    
    QPoint pos_last;
    QPointF pan_velocity;
    QTimer *timer_pan_inertia;
    qint64 time_last_innertia = 0;
    void initializeTimer();
    
    QCache<QString, QPixmap> cache;
    //QString cache_key_coords;
    QString cache_key_provider;
    
    MapProvider map_provider = MapProvider::ArcGISSat;
    
    void drawTiles(QPainter &p);
    void requestTile(const QString &key, int x, int y);
    void pan(const QPoint &delta);
    void clampCenter();
    
    double lonToTileX(double lon, int zoom) const;
    double latToTileY(double lat, int zoom) const;
    double tileXToLon(double x, int zoom) const;
    double tileYToLat(double y, int zoom) const;
    
signals:
    void signalZoomChanged(int zoom);
    void signalCoordsChanged(double lon, double lat);
};

#endif // MAP_WIDGET_H
