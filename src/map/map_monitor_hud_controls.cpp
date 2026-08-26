#include "map_monitor_hud_controls.h"

#include "map_model.h"

#include <QAbstractAnimation>
#include <QColor>
#include <QComboBox>
#include <QCursor>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPolygonF>
#include <QRadialGradient>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>

#include <cmath>
#include <functional>

namespace
{
constexpr int HudMarginPx = 6;
constexpr int CompassDiameterPx = 82;
constexpr double CompassWheelStepDeg = 5.0;
constexpr int NorthAnimationDurationMs = 280;
constexpr int SliderResetAnimationDurationMs = 240;
constexpr int CompassDragThresholdPx = 4;
constexpr int CompassWheelRotateEndDelayMs = 350;
constexpr int CameraDistanceSliderSteps = 1000;
constexpr int CameraControlSliderHeightPx = 82;
constexpr int CameraControlMinimumWidthPx = 72;
constexpr int NetworkGroundOffsetSliderSteps =
    static_cast<int>(MapModel::MaxView3dNetworkGroundOffsetM * 10.0);

int cameraDistanceSliderValue(double distance_m, double maximum_distance_m)
{
    const double bounded_maximum = qMax(
        MapModel::MinView3dCameraDistanceM, maximum_distance_m);
    const double bounded_distance = qBound(
        MapModel::MinView3dCameraDistanceM, distance_m, bounded_maximum);
    const double range = bounded_maximum - MapModel::MinView3dCameraDistanceM;
    if (range <= 0.0)
        return 0;
    return qRound((bounded_distance - MapModel::MinView3dCameraDistanceM)
        / range * CameraDistanceSliderSteps);
}

double cameraDistanceMeters(int slider_value, double maximum_distance_m)
{
    const double bounded_maximum = qMax(
        MapModel::MinView3dCameraDistanceM, maximum_distance_m);
    const double ratio = qBound(0, slider_value, CameraDistanceSliderSteps)
        / double(CameraDistanceSliderSteps);
    return MapModel::MinView3dCameraDistanceM
        + ratio * (bounded_maximum - MapModel::MinView3dCameraDistanceM);
}

QString cameraDistanceText(double distance_m)
{
    return QStringLiteral("%1 m").arg(qRound(distance_m));
}

QString cameraDistanceMaximumText(double distance_m)
{
    if (distance_m >= 100000.0)
        return QStringLiteral("%1 km").arg(qRound(distance_m / 1000.0));

    if (distance_m >= 10000.0)
        return QStringLiteral("%1 km").arg(distance_m / 1000.0, 0, 'f', 1);

    return cameraDistanceText(distance_m);
}

void configureHudFrame(QFrame *frame)
{
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setFrameShadow(QFrame::Raised);
    frame->setAutoFillBackground(true);
    frame->setFocusPolicy(Qt::NoFocus);

    QPalette hud_palette = frame->palette();
    QColor background = hud_palette.color(QPalette::Window);
    background.setAlpha(220);
    hud_palette.setColor(QPalette::Window, background);
    frame->setPalette(hud_palette);
}

class ResettableVerticalSlider final : public QSlider
{
public:
    explicit ResettableVerticalSlider(QWidget *parent = nullptr)
        : QSlider(Qt::Vertical, parent)
    {
    }

    void setResetCallback(const std::function<void()> &callback)
    {
        this->reset_callback = callback;
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::RightButton)
        {
            if (this->reset_callback)
                this->reset_callback();
            event->accept();
            return;
        }

        QSlider::mousePressEvent(event);
    }

private:
    std::function<void()> reset_callback;
};

