#include "tabs/tab_projects_widget.h"

#include "app/builtin_examples.h"
#include "config/gui_configuration.h"
#include "widgets/group_box_collapsible.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QMainWindow>
#include <QFormLayout>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QSet>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSizePolicy>
#include <QScrollArea>
#include <QSpacerItem>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QUuid>
#include <QVBoxLayout>

#include <functional>

namespace
{
constexpr int role_key = Qt::UserRole;
QPushButton *compactButton(const QString &label, const QString &tip, QWidget *parent)
{
    QPushButton *button = new QPushButton(label, parent);
    button->setToolTip(tip);
    button->setMinimumHeight(28);
    return button;
}
QString keyFor(const QString &type, const QString &id)
{
    return type + QLatin1Char(':') + id;
}
QTreeWidgetItem *newTreeItem(QTreeWidget *tree, const QString &label, const QString &key)
{
    QTreeWidgetItem *item = new QTreeWidgetItem(tree, QStringList{label});
    item->setData(0, role_key, key);
    item->setToolTip(0, label);
    return item;
}
QTreeWidgetItem *newTreeChild(QTreeWidgetItem *parent, const QString &label, const QString &key)
{
    QTreeWidgetItem *item = new QTreeWidgetItem(parent, QStringList{label});
    item->setData(0, role_key, key);
    item->setToolTip(0, label);
    return item;
}
}

