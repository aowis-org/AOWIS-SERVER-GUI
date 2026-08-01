#include "entity_inspector_reservoir.h"

EntityInspectorReservoir::EntityInspectorReservoir(HydraulicData *hydraulic_data, const QUuid &uuid, QWidget *parent)
    : EntityInspectorWidget(hydraulic_data, parent)
{
    addGroupOverviewImage(":/icon/lake.png", QString());
    addGroupGeneral(QString());
    addGroupPosition();
    bindHydraulicNode(InfrastructureEntity::Reservoir, uuid, "Reservoir");
    addGroupElevation();

    addGroupDemands();
    addGroupQuality();
    addGroupHistory();
    addStretches();
}

void EntityInspectorReservoir::addGroupDemands()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Demands");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    this->layoutConfiguration()->addWidget(group);
}

void EntityInspectorReservoir::addGroupQuality()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Quality Settings");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    this->layoutQuality()->addWidget(group);
}

