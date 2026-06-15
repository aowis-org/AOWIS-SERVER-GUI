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
#include "rest_client.h"

class MapWidget : public QWidget
{
    Q_OBJECT
    
public:
    explicit MapWidget(QWidget *parent = nullptr);
    explicit MapWidget(MapModel *model, QWidget *parent = nullptr);
    
    MapModel *model() const;
    
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
    void initRestConnection();
    void initTimer();
    
    void drawTiles(QPainter &p);
    void requestTile(const QString &key, int x, int y);
    void showContextMenu(const QPoint &pos);
    
    MapModel *m_model = nullptr;
    bool m_ownsModel = false;
    
    RESTClient *m_rest = nullptr;
    QCache<QString, QPixmap> m_cache;
    QSet<QString> m_pending;
    
    QPoint m_posLast;
    QPointF m_panVelocity;
    QTimer *m_timerPanInertia = nullptr;
    qint64 m_timeLastInertia = 0;
    
signals:
    void signalZoomChanged(int zoom);
    void signalCoordsChanged(double lon, double lat);
};

#endif // MAP_WIDGET_H