ProjectsWidget::ProjectsWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("projectsWorkspace"));
    seedPreview();

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    QFrame *header = new QFrame(this);
    header->setFrameShape(QFrame::StyledPanel);
    QHBoxLayout *header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(14, 8, 14, 8);
    QLabel *page_title = new QLabel(tr("Projects  /  Version explorer"), header);
    QFont title_font = page_title->font();
    title_font.setBold(true);
    title_font.setPointSizeF(title_font.pointSizeF() + 2.0);
    page_title->setFont(title_font);
    header_layout->addWidget(page_title);
    header_layout->addStretch();
    QLabel *preview = new QLabel(tr("LOCAL UI PREVIEW  ·  NOT CONNECTED TO DATABASE"), header);
    preview->setObjectName(QStringLiteral("projectsPreviewBanner"));
    header_layout->addWidget(preview);
    outer->addWidget(header);

    // Give Projects its own dock host: closing/floating this inspector must
    // never hide or rearrange the independent map inspector docks.
    QMainWindow *dock_host = new QMainWindow(this);
    // QMainWindow normally creates a top-level window, even with a parent.
    // The Projects page needs an *embedded* dock host: otherwise the host
    // escapes this page layout and the project tree and graph disappear.
    dock_host->setWindowFlags(Qt::Widget);
    dock_host->setObjectName(QStringLiteral("projectsDockHost"));
    dock_host->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    dock_host->setDockNestingEnabled(false);
    outer->addWidget(dock_host, 1);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, dock_host);
    splitter->setObjectName(QStringLiteral("projectsWorkspaceSplitter"));
    splitter->setChildrenCollapsible(false);
    dock_host->setCentralWidget(splitter);

    QScrollArea *left_scroll = new QScrollArea(splitter);
    left_scroll->setWidgetResizable(true);
    // Preserve the normal scroll behavior. The action layouts must fit
    // naturally without disabling horizontal scrolling to hide overflow.
    left_scroll->setObjectName(QStringLiteral("projectsSidebarScrollArea"));
    left_scroll->setFrameShape(QFrame::NoFrame);
    left_scroll->setMinimumWidth(240);
    QWidget *left = new QWidget(left_scroll);
    QVBoxLayout *left_layout = new QVBoxLayout(left);
    left_layout->setContentsMargins(8, 8, 8, 8);
    left_layout->setSpacing(10);

    // Server settings are a GUI-only preview. Selecting a server scopes the
    // project/branch/revision panels; no connection is opened or tested.
    GroupBoxCollapsible *servers_group = new GroupBoxCollapsible(tr("Servers"), left);
    servers_group->setObjectName(QStringLiteral("serversCollapsibleGroup"));
    QVBoxLayout *servers_layout = new QVBoxLayout(servers_group);
    QPushButton *add_server = compactButton(tr("+ Server"), tr("Add a server profile (preview only)"), servers_group);
    add_server->setObjectName(QStringLiteral("serversCreateButton"));
    add_server->setMinimumWidth(0);
    add_server->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    servers_layout->addWidget(add_server);
    this->server_tree = new QTreeWidget(servers_group);
    this->server_tree->setObjectName(QStringLiteral("serversTree"));
    this->server_tree->setHeaderHidden(true);
    this->server_tree->setMinimumWidth(0);
    this->server_tree->setMinimumHeight(86);
    // Keep the server selector compact. Without an upper bound its QTreeWidget
    // size hint reserves hundreds of pixels before the two revision/project
    // trees receive their stretch, even when only two servers are listed.
    // The tree remains scrollable, and its section can still be collapsed.
    this->server_tree->setMaximumHeight(112);
    this->server_tree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    servers_layout->addWidget(this->server_tree, 1);
    QLabel *server_notice = new QLabel(tr("Preview · Offline"), servers_group);
    server_notice->setObjectName(QStringLiteral("serversPreviewStatus"));
    servers_layout->addWidget(server_notice);
    left_layout->addWidget(servers_group, 0);

    // QGroupBox interprets a single & as a mnemonic marker. Use && for a visible ampersand.
    GroupBoxCollapsible *projects_group = new GroupBoxCollapsible(tr("Projects && Branches"), left);
    projects_group->setObjectName(QStringLiteral("projectsCollapsibleGroup"));
    QVBoxLayout *projects_layout = new QVBoxLayout(projects_group);
    // Place all project actions above the tree, in one compact row. Ignore
    // button width hints so translations never force horizontal scrolling.
    QHBoxLayout *project_actions = new QHBoxLayout();
    project_actions->setSpacing(4);
    QPushButton *create_project = compactButton(tr("+ Project"), tr("Add a project to this GUI preview"), projects_group);
    create_project->setObjectName(QStringLiteral("projectsCreateProjectButton"));
    this->create_branch_button = compactButton(tr("+ Branch"),
        tr("Create a branch from the current head or selected revision"), projects_group);
    this->create_branch_button->setObjectName(QStringLiteral("projectsCreateBranchButton"));
    QPushButton *import_button = compactButton(tr("Import"),
        tr("Import an EPANET network using the existing importer"), projects_group);
    import_button->setObjectName(QStringLiteral("projectsImportButton"));
    for (QPushButton *button : {create_project, this->create_branch_button, import_button})
    {
        button->setMinimumWidth(0);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    project_actions->addWidget(create_project, 4);
    project_actions->addWidget(this->create_branch_button, 4);
    project_actions->addWidget(import_button, 3);
    projects_layout->addLayout(project_actions);
    this->project_tree = new QTreeWidget(projects_group);
    this->project_tree->setObjectName(QStringLiteral("projectsTree"));
    this->project_tree->setHeaderHidden(true);
    this->project_tree->setIndentation(18);
    this->project_tree->setMinimumHeight(190);
    this->project_tree->setMinimumWidth(0);
    this->project_tree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    projects_layout->addWidget(this->project_tree, 1);
    left_layout->addWidget(projects_group, 1);

    // Bundled examples are templates, not objects in the selected DB server.
    // The library stays accessible while any server profile is selected.
    QPushButton *browse_examples_button = nullptr;
    if (guiConfiguration().examples_builtin_enable)
    {
        browse_examples_button = compactButton(tr("Browse examples…"),
            tr("Open a bundled network or prepare a project from it"), left);
        browse_examples_button->setObjectName(QStringLiteral("projectsBrowseExamplesButton"));
        // A long translated label must not widen the sidebar's scroll content.
        browse_examples_button->setMinimumWidth(0);
        browse_examples_button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        left_layout->addWidget(browse_examples_button, 0);
    }

    GroupBoxCollapsible *revisions_group = new GroupBoxCollapsible(tr("Revisions"), left);
    revisions_group->setObjectName(QStringLiteral("revisionsCollapsibleGroup"));
    QVBoxLayout *revisions_layout = new QVBoxLayout(revisions_group);
    // Two exclusive views over the same immutable revisions. The "editable"
    // heads are branch pointers, not mutable historical snapshots.
    QHBoxLayout *revision_views = new QHBoxLayout();
    revision_views->setSpacing(4);
    QPushButton *heads_button = compactButton(tr("Editable heads"),
        tr("Show only the latest revision referenced by each branch"), revisions_group);
    heads_button->setObjectName(QStringLiteral("revisionsHeadsOnlyButton"));
    this->full_tree_button = compactButton(tr("Full tree"),
        tr("Show all revisions nested by their parent relationships"), revisions_group);
    this->full_tree_button->setObjectName(QStringLiteral("revisionsFullTreeButton"));
    QButtonGroup *revision_views_group = new QButtonGroup(revisions_group);
    revision_views_group->setExclusive(true);
    for (QPushButton *button : {heads_button, this->full_tree_button})
    {
        button->setCheckable(true);
        button->setMinimumWidth(0);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        revision_views_group->addButton(button);
        revision_views->addWidget(button, 1);
    }
    this->full_tree_button->setChecked(true);
    revisions_layout->addLayout(revision_views);
    this->revision_tree = new QTreeWidget(revisions_group);
    this->revision_tree->setObjectName(QStringLiteral("revisionsTree"));
    this->revision_tree->setHeaderHidden(true);
    this->revision_tree->setMinimumHeight(220);
    this->revision_tree->setMinimumWidth(0);
    this->revision_tree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    revisions_layout->addWidget(this->revision_tree, 1);
    left_layout->addWidget(revisions_group, 1);
    // Keep the footer inert while either tree can grow. An always-expanding
    // spacer competes for height with the tree sections inside QScrollArea.
    QSpacerItem *footer_spacer = new QSpacerItem(0, 0, QSizePolicy::Minimum,
                                                QSizePolicy::Fixed);
    left_layout->addItem(footer_spacer);

    // Give the trees any spare sidebar height without reducing their existing
    // minimum heights. When a section is collapsed, its header should remain
    // compact and the other expanded tree should use the freed space.
    const auto update_section_stretches = [left_layout, footer_spacer, servers_group,
                                           projects_group, revisions_group]()
    {
        const bool projects_expanded = !projects_group->isCollapsed();
        const bool revisions_expanded = !revisions_group->isCollapsed();
        // The server selector must occupy only its own content height. A
        // vertically Preferred QGroupBox may absorb the extra viewport height
        // without growing its capped tree, leaving blank space while the two
        // actual project/revision trees are starved.
        servers_group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        projects_group->setSizePolicy(QSizePolicy::Preferred,
            projects_expanded ? QSizePolicy::Expanding : QSizePolicy::Maximum);
        revisions_group->setSizePolicy(QSizePolicy::Preferred,
            revisions_expanded ? QSizePolicy::Expanding : QSizePolicy::Maximum);
        left_layout->setStretch(left_layout->indexOf(servers_group), 0);
        left_layout->setStretch(left_layout->indexOf(projects_group), projects_expanded ? 1 : 0);
        left_layout->setStretch(left_layout->indexOf(revisions_group), revisions_expanded ? 1 : 0);
        // Only use the footer as a filler when neither tree can expand.
        const bool expand_footer = !projects_expanded && !revisions_expanded;
        footer_spacer->changeSize(0, 0, QSizePolicy::Minimum,
            expand_footer ? QSizePolicy::Expanding : QSizePolicy::Fixed);
        left_layout->setStretch(left_layout->count() - 1, expand_footer ? 1 : 0);
        left_layout->invalidate();
    };
    connect(servers_group, &GroupBoxCollapsible::signalExpanded, left,
            [update_section_stretches](GroupBoxCollapsible *) { update_section_stretches(); });
    connect(servers_group, &GroupBoxCollapsible::signalCollapsed, left,
            [update_section_stretches](GroupBoxCollapsible *) { update_section_stretches(); });
    connect(projects_group, &GroupBoxCollapsible::signalExpanded, left,
            [update_section_stretches](GroupBoxCollapsible *) { update_section_stretches(); });
    connect(projects_group, &GroupBoxCollapsible::signalCollapsed, left,
            [update_section_stretches](GroupBoxCollapsible *) { update_section_stretches(); });
    connect(revisions_group, &GroupBoxCollapsible::signalExpanded, left,
            [update_section_stretches](GroupBoxCollapsible *) { update_section_stretches(); });
    connect(revisions_group, &GroupBoxCollapsible::signalCollapsed, left,
            [update_section_stretches](GroupBoxCollapsible *) { update_section_stretches(); });
    update_section_stretches();
    left_scroll->setWidget(left);
    splitter->addWidget(left_scroll);

    QWidget *center = new QWidget(splitter);
    QVBoxLayout *center_layout = new QVBoxLayout(center);
    center_layout->setContentsMargins(0, 8, 0, 8);
    center_layout->setSpacing(5);
    QHBoxLayout *graph_header = new QHBoxLayout();
    QLabel *graph_title = new QLabel(tr("Branch & revision graph"), center);
    QFont graph_font = graph_title->font();
    graph_font.setBold(true);
    graph_title->setFont(graph_font);
    graph_header->addWidget(graph_title);
    graph_header->addStretch();
    // One visibly grouped, mutually exclusive control. The selected half
    // uses the Qt highlight color, so it is unmistakably a view-mode switch.
    // This remains independent of the left revision-tree filter.
    QFrame *graph_view_switch = new QFrame(center);
    graph_view_switch->setObjectName(QStringLiteral("projectsGraphViewSegmentedControl"));
    graph_view_switch->setStyleSheet(QStringLiteral(
        "QFrame#projectsGraphViewSegmentedControl { border: 1px solid palette(mid); "
        "border-radius: 5px; background: palette(button); } "
        "QFrame#projectsGraphViewSegmentedControl QPushButton { border: none; "
        "border-radius: 0px; padding: 3px 9px; background: transparent; } "
        "QFrame#projectsGraphViewSegmentedControl QPushButton:checked { "
        "background: palette(highlight); color: palette(highlighted-text); "
        "font-weight: bold; } "
        "QFrame#projectsGraphViewSegmentedControl QPushButton:hover:!checked { "
        "background: palette(midlight); }"));
    QHBoxLayout *graph_switch_layout = new QHBoxLayout(graph_view_switch);
    graph_switch_layout->setContentsMargins(2, 2, 2, 2);
    graph_switch_layout->setSpacing(0);
    QPushButton *graph_heads_button = compactButton(tr("Editable heads"),
        tr("Show branch heads, shared ancestors, and expandable hidden history"), graph_view_switch);
    graph_heads_button->setObjectName(QStringLiteral("projectsGraphHeadsOnlyButton"));
    QPushButton *graph_full_button = compactButton(tr("Full tree"),
        tr("Show every revision in the graph"), graph_view_switch);
    graph_full_button->setObjectName(QStringLiteral("projectsGraphFullTreeButton"));
    QButtonGroup *graph_view_group = new QButtonGroup(graph_view_switch);
    graph_view_group->setExclusive(true);
    for (QPushButton *button : {graph_heads_button, graph_full_button})
    {
        button->setCheckable(true);
        graph_view_group->addButton(button);
        graph_switch_layout->addWidget(button);
    }
    graph_full_button->setChecked(true);
    graph_header->addWidget(graph_view_switch);
    QPushButton *direction_button = compactButton(tr("Top down ↓"),
        tr("Switch the order of revisions between top-down and bottom-up"), center);
    direction_button->setObjectName(QStringLiteral("projectsGraphOrientationToggle"));
    direction_button->setCheckable(true);
    graph_header->addWidget(direction_button);
    QPushButton *fit_button = compactButton(tr("Fit graph"), tr("Fit all revisions to the canvas"), center);
    fit_button->setObjectName(QStringLiteral("projectsGraphFitButton"));
    graph_header->addWidget(fit_button);
    center_layout->addLayout(graph_header);
    this->graph = new ProjectRevisionGraph(center);
    center_layout->addWidget(this->graph, 1);
    splitter->addWidget(center);

    QScrollArea *right_scroll = new QScrollArea();
    right_scroll->setWidgetResizable(true);
    right_scroll->setFrameShape(QFrame::NoFrame);
    right_scroll->setMinimumWidth(250);
    QWidget *right = new QWidget(right_scroll);
    QVBoxLayout *right_layout = new QVBoxLayout(right);
    right_layout->setContentsMargins(12, 12, 12, 12);
    right_layout->setSpacing(12);
    this->inspector_heading = new QLabel(tr("Inspector"), right);
    QFont inspector_font = this->inspector_heading->font();
    inspector_font.setBold(true);
    inspector_font.setPointSizeF(inspector_font.pointSizeF() + 2);
    this->inspector_heading->setFont(inspector_font);
    right_layout->addWidget(this->inspector_heading);
    this->inspector_subtitle = new QLabel(right);
    this->inspector_subtitle->setWordWrap(true);
    right_layout->addWidget(this->inspector_subtitle);
    QFormLayout *form = new QFormLayout();
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    this->name_edit = new QLineEdit(right);
    this->name_edit->setObjectName(QStringLiteral("projectInspectorName"));
    this->tags_edit = new QLineEdit(right);
    this->tags_edit->setObjectName(QStringLiteral("projectInspectorTags"));
    this->tags_edit->setPlaceholderText(tr("hydraulics, approved, scenario"));
    this->description_edit = new QTextEdit(right);
    this->description_edit->setObjectName(QStringLiteral("projectInspectorDescription"));
    this->description_edit->setMinimumHeight(100);
    this->description_edit->setMaximumHeight(190);
    form->addRow(tr("Name / title"), this->name_edit);
    form->addRow(tr("Tags"), this->tags_edit);
    form->addRow(tr("Description"), this->description_edit);
    right_layout->addLayout(form);
    this->server_options = new QWidget(right);
    this->server_options->setObjectName(QStringLiteral("projectServerOptions"));
    QFormLayout *server_form = new QFormLayout(this->server_options);
    server_form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    server_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    this->server_endpoint_edit = new QLineEdit(this->server_options);
    this->server_endpoint_edit->setObjectName(QStringLiteral("projectServerEndpoint"));
    this->server_endpoint_edit->setPlaceholderText(tr("https://database.example.invalid"));
    this->server_auth_combo = new QComboBox(this->server_options);
    this->server_auth_combo->setObjectName(QStringLiteral("projectServerAuthMode"));
    this->server_auth_combo->addItems({tr("None"), tr("API token"), tr("User login")});
    server_form->addRow(tr("Server URL"), this->server_endpoint_edit);
    server_form->addRow(tr("Authentication (future)"), this->server_auth_combo);
    QLabel *server_warning = new QLabel(tr("Preview settings only. No connection, credentials, or tokens are stored."), this->server_options);
    server_warning->setWordWrap(true);
    server_form->addRow(server_warning);
    right_layout->addWidget(this->server_options);
    this->metadata_info = new QLabel(right);
    this->metadata_info->setObjectName(QStringLiteral("projectInspectorMetadata"));
    this->metadata_info->setWordWrap(true);
    this->metadata_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    right_layout->addWidget(this->metadata_info);
    QLabel *preview_note = new QLabel(tr("Changes here update only this in-memory preview. No server records are written."), right);
    preview_note->setWordWrap(true);
    preview_note->setObjectName(QStringLiteral("inspectorPreviewNote"));
    right_layout->addWidget(preview_note);
    // Renaming is handled by the Name field. Destructive operations belong
    // in the inspector for the selected server/project/branch, not in trees.
    this->inspector_delete_button = compactButton(tr("Delete"),
        tr("Remove the selected item from the in-memory preview"), right);
    this->inspector_delete_button->setObjectName(QStringLiteral("projectInspectorDeleteButton"));
    QHBoxLayout *inspector_actions = new QHBoxLayout();
    inspector_actions->addStretch(1);
    inspector_actions->addWidget(this->inspector_delete_button);
    right_layout->addLayout(inspector_actions);
    right_layout->addStretch(1);
    right_scroll->setWidget(right);
    this->inspector_dock = new QDockWidget(tr("Project Inspector"), dock_host);
    this->inspector_dock->setObjectName(QStringLiteral("projectsInspectorDock"));
    this->inspector_dock->setAllowedAreas(Qt::RightDockWidgetArea);
    this->inspector_dock->setFeatures(QDockWidget::DockWidgetClosable |
                                      QDockWidget::DockWidgetMovable |
                                      QDockWidget::DockWidgetFloatable);
    this->inspector_dock->setWidget(right_scroll);
    dock_host->addDockWidget(Qt::RightDockWidgetArea, this->inspector_dock);

    splitter->setSizes(QList<int>{295, 720});
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    connect(add_server, &QPushButton::clicked, this, &ProjectsWidget::createServer);
    connect(this->server_tree, &QTreeWidget::itemSelectionChanged, this, [this]
    {
        const QList<QTreeWidgetItem *> items = this->server_tree->selectedItems();
        if (items.isEmpty() || this->updating_ui) return;
        const QString key = items.first()->data(0, role_key).toString();
        if (key.startsWith(QStringLiteral("s:"))) select(SelectionKind::Server, key.mid(2));
    });
    connect(create_project, &QPushButton::clicked, this, &ProjectsWidget::createProject);
    connect(this->create_branch_button, &QPushButton::clicked, this, [this] { createBranch(); });
    connect(import_button, &QPushButton::clicked, this, &ProjectsWidget::importProjectRequested);
    if (browse_examples_button != nullptr)
        connect(browse_examples_button, &QPushButton::clicked,
                this, &ProjectsWidget::browseExamples);
    connect(graph_heads_button, &QPushButton::clicked, this, [this]
    {
        // A historical selection must not silently remain offscreen when the
        // graph displays only branch heads.
        if (this->selection_kind == SelectionKind::Revision && !isBranchHead(this->selected_id))
        {
            if (ProjectPreviewBranch *branch = currentBranch())
                if (isBranchHead(branch->head))
                    select(SelectionKind::Revision, branch->head);
        }
        this->graph->setShowFullTree(false);
    });
    connect(graph_full_button, &QPushButton::clicked, this->graph,
            [this]() { this->graph->setShowFullTree(true); });
    connect(fit_button, &QPushButton::clicked, this->graph, &ProjectRevisionGraph::fitGraph);
    connect(direction_button, &QPushButton::toggled, this,
            [this, direction_button](bool bottom_up)
    {
        direction_button->setText(bottom_up ? tr("Bottom up ↑") : tr("Top down ↓"));
        this->graph->setTopDown(!bottom_up);
    });
    connect(this->project_tree, &QTreeWidget::itemSelectionChanged, this, [this]
    {
        const QList<QTreeWidgetItem *> items = this->project_tree->selectedItems();
        if (items.isEmpty() || this->updating_ui) return;
        const QString key = items.first()->data(0, role_key).toString();
        if (key.startsWith(QStringLiteral("p:"))) select(SelectionKind::Project, key.mid(2));
        else if (key.startsWith(QStringLiteral("b:"))) select(SelectionKind::Branch, key.mid(2));
    });
    connect(heads_button, &QPushButton::clicked, this, [this]
    {
        this->show_full_revision_tree = false;
        populateRevisionTree();
        // A historical selection cannot remain highlighted in Heads-only mode.
        // Follow its branch to the editable head and synchronize all panes.
        if (this->selection_kind == SelectionKind::Revision && !isBranchHead(this->selected_id))
        {
            if (ProjectPreviewBranch *branch = currentBranch())
                if (isBranchHead(branch->head))
                {
                    select(SelectionKind::Revision, branch->head);
                    return;
                }
        }
        selectTreeItem(this->revision_tree,
            this->selection_kind == SelectionKind::Revision
                ? keyFor(QStringLiteral("r"), this->selected_id) : QString());
    });
    connect(this->full_tree_button, &QPushButton::clicked, this, [this]
    {
        this->show_full_revision_tree = true;
        populateRevisionTree();
        selectTreeItem(this->revision_tree,
            this->selection_kind == SelectionKind::Revision
                ? keyFor(QStringLiteral("r"), this->selected_id) : QString());
    });
    connect(this->revision_tree, &QTreeWidget::itemSelectionChanged, this, [this]
    {
        const QList<QTreeWidgetItem *> items = this->revision_tree->selectedItems();
        if (items.isEmpty() || this->updating_ui) return;
        const QString key = items.first()->data(0, role_key).toString();
        if (key.startsWith(QStringLiteral("r:"))) select(SelectionKind::Revision, key.mid(2));
    });
    connect(this->graph, &ProjectRevisionGraph::revisionActivated, this, [this](const QString &id)
    {
        select(SelectionKind::Revision, id);
    });
    connect(this->graph, &ProjectRevisionGraph::branchActivated, this, [this](const QString &name)
    {
        selectBranchByName(name);
    });
    connect(this->server_endpoint_edit, &QLineEdit::editingFinished, this, &ProjectsWidget::applyInspectorEdits);
    connect(this->server_auth_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { applyInspectorEdits(); });
    connect(this->inspector_delete_button, &QPushButton::clicked, this, [this]
    {
        if (this->selection_kind == SelectionKind::Server) deleteServer();
        else if (this->selection_kind == SelectionKind::Project
                 || this->selection_kind == SelectionKind::Branch) deleteSelection();
    });
    connect(this->name_edit, &QLineEdit::editingFinished, this, &ProjectsWidget::applyInspectorEdits);
    connect(this->tags_edit, &QLineEdit::editingFinished, this, &ProjectsWidget::applyInspectorEdits);
    connect(this->description_edit, &QTextEdit::textChanged, this, &ProjectsWidget::applyInspectorEdits);

    populateServerTree();
    populateProjectTree();
    populateRevisionTree();
    rebuildGraph();
    select(SelectionKind::Branch, this->branch_id);
}