class MapCompassWidget final : public QWidget
{
public:
    explicit MapCompassWidget(MapModel *map_model, QWidget *parent = nullptr)
        : QWidget(parent),
          map_model(map_model),
          north_animation(new QVariantAnimation(this))
    {
        Q_ASSERT(this->map_model != nullptr);

        setFixedSize(CompassDiameterPx, CompassDiameterPx);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setCursor(Qt::OpenHandCursor);
        setToolTip(QStringLiteral(
            "Compass\n"
            "Click: animate back to north\n"
            "Drag or mouse wheel: rotate map"));
        setFocusPolicy(Qt::NoFocus);

        this->wheel_rotate_end_timer.setSingleShot(true);
        this->wheel_rotate_end_timer.setInterval(CompassWheelRotateEndDelayMs);
        connect(&this->wheel_rotate_end_timer, &QTimer::timeout, this, [this]
        {
            finishWheelRotation();
        });

        this->north_animation->setDuration(NorthAnimationDurationMs);
        this->north_animation->setStartValue(0.0);
        this->north_animation->setEndValue(1.0);
        this->north_animation->setEasingCurve(QEasingCurve::InOutCubic);
        connect(this->north_animation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value)
        {
            const double progress = value.toDouble();
            const double progress_delta = progress - this->north_animation_last_progress;
            this->north_animation_last_progress = progress;
            this->map_model->orbitView3d(
                this->north_animation_delta_yaw_deg * progress_delta, 0.0);
        });

        connect(this->north_animation, &QVariantAnimation::stateChanged, this,
                [this](QAbstractAnimation::State new_state, QAbstractAnimation::State old_state)
        {
            if (old_state == QAbstractAnimation::Stopped
                && new_state != QAbstractAnimation::Stopped)
            {
                this->map_model->beginView3dRotateInteraction();
            }
            else if (old_state != QAbstractAnimation::Stopped
                     && new_state == QAbstractAnimation::Stopped)
            {
                this->map_model->endView3dRotateInteraction();
            }
        });

        connect(this->map_model, &MapModel::view3dCameraChanged, this, [this]
        {
            update();
        });
    }

    ~MapCompassWidget() override
    {
        finishWheelRotation();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF outer_rect = rect().adjusted(2.5, 2.5, -2.5, -2.5);
        const QPointF center = outer_rect.center();
        const double radius = outer_rect.width() / 2.0;

        QColor shadow = palette().color(QPalette::Shadow);
        shadow.setAlpha(85);
        painter.setPen(Qt::NoPen);
        painter.setBrush(shadow);
        painter.drawEllipse(outer_rect.translated(0.0, 1.5));

        QColor center_color = palette().color(QPalette::Base);
        center_color.setAlpha(245);
        QColor edge_color = palette().color(QPalette::Window);
        edge_color.setAlpha(225);
        QRadialGradient face_gradient(center, radius);
        face_gradient.setColorAt(0.0, center_color);
        face_gradient.setColorAt(1.0, edge_color);
        painter.setBrush(face_gradient);
        painter.setPen(QPen(palette().color(QPalette::Mid), 1.2));
        painter.drawEllipse(outer_rect);

        const QRectF ring_rect = outer_rect.adjusted(5.0, 5.0, -5.0, -5.0);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(palette().color(QPalette::Midlight), 1.0));
        painter.drawEllipse(ring_rect);

        painter.save();
        painter.translate(center);
        painter.rotate(this->map_model->view3dYawDeg());

        const double tick_outer = ring_rect.width() / 2.0 - 1.5;
        for (int tick = 0; tick < 24; ++tick)
        {
            const bool major = tick % 6 == 0;
            const bool medium = !major && tick % 3 == 0;
            const double tick_length = major ? 7.0 : (medium ? 5.0 : 3.0);
            painter.setPen(QPen(
                major ? palette().color(QPalette::Text) : palette().color(QPalette::Mid),
                major ? 1.5 : 1.0));
            painter.drawLine(
                QPointF(0.0, -tick_outer),
                QPointF(0.0, -tick_outer + tick_length));
            painter.rotate(15.0);
        }

        QFont cardinal_font = font();
        cardinal_font.setBold(true);
        cardinal_font.setPointSizeF(qMax(7.5, cardinal_font.pointSizeF() - 0.5));
        painter.setFont(cardinal_font);

        const QColor accent = palette().color(QPalette::Highlight);
        painter.setPen(accent);
        painter.drawText(QRectF(-12.0, -radius + 10.0, 24.0, 16.0),
                         Qt::AlignCenter, QStringLiteral("N"));
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(QRectF(radius - 25.0, -8.0, 20.0, 16.0),
                         Qt::AlignCenter, QStringLiteral("E"));
        painter.drawText(QRectF(-10.0, radius - 26.0, 20.0, 16.0),
                         Qt::AlignCenter, QStringLiteral("S"));
        painter.drawText(QRectF(-radius + 5.0, -8.0, 20.0, 16.0),
                         Qt::AlignCenter, QStringLiteral("W"));

        QPolygonF north_needle;
        north_needle << QPointF(0.0, -radius + 27.0)
                     << QPointF(-5.0, 4.0)
                     << QPointF(5.0, 4.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawPolygon(north_needle);

        QPolygonF south_needle;
        south_needle << QPointF(0.0, radius - 19.0)
                     << QPointF(-4.0, -4.0)
                     << QPointF(4.0, -4.0);
        painter.setBrush(palette().color(QPalette::Mid));
        painter.drawPolygon(south_needle);
        painter.restore();

        painter.setPen(QPen(palette().color(QPalette::Text), 1.0));
        painter.setBrush(palette().color(QPalette::Window));
        painter.drawEllipse(center, 3.5, 3.5);

        QPolygonF heading_marker;
        heading_marker << QPointF(center.x(), outer_rect.top() + 1.0)
                       << QPointF(center.x() - 4.0, outer_rect.top() + 7.0)
                       << QPointF(center.x() + 4.0, outer_rect.top() + 7.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::Text));
        painter.drawPolygon(heading_marker);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
        {
            QWidget::mousePressEvent(event);
            return;
        }

        stopNorthAnimation();
        finishWheelRotation();
        this->map_model->beginView3dRotateInteraction();
        this->drag_active = true;
        this->dragged = false;
        this->drag_distance_px = 0;
