#include "projects/project_source_writer.h"
#include "projects/dvmdb_identity.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <exception>
#include <memory>
#include <optional>
#include <utility>
#include <stdexcept>

#ifdef AOWIS_STANDALONE_DVMDB_ENABLED
#include "aowis/dvmdb/project_service.h"
#include <filesystem>
#endif

namespace
{
QString segment(const QString &part)
{
    return QString::fromLatin1(part.toUtf8().toPercentEncoding());
}

QString validateProfile(const ProjectServerProfile &profile)
{
    if (profile.connection_type == QStringLiteral("local"))
    {
#ifdef AOWIS_STANDALONE_DVMDB_ENABLED
        if (!QDir::isAbsolutePath(profile.local_path))
            return QObject::tr("An absolute embedded DVMDB directory is required.");
        return {};
#else
        return QObject::tr("This build does not include embedded DVMDB support.");
#endif
    }
    if (profile.connection_type != QStringLiteral("remote"))
        return QObject::tr("Unsupported database connection type.");
    if (profile.auth_mode != QStringLiteral("None") && !profile.auth_mode.isEmpty())
        return QObject::tr("DVMDB REST authentication is not implemented; select 'None'.");
    const QUrl url(profile.endpoint);
    if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))
        || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
        return QObject::tr("Invalid REST database endpoint.");
    return {};
}

#ifdef AOWIS_STANDALONE_DVMDB_ENABLED
// The embedded DVMDB is the same version as this GUI (pre-release: no
// compatibility with older embedded APIs). Identities are chosen by the
// caller exactly as on the REST path, and the native calls are idempotent
// for them.
QString writeNative(const ProjectServerProfile &profile, bool project_operation,
                    const QString &project_id, const QString &name,
                    const QString &from_branch_id, const QString &from_revision,
                    const QString &created_id)
{
    aowis::dvmdb::ProjectService service(std::filesystem::path(profile.local_path.toStdString()));
    if (project_operation)
    {
        const std::string id = created_id.toStdString();
        const QString created = QString::fromStdString(
            service.createUnbornProjectV2(name.toStdString(), std::string_view(id)).id);
        if (created != created_id)
            throw std::runtime_error("DVMDB created a project with a different UUIDv7");
        return created;
    }
    std::optional<aowis::dvmdb::RevisionId> revision;
    if (!from_revision.isEmpty())
    {
        revision = aowis::dvmdb::RevisionId::parse(from_revision.toStdString());
        if (!revision)
            throw std::runtime_error("Invalid historical branch revision UUIDv7");
    }
    const aowis::dvmdb::BranchCreationResultV2 created = service.createBranchFromIdV2(
        project_id.toStdString(), name.toStdString(), from_branch_id.toStdString(),
        revision, created_id.toStdString());
    const QString id = QString::fromStdString(created.branchId.toString());
    if (id != created_id)
        throw std::runtime_error("DVMDB created a branch with a different UUIDv7");
    return id;
}
#endif
}

ProjectSourceWriter::ProjectSourceWriter(QObject *parent)
    : QObject(parent), manager(this)
{
}

bool ProjectSourceWriter::isBusy() const
{
    return this->busy;
}

void ProjectSourceWriter::createProject(const ProjectServerProfile &profile, const QString &name,
                                       Completion completion)
{
    start(profile, Operation::Project, {}, name, {}, {}, std::move(completion));
}

void ProjectSourceWriter::createBranch(const ProjectServerProfile &profile,
                                      const QString &project_id, const QString &name,
                                      const QString &from_branch_id, const QString &from_revision,
                                      Completion completion)
{
    start(profile, Operation::Branch, project_id, name, from_branch_id, from_revision,
          std::move(completion));
}

void ProjectSourceWriter::finish(const QString &id, const QString &error)
{
    this->busy = false;
    Completion done = std::move(this->completion);
    this->completion = {};
    if (done) done(id, error);
}

