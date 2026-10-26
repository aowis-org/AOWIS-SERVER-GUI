#pragma once

#include "config/project_server_profile_storage.h"

#include <QList>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

class QNetworkReply;

// Read-only shared result model. Both backends expose the same immutable IDs
// and parent hashes; first-parent history does not represent the whole DAG.
struct SourceRevision
{
    QString id;
    QStringList parents;
};

struct SourceBranch
{
    QString name;
    QString head;
    QList<SourceRevision> history;
    bool history_limited = false;
};

struct SourceProject
{
    QString id;
    QString name;
    QList<SourceBranch> branches;
};

struct SourceSnapshot
{
    QList<SourceProject> projects;
};

// One in-flight load at a time. Cancel and replacement invalidate older native
// workers and abort REST replies; results are delivered on the GUI thread.
class ProjectSourceReader final : public QObject
{
public:
    using Completion = std::function<void(const SourceSnapshot &, const QString &)>;

    explicit ProjectSourceReader(QObject *parent = nullptr);
    void load(const ProjectServerProfile &profile, Completion completion);
    void cancel();

private:
    enum class Step { Projects, Branches, History };
    void nextRestRequest(quint64 generation);
    void complete(quint64 generation, const QString &error = QString());
    bool processRestDocument(const QByteArray &json, QString &error);

    QNetworkAccessManager manager;
    QPointer<QNetworkReply> reply;
    Completion completion;
    ProjectServerProfile profile;
    SourceSnapshot snapshot;
    Step step = Step::Projects;
    int project_index = 0;
    int branch_index = 0;
    int total_branches = 0;
    quint64 generation = 0;
};
