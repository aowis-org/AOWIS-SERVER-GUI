#include "entity_inspector_pipe.h"

EntityInspectorPipe::EntityInspectorPipe(QWidget *parent)
    : EntityInspectorWidget(parent),
    layout(new QVBoxLayout()),
    label(new QLabel())
{
    setLayout(this->layout);
    
    this->layout->addWidget(this->label);
    setTitle("Pipe P1");
    addGroupGeneral(":/icon/pipe.png", "P1");
    
    addGroupEndpoints();
    addGroupGeometry();
    addGroupQuality();
    addGroupSimMeas();
    addGroupGraphs();
    
    mainLayout()->addStretch();
}

void EntityInspectorPipe::addGroupEndpoints()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Endpoints");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *label_node_1 = new QLabel("Node 1");
    QLabel *label_node_1_id = new QLabel();
    QPushButton *button_node_1_locate = new QPushButton(QIcon(":/icon/gps.png"), "");
    button_node_1_locate->setToolTip("Show on Map");
    button_node_1_locate->setMaximumWidth(35);
    
    QLabel *label_node_2 = new QLabel("Node 2");
    QLabel *label_node_2_id = new QLabel();
    QPushButton *button_node_2_locate = new QPushButton(QIcon(":/icon/gps.png"), "");
    button_node_2_locate->setToolTip("Show on Map");
    button_node_2_locate->setMaximumWidth(35);
    
    grid->addWidget(label_node_1, 0, 0);
    grid->addWidget(label_node_1_id, 0, 1);
    grid->addWidget(button_node_1_locate, 0, 2);
    
    grid->addWidget(label_node_2, 1, 0);
    grid->addWidget(label_node_2_id, 1, 1);
    grid->addWidget(button_node_2_locate, 1, 2);
    
    mainLayout()->addWidget(group);
}

void EntityInspectorPipe::addGroupGeometry()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Geometry / Roughness");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *label_status_initial = new QLabel("Initial Status");
    this->combo_status_initial = new QComboBox();
    this->combo_status_initial->addItem("Open");
    this->combo_status_initial->addItem("Closed");
    this->combo_status_initial->addItem("Check Valve");
    
    QLabel *label_diameter = new QLabel("Diameter");
    this->spin_diameter = new QDoubleSpinBox();
    this->spin_diameter->setSuffix(" mm");
    this->spin_diameter->setDecimals(1);
    this->spin_diameter->setRange(1.0, 5000.0);
    this->spin_diameter->setSingleStep(10.0);
    this->spin_diameter->setValue(100.0);
    
    QLabel *label_length = new QLabel("Length");
    this->spin_length = new QDoubleSpinBox();
    this->spin_length->setSuffix(" m");
    this->spin_length->setDecimals(2);
    this->spin_length->setRange(0.0, 100000.0);
    this->spin_length->setSingleStep(1.0);
    //this->spin_length->setValue(100.0);
    
    QLabel *label_material = new QLabel("Material");
    this->combo_material = new QComboBox();
    
    QLabel *label_roughness = new QLabel("Roughness");
    this->spin_roughness = new QDoubleSpinBox();
    this->spin_roughness->setDecimals(0);
    this->spin_roughness->setRange(1.0, 200.0);
    this->spin_roughness->setSingleStep(1.0);
    this->spin_roughness->setValue(130.0);
    
    grid->addWidget(label_status_initial, 0, 0);
    grid->addWidget(this->combo_status_initial, 0, 1);
    
    grid->addWidget(label_diameter, 1, 0);
    grid->addWidget(this->spin_diameter, 1, 1);
    
    grid->addWidget(label_length, 2, 0);
    grid->addWidget(this->spin_length, 2, 1);
    
    grid->addWidget(label_material, 3, 0);
    grid->addWidget(this->combo_material, 3, 1);
    
    grid->addWidget(label_roughness, 4, 0);
    grid->addWidget(this->spin_roughness, 4, 1);
    
    mainLayout()->addWidget(group);
}

void EntityInspectorPipe::addGroupQuality()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Quality");
    QGridLayout *grid = new QGridLayout(group);
    
    this->check_override = new QCheckBox("Override global reaction coefficients");
    QPushButton *button_override_show = new QPushButton("Edit global reaction coefficients");
    
    QLabel *label_spin_bulk = new QLabel("Bulk reaction coefficient");
    label_spin_bulk->setWordWrap(true);
    
    this->spin_bulk_reaction = new QDoubleSpinBox();
    this->spin_bulk_reaction->setDecimals(6);
    this->spin_bulk_reaction->setMinimum(-1000.0);
    this->spin_bulk_reaction->setMaximum(1000.0);
    this->spin_bulk_reaction->setSingleStep(0.001);
    this->spin_bulk_reaction->setValue(0.0);
    this->spin_bulk_reaction->setSuffix(QStringLiteral(" 1/day"));
    this->spin_bulk_reaction->setAlignment(Qt::AlignRight);
    this->spin_bulk_reaction->setEnabled(false);
    
    QLabel *label_spin_wall = new QLabel("Wall reaction coefficient");
    label_spin_wall->setWordWrap(true);
    
    this->spin_wall_reaction = new QDoubleSpinBox();
    this->spin_wall_reaction->setDecimals(6);
    this->spin_wall_reaction->setMinimum(-1000.0);
    this->spin_wall_reaction->setMaximum(1000.0);
    this->spin_wall_reaction->setSingleStep(0.001);
    this->spin_wall_reaction->setValue(0.0);
    this->spin_wall_reaction->setSuffix(QStringLiteral(" m/day"));
    this->spin_wall_reaction->setAlignment(Qt::AlignRight);
    this->spin_wall_reaction->setEnabled(false);
    
    connect(this->check_override, &QCheckBox::checkStateChanged, this, [this]
    {
        if (this->check_override->isChecked())
        {
            this->spin_bulk_reaction->setEnabled(true);
            this->spin_wall_reaction->setEnabled(true);
        }
        else
        {
            this->spin_bulk_reaction->setEnabled(false);
            this->spin_wall_reaction->setEnabled(false);
        }
    });
    
    grid->addWidget(button_override_show, 0, 0, 1, 2);
    grid->addWidget(this->check_override, 1, 0, 1, 2);
    grid->addWidget(label_spin_bulk, 2, 0);
    grid->addWidget(this->spin_bulk_reaction, 2, 1);
    grid->addWidget(label_spin_wall, 3, 0);
    grid->addWidget(this->spin_wall_reaction, 3, 1);
    
    mainLayout()->addWidget(group);
}

void EntityInspectorPipe::addGroupSimMeas()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Simulation / Measurements");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    mainLayout()->addWidget(group);
}

void EntityInspectorPipe::addGroupGraphs()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Graphs");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    mainLayout()->addWidget(group);
}
