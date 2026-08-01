#ifndef MAP_TILE_REPOSITORY_H
#define MAP_TILE_REPOSITORY_H

#include <QCache>
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>

#include "../_enums_structs.h"
#include "../interface_server_map.h"

class MapTileRepository : public QObject
{
    Q_OBJECT

public:
    explicit MapTileRepository(QObject *parent = nullptr);

    const QPixmap *tile(const QString &key) const;
    void requestTile(const QString &endpoint, const QString &key, int x, int y);
    void deleteTiles(const QString &provider, int zoom,
                     int tile_x_min, int tile_x_max, int tile_y_min, int tile_y_max);
    void setMapServerMode(MapServerMode mode);

signals:
    void signalTileAvailable(const QString &key);
    void signalTileRetryReady(const QString &key);

private:
    struct TileFailure
    {
        int count = 0;
        qint64 retry_after_msecs = 0;
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
    void invalidateTiles(const PendingTileDeletion &deletion);
    bool tileDeletionPending(const QString &key, int x, int y) const;
    void finishTileDeletion(quint64 request_id, const QString &error = QString());
    void tileReceived(const QString &key, const QPixmap &pixmap);
    void tileFailed(const QString &key);

#ifdef AOWIS_STANDALONE
    MapServerMode map_server_mode = MapServerMode::Standalone;
#else
    MapServerMode map_server_mode = MapServerMode::REST;
#endif

    InterfaceServerMap *interface_map = nullptr;
    QSet<QString> tiles_pending;
    QSet<QString> tiles_invalidated_while_pending;
    QHash<QString, TileFailure> tile_failures;
    QHash<quint64, PendingTileDeletion> tile_deletions_pending;
    quint64 next_tile_deletion_id = 1;
    QCache<QString, QPixmap> cache;
};

#endif // MAP_TILE_REPOSITORY_H
