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

#include "../_enums_structs.h"
#include "../interface_server_map.h"
#include "map_terrain_tile.h"

class MapTerrainRepository : public QObject
{
    Q_OBJECT

public:
    explicit MapTerrainRepository(QObject *parent = nullptr);
    ~MapTerrainRepository() override;

    const MapTerrainTile *tile(const QString &key) const;
    const MapTerrainTile *tile(const QString &dataset, int zoom, quint32 x, quint32 y) const;
    void requestTile(const QString &dataset, int zoom, quint32 x, quint32 y);
    void setMapServerMode(MapServerMode mode);
    void requestUpstreamActivity();
    void cancelUpstreamDownloads();

signals:
    void signalTerrainTileAvailable(const QString &key);
    void signalTerrainTileRetryReady(const QString &key);
    void signalUpstreamActivityChanged(const MapUpstreamActivity &activity);
    void signalUpstreamControlError(const QString &error);
    void signalTerrainTileFailed(const QString &key, const QString &error);

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
#ifndef __EMSCRIPTEN__
    QThreadPool terrain_decode_pool;
#endif
    QCache<QString, MapTerrainTile> cache;
};

#endif // MAP_TERRAIN_REPOSITORY_H