#ifdef Q_OS_WASM
        this->last_drag_position = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
#else
        this->drag_restore_global = event->globalPosition().toPoint();
        this->drag_anchor_global = mapToGlobal(rect().center());
        grabMouse(QCursor(Qt::BlankCursor));
        this->drag_mouse_grabbed = true;
        QCursor::setPos(this->drag_anchor_global);
#endif
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!this->drag_active || !(event->buttons() & Qt::LeftButton))
        {
            if (this->drag_active)
                finishDrag();
            QWidget::mouseMoveEvent(event);
            return;
        }

#ifdef Q_OS_WASM
        const QPoint current_position = event->position().toPoint();
        const QPoint delta = current_position - this->last_drag_position;
        this->last_drag_position = current_position;
#else
        const QPoint global_position = event->globalPosition().toPoint();
        const QPoint delta = global_position - this->drag_anchor_global;
#endif
        if (!delta.isNull())
        {
            this->drag_distance_px += std::abs(delta.x()) + std::abs(delta.y());
            if (this->drag_distance_px >= CompassDragThresholdPx)
                this->dragged = true;

            const QPoint yaw_delta(delta.x(), 0);
            this->map_model->orbitView3dByPointerDelta(yaw_delta, false);
#ifndef Q_OS_WASM
            QCursor::setPos(this->drag_anchor_global);
#endif
        }

        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !this->drag_active)
        {
            QWidget::mouseReleaseEvent(event);
            return;
        }

        const bool was_dragged = this->dragged;
        finishDrag();
        if (!was_dragged)
            animateNorth();
        event->accept();
    }

    void wheelEvent(QWheelEvent *event) override
    {
        stopNorthAnimation();
        const QPoint angle_delta = event->angleDelta();
        if (angle_delta.y() == 0)
        {
            event->ignore();
            return;
        }

        if (!this->wheel_rotate_active)
        {
            this->map_model->beginView3dRotateInteraction();
            this->wheel_rotate_active = true;
        }

        const double steps = double(angle_delta.y()) / 120.0;
        this->map_model->orbitView3d(steps * CompassWheelStepDeg, 0.0);
        this->wheel_rotate_end_timer.start();
        event->accept();
    }

