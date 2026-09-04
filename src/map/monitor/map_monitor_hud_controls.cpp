#include "map/monitor/map_monitor_hud_controls.h"

#include "map/core/map_model.h"
#include "map/rhi/map_rhi_widget.h"
#include "map/core/map_scale_renderer.h"
#include "map/data/map_tile_repository.h"
#include "map/data/map_terrain_repository.h"

#include <QAbstractAnimation>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCursor>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPolygonF>
#include <QProgressBar>
#include <QPushButton>
#include <QRadialGradient>
#include <QSignalBlocker>
#include <QSizePolicy>
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
constexpr int CameraControlSliderHeightPx = 74;
constexpr int CameraControlWidthPx = 56;
constexpr int ScaleHudWidthPx = 116;
constexpr int ScaleHudHeightPx = 42;
constexpr int ScaleHudPaddingPx = 6;
constexpr int ScaleHudTickHeightPx = 6;
constexpr double View3dFieldOfViewDeg = 45.0;
constexpr int NetworkGroundOffsetSliderSteps =
    static_cast<int>(MapModel::MaxView3dNetworkGroundOffsetM * 10.0);
constexpr int VerticalExaggerationSliderScale = 10;

bool globeCameraActive(const MapModel *map_model)
{
    return map_model != nullptr && map_model->viewMode() == MapViewMode::Globe;
}

double cameraMinimumDistanceM(const MapModel *map_model)
{
    return globeCameraActive(map_model)
        ? MapModel::MinViewGlobeDistanceM
        : MapModel::MinView3dCameraDistanceM;
}

double cameraMaximumDistanceM(const MapModel *map_model)
{
    if (globeCameraActive(map_model))
        return MapModel::MaxViewGlobeDistanceM;
    return map_model != nullptr
        ? map_model->view3dMaximumCameraDistanceM()
        : MapModel::MinView3dCameraDistanceM;
}

double cameraDistanceM(const MapModel *map_model)
{
    if (map_model == nullptr)
        return MapModel::MinView3dCameraDistanceM;
    return globeCameraActive(map_model)
        ? map_model->viewGlobeDistanceM()
        : map_model->view3dCameraDistanceM();
}

int cameraDistanceSliderValue(const MapModel *map_model, double distance_m)
{
    const double minimum_distance_m = cameraMinimumDistanceM(map_model);
    const double maximum_distance_m = qMax(
        minimum_distance_m, cameraMaximumDistanceM(map_model));
    const double bounded_distance_m = qBound(
        minimum_distance_m, distance_m, maximum_distance_m);
    if (maximum_distance_m <= minimum_distance_m)
        return 0;

    double ratio = 0.0;
    if (globeCameraActive(map_model))
    {
        const double logarithmic_minimum = std::log(minimum_distance_m);
        const double logarithmic_maximum = std::log(maximum_distance_m);
        ratio = (std::log(bounded_distance_m) - logarithmic_minimum)
            / (logarithmic_maximum - logarithmic_minimum);
    }
    else
    {
        ratio = (bounded_distance_m - minimum_distance_m)
            / (maximum_distance_m - minimum_distance_m);
    }

    return qRound(qBound(0.0, ratio, 1.0) * CameraDistanceSliderSteps);
}

double cameraDistanceMeters(const MapModel *map_model, int slider_value)
{
    const double minimum_distance_m = cameraMinimumDistanceM(map_model);
    const double maximum_distance_m = qMax(
        minimum_distance_m, cameraMaximumDistanceM(map_model));
    const double ratio = qBound(0, slider_value, CameraDistanceSliderSteps)
        / double(CameraDistanceSliderSteps);

    if (globeCameraActive(map_model))
    {
        const double logarithmic_minimum = std::log(minimum_distance_m);
        const double logarithmic_maximum = std::log(maximum_distance_m);
        return std::exp(
            logarithmic_minimum + ratio * (logarithmic_maximum - logarithmic_minimum));
    }

    return minimum_distance_m + ratio * (maximum_distance_m - minimum_distance_m);
}

double cameraPitchDeg(const MapModel *map_model)
{
    if (map_model == nullptr)
        return MapModel::DefaultView3dPitchDeg;
    return globeCameraActive(map_model)
        ? map_model->viewGlobePitchDeg()
        : map_model->view3dPitchDeg();
}

double cameraMinimumPitchDeg(const MapModel *map_model)
{
    return globeCameraActive(map_model)
        ? MapModel::MinViewGlobePitchDeg
        : MapModel::MinView3dPitchDeg;
}

double cameraMaximumPitchDeg(const MapModel *map_model)
{
    return globeCameraActive(map_model)
        ? MapModel::MaxViewGlobePitchDeg
        : MapModel::MaxView3dPitchDeg;
}

double cameraDefaultPitchDeg(const MapModel *map_model)
{
    return globeCameraActive(map_model)
        ? MapModel::DefaultViewGlobePitchDeg
        : MapModel::DefaultView3dPitchDeg;
}

void setCameraDistanceM(MapModel *map_model, double distance_m)
{
    if (map_model == nullptr)
        return;
    if (globeCameraActive(map_model))
        map_model->setViewGlobeDistanceM(distance_m);
    else
        map_model->setView3dCameraDistanceM(distance_m);
}

void setCameraPitchDeg(MapModel *map_model, double pitch_deg)
{
    if (map_model == nullptr)
        return;
    if (globeCameraActive(map_model))
        map_model->setViewGlobePitchDeg(pitch_deg);
    else
        map_model->setView3dPitchDeg(pitch_deg);
}

