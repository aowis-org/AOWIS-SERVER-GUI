#pragma once

#include "config/project_server_profile_storage.h"
#include "tabs/project_revision_graph.h"
#include "projects/project_source_reader.h"

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
class QStackedWidget;
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

struct ProjectPreviewData
{
    QString id;
    QString server_id;
    QString name;
    QString description;
    QString source_example_resource; // template origin, preview only: network not persisted
    bool database_backed = false;
    QStringList tags;
    QList<ProjectPreviewBranch> branches;
    QList<ProjectPreviewRevision> revisions;
};

// Data-source profiles are locally persisted; real DVMDB listings are read-only.
// The legacy example fixture is explicitly isolated from connected data.
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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class SelectionKind { None, Server, Project, Branch, Revision, NewSource };
    void seedPreview();
    void populateServerTree();
    void populateProjectTree();
    void populateRevisionTree();
    bool isBranchHead(const QString &revision_id) const;
    void rebuildGraph();
    void updateInspector();
    void updateEmptyState();
    void updateSourceFields();
    void updateButtons();
    void connectSource();
    void disconnectSource();
    void applySourceSnapshot(const SourceSnapshot &snapshot);
    void updateConnectionStatus();
    bool isPreviewSource() const;
    void broadcastContext();
    void select(SelectionKind kind, const QString &id);
    void selectTreeItem(QTreeWidget *tree, const QString &key);
    void createServer();
    void saveNewSource();
    void cancelNewSource();
    void deleteServer();
    void createProject();
    void createBranch(const QString &source_revision = QString());
    void deleteSelection();
    void browseExamples();
    void createProjectFromExample(const BuiltinExampleRevision &example);
    void applyInspectorEdits();
    void reportServerSaveFailure();
    ProjectServerProfile *currentServer();
    ProjectPreviewData *currentProject();
    const ProjectPreviewData *currentProject() const;
    ProjectPreviewBranch *currentBranch();
    ProjectPreviewRevision *selectedRevision();
    static QString makeId(const QString &prefix);
    static QStringList parseTags(const QString &text);

    QList<ProjectServerProfile> servers;
    QString server_id;
    QList<ProjectPreviewData> projects;
    QString project_id;
    QString branch_id;
    SelectionKind selection_kind = SelectionKind::None;
    QString selected_id;
    QTreeWidget *server_tree = nullptr;
    QLineEdit *server_endpoint_edit = nullptr;
    QComboBox *server_auth_combo = nullptr;
    QComboBox *source_kind_combo = nullptr;
    QLineEdit *local_path_edit = nullptr;
    QWidget *server_options = nullptr;
    QWidget *remote_options = nullptr;
    QWidget *local_options = nullptr;
    QTreeWidget *project_tree = nullptr;
    QTreeWidget *revision_tree = nullptr;
    QPushButton *full_tree_button = nullptr;
    bool show_full_revision_tree = true;
    ProjectRevisionGraph *graph = nullptr;
    QStackedWidget *graph_stack = nullptr;
    QDockWidget *inspector_dock = nullptr;
    QLabel *inspector_heading = nullptr;
    QLabel *inspector_subtitle = nullptr;
    QLabel *metadata_info = nullptr;
    QLineEdit *name_edit = nullptr;
    QLineEdit *tags_edit = nullptr;
    QTextEdit *description_edit = nullptr;
    QPushButton *inspector_delete_button = nullptr;
    QPushButton *source_save_button = nullptr;
    QPushButton *source_cancel_button = nullptr;
    QPushButton *create_branch_button = nullptr;
    ProjectSourceReader *source_reader = nullptr;
    QPushButton *source_connect_button = nullptr;
    QLabel *source_status = nullptr;
    QLabel *no_projects_description = nullptr;
    QLabel *no_projects_title = nullptr;
    QPushButton *source_refresh_button = nullptr;
    QLabel *preview_banner = nullptr;
    QLabel *inspector_preview_note = nullptr;
    QString connected_source_id;
    QString source_error;
    enum class SourceState { Disconnected, Loading, Connected, Error };
    SourceState source_state = SourceState::Disconnected;
    bool updating_ui = false;
};