void ProjectSourceWriter::start(const ProjectServerProfile &profile, Operation operation,
                                const QString &project_id, const QString &name,
                                const QString &from_branch_id, const QString &from_revision,
                                Completion completion)
{
    if (this->busy)
    {
        if (completion) completion({}, tr("Another database write is in progress."));
        return;
    }
    const QString validation = validateProfile(profile);
    if (!validation.isEmpty())
    {
        if (completion) completion({}, validation);
        return;
    }
    if (name.trimmed().isEmpty()
        || (operation == Operation::Branch && (project_id.isEmpty() || from_branch_id.isEmpty())))
    {
        if (completion) completion({}, tr("Missing project or branch name/source."));
        return;
    }

    if (operation == Operation::Branch
        && (!DvmdbIdentity::isUuidV7(project_id) || !DvmdbIdentity::isUuidV7(from_branch_id)
            || (!from_revision.isEmpty() && !DvmdbIdentity::isUuidV7(from_revision))))
    {
        if (completion) completion({}, tr("Canonical DVMDB writes require UUIDv7 project, source branch and revision IDs."));
        return;
    }
    // Fixed before the first attempt, so every transport resend of this one
    // request names the same project/branch (idempotent in DVMDB).
    const QString created_id = DvmdbIdentity::newUuidV7();
    this->busy = true;
    this->completion = std::move(completion);
    if (profile.connection_type == QStringLiteral("local"))
    {
#ifdef AOWIS_STANDALONE_DVMDB_ENABLED
        struct Result { QString id; QString error; };
        const std::shared_ptr<Result> result = std::make_shared<Result>();
        QThread *thread = QThread::create([profile, operation, project_id, name,
                                           from_branch_id, from_revision, created_id, result]
        {
            try
            {
                result->id = writeNative(profile, operation == Operation::Project,
                                         project_id, name, from_branch_id, from_revision,
                                         created_id);
            }
            catch (const std::exception &error) { result->error = QString::fromUtf8(error.what()); }
            catch (...) { result->error = QStringLiteral("Unknown embedded DVMDB write error"); }
        });
        connect(thread, &QThread::finished, this, [this, result]
        {
            finish(result->id, result->error);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
#else
        finish({}, tr("Embedded DVMDB is unavailable."));
#endif
        return;
    }

    QString root = profile.endpoint.trimmed();
    while (root.endsWith(QLatin1Char('/'))) root.chop(1);
    if (!root.endsWith(QStringLiteral("/dvmdb/v1"))) root += QStringLiteral("/dvmdb/v1");
    QString path = QStringLiteral("/projects");
    QJsonObject body{{QStringLiteral("id"), created_id}, {QStringLiteral("name"), name}};
    if (operation == Operation::Project)
        body.insert(QStringLiteral("unborn"), true);
    if (operation == Operation::Branch)
    {
        path += QStringLiteral("/") + segment(project_id) + QStringLiteral("/branches");
        body.insert(QStringLiteral("fromBranchId"), from_branch_id);
        if (!from_revision.isEmpty()) body.insert(QStringLiteral("revision"), from_revision);
    }
    QNetworkRequest request(QUrl(root + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(12000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    this->reply = this->manager.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QNetworkReply *pending = this->reply;
    const QPointer<QNetworkReply> deadline(pending);
    QTimer::singleShot(15000, this, [deadline]
    {
        if (deadline) deadline->abort();
    });
    connect(pending, &QNetworkReply::finished, this, [this, pending, operation, name, created_id]
    {
        if (this->reply == pending) this->reply.clear();
        pending->deleteLater();
        const int status = pending->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (pending->bytesAvailable() > 1024 * 1024)
        {
            finish({}, tr("DVMDB write response exceeds the size limit. Refresh before retrying."));
            return;
        }
        const QByteArray payload = pending->readAll();
        const QJsonDocument document = QJsonDocument::fromJson(payload);
        const QJsonObject object = document.isObject() ? document.object() : QJsonObject();
        if (status != 201)
        {
            const QString server_error = object.value(QStringLiteral("error")).toObject()
                .value(QStringLiteral("message")).toString();
            if (status == 0)
                finish({}, tr("The write response was lost: %1. The operation may have succeeded. Refresh before retrying.")
                    .arg(pending->errorString()));
            else
                finish({}, server_error.isEmpty()
                    ? tr("DVMDB rejected the write (HTTP %1).").arg(status)
                    : tr("DVMDB rejected the write (HTTP %1): %2").arg(status).arg(server_error));
            return;
        }
        if (pending->error() != QNetworkReply::NoError || !document.isObject())
        {
            finish({}, tr("Invalid success response. The operation may have succeeded. Refresh before retrying."));
            return;
        }
        if (operation == Operation::Project)
        {
            const QString id = object.value(QStringLiteral("id")).toString();
            if (id != created_id || object.value(QStringLiteral("name")).toString() != name)
            {
                finish({}, tr("Invalid project creation response. Refresh before retrying."));
                return;
            }
            finish(id, {});
            return;
        }
        if (!DvmdbIdentity::isUuidV7(object.value(QStringLiteral("id")).toString())
            || object.value(QStringLiteral("branchId")).toString() != created_id
            || object.value(QStringLiteral("branch")).toString() != name)
        {
            finish({}, tr("Invalid branch creation response. Refresh before retrying."));
            return;
        }
        finish(object.value(QStringLiteral("branchId")).toString(), {});
    });
}
