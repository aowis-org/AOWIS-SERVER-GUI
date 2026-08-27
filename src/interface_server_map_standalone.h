#ifndef INTERFACE_SERVER_MAP_STANDALONE_H
#define INTERFACE_SERVER_MAP_STANDALONE_H

#include "interface_server_map.h"

#include <aowis/map/maptiles.h>
#include <aowis/map/terrain_data.h>
#ifdef Q_OS_WIN
#include <QThread>
#endif
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
    void requestMapTileUpstreamActivity() override;
    void requestTerrainUpstreamActivity() override;
    void cancelMapTileUpstreamDownloads() override;
    void cancelTerrainUpstreamDownloads() override;
private:
#ifdef Q_OS_WIN
    void finishTileRequest(const QString &endpoint, const QString &key, int x, int y,
                           const MapTiles::TileRequestResult &result);
    void finishTileDeletion(quint64 request_id, int deleted_count);
    QThread map_tile_thread;
#endif
    MapTiles *map_tiles = nullptr;
    Aowis::Map::TerrainData *terrain_data = nullptr;
    QThreadPool terrain_request_pool;
    bool terrain_data_initialized = false;
    bool terrain_elevation_pending = false;
    QString terrain_initialization_error;
};
#endif // INTERFACE_SERVER_MAP_STANDALONE_H
