#include "entity_inspector_junction.h"

EntityInspectorJunction::EntityInspectorJunction(QWidget *parent)
    : EntityInspectorWidget(parent)
{
    setTitle("Junction J1");
    addGroupGeneral(":/icon/junction.png", "J1");
    
    addGroupPosition();
    
    addGroupElevation();
    this->combo_elevation_mode->setItemText(0, "Total Elevation");
    this->label_tank_bottom_offset->setText("Offset");
    this->spin_tank_bottom_offset->setToolTip("Distance from <i>Terrain Elevation</i>.<br>Positive: Above Ground.<br>Negative: Below Ground.");
    this->label_tank_bottom_elevation->setText("Total Elevation");
    
    addGroupDemands();
    addGroupQuality();
    
    
    this->layoutConfiguration()->addStretch();
    this->layoutSimMeas()->addStretch();
    this->layoutHistory()->addStretch();
}

void EntityInspectorJunction::addGroupQuality()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Quality");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    this->layoutConfiguration()->addWidget(group);
}

