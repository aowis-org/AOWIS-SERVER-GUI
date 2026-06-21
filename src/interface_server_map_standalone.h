#ifndef INTERFACE_SERVER_MAP_STANDALONE_H
#define INTERFACE_SERVER_MAP_STANDALONE_H

#include <QObject>

#include "interface_server_map.h"

class InterfaceServerMapStandalone : public InterfaceServerMap
{
    Q_OBJECT
public:
    explicit InterfaceServerMapStandalone(QObject *parent = nullptr);
    
    void requestTile(const QString &endpoint, const QString &key, int x, int y) override;

signals:
    
};

#endif // INTERFACE_SERVER_MAP_STANDALONE_H
