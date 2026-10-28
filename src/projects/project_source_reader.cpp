#include "projects/project_source_reader.h"
#include "projects/dvmdb_identity.h"

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
#include <type_traits>
#include <utility>

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


// Canonical DVMDB P0: the transport always addresses revisions by UUIDv7.
// SHA-256 values authenticate their immutable records and parent bindings.
bool parseRevision(const QJsonValue &value, SourceRevision &revision)
{
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    revision.id = object.value(QStringLiteral("id")).toString();
    revision.record_sha256 = object.value(QStringLiteral("recordHash")).toString();
    const QJsonValue parents = object.value(QStringLiteral("parents"));
    const QJsonValue hashes = object.value(QStringLiteral("parentRecordHashes"));
    if (!DvmdbIdentity::isUuidV7(revision.id)
        || !DvmdbIdentity::isLegacySha256(revision.record_sha256)
        || !parents.isArray() || !hashes.isArray()
        || parents.toArray().size() != hashes.toArray().size()
        || parents.toArray().size() > 2) return false;
    QSet<QString> unique_parents;
    for (int i = 0; i < parents.toArray().size(); ++i)
    {
        const QString parent = parents.toArray().at(i).toString();
        const QString digest = hashes.toArray().at(i).toString();
        if (!DvmdbIdentity::isUuidV7(parent)
            || !DvmdbIdentity::isLegacySha256(digest)
            || parent == revision.id || unique_parents.contains(parent)) return false;
        unique_parents.insert(parent);
        revision.parents.append(parent);
        revision.parent_record_sha256.append(digest);
    }
    return true;
}

