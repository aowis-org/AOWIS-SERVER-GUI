#include "config/personal_repository_storage.h"
#include "config/project_server_profile_storage.h"
#include "settings/personal_repository_settings_widget.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <functional>

namespace
{
const QString personal_id = QStringLiteral("019a47b1-2040-7af9-8d1c-2670badb6371");
const QString other_id = QStringLiteral("019a47b1-2041-7af9-8d1c-2670badb6371");

bool check(bool value, const char *message)
{
    if (!value) std::fprintf(stderr, "FAILED: %s\n", message);
    return value;
}

bool waitFor(QApplication &app, const std::function<bool()> &predicate)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 2500)
    {
        app.processEvents();
        if (predicate()) return true;
        QThread::msleep(10);
    }
    app.processEvents();
    return predicate();
}

class RepositoryStub final : public QObject
{
public:
    QTcpServer server;
    QString id = personal_id;
    QString role = QStringLiteral("private");
    bool responding = true;
    int requests = 0;

    bool start()
    {
        connect(&this->server, &QTcpServer::newConnection, this, [this]
        {
            while (this->server.hasPendingConnections())
            {
                QTcpSocket *socket = this->server.nextPendingConnection();
                QByteArray *request = new QByteArray();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, request]
                {
                    request->append(socket->readAll());
                    if (!request->contains("\r\n\r\n")) return;
                    const bool correct_path = request->startsWith("GET /dvmdb/v1/repository/tracking ");
                    ++this->requests;
                    const QJsonObject response{
                        {QStringLiteral("repositoryId"), this->id},
                        {QStringLiteral("role"), this->role}};
                    const QByteArray data = QJsonDocument(response).toJson(QJsonDocument::Compact);
                    if (this->responding && correct_path)
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                                      + QByteArray::number(data.size()) + "\r\n\r\n" + data);
                    else
                        socket->write("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, [socket, request]
                {
                    delete request;
                    socket->deleteLater();
                });
            }
        });
        return this->server.listen(QHostAddress::LocalHost);
    }

    QString endpoint() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(this->server.serverPort());
    }
};
}

int main(int argc, char **argv)
{
    QTemporaryDir data_root;
    if (!check(data_root.isValid(), "could not create isolated GUI settings directory")) return 1;
    qputenv("XDG_DATA_HOME", data_root.path().toUtf8());
    QApplication app(argc, argv);
    RepositoryStub remote;
    if (!check(remote.start(), "could not start fake private DVMDB")) return 1;

    ProjectServerProfile shared;
    shared.id = QStringLiteral("shared-a");
    shared.name = QStringLiteral("Shared source A");
    shared.endpoint = QStringLiteral("https://shared.example.invalid");
    ProjectServerProfile other = shared;
    other.id = QStringLiteral("shared-b");
    other.name = QStringLiteral("Shared source B");
    other.endpoint = QStringLiteral("https://second.example.invalid");
    if (!check(saveProjectServerProfiles({shared, other}), "failed to save shared source fixtures")) return 1;
    if (!check(!loadPersonalRepositoryProfile().has_value(), "personal repository must default to absent")) return 1;
    if (!check(QDir::cleanPath(defaultPersonalDvmdbPath())
                   != QDir::cleanPath(defaultLocalDvmdbPath()),
               "Standalone private repository must not reuse the local project source")) return 1;
    if (!check(resolvePersonalRepositoryTarget().kind == PersonalRepositoryTarget::Kind::Unavailable,
               "missing personal HTTP repository must fail closed")) return 1;

    {
        PersonalRepositorySettingsWidget page;
        QLineEdit *endpoint = page.findChild<QLineEdit *>(QStringLiteral("personalRepositoryEndpoint"));
        QLineEdit *identity = page.findChild<QLineEdit *>(QStringLiteral("personalRepositoryExpectedId"));
        QPushButton *save = page.findChild<QPushButton *>(QStringLiteral("personalRepositorySave"));
        QPushButton *recheck = page.findChild<QPushButton *>(QStringLiteral("personalRepositoryRecheck"));
        if (!check(endpoint && identity && save && recheck, "personal repository settings controls missing")) return 1;

        endpoint->setText(QStringLiteral("http://user:password@127.0.0.1:8755"));
        identity->setText(personal_id);
        save->click();
        if (!check(!loadPersonalRepositoryProfile().has_value(), "credential-containing endpoint was persisted")) return 1;

        endpoint->setText(remote.endpoint());
        save->click();
        if (!check(waitFor(app, [&]
            {
                return page.verificationState() == PersonalRepositoryMonitor::State::Verified;
            }), "private repository failed UUID/role verification")) return 1;
        const std::optional<PersonalRepositoryProfile> persisted = loadPersonalRepositoryProfile();
        if (!check(persisted && persisted->repository_id == personal_id
                   && persisted->endpoint == remote.endpoint(), "personal repository config not persisted")) return 1;
        if (!check(loadProjectServerProfiles().size() == 2, "private repository replaced a source profile")) return 1;
        const PersonalRepositoryTarget selected = resolvePersonalRepositoryTarget();
        if (!check(selected.kind == PersonalRepositoryTarget::Kind::Http
                   && selected.endpoint == remote.endpoint()
                   && selected.repository_id == personal_id && selected.storage_path.isEmpty(),
                   "private target was not resolved by its independent HTTP configuration")) return 1;

        remote.id = other_id;
        recheck->click();
        if (!check(waitFor(app, [&]
            {
                return page.verificationState() == PersonalRepositoryMonitor::State::WrongIdentity;
            }), "wrong repository identity was silently accepted")) return 1;
        if (!check(loadPersonalRepositoryProfile()->repository_id == personal_id,
                   "identity mismatch silently rebound expected repository UUID")) return 1;

        remote.id = personal_id;
        remote.role = QStringLiteral("shared");
        recheck->click();
        if (!check(waitFor(app, [&]
            {
                return page.verificationState() == PersonalRepositoryMonitor::State::WrongRole;
            }), "shared repository was accepted as a private working root")) return 1;
        remote.role = QStringLiteral("private");
        remote.responding = false;
        recheck->click();
        if (!check(waitFor(app, [&]
            {
                return page.verificationState() == PersonalRepositoryMonitor::State::Unavailable;
            }), "offline personal repository did not fail closed")) return 1;
    }

    // Simulate restart: saved personal repository must not depend on which
    // of two shared sources happens to be selected.
    remote.responding = true;
    {
        PersonalRepositorySettingsWidget page;
        if (!check(waitFor(app, [&]
            {
                return page.verificationState() == PersonalRepositoryMonitor::State::Verified;
            }), "personal repository identity not reverified after UI restart")) return 1;
        if (!check(loadProjectServerProfiles().size() == 2,
                   "shared database list changed on personal repository restart")) return 1;
        QPushButton *remove = page.findChild<QPushButton *>(QStringLiteral("personalRepositoryRemove"));
        if (!check(remove != nullptr, "missing remove action")) return 1;
        remove->click();
        if (!check(!loadPersonalRepositoryProfile() &&
                   page.verificationState() == PersonalRepositoryMonitor::State::Unconfigured,
                   "removing personal repo did not persist disabled state")) return 1;
        if (!check(resolvePersonalRepositoryTarget().kind == PersonalRepositoryTarget::Kind::Unavailable,
                   "removing personal repository must disable target resolution")) return 1;
    }
    std::puts("Personal repository configuration and identity checks passed");
    return 0;
}