QString ProjectsWidget::makeId(const QString &prefix)
{
    return prefix + QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
}

void ProjectsWidget::seedPreview()
{
    // Deliberately fictional examples, prominently marked as NOT DB content.
    ProjectPreviewData north;
    north.id = QStringLiteral("preview-north");
    north.name = tr("Sample North Network");
    north.description = tr("Illustrative project for the version explorer layout.");
    north.tags = {QStringLiteral("preview"), QStringLiteral("water")};
    north.branches = {
        {QStringLiteral("north-main"), QStringLiteral("main"), tr("Stable hydraulic model"), {QStringLiteral("stable")}, QStringLiteral("c8d102ab")},
        {QStringLiteral("north-study"), QStringLiteral("pressure-study"), tr("Pressure optimization experiment"), {QStringLiteral("scenario")}, QStringLiteral("ec9045df")}
    };
    north.revisions = {
        {QStringLiteral("c8d102ab"), tr("Validated network"), tr("Update base network parameters."), QStringLiteral("main"), {QStringLiteral("93f0de41")}, {QStringLiteral("validated")}, tr("Preview author"), QStringLiteral("2026-10-25")},
        {QStringLiteral("ec9045df"), tr("Pressure tuning"), tr("Adjusted pressure scenario inputs."), QStringLiteral("pressure-study"), {QStringLiteral("a24f918d")}, {QStringLiteral("experiment")}, tr("Preview author"), QStringLiteral("2026-10-24")},
        {QStringLiteral("a24f918d"), tr("Scenario start"), tr("Branch off the network baseline."), QStringLiteral("pressure-study"), {QStringLiteral("93f0de41")}, {}, tr("Preview author"), QStringLiteral("2026-10-23")},
        {QStringLiteral("93f0de41"), tr("Network baseline"), tr("Initial imported hydraulic model."), QStringLiteral("main"), {}, {QStringLiteral("baseline")}, tr("Preview author"), QStringLiteral("2026-10-22")}
    };
    this->servers = {
        {QStringLiteral("preview-local"), tr("Local preview"), QStringLiteral("http://localhost:8080"),
         tr("Illustrative local database server profile; no connection is made."), {}, QStringLiteral("None")},
        {QStringLiteral("preview-remote"), tr("Remote preview"), QStringLiteral("https://database.example.invalid"),
         tr("Illustrative remote database profile for future multi-server workflows."), {}, QStringLiteral("API token")}
    };
    this->server_id = this->servers.first().id;
    north.server_id = this->server_id;
    this->projects.append(north);
    ProjectPreviewData south;
    south.id = QStringLiteral("preview-south");
    south.name = tr("Sample Pump Station");
    south.description = tr("Another fictional project in this GUI preview.");
    south.tags = {QStringLiteral("preview")};
    south.branches = {{QStringLiteral("south-main"), QStringLiteral("main"), QString(), {}, QStringLiteral("742b0a90")}};
    south.revisions = {{QStringLiteral("742b0a90"), tr("Initial layout"), tr("Demo revision."), QStringLiteral("main"), {}, {}, tr("Preview author"), QStringLiteral("2026-10-23")}};
    south.server_id = this->server_id;
    this->projects.append(south);
    this->project_id = north.id;
    this->branch_id = north.branches.first().id;
}