QString cameraDistanceText(double distance_m)
{
    if (distance_m >= 10000.0)
        return QStringLiteral("%1 km").arg(QString::number(distance_m / 1000.0, 'f', 1));

    return QStringLiteral("%1 m").arg(qRound(distance_m));
}

QString cameraDistanceMaximumText(double distance_m)
{
    return cameraDistanceText(distance_m);
}

double normalizedHeadingDegrees(double yaw_deg)
{
    double heading_deg = std::fmod(-yaw_deg, 360.0);
    if (heading_deg < 0.0)
        heading_deg += 360.0;
    return heading_deg;
}

QString headingCardinal(double heading_deg)
{
    const int sector = int(std::floor((heading_deg + 22.5) / 45.0)) % 8;
    switch (sector)
    {
    case 0: return QStringLiteral("N");
    case 1: return QStringLiteral("NE");
    case 2: return QStringLiteral("E");
    case 3: return QStringLiteral("SE");
    case 4: return QStringLiteral("S");
    case 5: return QStringLiteral("SW");
    case 6: return QStringLiteral("W");
    case 7: return QStringLiteral("NW");
    default: return QStringLiteral("N");
    }
}

QString compassOrientationText(double yaw_deg)
{
    const double heading_deg = normalizedHeadingDegrees(yaw_deg);
    const int rounded_heading_deg = qRound(heading_deg) % 360;
    return QStringLiteral("%1  %2°")
        .arg(headingCardinal(heading_deg))
        .arg(rounded_heading_deg);
}

double compassYawDeg(const MapModel *map_model)
{
    Q_ASSERT(map_model != nullptr);
    if (map_model->viewMode() == MapViewMode::Globe)
        return map_model->viewGlobeYawDeg();
    return map_model->view3dYawDeg();
}

void beginCompassRotateInteraction(MapModel *map_model)
{
    Q_ASSERT(map_model != nullptr);
    if (map_model->viewMode() == MapViewMode::ThreeD)
        map_model->beginView3dRotateInteraction();
}

void endCompassRotateInteraction(MapModel *map_model)
{
    Q_ASSERT(map_model != nullptr);
    if (map_model->viewMode() == MapViewMode::ThreeD)
        map_model->endView3dRotateInteraction();
}

void orbitCompass(MapModel *map_model, double yaw_delta_deg)
{
    Q_ASSERT(map_model != nullptr);
    if (map_model->viewMode() == MapViewMode::Globe)
        map_model->orbitViewGlobe(yaw_delta_deg, 0.0);
    else if (map_model->viewMode() == MapViewMode::ThreeD)
        map_model->orbitView3d(yaw_delta_deg, 0.0);
}

void orbitCompassByPointerDelta(MapModel *map_model, const QPoint &delta_pixels)
{
    Q_ASSERT(map_model != nullptr);
    if (map_model->viewMode() == MapViewMode::Globe)
        map_model->orbitViewGlobeByPointerDelta(delta_pixels, false);
    else if (map_model->viewMode() == MapViewMode::ThreeD)
        map_model->orbitView3dByPointerDelta(delta_pixels, false);
}

