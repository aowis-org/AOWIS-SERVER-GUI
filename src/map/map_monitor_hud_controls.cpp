#include "map_monitor_hud_controls.h"

#include "map_model.h"

#include <QAbstractAnimation>
#include <QColor>
#include <QComboBox>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPolygonF>
#include <QSignalBlocker>
#include <QSlider>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>

#include <cmath>

namespace
{
constexpr int HudMarginPx = 6;
constexpr int CompassDiameterPx = 72;
constexpr double CompassWheelStepDeg = 5.0;
constexpr int NorthAnimationDurationMs = 280;
constexpr int CompassDragThresholdPx = 4;

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
        setCursor(Qt::OpenHandCursor);
        setToolTip(QStringLiteral(
            "Compass\n"
            "Click: animate back to north\n"
            "Drag or mouse wheel: rotate map"));
        setFocusPolicy(Qt::NoFocus);

        this->north_animation->setDuration(NorthAnimationDurationMs);
        this->north_animation->setStartValue(0.0);
        this->north_animation->setEndValue(1.0);
        this->north_animation->setEasingCurve(QEasingCurve::InOutCubic);
        connect(this->north_animation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value)
        {
            const double progress = value.toDouble();
            this->map_model->setView3dYawDeg(
                this->north_animation_start_yaw_deg
                + this->north_animation_delta_yaw_deg * progress);
        });

        connect(this->map_model, &MapModel::view3dCameraChanged, this, [this]
        {
            update();
        });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF dial_rect = rect().adjusted(4, 4, -4, -4);
        QColor dial_background = palette().color(QPalette::Base);
        dial_background.setAlpha(225);
        painter.setPen(QPen(palette().color(QPalette::Mid), 1.0));
        painter.setBrush(dial_background);
        painter.drawEllipse(dial_rect);

        const QPointF center = dial_rect.center();
        painter.save();
        painter.translate(center);
        painter.rotate(-this->map_model->view3dYawDeg());

        QFont north_font = font();
        north_font.setBold(true);
        north_font.setPointSizeF(qMax(8.0, north_font.pointSizeF()));
        painter.setFont(north_font);
        painter.setPen(palette().color(QPalette::Text));
        const QRectF north_rect(-14.0, -dial_rect.height() / 2.0 + 6.0, 28.0, 18.0);
        painter.drawText(north_rect, Qt::AlignCenter, QStringLiteral("N"));

        QPolygonF north_pointer;
        north_pointer << QPointF(0.0, -dial_rect.height() / 2.0 + 24.0)
                      << QPointF(-5.0, -4.0)
                      << QPointF(5.0, -4.0);
        painter.setBrush(palette().color(QPalette::Text));
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(north_pointer);

        QPolygonF south_pointer;
        south_pointer << QPointF(0.0, dial_rect.height() / 2.0 - 9.0)
                      << QPointF(-4.0, 4.0)
                      << QPointF(4.0, 4.0);
        QColor south_color = palette().color(QPalette::Mid);
        painter.setBrush(south_color);
        painter.drawPolygon(south_pointer);
        painter.restore();

        painter.setPen(QPen(palette().color(QPalette::Text), 1.5));
        painter.setBrush(palette().color(QPalette::Window));
        painter.drawEllipse(center, 3.0, 3.0);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
        {
            QWidget::mousePressEvent(event);
            return;
        }

        stopNorthAnimation();
        this->drag_active = true;
        this->dragged = false;
        this->press_position = event->position();
        this->last_drag_angle_deg = angleForPosition(event->position());
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!this->drag_active || !(event->buttons() & Qt::LeftButton))
        {
            QWidget::mouseMoveEvent(event);
            return;
        }

        if (QLineF(this->press_position, event->position()).length() >= CompassDragThresholdPx)
            this->dragged = true;

        const double current_angle_deg = angleForPosition(event->position());
        const double pointer_delta_deg = std::remainder(
            current_angle_deg - this->last_drag_angle_deg, 360.0);
        this->last_drag_angle_deg = current_angle_deg;
        if (std::isfinite(pointer_delta_deg))
        {
            this->map_model->setView3dYawDeg(
                this->map_model->view3dYawDeg() - pointer_delta_deg);
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

        this->drag_active = false;
        setCursor(Qt::OpenHandCursor);
        if (!this->dragged)
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

        const double steps = double(angle_delta.y()) / 120.0;
        this->map_model->setView3dYawDeg(
            this->map_model->view3dYawDeg() + steps * CompassWheelStepDeg);
        event->accept();
    }

private:
    double angleForPosition(const QPointF &position) const
    {
        const QPointF center(width() / 2.0, height() / 2.0);
        const QPointF offset = position - center;
        if (std::abs(offset.x()) < 0.001 && std::abs(offset.y()) < 0.001)
            return this->last_drag_angle_deg;
        return qRadiansToDegrees(std::atan2(offset.x(), -offset.y()));
    }

    void animateNorth()
    {
        stopNorthAnimation();
        this->north_animation_start_yaw_deg = this->map_model->view3dYawDeg();
        this->north_animation_delta_yaw_deg = std::remainder(
            -this->north_animation_start_yaw_deg, 360.0);
        if (std::abs(this->north_animation_delta_yaw_deg) < 0.01)
        {
            this->map_model->setView3dYawDeg(0.0);
            return;
        }
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
    QPointF press_position;
    double last_drag_angle_deg = 0.0;
    double north_animation_start_yaw_deg = 0.0;
    double north_animation_delta_yaw_deg = 0.0;
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

MapMonitorCameraHudWidget::MapMonitorCameraHudWidget(MapModel *map_model, QWidget *parent)
    : QFrame(parent),
      map_model(map_model),
      tilt_slider(new QSlider(Qt::Vertical, this)),
      tilt_value_label(new QLabel(this))
{
    Q_ASSERT(this->map_model != nullptr);
    configureHudFrame(this);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(HudMarginPx, HudMarginPx, HudMarginPx, HudMarginPx);
    layout->setSpacing(7);

    MapCompassWidget *compass = new MapCompassWidget(this->map_model, this);
    layout->addWidget(compass, 0, Qt::AlignBottom);

    QVBoxLayout *tilt_layout = new QVBoxLayout();
    tilt_layout->setContentsMargins(0, 0, 0, 0);
    tilt_layout->setSpacing(1);

    QLabel *maximum_label = new QLabel(QStringLiteral("90°"), this);
    maximum_label->setAlignment(Qt::AlignHCenter);
    QLabel *minimum_label = new QLabel(QStringLiteral("0°"), this);
    minimum_label->setAlignment(Qt::AlignHCenter);
    this->tilt_value_label->setAlignment(Qt::AlignHCenter);

    this->tilt_slider->setRange(
        qRound(MapModel::MinView3dPitchDeg), qRound(MapModel::MaxView3dPitchDeg));
    this->tilt_slider->setValue(qRound(this->map_model->view3dPitchDeg()));
    this->tilt_slider->setFixedHeight(92);
    this->tilt_slider->setToolTip(QStringLiteral(
        "Camera tilt\n0° = horizon\n90° = straight down"));
    this->tilt_value_label->setText(
        QStringLiteral("%1°").arg(qRound(this->map_model->view3dPitchDeg())));

    tilt_layout->addWidget(maximum_label);
    tilt_layout->addWidget(this->tilt_slider, 1, Qt::AlignHCenter);
    tilt_layout->addWidget(minimum_label);
    tilt_layout->addWidget(this->tilt_value_label);
    layout->addLayout(tilt_layout);

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