ProjectPreviewServer *ProjectsWidget::currentServer()
{
    for (ProjectPreviewServer &server : this->servers)
        if (server.id == this->server_id) return &server;
    return nullptr;
}

void ProjectsWidget::populateServerTree()
{
    const QSignalBlocker blocked(this->server_tree);
    this->server_tree->clear();
    for (const ProjectPreviewServer &server : this->servers)
    {
        QTreeWidgetItem *item = newTreeItem(this->server_tree, server.name,
            keyFor(QStringLiteral("s"), server.id));
        item->setToolTip(0, tr("%1\nPreview only · Not connected").arg(server.endpoint));
    }
}

ProjectPreviewData *ProjectsWidget::currentProject()
{
    for (ProjectPreviewData &project : this->projects)
        if (project.id == this->project_id) return &project;
    return nullptr;
}

const ProjectPreviewData *ProjectsWidget::currentProject() const
{
    for (const ProjectPreviewData &project : this->projects)
        if (project.id == this->project_id) return &project;
    return nullptr;
}

ProjectPreviewBranch *ProjectsWidget::currentBranch()
{
    ProjectPreviewData *project = currentProject();
    if (project == nullptr) return nullptr;
    for (ProjectPreviewBranch &branch : project->branches)
        if (branch.id == this->branch_id) return &branch;
    return nullptr;
}

ProjectPreviewRevision *ProjectsWidget::selectedRevision()
{
    ProjectPreviewData *project = currentProject();
    if (project == nullptr) return nullptr;
    for (ProjectPreviewRevision &revision : project->revisions)
        if (revision.id == this->selected_id) return &revision;
    return nullptr;
}

QStringList ProjectsWidget::parseTags(const QString &text)
{
    QStringList output;
    for (const QString &tag : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        const QString cleaned = tag.trimmed();
        if (!cleaned.isEmpty() && !output.contains(cleaned)) output.append(cleaned);
    }
    return output;
}

void ProjectsWidget::populateProjectTree()
{
    const QSignalBlocker blocked(this->project_tree);
    this->project_tree->clear();
    for (const ProjectPreviewData &project : this->projects)
    {
        if (project.server_id != this->server_id) continue;
        QTreeWidgetItem *parent = newTreeItem(this->project_tree,
                                               QStringLiteral("▣  ") + project.name,
                                               keyFor(QStringLiteral("p"), project.id));
        parent->setExpanded(project.id == this->project_id);
        for (const ProjectPreviewBranch &branch : project.branches)
            newTreeChild(parent, QStringLiteral("⑂  ") + branch.name,
                         keyFor(QStringLiteral("b"), branch.id));
    }
}

bool ProjectsWidget::isBranchHead(const QString &revision_id) const
{
    if (revision_id.isEmpty()) return false;
    const ProjectPreviewData *project = currentProject();
    if (project == nullptr) return false;
    for (const ProjectPreviewBranch &branch : project->branches)
        if (branch.head == revision_id) return true;
    return false;
}

