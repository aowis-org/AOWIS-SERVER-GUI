#include "tabs/tab_projects_widget.h"

#include <QApplication>
#include <QDockWidget>
#include <QLineEdit>
#include <QMainWindow>
#include <QSplitter>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>

#include <cstdio>

namespace
{
bool require(bool condition, const char *reason)
{
    if (!condition)
        std::fprintf(stderr, "projects_inspector_dock: %s\n", reason);
    return condition;
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
