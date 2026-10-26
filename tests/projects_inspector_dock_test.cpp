#include "tabs/tab_projects_widget.h"
#include "widgets/group_box_collapsible.h"

#include <QApplication>
#include <QDockWidget>
#include <QComboBox>
#include <QDialog>
#include <QListWidget>
#include <QLabel>
#include <QFrame>
#include <QEventLoop>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QMainWindow>
#include <QSplitter>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

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

    // Servers are managed as GUI-only profiles. Selecting one scopes the
    // preview projects and reopens its inspector, without creating a session.
    GroupBoxCollapsible *server_section = projects.findChild<GroupBoxCollapsible *>(
        QStringLiteral("serversCollapsibleGroup"));
    QTreeWidget *servers = projects.findChild<QTreeWidget *>(QStringLiteral("serversTree"));
    QLineEdit *endpoint = projects.findChild<QLineEdit *>(QStringLiteral("projectServerEndpoint"));
    QComboBox *auth_mode = projects.findChild<QComboBox *>(QStringLiteral("projectServerAuthMode"));
    QWidget *server_options = projects.findChild<QWidget *>(QStringLiteral("projectServerOptions"));
    if (!require(server_section != nullptr && servers != nullptr
                     && servers->topLevelItemCount() == 2
                     && endpoint != nullptr && auth_mode != nullptr && server_options != nullptr,
                 "Servers panel or inspector configuration fields are missing"))
        return 1;
    if (!require(servers->parentWidget() != nullptr && server_section->isVisible(),
                 "Servers section is not visible above the project tree"))
        return 1;
    if (!require(servers->minimumHeight() == 86 && servers->maximumHeight() == 112,
                 "Servers tree must remain compact so Projects and Revisions can expand"))
        return 1;
    for (QLabel *label : projects.findChildren<QLabel *>())
        if (!require(label->text() != QStringLiteral("View:"),
                     "obsolete graph View: label is still present"))
            return 1;
    dock->close();
    servers->setCurrentItem(servers->topLevelItem(1));
    app.processEvents();
    if (!require(dock->isVisible() && name->text() == QStringLiteral("Remote preview")
                     && server_options->isVisible() && endpoint->text().startsWith(QStringLiteral("https://")),
                 "remote server selection did not show its preview inspector"))
        return 1;
    if (!require(project_tree->topLevelItemCount() == 1
                     || project_tree->topLevelItemCount() == 0,
                 "projects from the local server leaked into the remote server view"))
        return 1;
    endpoint->setText(QStringLiteral("https://new-preview.example.invalid"));
    QMetaObject::invokeMethod(endpoint, "editingFinished", Qt::DirectConnection);
    auth_mode->setCurrentIndex(2);
    app.processEvents();
    servers->setCurrentItem(servers->topLevelItem(0));
    app.processEvents();
    if (!require(project_tree->topLevelItemCount() >= 2
                     && server_options->isVisible(),
                 "switching to the local preview server failed"))
        return 1;
    servers->setCurrentItem(servers->topLevelItem(1));
    app.processEvents();
    if (!require(endpoint->text() == QStringLiteral("https://new-preview.example.invalid")
                     && auth_mode->currentText() == QStringLiteral("User login"),
                 "server configuration preview was lost on profile switch"))
        return 1;
    servers->setCurrentItem(servers->topLevelItem(0));
    projects.selectBranchByName(QStringLiteral("main"));
    app.processEvents();
    if (!require(!server_options->isVisible(),
                 "server-specific inspector fields remain visible for a branch"))
        return 1;

    QPushButton *heads_view = projects.findChild<QPushButton *>(
        QStringLiteral("revisionsHeadsOnlyButton"));
    QPushButton *full_tree_view = projects.findChild<QPushButton *>(
        QStringLiteral("revisionsFullTreeButton"));
    if (!require(heads_view != nullptr && full_tree_view != nullptr
                     && heads_view->isCheckable() && full_tree_view->isCheckable()
                     && full_tree_view->isChecked() && !heads_view->isChecked(),
                 "Revisions must default to the Full tree view"))
        return 1;
    const auto find_revision = [revisions](const QString &revision_id) -> QTreeWidgetItem *
    {
        for (QTreeWidgetItemIterator it(revisions); *it; ++it)
            if ((*it)->data(0, Qt::UserRole).toString()
                == QStringLiteral("r:") + revision_id)
                return *it;
        return nullptr;
    };
    const QString baseline_id = QStringLiteral("93f0de41");
    const QString main_head_id = QStringLiteral("c8d102ab");
    const QString scenario_start_id = QStringLiteral("a24f918d");
    const QString scenario_head_id = QStringLiteral("ec9045df");
    QTreeWidgetItem *baseline = find_revision(baseline_id);
    QTreeWidgetItem *main_head = find_revision(main_head_id);
    QTreeWidgetItem *scenario_start = find_revision(scenario_start_id);
    QTreeWidgetItem *scenario_head = find_revision(scenario_head_id);
    if (!require(revisions->topLevelItemCount() == 1 && baseline != nullptr
                     && main_head != nullptr && scenario_start != nullptr
                     && scenario_head != nullptr && baseline->parent() == nullptr
                     && main_head->parent() == baseline
                     && scenario_start->parent() == baseline
                     && scenario_head->parent() == scenario_start,
                 "Full tree should nest revisions under their parents"))
        return 1;
    if (!require(baseline->text(0) == QStringLiteral("Network baseline")
                     && main_head->text(0) == QStringLiteral("Validated network")
                     && scenario_start->text(0) == QStringLiteral("Scenario start")
                     && scenario_head->text(0) == QStringLiteral("Pressure tuning"),
                 "Revision labels must contain names only, never hashes"))
        return 1;

    QPushButton *graph_heads_view = projects.findChild<QPushButton *>(
        QStringLiteral("projectsGraphHeadsOnlyButton"));
    QPushButton *graph_full_view = projects.findChild<QPushButton *>(
        QStringLiteral("projectsGraphFullTreeButton"));
    QFrame *graph_view_switch = projects.findChild<QFrame *>(
        QStringLiteral("projectsGraphViewSegmentedControl"));
    if (!require(graph_heads_view != nullptr && graph_full_view != nullptr
                     && graph_view_switch != nullptr
                     && graph_heads_view->parentWidget() == graph_view_switch
                     && graph_full_view->parentWidget() == graph_view_switch
                     && graph_full_view->isChecked() && graph->showsFullTree(),
                 "graph modes must be visibly grouped with Full tree selected by default"))
        return 1;
    graph_heads_view->click();
    app.processEvents();
    if (!require(graph_heads_view->isChecked() && !graph->showsFullTree()
                     && graph->omissionCount() == 1
                     && !graph->revisionViewportPosition(baseline_id).isNull()
                     && revisions->topLevelItemCount() == 1,
                 "graph heads mode must retain the common ancestor and fold history"))
        return 1;
    graph->expandOmission(0);
    app.processEvents();
    if (!require(!graph->showsFullTree() && graph->omissionCount() == 0
                     && revisions->topLevelItemCount() == 1,
                 "graph omission expansion changed sidebar filter or did not unfold"))
        return 1;
    graph_full_view->click();
    app.processEvents();
    if (!require(graph_full_view->isChecked() && !graph_heads_view->isChecked()
                     && graph->showsFullTree() && graph->omissionCount() == 0,
                 "graph Full tree did not restore hidden history"))
        return 1;
    graph_heads_view->click();
    app.processEvents();
    if (!require(graph_heads_view->isChecked() && !graph_full_view->isChecked()
                     && !graph->showsFullTree() && graph->omissionCount() == 1,
                 "switching back to Editable heads must re-fold the graph"))
        return 1;
    graph->expandOmission(0);
    graph_heads_view->click();
    app.processEvents();
    if (!require(graph_heads_view->isChecked() && graph->omissionCount() == 1,
                 "pressing the active Editable heads control must re-fold expanded history"))
        return 1;

    heads_view->click();
    app.processEvents();
    if (!require(heads_view->isChecked() && !full_tree_view->isChecked()
                     && revisions->topLevelItemCount() == 2
                     && find_revision(main_head_id) != nullptr
                     && find_revision(scenario_head_id) != nullptr
                     && find_revision(baseline_id) == nullptr
                     && find_revision(scenario_start_id) == nullptr,
                 "Editable heads mode must show only distinct branch heads"))
        return 1;
    // The filter must also follow the active project instead of keeping
    // stale branch heads from the previously opened project.
    projects.selectProjectByName(QStringLiteral("Sample Pump Station"));
    app.processEvents();
    if (!require(revisions->topLevelItemCount() == 1
                     && revisions->topLevelItem(0)->text(0) == QStringLiteral("Initial layout")
                     && find_revision(main_head_id) == nullptr,
                 "Heads-only view retained revisions from the previous project"))
        return 1;
    projects.selectProjectByName(QStringLiteral("Sample North Network"));
    app.processEvents();
    if (!require(heads_view->isChecked() && revisions->topLevelItemCount() == 2
                     && find_revision(main_head_id) != nullptr,
                 "Heads-only view did not follow a project switch"))
        return 1;
    revisions->setCurrentItem(find_revision(scenario_head_id));
    app.processEvents();
    if (!require(name->text() == QStringLiteral("Pressure tuning")
                     && find_revision(scenario_head_id)->isSelected(),
                 "Selecting a head did not synchronize the inspector and tree"))
        return 1;
    full_tree_view->click();
    app.processEvents();
    if (!require(full_tree_view->isChecked() && revisions->topLevelItemCount() == 1
                     && find_revision(scenario_head_id) != nullptr
                     && find_revision(scenario_head_id)->isSelected(),
                 "Returning to Full tree did not keep the selected head"))
        return 1;
    revisions->setCurrentItem(find_revision(baseline_id));
    app.processEvents();
    heads_view->click();
    app.processEvents();
    if (!require(heads_view->isChecked() && find_revision(baseline_id) == nullptr
                     && find_revision(main_head_id) != nullptr
                     && find_revision(main_head_id)->isSelected()
                     && name->text() == QStringLiteral("Validated network"),
                 "Filtering a historical selection must select its branch head"))
        return 1;
    // A historical revision picked in the central DAG should be made visible
    // in the sidebar rather than silently losing tree synchronization.
    graph->revisionActivated(baseline_id);
    app.processEvents();
    if (!require(full_tree_view->isChecked()
                     && find_revision(baseline_id) != nullptr
                     && find_revision(baseline_id)->isSelected()
                     && name->text() == QStringLiteral("Network baseline"),
                 "Graph selection of history should reveal the Full tree"))
        return 1;
    // Selecting history must change the branch-creation *source*, not inflate
    // its button label and potentially break the compact three-action row.
    QPushButton *revision_branch_action = projects.findChild<QPushButton *>(
        QStringLiteral("projectsCreateBranchButton"));
    if (!require(revision_branch_action != nullptr
                     && revision_branch_action->text() == QStringLiteral("+ Branch"),
                 "historical revision selection expanded the + Branch button label"))
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
    if (!require(server_section->isVisible() && server_section->y() < project_section->y(),
                 "Servers section must appear above Projects & Branches"))
        return 1;
    // Projects contain only project/branch records. The example library is a
    // separate entry point, independent of the selected server profile.
    // The two editing actions must fit on one row without horizontal overflow.
    QScrollArea *sidebar = projects.findChild<QScrollArea *>(
        QStringLiteral("projectsSidebarScrollArea"));
    QPushButton *branch_action = projects.findChild<QPushButton *>(
        QStringLiteral("projectsCreateBranchButton"));
    QPushButton *import_action = projects.findChild<QPushButton *>(
        QStringLiteral("projectsImportButton"));
    QPushButton *browse_action = projects.findChild<QPushButton *>(
        QStringLiteral("projectsBrowseExamplesButton"));
    if (!require(sidebar != nullptr && branch_action != nullptr
                     && import_action != nullptr && browse_action != nullptr,
                 "sidebar action row or Example Library entry is missing"))
        return 1;
    if (!require(branch_action->text() == QStringLiteral("+ Branch")
                     && import_action->text() == QStringLiteral("Import")
                     && browse_action->text().contains(QStringLiteral("examples")),
                 "sidebar action labels are incorrect"))
        return 1;
    const auto actions_fit = [&]()
    {
        const QRect branch = branch_action->geometry();
        const QRect import = import_action->geometry();
        const QPoint branch_left = branch_action->mapTo(sidebar->viewport(), QPoint(0, 0));
        const QPoint import_right = import_action->mapTo(
            sidebar->viewport(), QPoint(import_action->width() - 1, 0));
        const QPoint browse_right = browse_action->mapTo(
            sidebar->viewport(), QPoint(browse_action->width() - 1, 0));
        return branch.y() == import.y() && branch.right() < import.left()
            && branch.width() > 0 && import.width() > 0
            && browse_action->geometry().y() >= 0
            && branch_left.x() >= 0 && import_right.x() < sidebar->viewport()->width()
            && browse_right.x() < sidebar->viewport()->width()
            && sidebar->horizontalScrollBarPolicy() == Qt::ScrollBarAsNeeded
            && sidebar->horizontalScrollBar()->maximum() == 0
            && !sidebar->horizontalScrollBar()->isVisible();
    };
    if (!require(actions_fit(), "sidebar buttons are not in a single non-scrolling row"))
        return 1;
    // Simulate the minimum-width pane, regardless of the current user splitter size.
    splitter->setSizes(QList<int>{240, 900});
    app.processEvents();
    if (!require(actions_fit(), "narrow sidebar creates an overflowing action row"))
        return 1;
    splitter->setSizes(QList<int>{295, 720});
    app.processEvents();

    const int default_project_height = project_tree->height();
    const int default_revision_height = revisions->height();
    const int default_project_section_height = project_section->height();
    const int default_revision_section_height = revision_section->height();
    const int default_servers_section_height = server_section->height();
    const int default_content_height = sidebar->widget()->height();
    const int default_viewport_height = sidebar->viewport()->height();
    if (!require(default_project_height >= 190 && default_revision_height >= 220,
                 "sidebar trees fell below their existing minimum heights"))
        return 1;
    projects.resize(1250, 1040);
    app.processEvents();
    const int project_growth = project_tree->height() - default_project_height;
    const int revision_growth = revisions->height() - default_revision_height;
    const int project_section_growth = project_section->height() - default_project_section_height;
    const int revision_section_growth = revision_section->height() - default_revision_section_height;
    // The initial sidebar can scroll vertically because its three sections
    // already exceed the short viewport. A fixed "+50 px per tree" threshold
    // mistakes viewport growth for *available* growth; much of the resize
    // first removes that initial overflow. Verify instead that both trees
    // actually grow and that the height received by each section is passed
    // through to its tree (rather than becoming empty space in its group).
    // A caption may rewrap when the sidebar's vertical scrollbar disappears,
    // changing the non-tree portion of a group by one line of text.
    const int caption_reflow_tolerance = 24;
    const bool trees_absorb_section_growth =
        project_growth >= 20 && revision_growth >= 20
        && std::abs(project_growth - project_section_growth) <= caption_reflow_tolerance
        && std::abs(revision_growth - revision_section_growth) <= caption_reflow_tolerance;
    const bool server_remains_compact =
        std::abs(server_section->height() - default_servers_section_height) <= 2;
    const bool content_fills_viewport =
        std::abs(sidebar->widget()->height() - sidebar->viewport()->height()) <= 2;
    if (!(trees_absorb_section_growth && server_remains_compact && content_fills_viewport))
    {
        std::fprintf(stderr,
            "projects_inspector_dock: sidebar tree growth (initial -> tall): "
            "projects %d -> %d (section %+d), revisions %d -> %d (section %+d), "
            "servers tree %d, servers section %d -> %d (hint %d); "
            "viewport height %d -> %d, content height %d -> %d\n",
            default_project_height, project_tree->height(), project_section_growth,
            default_revision_height, revisions->height(), revision_section_growth,
            servers->height(), default_servers_section_height, server_section->height(),
            server_section->sizeHint().height(),
            default_viewport_height, sidebar->viewport()->height(),
            default_content_height, sidebar->widget()->height());
        if (!require(false, "sidebar extra height was not allocated to the two trees"))
            return 1;
    }
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
    revisions->setCurrentItem(find_revision(main_head_id));
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

    // The bundled examples must NOT be injected into the selected server's
    // project tree. Their library works independently of that server.
    for (QTreeWidgetItemIterator it(project_tree); *it; ++it)
        if (!require(!(*it)->data(0, Qt::UserRole).toString().startsWith(QStringLiteral("e:"))
                         && (*it)->data(0, Qt::UserRole).toString() != QStringLiteral("examples-root"),
                     "built-in examples leaked into the server project hierarchy"))
            return 1;

    QString opened_resource;
    QString opened_file;
    QObject::connect(&projects, &ProjectsWidget::builtinExampleRequested, &projects,
        [&](const QString &resource, const QString &file)
    {
        opened_resource = resource;
        opened_file = file;
    });
    bool library_opened = false;
    QTimer::singleShot(0, &projects, [&]()
    {
        QDialog *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (dialog == nullptr) return;
        QListWidget *list = dialog->findChild<QListWidget *>(QStringLiteral("exampleLibraryList"));
        QPushButton *open = dialog->findChild<QPushButton *>(QStringLiteral("exampleLibraryOpenButton"));
        QPushButton *create = dialog->findChild<QPushButton *>(QStringLiteral("exampleLibraryCreateButton"));
        QLineEdit *filter = dialog->findChild<QLineEdit *>(QStringLiteral("exampleLibraryFilter"));
        library_opened = list != nullptr && list->count() == 6 && open != nullptr
                         && create != nullptr && filter != nullptr;
        if (!library_opened) { dialog->reject(); return; }
        filter->setText(QStringLiteral("leakage"));
        library_opened = list->currentItem() != nullptr
            && list->currentItem()->text().contains(QStringLiteral("Leakage"))
            && open->isEnabled();
        if (library_opened) open->click();
        else dialog->reject();
    });
    browse_action->click();
    if (!require(library_opened
                     && opened_resource == QStringLiteral(":/examples/epanet/tutorial-leakage.inp")
                     && opened_file == QStringLiteral("tutorial-leakage.inp"),
                 "Example Library did not open the selected bundled resource through the existing importer"))
        return 1;

    // A template can be converted to an in-memory project on ANY configured
    // server, with an initial branch/revision placeholder. No import or DB
    // write occurs in this path.
    bool wizard_ok = false;
    opened_resource.clear();
    QTimer::singleShot(0, &projects, [&]()
    {
        QDialog *browser = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (browser == nullptr) return;
        QListWidget *list = browser->findChild<QListWidget *>(QStringLiteral("exampleLibraryList"));
        QPushButton *create = browser->findChild<QPushButton *>(QStringLiteral("exampleLibraryCreateButton"));
        if (list == nullptr || create == nullptr) { browser->reject(); return; }
        list->setCurrentRow(0); // NET3
        QTimer::singleShot(0, &projects, [&]()
        {
            QDialog *wizard = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (wizard == nullptr) return;
            QComboBox *server = wizard->findChild<QComboBox *>(QStringLiteral("exampleCreateServer"));
            QLineEdit *project_name = wizard->findChild<QLineEdit *>(QStringLiteral("exampleCreateProjectName"));
            QLineEdit *branch_name = wizard->findChild<QLineEdit *>(QStringLiteral("exampleCreateBranchName"));
            QPushButton *confirm = wizard->findChild<QPushButton *>(QStringLiteral("exampleCreateConfirmButton"));
            QLabel *notice = wizard->findChild<QLabel *>(QStringLiteral("exampleCreatePreviewNotice"));
            wizard_ok = server != nullptr && server->count() == 2 && project_name != nullptr
                && branch_name != nullptr && confirm != nullptr && notice != nullptr
                && notice->text().contains(QStringLiteral("not copied"));
            if (!wizard_ok) { wizard->reject(); return; }
            server->setCurrentIndex(1); // Remote preview
            project_name->setText(QStringLiteral("NET3 Remote Experiment"));
            branch_name->setText(QStringLiteral("main"));
            wizard_ok = confirm->isEnabled();
            if (wizard_ok) confirm->click();
            else wizard->reject();
        });
        create->click();
    });
    browse_action->click();
    app.processEvents();
    if (!require(wizard_ok && opened_resource.isEmpty()
                     && name->text() == QStringLiteral("NET3 Remote Experiment")
                     && project_tree->topLevelItemCount() == 1
                     && revisions->topLevelItemCount() == 1
                     && revisions->topLevelItem(0)->text(0) == QStringLiteral("Initial import (preview)"),
                 "creating a project from an example did not create a remote-scoped preview"))
        return 1;
    QLabel *metadata = projects.findChild<QLabel *>(QStringLiteral("projectInspectorMetadata"));
    if (!require(metadata != nullptr
                     && metadata->text().contains(QStringLiteral("NOT stored")),
                 "example-origin projects must disclose that no network was saved"))
        return 1;
    // Restore the original preview context for the rest of the regression.
    servers->setCurrentItem(servers->topLevelItem(0));
    projects.selectBranchByName(QStringLiteral("main"));
    app.processEvents();

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
