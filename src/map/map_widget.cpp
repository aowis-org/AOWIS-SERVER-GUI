#include "map_widget.h"

#include <QApplication>
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
#endif

namespace
{
#ifdef Q_OS_WASM
int nextBrowserMapLayerOwnerId()
{
    static int next_owner_id = 1;
    return next_owner_id++;
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
#ifndef Q_OS_WASM
constexpr int MousePanReleaseTimeoutMs = 100;
constexpr double MousePanVelocitySmoothing = 0.65;
constexpr double MousePanMaximumSpeedPixelsPerSecond = 2400.0;
constexpr double MousePanMinimumInertiaSpeedPixelsPerSecond = 70.0;
constexpr double MousePanInertiaDecelerationPixelsPerSecondSquared = 2600.0;
#endif

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

    this->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &MapWidget::customContextMenuRequested, this, &MapWidget::showContextMenu);

    this->setMinimumHeight(500);
    this->setMinimumWidth(550);
    this->setContentsMargins(0, 0, 0, 0);

    this->setFocusPolicy(Qt::StrongFocus);
    this->setFocus();
    this->setMouseTracking(true);

    connect(this->m_model, &MapModel::zoomChanged, this, [this](int zoom)
    {
        emit signalZoomChanged(zoom);
#ifdef Q_OS_WASM
        if (this->browser_map_layer_enabled)
        {
            this->syncBrowserMapView();
            return;
        }
#endif
        update();
    });

