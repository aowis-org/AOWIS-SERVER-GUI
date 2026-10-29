#pragma once

#include "config/personal_repository_storage.h"
#include "projects/dvmdb_identity.h"
#include "services/http_health_monitor.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <functional>
#include <utility>

// Continuous read-only identity + intended-role verification. A configured
// URL is not authentication; this monitor never authorizes draft writes.
class PersonalRepositoryMonitor final : public QObject
{
public:
    enum class State { Unconfigured, Checking, Verified, Unavailable, WrongIdentity, WrongRole };
    using Observer = std::function<void(State, const QString &)>;

    explicit PersonalRepositoryMonitor(QObject *parent = nullptr)
        : QObject(parent), health(this) {}

    void start(const PersonalRepositoryProfile &profile, Observer observer,
               int interval_ms = 5000)
    {
        stop();
        this->observer = std::move(observer);
        QString invalid;
        if (!validatePersonalRepositoryProfile(profile, &invalid))
        {
            setState(State::Unavailable, invalid);
            return;
        }
        this->expected_id = profile.repository_id;
        QString endpoint = profile.endpoint;
        while (endpoint.endsWith(QLatin1Char('/'))) endpoint.chop(1);
        if (!endpoint.endsWith(QStringLiteral("/dvmdb/v1")))
            endpoint += QStringLiteral("/dvmdb/v1");
        this->health.start(QUrl(endpoint + QStringLiteral("/repository/tracking")),
            [this](const QByteArray &body)
            {
                this->observed_id.clear();
                this->observed_role.clear();
                const QJsonDocument response = QJsonDocument::fromJson(body);
                if (!response.isObject()) return false;
                const QJsonObject data = response.object();
                this->observed_id = data.value(QStringLiteral("repositoryId")).toString();
                this->observed_role = data.value(QStringLiteral("role")).toString();
                return DvmdbIdentity::isUuidV7(this->observed_id)
                    && this->observed_id == this->expected_id
                    && this->observed_role == QStringLiteral("private");
            },
            [this](HttpHealthMonitor::State state, const QString &error)
            {
                if (state == HttpHealthMonitor::State::Checking)
                    setState(State::Checking, QString());
                else if (state == HttpHealthMonitor::State::Healthy)
                    setState(State::Verified, QString());
                else if (DvmdbIdentity::isUuidV7(this->observed_id)
                         && this->observed_id != this->expected_id)
                    setState(State::WrongIdentity,
                             QStringLiteral("Repository UUID mismatch: expected %1, received %2.")
                                 .arg(this->expected_id, this->observed_id));
                else if (this->observed_id == this->expected_id
                         && this->observed_role != QStringLiteral("private"))
                    setState(State::WrongRole,
                             QStringLiteral("Working repository must have the private role (reported: %1).")
                                 .arg(this->observed_role));
                else
                    setState(State::Unavailable, error);
            }, {}, interval_ms);
    }

    void poll() { this->health.poll(); }
    void stop()
    {
        this->health.stop();
        this->expected_id.clear();
        this->observed_id.clear();
        this->observed_role.clear();
        this->observer = {};
        this->state = State::Unconfigured;
        this->detail.clear();
    }
    State currentState() const { return this->state; }
    QString currentDetail() const { return this->detail; }

private:
    void setState(State state, const QString &message)
    {
        if (state == this->state && message == this->detail) return;
        this->state = state;
        this->detail = message;
        if (this->observer) this->observer(state, message);
    }

    HttpHealthMonitor health;
    QString expected_id;
    QString observed_id;
    QString observed_role;
    Observer observer;
    State state = State::Unconfigured;
    QString detail;
};
