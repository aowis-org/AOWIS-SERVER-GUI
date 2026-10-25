#include "tabs/project_revision_graph.h"

#include "config/gui_configuration.h"
#include "config/shortcut_registry.h"

#include <QApplication>
#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPalette>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtMath>
#include <cmath>

namespace
{
constexpr double node_width = 224.0;
constexpr double node_height = 84.0;
constexpr int frame_interval_ms = 16;
constexpr double pan_speed = 900.0;
constexpr double fast_pan_multiplier = 2.0;
constexpr double pan_acceleration = 2800.0;
constexpr double fast_acceleration_multiplier = 1.75;
constexpr double pan_deceleration = 3600.0;
constexpr double mouse_pan_deceleration = 2600.0;
constexpr double mouse_maximum_speed = 2400.0;
constexpr double mouse_inertia_minimum_speed = 70.0;
constexpr int release_timeout_ms = 100;
constexpr double mouse_velocity_smoothing = 0.65;
constexpr double velocity_stop_threshold = 0.5;
constexpr double keyboard_zoom_octaves_per_second = 1.5;
constexpr double min_zoom = 0.25;
constexpr double max_zoom = 3.0;

qreal vectorLength(const QPointF &point)
{
    return std::hypot(point.x(), point.y());
}

QPointF moveTowards(const QPointF &current, const QPointF &target, double maximum_delta)
{
    const QPointF delta = target - current;
    const double length = vectorLength(delta);
    if (length <= maximum_delta || qFuzzyIsNull(length))
        return target;
    return current + delta / length * maximum_delta;
}
QColor tint(const QColor &color, int alpha)
{
    QColor result = color;
    result.setAlpha(alpha);
    return result;
}
}

ProjectRevisionGraph::ProjectRevisionGraph(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("projectRevisionGraph"));
    setMinimumSize(340, 250);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::OpenHandCursor);
    this->zoom_motion.setLimits(std::log2(min_zoom), std::log2(max_zoom));
    this->navigation_timer.setInterval(frame_interval_ms);
    this->navigation_timer.setTimerType(Qt::PreciseTimer);
    connect(&this->navigation_timer, &QTimer::timeout, this, &ProjectRevisionGraph::updateNavigation);
    // Navigation bindings are shared with the map, but no overlay or
    // hover tooltip is drawn over the revision graph.
    connect(&guiShortcutRegistry(), &GuiShortcutRegistry::shortcutChanged, this,
            [this](GuiShortcutId id)
    {
        // A rebinding can happen while the graph is focused and a key is held.
        // Discard the old binding's hold so it cannot become stuck.
        switch (id)
        {
            case GuiShortcutId::MapPanLeft:
            case GuiShortcutId::MapPanRight:
            case GuiShortcutId::MapPanUp:
            case GuiShortcutId::MapPanDown:
            case GuiShortcutId::MapZoomIn:
            case GuiShortcutId::MapZoomOut:
                stopNavigation();
                break;
            default:
                break;
        }
    });
    connect(&guiShortcutRegistry(), &GuiShortcutRegistry::shortcutsReset, this,
            [this]()
    {
        stopNavigation();
    });
}

void ProjectRevisionGraph::setNodes(const QVector<ProjectGraphNode> &nodes)
{
    bool same_history = this->nodes.size() == nodes.size();
    if (same_history)
    {
        for (int i = 0; i < nodes.size(); ++i)
            if (this->nodes.at(i).id != nodes.at(i).id)
            {
                same_history = false;
                break;
            }
    }
    this->nodes = nodes;
    if (same_history) update();
    else fitGraph();
}

void ProjectRevisionGraph::setSelection(const QString &revision_id)
{
    if (this->selected_revision == revision_id)
        return;
    this->selected_revision = revision_id;
    update();
}

QRectF ProjectRevisionGraph::nodeRect(const ProjectGraphNode &node) const
{
    return QRectF(node.position.x() - node_width / 2.0,
                  node.position.y() - node_height / 2.0,
                  node_width, node_height);
}

QPointF ProjectRevisionGraph::mapToGraph(const QPointF &point) const
{
    return (point - this->offset) / this->zoom;
}

QPointF ProjectRevisionGraph::graphToViewport(const QPointF &point) const
{
    return point * this->zoom + this->offset;
}

double ProjectRevisionGraph::zoomFactor() const
{
    return this->zoom;
}

