#include "projects/project_source_reader.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <exception>
#include <memory>
#include <stdexcept>

#ifdef AOWIS_STANDALONE_DVMDB_ENABLED
#include "aowis/dvmdb/project_service.h"
#include <filesystem>
#endif

namespace
{
constexpr int history_page_size = 100;
constexpr int maximum_projects = 100;
constexpr int maximum_branches = 30;
constexpr int maximum_total_branches = 150;
constexpr int maximum_json_bytes = 4 * 1024 * 1024;

QString segment(const QString &part)
{
    return QString::fromLatin1(part.toUtf8().toPercentEncoding());
}

bool parseRevision(const QJsonValue &value, SourceRevision &revision)
{
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    revision.id = object.value(QStringLiteral("id")).toString();
    if (revision.id.size() != 64 || !object.value(QStringLiteral("parents")).isArray()) return false;
    for (const QJsonValue &parent : object.value(QStringLiteral("parents")).toArray())
    {
        if (!parent.isString() || parent.toString().size() != 64) return false;
        revision.parents.append(parent.toString());
    }
    return revision.parents.size() <= 2;
}

#ifdef AOWIS_STANDALONE_DVMDB_ENABLED
SourceSnapshot loadNative(const ProjectServerProfile &profile)
{
    SourceSnapshot snapshot;
    const std::filesystem::path root(profile.local_path.toStdString());
    aowis::dvmdb::ProjectService service(root);
    const std::vector<aowis::dvmdb::ProjectInfo> projects = service.listProjects();
    if (projects.size() > maximum_projects) throw std::runtime_error("Too many projects to display");
    std::size_t branch_count = 0;
    for (const aowis::dvmdb::ProjectInfo &info : projects)
    {
        SourceProject project;
        project.id = QString::fromStdString(info.id);
        project.name = QString::fromStdString(info.name);
        const std::vector<std::string> branches = service.listBranches(info.id);
        branch_count += branches.size();
        if (branches.size() > maximum_branches || branch_count > maximum_total_branches)
            throw std::runtime_error("Too many branches to display");
        for (const std::string &branch_name : branches)
        {
            SourceBranch branch;
            branch.name = QString::fromStdString(branch_name);
            // One additional element detects omitted older revisions.
            const std::vector<aowis::dvmdb::Revision> history = service.history(
                info.id, branch_name, history_page_size + 1);
            branch.history_limited = history.size() > history_page_size;
            for (std::size_t i = 0; i < history.size() && i < history_page_size; ++i)
            {
                const aowis::dvmdb::Revision &entry = history[i];
                SourceRevision revision;
                revision.id = QString::fromStdString(aowis::dvmdb::toHex(entry.id));
                for (const aowis::dvmdb::Hash &parent : entry.parents)
                    revision.parents.append(QString::fromStdString(aowis::dvmdb::toHex(parent)));
                branch.history.append(revision);
            }
            if (!branch.history.isEmpty()) branch.head = branch.history.first().id;
            project.branches.append(branch);
        }
        snapshot.projects.append(project);
    }
    return snapshot;
}
#endif
}

ProjectSourceReader::ProjectSourceReader(QObject *parent)
    : QObject(parent), manager(this)
{
}

void ProjectSourceReader::cancel()
{
    ++this->generation;
    this->completion = {};
    if (this->reply)
    {
        QNetworkReply *previous = this->reply;
        this->reply.clear();
        previous->abort();
    }
    this->manager.clearConnectionCache();
}

void ProjectSourceReader::load(const ProjectServerProfile &profile, Completion completion)
{
    cancel();
    this->profile = profile;
    this->completion = std::move(completion);
    this->snapshot = {};
    this->step = Step::Projects;
    this->project_index = 0;
    this->branch_index = 0;
    this->total_branches = 0;
    const quint64 requested = this->generation;
    if (profile.connection_type == QStringLiteral("local"))
    {
#ifdef AOWIS_STANDALONE_DVMDB_ENABLED
        // Native filesystem operations can block; never run them in the UI thread.
        struct Result { SourceSnapshot snapshot; QString error; };
        const std::shared_ptr<Result> result = std::make_shared<Result>();
        QThread *thread = QThread::create([profile, result]
        {
            try { result->snapshot = loadNative(profile); }
            catch (const std::exception &error) { result->error = QString::fromUtf8(error.what()); }
            catch (...) { result->error = QStringLiteral("Unknown embedded DVMDB error"); }
        });
        connect(thread, &QThread::finished, this, [this, result, requested]
        {
            if (requested != this->generation) return;
            this->snapshot = result->snapshot;
            complete(requested, result->error);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
#else
        complete(requested, tr("This build does not include embedded DVMDB support."));
#endif
        return;
    }
    if (profile.connection_type != QStringLiteral("remote"))
    {
        complete(requested, tr("Unsupported database connection type."));
        return;
    }
    // The current DVMDB HTTP service does not implement authentication.
    if (profile.auth_mode != QStringLiteral("None") && !profile.auth_mode.isEmpty())
    {
        complete(requested, tr("DVMDB REST authentication is not implemented; select 'None' to connect."));
        return;
    }
    const QUrl url(profile.endpoint);
    if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))
        || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
    {
        complete(requested, tr("Invalid REST database endpoint."));
        return;
    }
    nextRestRequest(requested);
}

