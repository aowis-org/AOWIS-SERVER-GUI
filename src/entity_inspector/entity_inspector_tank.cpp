#include "entity_inspector_tank.h"

EntityInspectorTank::EntityInspectorTank(HydraulicData *hydraulic_data, const Tank &tank, QWidget *parent)
    : EntityInspectorWidget(hydraulic_data, parent)
{
    setTitle("Tank T1");
    
    addGroupOverviewImage(":/icon/tower_large.png", "T1");
    
    addGroupGeneral("T1");
    addGroupPosition();
    addGroupElevation();
    addGroupGeometry();
    
    addGroupQuality();
    
    addGroupHistory();
    
    addStretches();
}

void EntityInspectorTank::addGroupGeometry()
{
    this->group_geometry = new GroupBoxCollapsible("Geometry / Levels");
    QGridLayout *grid = new QGridLayout(group_geometry);
    
    connect(this->group_geometry, &GroupBoxCollapsible::signalCollapsed, this, &EntityInspectorTank::onGroupCollapse);
    connect(this->group_geometry, &GroupBoxCollapsible::signalExpanded, this, &EntityInspectorTank::onGroupExpand);
    
    QLabel *label_level_initial = new QLabel("Initial Level");
    this->spin_level_initial = new QDoubleSpinBox();
    this->spin_level_initial->setRange(0.0, 1000.0);
    this->spin_level_initial->setDecimals(2);
    this->spin_level_initial->setSingleStep(0.10);
    this->spin_level_initial->setSuffix(" m");
    
    this->check_overflow = new QCheckBox("Overflow Allowed");
    
    this->combo_geometry_type = new QComboBox();
    // get the state with function: tankGeometryInputType()
    this->combo_geometry_type->addItem(
        "Cylindrical",
        static_cast<int>(TankGeometryInputType::Cylindrical)
    );
    this->combo_geometry_type->addItem(
        "Uniform Area",
        static_cast<int>(TankGeometryInputType::UniformArea)
    );
    this->combo_geometry_type->addItem(
        "Volume at Maximum Level",
        static_cast<int>(TankGeometryInputType::VolumeAtMaximumLevel)
    );
    this->combo_geometry_type->addItem(
        "Volume Curve",
        static_cast<int>(TankGeometryInputType::VolumeCurve)
    );
    
    this->label_spin_diameter = new QLabel("Diameter");
    this->spin_diameter = new QDoubleSpinBox();
    this->spin_diameter->setRange(0.00, 1000.0);
    this->spin_diameter->setDecimals(2);
    this->spin_diameter->setSingleStep(0.10);
    this->spin_diameter->setSuffix(" m");
    
    this->label_spin_area = new QLabel("Area");
    this->label_spin_area->hide();
    this->spin_area = new QDoubleSpinBox();
    this->spin_area->setRange(0.01, 100000000.0);
    this->spin_area->setDecimals(2);
    this->spin_area->setSingleStep(1.00);
    this->spin_area->setSuffix(" m²");
    this->spin_area->hide();
    
    this->label_spin_level_min = new QLabel("Level Min");
    this->spin_level_min = new QDoubleSpinBox;
    spin_level_min->setRange(0.0, 1000.0);
    spin_level_min->setDecimals(2);
    spin_level_min->setSingleStep(0.10);
    spin_level_min->setSuffix(" m");
    
    this->label_spin_level_max = new QLabel("Level Max");
    this->spin_level_max = new QDoubleSpinBox;
    spin_level_max->setRange(0.0, 1000.0);
    spin_level_max->setDecimals(2);
    spin_level_max->setSingleStep(0.10);
    spin_level_max->setSuffix(" m");
    
    this->label_spin_volume_min = new QLabel("Dead Volume at Min");
    this->label_spin_volume_min->setWordWrap(true);
    this->spin_volume_min = new QDoubleSpinBox();
    this->spin_volume_min->setRange(0.0, 100000.0);
    this->spin_volume_min->setDecimals(2);
    this->spin_volume_min->setSingleStep(0.10);
    this->spin_volume_min->setSuffix(" m³");
    this->spin_volume_min->setGroupSeparatorShown(true);
    
    this->label_volume_max = new QLabel("Volume Max");
    this->label_volume_max_label = new QLabel();
    
    this->label_volume_curve = new QLabel("Volume Curve");
    this->label_volume_curve->hide();
    this->combo_volume_curve = new QComboBox();
    this->combo_volume_curve->hide();
    
    connect(this->combo_geometry_type, &QComboBox::currentIndexChanged, this, &EntityInspectorTank::onComboGeometryTypeChange);
    
    grid->addWidget(label_level_initial, 0, 0);
    grid->addWidget(this->spin_level_initial, 0, 1);
    
    grid->addWidget(this->check_overflow, 1, 0, 1, 2);
    
    grid->addWidget(this->combo_geometry_type, 2, 0, 1, 2);
    
    grid->addWidget(this->label_spin_diameter, 3, 0);
    grid->addWidget(this->spin_diameter, 3, 1);
    grid->addWidget(this->label_spin_area, 4, 0);
    grid->addWidget(this->spin_area, 4, 1);
    
    grid->addWidget(this->label_spin_level_min, 5, 0);
    grid->addWidget(this->spin_level_min, 5, 1);
    grid->addWidget(this->label_spin_level_max, 6, 0);
    grid->addWidget(this->spin_level_max, 6, 1);
    
    grid->addWidget(this->label_spin_volume_min, 7, 0);
    grid->addWidget(this->spin_volume_min, 7, 1);
    
    grid->addWidget(this->label_volume_max, 8, 0);
    grid->addWidget(this->label_volume_max_label, 8, 1);
    
    grid->addWidget(this->label_volume_curve, 9, 0);
    grid->addWidget(this->combo_volume_curve, 9, 1);
    
    this->layoutConfiguration()->addWidget(group_geometry);
}