const ProjectGraphNode *ProjectRevisionGraph::hitNode(const QPointF &point) const
{
    for (const ProjectGraphNode &node : this->nodes)
    {
        if (nodeRect(node).adjusted(-8, -8, 8, 8).contains(point))
            return &node;
    }
    return nullptr;
}

void ProjectRevisionGraph::fitGraph()
{
    stopNavigation();
    if (this->nodes.isEmpty())
    {
        this->zoom = 1.0;
        this->offset = QPointF(width() / 2.0, height() / 2.0);
        update();
        return;
    }

    QRectF bounds;
    for (const ProjectGraphNode &node : this->nodes)
        bounds = bounds.isNull() ? nodeRect(node) : bounds.united(nodeRect(node));
    bounds.adjust(-76, -85, 76, 85);
    this->zoom = qBound(min_zoom, qMin((width() - 32.0) / bounds.width(),
                                  (height() - 32.0) / bounds.height()), 1.5);
    this->offset = QPointF(width() / 2.0, height() / 2.0) - bounds.center() * this->zoom;
    update();
}

void ProjectRevisionGraph::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!event->oldSize().isValid())
    {
        fitGraph();
        return;
    }

    // A dock appearing/disappearing changes the canvas dimensions, not the
    // user's chosen camera. Keep the current center and zoom instead of
    // unexpectedly fitting/resetting the graph on each sidebar toggle.
    this->offset += QPointF((event->size().width() - event->oldSize().width()) / 2.0,
                            (event->size().height() - event->oldSize().height()) / 2.0);
    update();
}

void ProjectRevisionGraph::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QColor base = palette().color(QPalette::Base);
    const QColor ink = palette().color(QPalette::Text);
    const QColor muted = palette().color(QPalette::PlaceholderText);
    const QColor accent = palette().color(QPalette::Highlight);
    painter.fillRect(rect(), base);

    // A subtle reference grid reinforces the graph-as-workspace metaphor.
    painter.setPen(QPen(tint(muted, 27), 1));
    const double grid = 36.0 * this->zoom;
    if (grid >= 12.0)
    {
        const double start_x = std::fmod(this->offset.x(), grid);
        const double start_y = std::fmod(this->offset.y(), grid);
        for (double x = start_x; x < width(); x += grid)
            for (double y = start_y; y < height(); y += grid)
                painter.drawPoint(QPointF(x, y));
    }

    if (this->nodes.isEmpty())
    {
        painter.setPen(muted);
        painter.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter,
                         tr("No revisions to display\nSelect a project or create a branch."));
        return;
    }

    painter.save();
    painter.translate(this->offset);
    painter.scale(this->zoom, this->zoom);

    // Parents can belong to any branch: derive edges from immutable IDs, not
    // from a first-parent-only visible history list.
    for (const ProjectGraphNode &node : this->nodes)
    {
        for (const QString &parent_id : node.parents)
        {
            for (const ProjectGraphNode &parent : this->nodes)
            {
                if (parent.id != parent_id)
                    continue;
                const QPointF begin = parent.position + QPointF(0, -node_height / 2);
                const QPointF end = node.position + QPointF(0, node_height / 2);
                QPainterPath path(begin);
                path.cubicTo(begin + QPointF(0, -42), end + QPointF(0, 42), end);
                painter.setPen(QPen(tint(accent, 115), 3.0, Qt::SolidLine, Qt::RoundCap));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(path);
                break;
            }
        }
    }

    QFont title_font = font();
    title_font.setBold(true);
    QFont small_font = font();
    small_font.setPointSizeF(qMax(7.0, small_font.pointSizeF() - 1.0));
    for (const ProjectGraphNode &node : this->nodes)
    {
        const QRectF box = nodeRect(node);
        const bool selected = node.id == this->selected_revision;
        const QColor surface = palette().color(QPalette::Window);
        painter.setBrush(surface);
        painter.setPen(QPen(selected ? accent : tint(muted, 125), selected ? 3 : 1.4));
        painter.drawRoundedRect(box, 12, 12);
        if (selected)
        {
            painter.setBrush(accent);
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(QRectF(box.left(), box.top() + 10, 5, box.height() - 20), 2, 2);
        }

        painter.setPen(ink);
        painter.setFont(title_font);
        painter.drawText(box.adjusted(16, 10, -12, -48),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(node.title, Qt::ElideRight, 194));
        painter.setFont(small_font);
        painter.setPen(muted);
        painter.drawText(box.adjusted(16, 38, -12, -26), Qt::AlignLeft,
                         node.id.left(12));

        const QRectF pill(box.left() + 14, box.bottom() - 26, 118, 19);
        painter.setBrush(tint(accent, selected ? 80 : 38));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(pill, 8, 8);
        painter.setPen(ink);
        painter.drawText(pill.adjusted(7, 1, -7, -1), Qt::AlignVCenter | Qt::AlignLeft,
                         painter.fontMetrics().elidedText(node.branch, Qt::ElideRight, 101));

        if (!node.head_branch.isEmpty())
        {
            const QRectF head(box.left() + 139, box.bottom() - 26, 74, 19);
            painter.setBrush(tint(QColor(58, 171, 130), 80));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(head, 8, 8);
            painter.setPen(ink);
            painter.drawText(head, Qt::AlignCenter, QStringLiteral("HEAD"));
        }
    }
    painter.restore();
}

