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
    
    
    
    addGroupQuality();
    addGroupSimMeas();
    addGroupGraphs();
    
    mainLayout()->addStretch();
}

void EntityInspectorPipe::addGroupDemands()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Demands");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
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
