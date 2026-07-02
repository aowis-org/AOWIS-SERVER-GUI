#ifndef ENTITY_INSPECTOR_DOCK_H
#define ENTITY_INSPECTOR_DOCK_H

#include <QWidget>
#include <QDockWidget>

#include <QScrollArea>

#include <QVBoxLayout>

#include "entity_inspector_tank.h"

#include "../widgets/group_box_collapsible.h"

#include "../_enums_structs.h"
#include "../_sizes.h"
#include "../map_network_structs.h"

class EntityInspectorDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit EntityInspectorDock(QWidget *parent = nullptr);
    
    //void showEntity(MapNetworkStructs &entity);
    
    void clearEntity();
    
    void showEntityTank();
    
    
private:
    void setInspector(QWidget *inspector);
    
    QScrollArea *scroll = nullptr;
    QWidget *widget_current = nullptr;
    
};

#endif // ENTITY_INSPECTOR_DOCK_H
