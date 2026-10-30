#include "projects/personal_working_copy_client.h"
#include "projects/dvmdb_identity.h"
#include "projects/personal_edit_queue.h"

#include <aowis/model/edit/model_edit_transaction.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTemporaryDir>
#include <QUuid>

#include <cstdio>
#include <functional>

namespace
{
const QString repository_id = QStringLiteral("019a47b1-2040-7af9-8d1c-2670badb6371");
const QString different_repository = QStringLiteral("019a47b1-2040-7af9-8d1c-2670badb6372");
const QString project_id = QStringLiteral("019a47b1-2041-7af9-8d1c-2670badb6371");
const QString branch_id = QStringLiteral("019a47b1-2042-7af9-8d1c-2670badb6371");
const QString draft_id = QStringLiteral("019a47b1-2043-7af9-8d1c-2670badb6371");
const QString head_id = QStringLiteral("019a47b1-2044-7af9-8d1c-2670badb6371");
const QString lease_token = QStringLiteral("019a47b1-2045-7af9-8d1c-2670badb6371");
const QString entity_id = QStringLiteral("019a47b1-2046-7af9-8d1c-2670badb6371");

bool check(bool value, const char *message)
{
    if (!value) std::fprintf(stderr, "working_copy_client: %s\n", message);
    return value;
}

bool until(QCoreApplication &application, const std::function<bool()> &ready)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000)
    {
        application.processEvents();
        if (ready()) return true;
        QThread::msleep(5);
    }
    return ready();
}

class Stub final : public QObject
{
public:
    QTcpServer server;
    QString identity = repository_id;
    QString role = QStringLiteral("private");
    bool is_source = false;
    QString source_head = head_id;
    QString bundle_head = head_id;
    int provision_count = 0;
    int bundle_export_count = 0;
    int bundle_import_count = 0;
    QJsonObject last_provision;
    int listing_count = 0;
    int creating_count = 0;
    int open_count = 0;
    QJsonObject last_create;
    QJsonObject last_save;
    QJsonArray saved_changes;
    QJsonArray saved_events;
    int event_save_count = 0;
    bool drop_next_event_acknowledgement = false;
    bool reject_next_event_save = false;
    bool corrupt_event_reopen = false;
    QString lease_session;
    int save_count = 0;
    int release_count = 0;
    int generation = 0;
    bool malformed_overlay = false;
    bool drop_next_save_acknowledgement = false;

    QByteArray bundlePayload() const
    {
        QByteArray result(96, '\0');
        result.replace(0, 8, "AOWREV21");
        result.replace(40, 16, QUuid(this->bundle_head).toRfc4122());
        return result;
    }

