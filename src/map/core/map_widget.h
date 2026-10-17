#ifndef MAP_WIDGET_H
#define MAP_WIDGET_H

#include <QElapsedTimer>
#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QShowEvent>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

#include <functional>

#include "map/core/map_model.h"
#include "map/core/map_cached_terrain_altitude.h"
#include "map/core/map_zoom_animation.h"
#include "map/data/map_tile_repository.h"

#ifdef Q_OS_WASM
#include <emscripten/html5.h>

#include "gps/gps_provider_dummy.h"
#else
#include "gps/gps_provider.h"
#include "map/core/map_wayland_pointer_lock.h"

#include <QWindow>
#endif

#include "common/_enums_structs.h"

// Navigation controller of one map: turns mouse, wheel and keyboard input
// (and the zoom / pan buttons) into MapModel camera changes and reports the
// pointer coordinate and zoom level. It draws nothing; the bgfx surface
// stacked with it shows the map.
class MapWidget : public QWidget
{
    Q_OBJECT

public:
    using ScreenCoordinateResolver =
        std::function<bool(const QPointF &screen_position, CoordinateWGS84 *coordinate)>;
    using GlobeTerrainPanResolver =
        std::function<bool(const QPoint &delta_pixels)>;
    using CachedTerrainElevationResolver = MapCachedTerrainElevationLookup;

    explicit MapWidget(MapModel *model, MapTileRepository *tile_repository, GpsProvider *gps, QWidget *parent = nullptr);
    ~MapWidget() override;

    MapModel *model() const;
    void fitViewToBounds(const CoordinateWGS84 &minimum, const CoordinateWGS84 &maximum,
                         double elevation_minimum_m, double elevation_maximum_m);
    void deleteCachedTiles(int zoom, int tile_x_min, int tile_x_max, int tile_y_min, int tile_y_max);

    bool handleKeyPressEvent(QKeyEvent *event);
    bool handleKeyReleaseEvent(QKeyEvent *event);
    bool handleMousePressEvent(QMouseEvent *event);
    bool handleMouseReleaseEvent(QMouseEvent *event);
    bool handleMouseMoveEvent(QMouseEvent *event);
    void handleWheelEvent(QWheelEvent *event);

    void clearKeyboardPanInput();

    // Single navigation clock. A render surface that presents frames itself
    // (bgfx) enables the external driver and calls advanceNavigationFrame()
    // once per presented frame, *before* sampling the camera. MapWidget's own
    // 16 ms pan timer is then never started: two independent clocks (camera
    // integration timer + render timer, with a vsync-blocking swap on the
    // same thread) advance the camera by 0, 1 or 2 steps per displayed frame.
    void setNavigationFrameDriver(bool external);
    void advanceNavigationFrame(double elapsed_seconds);

#ifndef Q_OS_WASM
    // When mouse input arrives from an embedded native window (bgfx) rather
    // than from this widget, the Wayland orbit pointer lock is requested on
    // that window's own wl_surface as well as on the top-level's (which of
    // the two a compositor honours differs, see MapWaylandPointerLock). Its
    // coordinates equal this widget's (same stack geometry). nullptr: the
    // top-level window only.
    void setOrbitPointerLockWindow(QWindow *window);
#endif

    void setEdgePanningEnabled(bool enabled);
    void setRenderSurfaceScreenCoordinateResolver(ScreenCoordinateResolver resolver);
    void setRenderSurfaceGlobeTerrainPanResolver(GlobeTerrainPanResolver resolver);
    void setCachedTerrainElevationResolver(CachedTerrainElevationResolver resolver);

public slots:
    void zoomIn();
    void zoomOut();
    void panUp();
    void panDown();
    void panLeft();
    void panRight();

    void changeMapProvider(MapProvider provider);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void init();
    void initPanAnimation();
    void ensurePanAnimationRunning();
    void stopPanAnimationIfIdle();
    void stopAllPanMovement();
    void updatePanAnimation();
    void pollEdgePan();

#ifdef Q_OS_WASM
    static EM_BOOL browserMouseMoveCallback(int event_type, const EmscriptenMouseEvent *event, void *user_data);
    static EM_BOOL browserMouseLeaveCallback(int event_type, const EmscriptenMouseEvent *event, void *user_data);
#endif

