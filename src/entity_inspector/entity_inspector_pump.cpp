#include "entity_inspector_pump.h"

EntityInspectorPump::EntityInspectorPump(HydraulicData *hydraulic_data, Pump pump, QWidget *parent)
    : EntityInspectorWidget(hydraulic_data, parent),
    pump(pump)
{
    setTitle("Pump PU1");
    
    addGroupOverviewImage(":/icon/pump.png", "PU1");
    
    addGroupGeneral("PU1");
    
    addGroupEndpoints();
    
    addGroupControls();
    addGroupEnergyCostInput();
    
    addGroupEnergy();
    
    addGroupHistory();
    
    addStretches();
}

void EntityInspectorPump::addGroupControls()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Controls");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *label_type = new QLabel("Type");
    this->combo_type = new QComboBox();
    this->combo_type->addItem("Constant power");
    this->combo_type->addItem("1-point curve (design point)");
    this->combo_type->addItem("3-point curve (standard curve)");
    this->combo_type->addItem("From Library");
    
    QLabel *label_speed_initial = new QLabel("Initial Speed");
    this->spin_speed_initial = new QDoubleSpinBox();
    this->spin_speed_initial->setDecimals(2);
    this->spin_speed_initial->setSingleStep(0.05);
    this->spin_speed_initial->setRange(0.0, 10.0);
    this->spin_speed_initial->setValue(1.0);
    this->spin_speed_initial->setSuffix(" ×");
    this->spin_speed_initial->setSpecialValueText("0.00 × (off)");
    
    QLabel *label_status_initial = new QLabel("Initial Status");
    this->combo_status_initial = new QComboBox();
    this->combo_status_initial->addItem("On");
    this->combo_status_initial->addItem("Off");
    
    QLabel *label_speed_pattern = new QLabel("Speed Pattern");
    this->combo_speed_pattern = new QComboBox();
    this->combo_speed_pattern->addItem("Fixed speed");
    
    
    QLabel *label_controls = new QLabel("Control Type");
    this->combo_controls = new QComboBox();
    this->combo_controls->addItem("None");
    this->combo_controls->addItem("Level-based");
    this->combo_controls->addItem("Time-based");
    
    grid->addWidget(label_type, 0, 0);
    grid->addWidget(this->combo_type, 0, 1);
    
    grid->addWidget(label_speed_initial, 1, 0);
    grid->addWidget(this->spin_speed_initial, 1, 1);
    
    grid->addWidget(label_status_initial, 2, 0);
    grid->addWidget(this->combo_status_initial, 2, 1);
    
    grid->addWidget(label_speed_pattern, 3, 0);
    grid->addWidget(this->combo_speed_pattern, 3, 1);
    
    grid->addWidget(label_controls, 4, 0);
    grid->addWidget(this->combo_controls, 4, 1);
    
    this->layoutConfiguration()->addWidget(group);
}

void EntityInspectorPump::addGroupEnergyCostInput()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Energy Cost Input");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *label_efficiency_curve = new QLabel("Efficiency Curve");
    this->combo_efficiency_curve = new QComboBox();
    this->combo_efficiency_curve->addItem("Constant 75%");
    
    QLabel *label_energy_price = new QLabel("Energy Price");
    this->spin_energy_price = new QDoubleSpinBox(this);
    this->spin_energy_price->setDecimals(4);
    this->spin_energy_price->setSingleStep(0.01);
    this->spin_energy_price->setRange(0.0, 1000.0);
    this->spin_energy_price->setValue(0.0);
    this->spin_energy_price->setSuffix(" /kWh");
    this->spin_energy_price->setSpecialValueText("Not priced");
    
    QLabel *label_price_pattern = new QLabel("Price Pattern");
    this->combo_price_pattern = new QComboBox();
    this->combo_price_pattern->addItem("Constant");
    
    grid->addWidget(label_efficiency_curve, 0, 0);
    grid->addWidget(this->combo_efficiency_curve, 0, 1);
    
    grid->addWidget(label_energy_price, 1, 0);
    grid->addWidget(this->spin_energy_price, 1, 1);
    
    grid->addWidget(label_price_pattern, 2, 0);
    grid->addWidget(this->combo_price_pattern, 2, 1);
    
    this->layoutConfiguration()->addWidget(group);
}

void EntityInspectorPump::addGroupEnergy()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Energy");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    this->layoutConfiguration()->addWidget(group);
}