private:
    void finishWheelRotation()
    {
        if (!this->wheel_rotate_active)
            return;

        this->wheel_rotate_end_timer.stop();
        this->wheel_rotate_active = false;
        this->map_model->endView3dRotateInteraction();
    }

    void finishDrag()
    {
        if (!this->drag_active
#ifndef Q_OS_WASM
            && !this->drag_mouse_grabbed
#endif
        )
        {
            return;
        }

        this->drag_active = false;
        this->map_model->endView3dRotateInteraction();
#ifndef Q_OS_WASM
        if (this->drag_mouse_grabbed)
        {
            releaseMouse();
            this->drag_mouse_grabbed = false;
        }
        QCursor::setPos(this->drag_restore_global);
#endif
        setCursor(Qt::OpenHandCursor);
    }

    void animateNorth()
    {
        stopNorthAnimation();
        this->north_animation_start_yaw_deg = this->map_model->view3dYawDeg();
        this->north_animation_delta_yaw_deg = std::remainder(
            -this->north_animation_start_yaw_deg, 360.0);
        if (std::abs(this->north_animation_delta_yaw_deg) < 0.01)
            return;

        this->north_animation_last_progress = 0.0;
        this->north_animation->start();
    }

    void stopNorthAnimation()
    {
        if (this->north_animation->state() != QAbstractAnimation::Stopped)
            this->north_animation->stop();
    }

    MapModel *map_model = nullptr;
    QVariantAnimation *north_animation = nullptr;
    bool drag_active = false;
    bool dragged = false;
    int drag_distance_px = 0;
    QTimer wheel_rotate_end_timer;
    bool wheel_rotate_active = false;
#ifdef Q_OS_WASM
    QPoint last_drag_position;
#else
    QPoint drag_restore_global;
    QPoint drag_anchor_global;
    bool drag_mouse_grabbed = false;
#endif
    double north_animation_start_yaw_deg = 0.0;
    double north_animation_delta_yaw_deg = 0.0;
    double north_animation_last_progress = 0.0;
};
}

MapMonitorViewModeHudWidget::MapMonitorViewModeHudWidget(
    MapModel *map_model, QWidget *parent)
    : QFrame(parent),
      map_model(map_model),
      view_mode_combo(new QComboBox(this))
{
    Q_ASSERT(this->map_model != nullptr);
    configureHudFrame(this);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(HudMarginPx, HudMarginPx, HudMarginPx, HudMarginPx);
    layout->setSpacing(0);

    this->view_mode_combo->addItem(QStringLiteral("2D"), int(MapViewMode::TwoD));
    this->view_mode_combo->addItem(QStringLiteral("3D"), int(MapViewMode::ThreeD));
    this->view_mode_combo->setMinimumWidth(72);
    this->view_mode_combo->setToolTip(QStringLiteral(
        "Switch between the top-down 2D map and the 3D map view."));
    layout->addWidget(this->view_mode_combo);

    const int initial_index = this->view_mode_combo->findData(int(this->map_model->viewMode()));
    if (initial_index >= 0)
        this->view_mode_combo->setCurrentIndex(initial_index);

    connect(this->view_mode_combo, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        const QVariant data = this->view_mode_combo->itemData(index);
        if (data.isValid())
            this->map_model->setViewMode(static_cast<MapViewMode>(data.toInt()));
    });
    connect(this->map_model, &MapModel::viewModeChanged, this, [this](MapViewMode view_mode)
    {
        const int index = this->view_mode_combo->findData(int(view_mode));
        if (index < 0 || index == this->view_mode_combo->currentIndex())
            return;

        const QSignalBlocker blocker(this->view_mode_combo);
        this->view_mode_combo->setCurrentIndex(index);
    });
}

MapMonitorCompassHudWidget::MapMonitorCompassHudWidget(
    MapModel *map_model, QWidget *parent)
    : QWidget(parent)
{
    Q_ASSERT(map_model != nullptr);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::NoFocus);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(new MapCompassWidget(map_model, this));
}