void snapCompassNorth(MapModel *map_model)
{
    Q_ASSERT(map_model != nullptr);
    if (map_model->viewMode() == MapViewMode::Globe)
        map_model->setViewGlobeYawDeg(0.0);
    else if (map_model->viewMode() == MapViewMode::ThreeD)
        map_model->setView3dYawDeg(0.0);
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
            orbitCompass(
                this->map_model,
                this->north_animation_delta_yaw_deg * progress_delta);
        });

        connect(this->north_animation, &QVariantAnimation::stateChanged, this,
                [this](QAbstractAnimation::State new_state, QAbstractAnimation::State old_state)
        {
            if (old_state == QAbstractAnimation::Stopped
                && new_state != QAbstractAnimation::Stopped)
            {
                beginCompassRotateInteraction(this->map_model);
            }
            else if (old_state != QAbstractAnimation::Stopped
                     && new_state == QAbstractAnimation::Stopped)
            {
                endCompassRotateInteraction(this->map_model);
            }
        });
        connect(this->north_animation, &QAbstractAnimation::finished, this, [this]
        {
            snapCompassNorth(this->map_model);
        });

        connect(this->map_model, &MapModel::view3dCameraChanged, this, [this]
        {
            update();
        });
        connect(this->map_model, &MapModel::viewGlobeCameraChanged, this, [this]
        {
            update();
        });
        connect(this->map_model, &MapModel::viewModeChanged, this, [this](MapViewMode)
        {
            stopNorthAnimation();
            finishWheelRotation();
            if (this->drag_active)
                finishDrag();
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

        const QRectF outer_rect = QRectF(rect()).adjusted(2.5, 2.5, -2.5, -2.5);
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
        painter.rotate(compassYawDeg(this->map_model));

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
        beginCompassRotateInteraction(this->map_model);
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
            orbitCompassByPointerDelta(this->map_model, yaw_delta);
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
            beginCompassRotateInteraction(this->map_model);
            this->wheel_rotate_active = true;
        }

        const double steps = double(angle_delta.y()) / 120.0;
        orbitCompass(this->map_model, steps * CompassWheelStepDeg);
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
        endCompassRotateInteraction(this->map_model);
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
        endCompassRotateInteraction(this->map_model);
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
        this->north_animation_start_yaw_deg = compassYawDeg(this->map_model);
        this->north_animation_delta_yaw_deg = std::remainder(
            -this->north_animation_start_yaw_deg, 360.0);
        if (std::abs(this->north_animation_delta_yaw_deg) < 0.01)
        {
            snapCompassNorth(this->map_model);
            return;
        }

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

MapMonitorDownloadActivityHudWidget::MapMonitorDownloadActivityHudWidget(
    MapTileRepository *tile_repository, MapTerrainRepository *terrain_repository, QWidget *parent)
    : QWidget(parent),
      tile_repository(tile_repository),
      terrain_repository(terrain_repository),
      map_tiles_panel(new QFrame(this)),
      map_tiles_label(new QLabel(this->map_tiles_panel)),
      map_tiles_cancel(new QPushButton(QStringLiteral("Cancel"), this->map_tiles_panel)),
      terrain_panel(new QFrame(this)),
      terrain_label(new QLabel(this->terrain_panel)),
      terrain_cancel(new QPushButton(QStringLiteral("Cancel"), this->terrain_panel)),
      poll_timer(new QTimer(this))
{
    Q_ASSERT(this->tile_repository != nullptr);
    Q_ASSERT(this->terrain_repository != nullptr);

    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::NoFocus);

    QHBoxLayout *root_layout = new QHBoxLayout(this);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(8);

    configureHudFrame(this->map_tiles_panel);
    QHBoxLayout *map_layout = new QHBoxLayout(this->map_tiles_panel);
    map_layout->setContentsMargins(8, 5, 6, 5);
    map_layout->setSpacing(7);
    QLabel *map_title = new QLabel(QStringLiteral("Map tiles"), this->map_tiles_panel);
    QFont map_title_font = map_title->font();
    map_title_font.setBold(true);
    map_title->setFont(map_title_font);
    QProgressBar *map_progress = new QProgressBar(this->map_tiles_panel);
    map_progress->setRange(0, 0);
    map_progress->setTextVisible(false);
    map_progress->setFixedSize(54, 10);
    this->map_tiles_label->setMinimumWidth(82);
    this->map_tiles_cancel->setFocusPolicy(Qt::NoFocus);
    this->map_tiles_cancel->setToolTip(QStringLiteral(
        "Cancel internet downloads of raster map tiles into AOWIS-SERVER-MAP."));
    map_layout->addWidget(map_title);
    map_layout->addWidget(map_progress);
    map_layout->addWidget(this->map_tiles_label);
    map_layout->addWidget(this->map_tiles_cancel);

    configureHudFrame(this->terrain_panel);
    QHBoxLayout *terrain_layout = new QHBoxLayout(this->terrain_panel);
    terrain_layout->setContentsMargins(8, 5, 6, 5);
    terrain_layout->setSpacing(7);
    QLabel *terrain_title = new QLabel(QStringLiteral("Terrain"), this->terrain_panel);
    QFont terrain_title_font = terrain_title->font();
    terrain_title_font.setBold(true);
    terrain_title->setFont(terrain_title_font);
    QProgressBar *terrain_progress = new QProgressBar(this->terrain_panel);
    terrain_progress->setRange(0, 0);
    terrain_progress->setTextVisible(false);
    terrain_progress->setFixedSize(54, 10);
    this->terrain_label->setMinimumWidth(82);
    this->terrain_cancel->setFocusPolicy(Qt::NoFocus);
    this->terrain_cancel->setToolTip(QStringLiteral(
        "Cancel internet downloads of terrain source data into AOWIS-SERVER-MAP."));
    terrain_layout->addWidget(terrain_title);
    terrain_layout->addWidget(terrain_progress);
    terrain_layout->addWidget(this->terrain_label);
    terrain_layout->addWidget(this->terrain_cancel);

    root_layout->addWidget(this->map_tiles_panel);
    root_layout->addWidget(this->terrain_panel);
    this->map_tiles_panel->hide();
    this->terrain_panel->hide();
    hide();

    connect(this->tile_repository, &MapTileRepository::signalUpstreamActivityChanged,
            this, [this](const MapUpstreamActivity &activity)
    {
        setMapTileActivity(activity.active, activity.queued);
    });
    connect(this->terrain_repository, &MapTerrainRepository::signalUpstreamActivityChanged,
            this, [this](const MapUpstreamActivity &activity)
    {
        setTerrainActivity(activity.active, activity.queued);
    });
    connect(this->map_tiles_cancel, &QPushButton::clicked, this, [this]
    {
        this->map_tiles_cancel->setEnabled(false);
        this->tile_repository->cancelUpstreamDownloads();
        QTimer::singleShot(100, this, [this]
        {
            refreshActivity();
        });
    });
    connect(this->terrain_cancel, &QPushButton::clicked, this, [this]
    {
        this->terrain_cancel->setEnabled(false);
        this->terrain_repository->cancelUpstreamDownloads();
        QTimer::singleShot(100, this, [this]
        {
            refreshActivity();
        });
    });

    this->poll_timer->setInterval(400);
    connect(this->poll_timer, &QTimer::timeout, this, [this]
    {
        refreshActivity();
    });
}

void MapMonitorDownloadActivityHudWidget::setHudActive(bool active)
{
    this->hud_active = active;
    if (!this->hud_active)
    {
        this->poll_timer->stop();
        hide();
        return;
    }

    if (!this->poll_timer->isActive())
        this->poll_timer->start();
    refreshActivity();

    const bool any_busy = !this->map_tiles_panel->isHidden() || !this->terrain_panel->isHidden();
    setVisible(any_busy);
    if (!isVisible())
        return;

    adjustSize();
    QWidget *container = parentWidget();
    if (container != nullptr)
    {
        const int x = qMax(HudMarginPx, (container->width() - width()) / 2);
        move(x, HudMarginPx);
    }
    raise();
}