void ProjectRevisionGraph::startNavigation()
{
    if (!isVisible() || this->navigation_timer.isActive())
        return;
    this->frame_elapsed.start();
    this->navigation_timer.start();
}

QPointF ProjectRevisionGraph::keyboardDirection() const
{
    QPointF direction;
    if (this->key_left)
        direction.rx() += 1.0;
    if (this->key_right)
        direction.rx() -= 1.0;
    if (this->key_up)
        direction.ry() += 1.0;
    if (this->key_down)
        direction.ry() -= 1.0;
    return direction;
}

void ProjectRevisionGraph::stopNavigation()
{
    this->navigation_timer.stop();
    this->pan_velocity = QPointF();
    this->mouse_pan_velocity = QPointF();
    this->mouse_inertia = false;
    this->dragging = false;
    this->mouse_pressed = false;
    this->drag_distance = 0.0;
    this->key_left = false;
    this->key_right = false;
    this->key_up = false;
    this->key_down = false;
    this->key_zoom_in = false;
    this->key_zoom_out = false;
    this->key_fast = false;
    this->zoom_motion.cancel();
    setCursor(Qt::OpenHandCursor);
}

void ProjectRevisionGraph::updateNavigation()
{
    if (!isVisible())
    {
        stopNavigation();
        return;
    }

    const double dt = qBound(0.0, this->frame_elapsed.restart() / 1000.0, 0.05);
    if (dt <= 0.0)
        return;

    if (this->zoom_motion.active())
    {
        const double next_octaves = this->zoom_motion.advance(std::log2(this->zoom), dt);
        const double next_zoom = std::exp2(next_octaves);
        if (next_zoom != this->zoom)
        {
            this->zoom = next_zoom;
            this->offset = this->zoom_anchor - this->zoom_graph_anchor * this->zoom;
            update();
        }
    }

    if (!this->mouse_pressed)
    {
        const QPointF direction = keyboardDirection();
        double acceleration = direction.isNull()
            ? (this->mouse_inertia ? mouse_pan_deceleration : pan_deceleration)
            : pan_acceleration;
        if (!direction.isNull() && this->key_fast)
            acceleration *= fast_acceleration_multiplier;
        const QPointF target = direction * (pan_speed * (this->key_fast ? fast_pan_multiplier : 1.0));
        this->pan_velocity = moveTowards(this->pan_velocity, target, acceleration * dt);
        if (direction.isNull() && vectorLength(this->pan_velocity) < velocity_stop_threshold)
        {
            this->pan_velocity = QPointF();
            this->mouse_inertia = false;
        }
        if (!this->pan_velocity.isNull())
        {
            const QPointF pan_delta = this->pan_velocity * dt;
            this->offset += pan_delta;
            this->zoom_anchor += pan_delta;
            update();
        }
    }

    if (!this->zoom_motion.active() && keyboardDirection().isNull()
        && this->pan_velocity.isNull() && !this->mouse_pressed)
    {
        this->navigation_timer.stop();
    }
}

void ProjectRevisionGraph::setZoomAnchor(const QPointF &anchor)
{
    this->zoom_anchor = anchor;
    this->zoom_graph_anchor = mapToGraph(anchor);
}

void ProjectRevisionGraph::synchronizeZoomHold()
{
    const int direction = int(this->key_zoom_in) - int(this->key_zoom_out);
    if (direction != 0)
        setZoomAnchor(QPointF(width() / 2.0, height() / 2.0));
    const double rate = keyboard_zoom_octaves_per_second * (this->key_fast ? 2.0 : 1.0);
    this->zoom_motion.setHold(std::log2(this->zoom), direction, rate,
                              MapZoomAnimation::KeyboardTapOctaves);
    if (this->zoom_motion.active())
        startNavigation();
}

