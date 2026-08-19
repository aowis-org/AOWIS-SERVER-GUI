#include "entity_inspector_valve.h"

#include <optional>

#include <QSignalBlocker>

namespace
{
constexpr int uuid_role = Qt::UserRole + 1;

QString entityName(const QString &id, const QUuid &uuid)
{
    return id.isEmpty() ? uuid.toString(QUuid::WithoutBraces) : id;
}
}

EntityInspectorValve::EntityInspectorValve(HydraulicData *hydraulic_data, const HydraulicLinkValve &valve, QWidget *parent)
    : EntityInspectorWidget(hydraulic_data, parent),
      hydraulic_data(hydraulic_data),
      valve_uuid(valve.uuid)
{
    addGroupOverviewImage(":/icon/valve.png", valve.id);

    addGroupGeneral(QString());
    bindHydraulicLink(InfrastructureEntity::Valve, this->valve_uuid, "Valve");

    addGroupEndpoints();
    addGroupValveConfiguration();
    addGroupSimulation();
    addGroupWaterQualitySimulation();
    
    addGroupNoEntitySpecificQualityInputs(QStringLiteral("valve"));
    addGroupAlerts();
    addGroupHistory();
    
    addStretches();

    bindValve();
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
    
    this->label_setting_curve = new QLabel("Curve");
    this->combo_setting_curve = new QComboBox();
    this->combo_setting_curve->addItem("Select curve...");
    this->label_setting_curve->hide();
    this->combo_setting_curve->hide();
    
    QLabel *label_status_initial = new QLabel("Initial Status");
    this->combo_status_initial = new QComboBox();
    this->combo_status_initial->addItem(
        "Active", static_cast<int>(HydraulicLinkValveInitialStatus::Active));
    this->combo_status_initial->addItem(
        "Open", static_cast<int>(HydraulicLinkValveInitialStatus::Open));
    this->combo_status_initial->addItem(
        "Closed", static_cast<int>(HydraulicLinkValveInitialStatus::Closed));
    
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

    grid->addWidget(this->label_setting_curve, 3, 0);
    grid->addWidget(this->combo_setting_curve, 3, 1);
    
    grid->addWidget(label_status_initial, 4, 0);
    grid->addWidget(this->combo_status_initial, 4, 1);
    
    grid->addWidget(label_diameter, 5, 0);
    grid->addWidget(this->spin_diameter, 5, 1);
    
    grid->addWidget(label_loss_coeff, 6, 0);
    grid->addWidget(this->spin_loss_coeff, 6, 1);
    
    layoutConfiguration()->addWidget(group);
}

void EntityInspectorValve::bindValve()
{
    connect(this->combo_valve_type, &QComboBox::currentIndexChanged, this, [this](int)
    {
        const HydraulicLinkValveType type = static_cast<HydraulicLinkValveType>(
            this->combo_valve_type->currentData().toInt());
        onValveTypeChanged(type);
        this->hydraulic_data->setValveType(this->valve_uuid, type);
    });
    connect(this->spin_setting, &QDoubleSpinBox::valueChanged, this, [this](double setting)
    {
        const HydraulicLinkValveType type = static_cast<HydraulicLinkValveType>(
            this->combo_valve_type->currentData().toInt());
        switch (type)
        {
        case HydraulicLinkValveType::PRV:
        case HydraulicLinkValveType::PSV:
        case HydraulicLinkValveType::PBV:
            this->hydraulic_data->setValveSettingPressureHeadM(this->valve_uuid, setting);
            break;
        case HydraulicLinkValveType::FCV:
            this->hydraulic_data->setValveSettingFlowM3PerH(this->valve_uuid, setting);
            break;
        case HydraulicLinkValveType::TCV:
            this->hydraulic_data->setValveSettingLossCoefficient(this->valve_uuid, setting);
            break;
        case HydraulicLinkValveType::PCV:
            this->hydraulic_data->setValveSettingPositionPercent(this->valve_uuid, setting);
            break;
        case HydraulicLinkValveType::GPV:
            break;
        }
    });
    connect(this->combo_setting_curve, &QComboBox::currentIndexChanged, this, [this](int)
    {
        const HydraulicLinkValveType type = static_cast<HydraulicLinkValveType>(
            this->combo_valve_type->currentData().toInt());
        const QUuid curve_uuid = this->combo_setting_curve->currentData(uuid_role).toUuid();
        if (type == HydraulicLinkValveType::GPV)
            this->hydraulic_data->setValveHeadLossCurveUuid(this->valve_uuid, curve_uuid);
        else if (type == HydraulicLinkValveType::PCV)
            this->hydraulic_data->setValveCharacteristicCurveUuid(this->valve_uuid, curve_uuid);
    });
    connect(this->combo_status_initial, &QComboBox::currentIndexChanged, this, [this](int)
    {
        const HydraulicLinkValveInitialStatus initial_status =
            static_cast<HydraulicLinkValveInitialStatus>(
                this->combo_status_initial->currentData().toInt());
        this->hydraulic_data->setValveInitialStatus(this->valve_uuid, initial_status);
    });
    connect(this->spin_diameter, &QDoubleSpinBox::valueChanged, this, [this](double diameter_mm)
    {
        this->hydraulic_data->setValveDiameterMm(this->valve_uuid, diameter_mm);
    });
    connect(this->spin_loss_coeff, &QDoubleSpinBox::valueChanged, this, [this](double minor_loss_coefficient)
    {
        this->hydraulic_data->setValveMinorLoss(this->valve_uuid, minor_loss_coefficient);
    });

    connect(this->hydraulic_data, &HydraulicData::signalLinkChanged, this,
            [this](InfrastructureEntity entity_type, const QUuid &uuid)
    {
        if (entity_type == InfrastructureEntity::Valve && uuid == this->valve_uuid)
            refreshValve();
    });
    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded,
            this, &EntityInspectorValve::refreshValve);

    refreshValve();
}

