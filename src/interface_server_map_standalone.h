#ifndef INTERFACE_SERVER_MAP_STANDALONE_H
#define INTERFACE_SERVER_MAP_STANDALONE_H

#include "interface_server_map.h"

#include <aowis/map/maptiles.h>
#include <aowis/map/terrain_data.h>

#include <QThreadPool>

class InterfaceServerMapStandalone : public InterfaceServerMap
{
    Q_OBJECT

public:
    explicit InterfaceServerMapStandalone(QObject *parent = nullptr);
    ~InterfaceServerMapStandalone() override;

    void requestTile(const QString &endpoint, const QString &key, int x, int y) override;
    void requestTerrainTile(const QString &endpoint, const QString &key) override;
    void requestTerrainElevation(const QString &endpoint) override;
    void deleteTiles(quint64 request_id, const QString &provider, int zoom,
                     int tile_x_min, int tile_x_max, int tile_y_min, int tile_y_max) override;

private:
    MapTiles *map_tiles = nullptr;
    Aowis::Map::TerrainData *terrain_data = nullptr;
    QThreadPool terrain_request_pool;
    bool terrain_data_initialized = false;
    QString terrain_initialization_error;
};

#endif // INTERFACE_SERVER_MAP_STANDALONE_H
