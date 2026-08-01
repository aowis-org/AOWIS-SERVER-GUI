#ifndef REST_CLIENT_H
#define REST_CLIENT_H

#include <QObject>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <QJsonDocument>
#include <QJsonObject>

class RESTClient : public QObject
{
    Q_OBJECT
public:
    explicit RESTClient(const QString &url_base, QObject *parent = nullptr);
    
    void get(const QString &endpoint);
    void getTile(const QString &endpoint, const QString &key);
    void post(const QString &endpoint, const QJsonObject &payload);
    void deleteResource(const QString &endpoint, quint64 request_id);

private:
    QNetworkAccessManager network_manager;
    QString url_base;
    
    void handleReply(QNetworkReply *reply);
    void handleReplyTile(QNetworkReply *reply, QString key);
    void handleReplyDelete(QNetworkReply *reply, quint64 request_id);
    
signals:
    void requestFinished(const QByteArray &data);
    void requestFinishedTile(const QByteArray &data, const QString &key);
    void requestFinishedDelete(quint64 request_id);
    void requestError(const QString &error);
    void requestTileError(const QString &key, const QString &error);
    void requestDeleteError(quint64 request_id, const QString &error);
};

#endif // REST_CLIENT_H
