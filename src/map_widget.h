#ifndef MAP_WIDGET_H
#define MAP_WIDGET_H

#include <QCache>
#include <QDateTime>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPoint>
#include <QSet>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

#include "map_model.h"

#include "interface_server_map_rest.h"
#include "interface_server_map_standalone.h"

class MapWidget : public QWidget
{
    Q_OBJECT
    
public:
    explicit MapWidget(QWidget *parent = nullptr);
    explicit MapWidget(MapModel *model, QWidget *parent = nullptr);
    
    MapModel *model() const;
    
    void addPanVelocity(int x, int y);
    
public slots:
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
    void init();
    void initServerMapInterface();
    void setMapServerMode(MapServerMode mode);
    
    void initTimer();
    
    void drawTiles(QPainter &p);
    void showContextMenu(const QPoint &pos);
    
    #ifdef AOWIS_STANDALONE
        MapServerMode map_server_mode = MapServerMode::Standalone;
    #else
        MapServerMode map_server_mode = MapServerMode::REST;
    #endif
    InterfaceServerMap *interface_map = nullptr;
    
    MapModel *m_model = nullptr;
    bool m_ownsModel = false;
    
    void tileReceived(const QString &key, QPixmap *pix);
    QCache<QString, QPixmap> m_cache;
    
    QPoint m_posLast;
    QPointF m_panVelocity;
    QTimer *m_timerPanInertia = nullptr;
    qint64 m_timeLastInertia = 0;
    
signals:
    void signalZoomChanged(int zoom);
    void signalCoordsChangedWgs84(CoordinateWGS84 wgs);
    void signalCoordsChangedUTM(CoordinateUTM utm);
};

#endif // MAP_WIDGET_H
