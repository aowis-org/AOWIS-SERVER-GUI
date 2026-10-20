#include "map/data/map_terrain_repository.h"

#include "services/interface_server_map_rest.h"

#ifdef AOWIS_STANDALONE
#include "services/interface_server_map_standalone.h"
#endif

#include <QDateTime>
#include <QDebug>
#include <QTimer>
#ifndef __EMSCRIPTEN__
#include <QMetaObject>
#include <QRunnable>
#include <QThread>
#endif

namespace
{
constexpr int TerrainCacheMaximumCostKiB = 128 * 1024;
constexpr qint64 TerrainRetryInitialDelayMs = 1000;
constexpr qint64 TerrainRetryMaximumDelayMs = 30000;
#ifndef __EMSCRIPTEN__
constexpr int TerrainDecodeThreadCountMaximum = 4;
#endif
}

MapTerrainRepository::MapTerrainRepository(QObject *parent)
    : QObject(parent)
{
    this->cache.setMaxCost(TerrainCacheMaximumCostKiB);
#ifndef __EMSCRIPTEN__
    const int decode_threads = qMax(1, qMin(TerrainDecodeThreadCountMaximum,
        qMax(1, QThread::idealThreadCount() / 2)));
    this->terrain_decode_pool.setMaxThreadCount(decode_threads);
    this->terrain_decode_pool.setExpiryTimeout(30000);
#endif

    initServerMapInterface();
}

MapTerrainRepository::~MapTerrainRepository()
{
#ifndef __EMSCRIPTEN__
    this->terrain_decode_pool.clear();
    this->terrain_decode_pool.waitForDone();
#endif
}

const MapTerrainTile *MapTerrainRepository::tile(const QString &key) const
{
    return this->cache.object(key);
}

const MapTerrainTile *MapTerrainRepository::tile(const QString &dataset, int zoom,
                                                  quint32 x, quint32 y) const
{
    MapTerrainTileAddress address;
    address.zoom = zoom;
    address.x = x;
    address.y = y;
    return tile(mapTerrainTileKey(dataset, address));
}

void MapTerrainRepository::requestTile(const QString &dataset, int zoom, quint32 x, quint32 y)
{
    MapTerrainTileAddress address;
    address.zoom = zoom;
    address.x = x;
    address.y = y;

    const QString key = mapTerrainTileKey(dataset, address);
    const QString endpoint = mapTerrainTileEndpoint(dataset, address);
    if (key.isEmpty() || endpoint.isEmpty())
    {
        emit signalTerrainTileFailed(key,
                                     QStringLiteral("Invalid terrain dataset or XYZ address"));
        return;
    }

    if (this->cache.contains(key) || this->terrain_pending.contains(key) ||
        this->interface_map == nullptr)
    {
        return;
    }

    const QHash<QString, TerrainFailure>::const_iterator failure =
        this->terrain_failures.constFind(key);
    if (failure != this->terrain_failures.constEnd() &&
        QDateTime::currentMSecsSinceEpoch() < failure->retry_after_msecs)
    {
        return;
    }

    PendingTerrainRequest request;
    request.dataset = dataset;
    request.address = address;
    request.generation = this->terrain_generation;

    this->terrain_pending.insert(key);
    this->terrain_requests_pending.insert(key, request);
    this->interface_map->requestTerrainTile(endpoint, key);
}

const MapTerrainDatasetInfo *MapTerrainRepository::activeDataset() const
{
    return this->active_dataset.has_value() ? &this->active_dataset.value() : nullptr;
}

void MapTerrainRepository::requestDatasetCatalog()
{
    if (this->interface_map != nullptr)
        this->interface_map->requestTerrainDatasets();
}

