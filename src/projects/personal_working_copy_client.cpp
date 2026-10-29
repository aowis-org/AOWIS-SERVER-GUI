#include "projects/personal_working_copy_client.h"
#include "projects/dvmdb_identity.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QUuid>
#include <QUrl>

#include <utility>

namespace
{
QString endpointRoot(const QString &endpoint)
{
    QString url = endpoint;
    while (url.endsWith(QLatin1Char('/'))) url.chop(1);
    if (!url.endsWith(QStringLiteral("/dvmdb/v1"))) url += QStringLiteral("/dvmdb/v1");
    return url;
}

QString responseError(QNetworkReply *reply, const QByteArray &bytes)
{
    const QString message = QJsonDocument::fromJson(bytes).object()
        .value(QStringLiteral("error")).toString();
    if (!message.isEmpty()) return message;
    return reply->errorString();
}

bool decimal(const QString &value)
{
    if (value.isEmpty() || value.size() > 20) return false;
    for (const QChar digit : value)
        if (digit < QLatin1Char('0') || digit > QLatin1Char('9')) return false;
    return true;
}
}

PersonalWorkingCopyClient::PersonalWorkingCopyClient(QObject *parent)
    : QObject(parent), manager(this)
{
}

QString PersonalWorkingCopyClient::newSessionUuidV7()
{
    QByteArray bytes(16, '\0');
    const quint64 milliseconds = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
    for (int index = 0; index < 6; ++index)
        bytes[index] = static_cast<char>((milliseconds >> (40 - 8 * index)) & 0xff);
    for (int index = 6; index < 16; ++index)
        bytes[index] = static_cast<char>(QRandomGenerator::global()->generate() & 0xff);
    bytes[6] = static_cast<char>((static_cast<unsigned char>(bytes[6]) & 0x0f) | 0x70);
    bytes[8] = static_cast<char>((static_cast<unsigned char>(bytes[8]) & 0x3f) | 0x80);
    return QUuid::fromRfc4122(bytes).toString(QUuid::WithoutBraces);
}

bool PersonalWorkingCopyClient::parseDraft(const QJsonObject &object,
                                            PersonalWorkingCopyInfo &draft)
{
    draft.id = object.value(QStringLiteral("id")).toString();
    draft.project_id = object.value(QStringLiteral("projectId")).toString();
    draft.branch_id = object.value(QStringLiteral("branchId")).toString();
    draft.session_id = object.value(QStringLiteral("sessionId")).toString();
    draft.generation = object.value(QStringLiteral("generation")).toString();
    const QJsonValue base = object.value(QStringLiteral("baseRevision"));
    draft.base_revision = base.isNull() ? QString() : base.toString();
    if (!DvmdbIdentity::isUuidV7(draft.id) || !DvmdbIdentity::isUuidV7(draft.project_id)
        || !DvmdbIdentity::isUuidV7(draft.branch_id)
        || !DvmdbIdentity::isUuidV7(draft.session_id)
        || (!base.isNull() && !DvmdbIdentity::isUuidV7(draft.base_revision))
        || !decimal(draft.generation)
        || !object.value(QStringLiteral("dirty")).isBool()) return false;
    draft.dirty = object.value(QStringLiteral("dirty")).toBool();
    return true;
}