void MapMonitorDownloadActivityHudWidget::setMapTileActivity(int active, int queued)
{
    updatePanel(this->map_tiles_panel, this->map_tiles_label, this->map_tiles_cancel,
                QStringLiteral("Map tiles"), active, queued);
}

void MapMonitorDownloadActivityHudWidget::setTerrainActivity(int active, int queued)
{
    updatePanel(this->terrain_panel, this->terrain_label, this->terrain_cancel,
                QStringLiteral("Terrain"), active, queued);
}

void MapMonitorDownloadActivityHudWidget::updatePanel(
    QFrame *panel, QLabel *label, QPushButton *cancel_button, const QString &name,
    int active, int queued)
{
    const int bounded_active = qMax(0, active);
    const int bounded_queued = qMax(0, queued);
    const bool busy = bounded_active > 0 || bounded_queued > 0;
    panel->setVisible(busy);
    cancel_button->setEnabled(busy);

    if (bounded_queued > 0)
    {
        label->setText(QStringLiteral("%1 active · %2 queued")
                           .arg(bounded_active)
                           .arg(bounded_queued));
    }
    else
    {
        label->setText(QStringLiteral("%1 active").arg(bounded_active));
    }
    panel->setToolTip(QStringLiteral(
        "%1 internet → map server activity. Cached/local transfers are not counted.")
                          .arg(name));

    const bool any_busy = !this->map_tiles_panel->isHidden() || !this->terrain_panel->isHidden();
    setVisible(this->hud_active && any_busy);
    if (this->hud_active && any_busy)
    {
        adjustSize();
        QWidget *container = parentWidget();
        if (container != nullptr)
        {
            const int x = qMax(HudMarginPx, (container->width() - width()) / 2);
            move(x, HudMarginPx);
        }
        raise();
    }
}

void MapMonitorDownloadActivityHudWidget::refreshActivity()
{
    this->tile_repository->requestUpstreamActivity();
    this->terrain_repository->requestUpstreamActivity();
}

MapMonitorViewModeHudWidget::MapMonitorViewModeHudWidget(
    MapModel *map_model, MapRhiWidget *rhi_widget, QWidget *parent)
    : QFrame(parent),
      map_model(map_model),
      rhi_widget(rhi_widget),
      view_mode_combo(new QComboBox(this)),
      wireframe_checkbox(new QCheckBox(QStringLiteral("wireframe"), this)),
      map_checkbox(new QCheckBox(QStringLiteral("map"), this))
{
    Q_ASSERT(this->map_model != nullptr);
    Q_ASSERT(this->rhi_widget != nullptr);
    configureHudFrame(this);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(HudMarginPx, HudMarginPx, HudMarginPx, HudMarginPx);
    layout->setSpacing(8);

    this->view_mode_combo->addItem(QStringLiteral("2D"), int(MapViewMode::TwoD));
    this->view_mode_combo->addItem(QStringLiteral("3D"), int(MapViewMode::ThreeD));
    this->view_mode_combo->addItem(QStringLiteral("Globe"), int(MapViewMode::Globe));
    this->view_mode_combo->setMinimumWidth(72);
    this->view_mode_combo->setToolTip(QStringLiteral(
        "Switch between the top-down 2D map, the 3D map view, and the "
        "whole-planet WGS84 globe view."));
    layout->addWidget(this->view_mode_combo);

    this->wireframe_checkbox->setChecked(false);
    this->wireframe_checkbox->setFocusPolicy(Qt::NoFocus);
    this->wireframe_checkbox->setToolTip(QStringLiteral(
        "Draw the 3D terrain or globe surface mesh as a wireframe."));
    layout->addWidget(this->wireframe_checkbox);

    this->map_checkbox->setChecked(true);
    this->map_checkbox->setFocusPolicy(Qt::NoFocus);
    this->map_checkbox->setToolTip(QStringLiteral(
        "Draw map tiles as textures on the 3D terrain or globe surface."));
    layout->addWidget(this->map_checkbox);

    const int initial_index = this->view_mode_combo->findData(int(this->map_model->viewMode()));
    if (initial_index >= 0)
        this->view_mode_combo->setCurrentIndex(initial_index);

    connect(this->view_mode_combo, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        const QVariant data = this->view_mode_combo->itemData(index);
        if (data.isValid())
            this->map_model->setViewMode(static_cast<MapViewMode>(data.toInt()));
    });
    connect(this->wireframe_checkbox, &QCheckBox::toggled, this, [this](bool checked)
    {
        this->rhi_widget->setTerrainWireframeVisible(checked);
    });
    connect(this->map_checkbox, &QCheckBox::toggled, this, [this](bool checked)
    {
        this->rhi_widget->setMapTilesVisible(checked);
    });
    connect(this->map_model, &MapModel::viewModeChanged, this, [this](MapViewMode view_mode)
    {
        const int index = this->view_mode_combo->findData(int(view_mode));
        if (index >= 0 && index != this->view_mode_combo->currentIndex())
        {
            const QSignalBlocker blocker(this->view_mode_combo);
            this->view_mode_combo->setCurrentIndex(index);
        }
        update3dControlsVisibility();
    });

    this->rhi_widget->setTerrainWireframeVisible(this->wireframe_checkbox->isChecked());
    this->rhi_widget->setMapTilesVisible(this->map_checkbox->isChecked());
    update3dControlsVisibility();
}

