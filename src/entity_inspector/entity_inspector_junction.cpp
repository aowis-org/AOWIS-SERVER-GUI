#include "entity_inspector_junction.h"

EntityInspectorJunction::EntityInspectorJunction(HydraulicData *hydraulic_data, const QUuid &uuid, QWidget *parent)
    : EntityInspectorWidget(hydraulic_data, parent)
{
    addGroupOverviewImage(":/icon/junction.png", QString());
    addGroupGeneral(QString());
    addGroupPosition();
    bindHydraulicNode(InfrastructureEntity::Junction, uuid, "Junction");

    addGroupElevation();
    this->combo_elevation_mode->setItemText(0, "Total Elevation");
    this->label_tank_bottom_offset->setText("Offset");
    this->spin_tank_bottom_offset->setToolTip(
        "Distance from <i>Terrain Elevation</i>.<br>Positive: Above Ground.<br>Negative: Below Ground.");
    this->label_tank_bottom_elevation->setText("Total Elevation");

    addGroupDemands();
    addGroupQuality();
    addStretches();
}

void EntityInspectorJunction::addGroupQuality()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Quality Settings");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    this->layoutQuality()->addWidget(group);
}

