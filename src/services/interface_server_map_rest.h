#ifndef INTERFACE_SERVER_MAP_REST_H
#define INTERFACE_SERVER_MAP_REST_H

#include <QSet>

#include "services/interface_server_map.h"
#include "services/rest_client.h"

class InterfaceServerMapREST : public InterfaceServerMap
{
    Q_OBJECT

public:
    explicit InterfaceServerMapREST(QObject *parent = nullptr);

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
    void initRestConnection();

    RESTClient *rest = nullptr;
    QSet<QString> rest_pending;
    QSet<QString> terrain_pending;
    bool terrain_elevation_pending = false;
    bool map_upstream_activity_pending = false;
    bool terrain_upstream_activity_pending = false;
};

#endif // INTERFACE_SERVER_MAP_REST_H
