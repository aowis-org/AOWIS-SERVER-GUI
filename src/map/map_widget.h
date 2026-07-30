#ifndef MAP_WIDGET_H
#define MAP_WIDGET_H

#include <QDateTime>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPoint>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

#include "map_model.h"
#include "map_tile_repository.h"

#ifdef Q_OS_WASM
#include "../gps_provider_dummy.h"
#else
#include "../gps_provider.h"
#endif

#include "../_enums_structs.h"

class MapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MapWidget(MapTileRepository *tile_repository, GpsProvider *gps, QWidget *parent = nullptr);
    explicit MapWidget(MapModel *model, MapTileRepository *tile_repository, GpsProvider *gps, QWidget *parent = nullptr);

    MapModel *model() const;

    void addPanVelocity(int x, int y);
    void onMouseMove(QMouseEvent *event);
    void onMouseWheel(QWheelEvent *event);

public slots:
    void zoomIn();
    void zoomOut();
    void panUp();
    void panDown();
    void panLeft();
    void panRight();

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
    void initTimer();
    void drawTiles(QPainter &painter);
    void showContextMenu(const QPoint &pos);

    GpsProvider *gps = nullptr;
    CoordinateWGS84 gps_coordinate;

    MapModel *m_model = nullptr;
    MapTileRepository *tile_repository = nullptr;

    QPoint m_posLast;
    QPointF m_panVelocity;
    QTimer *m_timerPanInertia = nullptr;
    qint64 m_timeLastInertia = 0;
    int wheel_delta_accumulated = 0;

signals:
    void signalZoomChanged(int zoom);
    void signalCoordsChangedWgs84(CoordinateWGS84 wgs);
    void signalCoordsChangedUTM(CoordinateUTM utm);
};

#endif // MAP_WIDGET_H
