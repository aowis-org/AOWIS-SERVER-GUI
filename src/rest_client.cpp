#include "rest_client.h"

RESTClient::RESTClient(const QString &url_base, QObject *parent)
    : QObject{parent},
    url_base(url_base)
{
    
}

void RESTClient::get(const QString &endpoint)
{
    QNetworkRequest req((this->url_base + endpoint));
    req.setRawHeader("User-Agent", "aowis-epanet-gui/1.0 (https://github.com/aowis-org/AOWIS-EPANET-GUI)");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    req.setRawHeader("Accept", "*/*");
    QNetworkReply *reply = this->network_manager.get(req);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
    });
}
void RESTClient::getTile(const QString &endpoint, const QString &key)
{
    QNetworkRequest req((this->url_base + endpoint));
    req.setRawHeader("User-Agent", "aowis-epanet-gui/1.0 (https://github.com/aowis-org/AOWIS-EPANET-GUI)");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    req.setRawHeader("Accept", "*/*");
    QNetworkReply *reply = this->network_manager.get(req);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
        handleReplyTile(reply, key);
    });
}

void RESTClient::post(const QString &endpoint, const QJsonObject &payload)
{
    QNetworkRequest req((this->url_base + endpoint));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QJsonDocument doc(payload);
    QNetworkReply *reply = this->network_manager.post(req, doc.toJson());
    
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
    });
}

void RESTClient::handleReply(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError)
    {
        emit requestError(reply->errorString());
        reply->deleteLater();
        
        return;
    }
    
    emit requestFinished(reply->readAll());
    reply->deleteLater();
}
void RESTClient::handleReplyTile(QNetworkReply *reply, QString key)
{
    if (reply->error() != QNetworkReply::NoError)
    {
        emit requestTileError(key, reply->errorString());
        reply->deleteLater();
        
        return;
    }
    
    emit requestFinishedTile(reply->readAll(), key);
    reply->deleteLater();
}
