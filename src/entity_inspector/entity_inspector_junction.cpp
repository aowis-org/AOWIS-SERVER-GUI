#include "entity_inspector_junction.h"

EntityInspectorJunction::EntityInspectorJunction(QWidget *parent)
    : EntityInspectorWidget(parent),
    layout(new QVBoxLayout()),
    label(new QLabel())
{
    setLayout(this->layout);
    
    this->layout->addWidget(this->label);
    setTitle("Junction J1");
    addGroupGeneral(":/icon/junction.png", "J1");
    
    this->location_inspector = new EntityInspectorLocation(this);
    this->location_inspector->addGroupPosition(mainLayout());
    this->location_inspector->addGroupElevation(mainLayout());
    
    
    addGroupDemands();
    addGroupQuality();
    addGroupSimMeas();
    addGroupGraphs();
    
    mainLayout()->addStretch();
}

void EntityInspectorJunction::addGroupDemands()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Demands");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    mainLayout()->addWidget(group);
}

void EntityInspectorJunction::addGroupQuality()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Quality");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    mainLayout()->addWidget(group);
}

void EntityInspectorJunction::addGroupSimMeas()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Simulation / Measurements");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    mainLayout()->addWidget(group);
}

void EntityInspectorJunction::addGroupGraphs()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Graphs");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    mainLayout()->addWidget(group);
}