void EntityInspectorValve::refreshValve()
{
    if (this->hydraulic_data == nullptr || this->valve_uuid.isNull())
        return;

    const std::optional<HydraulicLinkValve> valve =
        this->hydraulic_data->valve(this->valve_uuid);
    if (!valve.has_value())
        return;

    const QSignalBlocker type_blocker(this->combo_valve_type);
    const QSignalBlocker setting_blocker(this->spin_setting);
    const QSignalBlocker curve_blocker(this->combo_setting_curve);
    const QSignalBlocker status_blocker(this->combo_status_initial);
    const QSignalBlocker diameter_blocker(this->spin_diameter);
    const QSignalBlocker loss_blocker(this->spin_loss_coeff);

    const int type_index = this->combo_valve_type->findData(static_cast<int>(valve->type));
    this->combo_valve_type->setCurrentIndex(type_index >= 0 ? type_index : 0);
    onValveTypeChanged(valve->type);
    QUuid curve_uuid;
    switch (valve->type)
    {
    case HydraulicLinkValveType::PRV:
    case HydraulicLinkValveType::PSV:
    case HydraulicLinkValveType::PBV:
        this->spin_setting->setValue(valve->setting_pressure_head_m);
        break;
    case HydraulicLinkValveType::FCV:
        this->spin_setting->setValue(valve->setting_flow_m3_per_h);
        break;
    case HydraulicLinkValveType::TCV:
        this->spin_setting->setValue(valve->setting_loss_coefficient);
        break;
    case HydraulicLinkValveType::PCV:
        this->spin_setting->setValue(valve->setting_position_percent);
        curve_uuid = valve->characteristic_curve_uuid;
        break;
    case HydraulicLinkValveType::GPV:
        curve_uuid = valve->head_loss_curve_uuid;
        break;
    }
    populateSettingCurveCombo(valve->type, curve_uuid);

    const int status_index =
        this->combo_status_initial->findData(static_cast<int>(valve->initial_status));
    this->combo_status_initial->setCurrentIndex(status_index >= 0 ? status_index : 0);
    this->spin_diameter->setValue(valve->diameter_mm);
    this->spin_loss_coeff->setValue(valve->minor_loss_coefficient);
}

void EntityInspectorValve::populateSettingCurveCombo(
    HydraulicLinkValveType type, const QUuid &curve_uuid)
{
    this->combo_setting_curve->clear();
    this->combo_setting_curve->addItem("Select curve...");
    this->combo_setting_curve->setItemData(0, QUuid(), uuid_role);

    bool selected_curve_exists = curve_uuid.isNull();
    if (type == HydraulicLinkValveType::GPV)
    {
        const QList<HydraulicCurveValveHeadloss> &curves =
            this->hydraulic_data->networkHydraulic().curves_valve_headloss;
        for (const HydraulicCurveValveHeadloss &curve : curves)
        {
            this->combo_setting_curve->addItem(entityName(curve.id, curve.uuid));
            this->combo_setting_curve->setItemData(
                this->combo_setting_curve->count() - 1, curve.uuid, uuid_role);
            if (curve.uuid == curve_uuid)
                selected_curve_exists = true;
        }
    }
    else if (type == HydraulicLinkValveType::PCV)
    {
        const QList<HydraulicCurveValveCharacteristic> &curves =
            this->hydraulic_data->networkHydraulic().curves_valve_characteristic;
        for (const HydraulicCurveValveCharacteristic &curve : curves)
        {
            this->combo_setting_curve->addItem(entityName(curve.id, curve.uuid));
            this->combo_setting_curve->setItemData(
                this->combo_setting_curve->count() - 1, curve.uuid, uuid_role);
            if (curve.uuid == curve_uuid)
                selected_curve_exists = true;
        }
    }

    if (!curve_uuid.isNull() && !selected_curve_exists)
    {
        this->combo_setting_curve->addItem(
            QStringLiteral("[Missing Curve] %1").arg(
                curve_uuid.toString(QUuid::WithoutBraces)));
        this->combo_setting_curve->setItemData(
            this->combo_setting_curve->count() - 1, curve_uuid, uuid_role);
    }

    int selected_index = 0;
    for (int index = 0; index < this->combo_setting_curve->count(); index++)
    {
        if (this->combo_setting_curve->itemData(index, uuid_role).toUuid() == curve_uuid)
        {
            selected_index = index;
            break;
        }
    }
    this->combo_setting_curve->setCurrentIndex(selected_index);
}

void EntityInspectorValve::onValveTypeChanged(HydraulicLinkValveType type)
{
    this->label_setting->show();
    this->spin_setting->show();
    this->label_setting_curve->hide();
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
            "Target flow through the flow control valve in the AOWIS canonical flow unit."
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
        this->label_setting->hide();
        this->spin_setting->hide();
        this->label_setting_curve->setText("Head Loss Curve");
        this->label_setting_curve->show();
        this->combo_setting_curve->show();
        this->combo_setting_curve->setToolTip(
            "Head-loss curve used by the general purpose valve."
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
        this->label_setting_curve->setText("Characteristic Curve");
        this->label_setting_curve->show();
        this->combo_setting_curve->show();
        this->combo_setting_curve->setToolTip(
            "Optional valve-characteristic curve used by the positional control valve."
            );
        break;
    }
}