void MapMonitorViewModeHudWidget::update3dControlsVisibility()
{
    const MapViewMode view_mode = this->map_model->viewMode();
    const bool visible =
        view_mode == MapViewMode::ThreeD || view_mode == MapViewMode::Globe;
    this->wireframe_checkbox->setVisible(visible);
    this->map_checkbox->setVisible(visible);
    adjustSize();
}

MapMonitorCompassHudWidget::MapMonitorCompassHudWidget(
    MapModel *map_model, QWidget *parent)
    : QWidget(parent)
{
    Q_ASSERT(map_model != nullptr);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::NoFocus);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);

    MapCompassWidget *compass_widget = new MapCompassWidget(map_model, this);
    QLabel *orientation_label = new QLabel(
        compassOrientationText(compassYawDeg(map_model)), this);
    configureHudFrame(orientation_label);
    orientation_label->setAlignment(Qt::AlignCenter);
    orientation_label->setMinimumWidth(CompassDiameterPx);
    orientation_label->setToolTip(QStringLiteral(
        "Current view heading at the top of the map."));

    layout->addWidget(compass_widget, 0, Qt::AlignHCenter);
    layout->addWidget(orientation_label, 0, Qt::AlignHCenter);

    const std::function<void()> update_orientation = [map_model, orientation_label]
    {
        orientation_label->setText(
            compassOrientationText(compassYawDeg(map_model)));
    };
    connect(map_model, &MapModel::view3dCameraChanged, this, update_orientation);
    connect(map_model, &MapModel::viewGlobeCameraChanged, this, update_orientation);
    connect(map_model, &MapModel::viewModeChanged, this,
            [update_orientation](MapViewMode)
    {
        update_orientation();
    });
}

MapMonitorScaleHudWidget::MapMonitorScaleHudWidget(
    MapModel *map_model, QWidget *parent)
    : QFrame(parent),
      map_model(map_model)
{
    Q_ASSERT(this->map_model != nullptr);
    configureHudFrame(this);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(ScaleHudWidthPx, ScaleHudHeightPx);
    setToolTip(QStringLiteral("Horizontal map scale."));

    connect(this->map_model, &MapModel::view3dCameraChanged, this, [this]
    {
        update();
    });
    connect(this->map_model, &MapModel::zoomChanged, this, [this]
    {
        update();
    });
    connect(this->map_model, &MapModel::view2dContinuousScaleChanged, this, [this](double)
    {
        update();
    });
    connect(this->map_model, &MapModel::centerChangedWGS84, this, [this]
    {
        update();
    });
}

void MapMonitorScaleHudWidget::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);

    if (this->map_model == nullptr)
        return;

    const QWidget *viewport_widget = parentWidget();
    const QSize viewport_size = viewport_widget != nullptr
        ? viewport_widget->size() : size();
    const int viewport_height = qMax(1, viewport_size.height());
    const int maximum_bar_width = qMax(1, width() - ScaleHudPaddingPx * 2);

    double maximum_distance_m = 0.0;
    if (this->map_model->viewMode() == MapViewMode::TwoD)
    {
        const int sample_y = viewport_height / 2;
        const int sample_x = qMax(0, (viewport_size.width() - maximum_bar_width) / 2);
        const CoordinateWGS84 sample_start = this->map_model->wgs84FromScreen(
            QPoint(sample_x, sample_y), viewport_size);
        const CoordinateWGS84 sample_end = this->map_model->wgs84FromScreen(
            QPoint(sample_x + maximum_bar_width, sample_y), viewport_size);
        maximum_distance_m = MapScaleRenderer::Detail::distanceMeters(
            sample_start, sample_end);
    }
    else
    {
        const double orbit_distance_m = qMax(
            MapModel::MinView3dCameraDistanceM,
            this->map_model->view3dCameraDistanceM());
        const double orbit_distance_world = this->map_model->view3dCameraDistanceWorld();
        const double world_units_per_meter = orbit_distance_world > 0.0
            ? orbit_distance_world / orbit_distance_m
            : 0.0;
        const double collision_lift_m = world_units_per_meter > 0.0
            ? this->map_model->view3dCameraCollisionLiftWorld() / world_units_per_meter
            : 0.0;
        const double pitch_rad = qDegreesToRadians(qBound(
            MapModel::MinView3dPitchDeg,
            this->map_model->view3dPitchDeg(),
            MapModel::MaxView3dPitchDeg));
        const double horizontal_distance_m = orbit_distance_m * std::cos(pitch_rad);
        const double vertical_distance_m =
            orbit_distance_m * std::sin(pitch_rad) + collision_lift_m;
        const double focus_distance_m = std::hypot(
            horizontal_distance_m, vertical_distance_m);
        const double meters_per_pixel = 2.0 * focus_distance_m
            * std::tan(qDegreesToRadians(View3dFieldOfViewDeg / 2.0))
            / double(viewport_height);
        maximum_distance_m = meters_per_pixel * maximum_bar_width;
    }
    const double scale_distance_m =
        MapScaleRenderer::Detail::roundedDistanceMeters(maximum_distance_m);
    if (!(maximum_distance_m > 0.0) || !(scale_distance_m > 0.0)
        || !std::isfinite(maximum_distance_m))
    {
        return;
    }

    const int scale_width = qBound(
        1,
        qRound(maximum_bar_width * scale_distance_m / maximum_distance_m),
        maximum_bar_width);
    const QString label = MapScaleRenderer::Detail::distanceLabel(scale_distance_m);
    const int scale_left = (width() - scale_width) / 2;
    const int scale_right = scale_left + scale_width;
    const int scale_bottom = height() - ScaleHudPaddingPx;
    const int scale_top = scale_bottom - ScaleHudTickHeightPx;

    QPainter painter(this);
    painter.setClipRegion(event->region());
    painter.setRenderHint(QPainter::Antialiasing, false);

    QFont label_font = painter.font();
    label_font.setPixelSize(11);
    painter.setFont(label_font);
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(
        QRect(ScaleHudPaddingPx, 2, width() - ScaleHudPaddingPx * 2, 18),
        Qt::AlignCenter, label);

    QPen scale_pen(palette().color(QPalette::Text));
    scale_pen.setWidth(2);
    painter.setPen(scale_pen);
    painter.drawLine(scale_left, scale_bottom, scale_right, scale_bottom);
    painter.drawLine(scale_left, scale_top, scale_left, scale_bottom);
    painter.drawLine(scale_right, scale_top, scale_right, scale_bottom);
}

