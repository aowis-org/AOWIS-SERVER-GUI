#include "interface_server_map_standalone.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRunnable>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <cmath>

namespace
{
constexpr int TerrainRequestThreadCountMaximum = 4;
const QString StandaloneTerrainDefaultDataset = QStringLiteral("copernicus-glo30");

QString terrainLookupError(const Aowis::Map::TerrainTileLookupResult &result)
{
    if (!result.error_message.isEmpty())
        return result.error_message;

    return QStringLiteral("Terrain tile request failed: %1")
        .arg(Aowis::Map::terrainTileLookupStatusId(result.status));
}

QString terrainElevationLookupError(const Aowis::Map::TerrainElevationLookupResult &result)
{
    if (!result.error_message.isEmpty())
        return result.error_message;

    return QStringLiteral("Terrain elevation request failed: %1")
        .arg(Aowis::Map::terrainElevationLookupStatusId(result.status));
}

QByteArray terrainElevationJson(const Aowis::Map::TerrainElevationLookupResult &result)
{
    if (result.status != Aowis::Map::TerrainElevationLookupStatus::Ready ||
        !result.sample.has_value())
    {
        return QByteArray();
    }

    const Aowis::Map::TerrainElevationSample &sample = result.sample.value();
    QJsonObject object;
    object.insert(QStringLiteral("status"), QStringLiteral("ready"));
    object.insert(QStringLiteral("elevation_m"), sample.elevation_m);
    object.insert(QStringLiteral("dataset"), sample.dataset);
    object.insert(QStringLiteral("nominal_resolution_m"), sample.nominal_resolution_m);
    object.insert(QStringLiteral("vertical_datum"),
                  Aowis::Map::terrainVerticalDatumId(sample.vertical_datum));
    object.insert(QStringLiteral("vertical_datum_name"),
                  Aowis::Map::terrainVerticalDatumDisplayName(sample.vertical_datum));
    const QString vertical_datum_authority =
        Aowis::Map::terrainVerticalDatumAuthorityCode(sample.vertical_datum);
    if (!vertical_datum_authority.isEmpty())
    {
        object.insert(QStringLiteral("vertical_datum_authority"),
                      vertical_datum_authority);
    }
    object.insert(QStringLiteral("vertical_reference"),
                  Aowis::Map::terrainVerticalReferenceId(
                      Aowis::Map::terrainVerticalReference(sample.vertical_datum)));
    object.insert(QStringLiteral("source_vertical_datum"),
                  Aowis::Map::terrainVerticalDatumId(sample.source_vertical_datum));
    object.insert(QStringLiteral("origin"),
                  Aowis::Map::terrainDataOriginId(sample.origin));

    if (result.tile_address.has_value())
    {
        const Aowis::Map::TerrainTileAddress &address = result.tile_address.value();
        QJsonObject tile;
        tile.insert(QStringLiteral("zoom"), address.zoom);
        tile.insert(QStringLiteral("x"), double(address.x));
        tile.insert(QStringLiteral("y"), double(address.y));
        object.insert(QStringLiteral("tile"), tile);
    }

    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool terrainTileRequestFromEndpoint(const QString &endpoint,
                                    QString *dataset,
                                    Aowis::Map::TerrainTileAddress *address,
                                    QString *error)
{
    if (dataset == nullptr || address == nullptr)
        return false;

    const QString path = QUrl(endpoint).path();
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() != 6 || parts.at(0) != QStringLiteral("terrain") ||
        parts.at(1) != QStringLiteral("v1"))
    {
        if (error != nullptr)
            *error = QStringLiteral("Invalid terrain tile endpoint: %1").arg(endpoint);
        return false;
    }

    QString y_text = parts.at(5);
    const QString suffix = QStringLiteral(".aowterrain");
    if (!y_text.endsWith(suffix))
    {
        if (error != nullptr)
            *error = QStringLiteral("Invalid terrain tile endpoint extension: %1").arg(endpoint);
        return false;
    }
    y_text.chop(suffix.size());

    bool zoom_ok = false;
    bool x_ok = false;
    bool y_ok = false;
    const int zoom = parts.at(3).toInt(&zoom_ok);
    const quint32 x = parts.at(4).toUInt(&x_ok);
    const quint32 y = y_text.toUInt(&y_ok);

    Aowis::Map::TerrainTileAddress parsed_address;
    parsed_address.zoom = zoom;
    parsed_address.x = x;
    parsed_address.y = y;
    if (!zoom_ok || !x_ok || !y_ok ||
        !Aowis::Map::isValidTerrainDatasetId(parts.at(2)) ||
        !Aowis::Map::isValidTerrainTileAddress(parsed_address))
    {
        if (error != nullptr)
            *error = QStringLiteral("Invalid terrain tile dataset/XYZ address: %1").arg(endpoint);
        return false;
    }

    *dataset = parts.at(2);
    *address = parsed_address;
    return true;
}

bool terrainElevationRequestFromEndpoint(const QString &endpoint,
                                         double *latitude_deg,
                                         double *longitude_deg,
                                         QString *error)
{
    if (latitude_deg == nullptr || longitude_deg == nullptr)
        return false;

    const QUrl url(endpoint);
    if (url.path() != QStringLiteral("/terrain/v1/elevation"))
    {
        if (error != nullptr)
            *error = QStringLiteral("Invalid terrain elevation endpoint: %1").arg(endpoint);
        return false;
    }

    const QUrlQuery query(url);
    bool latitude_ok = false;
    bool longitude_ok = false;
    const double latitude = query.queryItemValue(QStringLiteral("latitude")).toDouble(&latitude_ok);
    const double longitude = query.queryItemValue(QStringLiteral("longitude")).toDouble(&longitude_ok);
    if (!latitude_ok || !longitude_ok || !std::isfinite(latitude) ||
        latitude < -90.0 || latitude > 90.0 || !std::isfinite(longitude) ||
        longitude < -180.0 || longitude > 180.0)
    {
        if (error != nullptr)
            *error = QStringLiteral("Invalid terrain elevation coordinate: %1").arg(endpoint);
        return false;
    }

    *latitude_deg = latitude;
    *longitude_deg = longitude;
    return true;
}
}