MapMonitorCameraDistanceHudWidget::MapMonitorCameraDistanceHudWidget(
    MapModel *map_model, QWidget *parent)
    : QFrame(parent),
      map_model(map_model),
      distance_slider(new ResettableVerticalSlider(this)),
      distance_maximum_label(new QLabel(this)),
      distance_value_label(new QLabel(this))
{
    Q_ASSERT(this->map_model != nullptr);
    configureHudFrame(this);
    this->setMinimumWidth(CameraControlMinimumWidthPx);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 5, 4, 5);
    layout->setSpacing(1);

    this->distance_maximum_label->setAlignment(Qt::AlignHCenter);
    QLabel *minimum_label = new QLabel(QStringLiteral("150 m"), this);
    minimum_label->setAlignment(Qt::AlignHCenter);
    this->distance_value_label->setAlignment(Qt::AlignHCenter);

    const double initial_maximum_distance_m =
        this->map_model->view3dMaximumCameraDistanceM();
    this->distance_maximum_label->setText(
        cameraDistanceMaximumText(initial_maximum_distance_m));
    this->distance_slider->setRange(0, CameraDistanceSliderSteps);
    this->distance_slider->setValue(cameraDistanceSliderValue(
        this->map_model->view3dCameraDistanceM(), initial_maximum_distance_m));
    this->distance_slider->setFixedHeight(CameraControlSliderHeightPx);
    this->distance_slider->setToolTip(QStringLiteral(
        "Distance to focus\n"
        "Straight-line orbit radius to the terrain point under the crosshair\n"
        "Minimum requested distance: 150 m\n"
        "Maximum: native camera distance + 500 m, extended automatically for farther captured focus points\n"
        "Terrain collision keeps at least 2 m ground clearance without moving the focus point\n"
        "Right-click: animate back to the native camera distance"));
    this->distance_value_label->setText(cameraDistanceText(
        this->map_model->view3dCameraDistanceM()));

    layout->addWidget(this->distance_maximum_label);
    layout->addWidget(this->distance_slider, 1, Qt::AlignHCenter);
    layout->addWidget(minimum_label);
    layout->addWidget(this->distance_value_label);

    QVariantAnimation *distance_reset_animation = new QVariantAnimation(this);
    distance_reset_animation->setDuration(SliderResetAnimationDurationMs);
    distance_reset_animation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(distance_reset_animation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value)
    {
        this->map_model->setView3dCameraDistanceM(value.toDouble());
    });
    connect(distance_reset_animation, &QVariantAnimation::stateChanged, this,
            [this](QAbstractAnimation::State new_state, QAbstractAnimation::State old_state)
    {
        if (old_state == QAbstractAnimation::Stopped
            && new_state != QAbstractAnimation::Stopped)
        {
            this->map_model->beginView3dRotateInteraction();
        }
        else if (old_state != QAbstractAnimation::Stopped
                 && new_state == QAbstractAnimation::Stopped)
        {
            this->map_model->endView3dRotateInteraction();
        }
    });
    ResettableVerticalSlider *resettable_distance_slider =
        static_cast<ResettableVerticalSlider *>(this->distance_slider);
    resettable_distance_slider->setResetCallback([this, distance_reset_animation]
    {
        distance_reset_animation->stop();
        distance_reset_animation->setStartValue(this->map_model->view3dCameraDistanceM());
        distance_reset_animation->setEndValue(
            this->map_model->view3dNativeCameraDistanceM());
        distance_reset_animation->start();
    });
    connect(this->distance_slider, &QSlider::sliderPressed, distance_reset_animation,
            &QVariantAnimation::stop);

    connect(this->distance_slider, &QSlider::sliderPressed, this, [this]
    {
        this->map_model->beginView3dRotateInteraction();
    });
    connect(this->distance_slider, &QSlider::sliderReleased, this, [this]
    {
        this->map_model->endView3dRotateInteraction();
    });

    connect(this->distance_slider, &QSlider::valueChanged, this, [this](int slider_value)
    {
        this->map_model->setView3dCameraDistanceM(cameraDistanceMeters(
            slider_value, this->map_model->view3dMaximumCameraDistanceM()));
    });
    connect(this->map_model, &MapModel::view3dCameraChanged, this, [this]
    {
        const double distance_m = this->map_model->view3dCameraDistanceM();
        const double maximum_distance_m =
            this->map_model->view3dMaximumCameraDistanceM();
        const int slider_value = cameraDistanceSliderValue(
            distance_m, maximum_distance_m);
        if (this->distance_slider->value() != slider_value)
        {
            const QSignalBlocker blocker(this->distance_slider);
            this->distance_slider->setValue(slider_value);
        }
        this->distance_maximum_label->setText(
            cameraDistanceMaximumText(maximum_distance_m));
        this->distance_value_label->setText(cameraDistanceText(distance_m));
    });
}

