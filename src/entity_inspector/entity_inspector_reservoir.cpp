#include "entity_inspector_reservoir.h"

EntityInspectorReservoir::EntityInspectorReservoir(HydraulicData *hydraulic_data, QWidget *parent)
    : EntityInspectorWidget(hydraulic_data, parent)
{
    setTitle("Reservoir R1");
    
    addGroupOverviewImage(":/icon/lake.png", "R1");
    
    addGroupGeneral("R1");
    
    addGroupPosition();
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

