#include "map/core/map_widget.h"
#include "config/gui_configuration.h"
#include "config/shortcut_registry.h"

#include <QApplication>
#include <QFile>
#include <QPaintEvent>
#include <QPalette>

#ifndef Q_OS_WASM
#include <QCursor>
#include <QGeoCoordinate>
#include <QScreen>
#include <QWindow>
#else
#include <emscripten.h>
#endif

#include <cmath>
#include <utility>

#ifdef Q_OS_WASM
EM_JS(int, aowisBrowserViewportWidth, (),
{
    return window.innerWidth | 0;
});

EM_JS(int, aowisBrowserViewportHeight, (),
{
    return window.innerHeight | 0;
});

EM_JS(int, aowisBrowserDocumentActive, (),
{
    return document.hasFocus() && !document.hidden ? 1 : 0;
});

EM_JS(void, aowisBrowserMapSetGeometry, (int owner_id, int x, int y, int width, int height, int visible, int topmost),
{
    if (window.aowisBrowserMap)
        window.aowisBrowserMap.setGeometry(owner_id, x, y, width, height, visible !== 0, topmost !== 0);
});

EM_JS(void, aowisBrowserMapSetView, (int owner_id, double longitude, double latitude, int zoom, int provider),
{
    if (window.aowisBrowserMap)
        window.aowisBrowserMap.setView(owner_id, longitude, latitude, zoom, provider);
});

EM_JS(void, aowisBrowserMapInvalidateTiles, (),
{
    if (window.aowisBrowserMap)
        window.aowisBrowserMap.invalidateTiles();
});

EM_JS(void, aowisBrowserMapRelease, (int owner_id),
{
    if (window.aowisBrowserMap)
        window.aowisBrowserMap.release(owner_id);
});

EM_JS(void, aowisBrowserMapSetCrosshairImage, (const char *data_url),
{
    if (window.aowisBrowserMap &&
        typeof window.aowisBrowserMap.setCrosshairImage === "function")
    {
        window.aowisBrowserMap.setCrosshairImage(UTF8ToString(data_url));
    }
});

EM_JS(void, aowisBrowserMapMousePanBegin, (int owner_id),
{
    if (window.aowisBrowserMap && typeof window.aowisBrowserMap.mousePanBegin === "function")
        window.aowisBrowserMap.mousePanBegin(owner_id);
});

EM_JS(void, aowisBrowserMapMousePanMove, (int owner_id, int delta_x, int delta_y, double elapsed_ms),
{
    if (window.aowisBrowserMap && typeof window.aowisBrowserMap.mousePanMove === "function")
        window.aowisBrowserMap.mousePanMove(owner_id, delta_x, delta_y, elapsed_ms);
});

EM_JS(int, aowisBrowserMapMousePanRelease, (int owner_id, int start_drag_distance),
{
    if (!window.aowisBrowserMap || typeof window.aowisBrowserMap.mousePanRelease !== "function")
        return 0;

    return window.aowisBrowserMap.mousePanRelease(owner_id, start_drag_distance) ? 1 : 0;
});

EM_JS(int, aowisBrowserMapInertiaActive, (int owner_id),
{
    if (!window.aowisBrowserMap || typeof window.aowisBrowserMap.inertiaActive !== "function")
        return 0;

    return window.aowisBrowserMap.inertiaActive(owner_id) ? 1 : 0;
});

EM_JS(int, aowisBrowserMapTakeInertiaDelta, (int owner_id, int *delta_x, int *delta_y),
{
    let x = 0;
    let y = 0;
    let active = false;

    if (window.aowisBrowserMap && typeof window.aowisBrowserMap.takeInertiaDelta === "function")
    {
        const movement = window.aowisBrowserMap.takeInertiaDelta(owner_id);
        if (movement)
        {
            x = movement.x | 0;
            y = movement.y | 0;
            active = Boolean(movement.active);
        }
    }

    HEAP32[delta_x >> 2] = x;
    HEAP32[delta_y >> 2] = y;
    return active ? 1 : 0;
});

EM_JS(void, aowisBrowserMapMousePanCancel, (int owner_id),
{
    if (window.aowisBrowserMap && typeof window.aowisBrowserMap.mousePanCancel === "function")
        window.aowisBrowserMap.mousePanCancel(owner_id);
});
#endif

namespace
{
#ifdef Q_OS_WASM
int nextBrowserMapLayerOwnerId()
{
    static int next_owner_id = 1;
    return next_owner_id++;
}

QByteArray browserCrosshairDataUrl()
{
    static const QByteArray data_url = []
    {
        QFile file(QStringLiteral(":/icon/crosshair.png"));
        if (!file.open(QIODevice::ReadOnly))
            return QByteArray();

        return QByteArrayLiteral("data:image/png;base64,") + file.readAll().toBase64();
    }();
    return data_url;
}
#endif

constexpr int PanFrameIntervalMs = 16;
constexpr int TileUpdateIntervalMs = 16;
constexpr int PanButtonStepPixels = 120;
constexpr int EdgePanMarginPixels = 2;
constexpr int EdgePanPollIntervalMs = 50;
constexpr double PanMaximumSpeedPixelsPerSecond = 900.0;
constexpr double PanFastSpeedMultiplier = 2.0;
constexpr double PanFastAccelerationMultiplier = 1.75;
constexpr double PanAccelerationPixelsPerSecondSquared = 2800.0;
constexpr double PanDecelerationPixelsPerSecondSquared = 3600.0;
constexpr double PanVelocityStopThreshold = 0.5;
constexpr int MousePanReleaseTimeoutMs = 100;
constexpr double MousePanVelocitySmoothing = 0.65;
constexpr double MousePanMaximumSpeedPixelsPerSecond = 2400.0;
constexpr double MousePanMinimumInertiaSpeedPixelsPerSecond = 70.0;
constexpr double MousePanInertiaDecelerationPixelsPerSecondSquared = 2600.0;
constexpr double View2dKeyboardZoomOctavesPerSecond = 1.0;
constexpr double View3dKeyboardZoomOctavesPerSecond = 1.0;
constexpr double View3dKeyboardZoomFastMultiplier = 2.0;

qreal vectorLength(const QPointF &vector)
{
    return std::hypot(vector.x(), vector.y());
}

QPointF normalized(const QPointF &vector)
{
    const qreal length = vectorLength(vector);
    if (qFuzzyIsNull(length))
        return QPointF();

    return vector / length;
}

QPointF moveTowards(const QPointF &current, const QPointF &target, qreal maximum_delta)
{
    const QPointF delta = target - current;
    const qreal distance = vectorLength(delta);

    if (distance <= maximum_delta || qFuzzyIsNull(distance))
        return target;

    return current + delta / distance * maximum_delta;
}
}

MapWidget::MapWidget(MapModel *model, MapTileRepository *tile_repository, GpsProvider *gps, QWidget *parent)
    : QWidget(parent),
    gps(gps),
    m_model(model),
    tile_repository(tile_repository)
{
    Q_ASSERT(this->m_model);
    Q_ASSERT(this->tile_repository);

#ifdef Q_OS_WASM
    this->browser_map_layer_owner_id = nextBrowserMapLayerOwnerId();
#endif

    this->init();
}

MapWidget::~MapWidget()
{
#ifndef Q_OS_WASM
    endView3dOrbit(false);
#endif
#ifdef Q_OS_WASM
    if (this->browser_map_layer_enabled)
        aowisBrowserMapRelease(this->browser_map_layer_owner_id);

    emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_TRUE, nullptr);
    emscripten_set_mouseleave_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_TRUE, nullptr);
#endif
}

MapModel *MapWidget::model() const
{
    return this->m_model;
}

