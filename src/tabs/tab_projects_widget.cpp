#include "tabs/tab_projects_widget.h"

#include "app/builtin_examples.h"
#include "config/gui_configuration.h"
#include "widgets/group_box_collapsible.h"

#include <QAbstractItemView>
#include <QDockWidget>
#include <QMainWindow>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QUuid>
#include <QVBoxLayout>

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
    left_scroll->setFrameShape(QFrame::NoFrame);
    left_scroll->setMinimumWidth(240);
    QWidget *left = new QWidget(left_scroll);
    QVBoxLayout *left_layout = new QVBoxLayout(left);
    left_layout->setContentsMargins(8, 8, 8, 8);
    left_layout->setSpacing(10);

    // QGroupBox interprets a single & as a mnemonic marker. Use && for a visible ampersand.
    GroupBoxCollapsible *projects_group = new GroupBoxCollapsible(tr("Projects && Branches"), left);
    projects_group->setObjectName(QStringLiteral("projectsCollapsibleGroup"));
    QVBoxLayout *projects_layout = new QVBoxLayout(projects_group);
    QHBoxLayout *project_actions = new QHBoxLayout();
    QPushButton *create_project = compactButton(tr("+ Project"), tr("Add a project to this GUI preview"), projects_group);
    this->rename_button = compactButton(tr("Rename"), tr("Rename selected project or branch in preview"), projects_group);
    this->delete_button = compactButton(tr("Delete"), tr("Remove selected project or branch from preview"), projects_group);
    project_actions->addWidget(create_project);
    project_actions->addWidget(this->rename_button);
    project_actions->addWidget(this->delete_button);
    projects_layout->addLayout(project_actions);
    this->project_tree = new QTreeWidget(projects_group);
    this->project_tree->setObjectName(QStringLiteral("projectsTree"));
    this->project_tree->setHeaderHidden(true);
    this->project_tree->setIndentation(18);
    this->project_tree->setMinimumHeight(190);
    this->project_tree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    projects_layout->addWidget(this->project_tree, 1);
    this->create_branch_button = compactButton(tr("+ Branch"), tr("Create a branch from the current head or selected revision"), projects_group);
    projects_layout->addWidget(this->create_branch_button);
    QHBoxLayout *example_actions = new QHBoxLayout();
    QPushButton *import_button = compactButton(tr("Import EPANET…"), tr("Use the existing network import workflow"), projects_group);
    this->load_example_button = compactButton(tr("Load example"), tr("Load the selected built-in example with the existing importer"), projects_group);
    example_actions->addWidget(import_button);
    example_actions->addWidget(this->load_example_button);
    projects_layout->addLayout(example_actions);
    QPushButton *show_on_map = compactButton(tr("Show network on map"),
        tr("Fit the currently loaded hydraulic network in Maps"), projects_group);
    projects_layout->addWidget(show_on_map);
    left_layout->addWidget(projects_group, 1);

    GroupBoxCollapsible *revisions_group = new GroupBoxCollapsible(tr("Revisions"), left);
    revisions_group->setObjectName(QStringLiteral("revisionsCollapsibleGroup"));
    QVBoxLayout *revisions_layout = new QVBoxLayout(revisions_group);
    QLabel *history_caption = new QLabel(tr("Select a revision to inspect it. Earlier revisions remain read-only."), revisions_group);
    history_caption->setWordWrap(true);
    revisions_layout->addWidget(history_caption);
    this->revision_tree = new QTreeWidget(revisions_group);
    this->revision_tree->setObjectName(QStringLiteral("revisionsTree"));
    this->revision_tree->setHeaderHidden(true);
    this->revision_tree->setMinimumHeight(220);
    this->revision_tree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    revisions_layout->addWidget(this->revision_tree, 1);
    left_layout->addWidget(revisions_group, 1);
    left_layout->addStretch(0);

    // Give the trees any spare sidebar height without reducing their existing
    // minimum heights. When a section is collapsed, its header should remain
    // compact and the other expanded tree should use the freed space.
    const auto update_section_stretches = [left_layout, projects_group, revisions_group]()
    {
        const bool projects_expanded = !projects_group->isCollapsed();
        const bool revisions_expanded = !revisions_group->isCollapsed();
        projects_group->setSizePolicy(QSizePolicy::Preferred,
            projects_expanded ? QSizePolicy::Expanding : QSizePolicy::Maximum);
        revisions_group->setSizePolicy(QSizePolicy::Preferred,
            revisions_expanded ? QSizePolicy::Expanding : QSizePolicy::Maximum);
        left_layout->setStretch(left_layout->indexOf(projects_group), projects_expanded ? 1 : 0);
        left_layout->setStretch(left_layout->indexOf(revisions_group), revisions_expanded ? 1 : 0);
        left_layout->setStretch(left_layout->count() - 1,
            (!projects_expanded && !revisions_expanded) ? 1 : 0);
        left_layout->invalidate();
    };
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
    this->metadata_info = new QLabel(right);
    this->metadata_info->setWordWrap(true);
    this->metadata_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    right_layout->addWidget(this->metadata_info);
    QLabel *preview_note = new QLabel(tr("Changes here update only this in-memory preview. No server records are written."), right);
    preview_note->setWordWrap(true);
    preview_note->setObjectName(QStringLiteral("inspectorPreviewNote"));
    right_layout->addWidget(preview_note);
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

    connect(create_project, &QPushButton::clicked, this, &ProjectsWidget::createProject);
    connect(this->rename_button, &QPushButton::clicked, this, &ProjectsWidget::renameSelection);
    connect(this->delete_button, &QPushButton::clicked, this, &ProjectsWidget::deleteSelection);
    connect(this->create_branch_button, &QPushButton::clicked, this, [this] { createBranch(); });
    connect(import_button, &QPushButton::clicked, this, &ProjectsWidget::importProjectRequested);
    connect(this->load_example_button, &QPushButton::clicked, this, &ProjectsWidget::loadSelectedExample);
    connect(show_on_map, &QPushButton::clicked,
            this, &ProjectsWidget::showNetworkOnMapRequested);
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
        else if (key.startsWith(QStringLiteral("e:"))) select(SelectionKind::Example, key.mid(2));
    });
    connect(this->revision_tree, &QTreeWidget::itemSelectionChanged, this, [this]
    {
        const QList<QTreeWidgetItem *> items = this->revision_tree->selectedItems();
        if (items.isEmpty() || this->updating_ui) return;
        const QString key = items.first()->data(0, role_key).toString();
        if (key.startsWith(QStringLiteral("r:"))) select(SelectionKind::Revision, key.mid(2));
    });
    connect(this->project_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int)
    {
        if (item->data(0, role_key).toString().startsWith(QStringLiteral("e:")))
            loadSelectedExample();
    });
    connect(this->graph, &ProjectRevisionGraph::revisionActivated, this, [this](const QString &id)
    {
        select(SelectionKind::Revision, id);
    });
    connect(this->graph, &ProjectRevisionGraph::branchActivated, this, [this](const QString &name)
    {
        selectBranchByName(name);
    });
    connect(this->name_edit, &QLineEdit::editingFinished, this, &ProjectsWidget::applyInspectorEdits);
    connect(this->tags_edit, &QLineEdit::editingFinished, this, &ProjectsWidget::applyInspectorEdits);
    connect(this->description_edit, &QTextEdit::textChanged, this, &ProjectsWidget::applyInspectorEdits);

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
    this->projects.append(north);
    ProjectPreviewData south;
    south.id = QStringLiteral("preview-south");
    south.name = tr("Sample Pump Station");
    south.description = tr("Another fictional project in this GUI preview.");
    south.tags = {QStringLiteral("preview")};
    south.branches = {{QStringLiteral("south-main"), QStringLiteral("main"), QString(), {}, QStringLiteral("742b0a90")}};
    south.revisions = {{QStringLiteral("742b0a90"), tr("Initial layout"), tr("Demo revision."), QStringLiteral("main"), {}, {}, tr("Preview author"), QStringLiteral("2026-10-23")}};
    this->projects.append(south);
    this->project_id = north.id;
    this->branch_id = north.branches.first().id;
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
        QTreeWidgetItem *parent = newTreeItem(this->project_tree,
                                               QStringLiteral("▣  ") + project.name,
                                               keyFor(QStringLiteral("p"), project.id));
        parent->setExpanded(project.id == this->project_id);
        for (const ProjectPreviewBranch &branch : project.branches)
            newTreeChild(parent, QStringLiteral("⑂  ") + branch.name,
                         keyFor(QStringLiteral("b"), branch.id));
    }
    if (guiConfiguration().examples_builtin_enable)
    {
        QTreeWidgetItem *examples = newTreeItem(this->project_tree,
                                                tr("▤  Built-in examples"), QStringLiteral("examples-root"));
        examples->setExpanded(true);
        for (const BuiltinExampleRevision &example : builtinExampleRevisions())
            newTreeChild(examples, example.display_name,
                         keyFor(QStringLiteral("e"), example.resource_path));
    }
}

