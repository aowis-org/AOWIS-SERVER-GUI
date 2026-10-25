#include "tabs/project_revision_graph.h"
#include "config/shortcut_registry.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>

#include <cmath>

#include <cstdio>

namespace
{
bool require(bool condition, const char *reason)
{
    if (!condition)
        std::fprintf(stderr, "project_revision_graph: %s\n", reason);
    return condition;
}

void click(QWidget &widget, const QPointF &position)
{
    QMouseEvent press(QEvent::MouseButtonPress, position, position, position,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, position, position, position,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &release);
}

void waitFor(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

void sendKey(QWidget &widget, QEvent::Type type, int key,
             Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent event(type, key, modifiers);
    QCoreApplication::sendEvent(&widget, &event);
}

void sendMouse(QWidget &widget, QEvent::Type type, const QPointF &point,
               Qt::MouseButtons buttons)
{
    const Qt::MouseButton changed = type == QEvent::MouseMove
        ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent event(type, point, point, point, changed, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &event);
}

void sendWheel(QWidget &widget, const QPointF &point, const QPoint &angle,
               const QPoint &pixels = QPoint())
{
    QWheelEvent event(point, point, pixels, angle, Qt::NoButton,
                      Qt::NoModifier, Qt::ScrollUpdate, false);
    QCoreApplication::sendEvent(&widget, &event);
}
}

int main(int argc, char **argv)
{
    // Never read or overwrite the developer's actual shortcut settings.
    QTemporaryDir temporary_configuration;
    if (!require(temporary_configuration.isValid(), "could not create isolated GUI configuration"))
        return 1;
    qputenv("XDG_DATA_HOME", temporary_configuration.path().toUtf8());
    QApplication app(argc, argv);
    ProjectRevisionGraph graph;
    graph.resize(800, 500);
    graph.show();
    app.processEvents();

    ProjectGraphNode node;
    node.id = QStringLiteral("test-revision");
    node.title = QStringLiteral("Initial preview revision");
    node.branch = QStringLiteral("main");
    node.head_branch = QStringLiteral("main");
    node.position = QPointF(0.0, 0.0);
    graph.setNodes(QVector<ProjectGraphNode>{node});
    graph.setSelection(node.id);

    QString revision_clicked;
    QString branch_clicked;
    QObject::connect(&graph, &ProjectRevisionGraph::revisionActivated,
                     &graph, [&revision_clicked](const QString &id)
    {
        revision_clicked = id;
    });
    QObject::connect(&graph, &ProjectRevisionGraph::branchActivated,
                     &graph, [&branch_clicked](const QString &name)
    {
        branch_clicked = name;
    });

    click(graph, QPointF(400, 250));
    if (!require(revision_clicked == node.id, "revision click did not emit selected hash"))
        return 1;

    // The badge is at x=-98..20, y=16..35 around the node center.
    // Fit-to-view for a single node clamps at zoom=1.5 at 800x500.
    click(graph, QPointF(400 - 50 * 1.5, 250 + 25 * 1.5));
    if (!require(branch_clicked == node.branch, "branch badge click did not emit branch name"))
        return 1;

    // Key movement accelerates while held, then glides to a stop when released.
    graph.fitGraph();
    const double initial_x = graph.graphToViewport(QPointF(0.0, 0.0)).x();
    sendKey(graph, QEvent::KeyPress, Qt::Key_A);
    waitFor(90);
    const double holding_x = graph.graphToViewport(QPointF(0.0, 0.0)).x();
    if (!require(holding_x > initial_x + 1.0, "A key did not pan the graph"))
        return 1;
    sendKey(graph, QEvent::KeyRelease, Qt::Key_A);
    waitFor(45);
    if (!require(graph.graphToViewport(QPointF(0.0, 0.0)).x() > holding_x,
                 "keyboard pan stopped abruptly instead of decelerating"))
        return 1;

    // Same transform drives both rendering and hit testing after navigation.
    graph.fitGraph();
    revision_clicked.clear();
    branch_clicked.clear();
    sendKey(graph, QEvent::KeyPress, Qt::Key_D);
    waitFor(85);
    sendKey(graph, QEvent::KeyRelease, Qt::Key_D);
    waitFor(100);
    click(graph, graph.graphToViewport(QPointF(0.0, 0.0)));
    if (!require(revision_clicked == node.id, "revision selection broke after keyboard pan"))
        return 1;
    click(graph, graph.graphToViewport(QPointF(-50.0, 25.0)));
    if (!require(branch_clicked == node.branch, "branch badge selection broke after keyboard pan"))
        return 1;

    // The map's critically damped zoom motion keeps the point under the
    // wheel pointer fixed, including during the animation.
    graph.fitGraph();
    const QPointF wheel_anchor(185.0, 145.0);
    const double before_zoom = graph.zoomFactor();
    const QPointF before_center = graph.graphToViewport(QPointF());
    const QPointF fixed_graph_point = (wheel_anchor - before_center) / before_zoom;
    sendWheel(graph, wheel_anchor, QPoint(0, 120));
    waitFor(130);
    if (!require(graph.zoomFactor() > before_zoom, "wheel did not zoom in"))
        return 1;
    if (!require(std::hypot((graph.graphToViewport(fixed_graph_point) - wheel_anchor).x(),
                            (graph.graphToViewport(fixed_graph_point) - wheel_anchor).y()) < 0.01,
                 "wheel zoom did not preserve the pointer anchor"))
        return 1;

    graph.fitGraph();
    const double before_e = graph.zoomFactor();
    sendKey(graph, QEvent::KeyPress, Qt::Key_E);
    waitFor(85);
    sendKey(graph, QEvent::KeyRelease, Qt::Key_E);
    waitFor(100);
    if (!require(graph.zoomFactor() > before_e, "E key did not smoothly zoom in"))
        return 1;
    sendKey(graph, QEvent::KeyPress, Qt::Key_Q);
    waitFor(160);
    sendKey(graph, QEvent::KeyRelease, Qt::Key_Q);
    waitFor(140);
    if (!require(graph.zoomFactor() < before_e + 0.15, "Q key did not zoom out"))
        return 1;

    // A recent, sufficiently fast drag keeps moving after release.
    graph.fitGraph();
    sendMouse(graph, QEvent::MouseButtonPress, QPointF(180.0, 130.0), Qt::LeftButton);
    waitFor(20);
    sendMouse(graph, QEvent::MouseMove, QPointF(225.0, 130.0), Qt::LeftButton);
    waitFor(25);
    sendMouse(graph, QEvent::MouseMove, QPointF(267.0, 130.0), Qt::LeftButton);
    sendMouse(graph, QEvent::MouseButtonRelease, QPointF(267.0, 130.0), Qt::NoButton);
    const double at_release = graph.graphToViewport(QPointF()).x();
    waitFor(60);
    if (!require(graph.graphToViewport(QPointF()).x() > at_release + 1.0,
                 "mouse drag did not carry momentum past release"))
        return 1;

    // The graph and map must consume the very same editable shortcuts. These
    // changes go through the real settings registry (and its change signals).
    GuiShortcutRegistry &shortcuts = guiShortcutRegistry();
    QString binding_error;
    if (!require(shortcuts.setShortcut(GuiShortcutId::MapPanLeft,
                                      QKeySequence(QStringLiteral("Ctrl+J")), &binding_error),
                 "could not remap pan left"))
        return 1;
    if (!require(shortcuts.setShortcut(GuiShortcutId::MapZoomIn,
                                      QKeySequence(QStringLiteral("Alt+I")), &binding_error),
                 "could not remap zoom in"))
        return 1;
    if (!require(graph.toolTip().isEmpty(),
                 "revision graph must not display a navigation hover tooltip"))
        return 1;

    graph.fitGraph();
    const QPointF after_remap_start = graph.graphToViewport(QPointF());
    sendKey(graph, QEvent::KeyPress, Qt::Key_A);
    waitFor(75);
    sendKey(graph, QEvent::KeyRelease, Qt::Key_A);
    if (!require(graph.graphToViewport(QPointF()) == after_remap_start,
                 "old pan key is still active after remapping"))
        return 1;
    sendKey(graph, QEvent::KeyPress, Qt::Key_J, Qt::ControlModifier);
    waitFor(90);
    const double remapped_pan_x = graph.graphToViewport(QPointF()).x();
    if (!require(remapped_pan_x > after_remap_start.x() + 1.0,
                 "Ctrl+J did not pan using the updated setting"))
        return 1;
    // The modifier was released first; a release of the physical key must
    // still clear the held direction and begin inertia instead of sticking.
    sendKey(graph, QEvent::KeyRelease, Qt::Key_J);
    waitFor(50);
    if (!require(graph.graphToViewport(QPointF()).x() > remapped_pan_x,
                 "remapped pan key did not coast after modifier-less release"))
        return 1;

    graph.fitGraph();
    const double zoom_before_remap = graph.zoomFactor();
    sendKey(graph, QEvent::KeyPress, Qt::Key_E);
    waitFor(75);
    sendKey(graph, QEvent::KeyRelease, Qt::Key_E);
    if (!require(graph.zoomFactor() == zoom_before_remap,
                 "old zoom key remained active after remapping"))
        return 1;
    sendKey(graph, QEvent::KeyPress, Qt::Key_I, Qt::AltModifier);
    waitFor(95);
    sendKey(graph, QEvent::KeyRelease, Qt::Key_I);
    waitFor(65);
    if (!require(graph.zoomFactor() > zoom_before_remap,
                 "Alt+I did not zoom using the updated setting"))
        return 1;

    // Rebinding during a held key must cancel it instead of leaking a
    // persistent navigation state across the settings change.
    graph.fitGraph();
    sendKey(graph, QEvent::KeyPress, Qt::Key_J, Qt::ControlModifier);
    waitFor(65);
    if (!require(shortcuts.setShortcut(GuiShortcutId::MapPanLeft,
                                      QKeySequence(QStringLiteral("Ctrl+H")), &binding_error),
                 "could not rebind an active pan direction"))
        return 1;
    const QPointF position_at_rebind = graph.graphToViewport(QPointF());
    waitFor(90);
    if (!require(graph.graphToViewport(QPointF()) == position_at_rebind,
                 "changing a held shortcut left navigation running"))
        return 1;
    sendKey(graph, QEvent::KeyRelease, Qt::Key_J);

    // Losing focus must not leave any held navigation key moving the graph.
    graph.fitGraph();
    sendKey(graph, QEvent::KeyPress, Qt::Key_W);
    waitFor(55);
    QFocusEvent lose_focus(QEvent::FocusOut);
    QCoreApplication::sendEvent(&graph, &lose_focus);
    const QPointF focus_position = graph.graphToViewport(QPointF());
    waitFor(70);
    if (!require(graph.graphToViewport(QPointF()) == focus_position,
                 "focus loss did not stop navigation"))
        return 1;

    std::puts("Project revision graph navigation tests passed");
    return 0;
}