void ProjectsWidget::populateRevisionTree()
{
    const QSignalBlocker blocked(this->revision_tree);
    this->revision_tree->clear();
    const ProjectPreviewData *project = currentProject();
    if (project == nullptr) return;

    QHash<QString, const ProjectPreviewRevision *> revisions_by_id;
    for (const ProjectPreviewRevision &revision : project->revisions)
        revisions_by_id.insert(revision.id, &revision);

    const auto displayTitle = [this](const ProjectPreviewRevision &revision)
    {
        const QString title = revision.title.trimmed();
        return title.isEmpty() ? tr("Untitled revision") : title;
    };
    const auto labelRevision = [project](QTreeWidgetItem *item,
                                         const ProjectPreviewRevision &revision)
    {
        // The tree displays titles only: identities remain in Qt::UserRole so
        // duplicate names still select their own revision in the graph.
        QStringList head_branches;
        for (const ProjectPreviewBranch &branch : project->branches)
            if (branch.head == revision.id) head_branches.append(branch.name);
        QStringList details;
        if (!head_branches.isEmpty())
        {
            QFont label_font = item->font(0);
            label_font.setBold(true);
            item->setFont(0, label_font);
            details.append(QObject::tr("Editable head: %1").arg(head_branches.join(QStringLiteral(", "))));
        }
        if (!revision.description.isEmpty()) details.append(revision.description);
        item->setToolTip(0, details.join(QLatin1Char('\n')));
    };

    if (!this->show_full_revision_tree)
    {
        // One entry per *revision*, even when several branches share a head.
        QSet<QString> shown;
        for (const ProjectPreviewBranch &branch : project->branches)
        {
            const ProjectPreviewRevision *revision = revisions_by_id.value(branch.head, nullptr);
            if (revision == nullptr || shown.contains(revision->id)) continue;
            shown.insert(revision->id);
            QTreeWidgetItem *item = newTreeItem(this->revision_tree, displayTitle(*revision),
                keyFor(QStringLiteral("r"), revision->id));
            labelRevision(item, *revision);
        }
        return;
    }

    // QTreeWidget represents a tree rather than a DAG. Nest every revision
    // under its first available parent; additional merge edges stay visible in
    // the center graph, while each revision appears only once in the sidebar.
    QHash<QString, QTreeWidgetItem *> items_by_id;
    QSet<QString> visiting;
    std::function<QTreeWidgetItem *(const ProjectPreviewRevision &)> append_revision;
    append_revision = [&](const ProjectPreviewRevision &revision) -> QTreeWidgetItem *
    {
        if (QTreeWidgetItem *item = items_by_id.value(revision.id, nullptr)) return item;
        if (visiting.contains(revision.id)) return nullptr; // protect against bad preview data
        visiting.insert(revision.id);
        QTreeWidgetItem *parent_item = nullptr;
        for (const QString &parent_id : revision.parents)
        {
            if (const ProjectPreviewRevision *parent_revision = revisions_by_id.value(parent_id, nullptr))
            {
                parent_item = append_revision(*parent_revision);
                if (parent_item != nullptr) break;
            }
        }
        QTreeWidgetItem *item = parent_item != nullptr
            ? newTreeChild(parent_item, displayTitle(revision), keyFor(QStringLiteral("r"), revision.id))
            : newTreeItem(this->revision_tree, displayTitle(revision), keyFor(QStringLiteral("r"), revision.id));
        items_by_id.insert(revision.id, item);
        labelRevision(item, revision);
        visiting.remove(revision.id);
        return item;
    };
    for (const ProjectPreviewRevision &revision : project->revisions)
        append_revision(revision);
    this->revision_tree->expandAll();
}

void ProjectsWidget::rebuildGraph()
{
    const ProjectPreviewData *project = currentProject();
    QVector<ProjectGraphNode> nodes;
    if (project != nullptr)
    {
        QStringList lanes;
        for (const ProjectPreviewBranch &branch : project->branches)
            lanes.append(branch.name);
        int row = 0;
        for (const ProjectPreviewRevision &revision : project->revisions)
        {
            ProjectGraphNode node;
            node.id = revision.id;
            node.title = revision.title;
            node.branch = revision.branch;
            node.parents = revision.parents;
            node.tags = revision.tags;
            int lane_index = lanes.indexOf(revision.branch);
            if (lane_index < 0) lane_index = 0;
            node.position = QPointF(lane_index * 285.0, row * 155.0);
            for (const ProjectPreviewBranch &branch : project->branches)
                if (branch.head == revision.id) node.head_branch = branch.name;
            nodes.append(node);
            ++row;
        }
    }
    this->graph->setNodes(nodes);
    this->graph->setSelection(this->selection_kind == SelectionKind::Revision ? this->selected_id : QString());
}

void ProjectsWidget::selectTreeItem(QTreeWidget *tree, const QString &key)
{
    const QSignalBlocker blocked(tree);
    tree->clearSelection();
    if (key.isEmpty()) return;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
    {
        if ((*it)->data(0, role_key).toString() == key)
        {
            QTreeWidgetItem *ancestor = (*it)->parent();
            while (ancestor != nullptr)
            {
                ancestor->setExpanded(true);
                ancestor = ancestor->parent();
            }
            (*it)->setSelected(true);
            tree->scrollToItem(*it);
            return;
        }
    }
}

void ProjectsWidget::select(SelectionKind kind, const QString &id)
{
    if (kind == SelectionKind::Server)
    {
        bool found = false;
        for (const ProjectPreviewServer &server : this->servers)
            if (server.id == id) { found = true; break; }
        if (!found) return;
        if (this->server_id != id)
        {
            this->server_id = id;
            this->project_id.clear();
            this->branch_id.clear();
            for (const ProjectPreviewData &project : this->projects)
                if (project.server_id == id)
                {
                    this->project_id = project.id;
                    if (!project.branches.isEmpty()) this->branch_id = project.branches.first().id;
                    break;
                }
            populateProjectTree();
            populateRevisionTree();
            rebuildGraph();
            this->graph->fitGraph();
        }
    }
    else if (kind == SelectionKind::Project)
    {
        bool found = false;
        for (const ProjectPreviewData &project : this->projects)
            if (project.id == id) { found = true; break; }
        if (!found) return;
        if (id != this->project_id)
        {
            for (const ProjectPreviewData &project : this->projects)
                if (project.id == id && project.server_id != this->server_id)
                {
                    this->server_id = project.server_id;
                    populateServerTree();
                    populateProjectTree();
                    break;
                }
            this->project_id = id;
            const ProjectPreviewData *project = currentProject();
            this->branch_id = project != nullptr && !project->branches.isEmpty()
                ? project->branches.first().id : QString();
            populateRevisionTree();
            rebuildGraph();
            this->graph->fitGraph();
        }
    }
    else if (kind == SelectionKind::Branch)
    {
        bool found = false;
        for (const ProjectPreviewData &project : this->projects)
            for (const ProjectPreviewBranch &branch : project.branches)
                if (branch.id == id)
                {
                    if (this->project_id != project.id)
                    {
                        if (this->server_id != project.server_id)
                        {
                            this->server_id = project.server_id;
                            populateServerTree();
                            populateProjectTree();
                        }
                        this->project_id = project.id;
                        populateRevisionTree();
                        rebuildGraph();
                        this->graph->fitGraph();
                    }
                    this->branch_id = branch.id;
                    found = true;
                    break;
                }
        if (!found) return;
    }
    else if (kind == SelectionKind::Revision)
    {
        ProjectPreviewData *project = currentProject();
        if (project == nullptr) return;
        bool found = false;
        for (const ProjectPreviewRevision &revision : project->revisions)
            if (revision.id == id)
            {
                for (const ProjectPreviewBranch &branch : project->branches)
                    if (branch.name == revision.branch) this->branch_id = branch.id;
                found = true;
                break;
            }
        if (!found) return;
        // The graph can select historical revisions even when the sidebar is
        // filtered to heads. Reveal the full tree to keep that selection visible.
        if (!this->show_full_revision_tree && !isBranchHead(id))
        {
            this->show_full_revision_tree = true;
            this->full_tree_button->setChecked(true);
            populateRevisionTree();
        }
    }

    this->selection_kind = kind;
    this->selected_id = id;
    this->updating_ui = true;
    selectTreeItem(this->server_tree, keyFor(QStringLiteral("s"), this->server_id));
    selectTreeItem(this->project_tree,
                   kind == SelectionKind::Project ? keyFor(QStringLiteral("p"), id)
                   : kind == SelectionKind::Branch ? keyFor(QStringLiteral("b"), id) : QString());
    selectTreeItem(this->revision_tree,
                   kind == SelectionKind::Revision ? keyFor(QStringLiteral("r"), id) : QString());
    this->updating_ui = false;
    this->graph->setSelection(kind == SelectionKind::Revision ? id : QString());
    updateInspector();
    updateButtons();
    broadcastContext();
}

