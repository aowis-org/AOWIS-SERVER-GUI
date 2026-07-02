#include "entity_inspector_location.h"

EntityInspectorLocation::EntityInspectorLocation(QObject *parent)
    : QObject{parent}
{
    
}



void EntityInspectorLocation::addGroupElevation(QVBoxLayout *layout)
{
    this->group_elevation = new GroupBoxCollapsible("Elevation");
    QGridLayout *grid = new QGridLayout(group_elevation);
    
    this->combo_elevation_mode = new QComboBox();
    this->combo_elevation_mode->addItem("Tank Bottom Elevation");
    this->combo_elevation_mode->addItem("Terrain Elevation + Offset");
    
    this->button_terrain_elevation = new QPushButton("Terrain Elevation from GIS");
    this->button_terrain_elevation->setToolTip(
        "Uses terrain elevation from GIS/DEM data.<br>Accuracy depends on the dataset and local terrain."
    );
    
    this->label_terrain_elevation = new QLabel("Terrain elevation");
    this->spin_terrain_elevation = new QDoubleSpinBox;
    spin_terrain_elevation->setRange(-10000.0, 10000.0);
    spin_terrain_elevation->setDecimals(3);
    spin_terrain_elevation->setSingleStep(0.10);
    spin_terrain_elevation->setSuffix(" m");
    
    this->label_tank_bottom_offset = new QLabel("Tank bottom offset");
    this->label_tank_bottom_offset->setWordWrap(true);
    this->spin_tank_bottom_offset = new QDoubleSpinBox;
    spin_tank_bottom_offset->setRange(-100.0, 200.0);
    spin_tank_bottom_offset->setDecimals(3);
    spin_tank_bottom_offset->setSingleStep(0.10);
    spin_tank_bottom_offset->setSuffix(" m");
    this->spin_tank_bottom_offset->setToolTip(
        "Tank bottom relative to terrain. Positive = above ground, negative = below ground."
    );
    
    QLabel *label_tank_bottom_elevation = new QLabel("Tank bottom elevation");
    label_tank_bottom_elevation->setWordWrap(true);
    this->spin_tank_bottom_elevation = new QDoubleSpinBox;
    spin_tank_bottom_elevation->setRange(-10000.0, 10000.0);
    spin_tank_bottom_elevation->setDecimals(3);
    spin_tank_bottom_elevation->setSingleStep(0.10);
    spin_tank_bottom_elevation->setSuffix(" m");
    
    grid->addWidget(this->combo_elevation_mode, 0, 0, 1, 2);
    grid->addWidget(button_terrain_elevation, 1, 0, 1, 2);
    grid->addWidget(this->label_terrain_elevation, 2, 0);
    grid->addWidget(this->spin_terrain_elevation, 2, 1);
    grid->addWidget(this->label_tank_bottom_offset, 3, 0);
    grid->addWidget(this->spin_tank_bottom_offset, 3, 1);
    grid->addWidget(label_tank_bottom_elevation, 4, 0);
    grid->addWidget(this->spin_tank_bottom_elevation, 4, 1);
    
    connect(this->combo_elevation_mode, &QComboBox::currentIndexChanged, this, &EntityInspectorLocation::onElevationModeSignalChanged);
    
    this->combo_elevation_mode->setCurrentIndex(1);
    
    connect(this->spin_terrain_elevation, &QDoubleSpinBox::valueChanged, this, &EntityInspectorLocation::onElevationCalc);
    connect(this->spin_tank_bottom_offset, &QDoubleSpinBox::valueChanged, this, &EntityInspectorLocation::onElevationCalc);
    
    connect(group_elevation, &GroupBoxCollapsible::signalCollapsed, this, &EntityInspectorLocation::onGroupCollapse);
    connect(group_elevation, &GroupBoxCollapsible::signalExpanded, this, &EntityInspectorLocation::onGroupExpand);
    
    layout->addWidget(group_elevation);
}

void EntityInspectorLocation::onGroupCollapse(GroupBoxCollapsible *group)
{
    this->elevation_mode_current = this->combo_elevation_mode->currentIndex();
}
void EntityInspectorLocation::onGroupExpand(GroupBoxCollapsible *group)
{
    if (group == this->group_elevation)
        onElevationModeSignalChanged(this->elevation_mode_current);
}

void EntityInspectorLocation::onElevationModeSignalChanged(int index)
{
    switch (index)
    {
    case 0:
        this->button_terrain_elevation->hide();
        this->label_terrain_elevation->hide();
        this->spin_terrain_elevation->hide();
        this->label_tank_bottom_offset->hide();
        this->spin_tank_bottom_offset->hide();
        this->spin_tank_bottom_elevation->setReadOnly(false);
        this->spin_tank_bottom_elevation->setToolTip("");
        return;
    case 1:
        this->button_terrain_elevation->show();
        this->label_terrain_elevation->show();
        this->spin_terrain_elevation->show();
        this->label_tank_bottom_offset->show();
        this->spin_tank_bottom_offset->show();
        this->spin_tank_bottom_elevation->setReadOnly(true);
        this->spin_tank_bottom_elevation->setToolTip(
            "Calculated automatically from <i>Terrain Elevation</i> + <i>Offset</i>"
            );
        onElevationCalc();
        return;
    }
}

void EntityInspectorLocation::onElevationCalc()
{
    double ground = this->spin_terrain_elevation->value();
    double offset = this->spin_tank_bottom_offset->value();
    this->spin_tank_bottom_elevation->setValue(ground + offset);
}