    bool listen()
    {
        connect(&this->server, &QTcpServer::newConnection, this, [this]
        {
            while (this->server.hasPendingConnections())
            {
                QTcpSocket *socket = this->server.nextPendingConnection();
                QByteArray *buffer = new QByteArray();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]
                {
                    buffer->append(socket->readAll());
                    const int headers_end = buffer->indexOf("\r\n\r\n");
                    if (headers_end < 0) return;
                    const QByteArray header = buffer->left(headers_end);
                    int content_length = 0;
                    for (const QByteArray &line : header.split('\n'))
                    {
                        const QByteArray trimmed = line.trimmed();
                        if (trimmed.toLower().startsWith("content-length:"))
                            content_length = trimmed.mid(15).trimmed().toInt();
                    }
                    if (buffer->size() < headers_end + 4 + content_length) return;
                    const QByteArray request_line = header.left(header.indexOf("\r\n"));
                    const QJsonObject request_body = QJsonDocument::fromJson(
                        buffer->mid(headers_end + 4, content_length)).object();
                    QJsonObject document;
                    int status = 200;
                    bool binary_response = false;
                    if (request_line.startsWith("GET /dvmdb/v1/repository "))
                    {
                        document.insert(QStringLiteral("repositoryId"), this->identity);
                    }
                    else if (request_line.startsWith("GET /dvmdb/v1/projects/") &&
                             request_line.contains("/head ") && this->is_source)
                    {
                        document.insert(QStringLiteral("id"), this->source_head);
                    }
                    else if (request_line.startsWith("GET /dvmdb/v1/projects/") &&
                             request_line.contains("/bundle ") && this->is_source)
                    {
                        ++this->bundle_export_count;
                        binary_response = true;
                    }
                    else if (request_line.startsWith("POST /dvmdb/v1/projects/clone-target ") &&
                             !this->is_source)
                    {
                        ++this->provision_count;
                        this->last_provision = request_body;
                        document.insert(QStringLiteral("id"), project_id);
                    }
                    else if (request_line.startsWith("POST /dvmdb/v1/projects/") &&
                             request_line.contains("/bundle ") && !this->is_source)
                    {
                        if (buffer->mid(headers_end + 4, content_length) == bundlePayload())
                        {
                            ++this->bundle_import_count;
                            status = 201;
                            document.insert(QStringLiteral("head"), head_id);
                        }
                        else status = 400;
                    }
                    else if (request_line.startsWith("GET /dvmdb/v1/repository/tracking "))
                    {
                        document.insert(QStringLiteral("repositoryId"), this->identity);
                        document.insert(QStringLiteral("role"), this->role);
                    }
                    else if (request_line.startsWith("GET /dvmdb/v1/working-copies "))
                    {
                        ++this->listing_count;
                        document.insert(QStringLiteral("workingCopies"), QJsonArray{
                            this->draftRecord(QString())});
                    }
                    else if (request_line.startsWith("POST /dvmdb/v1/working-copies "))
                    {
                        ++this->creating_count;
                        this->last_create = request_body;
                        document = draftRecord(request_body.value(QStringLiteral("sessionId")).toString());
                        document.insert(QStringLiteral("baseRevision"), request_body.value(QStringLiteral("baseRevision")));
                        status = 201;
                    }
                    else if (request_line.startsWith("POST /dvmdb/v1/working-copies/") &&
                             request_line.contains("/lease "))
                    {
                        this->lease_session = request_body.value(QStringLiteral("sessionId")).toString();
                        document = QJsonObject{{QStringLiteral("token"), lease_token},
                            {QStringLiteral("generation"), QString::number(this->generation)},
                            {QStringLiteral("expiresAtMs"), QStringLiteral("1800000000000")}};
                    }
                    else if (request_line.startsWith("POST /dvmdb/v1/working-copies/") &&
                             request_line.contains("/save-events "))
                    {
                        ++this->event_save_count;
                        const QString expected = request_body.value(QStringLiteral("expectedGeneration")).toString();
                        if (this->reject_next_event_save || expected != QString::number(this->generation) ||
                            request_body.value(QStringLiteral("fencingToken")).toString() != lease_token)
                        {
                            this->reject_next_event_save = false;
                            status = 409;
                            document.insert(QStringLiteral("error"), QStringLiteral("stale event writer"));
                        }
                        else
                        {
                            bool duplicate = false;
                            for (const QJsonValue &encoded : request_body.value(QStringLiteral("events")).toArray())
                            {
                                const std::optional<aowis::model::ModelEditTransaction> decoded =
                                    aowis::model::ModelEditTransactionCodec::decode(
                                        QByteArray::fromBase64(encoded.toString().toLatin1()));
                                for (const QJsonValue &existing : this->saved_events)
                                    if (existing.toObject().value(QStringLiteral("id")).toString() ==
                                        decoded->transactionId.toString(QUuid::WithoutBraces))
                                        duplicate = true;
                            }
                            if (duplicate)
                            {
                                status = 409;
                                document.insert(QStringLiteral("error"), QStringLiteral("event already persisted"));
                            }
                            else
                            {
                            this->saved_changes = request_body.value(QStringLiteral("changes")).toArray();
                            QJsonArray accepted;
                            for (const QJsonValue &value : request_body.value(QStringLiteral("events")).toArray())
                            {
                                const QByteArray bytes = QByteArray::fromBase64(value.toString().toLatin1());
                                const std::optional<aowis::model::ModelEditTransaction> event =
                                    aowis::model::ModelEditTransactionCodec::decode(bytes);
                                const QString id = event->transactionId.toString(QUuid::WithoutBraces);
                                accepted.append(id);
                                this->saved_events.append(QJsonObject{
                                    {QStringLiteral("id"), id},
                                    {QStringLiteral("payloadBase64"), value.toString()}});
                            }
                            ++this->generation;
                            document = QJsonObject{
                                {QStringLiteral("id"), draft_id},
                                {QStringLiteral("generation"), QString::number(this->generation)},
                                {QStringLiteral("eventCount"), QString::number(this->saved_events.size())},
                                {QStringLiteral("acceptedEventIds"), accepted}};
                            }
                        }
                    }
                    else if (request_line.startsWith("POST /dvmdb/v1/working-copies/") &&
                             request_line.contains("/save "))
                    {
                        this->last_save = request_body;
                        if (request_body.value(QStringLiteral("sessionId")).toString() != this->lease_session
                            || request_body.value(QStringLiteral("fencingToken")).toString() != lease_token
                            || request_body.value(QStringLiteral("expectedGeneration")).toString()
                                != QString::number(this->generation))
                        {
                            status = 409;
                            document.insert(QStringLiteral("error"), QStringLiteral("stale writer"));
                        }
                        else
                        {
                            ++this->save_count;
                            ++this->generation;
                            this->saved_changes = request_body.value(QStringLiteral("changes")).toArray();
                            document = draftRecord(this->lease_session);
                        }
                    }
                    else if (request_line.startsWith("POST /dvmdb/v1/working-copies/") &&
                             request_line.contains("/release "))
                    {
                        if (request_body.value(QStringLiteral("fencingToken")).toString() != lease_token
                            || request_body.value(QStringLiteral("sessionId")).toString() != this->lease_session)
                            status = 409;
                        else
                        {
                            ++this->release_count;
                            this->lease_session.clear();
                            document.insert(QStringLiteral("released"), true);
                        }
                    }
                    else if (request_line.startsWith("GET /dvmdb/v1/working-copies/"))
                    {
                        ++this->open_count;
                        document = draftRecord(QString());
                        document.insert(QStringLiteral("changes"), this->saved_changes);
                        document.insert(QStringLiteral("events"), this->saved_events);
                        if (this->corrupt_event_reopen && !this->saved_events.isEmpty())
                        {
                            QJsonArray tampered = this->saved_events;
                            QJsonObject modified = tampered.last().toObject();
                            modified.insert(QStringLiteral("payloadBase64"), QStringLiteral("not-base64!!"));
                            tampered.replace(tampered.size() - 1, modified);
                            document.insert(QStringLiteral("events"), tampered);
                        }
                        if (this->malformed_overlay)
                            document.insert(QStringLiteral("changes"), QJsonArray{
                                QJsonObject{{QStringLiteral("uuid"), entity_id},
                                    {QStringLiteral("kind"), 4},
                                    {QStringLiteral("references"), QJsonArray{}},
                                    {QStringLiteral("payloadBase64"), QStringLiteral("@@@")}}});
                    }
                    else
                    {
                        status = 404;
                        document.insert(QStringLiteral("error"), QStringLiteral("Not found"));
                    }
                    if (request_line.startsWith("POST /dvmdb/v1/working-copies/") &&
                        request_line.contains("/save-events ") &&
                        this->drop_next_event_acknowledgement && status == 200)
                    {
                        this->drop_next_event_acknowledgement = false;
                        socket->disconnectFromHost();
                        return;
                    }
                    if (request_line.startsWith("POST /dvmdb/v1/working-copies/") &&
                        request_line.contains("/save ") &&
                        this->drop_next_save_acknowledgement && status == 200)
                    {
                        this->drop_next_save_acknowledgement = false;
                        socket->disconnectFromHost();
                        return;
                    }
                    const QByteArray payload = binary_response ? bundlePayload() :
                        QJsonDocument(document).toJson(QJsonDocument::Compact);
                    const QByteArray contentType = binary_response ?
                        QByteArray("application/vnd.aowis.revision-bundle-v2") : QByteArray("application/json");
                    socket->write("HTTP/1.1 " + QByteArray::number(status) +
                                  " OK\r\nContent-Type: " + contentType +
                                  "\r\nConnection: close\r\nContent-Length: "
                                  + QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, [socket, buffer]
                {
                    delete buffer;
                    socket->deleteLater();
                });
            }
        });
        return this->server.listen(QHostAddress::LocalHost);
    }