void ProjectsWidget::updateInspector()
{
    this->updating_ui = true;
    QString name;
    QString description;
    QStringList tags;
    QString heading = tr("Inspector");
    QString subtitle;
    QString metadata;
    bool editable = true;

    ProjectPreviewData *project = currentProject();
    if (this->selection_kind == SelectionKind::Server)
    {
        if (ProjectPreviewServer *server = currentServer())
        {
            heading = tr("Server");
            name = server->name;
            description = server->description;
            tags = server->tags;
            subtitle = tr("Configuration preview · not connected");
            metadata = tr("Profile: %1\nStatus: Not connected").arg(server->id);
        }
    }
    else if (this->selection_kind == SelectionKind::Project && project != nullptr)
    {
        heading = tr("Project");
        name = project->name;
        description = project->description;
        tags = project->tags;
        metadata = tr("Preview project ID: %1").arg(project->id);
        if (!project->source_example_resource.isEmpty())
        {
            metadata += tr("\nSource: bundled example (%1)\n"
                "Network data is NOT stored on this server; this is only a GUI preview.")
                .arg(project->source_example_resource);
        }
    }
    else if (this->selection_kind == SelectionKind::Branch)
    {
        for (ProjectPreviewData &entry : this->projects)
            for (const ProjectPreviewBranch &branch : entry.branches)
                if (branch.id == this->selected_id)
                {
                    heading = tr("Branch");
                    name = branch.name;
                    description = branch.description;
                    tags = branch.tags;
                    metadata = tr("Head revision: %1\nBranch ID: %2").arg(branch.head, branch.id);
                }
    }
    else if (this->selection_kind == SelectionKind::Revision)
    {
        if (ProjectPreviewRevision *revision = selectedRevision())
        {
            heading = tr("Revision");
            name = revision->title;
            description = revision->description;
            tags = revision->tags;
            subtitle = tr("Historical model state is immutable; these are preview annotations only.");
            metadata = tr("Revision: %1\nBranch: %2\nParents: %3\nAuthor: %4\nCreated: %5")
                .arg(revision->id, revision->branch,
                     revision->parents.isEmpty() ? tr("(root)") : revision->parents.join(QStringLiteral(", ")),
                     revision->author, revision->created_at);
        }
    }
    else
    {
        editable = false;
        subtitle = tr("Select a server, project, branch, or revision.");
    }
    const bool server_selected = this->selection_kind == SelectionKind::Server
        && currentServer() != nullptr;
    this->server_options->setVisible(server_selected);
    if (server_selected)
    {
        const ProjectPreviewServer *server = currentServer();
        this->server_endpoint_edit->setText(server->endpoint);
        const int index = this->server_auth_combo->findText(server->auth_mode);
        this->server_auth_combo->setCurrentIndex(index < 0 ? 0 : index);
    }
    this->inspector_heading->setText(heading);
    this->inspector_subtitle->setText(subtitle);
    this->inspector_subtitle->setVisible(!subtitle.isEmpty());
    this->name_edit->setText(name);
    this->tags_edit->setText(tags.join(QStringLiteral(", ")));
    this->description_edit->setPlainText(description);
    this->metadata_info->setText(metadata);
    this->name_edit->setEnabled(editable);
    this->tags_edit->setEnabled(editable);
    this->description_edit->setEnabled(editable);
    const bool deletable_server = this->selection_kind == SelectionKind::Server
        && currentServer() != nullptr;
    const bool deletable_project = this->selection_kind == SelectionKind::Project
        && currentProject() != nullptr;
    const bool deletable_branch = this->selection_kind == SelectionKind::Branch
        && currentBranch() != nullptr;
    this->inspector_delete_button->setVisible(
        deletable_server || deletable_project || deletable_branch);
    this->inspector_delete_button->setText(deletable_server ? tr("Delete server…")
        : deletable_project ? tr("Delete project…") : tr("Delete branch…"));
    this->updating_ui = false;

    // The close button hides the inspector until the next action updates its
    // contents (tree, graph, toolbar selection, creation/rename/deletion, etc.).
    // isHidden() tests the dock's own close state, rather than the visibility
    // of its parent page in the tab navigator.
    if (this->inspector_dock != nullptr && this->inspector_dock->isHidden())
        this->inspector_dock->show();
}

void ProjectsWidget::updateButtons()
{
    if (QPushButton *create_project = findChild<QPushButton *>(QStringLiteral("projectsCreateProjectButton")))
        create_project->setEnabled(currentServer() != nullptr);
    this->create_branch_button->setEnabled(currentProject() != nullptr);
    // Keep the action label short and stable so the three-button row never
    // changes width when selecting a historical revision. createBranch()
    // already uses the selected revision as the branch base when applicable.
}

void ProjectsWidget::broadcastContext()
{
    QStringList project_names;
    for (const ProjectPreviewData &project : this->projects)
        if (project.server_id == this->server_id) project_names.append(project.name);
    QString current_name;
    QStringList branch_names;
    QString branch_name;
    if (const ProjectPreviewData *project = currentProject())
    {
        current_name = project->name;
        for (const ProjectPreviewBranch &branch : project->branches)
        {
            branch_names.append(branch.name);
            if (branch.id == this->branch_id) branch_name = branch.name;
        }
    }
    emit projectContextChanged(project_names, current_name, branch_names, branch_name);
}

void ProjectsWidget::selectProjectByName(const QString &name)
{
    for (const ProjectPreviewData &project : this->projects)
        if (project.server_id == this->server_id && project.name == name)
        { select(SelectionKind::Project, project.id); return; }
}

void ProjectsWidget::selectBranchByName(const QString &name)
{
    if (const ProjectPreviewData *project = currentProject())
        for (const ProjectPreviewBranch &branch : project->branches)
            if (branch.name == name) { select(SelectionKind::Branch, branch.id); return; }
}

void ProjectsWidget::showRevisionHistory()
{
    if (this->selection_kind != SelectionKind::Revision)
    {
        if (ProjectPreviewBranch *branch = currentBranch())
        {
            if (!branch->head.isEmpty()) select(SelectionKind::Revision, branch->head);
        }
    }
    this->revision_tree->setFocus();
    // History is an explicit request to inspect a revision. Reopening works
    // even when the selected revision was already the current one.
    if (this->selection_kind == SelectionKind::Revision
        && this->inspector_dock != nullptr && this->inspector_dock->isHidden())
        this->inspector_dock->show();
}

void ProjectsWidget::createServer()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("New server (preview)"),
        tr("Server name"), QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    for (const ProjectPreviewServer &server : this->servers)
        if (server.name.compare(name, Qt::CaseInsensitive) == 0)
        {
            QMessageBox::information(this, tr("Already exists"), tr("Choose a different server name."));
            return;
        }
    ProjectPreviewServer server;
    server.id = makeId(QStringLiteral("s-"));
    server.name = name;
    server.auth_mode = QStringLiteral("None");
    this->servers.append(server);
    populateServerTree();
    select(SelectionKind::Server, server.id);
}

void ProjectsWidget::deleteServer()
{
    if (currentServer() == nullptr) return;
    if (QMessageBox::question(this, tr("Delete server preview"),
        tr("Remove this server profile and its local preview projects? No database server is contacted."))
        != QMessageBox::Yes) return;
    const QString removed = this->server_id;
    for (int i = this->projects.size() - 1; i >= 0; --i)
        if (this->projects.at(i).server_id == removed) this->projects.removeAt(i);
    for (int i = this->servers.size() - 1; i >= 0; --i)
        if (this->servers.at(i).id == removed) this->servers.removeAt(i);
    this->server_id.clear();
    this->project_id.clear();
    this->branch_id.clear();
    populateServerTree();
    this->selection_kind = SelectionKind::None;
    this->selected_id.clear();
    if (!this->servers.isEmpty()) select(SelectionKind::Server, this->servers.first().id);
    else
    {
        populateProjectTree();
        populateRevisionTree();
        rebuildGraph();
        updateInspector();
        updateButtons();
        broadcastContext();
    }
}