void ProjectRevisionGraph::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
    {
        QWidget::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    this->mouse_pressed = true;
    this->last_pointer = event->position();
    this->drag_distance = 0.0;
    this->dragging = false;
    this->mouse_inertia = false;
    this->pan_velocity = QPointF();
    this->mouse_pan_velocity = QPointF();
    this->drag_elapsed.start();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void ProjectRevisionGraph::mouseMoveEvent(QMouseEvent *event)
{
    if (this->mouse_pressed && (event->buttons() & Qt::LeftButton))
    {
        const QPointF delta = event->position() - this->last_pointer;
        this->last_pointer = event->position();
        this->drag_distance += vectorLength(delta);
        if (this->drag_distance >= QApplication::startDragDistance())
            this->dragging = true;
        const qint64 elapsed_ms = this->drag_elapsed.restart();
        if (this->dragging && !delta.isNull())
        {
            this->offset += delta;
            this->zoom_anchor += delta;
            if (elapsed_ms > 0)
            {
                const double elapsed_seconds = qBound(0.001, elapsed_ms / 1000.0, 0.1);
                QPointF measured_velocity = delta / elapsed_seconds;
                const double speed = vectorLength(measured_velocity);
                if (speed > mouse_maximum_speed)
                    measured_velocity *= mouse_maximum_speed / speed;
                if (this->mouse_pan_velocity.isNull())
                    this->mouse_pan_velocity = measured_velocity;
                else
                    this->mouse_pan_velocity = this->mouse_pan_velocity * (1.0 - mouse_velocity_smoothing)
                        + measured_velocity * mouse_velocity_smoothing;
            }
            update();
        }
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ProjectRevisionGraph::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && this->mouse_pressed)
    {
        this->mouse_pressed = false;
        setCursor(Qt::OpenHandCursor);
        if (!this->dragging)
        {
            const QPointF point = mapToGraph(event->position());
            if (const ProjectGraphNode *node = hitNode(point))
            {
                // The branch badge has a separate hit target from the revision.
                const QRectF pill(nodeRect(*node).left() + 14,
                                  nodeRect(*node).bottom() - 26, 118, 19);
                if (pill.contains(point))
                    emit branchActivated(node->branch);
                else
                    emit revisionActivated(node->id);
            }
        }
        else if (this->drag_elapsed.isValid()
                 && this->drag_elapsed.elapsed() <= release_timeout_ms
                 && vectorLength(this->mouse_pan_velocity) >= mouse_inertia_minimum_speed)
        {
            this->pan_velocity = this->mouse_pan_velocity;
            this->mouse_inertia = true;
            startNavigation();
        }
        this->mouse_pan_velocity = QPointF();
        this->dragging = false;
        this->drag_distance = 0.0;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ProjectRevisionGraph::mouseDoubleClickEvent(QMouseEvent *event)
{
    fitGraph();
    event->accept();
}

void ProjectRevisionGraph::wheelEvent(QWheelEvent *event)
{
    // A trackpad may send pixel-only wheel deltas. The map uses the same
    // conversion and critically damped zoom motion for both wheel devices.
    const double steps = event->angleDelta().y() != 0
        ? event->angleDelta().y() / 120.0 : event->pixelDelta().y() / 240.0;
    if (std::isfinite(steps) && std::abs(steps) > 1e-9)
    {
        setZoomAnchor(event->position());
        this->zoom_motion.addImpulse(std::log2(this->zoom), steps * 0.5);
        startNavigation();
    }
    event->accept();
}

ProjectRevisionGraph::NavigationKeyIdentity ProjectRevisionGraph::navigationKeyIdentity(
    const QKeyEvent *event)
{
    NavigationKeyIdentity identity;
    identity.scan_code = event->nativeScanCode();
    identity.virtual_key = event->nativeVirtualKey();
    identity.key = event->key();
    return identity;
}

bool ProjectRevisionGraph::sameNavigationKey(const NavigationKeyIdentity &first,
                                            const NavigationKeyIdentity &second)
{
    if (first.scan_code != 0 && second.scan_code != 0)
        return first.scan_code == second.scan_code;
    if (first.virtual_key != 0 && second.virtual_key != 0)
        return first.virtual_key == second.virtual_key;
    return first.key == second.key;
}

void ProjectRevisionGraph::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Shift)
    {
        this->key_fast = true;
        if (this->key_zoom_in || this->key_zoom_out)
            synchronizeZoomHold();
        startNavigation();
        event->accept();
        return;
    }

    const Qt::KeyboardModifiers arrow_modifiers =
        event->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
    const GuiShortcutRegistry &registry = guiShortcutRegistry();
    const Qt::KeyboardModifiers fast_modifier = Qt::ShiftModifier;

    bool *key = nullptr;
    NavigationKeyIdentity *identity = nullptr;
    bool zoom_key = false;
    if ((event->key() == Qt::Key_Left && arrow_modifiers == Qt::NoModifier)
        || guiShortcutMatches(event, registry.shortcut(GuiShortcutId::MapPanLeft), fast_modifier))
    {
        key = &this->key_left;
        identity = &this->key_left_identity;
    }
    else if ((event->key() == Qt::Key_Right && arrow_modifiers == Qt::NoModifier)
             || guiShortcutMatches(event, registry.shortcut(GuiShortcutId::MapPanRight), fast_modifier))
    {
        key = &this->key_right;
        identity = &this->key_right_identity;
    }
    else if ((event->key() == Qt::Key_Up && arrow_modifiers == Qt::NoModifier)
             || guiShortcutMatches(event, registry.shortcut(GuiShortcutId::MapPanUp), fast_modifier))
    {
        key = &this->key_up;
        identity = &this->key_up_identity;
    }
    else if ((event->key() == Qt::Key_Down && arrow_modifiers == Qt::NoModifier)
             || guiShortcutMatches(event, registry.shortcut(GuiShortcutId::MapPanDown), fast_modifier))
    {
        key = &this->key_down;
        identity = &this->key_down_identity;
    }
    else if (guiShortcutMatches(event, registry.shortcut(GuiShortcutId::MapZoomIn), fast_modifier))
    {
        key = &this->key_zoom_in;
        identity = &this->key_zoom_in_identity;
        zoom_key = true;
    }
    else if (guiShortcutMatches(event, registry.shortcut(GuiShortcutId::MapZoomOut), fast_modifier))
    {
        key = &this->key_zoom_out;
        identity = &this->key_zoom_out_identity;
        zoom_key = true;
    }
    if (key == nullptr)
    {
        QWidget::keyPressEvent(event);
        return;
    }

    this->key_fast = event->modifiers().testFlag(Qt::ShiftModifier);
    if (!*key)
    {
        *key = true;
        *identity = navigationKeyIdentity(event);
        if (zoom_key)
            synchronizeZoomHold();
    }
    startNavigation();
    event->accept();
}

void ProjectRevisionGraph::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Shift)
    {
        if (!event->isAutoRepeat())
            this->key_fast = false;
        if (this->key_zoom_in || this->key_zoom_out)
            synchronizeZoomHold();
        event->accept();
        return;
    }

    // Match the key that actually went down, not its now possibly changed
    // modifier combination (Ctrl/Alt/Shift can be released first).
    struct HeldKey
    {
        bool *pressed;
        const NavigationKeyIdentity *identity;
        bool zoom;
    };
    const HeldKey held_keys[] = {
        {&this->key_left, &this->key_left_identity, false},
        {&this->key_right, &this->key_right_identity, false},
        {&this->key_up, &this->key_up_identity, false},
        {&this->key_down, &this->key_down_identity, false},
        {&this->key_zoom_in, &this->key_zoom_in_identity, true},
        {&this->key_zoom_out, &this->key_zoom_out_identity, true},
    };
    const NavigationKeyIdentity released = navigationKeyIdentity(event);
    bool handled = false;
    bool zoom_released = false;
    for (const HeldKey &held_key : held_keys)
    {
        if (!*held_key.pressed || !sameNavigationKey(*held_key.identity, released))
            continue;
        handled = true;
        if (!event->isAutoRepeat())
        {
            *held_key.pressed = false;
            zoom_released = zoom_released || held_key.zoom;
        }
    }
    if (!handled)
    {
        QWidget::keyReleaseEvent(event);
        return;
    }
    if (!event->isAutoRepeat())
    {
        if (zoom_released)
            synchronizeZoomHold();
        startNavigation();
    }
    event->accept();
}

void ProjectRevisionGraph::focusOutEvent(QFocusEvent *event)
{
    stopNavigation();
    QWidget::focusOutEvent(event);
}

void ProjectRevisionGraph::hideEvent(QHideEvent *event)
{
    stopNavigation();
    QWidget::hideEvent(event);
}