void ProjectsWidget::populateRevisionTree()
{
    const QSignalBlocker blocked(this->revision_tree);
    this->revision_tree->clear();
    const ProjectPreviewData *project = currentProject();
    if (project == nullptr) return;
    for (const ProjectPreviewRevision &revision : project->revisions)
    {
        QTreeWidgetItem *item = newTreeItem(this->revision_tree,
                                            revision.title + QStringLiteral("  ·  ") + revision.id.left(8),
                                            keyFor(QStringLiteral("r"), revision.id));
        item->setToolTip(0, revision.branch + QStringLiteral("  /  ") + revision.description);
    }
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
    if (kind == SelectionKind::Project)
    {
        bool found = false;
        for (const ProjectPreviewData &project : this->projects)
            if (project.id == id) { found = true; break; }
        if (!found) return;
        if (id != this->project_id)
        {
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
    }
    else if (kind == SelectionKind::Example && !guiConfiguration().examples_builtin_enable)
        return;

    this->selection_kind = kind;
    this->selected_id = id;
    this->updating_ui = true;
    selectTreeItem(this->project_tree,
                   kind == SelectionKind::Project ? keyFor(QStringLiteral("p"), id)
                   : kind == SelectionKind::Branch ? keyFor(QStringLiteral("b"), id)
                   : kind == SelectionKind::Example ? keyFor(QStringLiteral("e"), id) : QString());
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
    if (this->selection_kind == SelectionKind::Project && project != nullptr)
    {
        heading = tr("Project");
        name = project->name;
        description = project->description;
        tags = project->tags;
        metadata = tr("Preview project ID: %1").arg(project->id);
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
    else if (this->selection_kind == SelectionKind::Example)
    {
        heading = tr("Built-in example");
        editable = false;
        for (const BuiltinExampleRevision &example : builtinExampleRevisions())
            if (example.resource_path == this->selected_id)
            {
                name = example.display_name;
                description = tr("Bundled example model. Double-click it or use Load example to import it into the editor.");
                metadata = tr("Resource: %1").arg(example.resource_path);
            }
    }
    else
    {
        editable = false;
        subtitle = tr("Select a project, branch, revision, or built-in example.");
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
    const bool project_selected = this->selection_kind == SelectionKind::Project;
    const bool branch_selected = this->selection_kind == SelectionKind::Branch;
    this->rename_button->setEnabled(project_selected || branch_selected);
    this->delete_button->setEnabled(project_selected || branch_selected);
    this->create_branch_button->setEnabled(currentProject() != nullptr);
    this->create_branch_button->setText(this->selection_kind == SelectionKind::Revision
        ? tr("+ Branch from revision") : tr("+ Branch"));
    this->load_example_button->setEnabled(this->selection_kind == SelectionKind::Example);
}

void ProjectsWidget::broadcastContext()
{
    QStringList project_names;
    for (const ProjectPreviewData &project : this->projects)
        project_names.append(project.name);
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
        if (project.name == name) { select(SelectionKind::Project, project.id); return; }
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

void ProjectsWidget::createProject()
{
    bool accepted = false;
    QString name = QInputDialog::getText(this, tr("New project (preview)"), tr("Project name"),
                                         QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    for (const ProjectPreviewData &project : this->projects)
        if (project.name.compare(name, Qt::CaseInsensitive) == 0)
        {
            QMessageBox::information(this, tr("Already exists"), tr("Choose a different project name."));
            return;
        }
    ProjectPreviewData project;
    project.id = makeId(QStringLiteral("p-"));
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
    rebuildGraph();
    select(SelectionKind::Branch, id);
}

void ProjectsWidget::renameSelection()
{
    if (this->selection_kind != SelectionKind::Project && this->selection_kind != SelectionKind::Branch)
        return;
    const QString existing = this->name_edit->text();
    bool accepted = false;
    const QString requested = QInputDialog::getText(this, tr("Rename (preview)"),
        tr("New name"), QLineEdit::Normal, existing, &accepted).trimmed();
    if (!accepted || requested.isEmpty() || requested == existing) return;
    if (this->selection_kind == SelectionKind::Project)
    {
        for (const ProjectPreviewData &other : this->projects)
            if (other.id != this->project_id
                && other.name.compare(requested, Qt::CaseInsensitive) == 0)
            {
                QMessageBox::information(this, tr("Already exists"), tr("Choose a different project name."));
                return;
            }
    }
    else if (ProjectPreviewData *project = currentProject())
    {
        for (const ProjectPreviewBranch &other : project->branches)
            if (other.id != this->selected_id
                && other.name.compare(requested, Qt::CaseInsensitive) == 0)
            {
                QMessageBox::information(this, tr("Already exists"), tr("Choose a different branch name."));
                return;
            }
    }
    this->name_edit->setText(requested);
    applyInspectorEdits();
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
        this->project_id = this->projects.isEmpty() ? QString() : this->projects.first().id;
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

void ProjectsWidget::loadSelectedExample()
{
    if (this->selection_kind != SelectionKind::Example) return;
    for (const BuiltinExampleRevision &example : builtinExampleRevisions())
        if (example.resource_path == this->selected_id)
        {
            emit builtinExampleRequested(example.resource_path, example.file_name);
            return;
        }
}

void ProjectsWidget::applyInspectorEdits()
{
    if (this->updating_ui) return;
    const QString name = this->name_edit->text().trimmed();
    const QString description = this->description_edit->toPlainText();
    const QStringList tags = parseTags(this->tags_edit->text());
    bool tree_changed = false;
    if (this->selection_kind == SelectionKind::Project)
    {
        ProjectPreviewData *project = currentProject();
        if (project == nullptr) return;
        if (!name.isEmpty() && name != project->name)
        {
            bool exists = false;
            for (const ProjectPreviewData &other : this->projects)
                if (other.id != project->id && other.name.compare(name, Qt::CaseInsensitive) == 0) exists = true;
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
        rebuildGraph();
        select(this->selection_kind, this->selected_id);
    }
}