void MapTerrainRepository::datasetCatalogReceived(const QByteArray &data)
{
    QString error;
    const std::optional<MapTerrainDatasetCatalog> catalog =
        decodeMapTerrainDatasetCatalog(data, &error);
    if (!catalog.has_value())
    {
        datasetCatalogFailed(error);
        return;
    }
    if (!error.isEmpty())
        qWarning().noquote() << error;

    this->dataset_catalog_failure_count = 0;
    const MapTerrainDatasetInfo *info = catalog->find(catalog->default_dataset);
    if (info == nullptr)
    {
        // A configured default without a provider (e.g. only cached tiles of
        // a dataset): its zoom range is unknown, so no relief.
        qWarning().noquote()
            << QStringLiteral("Terrain service describes no dataset %1 (its default); "
                              "drawing the globe without relief")
                   .arg(catalog->default_dataset);
        if (this->active_dataset.has_value())
        {
            this->active_dataset.reset();
            emit signalTerrainDatasetChanged();
        }
        return;
    }

    qInfo().noquote()
        << QStringLiteral("Terrain dataset %1: %2 m nominal resolution, zoom %3 to %4")
               .arg(info->dataset)
               .arg(info->nominal_resolution_m)
               .arg(info->minimum_zoom)
               .arg(info->maximum_zoom);
    this->active_dataset = *info;
    emit signalTerrainDatasetChanged();
}

void MapTerrainRepository::datasetCatalogFailed(const QString &error)
{
    // Retried like a tile, with the same backoff: the map server may still be
    // starting, or terrain may be disabled there for now.
    this->dataset_catalog_failure_count = qMin(this->dataset_catalog_failure_count + 1, 6);
    const qint64 retry_delay = qMin(
        TerrainRetryInitialDelayMs * (qint64(1) << (this->dataset_catalog_failure_count - 1)),
        TerrainRetryMaximumDelayMs);
    qWarning() << "Terrain dataset catalog request failed:" << error;

    const quint64 generation = this->terrain_generation;
    QTimer::singleShot(int(retry_delay), this, [this, generation]
    {
        if (generation == this->terrain_generation && !this->active_dataset.has_value())
            requestDatasetCatalog();
    });
}

void MapTerrainRepository::requestUpstreamActivity()
{
    if (this->interface_map != nullptr)
        this->interface_map->requestTerrainUpstreamActivity();
}

void MapTerrainRepository::cancelUpstreamDownloads()
{
    if (this->interface_map != nullptr)
        this->interface_map->cancelTerrainUpstreamDownloads();
}

void MapTerrainRepository::setMapServerMode(MapServerMode mode)
{
    if (this->map_server_mode == mode)
        return;

    this->map_server_mode = mode;
    initServerMapInterface();
}

void MapTerrainRepository::initServerMapInterface()
{
    if (this->interface_map != nullptr)
    {
        this->interface_map->disconnect(this);
        this->interface_map->deleteLater();
        this->interface_map = nullptr;
    }

    this->terrain_generation++;
    this->terrain_pending.clear();
    this->terrain_requests_pending.clear();
    this->terrain_failures.clear();
    this->dataset_catalog_failure_count = 0;
    if (this->active_dataset.has_value())
    {
        this->active_dataset.reset();
        emit signalTerrainDatasetChanged();
    }

    if (this->map_server_mode == MapServerMode::REST)
    {
        this->interface_map = new InterfaceServerMapREST(this);
    }
    else if (this->map_server_mode == MapServerMode::Standalone)
    {
#ifdef AOWIS_STANDALONE
        this->interface_map = new InterfaceServerMapStandalone(this);
#endif
    }

    if (this->interface_map == nullptr)
        return;

    connect(this->interface_map, &InterfaceServerMap::signalTerrainTileDataReceived,
            this, &MapTerrainRepository::terrainDataReceived);
    connect(this->interface_map, &InterfaceServerMap::signalTerrainTileFailed,
            this, &MapTerrainRepository::terrainFailed);
    connect(this->interface_map, &InterfaceServerMap::signalTerrainUpstreamActivity,
            this, &MapTerrainRepository::signalUpstreamActivityChanged);
    connect(this->interface_map, &InterfaceServerMap::signalUpstreamControlError,
            this, &MapTerrainRepository::signalUpstreamControlError);
    connect(this->interface_map, &InterfaceServerMap::signalTerrainDatasetsReceived,
            this, &MapTerrainRepository::datasetCatalogReceived);
    connect(this->interface_map, &InterfaceServerMap::signalTerrainDatasetsFailed,
            this, &MapTerrainRepository::datasetCatalogFailed);

    // Which dataset to draw and how fine it goes: nothing is requested from
    // the terrain service before this answer.
    requestDatasetCatalog();
}