    bool setKeyboardPanKey(QKeyEvent *event, bool pressed);
    bool hasKeyboardPanInput() const;
    bool hasFastKeyboardPanInput() const;
    void advanceNavigation(qreal elapsed_seconds);
    bool hasKeyboardZoomInput() const;
    QPointF viewportCenter() const;
    double zoomMotionOctaves() const;
    void applyZoomMotionOctaves(double octaves);
    bool prepareZoomMotion();
    void queueZoomImpulse(double octaves, const QPointF &anchor);
    void synchronizeKeyboardZoomHold();
    void updateSmoothZoom(qreal elapsed_seconds);
    void cancelSmoothZoom();
    QPointF keyboardPanDirection() const;
    QPointF edgePanDirection() const;
    void panMapByPixels(const QPoint &delta, bool keyboard_pan = false);
    void panByStep(const QPoint &delta);
    enum class View3dOrbitInput
    {
        None,
        MiddleMouse,
        CtrlMouse
    };

    void beginView3dOrbit(
        const QPoint &position, const QPoint &global_position, View3dOrbitInput input);
    void endView3dOrbit(bool restore_cursor_position = true);
    void applyView3dOrbitPointerDelta(const QPoint &delta);

    void updatePointerCoordinates(const QPoint &position);
    CoordinateWGS84 withCachedTerrainAltitude(const CoordinateWGS84 &wgs) const;
    void emitPointerCoordinate(const CoordinateWGS84 &wgs);

    GpsProvider *gps = nullptr;

    MapModel *m_model = nullptr;
    MapTileRepository *tile_repository = nullptr;

    bool mouse_pan_active = false;
    QPoint mouse_pan_last_position;
    QPointF mouse_pan_sensitivity_fractional_delta;
    bool view_3d_orbit_active = false;
    View3dOrbitInput view_3d_orbit_input = View3dOrbitInput::None;
    QPoint view_3d_orbit_last_position;
#ifndef Q_OS_WASM
    QPoint view_3d_orbit_last_global_position;
    QPoint view_3d_orbit_restore_global;
    QPoint view_3d_orbit_anchor_global;
    QPointF view_3d_orbit_relative_fractional_delta;
    MapWaylandPointerLock view_3d_orbit_wayland_pointer_lock;
    bool view_3d_orbit_pointer_warp_enabled = false;
    bool view_3d_orbit_mouse_grabbed = false;
    bool view_3d_orbit_cursor_hidden = false;
    QPointer<QWindow> orbit_pointer_lock_window;
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
    ScreenCoordinateResolver render_surface_screen_coordinate_resolver;
    GlobeTerrainPanResolver render_surface_globe_terrain_pan_resolver;
    CachedTerrainElevationResolver cached_terrain_elevation_resolver;
    bool zoom_in_key_pressed = false;
    bool zoom_out_key_pressed = false;
    bool keyboard_pan_motion_active = false;
    bool external_navigation_frame_driver = false;

    // The one continuous camera zoom motion for wheel, buttons and keys, in
    // every view mode and with every renderer. Octaves: the 2D continuous
    // zoom level, or -log2(globe distance). MapModel has no integer zoom;
    // imagery tile levels are derived where tiles are drawn.
    MapZoomMotion zoom_motion;
    MapViewMode zoom_motion_mode = MapViewMode::TwoD;
    QPointF zoom_motion_anchor;

    bool edge_panning_enabled = false;
    QPointF pan_velocity;
    QPointF pan_fractional_delta;
    QTimer *pan_timer = nullptr;
    QElapsedTimer pan_elapsed_timer;
    QTimer *edge_pan_poll_timer = nullptr;

#ifdef Q_OS_WASM
    QPoint browser_pointer_position;
    bool browser_pointer_inside = false;
#endif

signals:
    // The zoom level shown to the person: the continuous 2D zoom, or in
    // Globe mode the 2D-equivalent level of the camera distance (see
    // MapModel::viewGlobeZoomLevel()). Used by the footer's live zoom
    // control.
    void signalZoomLevelChanged(double zoom);
    void signalCoordsChangedWgs84(CoordinateWGS84 wgs);
    void signalCoordsChangedUTM(CoordinateUTM utm);
    void signalCoordsUnavailable();
};

#endif // MAP_WIDGET_H