void ProjectsWidget::createProject()
{
    if (currentServer() == nullptr) return;
    bool accepted = false;
    QString name = QInputDialog::getText(this, tr("New project (preview)"), tr("Project name"),
                                         QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    for (const ProjectPreviewData &project : this->projects)
        if (project.server_id == this->server_id && project.name.compare(name, Qt::CaseInsensitive) == 0)
        {
            QMessageBox::information(this, tr("Already exists"), tr("Choose a different project name."));
            return;
        }
    ProjectPreviewData project;
    project.id = makeId(QStringLiteral("p-"));
    project.server_id = this->server_id;
    project.name = name;
    const QString id = project.id;
    this->projects.append(project);
    populateProjectTree();
    select(SelectionKind::Project, id);
}

void ProjectsWidget::createBranch(const QString &source_revision)
{
    ProjectPreviewData *project = currentProject();
    if (project == nullptr) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("New branch (preview)"), tr("Branch name"),
                                                QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    for (const ProjectPreviewBranch &branch : project->branches)
        if (branch.name.compare(name, Qt::CaseInsensitive) == 0)
        {
            QMessageBox::information(this, tr("Already exists"), tr("Choose a different branch name."));
            return;
        }
    ProjectPreviewBranch branch;
    branch.id = makeId(QStringLiteral("b-"));
    branch.name = name;
    branch.head = source_revision;
    if (branch.head.isEmpty() && this->selection_kind == SelectionKind::Revision)
        branch.head = this->selected_id;
    if (branch.head.isEmpty())
        if (ProjectPreviewBranch *active = currentBranch()) branch.head = active->head;
    const QString id = branch.id;
    project->branches.append(branch);
    populateProjectTree();
    populateRevisionTree();
    rebuildGraph();
    select(SelectionKind::Branch, id);
}

void ProjectsWidget::deleteSelection()
{
    const SelectionKind kind = this->selection_kind;
    const QString id = this->selected_id;
    if (kind != SelectionKind::Project && kind != SelectionKind::Branch) return;
    if (QMessageBox::question(this, tr("Delete from preview"),
        tr("Remove this %1 from the in-memory UI preview? No database data is touched.")
            .arg(kind == SelectionKind::Project ? tr("project") : tr("branch"))) != QMessageBox::Yes)
        return;
    if (kind == SelectionKind::Project)
    {
        for (int i = 0; i < this->projects.size(); ++i)
            if (this->projects.at(i).id == id) { this->projects.removeAt(i); break; }
        this->project_id.clear();
        for (const ProjectPreviewData &candidate : this->projects)
            if (candidate.server_id == this->server_id)
            { this->project_id = candidate.id; break; }
        const ProjectPreviewData *project = currentProject();
        this->branch_id = project != nullptr && !project->branches.isEmpty()
            ? project->branches.first().id : QString();
    }
    else if (ProjectPreviewData *project = currentProject())
    {
        for (int i = 0; i < project->branches.size(); ++i)
            if (project->branches.at(i).id == id) { project->branches.removeAt(i); break; }
        this->branch_id = project->branches.isEmpty() ? QString() : project->branches.first().id;
    }
    this->selection_kind = SelectionKind::None;
    this->selected_id.clear();
    populateProjectTree();
    populateRevisionTree();
    rebuildGraph();
    if (kind == SelectionKind::Project && !this->project_id.isEmpty())
        select(SelectionKind::Project, this->project_id);
    else if (!this->branch_id.isEmpty())
        select(SelectionKind::Branch, this->branch_id);
    else
    {
        updateInspector();
        updateButtons();
        broadcastContext();
    }
}

void ProjectsWidget::browseExamples()
{
    if (!guiConfiguration().examples_builtin_enable) return;

    QDialog browser(this);
    browser.setObjectName(QStringLiteral("projectsExampleLibraryDialog"));
    browser.setWindowTitle(tr("Example Library"));
    browser.resize(720, 435);
    QVBoxLayout *layout = new QVBoxLayout(&browser);
    QLabel *heading = new QLabel(tr("Built-in network templates"), &browser);
    QFont heading_font = heading->font();
    heading_font.setBold(true);
    heading_font.setPointSizeF(heading_font.pointSizeF() + 2);
    heading->setFont(heading_font);
    layout->addWidget(heading);
    QLabel *intro = new QLabel(tr("Open a network for experimentation, or prepare a new project "
                                "on any configured server."), &browser);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    QHBoxLayout *content = new QHBoxLayout();
    QVBoxLayout *browser_left = new QVBoxLayout();
    QLineEdit *filter = new QLineEdit(&browser);
    filter->setObjectName(QStringLiteral("exampleLibraryFilter"));
    filter->setPlaceholderText(tr("Find a network…"));
    browser_left->addWidget(filter);
    QListWidget *list = new QListWidget(&browser);
    list->setObjectName(QStringLiteral("exampleLibraryList"));
    for (const BuiltinExampleRevision &example : builtinExampleRevisions())
    {
        QListWidgetItem *item = new QListWidgetItem(example.display_name, list);
        item->setData(Qt::UserRole, example.resource_path);
        item->setToolTip(example.description);
    }
    browser_left->addWidget(list, 1);
    content->addLayout(browser_left, 2);
    QVBoxLayout *browser_right = new QVBoxLayout();
    QLabel *example_name = new QLabel(&browser);
    example_name->setObjectName(QStringLiteral("exampleLibraryName"));
    QFont example_font = example_name->font();
    example_font.setBold(true);
    example_name->setFont(example_font);
    browser_right->addWidget(example_name);
    QLabel *description = new QLabel(&browser);
    description->setObjectName(QStringLiteral("exampleLibraryDescription"));
    description->setWordWrap(true);
    description->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    browser_right->addWidget(description);
    QLabel *origin = new QLabel(tr("Source: Bundled EPANET model (read-only template)"), &browser);
    origin->setWordWrap(true);
    browser_right->addWidget(origin);
    browser_right->addStretch();
    content->addLayout(browser_right, 3);
    layout->addLayout(content, 1);

    QLabel *disclaimer = new QLabel(tr("Creating a project is a GUI preview only: "
                                     "no database is contacted and no network is stored."), &browser);
    disclaimer->setObjectName(QStringLiteral("exampleLibraryPreviewNotice"));
    disclaimer->setWordWrap(true);
    layout->addWidget(disclaimer);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    QPushButton *close_button = compactButton(tr("Close"), QString(), &browser);
    QPushButton *open_button = compactButton(tr("Open in editor"),
        tr("Load this EPANET example into the current working model"), &browser);
    open_button->setObjectName(QStringLiteral("exampleLibraryOpenButton"));
    QPushButton *create_button = compactButton(tr("Create project from example…"),
        tr("Choose a target server and prepare a project preview"), &browser);
    create_button->setObjectName(QStringLiteral("exampleLibraryCreateButton"));
    buttons->addWidget(close_button);
    buttons->addWidget(open_button);
    buttons->addWidget(create_button);
    layout->addLayout(buttons);

    const auto selectedExample = [list]() -> const BuiltinExampleRevision *
    {
        const QListWidgetItem *item = list->currentItem();
        if (item == nullptr) return nullptr;
        const QString path = item->data(Qt::UserRole).toString();
        for (const BuiltinExampleRevision &example : builtinExampleRevisions())
            if (example.resource_path == path) return &example;
        return nullptr;
    };
    const auto updateDetail = [selectedExample, example_name, description, open_button, create_button]()
    {
        const BuiltinExampleRevision *example = selectedExample();
        example_name->setText(example == nullptr ? QString() : example->display_name);
        description->setText(example == nullptr ? QString() : example->description);
        open_button->setEnabled(example != nullptr);
        create_button->setEnabled(example != nullptr);
    };
    connect(list, &QListWidget::currentItemChanged, &browser,
            [updateDetail](QListWidgetItem *, QListWidgetItem *) { updateDetail(); });
    connect(filter, &QLineEdit::textChanged, &browser, [list](const QString &text)
    {
        for (int row = 0; row < list->count(); ++row)
            list->item(row)->setHidden(!list->item(row)->text().contains(text, Qt::CaseInsensitive));
        if (list->currentItem() == nullptr || list->currentItem()->isHidden())
        {
            for (int row = 0; row < list->count(); ++row)
                if (!list->item(row)->isHidden()) { list->setCurrentRow(row); return; }
            list->setCurrentRow(-1);
        }
    });
    connect(close_button, &QPushButton::clicked, &browser, &QDialog::reject);
    connect(open_button, &QPushButton::clicked, &browser, [this, &browser, selectedExample]()
    {
        const BuiltinExampleRevision *example = selectedExample();
        if (example == nullptr) return;
        const QString path = example->resource_path;
        const QString file_name = example->file_name;
        browser.accept();
        emit builtinExampleRequested(path, file_name);
    });
    connect(list, &QListWidget::itemDoubleClicked, open_button,
            [open_button](QListWidgetItem *) { open_button->click(); });
    connect(create_button, &QPushButton::clicked, &browser, [this, &browser, selectedExample]()
    {
        const BuiltinExampleRevision *example = selectedExample();
        if (example == nullptr) return;
        const QString path = example->resource_path;
        browser.accept();
        for (const BuiltinExampleRevision &entry : builtinExampleRevisions())
            if (entry.resource_path == path) { createProjectFromExample(entry); break; }
    });
    list->setCurrentRow(0);
    updateDetail();
    browser.exec();
}

void ProjectsWidget::createProjectFromExample(const BuiltinExampleRevision &example)
{
    if (this->servers.isEmpty())
    {
        QMessageBox::information(this, tr("No servers configured"),
            tr("Add a server profile before preparing a project from an example."));
        return;
    }

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("exampleCreateProjectDialog"));
    dialog.setWindowTitle(tr("Create project from %1 (preview)").arg(example.display_name));
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QFormLayout *fields = new QFormLayout();
    fields->setRowWrapPolicy(QFormLayout::WrapAllRows);
    QComboBox *server = new QComboBox(&dialog);
    server->setObjectName(QStringLiteral("exampleCreateServer"));
    for (const ProjectPreviewServer &entry : this->servers)
        server->addItem(entry.name, entry.id);
    const int selected_server = server->findData(this->server_id);
    if (selected_server >= 0) server->setCurrentIndex(selected_server);
    QLineEdit *project_name = new QLineEdit(example.display_name, &dialog);
    project_name->setObjectName(QStringLiteral("exampleCreateProjectName"));
    QLineEdit *branch_name = new QLineEdit(QStringLiteral("main"), &dialog);
    branch_name->setObjectName(QStringLiteral("exampleCreateBranchName"));
    QLineEdit *tags = new QLineEdit(QStringLiteral("example"), &dialog);
    tags->setObjectName(QStringLiteral("exampleCreateTags"));
    QLineEdit *description = new QLineEdit(tr("Based on %1").arg(example.display_name), &dialog);
    description->setObjectName(QStringLiteral("exampleCreateDescription"));
    fields->addRow(tr("Destination server"), server);
    fields->addRow(tr("Project name"), project_name);
    fields->addRow(tr("Initial branch"), branch_name);
    fields->addRow(tr("Tags"), tags);
    fields->addRow(tr("Description"), description);
    layout->addLayout(fields);
    QLabel *notice = new QLabel(tr("GUI PREVIEW ONLY — the server is not contacted. "
                                  "This creates a project/branch/revision placeholder; "
                                  "the hydraulic network is not copied or saved."), &dialog);
    notice->setObjectName(QStringLiteral("exampleCreatePreviewNotice"));
    notice->setWordWrap(true);
    layout->addWidget(notice);
    QDialogButtonBox *actions = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton *create = actions->addButton(tr("Create preview project"), QDialogButtonBox::AcceptRole);
    create->setObjectName(QStringLiteral("exampleCreateConfirmButton"));
    layout->addWidget(actions);
    connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const auto validate = [this, project_name, branch_name, server, create]()
    {
        const QString name = project_name->text().trimmed();
        const QString branch = branch_name->text().trimmed();
        bool duplicate = false;
        for (const ProjectPreviewData &entry : this->projects)
            if (entry.server_id == server->currentData().toString()
                && entry.name.compare(name, Qt::CaseInsensitive) == 0) duplicate = true;
        create->setEnabled(!name.isEmpty() && !branch.isEmpty() && !duplicate);
        create->setToolTip(duplicate ? tr("A project with this name already exists on the selected server") : QString());
    };
    connect(project_name, &QLineEdit::textChanged, &dialog, [validate](const QString &) { validate(); });
    connect(branch_name, &QLineEdit::textChanged, &dialog, [validate](const QString &) { validate(); });
    connect(server, QOverload<int>::of(&QComboBox::currentIndexChanged),
            &dialog, [validate](int) { validate(); });
    connect(create, &QPushButton::clicked, &dialog, &QDialog::accept);
    validate();
    if (dialog.exec() != QDialog::Accepted || !create->isEnabled()) return;

    ProjectPreviewData project;
    project.id = makeId(QStringLiteral("p-"));
    project.server_id = server->currentData().toString();
    project.name = project_name->text().trimmed();
    project.description = description->text().trimmed();
    project.tags = parseTags(tags->text());
    project.source_example_resource = example.resource_path;
    ProjectPreviewBranch branch;
    branch.id = makeId(QStringLiteral("b-"));
    branch.name = branch_name->text().trimmed();
    branch.description = tr("Preview branch for %1").arg(example.display_name);
    ProjectPreviewRevision revision;
    revision.id = makeId(QStringLiteral("r-"));
    revision.title = tr("Initial import (preview)");
    revision.description = tr("Planned import from %1. No network data is stored.")
        .arg(example.display_name);
    revision.branch = branch.name;
    revision.tags = {QStringLiteral("preview"), QStringLiteral("example")};
    branch.head = revision.id;
    project.branches.append(branch);
    project.revisions.append(revision);
    const QString id = project.id;
    this->projects.append(project);
    populateProjectTree();
    select(SelectionKind::Project, id);
}