    QJsonObject draftRecord(const QString &session) const
    {
        return QJsonObject{
            {QStringLiteral("id"), draft_id},
            {QStringLiteral("projectId"), project_id},
            {QStringLiteral("branchId"), branch_id},
            {QStringLiteral("baseRevision"), QJsonValue(QJsonValue::Null)},
            {QStringLiteral("sessionId"), session.isEmpty() ? head_id : session},
            {QStringLiteral("generation"), QString::number(this->generation)},
            {QStringLiteral("dirty"), this->generation != 0},
            {QStringLiteral("eventCount"), QString::number(this->saved_events.size())}};
    }

    PersonalRepositoryTarget target() const
    {
        PersonalRepositoryTarget result;
        result.kind = PersonalRepositoryTarget::Kind::Http;
        result.endpoint = QStringLiteral("http://127.0.0.1:%1").arg(this->server.serverPort());
        result.repository_id = repository_id;
        return result;
    }
};
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    Stub stub;
    if (!check(stub.listen(), "could not start private DVMDB fixture")) return 1;
    PersonalWorkingCopyClient client;
    if (!check(DvmdbIdentity::isUuidV7(PersonalWorkingCopyClient::newSessionUuidV7()),
               "session generator did not produce UUIDv7")) return 1;
    bool done = false;
    QString error;
    QList<PersonalWorkingCopyInfo> listed;
    client.list(stub.target(), [&](const QList<PersonalWorkingCopyInfo> &drafts, const QString &failure)
    {
        listed = drafts;
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && error.isEmpty()
               && listed.size() == 1 && listed.first().id == draft_id,
               "list could not read personal working-copy metadata")) return 1;

    done = false;
    PersonalWorkingCopyInfo created;
    client.create(stub.target(), project_id, QStringLiteral("main"), {},
        [&](const PersonalWorkingCopyInfo &draft, const QString &failure)
    {
        created = draft;
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && error.isEmpty()
               && created.id == draft_id && created.base_revision.isEmpty()
               && stub.last_create.value(QStringLiteral("baseRevision")).isNull()
               && DvmdbIdentity::isUuidV7(stub.last_create.value(QStringLiteral("sessionId")).toString())
               && stub.last_create.value(QStringLiteral("branch")).toString() == QStringLiteral("main"),
               "unborn draft create did not pin exact inputs")) return 1;
    done = false;
    client.reopen(stub.target(), draft_id,
        [&](const PersonalWorkingCopyInfo &draft, const QString &failure)
    {
        created = draft;
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && error.isEmpty()
               && created.id == draft_id && stub.open_count == 1,
               "reopen did not load the saved draft by UUID")) return 1;

    done = false;
    client.create(stub.target(), project_id, QStringLiteral("main"), head_id,
        [&](const PersonalWorkingCopyInfo &draft, const QString &failure)
    {
        created = draft;
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && error.isEmpty()
               && created.base_revision == head_id
               && stub.last_create.value(QStringLiteral("baseRevision")).toString() == head_id,
               "committed draft creation did not pin its source revision")) return 1;

    done = false;
    PersonalRepositoryTarget absent;
    client.create(absent, project_id, QStringLiteral("main"), head_id,
        [&](const PersonalWorkingCopyInfo &, const QString &failure)
    {
        error = failure;
        done = true;
    });
    if (!check(done && !error.isEmpty() && stub.creating_count == 2,
               "unconfigured personal repository silently used a source profile")) return 1;

    // New GUI checkout: validate remote identity/HEAD, provision the same
    // logical project UUID, and transfer a full revision bundle privately.
    Stub source;
    source.identity = different_repository;
    source.role = QStringLiteral("shared");
    source.is_source = true;
    if (!check(source.listen(), "could not start shared source DVMDB fixture")) return 1;
    done = false;
    client.checkoutCommittedBranch(stub.target(), source.target().endpoint,
        different_repository, project_id, QStringLiteral("Network test"),
        QStringLiteral("main"), head_id,
        [&](const QString &failure) { error = failure; done = true; });
    if (!check(until(application, [&] { return done; }) && error.isEmpty() &&
               source.bundle_export_count == 1 && stub.provision_count == 1 &&
               stub.bundle_import_count == 1 &&
               stub.last_provision.value(QStringLiteral("projectId")).toString() == project_id &&
               stub.last_provision.value(QStringLiteral("name")).toString() == QStringLiteral("Network test"),
               "checkout did not transfer the expected project, branch and HEAD")) return 1;
    done = false;
    client.checkoutCommittedBranch(stub.target(), source.target().endpoint,
        repository_id, project_id, QStringLiteral("Network test"),
        QStringLiteral("main"), head_id,
        [&](const QString &failure) { error = failure; done = true; });
    if (!check(until(application, [&] { return done; }) && !error.isEmpty() &&
               stub.provision_count == 1 && stub.bundle_import_count == 1,
               "checkout accepted a mismatched source repository identity")) return 1;
    source.source_head = draft_id;
    done = false;
    client.checkoutCommittedBranch(stub.target(), source.target().endpoint,
        different_repository, project_id, QStringLiteral("Network test"),
        QStringLiteral("main"), head_id,
        [&](const QString &failure) { error = failure; done = true; });
    if (!check(until(application, [&] { return done; }) && !error.isEmpty() &&
               stub.provision_count == 1 && stub.bundle_import_count == 1,
               "checkout accepted a changed source HEAD")) return 1;

    // A source commit during bundle export must not silently import a
    // different immutable HEAD into the private repository.
    source.source_head = head_id;
    source.bundle_head = draft_id;
    done = false;
    client.checkoutCommittedBranch(stub.target(), source.target().endpoint,
        different_repository, project_id, QStringLiteral("Network test"),
        QStringLiteral("main"), head_id,
        [&](const QString &failure) { error = failure; done = true; });
    if (!check(until(application, [&] { return done; }) && !error.isEmpty() &&
               stub.bundle_import_count == 1,
               "checkout imported a bundle with the wrong immutable HEAD")) return 1;

    // A model-record Save must use the exact fenced lease and generation.
    PersonalWorkingCopyLease lease;
    done = false;
    client.acquireLease(stub.target(), draft_id, head_id, 90,
        [&](const PersonalWorkingCopyLease &result, const QString &failure)
    {
        lease = result;
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && error.isEmpty()
               && lease.token == lease_token && lease.generation == QStringLiteral("0"),
               "lease acquisition did not expose the fencing token and generation")) return 1;

    PersonalWorkingCopyChange update;
    update.uuid = entity_id;
    update.kind = 42;
    update.references.append(project_id);
    update.payload = QByteArray("AOWIS raw payload \0 bytes", 25);
    done = false;
    client.saveChanges(stub.target(), draft_id, head_id, lease.token, lease.generation,
                       {update}, [&](const PersonalWorkingCopyInfo &draft, const QString &failure)
    {
        created = draft;
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && error.isEmpty()
               && created.generation == QStringLiteral("1") && created.dirty
               && stub.save_count == 1 && stub.saved_changes.size() == 1
               && stub.saved_changes.first().toObject().value(QStringLiteral("kind")).toInt() == 42
               && stub.saved_changes.first().toObject().value(QStringLiteral("payloadBase64")).toString()
                  == QString::fromLatin1(update.payload.toBase64()),
               "draft Save did not preserve the typed record and exact bytes")) return 1;

    done = false;
    client.reopen(stub.target(), draft_id,
        [&](const PersonalWorkingCopyInfo &draft, const QString &failure)
    {
        created = draft;
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && error.isEmpty()
               && created.changes.size() == 1 && created.changes.first().uuid == entity_id
               && created.changes.first().payload == update.payload
               && created.changes.first().references == update.references,
               "reopened draft did not decode its saved candidate overlay")) return 1;

    // Simulate an acknowledged durable write whose HTTP response is lost.
    // The client must reopen once and verify exact bytes plus generation;
    // it must NEVER issue a duplicate Save request.
    stub.drop_next_save_acknowledgement = true;
    update.payload.append(" after interruption");
    bool recovered = false;
    done = false;
    client.saveChangesRecovering(stub.target(), draft_id, head_id, lease.token,
        QStringLiteral("1"), {update},
        [&](const PersonalWorkingCopyInfo &draft, bool from_recovery, const QString &failure)
    {
        created = draft;
        recovered = from_recovery;
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && error.isEmpty()
               && recovered && created.generation == QStringLiteral("2")
               && created.changes.size() == 1
               && created.changes.first().payload == update.payload
               && stub.save_count == 2 && stub.open_count >= 3,
               "lost Save acknowledgement was not recovered exactly once")) return 1;

    done = false;
    client.saveChanges(stub.target(), draft_id, head_id, lease.token, lease.generation,
                       {update}, [&](const PersonalWorkingCopyInfo &, const QString &failure)
    {
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && !error.isEmpty()
               && stub.save_count == 2,
               "stale Save overwrote a newer generation")) return 1;
    done = false;
    client.saveChanges(stub.target(), draft_id, head_id, draft_id, QStringLiteral("1"),
                       {update}, [&](const PersonalWorkingCopyInfo &, const QString &failure)
    {
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && !error.isEmpty()
               && stub.save_count == 2,
               "stale fencing token was accepted")) return 1;

    // A stale request that happens to propose the same data must not be
    // mistaken for recovered success: generation must be exactly expected+1.
    done = false;
    recovered = true;
    client.saveChangesRecovering(stub.target(), draft_id, head_id, lease.token,
        QStringLiteral("0"), {update},
        [&](const PersonalWorkingCopyInfo &, bool from_recovery, const QString &failure)
    {
        recovered = from_recovery;
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && !error.isEmpty()
               && !recovered && stub.save_count == 2,
               "stale Save was incorrectly accepted as recovered")) return 1;

    stub.malformed_overlay = true;
    done = false;
    client.reopen(stub.target(), draft_id,
        [&](const PersonalWorkingCopyInfo &, const QString &failure)
    {
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && !error.isEmpty(),
               "malformed candidate model record was accepted on reopening")) return 1;
    stub.malformed_overlay = false;

    done = false;
    update.references = {QStringLiteral("not-a-uuid")};
    client.saveChanges(stub.target(), draft_id, head_id, lease.token, QStringLiteral("1"),
                       {update}, [&](const PersonalWorkingCopyInfo &, const QString &failure)
    {
        error = failure;
        done = true;
    });
    if (!check(done && !error.isEmpty() && stub.save_count == 2,
               "invalid model reference reached HTTP Save")) return 1;

    // Recovery cannot reinterpret an invalid command as an older completed
    // Save, even when the persisted generation and payload happen to match.
    const int reads_before_invalid = stub.open_count;
    done = false;
    recovered = true;
    client.saveChangesRecovering(stub.target(), draft_id, head_id, lease.token,
        QStringLiteral("1"), {update},
        [&](const PersonalWorkingCopyInfo &, bool from_recovery, const QString &failure)
    {
        recovered = from_recovery;
        error = failure;
        done = true;
    });
    if (!check(done && !error.isEmpty() && !recovered &&
               stub.open_count == reads_before_invalid && stub.save_count == 2,
               "invalid Save incorrectly triggered recovery or another write")) return 1;

    // Atomic MODEL event journal: small ACK, exact transaction identity,
    // dropped ACK recovery, and local queue removal only after verification.
    aowis::model::ModelEditTransaction event;
    event.transactionId = QUuid(PersonalWorkingCopyClient::newSessionUuidV7());
    event.operation = QStringLiteral("hydraulic.pipe.diameter");
    aowis::model::ModelEntityEdit edit;
    edit.entityId = QUuid(entity_id);
    edit.entityType = QStringLiteral("HydraulicLinkPipe");
    edit.after = update.payload;
    event.edits.append(edit);
    const QByteArray encoded = aowis::model::ModelEditTransactionCodec::encode(event);
    if (!check(!encoded.isEmpty(), "could not encode MODEL edit event")) return 1;
    update.references.clear();
    done = false;
    PersonalWorkingCopyEventSaveResult journal_ack;
    recovered = true;
    client.saveEventsRecovering(stub.target(), draft_id, head_id, lease.token,
        QStringLiteral("2"), {update}, {encoded},
        [&](const PersonalWorkingCopyEventSaveResult &result, bool from_recovery, const QString &failure)
    {
        journal_ack = result;
        recovered = from_recovery;
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && error.isEmpty() && !recovered &&
               journal_ack.generation == QStringLiteral("3") &&
               journal_ack.accepted_ids == QStringList{event.transactionId.toString(QUuid::WithoutBraces)} &&
               stub.event_save_count == 1 && stub.saved_events.size() == 1,
               "atomic event Save did not confirm ordered journal IDs")) return 1;

    QTemporaryDir queue_root;
    if (!check(queue_root.isValid(), "could not create temporary queue root")) return 1;
    PersonalEditQueue queue;
    QString queue_error;
    if (!check(queue.open(queue_root.path(), QUuid(repository_id), QUuid(draft_id), &queue_error),
               "could not open durable editing queue")) return 1;
    aowis::model::ModelEditTransaction queued_event = event;
    queued_event.transactionId = QUuid(PersonalWorkingCopyClient::newSessionUuidV7());
    queued_event.edits.first().after = QByteArray("post reconnect bytes");
    update.payload = *queued_event.edits.first().after;
    const QByteArray queued_bytes = aowis::model::ModelEditTransactionCodec::encode(queued_event);
    if (!check(queue.append(queued_bytes, &queue_error) && queue.pending().size() == 1,
               "could not persist queued editing event")) return 1;
    done = false;
    client.flushOldestQueuedEvent(stub.target(), queue, head_id, head_id, lease.token,
        QStringLiteral("3"), {update},
        [&](const QString &, bool, const QString &failure)
    {
        error = failure;
        done = true;
    });
    if (!check(done && !error.isEmpty() && queue.pending().size() == 1 &&
               stub.event_save_count == 1,
               "wrong draft target was not rejected before queue replay")) return 1;
    stub.drop_next_event_acknowledgement = true;
    done = false;
    QString next_generation;
    recovered = false;
    client.flushOldestQueuedEvent(stub.target(), queue, draft_id, head_id, lease.token,
        QStringLiteral("3"), {update},
        [&](const QString &next, bool from_recovery, const QString &failure)
    {
        next_generation = next;
        recovered = from_recovery;
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && error.isEmpty() && recovered &&
               next_generation == QStringLiteral("4") && queue.pending().isEmpty() &&
               stub.event_save_count == 2 && stub.saved_events.size() == 2,
               "dropped atomic Save acknowledgement did not recover without duplicate writes")) return 1;

    // A crash after server persistence but before deleting the local file
    // leaves the exact last event queued. Reopen confirms it without replay.
    if (!check(queue.append(queued_bytes) && queue.pending().size() == 1,
               "could not simulate a pre-acknowledgement GUI crash")) return 1;
    done = false;
    recovered = false;
    client.flushOldestQueuedEvent(stub.target(), queue, draft_id, head_id, lease.token,
        QStringLiteral("4"), {update},
        [&](const QString &next, bool from_recovery, const QString &failure)
    {
        next_generation = next;
        recovered = from_recovery;
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && error.isEmpty() && recovered &&
               next_generation == QStringLiteral("4") && queue.pending().isEmpty() &&
               stub.saved_events.size() == 2 && stub.generation == 4,
               "restart replay did not confirm existing journal without duplicate write")) return 1;

    // Refused server writes cannot acknowledge/remove queued local work.
    queued_event.transactionId = QUuid(PersonalWorkingCopyClient::newSessionUuidV7());
    queued_event.edits.first().after = QByteArray("still pending");
    update.payload = *queued_event.edits.first().after;
    if (!check(queue.append(aowis::model::ModelEditTransactionCodec::encode(queued_event)),
               "failed to enqueue rejection test")) return 1;
    stub.reject_next_event_save = true;
    done = false;
    client.flushOldestQueuedEvent(stub.target(), queue, draft_id, head_id, lease.token,
        QStringLiteral("4"), {update},
        [&](const QString &, bool, const QString &failure)
    {
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && !error.isEmpty() &&
               queue.pending().size() == 1 && stub.saved_events.size() == 2,
               "rejected server event incorrectly erased the local queue")) return 1;
    // A candidate payload unrelated to the journal must be rejected locally.
    update.payload = QByteArray("unrelated candidate");
    const int saves_before_mismatch = stub.event_save_count;
    done = false;
    client.flushOldestQueuedEvent(stub.target(), queue, draft_id, head_id, lease.token,
        QStringLiteral("4"), {update},
        [&](const QString &, bool, const QString &failure)
    {
        error = failure;
        done = true;
    });
    if (!check(done && !error.isEmpty() && queue.pending().size() == 1 &&
               stub.event_save_count == saves_before_mismatch,
               "mismatched event/model records were transmitted")) return 1;
    queue.close();

    done = false;
    client.releaseLease(stub.target(), draft_id, head_id, lease.token,
        [&](const QString &failure) { error = failure; done = true; });
    if (!check(until(application, [&] { return done; }) && error.isEmpty()
               && stub.release_count == 1, "writer lease was not released")) return 1;

    stub.identity = different_repository;
    done = false;
    client.create(stub.target(), project_id, QStringLiteral("main"), head_id,
        [&](const PersonalWorkingCopyInfo &, const QString &failure)
    {
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && !error.isEmpty()
               && stub.creating_count == 2,
               "identity change was not rejected before a draft write")) return 1;
    stub.identity = repository_id;
    stub.role = QStringLiteral("shared");
    done = false;
    client.list(stub.target(), [&](const QList<PersonalWorkingCopyInfo> &, const QString &failure)
    {
        error = failure;
        done = true;
    });
    if (!check(until(application, [&] { return done; }) && !error.isEmpty()
               && stub.listing_count == 1,
               "shared repository role was not rejected before draft access")) return 1;
    return 0;
}