InterfaceServerMapStandalone::InterfaceServerMapStandalone(QObject *parent)
    : InterfaceServerMap(parent),
      map_tiles(new MapTiles(this)),
      terrain_data(new Aowis::Map::TerrainData(Aowis::Map::TerrainData::Config(), this))
{
    const int terrain_threads = qMax(1, qMin(
        TerrainRequestThreadCountMaximum, qMax(1, QThread::idealThreadCount() / 2)));
    this->terrain_request_pool.setMaxThreadCount(terrain_threads);
    this->terrain_request_pool.setExpiryTimeout(30000);
    this->terrain_data_initialized =
        this->terrain_data->initialize(&this->terrain_initialization_error);
    if (!this->terrain_data_initialized)
    {
        qWarning() << "Failed to initialize builtin terrain data:"
                   << this->terrain_initialization_error;
    }

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

InterfaceServerMapStandalone::~InterfaceServerMapStandalone()
{
    this->terrain_request_pool.clear();
    this->terrain_request_pool.waitForDone();
}

void InterfaceServerMapStandalone::requestTerrainTile(const QString &endpoint, const QString &key)
{
    if (!this->terrain_data_initialized || this->terrain_data == nullptr)
    {
        const QString error = this->terrain_initialization_error.isEmpty()
            ? QStringLiteral("Builtin terrain data is not initialized")
            : this->terrain_initialization_error;
        emit signalTerrainTileFailed(key, error);
        return;
    }

    QString dataset;
    Aowis::Map::TerrainTileAddress address;
    QString parse_error;
    if (!terrainTileRequestFromEndpoint(endpoint, &dataset, &address, &parse_error))
    {
        emit signalTerrainTileFailed(key, parse_error);
        return;
    }

    InterfaceServerMapStandalone *interface_map = this;
    this->terrain_request_pool.start(QRunnable::create(
        [interface_map, dataset, address, key]
    {
        const Aowis::Map::TerrainTileLookupResult result =
            interface_map->terrain_data->terrainTile(dataset, address);
        const bool success = result.status == Aowis::Map::TerrainTileLookupStatus::Ready &&
                             !result.data.isEmpty();
        const QByteArray data = result.data;
        const QString error = success ? QString() : terrainLookupError(result);

        QMetaObject::invokeMethod(interface_map,
            [interface_map, key, success, data, error]
        {
            if (success)
                emit interface_map->signalTerrainTileDataReceived(key, data);
            else
                emit interface_map->signalTerrainTileFailed(key, error);
        }, Qt::QueuedConnection);
    }));
}

void InterfaceServerMapStandalone::requestTerrainElevation(const QString &endpoint)
{
    if (!this->terrain_data_initialized || this->terrain_data == nullptr)
    {
        const QString error = this->terrain_initialization_error.isEmpty()
            ? QStringLiteral("Builtin terrain data is not initialized")
            : this->terrain_initialization_error;
        emit signalTerrainElevationFailed(error);
        return;
    }

    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    QString parse_error;
    if (!terrainElevationRequestFromEndpoint(
            endpoint, &latitude_deg, &longitude_deg, &parse_error))
    {
        emit signalTerrainElevationFailed(parse_error);
        return;
    }

    InterfaceServerMapStandalone *interface_map = this;
    this->terrain_request_pool.start(QRunnable::create(
        [interface_map, latitude_deg, longitude_deg]
    {
        const Aowis::Map::TerrainElevationLookupResult result =
            interface_map->terrain_data->sampleElevation(
                StandaloneTerrainDefaultDataset, latitude_deg, longitude_deg);
        const bool success =
            result.status == Aowis::Map::TerrainElevationLookupStatus::Ready &&
            result.sample.has_value();
        const QByteArray data = success ? terrainElevationJson(result) : QByteArray();
        const QString error = success ? QString() : terrainElevationLookupError(result);

        QMetaObject::invokeMethod(interface_map,
            [interface_map, success, data, error]
        {
            if (success)
                emit interface_map->signalTerrainElevationDataReceived(data);
            else
                emit interface_map->signalTerrainElevationFailed(error);
        }, Qt::QueuedConnection);
    }));
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
