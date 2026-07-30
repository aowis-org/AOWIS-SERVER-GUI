#include "interface_server_map_rest.h"

InterfaceServerMapREST::InterfaceServerMapREST(QObject *parent)
    : InterfaceServerMap(parent)
{
    initRestConnection();
}

void InterfaceServerMapREST::initRestConnection()
{
    this->rest = new RESTClient("http://aowis-server-map.localhost:80", this);
    
    connect(this->rest, &RESTClient::requestFinishedTile, this, [this](const QByteArray &data, const QString &key)
    {
        this->rest_pending.remove(key);

        QPixmap pix;
        if (!pix.loadFromData(data))
        {
            qWarning() << "Tile decode failed:" << key;
            emit signalTileFailed(key);
            return;
        }

        emit signalTileReceived(key, new QPixmap(pix));
    });

    connect(this->rest, &RESTClient::requestTileError, this, [this](const QString &key, const QString &error)
    {
        this->rest_pending.remove(key);
        qWarning() << "Tile request failed:" << key << error;
        emit signalTileFailed(key);
    });
}

void InterfaceServerMapREST::requestTile(QString endpoint, const QString &key, int x, int y)
{
    Q_UNUSED(x)
    Q_UNUSED(y)

    if (this->rest_pending.contains(key))
        return;
    
    this->rest_pending.insert(key);
    this->rest->getTile(endpoint, key);
}

