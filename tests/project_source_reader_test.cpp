#include "projects/project_source_reader.h"

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
    if (!require(server.listen(QHostAddress::LocalHost, 0), "could not listen on loopback")) return 1;
    const QString a(64, QLatin1Char('a'));
    const QString b(64, QLatin1Char('b'));
    const QString c(64, QLatin1Char('c'));
    const QString d(64, QLatin1Char('d'));
    const QString e(64, QLatin1Char('e'));
    QStringList requested_paths;
    bool corrupt_second_page = false;
    const auto record = [](const QString &id, const QStringList &parents)
    {
        QJsonArray links;
        for (const QString &parent : parents) links.append(parent);
        return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("parents"), links}};
    };
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&]
    {
        while (server.hasPendingConnections())
        {
            QTcpSocket *socket = server.nextPendingConnection();
            const std::shared_ptr<QByteArray> buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket,
                [socket, buffer, &requested_paths, &corrupt_second_page, a, b, c, d, e, record]
            {
                buffer->append(socket->readAll());
                if (!buffer->contains("\r\n\r\n")) return;
                const QByteArray path = buffer->split(' ').value(1);
                requested_paths.append(QString::fromLatin1(path));
                QByteArray body;
                if (path == "/dvmdb/v1/projects")
                    body = R"({"projects":[{"id":"p1","name":"Waterworks"}]})";
                else if (path.startsWith("/dvmdb/v1/projects/p1/revision-graph?limit=256"))
                {
                    QJsonArray heads{
                        QJsonObject{{QStringLiteral("branch"), QStringLiteral("main")},
                                    {QStringLiteral("revision"), e}},
                        QJsonObject{{QStringLiteral("branch"), QStringLiteral("feature")},
                                    {QStringLiteral("revision"), d}},
                        QJsonObject{{QStringLiteral("branch"), QStringLiteral("archive")},
                                    {QStringLiteral("revision"), b}},
                        QJsonObject{{QStringLiteral("branch"), QStringLiteral("alias")},
                                    {QStringLiteral("revision"), e}}};
                    const bool second = path.contains("&cursor=page-2");
                    if (second && corrupt_second_page)
                        heads[0] = QJsonObject{{QStringLiteral("branch"), QStringLiteral("main")},
                                               {QStringLiteral("revision"), c}};
                    const QJsonArray revisions = second
                        ? QJsonArray{record(b, {a}), record(a, {})}
                        : QJsonArray{record(e, {c, d}), record(c, {b}), record(d, {b})};
                    body = QJsonDocument(QJsonObject{
                        {QStringLiteral("heads"), heads},
                        {QStringLiteral("revisions"), revisions},
                        {QStringLiteral("complete"), second},
                        {QStringLiteral("nextCursor"), second ? QJsonValue(QJsonValue::Null)
                            : QJsonValue(QStringLiteral("page-2"))}}).toJson();
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
    if (!require(waitUntil(app, [&] { return finished; }), "REST graph reader did not finish")) return 1;
    if (!require(failure.isEmpty() && received.projects.size() == 1,
                 "REST catalogue failed")) return 1;
    const SourceProject &project = received.projects.first();
    if (!require(project.id == QStringLiteral("p1") && project.name == QStringLiteral("Waterworks")
        && project.graph_complete && project.branches.size() == 4
        && project.branches.first().name == QStringLiteral("main")
        && project.branches.first().head == e
        && project.branches.at(1).head == d && project.branches.at(2).head == b
        && project.branches.at(3).head == e
        && project.revisions.size() == 5 && project.revisions.first().parents == QStringList({c, d}),
        "REST graph did not preserve all branches, merge parents or historical HEAD")) return 1;
    if (!require(requested_paths.size() == 3
        && requested_paths.at(1) == QStringLiteral("/dvmdb/v1/projects/p1/revision-graph?limit=256")
        && requested_paths.at(2) == QStringLiteral("/dvmdb/v1/projects/p1/revision-graph?limit=256&cursor=page-2"),
        "REST graph was not paginated via the pinned cursor")) return 1;

    corrupt_second_page = true;
    finished = false;
    reader.load(profile, [&](const SourceSnapshot &snapshot, const QString &error)
    {
        received = snapshot;
        failure = error;
        finished = true;
    });
    if (!require(waitUntil(app, [&] { return finished; }), "corrupt HEAD test did not finish")) return 1;
    if (!require(!failure.isEmpty() && received.projects.isEmpty(),
        "changed HEAD set was published as a complete graph")) return 1;
    corrupt_second_page = false;

    profile.auth_mode = QStringLiteral("API token");
    finished = false;
    reader.load(profile, [&](const SourceSnapshot &, const QString &error)
    {
        failure = error;
        finished = true;
    });
    if (!require(finished && failure.contains(QStringLiteral("not implemented")),
                 "unsupported REST auth was not rejected")) return 1;

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
