#include "entity_inspector_valve.h"

EntityInspectorValve::EntityInspectorValve(QWidget *parent)
    : EntityInspectorWidget(parent),
    layout(new QVBoxLayout()),
    label(new QLabel())
{
    setLayout(this->layout);
    
    this->layout->addWidget(this->label);
    setTitle("Valve V1");
    addGroupGeneral(":/icon/valve.png", "V1");
    
    addGroupEndpoints();
    
    addGroupSimMeas();
    addGroupGraphs();
    
    mainLayout()->addStretch();
}

void EntityInspectorValve::addGroupSimMeas()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Simulation / Measurements");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    mainLayout()->addWidget(group);
}

void EntityInspectorValve::addGroupGraphs()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Graphs");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    mainLayout()->addWidget(group);
}
