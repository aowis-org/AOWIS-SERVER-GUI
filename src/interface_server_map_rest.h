#ifndef INTERFACE_SERVER_MAP_REST_H
#define INTERFACE_SERVER_MAP_REST_H

#include <QObject>

#include "interface_server_map.h"
#include "rest_client.h"

class InterfaceServerMapREST : public InterfaceServerMap
{
    Q_OBJECT
public:
    explicit InterfaceServerMapREST(QObject *parent = nullptr);

signals:
};

#endif // INTERFACE_SERVER_MAP_REST_H
