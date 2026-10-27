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
#include <optional>
#include <stdexcept>

#ifdef AOWIS_STANDALONE_DVMDB_ENABLED
#include "aowis/dvmdb/project_service.h"
#include <filesystem>
#endif

namespace
{
constexpr int graph_page_size = 256;
constexpr int maximum_projects = 100;
constexpr int maximum_branches = 150;
constexpr int maximum_revisions_per_project = 25000;
constexpr int maximum_total_revisions = 60000;
constexpr int maximum_json_bytes = 4 * 1024 * 1024;
constexpr int maximum_cursor_bytes = 180000;

QString segment(const QString &part)
{
    return QString::fromLatin1(part.toUtf8().toPercentEncoding());
}

bool validHash(const QString &value)
{
    if (value.size() != 64) return false;
    for (const QChar c : value)
        if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9'))
            || (c >= QLatin1Char('a') && c <= QLatin1Char('f')))) return false;
    return true;
}

bool parseRevision(const QJsonValue &value, SourceRevision &revision)
{
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    revision.id = object.value(QStringLiteral("id")).toString();
    if (!validHash(revision.id) || !object.value(QStringLiteral("parents")).isArray()) return false;
    for (const QJsonValue &parent : object.value(QStringLiteral("parents")).toArray())
    {
        if (!parent.isString() || !validHash(parent.toString())) return false;
        revision.parents.append(parent.toString());
    }
    return revision.parents.size() <= 2 && !revision.parents.contains(revision.id);
}

// A graph is published only if every captured branch HEAD and every parent
// reference is actually present. A partial graph cannot masquerade as complete.
bool validateCompleteGraph(const SourceProject &project)
{
    QSet<QString> ids;
    for (const SourceRevision &revision : project.revisions) ids.insert(revision.id);
    for (const SourceBranch &branch : project.branches)
        if (!ids.contains(branch.head)) return false;
    QHash<QString, int> remaining_parents;
    QHash<QString, QStringList> children;
    QStringList roots;
    for (const SourceRevision &revision : project.revisions)
    {
        remaining_parents.insert(revision.id, revision.parents.size());
        if (revision.parents.isEmpty()) roots.append(revision.id);
        for (const QString &parent : revision.parents)
        {
            if (!ids.contains(parent)) return false;
            children[parent].append(revision.id);
        }
    }
    // A malformed REST payload could contain a closed ancestry cycle. The
    // graph is valid only if every vertex is reachable in a topological pass.
    int consumed = 0;
    while (!roots.isEmpty())
    {
        const QString id = roots.takeLast();
        ++consumed;
        for (const QString &child : children.value(id))
        {
            const int count = remaining_parents.value(child) - 1;
            remaining_parents.insert(child, count);
            if (count == 0) roots.append(child);
        }
    }
    return consumed == project.revisions.size();
}

