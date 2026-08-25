#ifndef INTERFACE_SERVER_MAP_REST_H
#define INTERFACE_SERVER_MAP_REST_H

#include <QSet>

#include "interface_server_map.h"
#include "rest_client.h"

class InterfaceServerMapREST : public InterfaceServerMap
{
    Q_OBJECT

public:
    explicit InterfaceServerMapREST(QObject *parent = nullptr);

    void requestTile(const QString &endpoint, const QString &key, int x, int y) override;
    void requestTerrainTile(const QString &endpoint, const QString &key) override;
    void deleteTiles(quint64 request_id, const QString &provider, int zoom,
                     int tile_x_min, int tile_x_max, int tile_y_min, int tile_y_max) override;

private:
    void initRestConnection();

    RESTClient *rest = nullptr;
    QSet<QString> rest_pending;
    QSet<QString> terrain_pending;
};

#endif // INTERFACE_SERVER_MAP_REST_H
