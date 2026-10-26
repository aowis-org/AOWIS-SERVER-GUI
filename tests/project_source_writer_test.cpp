#include "projects/project_source_writer.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

#include <cstdio>
#include <functional>
#include <memory>

namespace
{
bool require(bool condition, const char *message)
{
    if (!condition) std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}

bool waitUntil(QCoreApplication &app, const std::function<bool()> &predicate)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < 5000)
        app.processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTcpServer server;
    if (!require(server.listen(QHostAddress::LocalHost, 0), "cannot listen on loopback")) return 1;
    QStringList requested_paths;
    QList<QJsonObject> request_bodies;
    const QString root_revision(64, QLatin1Char('a'));
    const QString branch_revision(64, QLatin1Char('b'));
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&]
    {
        while (server.hasPendingConnections())
        {
            QTcpSocket *socket = server.nextPendingConnection();
            const std::shared_ptr<QByteArray> buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket,
                [socket, buffer, &requested_paths, &request_bodies, branch_revision]
            {
                buffer->append(socket->readAll());
                const qsizetype marker = buffer->indexOf("\r\n\r\n");
                if (marker < 0) return;
                const QByteArray header = buffer->left(marker);
                qint64 length = 0;
                for (const QByteArray &line : header.split('\n'))
                    if (line.trimmed().toLower().startsWith("content-length:"))
                        length = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
                if (length <= 0 || buffer->size() < marker + 4 + length) return;
                const QByteArray request_line = header.split('\n').first().trimmed();
                const QByteArray path = request_line.split(' ').value(1);
                requested_paths.append(QString::fromUtf8(path));
                const QJsonDocument json = QJsonDocument::fromJson(buffer->mid(marker + 4, length));
                request_bodies.append(json.object());
                QByteArray payload;
                int status = 201;
                if (path == "/dvmdb/v1/projects")
                    payload = R"({"id":"new-project","name":"Water Project"})";
                else if (path == "/dvmdb/v1/projects/p1/branches"
                    && json.object().value(QStringLiteral("name")) != QStringLiteral("duplicate"))
                    payload = QJsonDocument(QJsonObject{
                        {QStringLiteral("id"), branch_revision},
                        {QStringLiteral("parents"), QJsonArray{}}}).toJson(QJsonDocument::Compact);
                else
                {
                    status = 409;
                    payload = R"({"error":{"code":"conflict","message":"duplicate branch"}})";
                }
                socket->write(QByteArray("HTTP/1.1 ") + QByteArray::number(status)
                    + (status == 201 ? " Created\r\n" : " Conflict\r\n")
                    + "Content-Type: application/json\r\nConnection: close\r\nContent-Length: "
                    + QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
        }
    });

    ProjectServerProfile profile;
    profile.id = QStringLiteral("test");
    profile.connection_type = QStringLiteral("remote");
    profile.endpoint = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    profile.auth_mode = QStringLiteral("None");
    ProjectSourceWriter writer;
    bool done = false;
    QString created;
    QString failure;
    writer.createProject(profile, QStringLiteral("Water Project"), [&](const QString &id, const QString &error)
    {
        created = id;
        failure = error;
        done = true;
    });
    if (!require(writer.isBusy(), "writer must be busy during POST")) return 1;
    if (!require(waitUntil(app, [&] { return done; }), "project POST did not finish")) return 1;
    if (!require(!writer.isBusy() && failure.isEmpty() && created == QStringLiteral("new-project"),
                 "project POST did not return the persisted ID")) return 1;
    if (!require(requested_paths.size() == 1 && requested_paths.first() == QStringLiteral("/dvmdb/v1/projects")
                 && request_bodies.first().value(QStringLiteral("name")) == QStringLiteral("Water Project"),
                 "project POST did not match DVMDB contract")) return 1;

    done = false;
    writer.createBranch(profile, QStringLiteral("p1"), QStringLiteral("feature"),
                        QStringLiteral("main"), root_revision,
                        [&](const QString &id, const QString &error)
    {
        created = id;
        failure = error;
        done = true;
    });
    if (!require(waitUntil(app, [&] { return done; }), "branch POST did not finish")) return 1;
    if (!require(failure.isEmpty() && created == QStringLiteral("feature")
        && requested_paths.size() == 2
        && requested_paths.last() == QStringLiteral("/dvmdb/v1/projects/p1/branches")
        && request_bodies.last().value(QStringLiteral("name")) == QStringLiteral("feature")
        && request_bodies.last().value(QStringLiteral("fromBranch")) == QStringLiteral("main")
        && request_bodies.last().value(QStringLiteral("revision")) == root_revision,
        "branch-from-revision POST does not match DVMDB contract")) return 1;

    done = false;
    writer.createBranch(profile, QStringLiteral("p1"), QStringLiteral("duplicate"),
                        QStringLiteral("main"), {}, [&](const QString &id, const QString &error)
    {
        created = id;
        failure = error;
        done = true;
    });
    if (!require(waitUntil(app, [&] { return done; }), "conflict POST did not finish")) return 1;
    if (!require(created.isEmpty() && failure.contains(QStringLiteral("HTTP 409"))
                 && failure.contains(QStringLiteral("duplicate branch")),
        "structured branch conflict was lost")) return 1;

    profile.auth_mode = QStringLiteral("API token");
    done = false;
    writer.createProject(profile, QStringLiteral("invalid"), [&](const QString &, const QString &error)
    {
        failure = error;
        done = true;
    });
    if (!require(done && !writer.isBusy() && failure.contains(QStringLiteral("not implemented"))
                 && requested_paths.size() == 3,
        "unsupported authentication was not rejected before POST")) return 1;
    return 0;
}
