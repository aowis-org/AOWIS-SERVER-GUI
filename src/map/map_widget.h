#ifndef MAP_WIDGET_H
#define MAP_WIDGET_H

#include <QElapsedTimer>
#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
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
#include <emscripten/html5.h>

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
    ~MapWidget() override;

    MapModel *model() const;
    void deleteCachedTiles(int zoom, int tile_x_min, int tile_x_max, int tile_y_min, int tile_y_max);

    bool handleKeyPressEvent(QKeyEvent *event);
    bool handleKeyReleaseEvent(QKeyEvent *event);
    bool handleMousePressEvent(QMouseEvent *event);
    bool handleMouseReleaseEvent(QMouseEvent *event);
    bool handleMouseMoveEvent(QMouseEvent *event);
    void handleWheelEvent(QWheelEvent *event);

    void clearKeyboardPanInput();
    void setEdgePanningEnabled(bool enabled);

#ifdef Q_OS_WASM
    void setBrowserMapLayerEnabled(bool enabled);
    void setBrowserMapLayerTopmost(bool topmost);
    void setBrowserMapLayerGeometry(const QRect &geometry, bool visible);
    int browserMapLayerOwnerId() const;
#endif

public slots:
    void zoomIn();
    void zoomOut();
    void setRhiViewActive(bool active);
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
    void pollEdgePan();
    bool browserMapHandlesMouseInertia() const;
    void beginBrowserMapMousePan();
    void updateBrowserMapMousePan(const QPoint &delta, qint64 elapsed_ms);
    bool releaseBrowserMapMousePan();
    bool browserMapInertiaActive() const;
    bool updateBrowserMapInertia();
    void cancelBrowserMapMousePan();

#ifdef Q_OS_WASM
    static EM_BOOL browserMouseMoveCallback(int event_type, const EmscriptenMouseEvent *event, void *user_data);
    static EM_BOOL browserMouseLeaveCallback(int event_type, const EmscriptenMouseEvent *event, void *user_data);
    void syncBrowserMapView();
#endif

    bool setKeyboardPanKey(QKeyEvent *event, bool pressed);
    bool hasKeyboardPanInput() const;
    bool hasFastKeyboardPanInput() const;
    bool hasView2dKeyboardZoomInput() const;
    bool hasView3dKeyboardZoomInput() const;
    void updateView2dKeyboardZoom(qreal elapsed_seconds);
    void updateView3dKeyboardZoom(qreal elapsed_seconds);
    void beginView3dKeyboardZoomInteraction();
    void endView3dKeyboardZoomInteraction();
    QPointF keyboardPanDirection() const;
    QPointF edgePanDirection() const;
    void panMapByPixels(const QPoint &delta, bool angle_independent_3d = false);
    void panByStep(const QPoint &delta);
    enum class View3dOrbitInput
    {
        None,
        MiddleMouse,
        CtrlMouse
    };

    void beginView3dOrbit(const QMouseEvent *event, View3dOrbitInput input);
    void endView3dOrbit(bool restore_cursor_position = true);

    void updatePointerCoordinates(const QPoint &position);
    void scheduleTileUpdate(const QString &);
    void drawTiles(QPainter &painter);

    GpsProvider *gps = nullptr;
    CoordinateWGS84 gps_coordinate;
    bool has_gps_coordinate = false;

    MapModel *m_model = nullptr;
    MapTileRepository *tile_repository = nullptr;

    bool mouse_pan_active = false;
    QPoint mouse_pan_last_position;
    bool view_3d_orbit_active = false;
    View3dOrbitInput view_3d_orbit_input = View3dOrbitInput::None;
    QPoint view_3d_orbit_last_position;
#ifndef Q_OS_WASM
    QPoint view_3d_orbit_anchor_global;
    QPoint view_3d_orbit_restore_global;
    bool view_3d_orbit_mouse_grabbed = false;
#endif
    QPointF mouse_pan_velocity;
    QElapsedTimer mouse_pan_move_elapsed_timer;
    bool mouse_pan_inertia_active = false;
    int mouse_pan_drag_distance = 0;

    bool pan_key_left_pressed = false;
    bool pan_key_right_pressed = false;
    bool pan_key_up_pressed = false;
    bool pan_key_down_pressed = false;
    bool pan_fast_modifier_pressed = false;
    bool rhi_view_active = false;
    bool view_2d_zoom_in_key_pressed = false;
    bool view_2d_zoom_out_key_pressed = false;
    bool view_3d_zoom_in_key_pressed = false;
    bool view_3d_zoom_out_key_pressed = false;
    bool view_3d_keyboard_zoom_interaction_active = false;
    bool keyboard_pan_motion_active = false;

    bool edge_panning_enabled = false;
    QPointF pan_velocity;
    QPointF pan_fractional_delta;
    QTimer *pan_timer = nullptr;
    QElapsedTimer pan_elapsed_timer;
    QTimer *edge_pan_poll_timer = nullptr;
    QTimer *tile_update_timer = nullptr;

#ifdef Q_OS_WASM
    QPoint browser_pointer_position;
    bool browser_pointer_inside = false;
    bool browser_map_layer_enabled = false;
    bool browser_map_layer_topmost = false;
    int browser_map_layer_owner_id = 0;
#endif
    bool backing_store_pan_active = false;

    int wheel_delta_accumulated = 0;

signals:
    void signalZoomChanged(int zoom);
    void signalCoordsChangedWgs84(CoordinateWGS84 wgs);
    void signalCoordsChangedUTM(CoordinateUTM utm);
};

#endif // MAP_WIDGET_H