void PersonalWorkingCopyClient::request(const PersonalRepositoryTarget &target,
                                        const QByteArray &method, const QString &path,
                                        const QJsonObject &body, Response completion)
{
    if (target.kind != PersonalRepositoryTarget::Kind::Http)
    {
        completion({}, QStringLiteral("HTTP personal DVMDB is not configured. "
                                      "Standalone embedded draft integration is a separate VC-04 step."));
        return;
    }
    const PersonalRepositoryProfile profile{target.endpoint, target.repository_id};
    QString invalid;
    if (!validatePersonalRepositoryProfile(profile, &invalid))
    {
        completion({}, invalid);
        return;
    }
    const QString root = endpointRoot(target.endpoint);
    QNetworkRequest verify(QUrl(root + QStringLiteral("/repository/tracking")));
    verify.setTransferTimeout(8000);
    QNetworkReply *identity = this->manager.get(verify);
    connect(identity, &QNetworkReply::finished, this,
            [this, identity, root, target, method, path, body,
             completion = std::move(completion)]() mutable
    {
        const QByteArray bytes = identity->readAll();
        const bool ok = identity->error() == QNetworkReply::NoError
            && identity->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        identity->deleteLater();
        if (!ok)
        {
            completion({}, QStringLiteral("Personal DVMDB identity lookup failed."));
            return;
        }
        const QJsonObject repository = QJsonDocument::fromJson(bytes).object();
        if (repository.value(QStringLiteral("repositoryId")).toString() != target.repository_id
            || repository.value(QStringLiteral("role")).toString() != QStringLiteral("private"))
        {
            completion({}, QStringLiteral("Configured repository UUID or private role does not match."));
            return;
        }
        QNetworkRequest call(QUrl(root + path));
        call.setTransferTimeout(10000);
        call.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QNetworkReply *reply = method == "GET"
            ? this->manager.get(call)
            : this->manager.post(call, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this,
                [reply, completion = std::move(completion)]() mutable
        {
            const QByteArray data = reply->readAll();
            const bool success = reply->error() == QNetworkReply::NoError
                && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 200
                && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() < 300;
            const QString error = success ? QString() : responseError(reply, data);
            reply->deleteLater();
            if (!success) { completion({}, error); return; }
            QJsonParseError parsing;
            const QJsonDocument document = QJsonDocument::fromJson(data, &parsing);
            if (parsing.error != QJsonParseError::NoError || !document.isObject())
            {
                completion({}, QStringLiteral("Invalid personal DVMDB response."));
                return;
            }
            completion(document.object(), QString());
        });
    });
}

void PersonalWorkingCopyClient::list(const PersonalRepositoryTarget &target,
                                     ListCompletion completion)
{
    request(target, "GET", QStringLiteral("/working-copies"), {},
            [completion = std::move(completion)](const QJsonObject &body,
                                                 const QString &error)
    {
        if (!error.isEmpty()) { completion({}, error); return; }
        if (!body.value(QStringLiteral("workingCopies")).isArray())
        {
            completion({}, QStringLiteral("Invalid draft-list response."));
            return;
        }
        QList<PersonalWorkingCopyInfo> result;
        for (const QJsonValue &value : body.value(QStringLiteral("workingCopies")).toArray())
        {
            PersonalWorkingCopyInfo draft;
            if (!value.isObject() || !parseDraft(value.toObject(), draft))
            {
                completion({}, QStringLiteral("Invalid draft metadata in list."));
                return;
            }
            result.append(draft);
        }
        completion(result, QString());
    });
}

void PersonalWorkingCopyClient::create(const PersonalRepositoryTarget &target,
                                       const QString &project_id, const QString &branch,
                                       const QString &base_revision, DraftCompletion completion)
{
    if (!DvmdbIdentity::isUuidV7(project_id) || branch.trimmed().isEmpty()
        || (!base_revision.isEmpty() && !DvmdbIdentity::isUuidV7(base_revision)))
    {
        completion({}, QStringLiteral("Invalid project, branch or checkout revision UUIDv7."));
        return;
    }
    request(target, "POST", QStringLiteral("/working-copies"),
            QJsonObject{{QStringLiteral("projectId"), project_id},
                        {QStringLiteral("branch"), branch},
                        {QStringLiteral("baseRevision"), base_revision.isEmpty()
                            ? QJsonValue(QJsonValue::Null) : QJsonValue(base_revision)},
                        {QStringLiteral("sessionId"), newSessionUuidV7()}},
            [completion = std::move(completion)](const QJsonObject &body,
                                                 const QString &error)
    {
        if (!error.isEmpty()) { completion({}, error); return; }
        PersonalWorkingCopyInfo draft;
        if (!parseDraft(body, draft))
        {
            completion({}, QStringLiteral("Invalid created-draft response."));
            return;
        }
        completion(draft, QString());
    });
}

