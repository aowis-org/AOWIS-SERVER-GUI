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
    void deleteTiles(const QString &key_prefix, int tile_count, int tile_x_min, int tile_x_max, int tile_y_min, int tile_y_max);
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

    void initServerMapInterface();
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
    QCache<QString, QPixmap> cache;
};

#endif // MAP_TILE_REPOSITORY_H
