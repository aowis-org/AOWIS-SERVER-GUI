#include "interface_server_map_rest.h"

#include "map_server_client_configuration.h"

#include <QDebug>

InterfaceServerMapREST::InterfaceServerMapREST(QObject *parent)
    : InterfaceServerMap(parent)
{
    initRestConnection();
}

void InterfaceServerMapREST::initRestConnection()
{
    const MapServerClientConfiguration &configuration = mapServerClientConfiguration();
    this->rest = new RESTClient(configuration.base_url, configuration.api_key,
                                configuration.delete_api_key, this);

    connect(this->rest, &RESTClient::requestFinishedTile, this,
            [this](const QByteArray &data, const QString &key)
    {
        this->rest_pending.remove(key);

        if (data.isEmpty())
        {
            qWarning() << "Tile response is empty:" << key;
            emit signalTileFailed(key);
            return;
        }

        emit signalTileDataReceived(key, data);
    });

    connect(this->rest, &RESTClient::requestTileError, this,
            [this](const QString &key, const QString &error)
    {
        this->rest_pending.remove(key);
        qWarning() << "Tile request failed:" << key << error;
        emit signalTileFailed(key);
    });

    connect(this->rest, &RESTClient::requestFinishedTerrainTile, this,
            [this](const QByteArray &data, const QString &key)
    {
        this->terrain_pending.remove(key);

        if (data.isEmpty())
        {
            const QString error = QStringLiteral("Terrain response is empty");
            qWarning() << error << key;
            emit signalTerrainTileFailed(key, error);
            return;
        }

        emit signalTerrainTileDataReceived(key, data);
    });

    connect(this->rest, &RESTClient::requestTerrainTileError, this,
            [this](const QString &key, const QString &error)
    {
        this->terrain_pending.remove(key);
        qWarning() << "Terrain tile request failed:" << key << error;
        emit signalTerrainTileFailed(key, error);
    });

    connect(this->rest, &RESTClient::requestFinished, this,
            [this](const QByteArray &data)
    {
        if (!this->terrain_elevation_pending)
            return;

        this->terrain_elevation_pending = false;
        emit signalTerrainElevationDataReceived(data);
    });

    connect(this->rest, &RESTClient::requestError, this,
            [this](const QString &error)
    {
        if (!this->terrain_elevation_pending)
            return;

        this->terrain_elevation_pending = false;
        emit signalTerrainElevationFailed(error);
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

void InterfaceServerMapREST::requestTerrainTile(const QString &endpoint, const QString &key)
{
    if (this->terrain_pending.contains(key))
        return;

    this->terrain_pending.insert(key);
    this->rest->getTerrainTile(endpoint, key);
}

void InterfaceServerMapREST::requestTerrainElevation(const QString &endpoint)
{
    if (this->terrain_elevation_pending)
    {
        emit signalTerrainElevationFailed(
            QStringLiteral("A terrain elevation request is already in progress"));
        return;
    }

    this->terrain_elevation_pending = true;
    this->rest->get(endpoint);
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
