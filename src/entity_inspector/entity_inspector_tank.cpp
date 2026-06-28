#include "entity_inspector_tank.h"

EntityInspectorTank::EntityInspectorTank(QWidget *parent)
    : QWidget{parent},
    layout(new QVBoxLayout()),
    label(new QLabel())
{
    setLayout(this->layout);
    
    this->layout->addWidget(this->label);
    this->label->setText("<b>Tank T1</b>");
    
    addGroupGeneral();
    addGroupPosition();
    addGroupElevation();
    addGroupGeometry();
    addGroupQuality();
    addGroupSimMeas();
    addGroupGraphs();
    
    this->layout->addStretch();
}

void EntityInspectorTank::addGroupGeneral()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("General");
    QGridLayout *grid = new QGridLayout(group);
    
    QPixmap pixmap(":/icon/tower_large.png");
    this->picture = new QLabel();
    this->picture->setPixmap(pixmap.scaledToHeight(
        Sizes::SidebarRightImageHeight,
        Qt::SmoothTransformation
    ));
    this->picture->setAlignment(Qt::AlignCenter);
    
    QLabel *label_name = new QLabel("Name");
    this->line_name = new QLineEdit();
    connect(this->line_name, &QLineEdit::textChanged, this, [this]
    {
        if (this->line_name->text() == "T34")
        {
            QPixmap pixmap(":/icon/tank.png");
            this->picture->setPixmap(pixmap.scaledToHeight(
                Sizes::SidebarRightImageHeight,
                Qt::SmoothTransformation
            ));
            this->picture->setAlignment(Qt::AlignCenter);
        }
        else
        {
            QPixmap pixmap(":/icon/tower_large.png");
            this->picture->setPixmap(pixmap.scaledToHeight(
                Sizes::SidebarRightImageHeight,
                Qt::SmoothTransformation
            ));
        }
    });
    
    grid->addWidget(this->picture, 0, 0, 1, 2);
    grid->addWidget(label_name, 1, 0);
    grid->addWidget(this->line_name, 2, 0, 1, 2);
    
    this->layout->addWidget(group);
}

void EntityInspectorTank::addGroupPosition()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Position");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *label_latitude = new QLabel("Latitude");
    this->spin_latitude = new QDoubleSpinBox;
    this->spin_latitude->setRange(-90.0, 90.0);
    this->spin_latitude->setDecimals(6);
    this->spin_latitude->setSingleStep(0.000001);
    this->spin_latitude->setSuffix(" °");
    this->spin_latitude->setAccelerated(true);
    
    QLabel *label_longitude = new QLabel("Longitude");
    this->spin_longitude = new QDoubleSpinBox;
    this->spin_longitude->setRange(-180.0, 180.0);
    this->spin_longitude->setDecimals(6);
    this->spin_longitude->setSingleStep(0.000001);
    this->spin_longitude->setSuffix(" °");
    this->spin_longitude->setAccelerated(true);
    
    QPushButton *button_find = new QPushButton("Find on Map");
    
    grid->addWidget(label_latitude, 0, 0);
    grid->addWidget(this->spin_latitude, 0, 1);
    grid->addWidget(label_longitude, 1, 0);
    grid->addWidget(this->spin_longitude, 1, 1);
    grid->addWidget(button_find, 2, 0, 1, 2);
    
    this->layout->addWidget(group);
}