void MapWidget::fitViewToBounds(
    const CoordinateWGS84 &minimum, const CoordinateWGS84 &maximum,
    double elevation_minimum_m, double elevation_maximum_m)
{
    if (this->m_model == nullptr)
        return;

    this->m_model->fitViewToBounds(
        minimum, maximum, size(), elevation_minimum_m, elevation_maximum_m,
        this->rhi_view_active);
}

void MapWidget::deleteCachedTiles(int zoom, int tile_x_min, int tile_x_max, int tile_y_min, int tile_y_max)
{
    const int bounded_zoom = qBound(MapModel::MinZoom, zoom, MapModel::MaxZoom);
    this->tile_repository->deleteTiles(
        this->m_model->tileSourcePath(bounded_zoom),
        bounded_zoom,
        tile_x_min,
        tile_x_max,
        tile_y_min,
        tile_y_max);
}

void MapWidget::init()
{
    this->setAttribute(Qt::WA_OpaquePaintEvent);
    this->setAttribute(Qt::WA_NoSystemBackground);

    this->setMinimumHeight(500);
    this->setMinimumWidth(550);
    this->setContentsMargins(0, 0, 0, 0);

    this->setFocusPolicy(Qt::StrongFocus);
    this->setFocus();
    this->setMouseTracking(true);

    connect(this->m_model, &MapModel::zoomChanged, this, [this](int zoom)
    {
        emit signalZoomChanged(zoom);
        emit signalZoomLevelChanged(double(zoom));
#ifdef Q_OS_WASM
        if (this->browser_map_layer_enabled)
        {
            this->syncBrowserMapView();
            return;
        }
#endif
        update();
    });

    connect(this->m_model, &MapModel::viewGlobeCameraChanged, this, [this]
    {
        if (this->m_model->viewMode() == MapViewMode::Globe)
            emit signalZoomLevelChanged(this->m_model->viewGlobeZoomLevel(size()));
    });

    connect(this->m_model, &MapModel::centerChangedWGS84, this, [this](CoordinateWGS84 wgs)
    {
        if (this->m_model->viewMode() == MapViewMode::Globe)
            emit signalZoomLevelChanged(this->m_model->viewGlobeZoomLevel(size()));
        if (this->rhi_view_active
            && this->m_model->viewMode() == MapViewMode::ThreeD
            && this->rhi_screen_coordinate_resolver)
        {
            updatePointerCoordinates(
                QPoint(width() / 2, height() / 2));
        }
        else
        {
            emit signalCoordsChangedWgs84(wgs);
        }
        if (this->backing_store_pan_active)
            return;
#ifdef Q_OS_WASM
        if (this->browser_map_layer_enabled)
        {
            this->syncBrowserMapView();
            return;
        }
#endif
        update();
    });

    connect(this->m_model, &MapModel::centerChangedUTM, this,
            [this](CoordinateUTM utm)
    {
        if (this->rhi_view_active
            && this->m_model->viewMode() == MapViewMode::ThreeD
            && this->rhi_screen_coordinate_resolver)
        {
            return;
        }
        emit signalCoordsChangedUTM(utm);
    });
    connect(this->m_model, &MapModel::providerChanged, this, [this](MapProvider)
    {
#ifdef Q_OS_WASM
        if (this->browser_map_layer_enabled)
        {
            this->syncBrowserMapView();
            return;
        }
#endif
        update();
    });

    connect(this->m_model, &MapModel::viewModeChanged, this, [this](MapViewMode view_mode)
    {
        if (view_mode != MapViewMode::TwoD)
        {
            this->view_2d_zoom_in_key_pressed = false;
            this->view_2d_zoom_out_key_pressed = false;
        }
        if (view_mode != MapViewMode::ThreeD)
        {
            this->view_3d_zoom_in_key_pressed = false;
            this->view_3d_zoom_out_key_pressed = false;
            this->view_3d_keyboard_zoom_interaction_active = false;
            if (this->view_3d_orbit_active)
                endView3dOrbit();
            stopPanAnimationIfIdle();
        }

        if (this->rhi_view_active && this->rhi_screen_coordinate_resolver)
        {
            updatePointerCoordinates(
                QPoint(width() / 2, height() / 2));
        }

        if (view_mode == MapViewMode::Globe)
            emit signalZoomLevelChanged(this->m_model->viewGlobeZoomLevel(size()));
        else
            emit signalZoomLevelChanged(double(this->m_model->zoom()));
    });

    this->tile_update_timer = new QTimer(this);
    this->tile_update_timer->setSingleShot(true);
    this->tile_update_timer->setTimerType(Qt::PreciseTimer);
    this->tile_update_timer->setInterval(TileUpdateIntervalMs);
    connect(this->tile_update_timer, &QTimer::timeout, this, [this]
    {
#ifdef Q_OS_WASM
        if (this->browser_map_layer_enabled)
            return;
#endif
        update();
    });

    connect(this->tile_repository, &MapTileRepository::signalTileAvailable,
            this, &MapWidget::scheduleTileUpdate);
    connect(this->tile_repository, &MapTileRepository::signalTileRetryReady,
            this, &MapWidget::scheduleTileUpdate);
    connect(this->tile_repository, &MapTileRepository::signalTilesDeleted, this, [this]
    {
#ifdef Q_OS_WASM
        if (this->browser_map_layer_enabled)
            aowisBrowserMapInvalidateTiles();
#endif
    });

    if (this->gps)
    {
        connect(this->gps, &GpsProvider::positionChanged, this, [this](const QGeoPositionInfo &info)
        {
#ifndef Q_OS_WASM
            const QGeoCoordinate coordinate = info.coordinate();
            if (!info.isValid() || !coordinate.isValid())
            {
                this->has_gps_coordinate = false;
                update();
                return;
            }

            this->gps_coordinate.latitude_deg = coordinate.latitude();
            this->gps_coordinate.longitude_deg = coordinate.longitude();
            this->gps_coordinate.altitude_m = coordinate.altitude();
            this->has_gps_coordinate = true;
            update();
#else
            Q_UNUSED(info)
#endif
        });
        connect(this->gps, &GpsProvider::gpsDisconnected, this, [this]
        {
            this->has_gps_coordinate = false;
            update();
        });
        connect(this->gps, &GpsProvider::statusMessage, this, [](const QString &message)
        {
            qDebug() << message;
        });
    }

    QTimer::singleShot(0, this, [this]
    {
        emit signalZoomChanged(this->m_model->zoom());

        CoordinateWGS84 wgs;
        wgs.latitude_deg = this->m_model->centerLat();
        wgs.longitude_deg = this->m_model->centerLon();
        emit signalCoordsChangedWgs84(wgs);

        GeoMetricProjection projection;
        const CoordinateUTM utm = projection.wgs84ToUtm(wgs);
        emit signalCoordsChangedUTM(utm);
    });

    this->initPanAnimation();
}

void MapWidget::initPanAnimation()
{
    this->pan_timer = new QTimer(this);
    this->pan_timer->setInterval(PanFrameIntervalMs);
    this->pan_timer->setTimerType(Qt::PreciseTimer);
    connect(this->pan_timer, &QTimer::timeout, this, &MapWidget::updatePanAnimation);

    this->edge_pan_poll_timer = new QTimer(this);
    this->edge_pan_poll_timer->setInterval(EdgePanPollIntervalMs);
    connect(this->edge_pan_poll_timer, &QTimer::timeout, this, &MapWidget::pollEdgePan);

#ifdef Q_OS_WASM
    emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_TRUE, &MapWidget::browserMouseMoveCallback);
    emscripten_set_mouseleave_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_TRUE, &MapWidget::browserMouseLeaveCallback);
#endif
}

void MapWidget::ensurePanAnimationRunning()
{
    if (this->pan_timer->isActive())
        return;

    this->pan_elapsed_timer.start();
    this->pan_timer->start();
}