MapMonitorCameraDistanceHudWidget::MapMonitorCameraDistanceHudWidget(
    MapModel *map_model, QWidget *parent)
    : QFrame(parent),
      map_model(map_model),
      distance_slider(new ResettableVerticalSlider(this)),
      distance_maximum_label(new QLabel(this)),
      distance_minimum_label(new QLabel(this)),
      distance_value_label(new QLabel(this))
{
    Q_ASSERT(this->map_model != nullptr);
    configureHudFrame(this);
    setFixedWidth(CameraControlWidthPx);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 3, 2, 3);
    layout->setSpacing(1);

    this->distance_maximum_label->setAlignment(Qt::AlignHCenter);
    this->distance_minimum_label->setAlignment(Qt::AlignHCenter);
    this->distance_value_label->setAlignment(Qt::AlignHCenter);

    this->distance_slider->setRange(0, CameraDistanceSliderSteps);
    this->distance_slider->setFixedHeight(CameraControlSliderHeightPx);

    layout->addWidget(this->distance_maximum_label);
    layout->addWidget(this->distance_slider, 1, Qt::AlignHCenter);
    layout->addWidget(this->distance_minimum_label);
    layout->addWidget(this->distance_value_label);

    QVariantAnimation *distance_reset_animation = new QVariantAnimation(this);
    distance_reset_animation->setDuration(SliderResetAnimationDurationMs);
    distance_reset_animation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(distance_reset_animation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value)
    {
        setCameraDistanceM(this->map_model, value.toDouble());
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

    const std::function<void()> sync_distance = [this]
    {
        const double minimum_distance_m = cameraMinimumDistanceM(this->map_model);
        const double maximum_distance_m = cameraMaximumDistanceM(this->map_model);
        const double distance_m = cameraDistanceM(this->map_model);
        const int slider_value = cameraDistanceSliderValue(this->map_model, distance_m);

        if (this->distance_slider->value() != slider_value)
        {
            const QSignalBlocker blocker(this->distance_slider);
            this->distance_slider->setValue(slider_value);
        }
        this->distance_maximum_label->setText(cameraDistanceMaximumText(maximum_distance_m));
        this->distance_minimum_label->setText(cameraDistanceText(minimum_distance_m));
        this->distance_value_label->setText(cameraDistanceText(distance_m));

        if (globeCameraActive(this->map_model))
        {
            this->distance_slider->setToolTip(QStringLiteral(
                "Distance to focus\n"
                "Straight-line orbit radius to the globe point under the crosshair\n"
                "The logarithmic slider spans the full Globe zoom range\n"
                "Right-click: animate back to the full-globe default distance"));
        }
        else
        {
            this->distance_slider->setToolTip(QStringLiteral(
                "Distance to focus\n"
                "Straight-line orbit radius to the terrain point under the crosshair\n"
                "Terrain collision keeps at least 2 m ground clearance without moving the focus point\n"
                "Right-click: animate back to the native camera distance"));
        }
    };

    ResettableVerticalSlider *resettable_distance_slider =
        static_cast<ResettableVerticalSlider *>(this->distance_slider);
    resettable_distance_slider->setResetCallback([this, distance_reset_animation]
    {
        distance_reset_animation->stop();
        distance_reset_animation->setStartValue(cameraDistanceM(this->map_model));
        const double reset_distance_m = globeCameraActive(this->map_model)
            ? MapModel::DefaultViewGlobeDistanceM
            : this->map_model->view3dNativeCameraDistanceM();
        distance_reset_animation->setEndValue(reset_distance_m);
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
        setCameraDistanceM(
            this->map_model, cameraDistanceMeters(this->map_model, slider_value));
    });
    connect(this->map_model, &MapModel::view3dCameraChanged, this, sync_distance);
    connect(this->map_model, &MapModel::viewGlobeCameraChanged, this, sync_distance);
    connect(this->map_model, &MapModel::viewModeChanged, this,
            [distance_reset_animation, sync_distance](MapViewMode)
    {
        distance_reset_animation->stop();
        sync_distance();
    });

    sync_distance();
}