void MapTerrainRepository::terrainDataReceived(const QString &key, const QByteArray &data)
{
    const QHash<QString, PendingTerrainRequest>::const_iterator request_iterator =
        this->terrain_requests_pending.constFind(key);
    if (!this->terrain_pending.contains(key) || request_iterator == this->terrain_requests_pending.constEnd())
        return;

    if (data.isEmpty())
    {
        terrainFailed(key, QStringLiteral("Terrain response is empty"));
        return;
    }

    const PendingTerrainRequest request = request_iterator.value();
#ifndef __EMSCRIPTEN__
    MapTerrainRepository *repository = this;
    this->terrain_decode_pool.start(QRunnable::create([repository, key, data, request]
    {
        QString error;
        const std::optional<MapTerrainTile> decoded = decodeMapTerrainTile(data, &error);

        QMetaObject::invokeMethod(repository, [repository, key, request, decoded, error]
        {
            repository->finishTerrainDecode(key, request.generation, decoded, error);
        }, Qt::QueuedConnection);
    }));
#else
    QString error;
    const std::optional<MapTerrainTile> decoded = decodeMapTerrainTile(data, &error);
    finishTerrainDecode(key, request.generation, decoded, error);
#endif
}

void MapTerrainRepository::finishTerrainDecode(const QString &key, quint64 generation,
                                                const std::optional<MapTerrainTile> &tile,
                                                const QString &error)
{
    if (generation != this->terrain_generation || !this->terrain_pending.contains(key))
        return;

    const QHash<QString, PendingTerrainRequest>::const_iterator request_iterator =
        this->terrain_requests_pending.constFind(key);
    if (request_iterator == this->terrain_requests_pending.constEnd())
        return;

    const PendingTerrainRequest request = request_iterator.value();
    if (!tile.has_value())
    {
        terrainFailed(key, error.isEmpty()
            ? QStringLiteral("Terrain tile decoding failed")
            : error);
        return;
    }

    if (tile->dataset != request.dataset ||
        tile->address.zoom != request.address.zoom ||
        tile->address.x != request.address.x ||
        tile->address.y != request.address.y)
    {
        terrainFailed(
            key,
            QStringLiteral("Terrain tile embedded dataset/XYZ metadata does not match the request"));
        return;
    }

    this->terrain_pending.remove(key);
    this->terrain_requests_pending.remove(key);

    const qint64 cost_bytes =
        qint64(tile->elevations_m.size()) * qint64(sizeof(float)) +
        qint64(tile->dataset.size()) * qint64(sizeof(QChar)) + qint64(sizeof(MapTerrainTile));
    const qint64 cost_kib_rounded = (cost_bytes + 1023) / 1024;
    const int cost_kib = qMax(
        1, int(qMin<qint64>(cost_kib_rounded, TerrainCacheMaximumCostKiB)));

    if (!this->cache.insert(key, new MapTerrainTile(tile.value()), cost_kib))
    {
        terrainFailed(key, QStringLiteral("Failed to insert terrain tile into memory cache"));
        return;
    }

    this->terrain_failures.remove(key);
    emit signalTerrainTileAvailable(key);
}

void MapTerrainRepository::terrainFailed(const QString &key, const QString &error)
{
    this->terrain_pending.remove(key);
    this->terrain_requests_pending.remove(key);

    TerrainFailure failure = this->terrain_failures.value(key);
    failure.count = qMin(failure.count + 1, 6);

    const qint64 delay_multiplier = qint64(1) << (failure.count - 1);
    const qint64 retry_delay = qMin(
        TerrainRetryInitialDelayMs * delay_multiplier, TerrainRetryMaximumDelayMs);
    failure.retry_after_msecs = QDateTime::currentMSecsSinceEpoch() + retry_delay;

    this->terrain_failures.insert(key, failure);
    qWarning() << "Terrain tile request failed:" << key << error;
    emit signalTerrainTileFailed(key, error);

    const qint64 retry_after_msecs = failure.retry_after_msecs;
    QTimer::singleShot(int(retry_delay), this, [this, key, retry_after_msecs]
    {
        const QHash<QString, TerrainFailure>::const_iterator current_failure =
            this->terrain_failures.constFind(key);
        if (current_failure == this->terrain_failures.constEnd() ||
            current_failure->retry_after_msecs != retry_after_msecs)
        {
            return;
        }

        emit signalTerrainTileRetryReady(key);
    });
}