void MapWidget::stopPanAnimationIfIdle()
{
    if (hasKeyboardPanInput() || hasView2dKeyboardZoomInput()
        || hasView3dKeyboardZoomInput()
        || vectorLength(this->pan_velocity) >= PanVelocityStopThreshold
        || browserMapInertiaActive())
    {
        return;
    }

    this->pan_velocity = QPointF();
    this->pan_fractional_delta = QPointF();
    this->pan_timer->stop();
}

void MapWidget::stopAllPanMovement()
{
    endView3dKeyboardZoomInteraction();
    this->pan_key_left_pressed = false;
    this->pan_key_right_pressed = false;
    this->pan_key_up_pressed = false;
    this->pan_key_down_pressed = false;
    this->pan_fast_modifier_pressed = false;
    this->view_2d_zoom_in_key_pressed = false;
    this->view_2d_zoom_out_key_pressed = false;
    this->view_3d_zoom_in_key_pressed = false;
    this->view_3d_zoom_out_key_pressed = false;
    this->keyboard_pan_motion_active = false;
    this->mouse_pan_active = false;
    this->pan_velocity = QPointF();
    this->pan_fractional_delta = QPointF();
    this->mouse_pan_velocity = QPointF();
    this->mouse_pan_inertia_active = false;
    this->mouse_pan_drag_distance = 0;
    cancelBrowserMapMousePan();

    this->pan_timer->stop();
}

void MapWidget::updatePanAnimation()
{
    const qint64 elapsed_ms = this->pan_elapsed_timer.restart();
    const qreal elapsed_seconds = qBound<qreal>(0.0, elapsed_ms / 1000.0, 0.05);

    if (elapsed_seconds <= 0.0)
        return;

    if (!isVisible())
    {
        stopAllPanMovement();
        return;
    }

    updateView2dKeyboardZoom(elapsed_seconds);
    updateView3dKeyboardZoom(elapsed_seconds);

    if (this->mouse_pan_active)
    {
        this->pan_velocity = QPointF();
        this->pan_fractional_delta = QPointF();
        return;
    }

    QPointF direction = keyboardPanDirection();
    const bool keyboard_pan_active = !direction.isNull();
    bool edge_pan_active = false;
    if (keyboard_pan_active)
    {
        this->keyboard_pan_motion_active = true;
    }
    else
    {
        direction = edgePanDirection();
        edge_pan_active = !direction.isNull();
        if (edge_pan_active)
            this->keyboard_pan_motion_active = false;
    }
    direction = normalized(direction);

    if (!direction.isNull())
    {
        this->mouse_pan_inertia_active = false;
        cancelBrowserMapMousePan();
    }

    bool fast_pan_active = keyboard_pan_active && hasFastKeyboardPanInput();
#ifndef Q_OS_WASM
    if (edge_pan_active && QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))
        fast_pan_active = true;
#endif

    if (direction.isNull() && browserMapHandlesMouseInertia() && browserMapInertiaActive())
    {
        this->pan_velocity = QPointF();
        this->pan_fractional_delta = QPointF();
        updateBrowserMapInertia();
        stopPanAnimationIfIdle();
        return;
    }

    qreal maximum_speed = PanMaximumSpeedPixelsPerSecond;
    qreal acceleration = PanAccelerationPixelsPerSecondSquared;
    if (fast_pan_active)
    {
        maximum_speed *= PanFastSpeedMultiplier;
        acceleration *= PanFastAccelerationMultiplier;
    }

    if (direction.isNull() && this->mouse_pan_inertia_active)
        acceleration = MousePanInertiaDecelerationPixelsPerSecondSquared;
    else if (direction.isNull())
        acceleration = PanDecelerationPixelsPerSecondSquared;

    const QPointF target_velocity = direction * maximum_speed;
    this->pan_velocity = moveTowards(this->pan_velocity, target_velocity, acceleration * elapsed_seconds);

    if (direction.isNull() && vectorLength(this->pan_velocity) < PanVelocityStopThreshold)
    {
        this->pan_velocity = QPointF();
        this->mouse_pan_inertia_active = false;
        this->keyboard_pan_motion_active = false;
    }

    const QPointF precise_delta = this->pan_velocity * elapsed_seconds + this->pan_fractional_delta;
    const QPoint delta(int(std::trunc(precise_delta.x())), int(std::trunc(precise_delta.y())));
    this->pan_fractional_delta = precise_delta - QPointF(delta);

    if (!delta.isNull())
        panMapByPixels(delta, this->keyboard_pan_motion_active);

    stopPanAnimationIfIdle();
}

void MapWidget::pollEdgePan()
{
    if (!this->edgePanDirection().isNull())
        this->ensurePanAnimationRunning();
}

bool MapWidget::browserMapHandlesMouseInertia() const
{
#ifdef Q_OS_WASM
    return this->browser_map_layer_enabled;
#else
    return false;
#endif
}

void MapWidget::beginBrowserMapMousePan()
{
#ifdef Q_OS_WASM
    if (this->browser_map_layer_enabled)
        aowisBrowserMapMousePanBegin(this->browser_map_layer_owner_id);
#endif
}

void MapWidget::updateBrowserMapMousePan(const QPoint &delta, qint64 elapsed_ms)
{
#ifdef Q_OS_WASM
    if (this->browser_map_layer_enabled)
        aowisBrowserMapMousePanMove(this->browser_map_layer_owner_id, delta.x(), delta.y(), elapsed_ms);
#else
    Q_UNUSED(delta)
    Q_UNUSED(elapsed_ms)
#endif
}

bool MapWidget::releaseBrowserMapMousePan()
{
#ifdef Q_OS_WASM
    if (!this->browser_map_layer_enabled)
        return false;

    return aowisBrowserMapMousePanRelease(
        this->browser_map_layer_owner_id,
        QApplication::startDragDistance()) != 0;
#else
    return false;
#endif
}

bool MapWidget::browserMapInertiaActive() const
{
#ifdef Q_OS_WASM
    return this->browser_map_layer_enabled &&
        aowisBrowserMapInertiaActive(this->browser_map_layer_owner_id) != 0;
#else
    return false;
#endif
}

bool MapWidget::updateBrowserMapInertia()
{
#ifdef Q_OS_WASM
    if (!this->browser_map_layer_enabled)
        return false;

    int delta_x = 0;
    int delta_y = 0;
    const bool active = aowisBrowserMapTakeInertiaDelta(
        this->browser_map_layer_owner_id, &delta_x, &delta_y) != 0;
    const QPoint delta(delta_x, delta_y);
    if (!delta.isNull())
        panMapByPixels(delta);

    return active;
#else
    return false;
#endif
}

void MapWidget::cancelBrowserMapMousePan()
{
#ifdef Q_OS_WASM
    if (this->browser_map_layer_enabled)
        aowisBrowserMapMousePanCancel(this->browser_map_layer_owner_id);
#endif
}

#ifdef Q_OS_WASM
EM_BOOL MapWidget::browserMouseMoveCallback(int event_type, const EmscriptenMouseEvent *event, void *user_data)
{
    Q_UNUSED(event_type)

    MapWidget *map_widget = static_cast<MapWidget *>(user_data);
    if (map_widget == nullptr || event == nullptr)
        return EM_FALSE;

    map_widget->browser_pointer_position = QPoint(event->clientX, event->clientY);
    map_widget->browser_pointer_inside = true;

    if (map_widget->edge_panning_enabled && map_widget->isVisible() && !map_widget->edgePanDirection().isNull())
        map_widget->ensurePanAnimationRunning();

    return EM_FALSE;
}

