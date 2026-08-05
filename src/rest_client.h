#ifndef REST_CLIENT_H
#define REST_CLIENT_H

#include <QObject>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

class RESTClient : public QObject
{
    Q_OBJECT

public:
    explicit RESTClient(const QString &url_base, QObject *parent = nullptr);
    RESTClient(const QString &url_base, const QByteArray &api_key,
               const QByteArray &delete_api_key, QObject *parent = nullptr);

    void get(const QString &endpoint);
    void getTile(const QString &endpoint, const QString &key);
    void post(const QString &endpoint, const QJsonObject &payload);
    void deleteResource(const QString &endpoint, quint64 request_id);

signals:
    void requestFinished(const QByteArray &data);
    void requestFinishedTile(const QByteArray &data, const QString &key);
    void requestFinishedDelete(quint64 request_id);
    void requestError(const QString &error);
    void requestTileError(const QString &key, const QString &error);
    void requestDeleteError(quint64 request_id, const QString &error);

private:
    QNetworkRequest createRequest(const QString &endpoint, const QByteArray &key) const;
    void monitorReply(QNetworkReply *reply) const;
    QString replyErrorDescription(QNetworkReply *reply) const;
    void handleReply(QNetworkReply *reply);
    void handleReplyTile(QNetworkReply *reply, const QString &key);
    void handleReplyDelete(QNetworkReply *reply, quint64 request_id);

    QNetworkAccessManager network_manager;
    QString url_base;
    QByteArray api_key;
    QByteArray delete_api_key;
};

#endif // REST_CLIENT_H