    connect(this->m_model, &MapModel::centerChangedWGS84, this, [this](CoordinateWGS84 wgs)
    {
        emit signalCoordsChangedWgs84(wgs);
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

    connect(this->m_model, &MapModel::centerChangedUTM, this, &MapWidget::signalCoordsChangedUTM);
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
    if (this->hasKeyboardPanInput() || vectorLength(this->pan_velocity) >= PanVelocityStopThreshold)
        return;

    this->pan_velocity = QPointF();
    this->pan_fractional_delta = QPointF();
    this->pan_timer->stop();
}

void MapWidget::stopAllPanMovement()
{
    this->pan_key_left_pressed = false;
    this->pan_key_right_pressed = false;
    this->pan_key_up_pressed = false;
    this->pan_key_down_pressed = false;
    this->pan_fast_modifier_pressed = false;
    this->mouse_pan_active = false;
    this->pan_velocity = QPointF();
    this->pan_fractional_delta = QPointF();

#ifndef Q_OS_WASM
    this->mouse_pan_velocity = QPointF();
    this->mouse_pan_inertia_active = false;
    this->mouse_pan_drag_distance = 0;
#endif

    this->pan_timer->stop();
}

void MapWidget::updatePanAnimation()
{
    const qint64 elapsed_ms = this->pan_elapsed_timer.restart();
    const qreal elapsed_seconds = qBound<qreal>(0.0, elapsed_ms / 1000.0, 0.05);

    if (elapsed_seconds <= 0.0)
        return;

    if (!this->isVisible())
    {
        this->stopAllPanMovement();
        return;
    }

    if (this->mouse_pan_active)
    {
        this->pan_velocity = QPointF();
        this->pan_fractional_delta = QPointF();
        return;
    }

    QPointF direction = this->keyboardPanDirection();
    const bool keyboard_pan_active = !direction.isNull();
    bool edge_pan_active = false;
    if (!keyboard_pan_active)
    {
        direction = this->edgePanDirection();
        edge_pan_active = !direction.isNull();
    }
    direction = normalized(direction);

#ifndef Q_OS_WASM
    if (!direction.isNull())
        this->mouse_pan_inertia_active = false;
#endif

    bool fast_pan_active = keyboard_pan_active && this->hasFastKeyboardPanInput();
#ifndef Q_OS_WASM
    if (edge_pan_active && QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))
        fast_pan_active = true;
#endif

    qreal maximum_speed = PanMaximumSpeedPixelsPerSecond;
    qreal acceleration = PanAccelerationPixelsPerSecondSquared;
    if (fast_pan_active)
    {
        maximum_speed *= PanFastSpeedMultiplier;
        acceleration *= PanFastAccelerationMultiplier;
    }

#ifndef Q_OS_WASM
    if (direction.isNull() && this->mouse_pan_inertia_active)
        acceleration = MousePanInertiaDecelerationPixelsPerSecondSquared;
    else if (direction.isNull())
        acceleration = PanDecelerationPixelsPerSecondSquared;
#else
    if (direction.isNull())
        acceleration = PanDecelerationPixelsPerSecondSquared;
#endif

    const QPointF target_velocity = direction * maximum_speed;
    this->pan_velocity = moveTowards(this->pan_velocity, target_velocity, acceleration * elapsed_seconds);

    if (direction.isNull() && vectorLength(this->pan_velocity) < PanVelocityStopThreshold)
    {
        this->pan_velocity = QPointF();
#ifndef Q_OS_WASM
        this->mouse_pan_inertia_active = false;
#endif
    }

    const QPointF precise_delta = this->pan_velocity * elapsed_seconds + this->pan_fractional_delta;
    const QPoint delta(int(std::trunc(precise_delta.x())), int(std::trunc(precise_delta.y())));
    this->pan_fractional_delta = precise_delta - QPointF(delta);

    if (!delta.isNull())
        this->panMapByPixels(delta);

    this->stopPanAnimationIfIdle();
}

void MapWidget::pollEdgePan()
{
    if (!this->edgePanDirection().isNull())
        this->ensurePanAnimationRunning();
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

bool MapWidget::setKeyboardPanKey(int key, bool pressed)
{
    switch (key)
    {
    case Qt::Key_Left:
    case Qt::Key_U:
        this->pan_key_left_pressed = pressed;
        return true;

    case Qt::Key_Right:
    case Qt::Key_A:
        this->pan_key_right_pressed = pressed;
        return true;

    case Qt::Key_Up:
    case Qt::Key_V:
        this->pan_key_up_pressed = pressed;
        return true;

    case Qt::Key_Down:
    case Qt::Key_I:
        this->pan_key_down_pressed = pressed;
        return true;

    default:
        return false;
    }
}

bool MapWidget::hasKeyboardPanInput() const
{
    return this->pan_key_left_pressed || this->pan_key_right_pressed || this->pan_key_up_pressed || this->pan_key_down_pressed;
}

bool MapWidget::hasFastKeyboardPanInput() const
{
    return this->pan_fast_modifier_pressed && this->hasKeyboardPanInput();
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

    const Qt::KeyboardModifiers blocked_modifiers = Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    if (event->modifiers() & blocked_modifiers)
        return false;

    if (event->key() == Qt::Key_Shift)
    {
        this->pan_fast_modifier_pressed = true;
        if (this->hasKeyboardPanInput())
            this->ensurePanAnimationRunning();

        event->accept();
        return true;
    }

    if (this->setKeyboardPanKey(event->key(), true))
    {
        this->pan_fast_modifier_pressed = event->modifiers().testFlag(Qt::ShiftModifier);
#ifndef Q_OS_WASM
        this->mouse_pan_inertia_active = false;
#endif
        this->ensurePanAnimationRunning();
        event->accept();
        return true;
    }

    switch (event->key())
    {
    case Qt::Key_L:
        this->zoomIn();
        event->accept();
        return true;

    case Qt::Key_X:
        this->zoomOut();
        event->accept();
        return true;

    default:
        return false;
    }
}

bool MapWidget::handleKeyReleaseEvent(QKeyEvent *event)
{
    if (!event)
        return false;

    if (event->key() == Qt::Key_Shift)
    {
        if (!event->isAutoRepeat())
            this->pan_fast_modifier_pressed = false;

        if (this->hasKeyboardPanInput())
            this->ensurePanAnimationRunning();

        event->accept();
        return true;
    }

    if (this->setKeyboardPanKey(event->key(), event->isAutoRepeat()))
    {
        if (!event->isAutoRepeat())
            this->ensurePanAnimationRunning();

        event->accept();
        return true;
    }

    if (event->key() == Qt::Key_L || event->key() == Qt::Key_X)
    {
        event->accept();
        return true;
    }

    return false;
}

void MapWidget::clearKeyboardPanInput()
{
    this->pan_key_left_pressed = false;
    this->pan_key_right_pressed = false;
    this->pan_key_up_pressed = false;
    this->pan_key_down_pressed = false;
    this->pan_fast_modifier_pressed = false;
    this->stopPanAnimationIfIdle();
}

#ifdef Q_OS_WASM
void MapWidget::setBrowserMapLayerEnabled(bool enabled)
{
    if (this->browser_map_layer_enabled == enabled)
        return;

    this->browser_map_layer_enabled = enabled;
    if (enabled)
        this->syncBrowserMapView();
    else
        aowisBrowserMapRelease(this->browser_map_layer_owner_id);

    update();
}

void MapWidget::setBrowserMapLayerTopmost(bool topmost)
{
    this->browser_map_layer_topmost = topmost;
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
    QWidget::focusOutEvent(event);
}

void MapWidget::hideEvent(QHideEvent *event)
{
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
#ifndef Q_OS_WASM
    this->mouse_pan_inertia_active = false;
#endif
    this->panMapByPixels(delta);
}

void MapWidget::panMapByPixels(const QPoint &delta)
{
    if (delta.isNull())
        return;

    const QPointF old_center = this->m_model->centerTile();

    this->backing_store_pan_active = true;
    this->m_model->panByPixels(delta, this->size());
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

    this->m_model->zoomByAt(steps, event->position().toPoint(), this->size());
    event->accept();
}

void MapWidget::mousePressEvent(QMouseEvent *event)
{
    if (this->handleMousePressEvent(event))
        return;

    QWidget::mousePressEvent(event);
}

bool MapWidget::handleMousePressEvent(QMouseEvent *event)
{
    if (!event || event->button() != Qt::LeftButton)
        return false;

    this->mouse_pan_active = true;
    this->mouse_pan_last_position = event->position().toPoint();
    this->pan_velocity = QPointF();
    this->pan_fractional_delta = QPointF();
#ifndef Q_OS_WASM
    this->mouse_pan_velocity = QPointF();
    this->mouse_pan_move_elapsed_timer.start();
    this->mouse_pan_inertia_active = false;
    this->mouse_pan_drag_distance = 0;
#endif
    this->stopPanAnimationIfIdle();
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
    if (!event || event->button() != Qt::LeftButton || !this->mouse_pan_active)
        return false;

    this->mouse_pan_active = false;
#ifndef Q_OS_WASM
    const bool movement_is_recent = this->mouse_pan_move_elapsed_timer.isValid() &&
        this->mouse_pan_move_elapsed_timer.elapsed() <= MousePanReleaseTimeoutMs;
    const qreal release_speed = vectorLength(this->mouse_pan_velocity);
    const bool dragged_far_enough = this->mouse_pan_drag_distance >= QApplication::startDragDistance();

    if (dragged_far_enough && movement_is_recent && release_speed >= MousePanMinimumInertiaSpeedPixelsPerSecond)
    {
        this->pan_velocity = this->mouse_pan_velocity;
        this->pan_fractional_delta = QPointF();
        this->mouse_pan_inertia_active = true;
        this->ensurePanAnimationRunning();
    }
    else
    {
        this->pan_velocity = QPointF();
        this->mouse_pan_inertia_active = false;
    }

    this->mouse_pan_velocity = QPointF();
    this->mouse_pan_drag_distance = 0;
#endif
    this->stopPanAnimationIfIdle();
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

    if (!this->mouse_pan_active)
        return false;

    if (!(event->buttons() & Qt::LeftButton))
    {
        this->mouse_pan_active = false;
#ifndef Q_OS_WASM
        this->mouse_pan_velocity = QPointF();
        this->mouse_pan_inertia_active = false;
        this->mouse_pan_drag_distance = 0;
#endif
        return false;
    }

    const QPoint delta = position - this->mouse_pan_last_position;
    this->mouse_pan_last_position = position;

#ifndef Q_OS_WASM
    this->mouse_pan_drag_distance += delta.manhattanLength();

    const qint64 elapsed_ms = this->mouse_pan_move_elapsed_timer.restart();
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
#endif

    if (!delta.isNull())
        this->panMapByPixels(delta);

    event->accept();
    return true;
}

void MapWidget::updatePointerCoordinates(const QPoint &position)
{
    const CoordinateWGS84 wgs = this->m_model->wgs84FromScreen(position, this->size());
    emit signalCoordsChangedWgs84(wgs);

    GeoMetricProjection projection;
    const CoordinateUTM utm = projection.wgs84ToUtm(wgs);
    emit signalCoordsChangedUTM(utm);
}

void MapWidget::scheduleTileUpdate(const QString &)
{
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

void MapWidget::changeMapProvider(MapProvider provider)
{
    this->m_model->setProvider(provider);
}

void MapWidget::paintEvent(QPaintEvent *event)
{
#ifdef Q_OS_WASM
    if (this->browser_map_layer_enabled)
    {
        QPainter painter(this);
        painter.fillRect(event->rect(), this->palette().brush(QPalette::Window));
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
    const int start_x = int(std::floor(center_x)) - tiles_x / 2;
    const int start_y = int(std::floor(center_y)) - tiles_y / 2;

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
                this->tile_repository->requestTile(this->m_model->tileEndpoint(tile_x, y), key, tile_x, y);
                continue;
            }

            const int pixel_x = int(std::floor((virtual_x - center_x) * MapModel::TileSize + viewport_width / 2.0));
            const int pixel_y = int(std::floor((y - center_y) * MapModel::TileSize + viewport_height / 2.0));
            painter.drawPixmap(pixel_x, pixel_y, *pixmap);
        }
    }
}

void MapWidget::showContextMenu(const QPoint &pos)
{
    QMenu *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction *action_elevation = menu->addAction("Get Elevation");
    Q_UNUSED(action_elevation)

    menu->popup(this->mapToGlobal(pos));
}