void ProjectsWidget::applyInspectorEdits()
{
    if (this->updating_ui) return;
    const QString name = this->name_edit->text().trimmed();
    const QString description = this->description_edit->toPlainText();
    const QStringList tags = parseTags(this->tags_edit->text());
    bool tree_changed = false;
    if (this->selection_kind == SelectionKind::Server)
    {
        ProjectPreviewServer *server = currentServer();
        if (server == nullptr) return;
        if (!name.isEmpty() && name != server->name)
        {
            bool exists = false;
            for (const ProjectPreviewServer &other : this->servers)
                if (other.id != server->id && other.name.compare(name, Qt::CaseInsensitive) == 0)
                    exists = true;
            if (!exists) { server->name = name; tree_changed = true; }
        }
        server->endpoint = this->server_endpoint_edit->text().trimmed();
        server->auth_mode = this->server_auth_combo->currentText();
        server->description = description;
        server->tags = tags;
        // Keep the server tooltip and list selection in sync after changing
        // even just the endpoint or authentication preview.
        populateServerTree();
        selectTreeItem(this->server_tree, keyFor(QStringLiteral("s"), this->server_id));
    }
    else if (this->selection_kind == SelectionKind::Project)
    {
        ProjectPreviewData *project = currentProject();
        if (project == nullptr) return;
        if (!name.isEmpty() && name != project->name)
        {
            bool exists = false;
            for (const ProjectPreviewData &other : this->projects)
                if (other.id != project->id && other.server_id == this->server_id
                    && other.name.compare(name, Qt::CaseInsensitive) == 0) exists = true;
            if (!exists) { project->name = name; tree_changed = true; }
        }
        project->description = description;
        project->tags = tags;
    }
    else if (this->selection_kind == SelectionKind::Branch)
    {
        ProjectPreviewData *project = currentProject();
        if (project == nullptr) return;
        for (ProjectPreviewBranch &branch : project->branches)
            if (branch.id == this->selected_id)
            {
                if (!name.isEmpty() && branch.name != name)
                {
                    bool exists = false;
                    for (const ProjectPreviewBranch &other : project->branches)
                        if (other.id != branch.id && other.name.compare(name, Qt::CaseInsensitive) == 0) exists = true;
                    if (!exists)
                    {
                        const QString previous = branch.name;
                        branch.name = name;
                        for (ProjectPreviewRevision &revision : project->revisions)
                            if (revision.branch == previous) revision.branch = name;
                        tree_changed = true;
                    }
                }
                branch.description = description;
                branch.tags = tags;
                break;
            }
    }
    else if (this->selection_kind == SelectionKind::Revision)
    {
        if (ProjectPreviewRevision *revision = selectedRevision())
        {
            if (!name.isEmpty() && revision->title != name) { revision->title = name; tree_changed = true; }
            revision->description = description;
            revision->tags = tags;
        }
    }
    else return;
    if (tree_changed)
    {
        populateProjectTree();
        populateRevisionTree();
        if (this->selection_kind != SelectionKind::Server) rebuildGraph();
        select(this->selection_kind, this->selected_id);
    }
}