EM_BOOL MapWidget::browserMouseLeaveCallback(int event_type, const EmscriptenMouseEvent *event, void *user_data)
{
    Q_UNUSED(event_type)
    Q_UNUSED(event)

    MapWidget *map_widget = static_cast<MapWidget *>(user_data);
    if (map_widget == nullptr)
        return EM_FALSE;

    map_widget->browser_pointer_inside = false;
    return EM_FALSE;
}
#endif

bool MapWidget::setKeyboardPanKey(QKeyEvent *event, bool pressed)
{
    if (event == nullptr)
        return false;

    const Qt::KeyboardModifiers arrow_modifiers =
        event->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);

    if ((event->key() == Qt::Key_Left && arrow_modifiers == Qt::NoModifier)
        || guiShortcutMatches(event, guiShortcutRegistry().shortcut(GuiShortcutId::MapPanLeft), Qt::ShiftModifier))
    {
        this->pan_key_left_pressed = pressed;
        return true;
    }

    if ((event->key() == Qt::Key_Right && arrow_modifiers == Qt::NoModifier)
        || guiShortcutMatches(event, guiShortcutRegistry().shortcut(GuiShortcutId::MapPanRight), Qt::ShiftModifier))
    {
        this->pan_key_right_pressed = pressed;
        return true;
    }

    if ((event->key() == Qt::Key_Up && arrow_modifiers == Qt::NoModifier)
        || guiShortcutMatches(event, guiShortcutRegistry().shortcut(GuiShortcutId::MapPanUp), Qt::ShiftModifier))
    {
        this->pan_key_up_pressed = pressed;
        return true;
    }

    if ((event->key() == Qt::Key_Down && arrow_modifiers == Qt::NoModifier)
        || guiShortcutMatches(event, guiShortcutRegistry().shortcut(GuiShortcutId::MapPanDown), Qt::ShiftModifier))
    {
        this->pan_key_down_pressed = pressed;
        return true;
    }

    return false;
}

bool MapWidget::hasKeyboardPanInput() const
{
    return this->pan_key_left_pressed || this->pan_key_right_pressed || this->pan_key_up_pressed || this->pan_key_down_pressed;
}

bool MapWidget::hasFastKeyboardPanInput() const
{
    return this->pan_fast_modifier_pressed && this->hasKeyboardPanInput();
}

bool MapWidget::hasView2dKeyboardZoomInput() const
{
    return this->rhi_view_active
        && this->m_model != nullptr
        && this->m_model->viewMode() == MapViewMode::TwoD
        && (this->view_2d_zoom_in_key_pressed || this->view_2d_zoom_out_key_pressed);
}

void MapWidget::updateView2dKeyboardZoom(qreal elapsed_seconds)
{
    if (!hasView2dKeyboardZoomInput() || elapsed_seconds <= 0.0)
        return;

    int direction = 0;
    if (this->view_2d_zoom_in_key_pressed)
        ++direction;
    if (this->view_2d_zoom_out_key_pressed)
        --direction;
    if (direction == 0)
        return;

    const double current_zoom = this->m_model->view2dContinuousZoom();
    const double next_zoom = current_zoom
        + double(direction) * View2dKeyboardZoomOctavesPerSecond
            * double(elapsed_seconds);
    this->m_model->setView2dContinuousZoom(next_zoom, size());
}

bool MapWidget::hasView3dKeyboardZoomInput() const
{
    return this->m_model != nullptr
        && (this->m_model->viewMode() == MapViewMode::ThreeD
            || this->m_model->viewMode() == MapViewMode::Globe)
        && (this->view_3d_zoom_in_key_pressed || this->view_3d_zoom_out_key_pressed);
}

void MapWidget::updateView3dKeyboardZoom(qreal elapsed_seconds)
{
    if (!hasView3dKeyboardZoomInput() || elapsed_seconds <= 0.0)
        return;

    int direction = 0;
    if (this->view_3d_zoom_in_key_pressed)
        ++direction;
    if (this->view_3d_zoom_out_key_pressed)
        --direction;
    if (direction == 0)
        return;

    double octaves_per_second = View3dKeyboardZoomOctavesPerSecond;
    if (this->pan_fast_modifier_pressed)
        octaves_per_second *= View3dKeyboardZoomFastMultiplier;

    if (this->m_model->viewMode() == MapViewMode::Globe)
    {
        const double current_distance_m = qMax(
            MapModel::MinViewGlobeDistanceM, this->m_model->viewGlobeDistanceM());
        const double distance_scale = std::exp2(
            -double(direction) * octaves_per_second * double(elapsed_seconds));
        this->m_model->setViewGlobeDistanceM(current_distance_m * distance_scale);
        return;
    }

    const double current_distance_m = qMax(
        MapModel::MinView3dCameraDistanceM,
        this->m_model->view3dCameraDistanceM());
    const double distance_scale = std::exp2(
        -double(direction) * octaves_per_second * double(elapsed_seconds));
    const double next_distance_m = qMax(
        MapModel::MinView3dCameraDistanceM,
        current_distance_m * distance_scale);

    const double native_distance_m = qMax(
        MapModel::MinView3dCameraDistanceM,
        this->m_model->view3dNativeCameraDistanceM());
    const double continuous_zoom = double(this->m_model->zoom())
        + std::log2(native_distance_m / next_distance_m);
    const int tile_zoom = qBound(
        MapModel::MinZoom,
        qRound(continuous_zoom),
        MapModel::MaxZoom);

    if (tile_zoom != this->m_model->zoom())
        this->m_model->setView3dTileZoomPreservingCameraDistance(tile_zoom, size());

    this->m_model->setView3dContinuousCameraDistanceM(next_distance_m);
}

void MapWidget::beginView3dKeyboardZoomInteraction()
{
    if (this->view_3d_keyboard_zoom_interaction_active
        || this->m_model == nullptr
        || this->m_model->viewMode() != MapViewMode::ThreeD)
    {
        return;
    }

    this->view_3d_keyboard_zoom_interaction_active = true;
    this->m_model->beginView3dRotateInteraction();
}

void MapWidget::endView3dKeyboardZoomInteraction()
{
    if (!this->view_3d_keyboard_zoom_interaction_active)
        return;

    this->view_3d_keyboard_zoom_interaction_active = false;
    if (this->m_model != nullptr)
        this->m_model->endView3dRotateInteraction();
}

QPointF MapWidget::keyboardPanDirection() const
{
    QPointF direction;

    if (this->pan_key_left_pressed)
        direction.rx() += 1.0;
    if (this->pan_key_right_pressed)
        direction.rx() -= 1.0;
    if (this->pan_key_up_pressed)
        direction.ry() += 1.0;
    if (this->pan_key_down_pressed)
        direction.ry() -= 1.0;

    return direction;
}