// A graph is published only if every captured branch HEAD and every parent
// reference is actually present. A partial graph cannot masquerade as complete.
bool validateCompleteGraph(const SourceProject &project)
{
    QHash<QString, QString> hashes;
    QHash<QString, QStringList> parent_links;
    QSet<QString> branch_ids;
    QSet<QString> branch_names;
    for (const SourceRevision &revision : project.revisions)
    {
        if (!DvmdbIdentity::isUuidV7(revision.id)
            || !DvmdbIdentity::isLegacySha256(revision.record_sha256)
            || hashes.contains(revision.id)
            || revision.parents.size() != revision.parent_record_sha256.size()) return false;
        hashes.insert(revision.id, revision.record_sha256);
        parent_links.insert(revision.id, revision.parents);
    }
    for (const SourceBranch &branch : project.branches)
    {
        if (branch.name.isEmpty() || branch_names.contains(branch.name)
            || !DvmdbIdentity::isUuidV7(branch.id) || branch_ids.contains(branch.id)
            || (branch.head.isEmpty()
                ? !branch.record_sha256.isEmpty()
                : (!DvmdbIdentity::isUuidV7(branch.head)
                   || hashes.value(branch.head) != branch.record_sha256))) return false;
        if (!branch.fork_revision_id.isEmpty())
        {
            if (branch.origin_branch_id.isEmpty()
                || !DvmdbIdentity::isUuidV7(branch.fork_revision_id)
                || !hashes.contains(branch.fork_revision_id)) return false;
            // Fork provenance is metadata, but it must identify a genuine
            // ancestor of this HEAD. Never turn it into a graph parent edge.
            QSet<QString> visited;
            QStringList pending{branch.head};
            bool reached_fork = false;
            while (!pending.isEmpty())
            {
                const QString revision = pending.takeLast();
                if (visited.contains(revision)) continue;
                visited.insert(revision);
                if (revision == branch.fork_revision_id)
                {
                    reached_fork = true;
                    break;
                }
                pending.append(parent_links.value(revision));
            }
            if (!reached_fork) return false;
        }
        branch_names.insert(branch.name);
        branch_ids.insert(branch.id);
    }
    QHash<QString, int> remaining_parents;
    QHash<QString, QStringList> children;
    QStringList roots;
    for (const SourceRevision &revision : project.revisions)
    {
        remaining_parents.insert(revision.id, revision.parents.size());
        if (revision.parents.isEmpty()) roots.append(revision.id);
        for (int i = 0; i < revision.parents.size(); ++i)
        {
            const QString parent = revision.parents.at(i);
            if (hashes.value(parent) != revision.parent_record_sha256.at(i)) return false;
            children[parent].append(revision.id);
        }
    }
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
// Canonical native graph reading uses UUIDv7 and record hashes. Retain the
// compile-time version check for Standalone submodules not updated to DVMDB
// UUIDv7 yet; older APIs fail explicitly instead of reading SHA histories.
template <typename Service, typename = void>
struct NativeRevisionGraphV2Available : std::false_type {};

template <typename Service>
struct NativeRevisionGraphV2Available<Service, std::void_t<decltype(
    std::declval<Service &>().revisionGraphV2(
        std::declval<std::string_view>(), std::size_t{}))>> : std::true_type {};

template <typename Service>
bool loadNativeV2Graph(Service &service, const std::string &project_id,
                       SourceProject &project, int &total_branches,
                       int &total_revisions,
                       const std::shared_ptr<std::atomic_bool> &cancelled)
{
    if constexpr (!NativeRevisionGraphV2Available<Service>::value)
    {
        return false;
    }
    else
    {
        if (!DvmdbIdentity::isUuidV7(QString::fromStdString(project_id)))
            throw std::runtime_error("DVMDB project is not UUIDv7; recreate obsolete pre-release data");
        using Page = decltype(service.revisionGraphV2(project_id, graph_page_size));
        Page page = service.revisionGraphV2(project_id, graph_page_size);
        QStringList pinned_heads;
        QSet<QString> seen_revisions;
        std::size_t prior_skip = 0;
        bool first_page = true;
        for (;;)
        {
            if (cancelled->load()) return true;
            QStringList current_heads;
            QSet<QString> current_branch_ids;
            QSet<QString> current_names;
            for (const auto &head : page.heads)
            {
                const QString name = QString::fromStdString(head.name);
                const QString branch_id = QString::fromStdString(head.branchId.toString());
                const QString revision_id = []<typename Head>(const Head &item) -> QString {
                    if constexpr (requires { item.revision.has_value(); })
                        return item.revision ? QString::fromStdString(item.revision->toString()) : QString();
                    else return QString::fromStdString(item.revision.toString());
                }(head);
                const QString digest = []<typename Head>(const Head &item) -> QString {
                    if constexpr (requires { item.recordHash.has_value(); })
                        return item.recordHash ? QString::fromStdString(aowis::dvmdb::toHex(*item.recordHash)) : QString();
                    else return QString::fromStdString(aowis::dvmdb::toHex(item.recordHash));
                }(head);
                if (name.isEmpty() || !DvmdbIdentity::isUuidV7(branch_id)
                    || (revision_id.isEmpty() ? !digest.isEmpty()
                        : (!DvmdbIdentity::isUuidV7(revision_id)
                           || !DvmdbIdentity::isLegacySha256(digest)))
                    || current_branch_ids.contains(branch_id)
                    || current_names.contains(name))
                    throw std::runtime_error("Invalid UUIDv7 branch HEAD in native graph");
                current_branch_ids.insert(branch_id);
                current_names.insert(name);
                QString origin;
                // Embedded Standalone builds can still link a DVMDB checkout
                // without branch provenance; do not fabricate a parent.
                if constexpr (requires { head.originBranchId; })
                    if (head.originBranchId)
                        origin = QString::fromStdString(head.originBranchId->toString());
                if (!origin.isEmpty() && (!DvmdbIdentity::isUuidV7(origin) || origin == branch_id))
                    throw std::runtime_error("Invalid DVMDB branch creation origin");
                QString fork_revision;
                // Preserve compatibility with embedded DVMDB versions which
                // predate immutable fork provenance. Never infer it from HEAD.
                if constexpr (requires { head.forkRevisionId; })
                    if (head.forkRevisionId)
                        fork_revision = QString::fromStdString(head.forkRevisionId->toString());
                if (!fork_revision.isEmpty() && (origin.isEmpty()
                    || !DvmdbIdentity::isUuidV7(fork_revision)))
                    throw std::runtime_error("Invalid DVMDB branch fork revision");
                current_heads.append(name + QLatin1Char('\n') + branch_id
                                     + QLatin1Char('\n') + revision_id
                                     + QLatin1Char('\n') + digest
                                     + QLatin1Char('\n') + origin
                                     + QLatin1Char('\n') + fork_revision);
                if (first_page)
                {
                    SourceBranch branch;
                    branch.name = name;
                    branch.id = branch_id;
                    branch.head = revision_id;
                    branch.record_sha256 = digest;
                    branch.origin_branch_id = origin;
                    branch.fork_revision_id = fork_revision;
                    project.branches.append(branch);
                }
            }
            if (first_page)
            {
                total_branches += current_heads.size();
                if (total_branches > maximum_branches)
                    throw std::runtime_error("Too many UUIDv7 branches to display");
                pinned_heads = current_heads;
            }
            else if (current_heads != pinned_heads)
                throw std::runtime_error("DVMDB UUIDv7 graph HEAD snapshot changed between pages");

            for (const auto &entry : page.revisions)
            {
                SourceRevision revision;
                revision.id = QString::fromStdString(entry.id.toString());
                if (!DvmdbIdentity::isUuidV7(revision.id)
                    || seen_revisions.contains(revision.id))
                    throw std::runtime_error("Invalid or duplicated UUIDv7 revision in graph");
                seen_revisions.insert(revision.id);
                revision.record_sha256 = QString::fromStdString(
                    aowis::dvmdb::toHex(entry.recordHash));
                if (!DvmdbIdentity::isLegacySha256(revision.record_sha256))
                    throw std::runtime_error("Invalid canonical SHA-256 record digest");
                for (const auto &parent : entry.parents)
                {
                    const QString parent_id = QString::fromStdString(parent.id.toString());
                    if (!DvmdbIdentity::isUuidV7(parent_id))
                        throw std::runtime_error("Invalid UUIDv7 graph parent");
                    revision.parents.append(parent_id);
                    const QString expected = QString::fromStdString(
                        aowis::dvmdb::toHex(parent.expectedRecordHash));
                    if (!DvmdbIdentity::isLegacySha256(expected))
                        throw std::runtime_error("Invalid parent record digest in native graph");
                    revision.parent_record_sha256.append(expected);
                }
                if (revision.parents.size() > 2 || revision.parents.contains(revision.id))
                    throw std::runtime_error("Invalid UUIDv7 revision ancestry");
                project.revisions.append(revision);
                ++total_revisions;
                if (project.revisions.size() > maximum_revisions_per_project
                    || total_revisions > maximum_total_revisions)
                    throw std::runtime_error("UUIDv7 graph exceeds the GUI's safe display limit");
            }
            if (!page.next) break;
            if (page.revisions.empty() || page.next->skip <= prior_skip)
                throw std::runtime_error("DVMDB UUIDv7 graph cursor did not advance");
            prior_skip = page.next->skip;
            first_page = false;
            if (cancelled->load()) return true;
            page = service.revisionGraphV2(project_id, graph_page_size, page.next);
        }
        project.graph_complete = validateCompleteGraph(project);
        if (!project.graph_complete)
            throw std::runtime_error("DVMDB returned an incomplete UUIDv7 graph");
        return true;
    }
}

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
        if (!loadNativeV2Graph(service, info.id, project, branch_count,
                               revision_count, cancelled))
            throw std::runtime_error("Embedded DVMDB is outdated: UUIDv7 revisionGraphV2 is required");
        if (cancelled->load()) return {};
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
        QSet<QString> project_ids;
        for (const QJsonValue &value : projects.toArray())
        {
            if (!value.isObject()) { error = tr("Invalid project record."); return false; }
            const QJsonObject item = value.toObject();
            SourceProject project;
            project.id = item.value(QStringLiteral("id")).toString();
            project.name = item.value(QStringLiteral("name")).toString();
            if (!DvmdbIdentity::isUuidV7(project.id) || project.name.isEmpty()
                || project_ids.contains(project.id))
            { error = tr("Invalid or duplicate project identity."); return false; }
            project_ids.insert(project.id);
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
        const QJsonValue head_revision = head.value(QStringLiteral("revision"));
        const QJsonValue head_digest = head.value(QStringLiteral("recordHash"));
        if ((!head_revision.isString() && !head_revision.isNull())
            || (!head_digest.isString() && !head_digest.isNull())
            || head_revision.isNull() != head_digest.isNull())
        { error = tr("Invalid nullable DVMDB branch HEAD."); return false; }
        branch.head = head_revision.toString();
        branch.id = head.value(QStringLiteral("branchId")).toString();
        branch.record_sha256 = head_digest.toString();
        const QJsonValue origin = head.value(QStringLiteral("originBranchId"));
        if (!origin.isUndefined() && !origin.isNull())
        {
            if (!origin.isString())
            { error = tr("Invalid DVMDB branch creation origin."); return false; }
            branch.origin_branch_id = origin.toString();
            if (!DvmdbIdentity::isUuidV7(branch.origin_branch_id)
                || branch.origin_branch_id == branch.id)
            { error = tr("Invalid DVMDB branch creation origin."); return false; }
        }
        const QJsonValue fork = head.value(QStringLiteral("forkRevisionId"));
        if (!fork.isUndefined() && !fork.isNull())
        {
            if (!fork.isString())
            { error = tr("Invalid DVMDB branch fork revision."); return false; }
            branch.fork_revision_id = fork.toString();
            if (!DvmdbIdentity::isUuidV7(branch.fork_revision_id)
                || branch.origin_branch_id.isEmpty())
            { error = tr("Invalid DVMDB branch fork revision."); return false; }
        }
        if (branch.name.isEmpty() || !DvmdbIdentity::isUuidV7(branch.id)
            || (branch.head.isEmpty() ? !branch.record_sha256.isEmpty()
                : (!DvmdbIdentity::isUuidV7(branch.head)
                   || !DvmdbIdentity::isLegacySha256(branch.record_sha256)))
            || names.contains(branch.name))
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
                || page_heads[index].id != project.branches[index].id
                || page_heads[index].head != project.branches[index].head
                || page_heads[index].record_sha256 != project.branches[index].record_sha256
                || page_heads[index].origin_branch_id != project.branches[index].origin_branch_id
                || page_heads[index].fork_revision_id != project.branches[index].fork_revision_id)
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
