#pragma once

#include "config/project_server_profile_storage.h"

#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>

#include <functional>

class QNetworkReply;

// DVMDB currently supports project and branch creation, but not lifecycle
// rename/delete. Exactly one write may be in flight. A timeout on POST does
// not prove that the server rejected the write: callers must refresh before
// attempting another creation.
class ProjectSourceWriter final : public QObject
{
public:
    using Completion = std::function<void(const QString &created_id, const QString &error)>;

    explicit ProjectSourceWriter(QObject *parent = nullptr);
    bool isBusy() const;
    void createProject(const ProjectServerProfile &profile, const QString &name, Completion completion);
    void createBranch(const ProjectServerProfile &profile, const QString &project_id,
                      const QString &name, const QString &from_branch,
                      const QString &from_revision, Completion completion);

private:
    enum class Operation { Project, Branch };
    void start(const ProjectServerProfile &profile, Operation operation,
               const QString &project_id, const QString &name, const QString &from_branch,
               const QString &from_revision, Completion completion);
    void finish(const QString &id, const QString &error);

    QNetworkAccessManager manager;
    QPointer<QNetworkReply> reply;
    Completion completion;
    bool busy = false;
};