void ProjectSourceReader::nextRestRequest(quint64 requested)
{
    if (requested != this->generation) return;
    while (this->step == Step::Branches && this->project_index >= this->snapshot.projects.size())
    {
        complete(requested);
        return;
    }
    QString path = QStringLiteral("/projects");
    if (this->step != Step::Projects)
    {
        SourceProject &project = this->snapshot.projects[this->project_index];
        path += QStringLiteral("/") + segment(project.id) + QStringLiteral("/branches");
        if (this->step == Step::History)
        {
            if (this->branch_index >= project.branches.size())
            {
                ++this->project_index;
                this->branch_index = 0;
                this->step = Step::Branches;
                nextRestRequest(requested);
                return;
            }
            path += QStringLiteral("/") + segment(project.branches[this->branch_index].name)
                 + QStringLiteral("/history?limit=101");
        }
    }
    // Endpoints can be host roots or an explicit /dvmdb/v1 API base path.
    QString root = this->profile.endpoint.trimmed();
    while (root.endsWith(QLatin1Char('/'))) root.chop(1);
    if (!root.endsWith(QStringLiteral("/dvmdb/v1"))) root += QStringLiteral("/dvmdb/v1");
    QNetworkRequest request(QUrl(root + path));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(12000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::ManualRedirectPolicy);
    this->reply = this->manager.get(request);
    QNetworkReply *pending = this->reply;
    // Absolute deadline as well as Qt's inactivity-based transfer timeout.
    const QPointer<QNetworkReply> deadline_reply(pending);
    QTimer::singleShot(15000, this, [this, requested, deadline_reply]
    {
        if (requested == this->generation && deadline_reply) deadline_reply->abort();
    });
    connect(pending, &QNetworkReply::finished, this, [this, requested, pending]
    {
        if (this->reply == pending) this->reply.clear();
        pending->deleteLater();
        if (requested != this->generation) return;
        if (pending->error() != QNetworkReply::NoError)
        {
            complete(requested, pending->errorString());
            return;
        }
        if (pending->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
        {
            complete(requested, tr("Unexpected HTTP response (%1).")
                .arg(pending->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));
            return;
        }
        if (pending->bytesAvailable() > maximum_json_bytes)
        {
            complete(requested, tr("DVMDB response exceeds the size limit."));
            return;
        }
        QString error;
        if (!processRestDocument(pending->readAll(), error))
        {
            complete(requested, error);
            return;
        }
        nextRestRequest(requested);
    });
}

bool ProjectSourceReader::processRestDocument(const QByteArray &json, QString &error)
{
    QJsonParseError parsing;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parsing);
    if (parsing.error != QJsonParseError::NoError || !document.isObject())
    {
        error = tr("Invalid DVMDB JSON response.");
        return false;
    }
    const QJsonObject object = document.object();
    QString key;
    if (this->step == Step::Projects) key = QStringLiteral("projects");
    else if (this->step == Step::Branches) key = QStringLiteral("branches");
    else key = QStringLiteral("revisions");
    if (!object.value(key).isArray())
    {
        error = tr("DVMDB response is missing '%1'.").arg(key);
        return false;
    }
    const QJsonArray array = object.value(key).toArray();
    if ((this->step == Step::Projects && array.size() > maximum_projects)
        || (this->step == Step::Branches && array.size() > maximum_branches)
        || (this->step == Step::History && array.size() > history_page_size + 1))
    {
        error = tr("DVMDB result exceeds the display limit.");
        return false;
    }
    if (this->step == Step::Projects)
    {
        for (const QJsonValue &value : array)
        {
            if (!value.isObject()) { error = tr("Invalid project record."); return false; }
            const QJsonObject item = value.toObject();
            SourceProject project;
            project.id = item.value(QStringLiteral("id")).toString();
            project.name = item.value(QStringLiteral("name")).toString();
            if (project.id.isEmpty() || project.name.isEmpty())
            { error = tr("Invalid project identity."); return false; }
            this->snapshot.projects.append(project);
        }
        this->step = Step::Branches;
    }
    else if (this->step == Step::Branches)
    {
        SourceProject &project = this->snapshot.projects[this->project_index];
        this->total_branches += array.size();
        if (this->total_branches > maximum_total_branches)
        { error = tr("Too many branches to display."); return false; }
        for (const QJsonValue &value : array)
        {
            if (!value.isString() || value.toString().isEmpty())
            { error = tr("Invalid branch name."); return false; }
            SourceBranch branch;
            branch.name = value.toString();
            project.branches.append(branch);
        }
        this->step = Step::History;
    }
    else
    {
        SourceBranch &branch = this->snapshot.projects[this->project_index].branches[this->branch_index];
        branch.history_limited = array.size() > history_page_size;
        for (int i = 0; i < array.size() && i < history_page_size; ++i)
        {
            SourceRevision revision;
            if (!parseRevision(array.at(i), revision))
            { error = tr("Invalid revision record."); return false; }
            branch.history.append(revision);
        }
        if (!branch.history.isEmpty()) branch.head = branch.history.first().id;
        ++this->branch_index;
    }
    return true;
}

void ProjectSourceReader::complete(quint64 requested, const QString &error)
{
    if (requested != this->generation) return;
    Completion done = std::move(this->completion);
    this->completion = {};
    if (done) done(this->snapshot, error);
}