void PersonalWorkingCopyClient::reopen(const PersonalRepositoryTarget &target,
                                       const QString &draft_id, DraftCompletion completion)
{
    if (!DvmdbIdentity::isUuidV7(draft_id))
    {
        completion({}, QStringLiteral("Invalid draft UUIDv7."));
        return;
    }
    request(target, "GET", QStringLiteral("/working-copies/") + draft_id, {},
            [completion = std::move(completion)](const QJsonObject &body,
                                                 const QString &error)
    {
        if (!error.isEmpty()) { completion({}, error); return; }
        PersonalWorkingCopyInfo draft;
        if (!parseDraft(body, draft))
        {
            completion({}, QStringLiteral("Invalid reopened-draft response."));
            return;
        }
        completion(draft, QString());
    });
}

void PersonalWorkingCopyClient::checkoutCommittedBranch(
    const PersonalRepositoryTarget &target, const QString &source_endpoint,
    const QString &source_repository_id, const QString &project_id,
    const QString &project_name, const QString &branch_name,
    const QString &expected_head, CheckoutCompletion completion)
{
    if (target.kind != PersonalRepositoryTarget::Kind::Http ||
        !DvmdbIdentity::isUuidV7(source_repository_id) ||
        !DvmdbIdentity::isUuidV7(project_id) ||
        !DvmdbIdentity::isUuidV7(expected_head) ||
        project_name.trimmed().isEmpty() || branch_name.trimmed().isEmpty() ||
        source_repository_id == target.repository_id)
    {
        completion(QStringLiteral("Committed checkout requires two distinct HTTP DVMDB "
                                  "repositories and valid project, branch and HEAD identities."));
        return;
    }
    const QUrl sourceUrl(source_endpoint);
    if (!sourceUrl.isValid() ||
        (sourceUrl.scheme() != QStringLiteral("http") &&
         sourceUrl.scheme() != QStringLiteral("https")) ||
        sourceUrl.host().isEmpty() || !sourceUrl.userInfo().isEmpty() ||
        sourceUrl.hasQuery() || sourceUrl.hasFragment())
    {
        completion(QStringLiteral("Source REST endpoint is not valid for checkout."));
        return;
    }
    const QString sourceRoot = endpointRoot(source_endpoint);
    QNetworkRequest identify(QUrl(sourceRoot + QStringLiteral("/repository")));
    identify.setTransferTimeout(10000);
    QNetworkReply *sourceIdentity = this->manager.get(identify);
    connect(sourceIdentity, &QNetworkReply::finished, this,
        [this, sourceIdentity, sourceRoot, source_repository_id, target,
         project_id, project_name, branch_name, expected_head,
         completion = std::move(completion)]() mutable
    {
        const QByteArray identityBytes = sourceIdentity->readAll();
        const bool validIdentity = sourceIdentity->error() == QNetworkReply::NoError &&
            sourceIdentity->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        sourceIdentity->deleteLater();
        if (!validIdentity || QJsonDocument::fromJson(identityBytes).object()
                .value(QStringLiteral("repositoryId")).toString() != source_repository_id)
        {
            completion(QStringLiteral("Source repository UUID changed or is unavailable."));
            return;
        }
        // Check the source's HEAD before transferring. Verify the imported
        // HEAD again afterward: a concurrent source commit is never silently
        // treated as the revision selected in the Projects graph.
        const QString branchPath = QStringLiteral("/projects/") + project_id +
            QStringLiteral("/branches/") +
            QString::fromLatin1(QUrl::toPercentEncoding(branch_name));
        QNetworkRequest headRequest(QUrl(sourceRoot + branchPath + QStringLiteral("/head")));
        headRequest.setTransferTimeout(10000);
        QNetworkReply *headReply = this->manager.get(headRequest);
        connect(headReply, &QNetworkReply::finished, this,
            [this, headReply, sourceRoot, branchPath, target, project_id,
             project_name, expected_head, completion = std::move(completion)]() mutable
        {
            const QByteArray headBytes = headReply->readAll();
            const bool headOk = headReply->error() == QNetworkReply::NoError &&
                headReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
            headReply->deleteLater();
            if (!headOk || QJsonDocument::fromJson(headBytes).object()
                    .value(QStringLiteral("id")).toString() != expected_head)
            {
                completion(QStringLiteral("Source branch HEAD changed. Refresh Projects before checkout."));
                return;
            }
            request(target, "POST", QStringLiteral("/projects/clone-target"),
                QJsonObject{{QStringLiteral("projectId"), project_id},
                            {QStringLiteral("name"), project_name}},
                [this, sourceRoot, branchPath, target, expected_head,
                 completion = std::move(completion)](const QJsonObject &, const QString &error) mutable
            {
                if (!error.isEmpty()) { completion(QStringLiteral("Private project provisioning failed: ") + error); return; }
                QNetworkRequest exportRequest(QUrl(sourceRoot + branchPath + QStringLiteral("/bundle")));
                exportRequest.setTransferTimeout(60000);
                QNetworkReply *download = this->manager.get(exportRequest);
                constexpr qint64 maxCheckoutBytes = 64 * 1024 * 1024;
                connect(download, &QNetworkReply::downloadProgress, download,
                    [download](qint64 received, qint64 total)
                {
                    if (received > maxCheckoutBytes || total > maxCheckoutBytes) download->abort();
                });
                connect(download, &QNetworkReply::finished, this,
                    [this, download, branchPath, target, expected_head,
                     completion = std::move(completion)]() mutable
                {
                    const QByteArray bundle = download->readAll();
                    const bool downloaded = download->error() == QNetworkReply::NoError &&
                        download->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
                        download->header(QNetworkRequest::ContentTypeHeader).toString()
                            .startsWith(QStringLiteral("application/vnd.aowis.revision-bundle-v2")) &&
                        !bundle.isEmpty() && bundle.size() <= maxCheckoutBytes;
                    const QString fetchError = download->errorString();
                    download->deleteLater();
                    if (!downloaded)
                    {
                        completion(QStringLiteral("Revision bundle download failed or exceeded 64 MiB: ") + fetchError);
                        return;
                    }
                    // AOWREV21/23 full bundles carry the immutable tip UUID
                    // at offset 40. Never import a later HEAD than the one
                    // the user selected, even if source advances mid-export.
                    const QString exportedHead = bundle.size() >= 96 &&
                        (bundle.startsWith("AOWREV21") || bundle.startsWith("AOWREV23"))
                        ? QUuid::fromRfc4122(bundle.mid(40, 16)).toString(QUuid::WithoutBraces)
                        : QString();
                    if (exportedHead != expected_head)
                    {
                        completion(QStringLiteral("Exported branch HEAD differs from the selected revision. "
                                                  "Refresh Projects before checkout."));
                        return;
                    }
                    QNetworkRequest upload(QUrl(endpointRoot(target.endpoint) + branchPath +
                                                QStringLiteral("/bundle")));
                    upload.setTransferTimeout(60000);
                    upload.setHeader(QNetworkRequest::ContentTypeHeader,
                                     QStringLiteral("application/vnd.aowis.revision-bundle-v2"));
                    QNetworkReply *imported = this->manager.post(upload, bundle);
                    connect(imported, &QNetworkReply::finished, this,
                        [imported, expected_head, completion = std::move(completion)]() mutable
                    {
                        const QByteArray response = imported->readAll();
                        const int status = imported->attribute(
                            QNetworkRequest::HttpStatusCodeAttribute).toInt();
                        const bool success = imported->error() == QNetworkReply::NoError &&
                            (status == 200 || status == 201);
                        const QString failure = responseError(imported, response);
                        imported->deleteLater();
                        if (!success) { completion(QStringLiteral("Private bundle import failed: ") + failure); return; }
                        if (QJsonDocument::fromJson(response).object()
                                .value(QStringLiteral("head")).toString() != expected_head)
                        {
                            completion(QStringLiteral("Imported HEAD differs from selected revision. "
                                                      "Refresh Projects before creating a draft."));
                            return;
                        }
                        completion(QString());
                    });
                });
            });
        });
    });
}