QPointF MapWidget::edgePanDirection() const
{
    if (!this->edge_panning_enabled || !this->isVisible())
        return QPointF();

    if (QApplication::activePopupWidget() || QApplication::activeModalWidget())
        return QPointF();

#ifndef Q_OS_WASM
    if (!this->window()->isFullScreen() || !this->window()->isActiveWindow())
        return QPointF();

    QWindow *window_handle = this->window()->windowHandle();
    if (!window_handle || !window_handle->screen())
        return QPointF();

    const QRect screen_geometry = window_handle->screen()->geometry();
    const QPoint cursor_position = QCursor::pos();
    if (!screen_geometry.contains(cursor_position))
        return QPointF();

    QPointF direction;

    if (cursor_position.x() <= screen_geometry.left() + EdgePanMarginPixels)
        direction.rx() += 1.0;
    else if (cursor_position.x() >= screen_geometry.right() - EdgePanMarginPixels)
        direction.rx() -= 1.0;

    if (cursor_position.y() <= screen_geometry.top() + EdgePanMarginPixels)
        direction.ry() += 1.0;
    else if (cursor_position.y() >= screen_geometry.bottom() - EdgePanMarginPixels)
        direction.ry() -= 1.0;

    return direction;
#else
    if (!this->browser_pointer_inside || aowisBrowserDocumentActive() == 0)
        return QPointF();

    const int viewport_width = aowisBrowserViewportWidth();
    const int viewport_height = aowisBrowserViewportHeight();
    if (viewport_width <= 0 || viewport_height <= 0)
        return QPointF();

    const int pointer_x = this->browser_pointer_position.x();
    const int pointer_y = this->browser_pointer_position.y();
    if (pointer_x < 0 || pointer_y < 0 || pointer_x >= viewport_width || pointer_y >= viewport_height)
        return QPointF();

    QPointF direction;

    if (pointer_x <= EdgePanMarginPixels)
        direction.rx() += 1.0;
    else if (pointer_x >= viewport_width - EdgePanMarginPixels - 1)
        direction.rx() -= 1.0;

    if (pointer_y <= EdgePanMarginPixels)
        direction.ry() += 1.0;
    else if (pointer_y >= viewport_height - EdgePanMarginPixels - 1)
        direction.ry() -= 1.0;

    return direction;
#endif
}

bool MapWidget::handleKeyPressEvent(QKeyEvent *event)
{
    if (!event)
        return false;

#ifndef Q_OS_WASM
    if (event->key() == Qt::Key_Control
        && !event->isAutoRepeat()
        && this->m_model->viewMode() == MapViewMode::ThreeD
        && !this->view_3d_orbit_active
        && QApplication::mouseButtons() == Qt::NoButton)
    {
        const QPoint global_position = QCursor::pos();
        const QPoint local_position = mapFromGlobal(global_position);
        if (rect().contains(local_position))
        {
            beginView3dOrbit(
                local_position, global_position, View3dOrbitInput::CtrlMouse);
            this->pan_velocity = QPointF();
            this->mouse_pan_inertia_active = false;
            stopPanAnimationIfIdle();
            event->accept();
            return true;
        }
    }
#endif

    if (event->key() == Qt::Key_Shift)
    {
        this->pan_fast_modifier_pressed = true;
        this->view_2d_zoom_in_key_pressed = false;
        this->view_2d_zoom_out_key_pressed = false;
        if (this->hasKeyboardPanInput())
            this->ensurePanAnimationRunning();

        event->accept();
        return true;
    }

    if (this->setKeyboardPanKey(event, true))
    {
        this->pan_fast_modifier_pressed = event->modifiers().testFlag(Qt::ShiftModifier);
        this->mouse_pan_inertia_active = false;
        cancelBrowserMapMousePan();
        this->ensurePanAnimationRunning();
        event->accept();
        return true;
    }

    const bool zoom_in = guiShortcutMatches(event, guiShortcutRegistry().shortcut(GuiShortcutId::MapZoomIn), Qt::ShiftModifier);
    const bool zoom_out = guiShortcutMatches(event, guiShortcutRegistry().shortcut(GuiShortcutId::MapZoomOut), Qt::ShiftModifier);
    if (zoom_in || zoom_out)
    {
        if (this->m_model->viewMode() == MapViewMode::ThreeD
            || this->m_model->viewMode() == MapViewMode::Globe)
        {
            if (!event->isAutoRepeat())
            {
                if (zoom_in)
                    this->view_3d_zoom_in_key_pressed = true;
                else
                    this->view_3d_zoom_out_key_pressed = true;
                beginView3dKeyboardZoomInteraction();
            }
            this->pan_fast_modifier_pressed = event->modifiers().testFlag(Qt::ShiftModifier);
            ensurePanAnimationRunning();
        }
        else if (this->rhi_view_active
                 && !event->modifiers().testFlag(Qt::ShiftModifier))
        {
            if (zoom_in)
                this->view_2d_zoom_in_key_pressed = true;
            else
                this->view_2d_zoom_out_key_pressed = true;
            ensurePanAnimationRunning();
        }
        else if (zoom_in)
        {
            zoomIn();
        }
        else
        {
            zoomOut();
        }
        event->accept();
        return true;
    }

    const Qt::KeyboardModifiers blocked_modifiers = Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    if (event->modifiers() & blocked_modifiers)
        return false;

    return false;
}

bool MapWidget::handleKeyReleaseEvent(QKeyEvent *event)
{
    if (!event)
        return false;

    if (event->key() == Qt::Key_Control
        && this->view_3d_orbit_active
        && this->view_3d_orbit_input == View3dOrbitInput::CtrlMouse)
    {
        if (!event->isAutoRepeat())
            endView3dOrbit();
        event->accept();
        return true;
    }

    if (event->key() == Qt::Key_Shift)
    {
        if (!event->isAutoRepeat())
            this->pan_fast_modifier_pressed = false;

        if (this->hasKeyboardPanInput())
            this->ensurePanAnimationRunning();

        event->accept();
        return true;
    }

    if (this->setKeyboardPanKey(event, event->isAutoRepeat()))
    {
        if (!event->isAutoRepeat())
            this->ensurePanAnimationRunning();

        event->accept();
        return true;
    }

    const bool zoom_in = guiShortcutMatches(event, guiShortcutRegistry().shortcut(GuiShortcutId::MapZoomIn), Qt::ShiftModifier);
    const bool zoom_out = guiShortcutMatches(event, guiShortcutRegistry().shortcut(GuiShortcutId::MapZoomOut), Qt::ShiftModifier);
    if (zoom_in || zoom_out)
    {
        if (!event->isAutoRepeat())
        {
            if (zoom_in)
            {
                this->view_2d_zoom_in_key_pressed = false;
                this->view_3d_zoom_in_key_pressed = false;
            }
            else
            {
                this->view_2d_zoom_out_key_pressed = false;
                this->view_3d_zoom_out_key_pressed = false;
            }
            if (!hasView3dKeyboardZoomInput())
                endView3dKeyboardZoomInteraction();
            stopPanAnimationIfIdle();
        }
        event->accept();
        return true;
    }

    return false;
}

void MapWidget::clearKeyboardPanInput()
{
    endView3dKeyboardZoomInteraction();
    if (this->view_3d_orbit_active
        && this->view_3d_orbit_input == View3dOrbitInput::CtrlMouse)
    {
        endView3dOrbit();
    }
    this->pan_key_left_pressed = false;
    this->pan_key_right_pressed = false;
    this->pan_key_up_pressed = false;
    this->pan_key_down_pressed = false;
    this->pan_fast_modifier_pressed = false;
    this->view_2d_zoom_in_key_pressed = false;
    this->view_2d_zoom_out_key_pressed = false;
    this->view_3d_zoom_in_key_pressed = false;
    this->view_3d_zoom_out_key_pressed = false;
    this->stopPanAnimationIfIdle();
}

#ifdef Q_OS_WASM
void MapWidget::setBrowserMapLayerEnabled(bool enabled)
{
    if (this->browser_map_layer_enabled == enabled)
        return;

    this->browser_map_layer_enabled = enabled;
    if (enabled)
    {
        const QByteArray crosshair_data_url = browserCrosshairDataUrl();
        if (!crosshair_data_url.isEmpty())
            aowisBrowserMapSetCrosshairImage(crosshair_data_url.constData());
        this->syncBrowserMapView();
    }
    else
    {
        aowisBrowserMapRelease(this->browser_map_layer_owner_id);
        update();
    }
}

void MapWidget::setBrowserMapLayerTopmost(bool topmost)
{
    this->browser_map_layer_topmost = topmost;
}

int MapWidget::browserMapLayerOwnerId() const
{
    return this->browser_map_layer_owner_id;
}

