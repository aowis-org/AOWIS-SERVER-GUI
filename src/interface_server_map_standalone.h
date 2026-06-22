#ifndef INTERFACE_SERVER_MAP_STANDALONE_H
#define INTERFACE_SERVER_MAP_STANDALONE_H

#include <QObject>
#include <QPixmap>
#include <QTimer>

#include "interface_server_map.h"

#include "maptiles.h"

#include <QDebug>

class InterfaceServerMapStandalone : public InterfaceServerMap
{
    Q_OBJECT
public:
    explicit InterfaceServerMapStandalone(QObject *parent = nullptr);
    
    void requestTile(QString endpoint, const QString &key, int x, int y) override;
    
private:
    MapTiles *map_tiles;
    
signals:
    
};

#endif // INTERFACE_SERVER_MAP_STANDALONE_H