void EntityInspectorTank::addGroupElevation()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Elevation");
    QGridLayout *grid = new QGridLayout(group);
    
    this->combo_elevation_mode = new QComboBox();
    this->combo_elevation_mode->addItem("Tank Bottom Elevation");
    this->combo_elevation_mode->addItem("Terrain Elevation + Offset");
    
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
    grid->addWidget(this->label_terrain_elevation, 1, 0);
    grid->addWidget(this->spin_terrain_elevation, 1, 1);
    grid->addWidget(this->label_tank_bottom_offset, 2, 0);
    grid->addWidget(this->spin_tank_bottom_offset, 2, 1);
    grid->addWidget(label_tank_bottom_elevation, 3, 0);
    grid->addWidget(this->spin_tank_bottom_elevation, 3, 1);
    
    connect(this->combo_elevation_mode, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        switch (index)
        {
        case 0:
            this->label_terrain_elevation->hide();
            this->spin_terrain_elevation->hide();
            this->label_tank_bottom_offset->hide();
            this->spin_tank_bottom_offset->hide();
            this->spin_tank_bottom_elevation->setReadOnly(false);
            this->spin_tank_bottom_elevation->setToolTip("");
            return;
        case 1:
            this->label_terrain_elevation->show();
            this->spin_terrain_elevation->show();
            this->label_tank_bottom_offset->show();
            this->spin_tank_bottom_offset->show();
            this->spin_tank_bottom_elevation->setReadOnly(true);
            this->spin_tank_bottom_elevation->setToolTip(
                "Calculated automatically from <i>Terrain Elevation</i> + <i>Offset</i>"
            );
            elevationCalc();
            return;
        }
    });
    
    this->combo_elevation_mode->setCurrentIndex(1);
    
    connect(this->spin_terrain_elevation, &QDoubleSpinBox::valueChanged, this, &EntityInspectorTank::elevationCalc);
    connect(this->spin_tank_bottom_offset, &QDoubleSpinBox::valueChanged, this, &EntityInspectorTank::elevationCalc);
    
    this->layout->addWidget(group);
}

void EntityInspectorTank::elevationCalc()
{
    double ground = this->spin_terrain_elevation->value();
    double offset = this->spin_tank_bottom_offset->value();
    this->spin_tank_bottom_elevation->setValue(ground + offset);
}

void EntityInspectorTank::addGroupGeometry()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Geometry / Levels");
    QGridLayout *grid = new QGridLayout(group);
    
    this->combo_geometry_type = new QComboBox();
    this->combo_geometry_type->addItem("Cylindrical");
    this->combo_geometry_type->addItem("Uniform Area");
    this->combo_geometry_type->addItem("Total Volume");
    this->combo_geometry_type->addItem("Volume Curve");
    
    this->spin_level_min = new QDoubleSpinBox();
    this->spin_level_max = new QDoubleSpinBox();
    
    connect(this->combo_geometry_type, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        switch (index)
        {
        case 0:
            this->label_terrain_elevation->hide();
            this->spin_terrain_elevation->hide();
            this->label_tank_bottom_offset->hide();
            this->spin_tank_bottom_offset->hide();
            this->spin_tank_bottom_elevation->setReadOnly(false);
            return;
        case 1:
            this->label_terrain_elevation->show();
            this->spin_terrain_elevation->show();
            this->label_tank_bottom_offset->show();
            this->spin_tank_bottom_offset->show();
            this->spin_tank_bottom_elevation->setReadOnly(true);
            elevationCalc();
            return;
        }
    });
    
    QLabel *label_level_initial = new QLabel("Initial Level");
    this->spin_level_initial = new QDoubleSpinBox();
    
    QLabel *label_overflow = new QLabel("Overflow Allowed");
    this->check_overflow = new QCheckBox();
    
    grid->addWidget(combo_geometry_type, 0, 0, 1, 2);
    
    grid->addWidget(label_level_initial, 1, 0);
    grid->addWidget(this->spin_level_initial, 1, 1);
    
    grid->addWidget(label_overflow, 2, 0);
    grid->addWidget(this->check_overflow, 2, 1);
    
    this->layout->addWidget(group);
}

void EntityInspectorTank::addGroupQuality()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Quality");
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
    
    this->layout->addWidget(group);
}

void EntityInspectorTank::addGroupSimMeas()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Simulation / Measurements");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    this->layout->addWidget(group);
}

void EntityInspectorTank::addGroupGraphs()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Graphs");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    this->layout->addWidget(group);
}
