#include "projects/personal_working_copy_client.h"
#include "projects/dvmdb_identity.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
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
                    else if (request_line.startsWith("GET /dvmdb/v1/working-copies/"))
                    {
                        ++this->open_count;
                        document = draftRecord(QString());
                    }
                    else
                    {
                        status = 404;
                        document.insert(QStringLiteral("error"), QStringLiteral("Not found"));
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
            {QStringLiteral("generation"), QStringLiteral("0")},
            {QStringLiteral("dirty"), false}};
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
