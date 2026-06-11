#ifndef MAP_WIDGET_H
#define MAP_WIDGET_H

#include <QObject>
#include <QWidget>
#include <QPainter>
#include <QCache>
#include <QImage>
#include <QPoint>

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
    int zoom;
    double center_lat;
    double center_lon;
    
    QPoint pos_last;
    
    QCache<QString, QImage> cache;
    
    void drawTiles(QPainter &p);
    void requestTile(const QString &key, int x, int y);
    void pan(const QPoint &delta);
    
    
    double lonToTileX(double lon, int zoom) const;
    double latToTileY(double lat, int zoom) const;
};

#endif // MAP_WIDGET_H
