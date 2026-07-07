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
    
    addGroupClassification();
    
    addGroupPosition();
    
    addGroupElevation();
    this->combo_elevation_mode->setItemText(0, "Total Elevation");
    this->label_tank_bottom_offset->setText("Offset");
    this->spin_tank_bottom_offset->setToolTip("Distance from <i>Terrain Elevation</i>.<br>Positive: Above Ground.<br>Negative: Below Ground.");
    this->label_tank_bottom_elevation->setText("Total Elevation");
    
    addGroupDemands();
    addGroupQuality();
    addGroupSimMeas();
    addGroupGraphs();
    
    addGroupHistory();
    
    mainLayout()->addStretch();
}

void EntityInspectorJunction::addGroupClassification()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Classification");
    QGridLayout *grid = new QGridLayout(group);
    
    this->combo_classification_kind = new QComboBox();
    this->combo_classification_kind->addItem("Physical");
    this->combo_classification_kind->addItem("Virtual");
    
    grid->addWidget(this->combo_classification_kind, 0, 0);
    
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