MapMonitorTiltHudWidget::MapMonitorTiltHudWidget(MapModel *map_model, QWidget *parent)
    : QFrame(parent),
      map_model(map_model),
      tilt_slider(new ResettableVerticalSlider(this)),
      tilt_maximum_label(new QLabel(this)),
      tilt_minimum_label(new QLabel(this)),
      tilt_value_label(new QLabel(this))
{
    Q_ASSERT(this->map_model != nullptr);
    configureHudFrame(this);
    setFixedWidth(CameraControlWidthPx);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 3, 2, 3);
    layout->setSpacing(1);

    this->tilt_maximum_label->setAlignment(Qt::AlignHCenter);
    this->tilt_minimum_label->setAlignment(Qt::AlignHCenter);
    this->tilt_value_label->setAlignment(Qt::AlignHCenter);
    this->tilt_slider->setFixedHeight(CameraControlSliderHeightPx);

    layout->addWidget(this->tilt_maximum_label);
    layout->addWidget(this->tilt_slider, 1, Qt::AlignHCenter);
    layout->addWidget(this->tilt_minimum_label);
    layout->addWidget(this->tilt_value_label);

    QVariantAnimation *tilt_reset_animation = new QVariantAnimation(this);
    tilt_reset_animation->setDuration(SliderResetAnimationDurationMs);
    tilt_reset_animation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(tilt_reset_animation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value)
    {
        setCameraPitchDeg(this->map_model, value.toDouble());
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

    const std::function<void()> sync_tilt = [this]
    {
        const int minimum_pitch_deg = qRound(cameraMinimumPitchDeg(this->map_model));
        const int maximum_pitch_deg = qRound(cameraMaximumPitchDeg(this->map_model));
        const int pitch_deg = qRound(cameraPitchDeg(this->map_model));

        if (this->tilt_slider->minimum() != minimum_pitch_deg
            || this->tilt_slider->maximum() != maximum_pitch_deg)
        {
            const QSignalBlocker blocker(this->tilt_slider);
            this->tilt_slider->setRange(minimum_pitch_deg, maximum_pitch_deg);
        }
        if (this->tilt_slider->value() != pitch_deg)
        {
            const QSignalBlocker blocker(this->tilt_slider);
            this->tilt_slider->setValue(pitch_deg);
        }

        this->tilt_maximum_label->setText(QStringLiteral("%1°").arg(maximum_pitch_deg));
        this->tilt_minimum_label->setText(QStringLiteral("%1°").arg(minimum_pitch_deg));
        this->tilt_value_label->setText(QStringLiteral("%1°").arg(pitch_deg));
        this->tilt_slider->setToolTip(QStringLiteral(
            "Camera tilt\n%1° = near horizon\n%2° = straight down\n"
            "Right-click: animate back to the default tilt")
            .arg(minimum_pitch_deg)
            .arg(maximum_pitch_deg));
    };

    ResettableVerticalSlider *resettable_tilt_slider =
        static_cast<ResettableVerticalSlider *>(this->tilt_slider);
    resettable_tilt_slider->setResetCallback([this, tilt_reset_animation]
    {
        tilt_reset_animation->stop();
        tilt_reset_animation->setStartValue(cameraPitchDeg(this->map_model));
        tilt_reset_animation->setEndValue(cameraDefaultPitchDeg(this->map_model));
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
        setCameraPitchDeg(this->map_model, double(pitch_deg));
    });
    connect(this->map_model, &MapModel::view3dCameraChanged, this, sync_tilt);
    connect(this->map_model, &MapModel::viewGlobeCameraChanged, this, sync_tilt);
    connect(this->map_model, &MapModel::viewModeChanged, this,
            [tilt_reset_animation, sync_tilt](MapViewMode)
    {
        tilt_reset_animation->stop();
        sync_tilt();
    });

    sync_tilt();
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
    setFixedWidth(CameraControlWidthPx);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 3, 2, 3);
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

MapMonitorVerticalExaggerationHudWidget::MapMonitorVerticalExaggerationHudWidget(
    MapModel *map_model, QWidget *parent)
    : QFrame(parent),
      map_model(map_model),
      exaggeration_slider(new ResettableVerticalSlider(this)),
      exaggeration_value_label(new QLabel(this))
{
    Q_ASSERT(this->map_model != nullptr);
    configureHudFrame(this);
    setFixedWidth(CameraControlWidthPx);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 3, 2, 3);
    layout->setSpacing(1);

    QLabel *maximum_label = new QLabel(
        QStringLiteral("%1×").arg(MapModel::MaxView3dVerticalExaggeration, 0, 'f', 0),
        this);
    QLabel *minimum_label = new QLabel(
        QStringLiteral("%1×").arg(MapModel::MinView3dVerticalExaggeration, 0, 'f', 1),
        this);
    maximum_label->setAlignment(Qt::AlignHCenter);
    minimum_label->setAlignment(Qt::AlignHCenter);
    this->exaggeration_value_label->setAlignment(Qt::AlignHCenter);

    this->exaggeration_slider->setRange(
        qRound(MapModel::MinView3dVerticalExaggeration * VerticalExaggerationSliderScale),
        qRound(MapModel::MaxView3dVerticalExaggeration * VerticalExaggerationSliderScale));
    this->exaggeration_slider->setSingleStep(1);
    this->exaggeration_slider->setPageStep(5);
    this->exaggeration_slider->setFixedHeight(CameraControlSliderHeightPx);
    this->exaggeration_slider->setValue(qRound(
        this->map_model->view3dVerticalExaggeration() * VerticalExaggerationSliderScale));
    this->exaggeration_slider->setToolTip(QStringLiteral(
        "Vertical exaggeration\n"
        "Scales terrain relief and water-network elevation differences together\n"
        "1.0× = true vertical scale\n"
        "The separate network-height control remains a render-only lift in metres\n"
        "Right-click: animate back to 1.0×"));
    this->exaggeration_value_label->setText(QStringLiteral("%1×").arg(
        this->map_model->view3dVerticalExaggeration(), 0, 'f', 1));

    layout->addWidget(maximum_label);
    layout->addWidget(this->exaggeration_slider, 1, Qt::AlignHCenter);
    layout->addWidget(minimum_label);
    layout->addWidget(this->exaggeration_value_label);

    QVariantAnimation *reset_animation = new QVariantAnimation(this);
    reset_animation->setDuration(SliderResetAnimationDurationMs);
    reset_animation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(reset_animation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value)
    {
        this->map_model->setView3dVerticalExaggeration(value.toDouble());
    });

    ResettableVerticalSlider *resettable_slider =
        static_cast<ResettableVerticalSlider *>(this->exaggeration_slider);
    resettable_slider->setResetCallback([this, reset_animation]
    {
        reset_animation->stop();
        reset_animation->setStartValue(this->map_model->view3dVerticalExaggeration());
        reset_animation->setEndValue(MapModel::DefaultView3dVerticalExaggeration);
        reset_animation->start();
    });
    connect(this->exaggeration_slider, &QSlider::sliderPressed, reset_animation,
            &QVariantAnimation::stop);

    connect(this->exaggeration_slider, &QSlider::valueChanged, this, [this](int value)
    {
        this->map_model->setView3dVerticalExaggeration(
            double(value) / VerticalExaggerationSliderScale);
    });
    connect(this->map_model, &MapModel::view3dCameraChanged, this, [this]
    {
        const double exaggeration = this->map_model->view3dVerticalExaggeration();
        const int slider_value = qRound(exaggeration * VerticalExaggerationSliderScale);
        if (this->exaggeration_slider->value() != slider_value)
        {
            const QSignalBlocker blocker(this->exaggeration_slider);
            this->exaggeration_slider->setValue(slider_value);
        }
        this->exaggeration_value_label->setText(
            QStringLiteral("%1×").arg(exaggeration, 0, 'f', 1));
    });
}

