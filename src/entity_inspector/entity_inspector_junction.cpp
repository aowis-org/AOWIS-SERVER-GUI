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

void EntityInspectorJunction::addGroupDemands()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Demands");
    QGridLayout *grid = new QGridLayout(group);
    
    QPushButton *button_editor = new QPushButton("Open Editor");
    connect(button_editor, &QPushButton::clicked, this, [this]
    {
        openDemandsEditor();
    });
    
    grid->addWidget(button_editor, 1, 0);
    
    mainLayout()->addWidget(group);
}
void EntityInspectorJunction::openDemandsEditor()
{
    if (this->dialog_demands != nullptr)
    {
        this->dialog_demands->raise();
        this->dialog_demands->activateWindow();
        return;
    }
    
    this->dialog_demands = new QDialog(this);
    this->dialog_demands->setWindowTitle("Demands");
    this->dialog_demands->resize(700, 400);
    this->dialog_demands->setAttribute(Qt::WA_DeleteOnClose);
    
    QGridLayout *grid = new QGridLayout(this->dialog_demands);
    
    this->table_demands = new QTableWidget();
    this->table_demands->setColumnCount(5);
    this->table_demands->setHorizontalHeaderLabels(QStringList{"Demand", "Pattern", "Source / Method", "Note", ""});
    //this->table_demands->setAlternatingRowColors(true);
    /*
    this->table_demands->verticalHeader()->setVisible(false);
    this->table_demands->horizontalHeader()->setStretchLastSection(false);
    this->table_demands->setColumnWidth(0, 95);
    this->table_demands->setColumnWidth(1, 70);
    this->table_demands->setColumnWidth(2, 70);
    */
    this->table_demands->horizontalHeader()->setMinimumSectionSize(1);
    this->table_demands->setColumnWidth(3, 30);
    
    QPushButton *button_demand = new QPushButton("Add Demand");
    connect(button_demand, &QPushButton::clicked, this, [this]
            {
                int row = this->table_demands->rowCount();
                this->table_demands->insertRow(row);
                
                QDoubleSpinBox *spin_base_demand = new QDoubleSpinBox();
                
                spin_base_demand->setDecimals(3);
                spin_base_demand->setMinimum(0.0);
                spin_base_demand->setMaximum(100000.0);
                spin_base_demand->setSingleStep(0.1);
                spin_base_demand->setValue(0.0);
                spin_base_demand->setSuffix(QStringLiteral(" m³/h"));
                spin_base_demand->setAlignment(Qt::AlignRight);
                //spin_base_demand->setMaximumWidth(95);
                
                QComboBox *combo_demand = new QComboBox();
                combo_demand->addItem("Constant");
                combo_demand->addItem("RESIDENTIAL");
                //combo_demand->setMaximumWidth(70);
                
                QComboBox *combo_source = new QComboBox();
                combo_source->addItem("Manual Estimation");
                combo_source->addItem("Meter Data");
                combo_source->addItem("Scenario");
                
                QLineEdit *line_demand = new QLineEdit();
                line_demand->setPlaceholderText("");
                line_demand->setMinimumWidth(200);
                
                QPushButton *button_delete = new QPushButton(QIcon(":/icon/remove.png"), "");
                button_delete->setToolTip("Delete this entry");
                //button_delete->setMaximumWidth(30);
                
                this->table_demands->setCellWidget(row, 0, spin_base_demand);
                this->table_demands->setCellWidget(row, 1, combo_demand);
                this->table_demands->setCellWidget(row, 2, combo_source);
                this->table_demands->setCellWidget(row, 3, line_demand);
                this->table_demands->setCellWidget(row, 4, button_delete);
                
                this->table_demands->resizeColumnsToContents();
            });
    
    
    QPushButton *button_patterns = new QPushButton("Manage Patterns");
    
    grid->addWidget(this->table_demands, 0, 0, 1, 2);
    grid->addWidget(button_demand, 1, 0);
    
    grid->addWidget(button_patterns, 1, 1);
    
    connect(
        this->dialog_demands,
        &QObject::destroyed,
        this,
        [this]
        {
            this->dialog_demands = nullptr;
            this->table_demands = nullptr;
        }
    );
    
    this->dialog_demands->show();
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
