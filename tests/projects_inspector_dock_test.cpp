#include "tabs/tab_projects_widget.h"
#include "widgets/group_box_collapsible.h"

#include <QApplication>
#include <QDockWidget>
#include <QEventLoop>
#include <QLineEdit>
#include <QPushButton>
#include <QMainWindow>
#include <QSplitter>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <cstdio>
#include <cmath>

namespace
{
bool require(bool condition, const char *reason)
{
    if (!condition)
        std::fprintf(stderr, "projects_inspector_dock: %s\n", reason);
    return condition;
}

void waitFor(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

bool near(double a, double b)
{
    return std::abs(a - b) < 0.001;
}
}

int main(int argc, char **argv)
{
    QTemporaryDir configuration_dir;
    if (!require(configuration_dir.isValid(), "could not create a temporary configuration"))
        return 1;
    qputenv("XDG_DATA_HOME", configuration_dir.path().toUtf8());
    QApplication app(argc, argv);

    ProjectsWidget projects;
    projects.resize(1250, 740);
    projects.show();
    app.processEvents();
    waitFor(120); // first fit follows the embedded dock/splitter layout

    QMainWindow *host = projects.findChild<QMainWindow *>(QStringLiteral("projectsDockHost"));
    QDockWidget *dock = projects.findChild<QDockWidget *>(QStringLiteral("projectsInspectorDock"));
    QTreeWidget *revisions = projects.findChild<QTreeWidget *>(QStringLiteral("revisionsTree"));
    QLineEdit *name = projects.findChild<QLineEdit *>(QStringLiteral("projectInspectorName"));
    ProjectRevisionGraph *graph = projects.findChild<ProjectRevisionGraph *>(
        QStringLiteral("projectRevisionGraph"));
    if (!require(host != nullptr && dock != nullptr
                     && revisions != nullptr && name != nullptr && graph != nullptr,
                 "workspace did not create its dock and inspector controls"))
        return 1;
    if (!require(!host->isWindow(),
                 "projects dock host must be embedded, not a separate top-level window"))
        return 1;
    if (!require(host->isVisible() && host->width() > 500 && host->height() > 300,
                 "projects dock host does not fill the visible workspace"))
        return 1;
    QSplitter *splitter = projects.findChild<QSplitter *>(
        QStringLiteral("projectsWorkspaceSplitter"));
    QTreeWidget *project_tree = projects.findChild<QTreeWidget *>(
        QStringLiteral("projectsTree"));
    if (!require(splitter != nullptr && splitter->isVisible()
                     && project_tree != nullptr && project_tree->isVisible()
                     && graph->isVisible() && graph->width() > 200 && graph->height() > 200,
                 "project tree or revision graph is missing from the visible layout"))
        return 1;
    // The map-fit action belongs to the application's top toolbar, not this sidebar.
    for (QPushButton *button : projects.findChildren<QPushButton *>())
    {
        if (!require(button->text() != QStringLiteral("Show network on map"),
                     "obsolete Show network on map button remains in Projects sidebar"))
            return 1;
    }
    if (!require(host->dockWidgetArea(dock) == Qt::RightDockWidgetArea,
                 "inspector is not docked on the right of the projects workspace"))
        return 1;
    if (!require(dock->features().testFlag(QDockWidget::DockWidgetClosable)
                     && dock->features().testFlag(QDockWidget::DockWidgetMovable)
                     && dock->features().testFlag(QDockWidget::DockWidgetFloatable),
                 "inspector does not support close, move and float"))
        return 1;
    if (!require(dock->isVisible(), "inspector should start open"))
        return 1;
    QPushButton *orientation_toggle = projects.findChild<QPushButton *>(
        QStringLiteral("projectsGraphOrientationToggle"));
    if (!require(orientation_toggle != nullptr && orientation_toggle->isCheckable()
                     && !orientation_toggle->isChecked() && graph->isTopDown(),
                 "graph orientation toggle did not default to Top down"))
        return 1;
    const QString root_id = QStringLiteral("93f0de41");
    const QString head_id = QStringLiteral("c8d102ab");
    if (!require(graph->revisionViewportPosition(root_id).y()
                     < graph->revisionViewportPosition(head_id).y(),
                 "default Top down graph must place root above HEAD"))
        return 1;
    // Automatic startup fit must match an explicit press of Fit graph, not
    // merely use the temporary size from widget construction.
    QPushButton *fit_button = projects.findChild<QPushButton *>(
        QStringLiteral("projectsGraphFitButton"));
    if (!require(fit_button != nullptr, "Fit graph control is missing"))
        return 1;
    const double startup_zoom = graph->zoomFactor();
    const QPointF startup_head = graph->revisionViewportPosition(head_id);
    fit_button->click();
    if (!require(near(startup_zoom, graph->zoomFactor())
                     && near(startup_head.x(), graph->revisionViewportPosition(head_id).x())
                     && near(startup_head.y(), graph->revisionViewportPosition(head_id).y()),
                 "first graph display was not already fitted as if Fit graph was clicked"))
        return 1;
    // Both sidebar trees keep their existing minimum heights and consume
    // additional vertical space as the Projects page becomes taller.
    GroupBoxCollapsible *project_section = projects.findChild<GroupBoxCollapsible *>(
        QStringLiteral("projectsCollapsibleGroup"));
    GroupBoxCollapsible *revision_section = projects.findChild<GroupBoxCollapsible *>(
        QStringLiteral("revisionsCollapsibleGroup"));
    // QGroupBox uses && for a visible ampersand rather than a mnemonic.
    if (!require(project_section != nullptr && revision_section != nullptr
                     && project_section->title() == QStringLiteral("Projects && Branches"),
                 "project/branch section is missing or incorrectly titled"))
        return 1;
    const int default_project_height = project_tree->height();
    const int default_revision_height = revisions->height();
    if (!require(default_project_height >= 190 && default_revision_height >= 220,
                 "sidebar trees fell below their existing minimum heights"))
        return 1;
    projects.resize(1250, 1040);
    app.processEvents();
    if (!require(project_tree->height() > default_project_height + 50
                     && revisions->height() > default_revision_height + 50,
                 "extra left-panel space was not distributed to both trees"))
        return 1;
    const int expanded_revision_height = revisions->height();
    project_section->setCollapsed(true);
    app.processEvents();
    if (!require(!project_tree->isVisible() && revisions->isVisible()
                     && revisions->height() > expanded_revision_height + 50,
                 "collapsing Projects & Branches did not expand the Revisions tree"))
        return 1;
    project_section->setCollapsed(false);
    app.processEvents();
    const int expanded_project_height = project_tree->height();
    revision_section->setCollapsed(true);
    app.processEvents();
    if (!require(!revisions->isVisible() && project_tree->isVisible()
                     && project_tree->height() > expanded_project_height + 50,
                 "collapsing Revisions did not expand the Projects & Branches tree"))
        return 1;
    revision_section->setCollapsed(false);
    projects.resize(1250, 740);
    app.processEvents();
    if (!require(project_tree->height() >= 190 && revisions->height() >= 220,
                 "restoring sidebar sections lost their minimum tree heights"))
        return 1;

    orientation_toggle->click();
    if (!require(orientation_toggle->isChecked() && !graph->isTopDown()
                     && graph->revisionViewportPosition(head_id).y()
                        < graph->revisionViewportPosition(root_id).y(),
                 "Bottom up toggle did not put HEAD above root"))
        return 1;
    orientation_toggle->click();
    if (!require(!orientation_toggle->isChecked() && graph->isTopDown(),
                 "Top down toggle did not restore the default orientation"))
        return 1;
    if (!require(projects.findChild<QToolButton *>(QStringLiteral("projectsInspectorToggle")) == nullptr,
                 "obsolete inspector toggle must not exist above the graph"))
        return 1;

    const double zoom_before_close = graph->zoomFactor();
    dock->close();
    app.processEvents();
    if (!require(graph->zoomFactor() == zoom_before_close,
                 "closing the inspector unexpectedly reset graph zoom"))
        return 1;
    if (!require(!dock->isVisible(), "closing inspector did not hide the dock"))
        return 1;

    // Closing is sticky while the workspace is idle. Graph camera actions
    // must not unexpectedly restore the inspector.
    graph->fitGraph();
    app.processEvents();
    if (!require(!dock->isVisible(), "graph navigation unexpectedly reopened the inspector"))
        return 1;

    // A revision selected in the left tree changes inspector contents and
    // therefore automatically restores the dock.
    if (!require(revisions->topLevelItemCount() > 0,
                 "preview revision tree is unexpectedly empty"))
        return 1;
    revisions->setCurrentItem(revisions->topLevelItem(0));
    app.processEvents();
    if (!require(dock->isVisible() && name->text() == QStringLiteral("Validated network"),
                 "selecting a revision in the tree did not reopen its inspector"))
        return 1;

    dock->close();
    app.processEvents();
    if (!require(project_tree->topLevelItemCount() >= 2, "preview project tree is empty"))
        return 1;
    project_tree->setCurrentItem(project_tree->topLevelItem(1));
    app.processEvents();
    if (!require(dock->isVisible() && name->text() == QStringLiteral("Sample Pump Station"),
                 "selecting a project in the tree did not reopen its inspector"))
        return 1;
    const QPointF south_node_center = graph->graphToViewport(QPointF(0.0, 0.0));
    if (!require(std::abs(south_node_center.x() - graph->width() / 2.0) < 1.0
                     && std::abs(south_node_center.y() - graph->height() / 2.0) < 1.0,
                 "switching projects did not automatically fit the newly loaded DAG"))
        return 1;

    // Toolbar-driven project and branch changes use the same selection path.
    dock->close();
    projects.selectProjectByName(QStringLiteral("Sample North Network"));
    app.processEvents();
    if (!require(dock->isVisible() && name->text() == QStringLiteral("Sample North Network"),
                 "toolbar project change did not reopen its inspector"))
        return 1;

    dock->close();
    projects.selectBranchByName(QStringLiteral("pressure-study"));
    app.processEvents();
    if (!require(dock->isVisible() && name->text() == QStringLiteral("pressure-study"),
                 "toolbar branch change did not reopen its inspector"))
        return 1;

    // The graph and the left tree must behave identically.
    dock->close();
    graph->revisionActivated(QStringLiteral("ec9045df"));
    app.processEvents();
    if (!require(dock->isVisible() && name->text() == QStringLiteral("Pressure tuning"),
                 "selecting a revision on the graph did not reopen its inspector"))
        return 1;

    dock->close();
    graph->branchActivated(QStringLiteral("main"));
    app.processEvents();
    if (!require(dock->isVisible() && name->text() == QStringLiteral("main"),
                 "selecting a branch on the graph did not reopen its inspector"))
        return 1;

    // Return to a revision; then close it and ask for History again.
    graph->revisionActivated(QStringLiteral("ec9045df"));
    app.processEvents();
    // Explicitly requesting History should reopen even when the same revision
    // is already selected, because its details are the requested action.
    dock->close();
    projects.showRevisionHistory();
    app.processEvents();
    if (!require(dock->isVisible() && name->text() == QStringLiteral("Pressure tuning"),
                 "History did not reopen the already selected revision"))
        return 1;

    // Built-in examples also have their own inspector content.
    if (project_tree->topLevelItemCount() > 2
        && project_tree->topLevelItem(2)->childCount() > 0)
    {
        dock->close();
        QTreeWidgetItem *example = project_tree->topLevelItem(2)->child(0);
        project_tree->setCurrentItem(example);
        app.processEvents();
        if (!require(dock->isVisible() && name->text() == example->text(0),
                     "selecting a built-in example did not reopen its inspector"))
            return 1;
    }

    // Reproduce the real application hierarchy: Projects is a page in a
    // stacked navigator, not a top-level window. The previous regression
    // left only the Projects header visible inside this layout.
    QStackedWidget navigator;
    navigator.resize(1320, 760);
    ProjectsWidget *embedded_projects = new ProjectsWidget(&navigator);
    navigator.addWidget(embedded_projects);
    navigator.addWidget(new QWidget(&navigator));
    navigator.setCurrentIndex(1);
    navigator.show();
    app.processEvents();
    navigator.setCurrentIndex(0);
    app.processEvents();
    waitFor(120); // stack activation is a separate first-visible layout path

    QMainWindow *embedded_host = embedded_projects->findChild<QMainWindow *>(
        QStringLiteral("projectsDockHost"));
    QTreeWidget *embedded_tree = embedded_projects->findChild<QTreeWidget *>(
        QStringLiteral("projectsTree"));
    ProjectRevisionGraph *embedded_graph = embedded_projects->findChild<ProjectRevisionGraph *>(
        QStringLiteral("projectRevisionGraph"));
    QDockWidget *embedded_dock = embedded_projects->findChild<QDockWidget *>(
        QStringLiteral("projectsInspectorDock"));
    if (!require(embedded_host != nullptr && !embedded_host->isWindow()
                     && embedded_host->isVisible()
                     && embedded_host->width() > 500 && embedded_host->height() > 300,
                 "embedded projects dock host disappeared after page activation"))
        return 1;
    if (!require(embedded_tree != nullptr && embedded_tree->isVisible()
                     && embedded_graph != nullptr && embedded_graph->isVisible()
                     && embedded_graph->width() > 200 && embedded_graph->height() > 200
                     && embedded_dock != nullptr && embedded_dock->isVisible(),
                 "embedded page must show project tree, graph and inspector together"))
        return 1;
    const double embedded_startup_zoom = embedded_graph->zoomFactor();
    const QPointF embedded_head_before = embedded_graph->revisionViewportPosition(head_id);
    embedded_graph->fitGraph();
    const QPointF embedded_head_after = embedded_graph->revisionViewportPosition(head_id);
    if (!require(near(embedded_startup_zoom, embedded_graph->zoomFactor())
                     && near(embedded_head_before.x(), embedded_head_after.x())
                     && near(embedded_head_before.y(), embedded_head_after.y()),
                 "the initially hidden Projects tab was not automatically fitted on activation"))
        return 1;
    embedded_dock->close();
    app.processEvents();
    if (!require(!embedded_dock->isVisible() && embedded_tree->isVisible()
                     && embedded_graph->isVisible() && embedded_graph->width() > 200,
                 "closing the embedded inspector also hid the project tree or graph"))
        return 1;
    QTreeWidget *embedded_revisions = embedded_projects->findChild<QTreeWidget *>(
        QStringLiteral("revisionsTree"));
    if (!require(embedded_revisions != nullptr && embedded_revisions->topLevelItemCount() > 0,
                 "embedded revision list is missing"))
        return 1;
    embedded_revisions->setCurrentItem(embedded_revisions->topLevelItem(0));
    app.processEvents();
    if (!require(embedded_dock->isVisible() && embedded_tree->isVisible()
                     && embedded_graph->isVisible(),
                 "selection did not restore the inspector in an embedded Projects page"))
        return 1;

    std::puts("Projects inspector dock tests passed");
    return 0;
}
