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

#include <QDebug>

#include "rest_client.h"

class MapWidget : public QWidget
{
    Q_OBJECT
    
public:
    explicit MapWidget(QWidget *parent = nullptr);
    
protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    
    void paintEvent(QPaintEvent *event) override;
    
private:
    const int tile_size = 256;
    int zoom;
    double center_lat;
    double center_lon;
    
    RESTClient *rest;
    QSet<QString> pending;
    
    QPoint pos_last;
    
    QCache<QString, QImage> cache;
    
    void drawTiles(QPainter &p);
    void requestTile(const QString &key, int x, int y);
    void pan(const QPoint &delta);
    
    
    double lonToTileX(double lon, int zoom) const;
    double latToTileY(double lat, int zoom) const;
    double tileXToLon(double x, int zoom) const;
    double tileYToLat(double y, int zoom) const;
};

#endif // MAP_WIDGET_H