MapMonitorTiltHudWidget::MapMonitorTiltHudWidget(MapModel *map_model, QWidget *parent)
    : QFrame(parent),
      map_model(map_model),
      tilt_slider(new ResettableVerticalSlider(this)),
      tilt_value_label(new QLabel(this))
{
    Q_ASSERT(this->map_model != nullptr);
    configureHudFrame(this);
    this->setMinimumWidth(CameraControlMinimumWidthPx);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 5, 4, 5);
    layout->setSpacing(1);

    QLabel *maximum_label = new QLabel(QStringLiteral("90°"), this);
    maximum_label->setAlignment(Qt::AlignHCenter);
    QLabel *minimum_label = new QLabel(QStringLiteral("0°"), this);
    minimum_label->setAlignment(Qt::AlignHCenter);
    this->tilt_value_label->setAlignment(Qt::AlignHCenter);

    this->tilt_slider->setRange(
        qRound(MapModel::MinView3dPitchDeg), qRound(MapModel::MaxView3dPitchDeg));
    this->tilt_slider->setValue(qRound(this->map_model->view3dPitchDeg()));
    this->tilt_slider->setFixedHeight(CameraControlSliderHeightPx);
    this->tilt_slider->setToolTip(QStringLiteral(
        "Camera tilt\n0° = horizon\n90° = straight down\n"
        "Right-click: animate back to the default tilt"));
    this->tilt_value_label->setText(
        QStringLiteral("%1°").arg(qRound(this->map_model->view3dPitchDeg())));

    layout->addWidget(maximum_label);
    layout->addWidget(this->tilt_slider, 1, Qt::AlignHCenter);
    layout->addWidget(minimum_label);
    layout->addWidget(this->tilt_value_label);

    QVariantAnimation *tilt_reset_animation = new QVariantAnimation(this);
    tilt_reset_animation->setDuration(SliderResetAnimationDurationMs);
    tilt_reset_animation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(tilt_reset_animation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value)
    {
        this->map_model->setView3dPitchDeg(value.toDouble());
    });
    connect(tilt_reset_animation, &QVariantAnimation::stateChanged, this,
            [this](QAbstractAnimation::State new_state, QAbstractAnimation::State old_state)
    {
        if (old_state == QAbstractAnimation::Stopped
            && new_state != QAbstractAnimation::Stopped)
        {
            this->map_model->beginView3dRotateInteraction();
        }
        else if (old_state != QAbstractAnimation::Stopped
                 && new_state == QAbstractAnimation::Stopped)
        {
            this->map_model->endView3dRotateInteraction();
        }
    });
    ResettableVerticalSlider *resettable_tilt_slider =
        static_cast<ResettableVerticalSlider *>(this->tilt_slider);
    resettable_tilt_slider->setResetCallback([this, tilt_reset_animation]
    {
        tilt_reset_animation->stop();
        tilt_reset_animation->setStartValue(this->map_model->view3dPitchDeg());
        tilt_reset_animation->setEndValue(MapModel::DefaultView3dPitchDeg);
        tilt_reset_animation->start();
    });
    connect(this->tilt_slider, &QSlider::sliderPressed, tilt_reset_animation,
            &QVariantAnimation::stop);

    connect(this->tilt_slider, &QSlider::sliderPressed, this, [this]
    {
        this->map_model->beginView3dRotateInteraction();
    });
    connect(this->tilt_slider, &QSlider::sliderReleased, this, [this]
    {
        this->map_model->endView3dRotateInteraction();
    });

    connect(this->tilt_slider, &QSlider::valueChanged, this, [this](int pitch_deg)
    {
        this->map_model->setView3dPitchDeg(double(pitch_deg));
    });
    connect(this->map_model, &MapModel::view3dCameraChanged, this, [this]
    {
        const int pitch_deg = qRound(this->map_model->view3dPitchDeg());
        if (this->tilt_slider->value() != pitch_deg)
        {
            const QSignalBlocker blocker(this->tilt_slider);
            this->tilt_slider->setValue(pitch_deg);
        }
        this->tilt_value_label->setText(QStringLiteral("%1°").arg(pitch_deg));
    });
}

