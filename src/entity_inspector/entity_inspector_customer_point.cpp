#include "entity_inspector/entity_inspector_customer_point.h"

EntityInspectorCustomerPoint::EntityInspectorCustomerPoint(HydraulicData *hydraulic_data, NetworkHydraulicCustomerPoint customer_point, QWidget *parent)
    : EntityInspectorWidget(hydraulic_data, parent),
    customer_point(customer_point)
{
    setTitle("Customer Point C1");
    
    addGroupOverviewImage(":/icon/customer.png", "C1");
    
    addGroupGeneral("C1");
    
    addGroupConnections();
    addGroupDemands();
    addGroupGraphs();
    
    addGroupHistory();
    
    addStretches();
}

void EntityInspectorCustomerPoint::addGroupConnections()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Connections");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *label_connection_pipe = new QLabel("Pipe");
    this->combo_connection_pipe = new QComboBox();
    
    QLabel *label_connection_junction = new QLabel("Junction");
    this->combo_connection_junction = new QComboBox();
    
    grid->addWidget(label_connection_pipe, 0, 0);
    grid->addWidget(this->combo_connection_pipe, 0, 1);
    
    grid->addWidget(label_connection_junction, 1, 0);
    grid->addWidget(this->combo_connection_junction, 1, 1);
    
    this->layoutConfiguration()->addWidget(group);
}

void EntityInspectorCustomerPoint::addGroupGraphs()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Graphs");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *label_devnote = new QLabel("This section could show Water Meter readings (manual or smart auto)");
    label_devnote->setWordWrap(true);
    
    grid->addWidget(label_devnote, 0, 0);
    
    this->layoutSimMeas()->addWidget(group);
}
