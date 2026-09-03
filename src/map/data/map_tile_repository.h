#ifndef MAP_TILE_REPOSITORY_H
#define MAP_TILE_REPOSITORY_H

#include <QByteArray>
#include <QCache>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QImage>
#include <QThreadPool>
#include <QTimer>

#include "common/_enums_structs.h"
#include "services/interface_server_map.h"

class MapTileRepository : public QObject
{
    Q_OBJECT

public:
    explicit MapTileRepository(QObject *parent = nullptr);
    ~MapTileRepository() override;

    const QPixmap *tile(const QString &key) const;
    quint64 beginTileRequestBatch(const void *owner, const QString &layout_key);
    void requestTile(const QString &endpoint, const QString &key, int x, int y,
                     int priority, quint64 request_batch, bool foreground = true);
    void deleteTiles(const QString &provider, int zoom,
                     int tile_x_min, int tile_x_max, int tile_y_min, int tile_y_max);
    void setMapServerMode(MapServerMode mode);
    void requestUpstreamActivity();
    void cancelUpstreamDownloads();

signals:
    void signalTileAvailable(const QString &key);
    void signalTileRetryReady(const QString &key);
    void signalTilesDeleted();
    void signalUpstreamActivityChanged(const MapUpstreamActivity &activity);
    void signalUpstreamControlError(const QString &error);

private:
    struct TileFailure
    {
        int count = 0;
        qint64 retry_after_msecs = 0;
    };

    struct PendingTileRequest
    {
        QString endpoint;
        int x = 0;
        int y = 0;
        int priority = 0;
        quint64 request_batch = 0;
        quint64 request_sequence = 0;
        bool foreground = true;
    };

    struct PendingTileDeletion
    {
        QString key_prefix;
        int tile_count = 0;
        int tile_x_min = 0;
        int tile_x_max = -1;
        int tile_y_min = 0;
        int tile_y_max = -1;
    };

    void initServerMapInterface();
    int tileRequestInFlightLimit() const;
    int backgroundTileRequestInFlightLimit() const;
    void scheduleTileRequestDispatch();
    void processTileRequestQueue();
    void trimTileRequestQueue();
    void invalidateTiles(const PendingTileDeletion &deletion);
    bool tileDeletionPending(const QString &key, int x, int y) const;
    void finishTileDeletion(quint64 request_id, const QString &error = QString());
    void tileDataReceived(const QString &key, const QByteArray &data);
    void finishTileDecode(const QString &key, quint64 generation, const QImage &image);
    void tileFailed(const QString &key);

#ifdef AOWIS_STANDALONE
    MapServerMode map_server_mode = MapServerMode::Standalone;
#else
    MapServerMode map_server_mode = MapServerMode::REST;
#endif

    InterfaceServerMap *interface_map = nullptr;
    QSet<QString> tiles_pending;
    QSet<QString> tiles_in_flight;
    QSet<QString> background_tiles_in_flight;
    QHash<QString, PendingTileRequest> tile_requests_queued;
    QTimer *tile_request_timer = nullptr;
    quint64 next_tile_request_batch = 1;
    quintptr active_tile_request_owner = 0;
    QString active_tile_request_layout_key;
    quint64 active_tile_request_batch = 0;
    quint64 next_tile_request_sequence = 1;
    QSet<QString> tiles_invalidated_while_pending;
    QHash<QString, TileFailure> tile_failures;
    QHash<quint64, PendingTileDeletion> tile_deletions_pending;
    quint64 next_tile_deletion_id = 1;
    quint64 tile_generation = 0;
    QThreadPool tile_decode_pool;
    QCache<QString, QPixmap> cache;
};

#endif // MAP_TILE_REPOSITORY_H
