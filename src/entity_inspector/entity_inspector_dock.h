#ifndef ENTITY_INSPECTOR_DOCK_H
#define ENTITY_INSPECTOR_DOCK_H

#include <QWidget>
#include <QDockWidget>

#include <QVBoxLayout>

#include "entity_inspector_tank.h"
#include "entity_inspector_junction.h"
#include "entity_inspector_pipe.h"
#include "entity_inspector_pump.h"
#include "entity_inspector_valve.h"
#include "entity_inspector_reservoir.h"
#include "entity_inspector_customer_point.h"

#include "../widgets/group_box_collapsible.h"

#include "../_enums_structs.h"
#include "../_sizes.h"
#include "../map/map_models.h"

class EntityInspectorDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit EntityInspectorDock(QWidget *parent = nullptr);
    
    //void showEntity(MapNetworkStructs &entity);
    
    void clearEntity();
    
    void showEntityTank();
    void showEntityJunction();
    void showEntityPipe();
    void showEntityPump();
    void showEntityValve();
    void showEntityReservoir();
    void showEntityCustomerPoint();
    
private:
    void setInspector(EntityInspectorWidget *inspector);
    
    EntityInspectorWidget *widget_current = nullptr;
    
    HeadlossFormulas headloss_formulas_current =
        HeadlossFormulas(HeadlossFormula::HazenWilliams);
    
public slots:
    virtual void onHeadlossFormulaChanged(HeadlossFormulas formulas);
    
};

#endif // ENTITY_INSPECTOR_DOCK_H
