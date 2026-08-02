#include "entity_inspector_valve.h"

EntityInspectorValve::EntityInspectorValve(HydraulicData *hydraulic_data, const HydraulicLinkValve &valve, QWidget *parent)
    : EntityInspectorWidget(hydraulic_data, parent)
{
    addGroupOverviewImage(":/icon/valve.png", valve.id);

    addGroupGeneral(QString());
    bindHydraulicLink(InfrastructureEntity::Valve, valve.uuid, "Valve");

    addGroupEndpoints();
    addGroupValveConfiguration();
    
    addGroupHistory();
    
    addStretches();
}

void EntityInspectorValve::addGroupValveConfiguration()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Valve Configuration");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *label_valve_type = new QLabel("Valve Type");
    this->combo_valve_type = new QComboBox();
    this->combo_valve_type->addItem(
        "PRV: Pressure Reducing Valve",
        static_cast<int>(HydraulicLinkValveType::PRV)
        );
    this->combo_valve_type->addItem(
        "PSV: Pressure Sustaining Valve",
        static_cast<int>(HydraulicLinkValveType::PSV)
        );
    this->combo_valve_type->addItem(
        "FCV: Flow Control Valve",
        static_cast<int>(HydraulicLinkValveType::FCV)
        );
    this->combo_valve_type->addItem(
        "PBV: Pressure Breaker Valve",
        static_cast<int>(HydraulicLinkValveType::PBV)
        );
    this->combo_valve_type->addItem(
        "TCV: Throttle Control Valve",
        static_cast<int>(HydraulicLinkValveType::TCV)
        );
    this->combo_valve_type->addItem(
        "GPV: General Purpose Valve",
        static_cast<int>(HydraulicLinkValveType::GPV)
        );
    this->combo_valve_type->addItem(
        "PCV: Positional Control Valve",
        static_cast<int>(HydraulicLinkValveType::PCV)
        );
    
    this->label_setting = new QLabel("Setting");
    this->spin_setting = new QDoubleSpinBox();
    this->spin_setting->setDecimals(2);
    this->spin_setting->setRange(0.0, 1000000.0);
    this->spin_setting->setSingleStep(1.0);
    this->spin_setting->setValue(0.0);
    
    this->combo_setting_curve = new QComboBox();
    this->combo_setting_curve->addItem("Select curve...");
    this->combo_setting_curve->hide();
    
    QLabel *label_status_initial = new QLabel("Initial Status");
    this->combo_status_initial = new QComboBox();
    this->combo_status_initial->addItem("Active");
    this->combo_status_initial->addItem("Open");
    this->combo_status_initial->addItem("Closed");
    
    QLabel *label_diameter = new QLabel("Diameter");
    this->spin_diameter = new QDoubleSpinBox();
    this->spin_diameter->setDecimals(2);
    this->spin_diameter->setRange(0.0, 1000000.0);
    this->spin_diameter->setSingleStep(10.0);
    this->spin_diameter->setValue(0.0);
    this->spin_diameter->setSuffix(" mm");
    
    QLabel *label_loss_coeff = new QLabel("Loss Coefficient");
    label_loss_coeff->setWordWrap(true);
    this->spin_loss_coeff = new QDoubleSpinBox();
    this->spin_loss_coeff->setDecimals(4);
    this->spin_loss_coeff->setRange(0.0, 1000000.0);
    this->spin_loss_coeff->setSingleStep(0.1);
    this->spin_loss_coeff->setValue(0.0);
    
    grid->addWidget(label_valve_type, 0, 0, 1, 2);
    grid->addWidget(this->combo_valve_type, 1, 0, 1, 2);
    
    grid->addWidget(this->label_setting, 2, 0);
    grid->addWidget(this->spin_setting, 2, 1);
    grid->addWidget(this->combo_setting_curve, 2, 1);
    
    grid->addWidget(label_status_initial, 3, 0);
    grid->addWidget(this->combo_status_initial, 3, 1);
    
    grid->addWidget(label_diameter, 4, 0);
    grid->addWidget(this->spin_diameter, 4, 1);
    
    grid->addWidget(label_loss_coeff, 5, 0);
    grid->addWidget(this->spin_loss_coeff, 5, 1);
    
    connect(
        this->combo_valve_type,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        [this](int index)
        {
            const QVariant data = this->combo_valve_type->itemData(index);
            
            if (!data.isValid()) {
                return;
            }
            
            const HydraulicLinkValveType type = static_cast<HydraulicLinkValveType>(data.toInt());
            onValveTypeChanged(type);
        }
    );
    
    onValveTypeChanged(
        static_cast<HydraulicLinkValveType>(
            this->combo_valve_type->currentData().toInt()
            )
    );
    
    this->layoutConfiguration()->addWidget(group);
}
void EntityInspectorValve::onValveTypeChanged(HydraulicLinkValveType type)
{
    this->spin_setting->show();
    this->combo_setting_curve->hide();
    
    switch (type) {
    case HydraulicLinkValveType::PRV:
        this->label_setting->setText("Pressure Setting");
        this->spin_setting->setSuffix(" m");
        this->spin_setting->setDecimals(2);
        this->spin_setting->setRange(0.0, 1000000.0);
        this->spin_setting->setSingleStep(1.0);
        this->spin_setting->setToolTip(
            "Target downstream pressure head for the pressure reducing valve."
            );
        break;
        
    case HydraulicLinkValveType::PSV:
        this->label_setting->setText("Pressure Setting");
        this->spin_setting->setSuffix(" m");
        this->spin_setting->setDecimals(2);
        this->spin_setting->setRange(0.0, 1000000.0);
        this->spin_setting->setSingleStep(1.0);
        this->spin_setting->setToolTip(
            "Target upstream pressure head for the pressure sustaining valve."
            );
        break;
        
    case HydraulicLinkValveType::PBV:
        this->label_setting->setText("Pressure Drop");
        this->spin_setting->setSuffix(" m");
        this->spin_setting->setDecimals(2);
        this->spin_setting->setRange(0.0, 1000000.0);
        this->spin_setting->setSingleStep(1.0);
        this->spin_setting->setToolTip(
            "Fixed pressure drop across the pressure breaker valve."
            );
        break;
        
    case HydraulicLinkValveType::FCV:
        this->label_setting->setText("Flow Setting");
        this->spin_setting->setSuffix(" m³/h");
        this->spin_setting->setDecimals(3);
        this->spin_setting->setRange(0.0, 1000000.0);
        this->spin_setting->setSingleStep(1.0);
        this->spin_setting->setToolTip(
            "Target flow through the flow control valve,<br>using the current project flow units."
            );
        break;
        
    case HydraulicLinkValveType::TCV:
        this->label_setting->setText("Loss Coefficient");
        this->spin_setting->setSuffix("");
        this->spin_setting->setDecimals(3);
        this->spin_setting->setRange(0.0, 1000000.0);
        this->spin_setting->setSingleStep(0.1);
        this->spin_setting->setToolTip(
            "Throttle setting expressed as a dimensionless loss coefficient."
            );
        break;
        
    case HydraulicLinkValveType::GPV:
        this->label_setting->setText("Headloss Curve");
        this->spin_setting->hide();
        this->combo_setting_curve->show();
        this->combo_setting_curve->setToolTip(
            "Headloss curve used by the general purpose valve."
            );
        break;
        
    case HydraulicLinkValveType::PCV:
        this->label_setting->setText("Position Setting");
        this->spin_setting->setSuffix(" %");
        this->spin_setting->setDecimals(2);
        this->spin_setting->setRange(0.0, 100.0);
        this->spin_setting->setSingleStep(1.0);
        this->spin_setting->setToolTip(
            "Valve opening position as a percentage.<br>PCV support requires EPANET 2.3 or newer."
            );
        break;
    }
}


