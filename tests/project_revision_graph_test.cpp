#include "tabs/project_revision_graph.h"
#include "tabs/project_graph_placement.h"
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
#include <QVBoxLayout>

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
    waitFor(110); // allow first-show geometry and automatic fit to settle

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

    // Top-down is the default: a parent/root sits above its descendants.
    // Bottom-up mirrors node positions without flipping their labels or hit
    // targets. The canonical input list is intentionally newest-first.
    ProjectGraphNode previous = node;
    previous.id = QStringLiteral("parent-revision");
    previous.position = QPointF(0.0, 320.0);
    ProjectGraphNode latest = node;
    latest.parents = {previous.id};
    graph.setNodes(QVector<ProjectGraphNode>{latest, previous});
    if (!require(graph.isTopDown(), "graph did not default to top-down"))
        return 1;
    if (!require(graph.revisionViewportPosition(previous.id).y()
                     < graph.revisionViewportPosition(latest.id).y(),
                 "top-down graph must put the root above its descendants"))
        return 1;
    revision_clicked.clear();
    click(graph, graph.revisionViewportPosition(previous.id));
    if (!require(revision_clicked == previous.id,
                 "root revision hit testing failed in the default orientation"))
        return 1;
    graph.setTopDown(false);
    if (!require(!graph.isTopDown(), "bottom-up orientation was not applied"))
        return 1;
    if (!require(graph.revisionViewportPosition(latest.id).y()
                     < graph.revisionViewportPosition(previous.id).y(),
                 "bottom-up graph did not put HEAD above its parent"))
        return 1;
    revision_clicked.clear();
    click(graph, graph.revisionViewportPosition(latest.id));
    if (!require(revision_clicked == latest.id,
                 "revision hit testing broke after flipping the DAG"))
        return 1;
    graph.setTopDown(true);
    if (!require(graph.revisionViewportPosition(previous.id).y()
                     < graph.revisionViewportPosition(latest.id).y(),
                 "returning to top-down did not restore parent-first layout"))
        return 1;
    // Editable-heads canvas mode must retain a visible, clickable summary of
    // every hidden ancestry path. Unfolding one path must not expand the other.
    ProjectGraphNode main_head = node;
    main_head.id = QStringLiteral("head-main");
    main_head.parents = {QStringLiteral("main-intermediate")};
    main_head.position = QPointF(0, 0);
    ProjectGraphNode scenario_head = node;
    scenario_head.id = QStringLiteral("head-scenario");
    scenario_head.branch = QStringLiteral("scenario");
    scenario_head.head_branch = QStringLiteral("scenario");
    scenario_head.parents = {QStringLiteral("intermediate")};
    scenario_head.position = QPointF(285, 155);
    ProjectGraphNode intermediate = scenario_head;
    intermediate.id = QStringLiteral("intermediate");
    intermediate.head_branch.clear();
    intermediate.parents = {QStringLiteral("root")};
    intermediate.position = QPointF(285, 310);
    ProjectGraphNode main_intermediate = main_head;
    main_intermediate.id = QStringLiteral("main-intermediate");
    main_intermediate.head_branch.clear();
    main_intermediate.parents = {QStringLiteral("root")};
    main_intermediate.position = QPointF(0, 310);
    ProjectGraphNode root = main_head;
    root.id = QStringLiteral("root");
    root.head_branch.clear();
    root.parents.clear();
    root.position = QPointF(0, 465);
    graph.setNodes(QVector<ProjectGraphNode>{main_head, scenario_head, main_intermediate, intermediate, root});
    if (!require(graph.showsFullTree() && graph.omissionCount() == 0,
                 "revision graph must default to Full tree"))
        return 1;
    graph.setShowFullTree(false);
    if (!require(!graph.showsFullTree() && graph.omissionCount() == 2
                     && !graph.revisionViewportPosition(root.id).isNull()
                     && graph.revisionViewportPosition(main_intermediate.id).isNull()
                     && graph.revisionViewportPosition(main_head.id) != QPointF(),
                 "Editable heads must preserve the shared ancestor and fold intervening paths"))
        return 1;
    // Fold markers must lie *between* a visible branch ancestor and each
    // head; an isolated "hidden history" bubble breaks the DAG visually.
    const double root_y = graph.revisionViewportPosition(root.id).y();
    const double furthest_head_y = qMax(graph.revisionViewportPosition(main_head.id).y(),
                                         graph.revisionViewportPosition(scenario_head.id).y());
    for (int i = 0; i < graph.omissionCount(); ++i)
    {
        const double marker_y = graph.omissionViewportPosition(i).y();
        if (!require(marker_y > root_y && marker_y < furthest_head_y,
                     "fold marker must lie on the visible root-to-head branch connection"))
            return 1;
    }
    // Pick the folded history belonging to main, not scenario.
    int main_fold = -1;
    for (int i = 0; i < graph.omissionCount(); ++i)
        if (std::abs(graph.omissionViewportPosition(i).x()
            - graph.revisionViewportPosition(root.id).x()) < 0.1) main_fold = i;
    if (!require(main_fold >= 0, "could not find the main branch history fold"))
        return 1;
    click(graph, graph.omissionViewportPosition(main_fold));
    if (!require(!graph.showsFullTree() && graph.omissionCount() == 1
                     && !graph.revisionViewportPosition(main_intermediate.id).isNull()
                     && graph.revisionViewportPosition(intermediate.id).isNull(),
                 "unfolding a graph omission expanded unrelated history or changed view mode"))
        return 1;
    click(graph, graph.omissionViewportPosition(0));
    if (!require(graph.omissionCount() == 0
                     && !graph.revisionViewportPosition(intermediate.id).isNull(),
                 "unfolding the remaining history path failed"))
        return 1;
    // Mode changes reset local unfolding. Otherwise the two-state control
    // appears to do nothing after every omission has been expanded.
    graph.setShowFullTree(true);
    if (!require(graph.showsFullTree() && graph.omissionCount() == 0,
                 "returning to Full tree did not restore all revisions"))
        return 1;
    graph.setShowFullTree(false);
    if (!require(graph.omissionCount() == 2
                     && graph.revisionViewportPosition(main_intermediate.id).isNull()
                     && graph.revisionViewportPosition(intermediate.id).isNull(),
                 "switching back to Editable heads retained old expanded paths"))
        return 1;
    graph.expandOmission(0);
    graph.setShowFullTree(false); // pressing the selected mode re-folds history
    if (!require(graph.omissionCount() == 2,
                 "clicking Editable heads again must collapse expanded history"))
        return 1;
    graph.setNodes(QVector<ProjectGraphNode>{scenario_head, intermediate, root});
    if (!require(graph.omissionCount() == 1
                     && graph.revisionViewportPosition(root.id).isNull(),
                 "loading a new DAG retained expansion state from the old graph"))
        return 1;
    graph.setShowFullTree(true);
    graph.setNodes(QVector<ProjectGraphNode>{main_head, scenario_head, main_intermediate, intermediate, root});
    graph.setShowFullTree(false);
    graph.setSelection(intermediate.id);
    if (!require(!graph.showsFullTree()
                     && !graph.revisionViewportPosition(intermediate.id).isNull(),
                 "selecting history in the sidebar must unfold its graph branch"))
        return 1;
    graph.setShowFullTree(true);
    graph.setNodes(QVector<ProjectGraphNode>{node});
    graph.fitGraph();

    // A graph populated before its containing page first appears must fit
    // against the *final* visible size, not its unlaid-out default size.
    QWidget graph_page;
    QVBoxLayout page_layout(&graph_page);
    page_layout.setContentsMargins(0, 0, 0, 0);
    ProjectRevisionGraph *first_show_graph = new ProjectRevisionGraph(&graph_page);
    page_layout.addWidget(first_show_graph);
    ProjectGraphNode start_node = latest;
    start_node.parents.clear();
    ProjectGraphNode end_node = previous;
    end_node.position = QPointF(0.0, 500.0);
    first_show_graph->setNodes(QVector<ProjectGraphNode>{start_node, end_node});
    graph_page.resize(960, 340);
    graph_page.show();
    app.processEvents();
    waitFor(110); // dock/splitter startup geometry must settle before fitting
    const double expected_fit = qBound(0.25,
        qMin((first_show_graph->width() - 32.0) / (224.0 + 152.0),
             (first_show_graph->height() - 32.0) / (500.0 + 84.0 + 170.0)), 1.5);
    if (!require(std::abs(first_show_graph->zoomFactor() - expected_fit) < 0.02,
                 "initial graph was not fitted after the page received its final size"))
        return 1;
    // Simulate a late dock/splitter resize during the FIRST show. The
    // automatic fit must use the new geometry, without any user button press.
    QWidget late_layout_page;
    QVBoxLayout late_layout(&late_layout_page);
    late_layout.setContentsMargins(0, 0, 0, 0);
    ProjectRevisionGraph *late_graph = new ProjectRevisionGraph(&late_layout_page);
    late_layout.addWidget(late_graph);
    late_graph->setNodes(QVector<ProjectGraphNode>{start_node, end_node});
    late_layout_page.resize(800, 300);
    late_layout_page.show();
    app.processEvents();
    late_layout_page.resize(1020, 650);
    app.processEvents();
    waitFor(110);
    const double late_fit = qBound(0.25,
        qMin((late_graph->width() - 32.0) / (224.0 + 152.0),
             (late_graph->height() - 32.0) / (500.0 + 84.0 + 170.0)), 1.5);
    if (!require(std::abs(late_graph->zoomFactor() - late_fit) < 0.02,
                 "initial fit used stale geometry after a delayed page resize"))
        return 1;
    late_layout_page.close();
    // A genuinely new graph fits automatically without the Fit button.
    first_show_graph->setNodes(QVector<ProjectGraphNode>{node});
    const QPointF new_graph_center = first_show_graph->graphToViewport(node.position);
    if (!require(std::abs(new_graph_center.x() - first_show_graph->width() / 2.0) < 1.0
                     && std::abs(new_graph_center.y() - first_show_graph->height() / 2.0) < 1.0,
                 "loading a different graph did not automatically center it"))
        return 1;
    graph_page.close();

    // A small fitted graph is pinned in the viewport: panning cannot move
    // every card offscreen, even with a huge mouse drag.
    graph.setNodes({node});
    graph.fitGraph();
    sendMouse(graph, QEvent::MouseButtonPress, QPointF(300.0, 200.0), Qt::LeftButton);
    sendMouse(graph, QEvent::MouseMove, QPointF(760.0, 400.0), Qt::LeftButton);
    sendMouse(graph, QEvent::MouseButtonRelease, QPointF(760.0, 400.0), Qt::NoButton);
    if (!require(std::abs(graph.revisionViewportPosition(node.id).x() - graph.width() / 2.0) < 0.01
                     && std::abs(graph.revisionViewportPosition(node.id).y() - graph.height() / 2.0) < 0.01,
                 "small fitted graph can be dragged away from the viewport")) return 1;

    // Navigation tests need an actual scrollable canvas. The bounds here
    // exceed the available viewport in X; Y stays centred when undersized.
    ProjectGraphNode far_left = node;
    far_left.id = QStringLiteral("far-left");
    far_left.position = QPointF(-1000, 0);
    far_left.head_branch.clear();
    ProjectGraphNode far_right = far_left;
    far_right.id = QStringLiteral("far-right");
    far_right.position = QPointF(1000, 0);
    graph.setNodes({node, far_left, far_right});
    graph.fitGraph();

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
    const QPointF wheel_anchor(400.0, 250.0);
    const double before_zoom = graph.zoomFactor();
    const QPointF before_center = graph.graphToViewport(QPointF());
    const QPointF fixed_graph_point = (wheel_anchor - before_center) / before_zoom;
    sendWheel(graph, wheel_anchor, QPoint(0, 120));
    waitFor(130);
    if (!require(graph.zoomFactor() > before_zoom, "wheel did not zoom in"))
        return 1;
    if (!require(std::hypot((graph.graphToViewport(fixed_graph_point) - wheel_anchor).x(),
                            (graph.graphToViewport(fixed_graph_point) - wheel_anchor).y()) < 0.01,
                 "centered wheel zoom lost its anchor without reaching the scene boundary"))
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

    // A sufficiently zoomed-in scene has room for inertia; it must stop at
    // content bounds rather than allowing the entire graph to disappear.
    graph.fitGraph();
    sendWheel(graph, QPointF(400, 250), QPoint(0, 480));
    waitFor(260);
    sendMouse(graph, QEvent::MouseButtonPress, QPointF(180.0, 130.0), Qt::LeftButton);
    waitFor(20);
    sendMouse(graph, QEvent::MouseMove, QPointF(225.0, 130.0), Qt::LeftButton);
    waitFor(25);
    sendMouse(graph, QEvent::MouseMove, QPointF(267.0, 130.0), Qt::LeftButton);
    sendMouse(graph, QEvent::MouseButtonRelease, QPointF(267.0, 130.0), Qt::NoButton);
    const double at_release = graph.graphToViewport(QPointF()).x();
    waitFor(60);
    if (!require(graph.graphToViewport(QPointF()).x() > at_release + 1.0,
                 "mouse drag did not carry momentum while inside scene bounds"))
        return 1;
    // Extreme pan in either direction cannot push the scene entirely away.
    for (int direction : {-1, 1})
    {
        const QPointF origin(370.0, 210.0);
        sendMouse(graph, QEvent::MouseButtonPress, origin, Qt::LeftButton);
        sendMouse(graph, QEvent::MouseMove, origin + QPointF(direction * 20000.0, 0), Qt::LeftButton);
        sendMouse(graph, QEvent::MouseButtonRelease,
                  origin + QPointF(direction * 20000.0, 0), Qt::NoButton);
        waitFor(40);
        const QRectF bounds = graph.graphBounds();
        const double left = graph.graphToViewport(bounds.topLeft()).x();
        const double right = graph.graphToViewport(bounds.bottomRight()).x();
        if (!require(left <= 111.0 && right >= graph.width() - 111.0,
                     "drag or inertia escaped the content bounding box")) return 1;
    }

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

    // Structural graph cards are separate from revision DAG nodes. Their hit
    // targets must select the owning source/project without fabricating a
    // revision, and they must remain visible in folded history mode.
    ProjectGraphNode server_card;
    server_card.kind = ProjectGraphNode::Kind::Server;
    server_card.id = QStringLiteral("group:server:test-source");
    server_card.title = QStringLiteral("Test database");
    server_card.position = QPointF(0, 310);
    ProjectGraphNode project_card;
    project_card.kind = ProjectGraphNode::Kind::Project;
    project_card.id = QStringLiteral("group:project:test-project");
    project_card.title = QStringLiteral("Test project");
    project_card.container_id = server_card.id;
    project_card.position = QPointF(0, 155);
    ProjectGraphNode revision_card = node;
    revision_card.id = QStringLiteral("revision-with-project");
    revision_card.project_group_id = project_card.id;
    revision_card.position = QPointF(0, 0);
    graph.setShowFullTree(true);
    graph.setNodes({server_card, project_card, revision_card});
    graph.fitGraph();
    QString selected_source;
    QString selected_project;
    QObject::connect(&graph, &ProjectRevisionGraph::serverActivated,
        &graph, [&selected_source](const QString &id) { selected_source = id; });
    QObject::connect(&graph, &ProjectRevisionGraph::projectActivated,
        &graph, [&selected_project](const QString &id) { selected_project = id; });
    click(graph, graph.nodeViewportPosition(server_card.id));
    click(graph, graph.nodeViewportPosition(project_card.id));
    if (!require(selected_source == QStringLiteral("test-source")
                 && selected_project == QStringLiteral("test-project"),
                 "server/project cards did not emit their own selection signals")) return 1;
    ProjectGraphNode branch_card;
    branch_card.kind = ProjectGraphNode::Kind::Branch;
    branch_card.id = QStringLiteral("group:branch:test-branch");
    branch_card.branch_id = QStringLiteral("test-branch");
    branch_card.title = QStringLiteral("main");
    branch_card.container_id = project_card.id;
    branch_card.head_revision_id = revision_card.id;
    branch_card.position = QPointF(0, -150);
    QString selected_branch;
    QObject::connect(&graph, &ProjectRevisionGraph::branchActivated,
        &graph, [&selected_branch](const QString &id) { selected_branch = id; });
    graph.setNodes({server_card, project_card, revision_card, branch_card});
    graph.fitGraph();
    click(graph, graph.nodeViewportPosition(branch_card.id));
    if (!require(selected_branch == QStringLiteral("test-branch"),
                 "branch cards did not activate their stable branch ID")) return 1;
    graph.setSelection(server_card.id);
    if (!require(graph.selectionId() == server_card.id,
                 "server selection is not represented in the graph")) return 1;
    graph.setSelection(project_card.id);
    if (!require(graph.selectionId() == project_card.id,
                 "project selection is not represented in the graph")) return 1;
    graph.setSelection(branch_card.id);
    if (!require(graph.selectionId() == branch_card.id,
                 "branch selection is not represented in the graph")) return 1;
    graph.setShowFullTree(false);
    if (!require(!graph.nodeViewportPosition(server_card.id).isNull()
                 && !graph.nodeViewportPosition(project_card.id).isNull(),
                 "folded revision mode removed the server/project hierarchy")) return 1;

    // An unbranched graph (database -> project -> revision -> branch) must
    // occupy a single geometric centreline, with no curved misalignment.
    QVector<ProjectGraphNode> straight{server_card, project_card, revision_card, branch_card};
    ProjectGraphPlacement::place(straight, 1.8);
    for (const ProjectGraphNode &placed : straight)
        if (!require(std::abs(placed.position.x() - straight.first().position.x()) < 0.01,
                     "single-chain graph is not horizontally centred")) return 1;

    // Placement regression: three independent databases and uneven project
    // revision histories should not become an infinite horizontal strip.
    QVector<ProjectGraphNode> compound;
    for (int database_index = 0; database_index < 8; ++database_index)
    {
        const QString database_id = QStringLiteral("group:server:db-%1").arg(database_index);
        ProjectGraphNode database;
        database.id = database_id;
        database.kind = ProjectGraphNode::Kind::Server;
        compound.append(database);
        const int project_count = database_index == 0 ? 5 : 1;
        for (int project_index = 0; project_index < project_count; ++project_index)
        {
            const QString project_id = QStringLiteral("group:project:db-%1:p-%2")
                .arg(database_index).arg(project_index);
            ProjectGraphNode project;
            project.id = project_id;
            project.kind = ProjectGraphNode::Kind::Project;
            project.container_id = database_id;
            compound.append(project);
            const int history_length = project_index == 0 && database_index == 0 ? 9 : 2;
            for (int revision_index = 0; revision_index < history_length; ++revision_index)
            {
                ProjectGraphNode revision;
                revision.id = QStringLiteral("db-%1:p-%2:r-%3")
                    .arg(database_index).arg(project_index).arg(revision_index);
                revision.project_group_id = project_id;
                if (revision_index > 0)
                    revision.parents.append(QStringLiteral("db-%1:p-%2:r-%3")
                        .arg(database_index).arg(project_index).arg(revision_index - 1));
                compound.append(revision);
            }
        }
    }
    ProjectGraphPlacement::place(compound, 1.8);
    int distinct_database_rows = 0;
    QSet<int> database_rows;
    for (const ProjectGraphNode &placed : compound)
    {
        if (placed.kind == ProjectGraphNode::Kind::Server)
            database_rows.insert(qRound(placed.position.y()));
        if (placed.kind == ProjectGraphNode::Kind::Revision)
        {
            for (const QString &parent_id : placed.parents)
                for (const ProjectGraphNode &parent : compound)
                    if (parent.id == parent_id
                        && !require(parent.position.y() > placed.position.y(),
                            "layout must place all ancestors above descendants")) return 1;
        }
    }
    distinct_database_rows = database_rows.size();
    if (!require(distinct_database_rows >= 2 && distinct_database_rows <= 5,
                 "database layout must wrap into balanced rows")) return 1;
    for (int index = 0; index < compound.size(); ++index)
    {
        const ProjectGraphNode &lhs = compound.at(index);
        const double lw = lhs.kind == ProjectGraphNode::Kind::Server ? 252.0 : 224.0;
        const double lh = lhs.kind == ProjectGraphNode::Kind::Server ? 76.0
            : lhs.kind == ProjectGraphNode::Kind::Project ? 68.0 : 84.0;
        for (int other = index + 1; other < compound.size(); ++other)
        {
            const ProjectGraphNode &rhs = compound.at(other);
            const double rw = rhs.kind == ProjectGraphNode::Kind::Server ? 252.0 : 224.0;
            const double rh = rhs.kind == ProjectGraphNode::Kind::Server ? 76.0
                : rhs.kind == ProjectGraphNode::Kind::Project ? 68.0 : 84.0;
            const bool collision = std::abs(lhs.position.x() - rhs.position.x()) < (lw + rw) / 2.0
                && std::abs(lhs.position.y() - rhs.position.y()) < (lh + rh) / 2.0;
            if (!require(!collision, "compound layout allows overlapping node cards")) return 1;
        }
    }

    // Same-depth branches must have separate cards even when their textual
    // branch label is identical. Order should be stable across API responses.
    QVector<ProjectGraphNode> same_branch;
    ProjectGraphNode db = server_card;
    db.id = QStringLiteral("group:server:stable");
    ProjectGraphNode proj = project_card;
    proj.id = QStringLiteral("group:project:stable");
    proj.container_id = db.id;
    same_branch.append(db);
    same_branch.append(proj);
    for (const QString &suffix : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")})
    {
        ProjectGraphNode rev;
        rev.id = suffix;
        rev.branch = QStringLiteral("main");
        rev.project_group_id = proj.id;
        same_branch.append(rev);
    }
    ProjectGraphPlacement::place(same_branch, 1.8);
    QHash<QString, QPointF> expected;
    for (const ProjectGraphNode &entry : same_branch)
        expected.insert(entry.id, entry.position);
    if (!require(std::abs(expected.value(QStringLiteral("a")).x()
                          - expected.value(QStringLiteral("b")).x()) >= 224.0,
                 "same-branch revisions at one layer overlap")) return 1;
    std::swap(same_branch[2], same_branch[4]);
    ProjectGraphPlacement::place(same_branch, 1.8);
    for (const ProjectGraphNode &entry : same_branch)
        if (!require(entry.position == expected.value(entry.id),
                     "fetch order changed deterministic graph placement")) return 1;

    std::puts("Project revision graph navigation and hierarchy tests passed");
    return 0;
}
