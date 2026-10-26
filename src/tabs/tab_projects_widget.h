#pragma once

#include "tabs/project_revision_graph.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class GroupBoxCollapsible;
struct BuiltinExampleRevision;
class QDockWidget;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;
class QTreeWidget;
class QTreeWidgetItem;

struct ProjectPreviewRevision
{
    QString id;
    QString title;
    QString description;
    QString branch;
    QStringList parents;
    QStringList tags;
    QString author;
    QString created_at;
};

struct ProjectPreviewBranch
{
    QString id;
    QString name;
    QString description;
    QStringList tags;
    QString head;
};

struct ProjectPreviewServer
{
    QString id;
    QString name;
    QString endpoint;
    QString description;
    QStringList tags;
    QString auth_mode;
};

struct ProjectPreviewData
{
    QString id;
    QString server_id;
    QString name;
    QString description;
    QString source_example_resource; // template origin, preview only: network not persisted
    QStringList tags;
    QList<ProjectPreviewBranch> branches;
    QList<ProjectPreviewRevision> revisions;
};

// Projects tab prototype: all project/branch/metadata mutations below are
// in-memory GUI state only. It does not issue requests to the DB server.
class ProjectsWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit ProjectsWidget(QWidget *parent = nullptr);

public slots:
    void selectProjectByName(const QString &name);
    void selectBranchByName(const QString &name);
    void showRevisionHistory();

signals:
    void projectContextChanged(const QStringList &projects,
                               const QString &project,
                               const QStringList &branches,
                               const QString &branch);
    void builtinExampleRequested(const QString &resource_path, const QString &file_name);
    void importProjectRequested();

private:
    enum class SelectionKind { None, Server, Project, Branch, Revision };
    void seedPreview();
    void populateServerTree();
    void populateProjectTree();
    void populateRevisionTree();
    bool isBranchHead(const QString &revision_id) const;
    void rebuildGraph();
    void updateInspector();
    void updateButtons();
    void broadcastContext();
    void select(SelectionKind kind, const QString &id);
    void selectTreeItem(QTreeWidget *tree, const QString &key);
    void createServer();
    void deleteServer();
    void createProject();
    void createBranch(const QString &source_revision = QString());
    void deleteSelection();
    void browseExamples();
    void createProjectFromExample(const BuiltinExampleRevision &example);
    void applyInspectorEdits();
    ProjectPreviewServer *currentServer();
    ProjectPreviewData *currentProject();
    const ProjectPreviewData *currentProject() const;
    ProjectPreviewBranch *currentBranch();
    ProjectPreviewRevision *selectedRevision();
    static QString makeId(const QString &prefix);
    static QStringList parseTags(const QString &text);

    QList<ProjectPreviewServer> servers;
    QString server_id;
    QList<ProjectPreviewData> projects;
    QString project_id;
    QString branch_id;
    SelectionKind selection_kind = SelectionKind::None;
    QString selected_id;
    QTreeWidget *server_tree = nullptr;
    QLineEdit *server_endpoint_edit = nullptr;
    QComboBox *server_auth_combo = nullptr;
    QWidget *server_options = nullptr;
    QTreeWidget *project_tree = nullptr;
    QTreeWidget *revision_tree = nullptr;
    QPushButton *full_tree_button = nullptr;
    bool show_full_revision_tree = true;
    ProjectRevisionGraph *graph = nullptr;
    QDockWidget *inspector_dock = nullptr;
    QLabel *inspector_heading = nullptr;
    QLabel *inspector_subtitle = nullptr;
    QLabel *metadata_info = nullptr;
    QLineEdit *name_edit = nullptr;
    QLineEdit *tags_edit = nullptr;
    QTextEdit *description_edit = nullptr;
    QPushButton *inspector_delete_button = nullptr;
    QPushButton *create_branch_button = nullptr;
    bool updating_ui = false;
};