void EntityInspectorTank::addGroupQuality()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Quality Settings");
    QGridLayout *grid = new QGridLayout(group);
    
    this->combo_chem_source = new QComboBox();
    this->combo_chem_source->addItem("None");
    this->combo_chem_source->addItem("Concentration");
    this->combo_chem_source->addItem("Mass Booster");
    this->combo_chem_source->addItem("Flow Paced Booster");
    this->combo_chem_source->addItem("Setpoint Booster");
    
    this->combo_chem_mixing = new QComboBox();
    this->combo_chem_mixing->addItem("Complete Mix");
    this->combo_chem_mixing->addItem("Two-Compartment");
    this->combo_chem_mixing->addItem("First In, First Out");
    this->combo_chem_mixing->addItem("Last In, First Out");
    
    grid->addWidget(this->combo_chem_source);
    grid->addWidget(this->combo_chem_mixing);
    
    this->layoutQuality()->addWidget(group);
}

void EntityInspectorTank::onGroupCollapse(GroupBoxCollapsible *group)
{
    this->geometry_type_current = this->combo_geometry_type->currentIndex();
}
void EntityInspectorTank::onGroupExpand(GroupBoxCollapsible *group)
{
    if (group == this->group_geometry)
        onComboGeometryTypeChange(this->geometry_type_current);
}

void EntityInspectorTank::onComboGeometryTypeChange(int index)
{
    switch (index)
    {
    case 0:
        this->label_spin_diameter->show();
        this->spin_diameter->show();
        this->label_spin_area->hide();
        this->spin_area->hide();
        
        this->label_spin_level_min->show();
        this->spin_level_min->show();
        this->label_spin_level_max->show();
        this->spin_level_max->show();
        
        this->label_spin_volume_min->show();
        this->spin_volume_min->show();
        this->label_volume_max->show();
        this->label_volume_max_label->show();
        
        this->label_volume_curve->hide();
        this->combo_volume_curve->hide();
        
        return;
    case 1:
        this->label_spin_diameter->hide();
        this->spin_diameter->hide();
        this->label_spin_area->show();
        this->spin_area->show();
        
        this->label_spin_level_min->show();
        this->spin_level_min->show();
        this->label_spin_level_max->show();
        this->spin_level_max->show();
        
        this->label_spin_volume_min->show();
        this->spin_volume_min->show();
        this->label_volume_max->show();
        this->label_volume_max_label->show();
        
        this->label_volume_curve->hide();
        this->combo_volume_curve->hide();
        
        return;
    case 2:
        this->label_spin_diameter->hide();
        this->spin_diameter->hide();
        this->label_spin_area->hide();
        this->spin_area->hide();
        
        this->label_spin_level_min->show();
        this->spin_level_min->show();
        this->label_spin_level_max->show();
        this->spin_level_max->show();
        
        this->label_spin_volume_min->show();
        this->spin_volume_min->show();
        this->label_volume_max->show();
        this->label_volume_max_label->show();
        
        this->label_volume_curve->hide();
        this->combo_volume_curve->hide();
        
        return;
    case 3:
        this->label_spin_diameter->hide();
        this->spin_diameter->hide();
        this->label_spin_area->hide();
        this->spin_area->hide();
        
        this->label_spin_level_min->hide();
        this->spin_level_min->hide();
        this->label_spin_level_max->hide();
        this->spin_level_max->hide();
        
        this->label_spin_volume_min->hide();
        this->spin_volume_min->hide();
        this->label_volume_max->hide();
        this->label_volume_max_label->hide();
        
        this->label_volume_curve->show();
        this->combo_volume_curve->show();
        
        return;
    }
}

TankGeometryInputType EntityInspectorTank::tankGeometryInputType()
{
    TankGeometryInputType geometry_input_type = static_cast<TankGeometryInputType>
    (
        this->combo_geometry_type->currentData().toInt()
    );
    
    return geometry_input_type;
}