MapMonitorNetworkGroundOffsetHudWidget::MapMonitorNetworkGroundOffsetHudWidget(
    MapModel *map_model, QWidget *parent)
    : QFrame(parent),
      map_model(map_model),
      offset_slider(new ResettableVerticalSlider(this)),
      offset_value_label(new QLabel(this))
{
    Q_ASSERT(this->map_model != nullptr);
    configureHudFrame(this);
    this->setMinimumWidth(CameraControlMinimumWidthPx);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 5, 4, 5);
    layout->setSpacing(1);

    QLabel *maximum_label = new QLabel(
        QStringLiteral("%1 m").arg(
            MapModel::MaxView3dNetworkGroundOffsetM, 0, 'f', 0), this);
    maximum_label->setAlignment(Qt::AlignHCenter);
    QLabel *minimum_label = new QLabel(QStringLiteral("0 m"), this);
    minimum_label->setAlignment(Qt::AlignHCenter);
    this->offset_value_label->setAlignment(Qt::AlignHCenter);

    this->offset_slider->setRange(0, NetworkGroundOffsetSliderSteps);
    this->offset_slider->setSingleStep(1);
    this->offset_slider->setPageStep(5);
    this->offset_slider->setFixedHeight(CameraControlSliderHeightPx);
    this->offset_slider->setValue(qRound(
        this->map_model->view3dNetworkGroundOffsetM() * 10.0));
    this->offset_slider->setToolTip(QStringLiteral(
        "Water network height above its real model elevation\n"
        "0 m = true elevation\n"
        "Up to %1 m render-only lift to avoid terrain z-fighting\n"
        "Right-click: animate back to 0 m")
        .arg(MapModel::MaxView3dNetworkGroundOffsetM, 0, 'f', 0));
    this->offset_value_label->setText(QStringLiteral("%1 m").arg(
        this->map_model->view3dNetworkGroundOffsetM(), 0, 'f', 1));

    layout->addWidget(maximum_label);
    layout->addWidget(this->offset_slider, 1, Qt::AlignHCenter);
    layout->addWidget(minimum_label);
    layout->addWidget(this->offset_value_label);

    QVariantAnimation *reset_animation = new QVariantAnimation(this);
    reset_animation->setDuration(SliderResetAnimationDurationMs);
    reset_animation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(reset_animation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value)
    {
        this->map_model->setView3dNetworkGroundOffsetM(value.toDouble());
    });

    ResettableVerticalSlider *resettable_slider =
        static_cast<ResettableVerticalSlider *>(this->offset_slider);
    resettable_slider->setResetCallback([this, reset_animation]
    {
        reset_animation->stop();
        reset_animation->setStartValue(this->map_model->view3dNetworkGroundOffsetM());
        reset_animation->setEndValue(MapModel::MinView3dNetworkGroundOffsetM);
        reset_animation->start();
    });
    connect(this->offset_slider, &QSlider::sliderPressed, reset_animation,
            &QVariantAnimation::stop);

    connect(this->offset_slider, &QSlider::valueChanged, this, [this](int value)
    {
        this->map_model->setView3dNetworkGroundOffsetM(double(value) / 10.0);
    });
    connect(this->map_model, &MapModel::view3dNetworkGroundOffsetChanged, this,
            [this](double offset_m)
    {
        const int slider_value = qRound(offset_m * 10.0);
        if (this->offset_slider->value() != slider_value)
        {
            const QSignalBlocker blocker(this->offset_slider);
            this->offset_slider->setValue(slider_value);
        }
        this->offset_value_label->setText(
            QStringLiteral("%1 m").arg(offset_m, 0, 'f', 1));
    });
}
