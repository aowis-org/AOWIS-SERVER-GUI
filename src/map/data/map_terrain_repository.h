#ifndef MAP_TERRAIN_REPOSITORY_H
#define MAP_TERRAIN_REPOSITORY_H

#include <QCache>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#ifndef __EMSCRIPTEN__
#include <QThreadPool>
#endif

#include "common/_enums_structs.h"
#include "services/interface_server_map.h"
#include "map/data/map_terrain_dataset.h"
#include "map/data/map_terrain_tile.h"

#include <optional>

class MapTerrainRepository : public QObject
{
    Q_OBJECT

public:
    explicit MapTerrainRepository(QObject *parent = nullptr);
    ~MapTerrainRepository() override;

    const MapTerrainTile *tile(const QString &key) const;
    const MapTerrainTile *tile(const QString &dataset, int zoom, quint32 x, quint32 y) const;
    void requestTile(const QString &dataset, int zoom, quint32 x, quint32 y);
    // The dataset the map draws: the terrain service's default dataset, as
    // described by its catalog (GET /terrain/v1/datasets). nullptr until the
    // catalog arrived, or when it does not describe its default dataset:
    // then no relief is drawn rather than guessing a provider.
    const MapTerrainDatasetInfo *activeDataset() const;
    void setMapServerMode(MapServerMode mode);
    void requestUpstreamActivity();
    void cancelUpstreamDownloads();

signals:
    void signalTerrainTileAvailable(const QString &key);
    void signalTerrainTileRetryReady(const QString &key);
    void signalUpstreamActivityChanged(const MapUpstreamActivity &activity);
    void signalUpstreamControlError(const QString &error);
    void signalTerrainTileFailed(const QString &key, const QString &error);
    // activeDataset() changed: its catalog arrived, or the map server mode
    // changed and the catalog is requested again.
    void signalTerrainDatasetChanged();

private:
    struct TerrainFailure
    {
        int count = 0;
        qint64 retry_after_msecs = 0;
    };

    struct PendingTerrainRequest
    {
        QString dataset;
        MapTerrainTileAddress address;
        quint64 generation = 0;
    };

    void initServerMapInterface();
    void requestDatasetCatalog();
    void datasetCatalogReceived(const QByteArray &data);
    void datasetCatalogFailed(const QString &error);
    void terrainDataReceived(const QString &key, const QByteArray &data);
    void terrainFailed(const QString &key, const QString &error);
    void finishTerrainDecode(const QString &key, quint64 generation,
                             const std::optional<MapTerrainTile> &tile,
                             const QString &error);

#ifdef AOWIS_STANDALONE
    MapServerMode map_server_mode = MapServerMode::Standalone;
#else
    MapServerMode map_server_mode = MapServerMode::REST;
#endif

    InterfaceServerMap *interface_map = nullptr;
    QSet<QString> terrain_pending;
    QHash<QString, PendingTerrainRequest> terrain_requests_pending;
    QHash<QString, TerrainFailure> terrain_failures;
    quint64 terrain_generation = 0;
    std::optional<MapTerrainDatasetInfo> active_dataset;
    int dataset_catalog_failure_count = 0;
#ifndef __EMSCRIPTEN__
    QThreadPool terrain_decode_pool;
#endif
    QCache<QString, MapTerrainTile> cache;
};

#endif // MAP_TERRAIN_REPOSITORY_H
