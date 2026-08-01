#ifndef INTERFACE_SERVER_MAP_STANDALONE_H
#define INTERFACE_SERVER_MAP_STANDALONE_H

#include "interface_server_map.h"

#include <aowis/map/maptiles.h>

class InterfaceServerMapStandalone : public InterfaceServerMap
{
    Q_OBJECT

public:
    explicit InterfaceServerMapStandalone(QObject *parent = nullptr);

    void requestTile(const QString &endpoint, const QString &key, int x, int y) override;
    void deleteTiles(quint64 request_id, const QString &provider, int zoom,
                     int tile_x_min, int tile_x_max, int tile_y_min, int tile_y_max) override;

private:
    MapTiles *map_tiles = nullptr;
};

#endif // INTERFACE_SERVER_MAP_STANDALONE_H
