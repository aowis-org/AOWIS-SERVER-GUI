#ifndef MAP_TILE_REPOSITORY_H
#define MAP_TILE_REPOSITORY_H

#include <QCache>
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

    QPixmap *tile(const QString &key) const;
    void requestTile(const QString &endpoint, const QString &key, int x, int y);
    void setMapServerMode(MapServerMode mode);

signals:
    void signalTileAvailable(const QString &key);
    void signalTileFailed(const QString &key);

private:
    void initServerMapInterface();
    void tileReceived(const QString &key, QPixmap *pixmap);

#ifdef AOWIS_STANDALONE
    MapServerMode map_server_mode = MapServerMode::Standalone;
#else
    MapServerMode map_server_mode = MapServerMode::REST;
#endif

    InterfaceServerMap *interface_map = nullptr;
    QSet<QString> tiles_pending;
    QCache<QString, QPixmap> cache;
};

#endif // MAP_TILE_REPOSITORY_H
