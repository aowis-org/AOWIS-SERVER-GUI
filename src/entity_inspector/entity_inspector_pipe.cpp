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
    addGroupRoughness();
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
    QPushButton *button_node_1_locate = new QPushButton(QIcon(":/icon/geomarker.png"), "");
    button_node_1_locate->setIconSize(QSize(20, 20));
    button_node_1_locate->setToolTip("Show on Map");
    button_node_1_locate->setMaximumWidth(35);
    QPushButton *button_node_1_inspect = new QPushButton(QIcon(":/icon/target.png"), "");
    button_node_1_inspect->setIconSize(QSize(20, 20));
    button_node_1_inspect->setToolTip("Inspect");
    button_node_1_inspect->setMaximumWidth(35);
    
    QLabel *label_node_2 = new QLabel("Node 2");
    QLabel *label_node_2_id = new QLabel();
    QPushButton *button_node_2_locate = new QPushButton(QIcon(":/icon/geomarker.png"), "");
    button_node_2_locate->setIconSize(QSize(20, 20));
    button_node_2_locate->setToolTip("Show on Map");
    button_node_2_locate->setMaximumWidth(35);
    QPushButton *button_node_2_inspect = new QPushButton(QIcon(":/icon/target.png"), "");
    button_node_2_inspect->setIconSize(QSize(20, 20));
    button_node_2_inspect->setToolTip("Inspect");
    button_node_2_inspect->setMaximumWidth(35);
    
    grid->addWidget(label_node_1, 0, 0);
    grid->addWidget(label_node_1_id, 0, 1);
    grid->addWidget(button_node_1_locate, 0, 2);
    grid->addWidget(button_node_1_inspect, 0, 3);
    
    grid->addWidget(label_node_2, 1, 0);
    grid->addWidget(label_node_2_id, 1, 1);
    grid->addWidget(button_node_2_locate, 1, 2);
    grid->addWidget(button_node_2_inspect, 1, 3);
    
    mainLayout()->addWidget(group);
}

void EntityInspectorPipe::addGroupGeometry()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Geometry");
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
    
    grid->addWidget(label_status_initial, 0, 0);
    grid->addWidget(this->combo_status_initial, 0, 1);
    
    grid->addWidget(label_diameter, 1, 0);
    grid->addWidget(this->spin_diameter, 1, 1);
    
    grid->addWidget(label_length, 2, 0);
    grid->addWidget(this->spin_length, 2, 1);
    
    mainLayout()->addWidget(group);
}

void EntityInspectorPipe::addGroupRoughness()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Geometry / Roughness");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *label_material = new QLabel("Material");
    this->combo_material = new QComboBox();
    
    QLabel *label_roughness_hw = new QLabel("Roughness<br>Hazen-Williams");
    label_roughness_hw->setWordWrap(true);
    this->spin_roughness_hw = new QDoubleSpinBox();
    this->spin_roughness_hw->setToolTip("Pipe roughness coefficient C");
    this->spin_roughness_hw->setDecimals(0);
    this->spin_roughness_hw->setRange(1.0, 200.0);
    this->spin_roughness_hw->setSingleStep(1.0);
    this->spin_roughness_hw->setValue(130.0);
    
    QLabel *label_roughness_dw = new QLabel("Roughness<br>Darcy-Weisbach");
    label_roughness_dw->setWordWrap(true);
    this->spin_roughness_dw = new QDoubleSpinBox();
    this->spin_roughness_dw->setToolTip("Absolute pipe roughness ε in mm");
    this->spin_roughness_dw->setSuffix(" mm");
    this->spin_roughness_dw->setDecimals(6);
    this->spin_roughness_dw->setRange(0.000001, 100.0);
    this->spin_roughness_dw->setSingleStep(0.001);
    this->spin_roughness_dw->setValue(0.100000);
    
    QLabel *label_roughness_cm = new QLabel("Roughness<br>Chezy-Manning");
    label_roughness_cm->setWordWrap(true);
    this->spin_roughness_cm = new QDoubleSpinBox();
    this->spin_roughness_cm->setToolTip("Manning roughness coefficient n");
    this->spin_roughness_cm->setDecimals(4);
    this->spin_roughness_cm->setRange(0.0010, 0.1000);
    this->spin_roughness_cm->setSingleStep(0.0010);
    this->spin_roughness_cm->setValue(0.0130);
    
    QLabel *label_loss_coefficient = new QLabel("Loss Coefficient");
    label_loss_coefficient->setWordWrap(true);
    this->spin_loss_coefficient = new QDoubleSpinBox();
    this->spin_loss_coefficient->setToolTip(QStringLiteral("Dimensionless minor loss coefficient K<br>for local losses from bends, fittings,<br>entrances, exits, etc.<br><br>Default is 0 for no additional minor losses."));
    this->spin_loss_coefficient->setDecimals(3);
    this->spin_loss_coefficient->setRange(0.0, 1000.0);
    this->spin_loss_coefficient->setSingleStep(0.1);
    this->spin_loss_coefficient->setValue(0.0);
    
    grid->addWidget(label_material, 3, 0);
    grid->addWidget(this->combo_material, 3, 1);
    
    grid->addWidget(label_roughness_hw, 4, 0);
    grid->addWidget(this->spin_roughness_hw, 4, 1);
    grid->addWidget(label_roughness_dw, 5, 0);
    grid->addWidget(this->spin_roughness_dw, 5, 1);
    grid->addWidget(label_roughness_cm, 6, 0);
    grid->addWidget(this->spin_roughness_cm, 6, 1);
    
    grid->addWidget(label_loss_coefficient, 7, 0);
    grid->addWidget(this->spin_loss_coefficient, 7, 1);
    
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

void EntityInspectorPipe::onHeadlossFormulaChanged(HeadlossFormulas formulas)
{
    const bool use_hw = formulas.testFlag(HeadlossFormula::HazenWilliams);
    const bool use_dw = formulas.testFlag(HeadlossFormula::DarcyWeisbach);
    const bool use_cm = formulas.testFlag(HeadlossFormula::ChezyManning);
    
    this->spin_roughness_hw->setDisabled(!use_hw);
    this->spin_roughness_dw->setDisabled(!use_dw);
    this->spin_roughness_cm->setDisabled(!use_cm);
}