void MapWidget::setBrowserMapLayerGeometry(const QRect &geometry, bool visible)
{
    aowisBrowserMapSetGeometry(
        this->browser_map_layer_owner_id,
        geometry.x(),
        geometry.y(),
        geometry.width(),
        geometry.height(),
        visible ? 1 : 0,
        this->browser_map_layer_topmost ? 1 : 0);

    if (visible)
        this->syncBrowserMapView();
}

void MapWidget::syncBrowserMapView()
{
    if (!this->browser_map_layer_enabled)
        return;

    aowisBrowserMapSetView(
        this->browser_map_layer_owner_id,
        this->m_model->centerLon(),
        this->m_model->centerLat(),
        this->m_model->zoom(),
        int(this->m_model->provider()));
}
#endif

void MapWidget::setEdgePanningEnabled(bool enabled)
{
    if (this->edge_panning_enabled == enabled)
        return;

    this->edge_panning_enabled = enabled;

    if (enabled)
    {
        if (this->isVisible())
        {
            this->edge_pan_poll_timer->start();
            this->pollEdgePan();
        }
    }
    else
    {
        this->edge_pan_poll_timer->stop();
        this->stopPanAnimationIfIdle();
    }
}

void MapWidget::keyPressEvent(QKeyEvent *event)
{
    if (this->handleKeyPressEvent(event))
        return;

    QWidget::keyPressEvent(event);
}

void MapWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (this->handleKeyReleaseEvent(event))
        return;

    QWidget::keyReleaseEvent(event);
}

void MapWidget::focusOutEvent(QFocusEvent *event)
{
    this->clearKeyboardPanInput();
    if (this->view_3d_orbit_active)
        endView3dOrbit();
    QWidget::focusOutEvent(event);
}

void MapWidget::hideEvent(QHideEvent *event)
{
    if (this->view_3d_orbit_active)
        endView3dOrbit(false);
    this->stopAllPanMovement();
    this->edge_pan_poll_timer->stop();
    QWidget::hideEvent(event);
}

void MapWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    if (this->edge_panning_enabled)
    {
        this->edge_pan_poll_timer->start();
        this->pollEdgePan();
    }
}

void MapWidget::panByStep(const QPoint &delta)
{
    this->pan_velocity = QPointF();
    this->pan_fractional_delta = QPointF();
    this->mouse_pan_inertia_active = false;
    cancelBrowserMapMousePan();
    panMapByPixels(delta);
}

void MapWidget::panMapByPixels(const QPoint &delta, bool angle_independent_3d)
{
    if (delta.isNull())
        return;

    const QPointF old_center = this->m_model->centerTile();

    this->backing_store_pan_active = true;
    if (this->m_model->viewMode() == MapViewMode::ThreeD)
    {
        if (angle_independent_3d)
            this->m_model->panByPixels3dKeyboard(delta, size());
        else
            this->m_model->panByPixels3d(delta, size());
    }
    else if (this->m_model->viewMode() == MapViewMode::Globe)
    {
        this->m_model->panByPixelsGlobe(delta, size());
    }
    else
    {
        this->m_model->panByPixels(delta, size());
    }
    this->backing_store_pan_active = false;

    const QPointF new_center = this->m_model->centerTile();
    const double tile_count = double(this->m_model->tileCount());
    const double horizontal_tile_delta = std::remainder(old_center.x() - new_center.x(), tile_count);
    const QPoint actual_delta(
        qRound(horizontal_tile_delta * MapModel::TileSize),
        qRound((old_center.y() - new_center.y()) * MapModel::TileSize));

#ifdef Q_OS_WASM
    if (this->browser_map_layer_enabled)
    {
        this->syncBrowserMapView();
        return;
    }
#endif

    if (actual_delta.isNull())
        return;

    if (std::abs(actual_delta.x()) >= this->width() || std::abs(actual_delta.y()) >= this->height())
    {
        update();
        return;
    }

    this->scroll(actual_delta.x(), actual_delta.y(), this->rect());
}

void MapWidget::panUp()
{
    this->panByStep(QPoint(0, PanButtonStepPixels));
}

void MapWidget::panDown()
{
    this->panByStep(QPoint(0, -PanButtonStepPixels));
}

void MapWidget::panLeft()
{
    this->panByStep(QPoint(PanButtonStepPixels, 0));
}

void MapWidget::panRight()
{
    this->panByStep(QPoint(-PanButtonStepPixels, 0));
}

void MapWidget::wheelEvent(QWheelEvent *event)
{
    this->handleWheelEvent(event);
}

void MapWidget::handleWheelEvent(QWheelEvent *event)
{
    this->wheel_delta_accumulated += event->angleDelta().y();

    const int threshold = 120;
    if (std::abs(this->wheel_delta_accumulated) < threshold)
    {
        event->accept();
        return;
    }

    const int steps = this->wheel_delta_accumulated / threshold;
    this->wheel_delta_accumulated %= threshold;

    if (this->m_model->viewMode() == MapViewMode::Globe)
    {
        // Multiplicative zoom (rather than the additive tile-zoom step used
        // by ThreeD) since globe distance is a continuous meters value with
        // no discrete zoom levels to snap to.
        this->m_model->setViewGlobeDistanceM(
            this->m_model->viewGlobeDistanceM() * std::pow(0.85, steps));
    }
    else if (this->m_model->viewMode() == MapViewMode::ThreeD)
        this->m_model->setZoom(this->m_model->zoom() + steps, size());
    else
        this->m_model->zoomByAt(steps, event->position().toPoint(), size());
    event->accept();
}

void MapWidget::beginView3dOrbit(
    const QPoint &position, const QPoint &global_position, View3dOrbitInput input)
{
    if (this->view_3d_orbit_active || input == View3dOrbitInput::None)
        return;

#ifdef Q_OS_WASM
    Q_UNUSED(global_position);
#else
    Q_UNUSED(position);
#endif

    this->view_3d_orbit_active = true;
    this->view_3d_orbit_input = input;
    this->m_model->beginView3dRotateInteraction();
#ifdef Q_OS_WASM
    this->view_3d_orbit_last_position = position;
#else
    this->view_3d_orbit_restore_global = global_position;
    this->view_3d_orbit_last_global_position = global_position;
    this->view_3d_orbit_warp_pending = false;
    grabMouse(QCursor(Qt::BlankCursor));
    this->view_3d_orbit_mouse_grabbed = true;
#endif
}

void MapWidget::endView3dOrbit(bool restore_cursor_position)
{
    if (!this->view_3d_orbit_active
#ifndef Q_OS_WASM
        && !this->view_3d_orbit_mouse_grabbed
#endif
    )
    {
        return;
    }

    this->view_3d_orbit_active = false;
    this->view_3d_orbit_input = View3dOrbitInput::None;
    this->m_model->endView3dRotateInteraction();
#ifndef Q_OS_WASM
    this->view_3d_orbit_warp_pending = false;
    if (this->view_3d_orbit_mouse_grabbed)
    {
        releaseMouse();
        this->view_3d_orbit_mouse_grabbed = false;
    }
    if (restore_cursor_position)
        QCursor::setPos(this->view_3d_orbit_restore_global);
#endif
}

void MapWidget::mousePressEvent(QMouseEvent *event)
{
    if (this->handleMousePressEvent(event))
        return;

    QWidget::mousePressEvent(event);
}

