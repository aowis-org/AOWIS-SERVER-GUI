#include "map_widget.h"

#include <QApplication>

#ifndef Q_OS_WASM
#include <QCursor>
#include <QGeoCoordinate>
#include <QScreen>
#include <QWindow>
#endif

#include <cmath>

namespace
{
constexpr int PanFrameIntervalMs = 16;
constexpr int PanButtonStepPixels = 120;
constexpr int EdgePanMarginPixels = 2;
#ifndef Q_OS_WASM
constexpr int EdgePanPollIntervalMs = 50;
#endif
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

    this->init();
}

MapModel *MapWidget::model() const
{
    return this->m_model;
}

void MapWidget::init()
{
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
        this->update();
    });

    connect(this->m_model, &MapModel::centerChangedWGS84, this, [this](CoordinateWGS84 wgs)
    {
        emit signalCoordsChangedWgs84(wgs);
        this->update();
    });

    connect(this->m_model, &MapModel::centerChangedUTM, this, &MapWidget::signalCoordsChangedUTM);
    connect(this->m_model, &MapModel::providerChanged, this, [this](MapProvider)
    {
        this->update();
    });

    connect(this->tile_repository, &MapTileRepository::signalTileAvailable, this, [this](const QString &)
    {
        this->update();
    });
    connect(this->tile_repository, &MapTileRepository::signalTileFailed, this, [this](const QString &)
    {
        this->update();
    });
    connect(this->tile_repository, &MapTileRepository::signalTileRetryReady, this, [this](const QString &)
    {
        this->update();
    });

    if (this->gps)
    {
        connect(this->gps, &GpsProvider::positionChanged, this, [this](const QGeoPositionInfo &info)
        {
#ifndef Q_OS_WASM
            const QGeoCoordinate coord = info.coordinate();

            this->gps_coordinate.latitude_deg = coord.latitude();
            this->gps_coordinate.longitude_deg = coord.longitude();
            this->gps_coordinate.altitude_m = coord.altitude();
#endif
        });
        connect(this->gps, &GpsProvider::statusMessage, this, [](const QString &message)
        {
            qDebug() << message;
        });
    }

    QTimer::singleShot(100, this, [this]
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

#ifndef Q_OS_WASM
    this->edge_pan_poll_timer = new QTimer(this);
    this->edge_pan_poll_timer->setInterval(EdgePanPollIntervalMs);
    connect(this->edge_pan_poll_timer, &QTimer::timeout, this, &MapWidget::pollEdgePan);
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
        this->m_model->panByPixels(delta, this->size());

    this->stopPanAnimationIfIdle();
}

#ifndef Q_OS_WASM
void MapWidget::pollEdgePan()
{
    if (!this->edgePanDirection().isNull())
        this->ensurePanAnimationRunning();
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
#ifndef Q_OS_WASM
    if (!this->edge_panning_enabled || !this->isVisible() || !this->window()->isFullScreen() || !this->window()->isActiveWindow())
        return QPointF();

    if (QApplication::activePopupWidget() || QApplication::activeModalWidget())
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
    return QPointF();
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

void MapWidget::setEdgePanningEnabled(bool enabled)
{
#ifdef Q_OS_WASM
    Q_UNUSED(enabled)
    this->edge_panning_enabled = false;
#else
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
#endif
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
#ifndef Q_OS_WASM
    this->edge_pan_poll_timer->stop();
#endif
    QWidget::hideEvent(event);
}

void MapWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

#ifndef Q_OS_WASM
    if (this->edge_panning_enabled)
    {
        this->edge_pan_poll_timer->start();
        this->pollEdgePan();
    }
#endif
}

void MapWidget::panByStep(const QPoint &delta)
{
    this->pan_velocity = QPointF();
    this->pan_fractional_delta = QPointF();
#ifndef Q_OS_WASM
    this->mouse_pan_inertia_active = false;
#endif
    this->m_model->panByPixels(delta, this->size());
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
        this->m_model->panByPixels(delta, this->size());

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

void MapWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    this->drawTiles(painter);

    const QPointF gps_point = this->m_model->screenFromWgs84(this->gps_coordinate, this->size());

    painter.setBrush(Qt::red);
    painter.drawEllipse(gps_point, 5.0, 5.0);
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
            QPixmap *pixmap = this->tile_repository->tile(key);

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
