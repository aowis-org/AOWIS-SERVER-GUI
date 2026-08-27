#include "interface_server_map_rest.h"

#include "map_server_client_configuration.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
bool isUpstreamCancellationError(const QString &error)
{
    return error.contains(QStringLiteral("upstream download canceled"), Qt::CaseInsensitive);
}
}

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
        if (!isUpstreamCancellationError(error))
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
        if (!isUpstreamCancellationError(error))
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

    connect(this->rest, &RESTClient::requestFinishedControl, this,
            [this](const QString &request_key, const QByteArray &data)
    {
        if (request_key == QStringLiteral("map-upstream-activity"))
            this->map_upstream_activity_pending = false;
        else if (request_key == QStringLiteral("terrain-upstream-activity"))
            this->terrain_upstream_activity_pending = false;

        if (request_key != QStringLiteral("map-upstream-activity") &&
            request_key != QStringLiteral("terrain-upstream-activity"))
        {
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(data);
        if (!document.isObject())
        {
            emit signalUpstreamControlError(QStringLiteral("Map server returned invalid upstream activity JSON"));
            return;
        }

        const QJsonObject root = document.object();
        const QString section_name = request_key == QStringLiteral("map-upstream-activity")
            ? QStringLiteral("map_tiles")
            : QStringLiteral("terrain");
        const QJsonObject section = root.value(section_name).toObject();
        MapUpstreamActivity activity;
        activity.active = qMax(0, section.value(QStringLiteral("active")).toInt());
        activity.queued = qMax(0, section.value(QStringLiteral("queued")).toInt());

        if (request_key == QStringLiteral("map-upstream-activity"))
            emit signalMapTileUpstreamActivity(activity);
        else
            emit signalTerrainUpstreamActivity(activity);
    });

    connect(this->rest, &RESTClient::requestControlError, this,
            [this](const QString &request_key, const QString &error)
    {
        if (request_key == QStringLiteral("map-upstream-activity"))
            this->map_upstream_activity_pending = false;
        else if (request_key == QStringLiteral("terrain-upstream-activity"))
            this->terrain_upstream_activity_pending = false;
        emit signalUpstreamControlError(error);
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

void InterfaceServerMapREST::requestMapTileUpstreamActivity()
{
    if (this->map_upstream_activity_pending)
        return;
    this->map_upstream_activity_pending = true;
    this->rest->getControl(QStringLiteral("/upstream/v1/activity"),
                           QStringLiteral("map-upstream-activity"));
}

void InterfaceServerMapREST::requestTerrainUpstreamActivity()
{
    if (this->terrain_upstream_activity_pending)
        return;
    this->terrain_upstream_activity_pending = true;
    this->rest->getControl(QStringLiteral("/upstream/v1/activity"),
                           QStringLiteral("terrain-upstream-activity"));
}

void InterfaceServerMapREST::cancelMapTileUpstreamDownloads()
{
    this->rest->deleteControl(QStringLiteral("/upstream/v1/map-tiles"),
                              QStringLiteral("cancel-map-upstream"));
}

void InterfaceServerMapREST::cancelTerrainUpstreamDownloads()
{
    this->rest->deleteControl(QStringLiteral("/upstream/v1/terrain"),
                              QStringLiteral("cancel-terrain-upstream"));
}