bool MapWidget::handleMousePressEvent(QMouseEvent *event)
{
    if (!event)
        return false;

    if ((this->m_model->viewMode() == MapViewMode::ThreeD
            || this->m_model->viewMode() == MapViewMode::Globe)
        && event->button() == Qt::MiddleButton)
    {
        beginView3dOrbit(
            event->position().toPoint(),
            event->globalPosition().toPoint(),
            View3dOrbitInput::MiddleMouse);
        this->pan_velocity = QPointF();
        this->mouse_pan_inertia_active = false;
        stopPanAnimationIfIdle();
        event->accept();
        return true;
    }

    if (event->button() != Qt::LeftButton)
        return false;

    this->mouse_pan_active = true;
    this->mouse_pan_last_position = event->position().toPoint();
    this->pan_velocity = QPointF();
    this->pan_fractional_delta = QPointF();
    this->mouse_pan_velocity = QPointF();
    this->mouse_pan_move_elapsed_timer.start();
    this->mouse_pan_inertia_active = false;
    this->mouse_pan_drag_distance = 0;
    beginBrowserMapMousePan();
    stopPanAnimationIfIdle();
    event->accept();
    return true;
}

void MapWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (this->handleMouseReleaseEvent(event))
        return;

    QWidget::mouseReleaseEvent(event);
}

bool MapWidget::handleMouseReleaseEvent(QMouseEvent *event)
{
    if (!event)
        return false;

    if (event->button() == Qt::MiddleButton && this->view_3d_orbit_active)
    {
        endView3dOrbit();
        event->accept();
        return true;
    }

    if (event->button() != Qt::LeftButton || !this->mouse_pan_active)
        return false;

    this->mouse_pan_active = false;

    if (browserMapHandlesMouseInertia())
    {
        this->pan_velocity = QPointF();
        this->pan_fractional_delta = QPointF();
        this->mouse_pan_velocity = QPointF();
        this->mouse_pan_inertia_active = false;
        this->mouse_pan_drag_distance = 0;

        if (releaseBrowserMapMousePan())
            ensurePanAnimationRunning();

        stopPanAnimationIfIdle();
        event->accept();
        return true;
    }

    const bool movement_is_recent = this->mouse_pan_move_elapsed_timer.isValid() &&
        this->mouse_pan_move_elapsed_timer.elapsed() <= MousePanReleaseTimeoutMs;
    const qreal release_speed = vectorLength(this->mouse_pan_velocity);
    const bool dragged_far_enough = this->mouse_pan_drag_distance >= QApplication::startDragDistance();

    if (dragged_far_enough && movement_is_recent && release_speed >= MousePanMinimumInertiaSpeedPixelsPerSecond)
    {
        this->pan_velocity = this->mouse_pan_velocity;
        this->pan_fractional_delta = QPointF();
        this->mouse_pan_inertia_active = true;
        ensurePanAnimationRunning();
    }
    else
    {
        this->pan_velocity = QPointF();
        this->mouse_pan_inertia_active = false;
    }

    this->mouse_pan_velocity = QPointF();
    this->mouse_pan_drag_distance = 0;
    stopPanAnimationIfIdle();
    event->accept();
    return true;
}

void MapWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (this->handleMouseMoveEvent(event))
        return;

    QWidget::mouseMoveEvent(event);
}

bool MapWidget::handleMouseMoveEvent(QMouseEvent *event)
{
    if (!event)
        return false;

    const QPoint position = event->position().toPoint();
    this->updatePointerCoordinates(position);

    const bool ctrl_mouse_orbit_requested =
        this->m_model->viewMode() == MapViewMode::ThreeD
        && event->modifiers().testFlag(Qt::ControlModifier)
        && event->buttons() == Qt::NoButton;

    if (!this->view_3d_orbit_active && ctrl_mouse_orbit_requested)
    {
        beginView3dOrbit(
            event->position().toPoint(),
            event->globalPosition().toPoint(),
            View3dOrbitInput::CtrlMouse);
        this->pan_velocity = QPointF();
        this->mouse_pan_inertia_active = false;
        stopPanAnimationIfIdle();
        event->accept();
        return true;
    }

    if (this->view_3d_orbit_active)
    {
        bool orbit_input_still_active = false;
        if (this->view_3d_orbit_input == View3dOrbitInput::MiddleMouse)
            orbit_input_still_active = event->buttons().testFlag(Qt::MiddleButton);
        else if (this->view_3d_orbit_input == View3dOrbitInput::CtrlMouse)
            orbit_input_still_active = ctrl_mouse_orbit_requested;

        if (!orbit_input_still_active)
        {
            endView3dOrbit();
            return false;
        }

#ifndef Q_OS_WASM
        const QPoint global_position = event->globalPosition().toPoint();

        if (this->view_3d_orbit_warp_pending)
        {
            const bool reached_warp_target =
                (global_position - this->view_3d_orbit_warp_target_global)
                    .manhattanLength() <= 2;
            if (reached_warp_target)
            {
                this->view_3d_orbit_warp_pending = false;
                this->view_3d_orbit_last_global_position = global_position;
                event->accept();
                return true;
            }

            const int distance_to_warp_target =
                (global_position - this->view_3d_orbit_warp_target_global)
                    .manhattanLength();
            const int distance_to_warp_source =
                (global_position - this->view_3d_orbit_warp_source_global)
                    .manhattanLength();
            if (distance_to_warp_source < distance_to_warp_target)
            {
                // This event still belongs to the pre-warp edge position.
                event->accept();
                return true;
            }

            // The platform may coalesce away the exact warp-target event. A
            // subsequent event nearer the target is genuine post-warp motion.
            this->view_3d_orbit_warp_pending = false;
            this->view_3d_orbit_last_global_position =
                this->view_3d_orbit_warp_target_global;
        }

        const QPoint delta =
            global_position - this->view_3d_orbit_last_global_position;
        this->view_3d_orbit_last_global_position = global_position;
        if (!delta.isNull())
        {
            if (this->m_model->viewMode() == MapViewMode::Globe)
                this->m_model->orbitViewGlobeByPointerDelta(delta, true);
            else
                this->m_model->orbitView3dByPointerDelta(delta, true);
        }

        // Do not recenter on every mouse event. Apart from being unnecessary,
        // that creates a stream of synthetic motion events whose ordering is
        // platform-dependent. Recenter only when the hidden cursor is actually
        // about to hit a physical screen edge.
        constexpr int OrbitCursorEdgeMarginPixels = 24;
        QScreen *screen = QApplication::screenAt(global_position);
        if (screen != nullptr)
        {
            const QRect screen_geometry = screen->geometry();
            const bool near_screen_edge =
                global_position.x() <= screen_geometry.left() + OrbitCursorEdgeMarginPixels
                || global_position.x() >= screen_geometry.right() - OrbitCursorEdgeMarginPixels
                || global_position.y() <= screen_geometry.top() + OrbitCursorEdgeMarginPixels
                || global_position.y() >= screen_geometry.bottom() - OrbitCursorEdgeMarginPixels;
            if (near_screen_edge)
            {
                const QPoint warp_target_global = mapToGlobal(rect().center());
                if (warp_target_global != global_position)
                {
                    this->view_3d_orbit_warp_pending = true;
                    this->view_3d_orbit_warp_source_global = global_position;
                    this->view_3d_orbit_warp_target_global = warp_target_global;
                    this->view_3d_orbit_last_global_position = warp_target_global;
                    QCursor::setPos(warp_target_global);
                }
            }
        }
#else
        const QPoint delta = position - this->view_3d_orbit_last_position;
        this->view_3d_orbit_last_position = position;
        if (!delta.isNull())
        {
            if (this->m_model->viewMode() == MapViewMode::Globe)
                this->m_model->orbitViewGlobeByPointerDelta(delta, true);
            else
                this->m_model->orbitView3dByPointerDelta(delta, true);
        }
#endif
        event->accept();
        return true;
    }

#ifdef Q_OS_WASM
    // Qt for WebAssembly receives browser pointer events. A pointerdown event is
    // only generated for the first pressed mouse button, so pressing left while
    // right is already held does not reach handleMousePressEvent(). Recover the
    // missing transition from the buttons state carried by pointermove.
    if (!this->mouse_pan_active && (event->buttons() & Qt::LeftButton))
    {
        this->mouse_pan_active = true;
        this->mouse_pan_last_position = position;
        this->pan_velocity = QPointF();
        this->pan_fractional_delta = QPointF();
        this->mouse_pan_velocity = QPointF();
        this->mouse_pan_move_elapsed_timer.start();
        this->mouse_pan_inertia_active = false;
        this->mouse_pan_drag_distance = 0;
        beginBrowserMapMousePan();
        stopPanAnimationIfIdle();
        event->accept();
        return true;
    }
#endif

    if (!this->mouse_pan_active)
        return false;

    if (!(event->buttons() & Qt::LeftButton))
    {
        this->mouse_pan_active = false;
        this->mouse_pan_velocity = QPointF();
        this->mouse_pan_inertia_active = false;
        this->mouse_pan_drag_distance = 0;
        cancelBrowserMapMousePan();
        return false;
    }

    const QPoint delta = position - this->mouse_pan_last_position;
    this->mouse_pan_last_position = position;

    const qint64 elapsed_ms = this->mouse_pan_move_elapsed_timer.restart();
    if (browserMapHandlesMouseInertia())
    {
        updateBrowserMapMousePan(delta, elapsed_ms);
    }
    else
    {
        this->mouse_pan_drag_distance += delta.manhattanLength();

        if (!delta.isNull() && elapsed_ms > 0)
        {
            const qreal elapsed_seconds = qBound<qreal>(0.001, elapsed_ms / 1000.0, 0.1);
            QPointF measured_velocity = QPointF(delta) / elapsed_seconds;
            const qreal measured_speed = vectorLength(measured_velocity);
            if (measured_speed > MousePanMaximumSpeedPixelsPerSecond)
                measured_velocity = normalized(measured_velocity) * MousePanMaximumSpeedPixelsPerSecond;

            if (this->mouse_pan_velocity.isNull())
            {
                this->mouse_pan_velocity = measured_velocity;
            }
            else
            {
                this->mouse_pan_velocity =
                    this->mouse_pan_velocity * (1.0 - MousePanVelocitySmoothing) +
                    measured_velocity * MousePanVelocitySmoothing;
            }
        }
    }

    if (!delta.isNull())
    {
        this->keyboard_pan_motion_active = false;
        panMapByPixels(delta);
    }

    event->accept();
    return true;
}

