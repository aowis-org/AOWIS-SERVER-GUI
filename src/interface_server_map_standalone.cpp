#include "interface_server_map_standalone.h"

#include <QDebug>
#include <QTimer>

InterfaceServerMapStandalone::InterfaceServerMapStandalone(QObject *parent)
    : InterfaceServerMap(parent),
    map_tiles(new MapTiles(this))
{
    connect(this->map_tiles, &MapTiles::tileReady, this,
            [this](const QString &key, const QByteArray &data)
    {
        if (data.isEmpty())
        {
            qWarning() << "Downloaded tile is empty:" << key;
            emit signalTileFailed(key);
            return;
        }

        emit signalTileDataReceived(key, data);
    }, Qt::QueuedConnection);

    connect(this->map_tiles, &MapTiles::tileFailed, this,
            [this](const QString &key, MapTiles::TileFailureReason reason)
    {
        if (reason == MapTiles::TileFailureReason::Timeout)
            qWarning() << "Tile request timed out:" << key;
        else
            qWarning() << "Tile request failed upstream:" << key;

        emit signalTileFailed(key);
    });
}

void InterfaceServerMapStandalone::requestTile(const QString &endpoint, const QString &key, int x, int y)
{
    QString endpoint_without_extension = endpoint;
    endpoint_without_extension.remove(".png");

    const QStringList parts = endpoint_without_extension.split("/", Qt::SkipEmptyParts);
    if (parts.size() < 2)
    {
        qWarning() << "Invalid endpoint:" << endpoint;
        emit signalTileFailed(key);
        return;
    }

    const QString provider = parts[0];
    bool zoom_valid = false;
    const int zoom = parts[1].toInt(&zoom_valid);
    if (!zoom_valid)
    {
        qWarning() << "Invalid tile zoom in endpoint:" << endpoint;
        emit signalTileFailed(key);
        return;
    }

    const MapTiles::TileRequestResult result = this->map_tiles->getTile(provider, zoom, x, y, key);
    switch (result.status)
    {
    case MapTiles::TileRequestStatus::Ready:
        if (result.data.isEmpty())
        {
            qWarning() << "Cached tile is empty:" << key;
            emit signalTileFailed(key);
            return;
        }

        QTimer::singleShot(0, this, [this, key, data = result.data]
        {
            emit signalTileDataReceived(key, data);
        });
        return;

    case MapTiles::TileRequestStatus::Pending:
        return;

    case MapTiles::TileRequestStatus::InvalidRequest:
        qWarning() << "Invalid tile request:" << endpoint << x << y;
        emit signalTileFailed(key);
        return;

    case MapTiles::TileRequestStatus::ServerBusy:
        qWarning() << "Map tile downloader is busy:" << key;
        emit signalTileFailed(key);
        return;
    }
}

void InterfaceServerMapStandalone::deleteTiles(quint64 request_id, const QString &provider, int zoom,
                                               int tile_x_min, int tile_x_max,
                                               int tile_y_min, int tile_y_max)
{
    const int deleted_count = this->map_tiles->deleteTiles(
        provider, zoom, tile_x_min, tile_x_max, tile_y_min, tile_y_max);
    if (deleted_count == -1)
    {
        emit signalTileDeletionFailed(request_id, QStringLiteral("Invalid tile cache deletion request"));
        return;
    }
    if (deleted_count < -1)
    {
        emit signalTileDeletionFailed(request_id, QStringLiteral("Failed to delete one or more cached tiles"));
        return;
    }

    emit signalTilesDeleted(request_id);
}
