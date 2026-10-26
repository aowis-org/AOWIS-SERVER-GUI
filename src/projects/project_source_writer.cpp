#include "projects/project_source_writer.h"

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
QString writeNative(const ProjectServerProfile &profile, bool project_operation,
                    const QString &project_id, const QString &name,
                    const QString &from_branch, const QString &from_revision)
{
    aowis::dvmdb::ProjectService service(std::filesystem::path(profile.local_path.toStdString()));
    if (project_operation)
        return QString::fromStdString(service.createProject(name.toStdString()).id);
    const std::string source = from_branch.toStdString();
    const std::optional<std::string_view> branch = source.empty()
        ? std::nullopt : std::optional<std::string_view>(source);
    const std::optional<aowis::dvmdb::Hash> revision = from_revision.isEmpty()
        ? std::nullopt
        : std::optional<aowis::dvmdb::Hash>(aowis::dvmdb::fromHex(from_revision.toStdString()));
    service.createBranch(project_id.toStdString(), name.toStdString(), branch, revision);
    return name;
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
                                      const QString &from_branch, const QString &from_revision,
                                      Completion completion)
{
    start(profile, Operation::Branch, project_id, name, from_branch, from_revision,
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
                                const QString &from_branch, const QString &from_revision,
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
    if (name.trimmed().isEmpty() || (operation == Operation::Branch && project_id.isEmpty())
        || (!from_revision.isEmpty() && from_branch.isEmpty()))
    {
        if (completion) completion({}, tr("Missing project or branch name/source."));
        return;
    }

    this->busy = true;
    this->completion = std::move(completion);
    if (profile.connection_type == QStringLiteral("local"))
    {
#ifdef AOWIS_STANDALONE_DVMDB_ENABLED
        struct Result { QString id; QString error; };
        const std::shared_ptr<Result> result = std::make_shared<Result>();
        QThread *thread = QThread::create([profile, operation, project_id, name,
                                           from_branch, from_revision, result]
        {
            try
            {
                result->id = writeNative(profile, operation == Operation::Project,
                                         project_id, name, from_branch, from_revision);
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
    QJsonObject body{{QStringLiteral("name"), name}};
    if (operation == Operation::Branch)
    {
        path += QStringLiteral("/") + segment(project_id) + QStringLiteral("/branches");
        if (!from_branch.isEmpty()) body.insert(QStringLiteral("fromBranch"), from_branch);
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
    connect(pending, &QNetworkReply::finished, this, [this, pending, operation, name]
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
            if (id.isEmpty() || object.value(QStringLiteral("name")).toString() != name)
            {
                finish({}, tr("Invalid project creation response. Refresh before retrying."));
                return;
            }
            finish(id, {});
            return;
        }
        if (object.value(QStringLiteral("id")).toString().size() != 64)
        {
            finish({}, tr("Invalid branch creation response. Refresh before retrying."));
            return;
        }
        finish(name, {});
    });
}