MapMonitorUndergroundHudWidget::MapMonitorUndergroundHudWidget(
    MapRhiWidget *rhi_widget, QWidget *parent)
    : QFrame(parent),
      rhi_widget(rhi_widget),
      underground_combo(new QComboBox(this))
{
    Q_ASSERT(this->rhi_widget != nullptr);
    configureHudFrame(this);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(0);

    this->underground_combo->addItem(QStringLiteral("X-Ray"),
        int(MapRhiUndergroundMode::XRay));
    this->underground_combo->addItem(QStringLiteral("Hide"),
        int(MapRhiUndergroundMode::Hide));
    this->underground_combo->addItem(QStringLiteral("Solid"),
        int(MapRhiUndergroundMode::Solid));
    this->underground_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    this->underground_combo->setToolTip(QStringLiteral(
        "Underground network display\n"
        "X-Ray: show actual underground pipe sections and junctions with a distinct pattern.\n"
        "Hide: let terrain hide the network normally.\n"
        "Solid: show the network through terrain without an underground pattern."));

    const int current_index = this->underground_combo->findData(
        int(this->rhi_widget->undergroundMode()));
    if (current_index >= 0)
        this->underground_combo->setCurrentIndex(current_index);

    connect(this->underground_combo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index)
    {
        if (index < 0)
            return;
        const MapRhiUndergroundMode mode = static_cast<MapRhiUndergroundMode>(
            this->underground_combo->itemData(index).toInt());
        this->rhi_widget->setUndergroundMode(mode);
    });

    layout->addWidget(this->underground_combo);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

MapMonitorVerticalControlsHudWidget::MapMonitorVerticalControlsHudWidget(
    MapModel *map_model, MapRhiWidget *rhi_widget, QWidget *parent)
    : QFrame(parent)
{
    Q_ASSERT(map_model != nullptr);
    Q_ASSERT(rhi_widget != nullptr);

    setFrameShape(QFrame::NoFrame);
    setFrameShadow(QFrame::Plain);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->setAlignment(Qt::AlignBottom);

    QFrame *slider_panel = new QFrame(this);
    configureHudFrame(slider_panel);
    QHBoxLayout *slider_layout = new QHBoxLayout(slider_panel);
    slider_layout->setContentsMargins(3, 3, 3, 3);
    slider_layout->setSpacing(0);

    MapMonitorNetworkGroundOffsetHudWidget *network_ground_offset =
        new MapMonitorNetworkGroundOffsetHudWidget(map_model, slider_panel);
    MapMonitorTiltHudWidget *tilt =
        new MapMonitorTiltHudWidget(map_model, slider_panel);
    MapMonitorCameraDistanceHudWidget *camera_distance =
        new MapMonitorCameraDistanceHudWidget(map_model, slider_panel);
    MapMonitorVerticalExaggerationHudWidget *vertical_exaggeration =
        new MapMonitorVerticalExaggerationHudWidget(map_model, slider_panel);

    QFrame *slider_controls[4] = {
        network_ground_offset,
        tilt,
        camera_distance,
        vertical_exaggeration
    };
    for (QFrame *control : slider_controls)
    {
        control->setFrameShape(QFrame::NoFrame);
        control->setFrameShadow(QFrame::Plain);
        control->setAutoFillBackground(false);
        slider_layout->addWidget(control);
    }

    MapMonitorUndergroundHudWidget *underground =
        new MapMonitorUndergroundHudWidget(rhi_widget, this);

    layout->addWidget(slider_panel, 0, Qt::AlignBottom);
    layout->addWidget(underground, 0, Qt::AlignBottom);
}
