#ifndef MAP_INSPECTOR_DOCK_H
#define MAP_INSPECTOR_DOCK_H

#include <QDockWidget>

#include <QPushButton>

#include "_enums_structs.h"
#include "map_network_structs.h"

class MapInspectorDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit MapInspectorDock(QWidget *parent = nullptr);
    
    //void showEntity(MapNetworkStructs &entity);
    
};

#endif // MAP_INSPECTOR_DOCK_H
