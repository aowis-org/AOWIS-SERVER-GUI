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