#ifdef AOWIS_STANDALONE_DVMDB_ENABLED
SourceSnapshot loadNative(const ProjectServerProfile &profile,
                          const std::shared_ptr<std::atomic_bool> &cancelled)
{
    SourceSnapshot snapshot;
    aowis::dvmdb::ProjectService service(std::filesystem::path(profile.local_path.toStdString()));
    const std::vector<aowis::dvmdb::ProjectInfo> projects = service.listProjects();
    if (projects.size() > maximum_projects) throw std::runtime_error("Too many projects to display");
    int branch_count = 0;
    int revision_count = 0;
    for (const aowis::dvmdb::ProjectInfo &info : projects)
    {
        if (cancelled->load()) return {};
        SourceProject project;
        project.id = QString::fromStdString(info.id);
        project.name = QString::fromStdString(info.name);
        std::optional<aowis::dvmdb::RevisionGraphCursor> cursor;
        QSet<QString> seen;
        std::vector<aowis::dvmdb::RevisionGraphHead> pinned_heads;
        do
        {
            if (cancelled->load()) return {};
            const aowis::dvmdb::RevisionGraphPage page = service.revisionGraph(
                info.id, graph_page_size, cursor);
            if (!cursor)
            {
                pinned_heads = page.heads;
                branch_count += int(pinned_heads.size());
                if (branch_count > maximum_branches)
                    throw std::runtime_error("Too many branches to display");
                for (const aowis::dvmdb::RevisionGraphHead &head : pinned_heads)
                    project.branches.append({QString::fromStdString(head.name),
                        QString::fromStdString(aowis::dvmdb::toHex(head.revision))});
            }
            else if (page.heads.size() != pinned_heads.size())
                throw std::runtime_error("DVMDB graph HEAD set changed between pages");
            else
                for (std::size_t i = 0; i < pinned_heads.size(); ++i)
                    if (page.heads[i].name != pinned_heads[i].name
                        || page.heads[i].revision != pinned_heads[i].revision)
                        throw std::runtime_error("DVMDB graph HEAD set changed between pages");
            for (const aowis::dvmdb::Revision &entry : page.revisions)
            {
                const QString hash = QString::fromStdString(aowis::dvmdb::toHex(entry.id));
                if (seen.contains(hash))
                    throw std::runtime_error("Duplicate DVMDB graph revision across pages");
                seen.insert(hash);
                SourceRevision revision;
                revision.id = hash;
                for (const aowis::dvmdb::Hash &parent : entry.parents)
                    revision.parents.append(QString::fromStdString(aowis::dvmdb::toHex(parent)));
                project.revisions.append(revision);
                ++revision_count;
                if (project.revisions.size() > maximum_revisions_per_project
                    || revision_count > maximum_total_revisions)
                    throw std::runtime_error("Revision graph exceeds the GUI's safe display limit");
            }
            if (page.next && (page.revisions.empty() || page.next->skip <= (cursor ? cursor->skip : 0)))
                throw std::runtime_error("DVMDB graph pagination failed to progress");
            cursor = page.next;
        } while (cursor);
        project.graph_complete = validateCompleteGraph(project);
        if (!project.graph_complete)
            throw std::runtime_error("DVMDB returned an incomplete revision graph");
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
    if (this->native_cancel) this->native_cancel->store(true);
    this->native_cancel.reset();
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
    this->total_branches = 0;
    this->total_revisions = 0;
    this->next_cursor.clear();
    this->used_cursors.clear();
    this->revisions_seen.clear();
    const quint64 requested = this->generation;
    if (profile.connection_type == QStringLiteral("local"))
    {
#ifdef AOWIS_STANDALONE_DVMDB_ENABLED
        struct Result { SourceSnapshot snapshot; QString error; };
        const std::shared_ptr<Result> result = std::make_shared<Result>();
        const std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
        this->native_cancel = cancelled;
        QThread *thread = QThread::create([profile, result, cancelled]
        {
            try { result->snapshot = loadNative(profile, cancelled); }
            catch (const std::exception &error) { result->error = QString::fromUtf8(error.what()); }
            catch (...) { result->error = QStringLiteral("Unknown embedded DVMDB error"); }
        });
        connect(thread, &QThread::finished, this, [this, result, requested]
        {
            if (requested != this->generation) return;
            this->native_cancel.reset();
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
    if (this->step == Step::Graph && this->project_index >= this->snapshot.projects.size())
    {
        complete(requested);
        return;
    }
    QString path = QStringLiteral("/projects");
    if (this->step == Step::Graph)
    {
        path += QStringLiteral("/") + segment(this->snapshot.projects[this->project_index].id)
            + QStringLiteral("/revision-graph?limit=%1").arg(graph_page_size);
        if (!this->next_cursor.isEmpty())
            path += QStringLiteral("&cursor=") + segment(this->next_cursor);
    }
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
    const QPointer<QNetworkReply> deadline_reply(pending);
    QTimer::singleShot(15000, this, [this, requested, deadline_reply]
    {
        if (requested == this->generation && deadline_reply) deadline_reply->abort();
    });
    connect(pending, &QNetworkReply::finished, this, [this, requested, pending]
    {
        if (requested != this->generation) { pending->deleteLater(); return; }
        this->reply.clear();
        pending->deleteLater();
        if (pending->error() != QNetworkReply::NoError)
        {
            complete(requested, tr("DVMDB request failed: %1").arg(pending->errorString()));
            return;
        }
        if (pending->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
        {
            complete(requested, tr("DVMDB request returned HTTP %1")
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
        // Avoid deep recursive callbacks for large paginated histories.
        QTimer::singleShot(0, this, [this, requested] { nextRestRequest(requested); });
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
    if (this->step == Step::Projects)
    {
        const QJsonValue projects = object.value(QStringLiteral("projects"));
        if (!projects.isArray() || projects.toArray().size() > maximum_projects)
        { error = tr("Invalid or oversized DVMDB project list."); return false; }
        for (const QJsonValue &value : projects.toArray())
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
        this->step = Step::Graph;
        return true;
    }
    const QJsonValue heads = object.value(QStringLiteral("heads"));
    const QJsonValue revisions = object.value(QStringLiteral("revisions"));
    const QJsonValue complete = object.value(QStringLiteral("complete"));
    const QJsonValue cursor = object.value(QStringLiteral("nextCursor"));
    if (!heads.isArray() || !revisions.isArray() || !complete.isBool()
        || revisions.toArray().size() > graph_page_size || heads.toArray().size() > maximum_branches
        || (cursor.isString() && cursor.toString().toUtf8().size() > maximum_cursor_bytes)
        || (!cursor.isString() && !cursor.isNull()))
    { error = tr("Invalid DVMDB revision graph page."); return false; }

    SourceProject &project = this->snapshot.projects[this->project_index];
    QList<SourceBranch> page_heads;
    QSet<QString> names;
    for (const QJsonValue &value : heads.toArray())
    {
        if (!value.isObject()) { error = tr("Invalid DVMDB graph HEAD."); return false; }
        const QJsonObject head = value.toObject();
        SourceBranch branch;
        branch.name = head.value(QStringLiteral("branch")).toString();
        branch.head = head.value(QStringLiteral("revision")).toString();
        if (branch.name.isEmpty() || !validHash(branch.head) || names.contains(branch.name))
        { error = tr("Invalid or repeated DVMDB branch HEAD."); return false; }
        names.insert(branch.name);
        page_heads.append(branch);
    }
    if (this->next_cursor.isEmpty())
    {
        this->total_branches += page_heads.size();
        if (this->total_branches > maximum_branches)
        { error = tr("Too many branches to display."); return false; }
        project.branches = page_heads;
    }
    else
    {
        if (page_heads.size() != project.branches.size())
        { error = tr("DVMDB graph HEADs changed during pagination."); return false; }
        for (int index = 0; index < page_heads.size(); ++index)
            if (page_heads[index].name != project.branches[index].name
                || page_heads[index].head != project.branches[index].head)
            { error = tr("DVMDB graph HEADs changed during pagination."); return false; }
    }
    if (revisions.toArray().isEmpty() && !complete.toBool())
    { error = tr("DVMDB returned an empty intermediate graph page."); return false; }
    for (const QJsonValue &value : revisions.toArray())
    {
        SourceRevision revision;
        if (!parseRevision(value, revision) || this->revisions_seen.contains(revision.id))
        { error = tr("Invalid or repeated DVMDB graph revision."); return false; }
        this->revisions_seen.insert(revision.id, project.revisions.size());
        project.revisions.append(revision);
        ++this->total_revisions;
        if (project.revisions.size() > maximum_revisions_per_project
            || this->total_revisions > maximum_total_revisions)
        { error = tr("Revision graph exceeds the GUI's safe display limit."); return false; }
    }
    if (complete.toBool())
    {
        if (!cursor.isNull() || !validateCompleteGraph(project))
        { error = tr("DVMDB returned an incomplete revision graph."); return false; }
        project.graph_complete = true;
        ++this->project_index;
        this->next_cursor.clear();
        this->used_cursors.clear();
        this->revisions_seen.clear();
    }
    else
    {
        const QString next = cursor.toString();
        if (next.isEmpty() || this->used_cursors.contains(next))
        { error = tr("DVMDB revision graph cursor did not advance."); return false; }
        this->used_cursors.insert(next);
        this->next_cursor = next;
    }
    return true;
}

void ProjectSourceReader::complete(quint64 requested, const QString &error)
{
    if (requested != this->generation) return;
    Completion done = std::move(this->completion);
    this->completion = {};
    if (done) done(error.isEmpty() ? this->snapshot : SourceSnapshot{}, error);
}
