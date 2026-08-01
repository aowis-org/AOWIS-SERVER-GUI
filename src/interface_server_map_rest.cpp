#include "interface_server_map_rest.h"

#include <QDebug>

InterfaceServerMapREST::InterfaceServerMapREST(QObject *parent)
    : InterfaceServerMap(parent)
{
    initRestConnection();
}

void InterfaceServerMapREST::initRestConnection()
{
    this->rest = new RESTClient("http://aowis-server-map.localhost:80", this);

    connect(this->rest, &RESTClient::requestFinishedTile, this,
            [this](const QByteArray &data, const QString &key)
    {
        this->rest_pending.remove(key);

        QPixmap pixmap;
        if (!pixmap.loadFromData(data))
        {
            qWarning() << "Tile decode failed:" << key;
            emit signalTileFailed(key);
            return;
        }

        emit signalTileReceived(key, pixmap);
    });

    connect(this->rest, &RESTClient::requestTileError, this,
            [this](const QString &key, const QString &error)
    {
        this->rest_pending.remove(key);
        qWarning() << "Tile request failed:" << key << error;
        emit signalTileFailed(key);
    });

    connect(this->rest, &RESTClient::requestFinishedDelete, this,
            [this](quint64 request_id)
    {
        emit signalTilesDeleted(request_id);
    });

    connect(this->rest, &RESTClient::requestDeleteError, this,
            [this](quint64 request_id, const QString &error)
    {
        emit signalTileDeletionFailed(request_id, error);
    });
}

void InterfaceServerMapREST::requestTile(const QString &endpoint, const QString &key, int x, int y)
{
    Q_UNUSED(x)
    Q_UNUSED(y)

    if (this->rest_pending.contains(key))
        return;

    this->rest_pending.insert(key);
    this->rest->getTile(endpoint, key);
}

void InterfaceServerMapREST::deleteTiles(quint64 request_id, const QString &provider, int zoom,
                                         int tile_x_min, int tile_x_max,
                                         int tile_y_min, int tile_y_max)
{
    const QString endpoint = QString("/cache/%1/%2/%3/%4/%5/%6")
        .arg(provider)
        .arg(zoom)
        .arg(tile_x_min)
        .arg(tile_x_max)
        .arg(tile_y_min)
        .arg(tile_y_max);
    this->rest->deleteResource(endpoint, request_id);
}
