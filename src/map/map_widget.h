#ifndef MAP_WIDGET_H
#define MAP_WIDGET_H

#include <QElapsedTimer>
#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QShowEvent>
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
    explicit MapWidget(MapModel *model, MapTileRepository *tile_repository, GpsProvider *gps, QWidget *parent = nullptr);

    MapModel *model() const;

    bool handleKeyPressEvent(QKeyEvent *event);
    bool handleKeyReleaseEvent(QKeyEvent *event);
    bool handleMousePressEvent(QMouseEvent *event);
    bool handleMouseReleaseEvent(QMouseEvent *event);
    bool handleMouseMoveEvent(QMouseEvent *event);
    void handleWheelEvent(QWheelEvent *event);

    void clearKeyboardPanInput();
    void setEdgePanningEnabled(bool enabled);

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
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void init();
    void initPanAnimation();
    void ensurePanAnimationRunning();
    void stopPanAnimationIfIdle();
    void stopAllPanMovement();
    void updatePanAnimation();
#ifndef Q_OS_WASM
    void pollEdgePan();
#endif

    bool setKeyboardPanKey(int key, bool pressed);
    bool hasKeyboardPanInput() const;
    bool hasFastKeyboardPanInput() const;
    QPointF keyboardPanDirection() const;
    QPointF edgePanDirection() const;
    void panByStep(const QPoint &delta);

    void updatePointerCoordinates(const QPoint &position);
    void drawTiles(QPainter &painter);
    void showContextMenu(const QPoint &pos);

    GpsProvider *gps = nullptr;
    CoordinateWGS84 gps_coordinate;

    MapModel *m_model = nullptr;
    MapTileRepository *tile_repository = nullptr;

    bool mouse_pan_active = false;
    QPoint mouse_pan_last_position;
#ifndef Q_OS_WASM
    QPointF mouse_pan_velocity;
    QElapsedTimer mouse_pan_move_elapsed_timer;
    bool mouse_pan_inertia_active = false;
    int mouse_pan_drag_distance = 0;
#endif

    bool pan_key_left_pressed = false;
    bool pan_key_right_pressed = false;
    bool pan_key_up_pressed = false;
    bool pan_key_down_pressed = false;
    bool pan_fast_modifier_pressed = false;

    bool edge_panning_enabled = false;
    QPointF pan_velocity;
    QPointF pan_fractional_delta;
    QTimer *pan_timer = nullptr;
    QElapsedTimer pan_elapsed_timer;
#ifndef Q_OS_WASM
    QTimer *edge_pan_poll_timer = nullptr;
#endif

    int wheel_delta_accumulated = 0;

signals:
    void signalZoomChanged(int zoom);
    void signalCoordsChangedWgs84(CoordinateWGS84 wgs);
    void signalCoordsChangedUTM(CoordinateUTM utm);
};

#endif // MAP_WIDGET_H