void MapWidget::updatePointerCoordinates(const QPoint &position)
{
    if (this->rhi_view_active
        && this->m_model->viewMode() == MapViewMode::ThreeD
        && this->rhi_screen_coordinate_resolver)
    {
        CoordinateWGS84 terrain_coordinate;
        if (!this->rhi_screen_coordinate_resolver(
                QPointF(position), &terrain_coordinate))
        {
            emit signalCoordsUnavailable();
            return;
        }

        emitPointerCoordinate(terrain_coordinate);
        return;
    }

    const CoordinateWGS84 wgs = this->m_model->wgs84FromScreen(position, this->size());
    emitPointerCoordinate(wgs);
}

void MapWidget::emitPointerCoordinate(const CoordinateWGS84 &wgs)
{
    emit signalCoordsChangedWgs84(wgs);

    GeoMetricProjection projection;
    const CoordinateUTM utm = projection.wgs84ToUtm(wgs);
    emit signalCoordsChangedUTM(utm);
}

void MapWidget::setRhiScreenCoordinateResolver(ScreenCoordinateResolver resolver)
{
    this->rhi_screen_coordinate_resolver = std::move(resolver);
}

void MapWidget::scheduleTileUpdate(const QString &)
{
    if (this->rhi_view_active)
        return;
#ifdef Q_OS_WASM
    if (this->browser_map_layer_enabled)
        return;
#endif

    if (!this->tile_update_timer->isActive())
        this->tile_update_timer->start();
}

void MapWidget::zoomIn()
{
    this->m_model->zoomIn(this->size());
}

void MapWidget::zoomOut()
{
    this->m_model->zoomOut(this->size());
}

void MapWidget::setRhiViewActive(bool active)
{
    if (this->rhi_view_active == active)
        return;

    this->rhi_view_active = active;
    if (active)
        return;

    this->view_2d_zoom_in_key_pressed = false;
    this->view_2d_zoom_out_key_pressed = false;
    if (this->m_model != nullptr && this->m_model->viewMode() == MapViewMode::TwoD)
        this->m_model->resetView2dContinuousZoom(this->size());
    stopPanAnimationIfIdle();
}

void MapWidget::changeMapProvider(MapProvider provider)
{
    this->m_model->setProvider(provider);
}

void MapWidget::paintEvent(QPaintEvent *event)
{
    if (this->rhi_view_active)
    {
        Q_UNUSED(event)
        return;
    }
#ifdef Q_OS_WASM
    if (this->browser_map_layer_enabled)
    {
        Q_UNUSED(event)
        return;
    }
#endif

    QPainter painter(this);
    painter.setClipRegion(event->region());
    painter.fillRect(event->rect(), this->palette().brush(QPalette::Window));
    this->drawTiles(painter);

    if (this->has_gps_coordinate)
    {
        const QPointF gps_point = this->m_model->screenFromWgs84(
            this->gps_coordinate, size());
        painter.setBrush(Qt::red);
        painter.drawEllipse(gps_point, 5.0, 5.0);
    }
}

void MapWidget::drawTiles(QPainter &painter)
{
    const int tile_count = this->m_model->tileCount();
    const QPointF center = this->m_model->centerTile();
    const double center_x = center.x();
    const double center_y = center.y();

    const int viewport_width = this->width();
    const int viewport_height = this->height();
    const int tiles_x = viewport_width / MapModel::TileSize + 4;
    const int tiles_y = viewport_height / MapModel::TileSize + 4;
    const int center_tile_x = int(std::floor(center_x));
    const int center_tile_y = int(std::floor(center_y));
    const int start_x = center_tile_x - tiles_x / 2;
    const int start_y = center_tile_y - tiles_y / 2;
    const QString request_layout_key = QStringLiteral("%1|%2|%3|%4|%5")
        .arg(this->m_model->tileCachePrefix(this->m_model->zoom()))
        .arg(start_x)
        .arg(start_y)
        .arg(tiles_x)
        .arg(tiles_y);
    const quint64 request_batch = this->tile_repository->beginTileRequestBatch(
        this, request_layout_key);

    for (int delta_x = 0; delta_x < tiles_x; ++delta_x)
    {
        for (int delta_y = 0; delta_y < tiles_y; ++delta_y)
        {
            const int virtual_x = start_x + delta_x;
            const int y = start_y + delta_y;

            if (y < 0 || y >= tile_count)
                continue;

            const int tile_x = GeoWebMercator::wrapTileX(virtual_x, this->m_model->zoom());
            const QString key = this->m_model->tileCacheKey(tile_x, y);
            const QPixmap *pixmap = this->tile_repository->tile(key);

            if (!pixmap)
            {
                const int priority_x = virtual_x - center_tile_x;
                const int priority_y = y - center_tile_y;
                const int priority = priority_x * priority_x + priority_y * priority_y;
                this->tile_repository->requestTile(
                    this->m_model->tileEndpoint(tile_x, y), key, tile_x, y,
                    priority, request_batch);
                continue;
            }

            const int pixel_x = int(std::floor((virtual_x - center_x) * MapModel::TileSize + viewport_width / 2.0));
            const int pixel_y = int(std::floor((y - center_y) * MapModel::TileSize + viewport_height / 2.0));
            painter.drawPixmap(pixel_x, pixel_y, *pixmap);
        }
    }
}
