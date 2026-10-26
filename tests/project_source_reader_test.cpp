#include "projects/project_source_reader.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <memory>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <cstdio>

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
    if (!require(server.listen(QHostAddress::LocalHost, 0), "could not listen on loopback")) return 1;
    const QString hash1(64, QLatin1Char('a'));
    const QString hash2(64, QLatin1Char('b'));
    QStringList requested_paths;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&]
    {
        while (server.hasPendingConnections())
        {
            QTcpSocket *socket = server.nextPendingConnection();
            const std::shared_ptr<QByteArray> request_buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, request_buffer, &requested_paths, hash1, hash2]
            {
                request_buffer->append(socket->readAll());
                if (!request_buffer->contains("\r\n\r\n")) return;
                const QByteArray path = request_buffer->split(' ').value(1);
                requested_paths.append(QString::fromLatin1(path));
                QByteArray body;
                if (path == "/dvmdb/v1/projects")
                    body = R"({"projects":[{"id":"p1","name":"Waterworks"}]})";
                else if (path == "/dvmdb/v1/projects/p1/branches")
                    body = R"({"branches":["main"]})";
                else if (path == "/dvmdb/v1/projects/p1/branches/main/history?limit=101")
                {
                    QJsonArray list;
                    list.append(QJsonObject{{QStringLiteral("id"), hash1},
                                            {QStringLiteral("parents"), QJsonArray{hash2}}});
                    list.append(QJsonObject{{QStringLiteral("id"), hash2},
                                            {QStringLiteral("parents"), QJsonArray{}}});
                    body = QJsonDocument(QJsonObject{{QStringLiteral("revisions"), list}}).toJson();
                }
                else body = R"({"error":"unexpected route"})";
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
        }
    });
    ProjectServerProfile profile;
    profile.id = QStringLiteral("test");
    profile.connection_type = QStringLiteral("remote");
    profile.name = QStringLiteral("Test DB");
    profile.auth_mode = QStringLiteral("None");
    profile.endpoint = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    ProjectSourceReader reader;
    bool finished = false;
    SourceSnapshot received;
    QString failure;
    reader.load(profile, [&](const SourceSnapshot &snapshot, const QString &error)
    {
        received = snapshot;
        failure = error;
        finished = true;
    });
    if (!require(waitUntil(app, [&] { return finished; }), "REST reader did not finish")) return 1;
    if (!require(failure.isEmpty() && received.projects.size() == 1,
                 "REST catalogue failed")) return 1;
    const SourceProject &project = received.projects.first();
    if (!require(project.id == QStringLiteral("p1") && project.name == QStringLiteral("Waterworks")
        && project.branches.size() == 1 && project.branches.first().name == QStringLiteral("main")
        && project.branches.first().head == hash1 && project.branches.first().history.size() == 2
        && project.branches.first().history.first().parents == QStringList{hash2},
        "REST normalized branch history does not match API")) return 1;
    if (!require(requested_paths.size() == 3, "unexpected REST request sequence")) return 1;

    // A non-implemented authentication mode must never be silently claimed as connected.
    profile.auth_mode = QStringLiteral("API token");
    finished = false;
    reader.load(profile, [&](const SourceSnapshot &, const QString &error)
    {
        failure = error;
        finished = true;
    });
    if (!require(finished && failure.contains(QStringLiteral("not implemented")),
                 "unsupported REST auth was not rejected")) return 1;

    // Cancellation invalidates results (including an already queued reply).
    profile.auth_mode = QStringLiteral("None");
    bool cancelled_callback = false;
    reader.load(profile, [&](const SourceSnapshot &, const QString &)
    {
        cancelled_callback = true;
    });
    reader.cancel();
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 150) app.processEvents(QEventLoop::AllEvents, 20);
    if (!require(!cancelled_callback, "cancelled request delivered stale result")) return 1;
    return 0;
}
