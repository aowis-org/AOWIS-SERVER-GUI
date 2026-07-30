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

private:
    void initRestConnection();

    RESTClient *rest = nullptr;
    QSet<QString> rest_pending;
};

#endif // INTERFACE_SERVER_MAP_REST_H
