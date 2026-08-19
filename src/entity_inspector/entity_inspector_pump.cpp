#include "entity_inspector_pump.h"

#include <optional>

#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>

namespace
{
constexpr int value_role = Qt::UserRole;
constexpr int uuid_role = Qt::UserRole + 1;

QString entityName(const QString &id, const QUuid &uuid)
{
    return id.isEmpty() ? uuid.toString(QUuid::WithoutBraces) : id;
}
}

EntityInspectorPump::EntityInspectorPump(HydraulicData *hydraulic_data,
                                         const HydraulicLinkPump &pump,
                                         QWidget *parent)
    : EntityInspectorWidget(hydraulic_data, parent),
      hydraulic_data(hydraulic_data),
      pump_uuid(pump.uuid)
{
    addGroupOverviewImage(":/icon/pump.png", pump.id);

    addGroupGeneral(QString());
    bindHydraulicLink(InfrastructureEntity::Pump, this->pump_uuid, "Pump");
    addGroupEndpoints();

    addGroupControls();
    addGroupEnergyCostInput();
    addGroupEnergy();
    addGroupSimulation();
    addGroupNoEntitySpecificQualityInputs(QStringLiteral("pump"));
    addGroupAlerts();
    addGroupHistory();
    addStretches();

    bindPump();
}

void EntityInspectorPump::addGroupControls()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Controls");
    QGridLayout *grid = new QGridLayout(group);

    QLabel *label_type = new QLabel("Type");
    this->combo_type = new QComboBox();
    this->combo_type->addItem(
        "Constant power", static_cast<int>(HydraulicLinkPumpDefinitionType::ConstantPower));
    this->combo_type->addItem(
        "1-point curve",
        static_cast<int>(HydraulicLinkPumpDefinitionType::OnePointCurve));
    this->combo_type->addItem(
        "3-point curve",
        static_cast<int>(HydraulicLinkPumpDefinitionType::ThreePointCurve));
    this->combo_type->addItem(
        "From Library", static_cast<int>(HydraulicLinkPumpDefinitionType::Library));

    this->label_constant_power = new QLabel("Power");
    this->spin_constant_power = new QDoubleSpinBox();
    this->spin_constant_power->setDecimals(3);
    this->spin_constant_power->setSingleStep(1.0);
    this->spin_constant_power->setRange(0.0, 1000000.0);
    this->spin_constant_power->setSuffix(" kW");
    this->spin_constant_power->setSpecialValueText("Not configured");

    QLabel *label_speed_initial = new QLabel("Initial Speed");
    this->spin_speed_initial = new QDoubleSpinBox();
    this->spin_speed_initial->setDecimals(2);
    this->spin_speed_initial->setSingleStep(0.05);
    this->spin_speed_initial->setRange(0.0, 10.0);
    this->spin_speed_initial->setSuffix(" ×");
    this->spin_speed_initial->setSpecialValueText("0.00 × (off)");

    QLabel *label_status_initial = new QLabel("Initial Status");
    this->combo_status_initial = new QComboBox();
    this->combo_status_initial->addItem(
        "On", static_cast<int>(HydraulicLinkPumpInitialStatus::On));
    this->combo_status_initial->addItem(
        "Off", static_cast<int>(HydraulicLinkPumpInitialStatus::Off));

    QLabel *label_speed_pattern = new QLabel("Speed Pattern");
    this->combo_speed_pattern = new QComboBox();

    QLabel *label_controls = new QLabel("Control Type");
    this->combo_controls = new QComboBox();
    this->combo_controls->addItem(
        "None", static_cast<int>(HydraulicLinkPumpControlType::None));
    this->combo_controls->addItem(
        "Level-based", static_cast<int>(HydraulicLinkPumpControlType::LevelBased));
    this->combo_controls->addItem(
        "Time-based", static_cast<int>(HydraulicLinkPumpControlType::TimeBased));

    grid->addWidget(label_type, 0, 0);
    grid->addWidget(this->combo_type, 0, 1);
    grid->addWidget(this->label_constant_power, 1, 0);
    grid->addWidget(this->spin_constant_power, 1, 1);
    grid->addWidget(label_speed_initial, 2, 0);
    grid->addWidget(this->spin_speed_initial, 2, 1);
    grid->addWidget(label_status_initial, 3, 0);
    grid->addWidget(this->combo_status_initial, 3, 1);
    grid->addWidget(label_speed_pattern, 4, 0);
    grid->addWidget(this->combo_speed_pattern, 4, 1);
    grid->addWidget(label_controls, 5, 0);
    grid->addWidget(this->combo_controls, 5, 1);

    layoutConfiguration()->addWidget(group);
}

void EntityInspectorPump::addGroupEnergyCostInput()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Energy Cost Input");
    QGridLayout *grid = new QGridLayout(group);

    QLabel *label_efficiency_curve = new QLabel("Efficiency Curve");
    this->combo_efficiency_curve = new QComboBox();

    QLabel *label_energy_price = new QLabel("Energy Price");
    this->spin_energy_price = new QDoubleSpinBox();
    this->spin_energy_price->setDecimals(4);
    this->spin_energy_price->setSingleStep(0.01);
    this->spin_energy_price->setRange(0.0, 1000.0);
    this->spin_energy_price->setSuffix(" /kWh");
    this->spin_energy_price->setSpecialValueText("Not priced");

    QLabel *label_price_pattern = new QLabel("Price Pattern");
    this->combo_price_pattern = new QComboBox();

    grid->addWidget(label_efficiency_curve, 0, 0);
    grid->addWidget(this->combo_efficiency_curve, 0, 1);
    grid->addWidget(label_energy_price, 1, 0);
    grid->addWidget(this->spin_energy_price, 1, 1);
    grid->addWidget(label_price_pattern, 2, 0);
    grid->addWidget(this->combo_price_pattern, 2, 1);

    layoutConfiguration()->addWidget(group);
}

void EntityInspectorPump::addGroupEnergy()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Energy");
    new QGridLayout(group);
    layoutConfiguration()->addWidget(group);
}

void EntityInspectorPump::bindPump()
{
    connect(this->combo_type, &QComboBox::currentIndexChanged, this, [this](int)
    {
        const HydraulicLinkPumpDefinitionType definition_type =
            static_cast<HydraulicLinkPumpDefinitionType>(
                this->combo_type->currentData(value_role).toInt());
        this->hydraulic_data->setPumpDefinitionType(this->pump_uuid, definition_type);
    });
    connect(this->spin_constant_power, &QDoubleSpinBox::valueChanged, this, [this](double power_kw)
    {
        this->hydraulic_data->setPumpConstantPowerKw(this->pump_uuid, power_kw);
    });
    connect(this->spin_speed_initial, &QDoubleSpinBox::valueChanged, this, [this](double speed)
    {
        this->hydraulic_data->setPumpInitialSpeed(this->pump_uuid, speed);
    });
    connect(this->combo_status_initial, &QComboBox::currentIndexChanged, this, [this](int)
    {
        const HydraulicLinkPumpInitialStatus initial_status =
            static_cast<HydraulicLinkPumpInitialStatus>(
                this->combo_status_initial->currentData(value_role).toInt());
        this->hydraulic_data->setPumpInitialStatus(this->pump_uuid, initial_status);
    });
    connect(this->combo_speed_pattern, &QComboBox::currentIndexChanged, this, [this](int)
    {
        this->hydraulic_data->setPumpSpeedPatternUuid(
            this->pump_uuid, this->combo_speed_pattern->currentData(uuid_role).toUuid());
    });
    connect(this->combo_controls, &QComboBox::currentIndexChanged, this, [this](int)
    {
        const HydraulicLinkPumpControlType control_type =
            static_cast<HydraulicLinkPumpControlType>(
                this->combo_controls->currentData(value_role).toInt());
        this->hydraulic_data->setPumpControlType(this->pump_uuid, control_type);
    });
    connect(this->combo_efficiency_curve, &QComboBox::currentIndexChanged, this, [this](int)
    {
        const HydraulicLinkPumpEfficiencyInputType input_type =
            static_cast<HydraulicLinkPumpEfficiencyInputType>(
                this->combo_efficiency_curve->currentData(value_role).toInt());
        this->hydraulic_data->setPumpEfficiencyInput(
            this->pump_uuid, input_type,
            this->combo_efficiency_curve->currentData(uuid_role).toUuid());
    });
    connect(this->spin_energy_price, &QDoubleSpinBox::valueChanged, this, [this](double price)
    {
        this->hydraulic_data->setPumpEnergyPricePerKwh(this->pump_uuid, price);
    });
    connect(this->combo_price_pattern, &QComboBox::currentIndexChanged, this, [this](int)
    {
        const HydraulicLinkPumpEnergyPriceInputType input_type =
            static_cast<HydraulicLinkPumpEnergyPriceInputType>(
                this->combo_price_pattern->currentData(value_role).toInt());
        this->hydraulic_data->setPumpEnergyPriceInput(
            this->pump_uuid, input_type,
            this->combo_price_pattern->currentData(uuid_role).toUuid());
    });

    connect(this->hydraulic_data, &HydraulicData::signalLinkChanged, this,
            [this](InfrastructureEntity entity_type, const QUuid &uuid)
    {
        if (entity_type == InfrastructureEntity::Pump && uuid == this->pump_uuid)
            refreshPump();
    });
    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded,
            this, &EntityInspectorPump::refreshPump);

    refreshPump();
}

void EntityInspectorPump::refreshPump()
{
    if (this->hydraulic_data == nullptr || this->pump_uuid.isNull())
        return;

    const std::optional<HydraulicLinkPump> pump = this->hydraulic_data->pump(this->pump_uuid);
    if (!pump.has_value())
        return;

    const QSignalBlocker type_blocker(this->combo_type);
    const QSignalBlocker constant_power_blocker(this->spin_constant_power);
    const QSignalBlocker speed_blocker(this->spin_speed_initial);
    const QSignalBlocker status_blocker(this->combo_status_initial);
    const QSignalBlocker speed_pattern_blocker(this->combo_speed_pattern);
    const QSignalBlocker controls_blocker(this->combo_controls);
    const QSignalBlocker efficiency_blocker(this->combo_efficiency_curve);
    const QSignalBlocker energy_price_blocker(this->spin_energy_price);
    const QSignalBlocker price_pattern_blocker(this->combo_price_pattern);

    const int type_index = this->combo_type->findData(
        static_cast<int>(pump->definition_type), value_role);
    this->combo_type->setCurrentIndex(type_index >= 0 ? type_index : 0);
    const bool constant_power = pump->definition_type == HydraulicLinkPumpDefinitionType::ConstantPower;
    this->label_constant_power->setVisible(constant_power);
    this->spin_constant_power->setVisible(constant_power);
    this->spin_constant_power->setValue(pump->constant_power_kw);
    this->spin_speed_initial->setValue(pump->initial_speed_ratio);

    const int status_index = this->combo_status_initial->findData(
        static_cast<int>(pump->initial_status), value_role);
    this->combo_status_initial->setCurrentIndex(status_index >= 0 ? status_index : 0);

    populateSpeedPatternCombo(pump->speed_pattern_uuid);

    const int controls_index = this->combo_controls->findData(
        static_cast<int>(pump->control_type), value_role);
    this->combo_controls->setCurrentIndex(controls_index >= 0 ? controls_index : 0);

    populateEfficiencyInputCombo(pump.value());
    const QString energy_currency = this->hydraulic_data->networkHydraulic().options_energy.currency_iso4217;
    this->spin_energy_price->setSuffix(energy_currency.isEmpty()
        ? QStringLiteral(" /kWh")
        : QStringLiteral(" %1/kWh").arg(energy_currency));
    this->spin_energy_price->setValue(pump->energy_price_per_kw_h);
    this->spin_energy_price->setEnabled(
        pump->energy_price_input_type == HydraulicLinkPumpEnergyPriceInputType::Constant);
    populatePricePatternCombo(pump.value());
}

void EntityInspectorPump::populateSpeedPatternCombo(const QUuid &pattern_uuid)
{
    this->combo_speed_pattern->clear();
    this->combo_speed_pattern->addItem("Fixed speed");
    this->combo_speed_pattern->setItemData(
        this->combo_speed_pattern->count() - 1, QUuid(), uuid_role);

    bool selected_pattern_exists = pattern_uuid.isNull();
    const QList<HydraulicPatternTime> &patterns =
        this->hydraulic_data->networkHydraulic().patterns_time;
    for (const HydraulicPatternTime &pattern : patterns)
    {
        this->combo_speed_pattern->addItem(entityName(pattern.id, pattern.uuid));
        this->combo_speed_pattern->setItemData(
            this->combo_speed_pattern->count() - 1, pattern.uuid, uuid_role);
        if (pattern.uuid == pattern_uuid)
            selected_pattern_exists = true;
    }

    if (!pattern_uuid.isNull() && !selected_pattern_exists)
    {
        this->combo_speed_pattern->addItem(
            QStringLiteral("[Missing Pattern] %1").arg(
                pattern_uuid.toString(QUuid::WithoutBraces)));
        this->combo_speed_pattern->setItemData(
            this->combo_speed_pattern->count() - 1, pattern_uuid, uuid_role);
    }

    int selected_index = 0;
    for (int index = 0; index < this->combo_speed_pattern->count(); index++)
    {
        if (this->combo_speed_pattern->itemData(index, uuid_role).toUuid() == pattern_uuid)
        {
            selected_index = index;
            break;
        }
    }
    this->combo_speed_pattern->setCurrentIndex(selected_index);
}

void EntityInspectorPump::populateEfficiencyInputCombo(const HydraulicLinkPump &pump)
{
    this->combo_efficiency_curve->clear();
    this->combo_efficiency_curve->addItem(
        "Global", static_cast<int>(HydraulicLinkPumpEfficiencyInputType::Global));
    this->combo_efficiency_curve->addItem(
        QStringLiteral("Constant (%1%)").arg(pump.constant_efficiency_percent, 0, 'f', 1),
        static_cast<int>(HydraulicLinkPumpEfficiencyInputType::Constant));

    bool selected_curve_exists = pump.efficiency_curve_uuid.isNull();
    const QList<HydraulicCurvePumpEfficiency> &curves =
        this->hydraulic_data->networkHydraulic().curves_pump_efficiency;
    for (const HydraulicCurvePumpEfficiency &curve : curves)
    {
        this->combo_efficiency_curve->addItem(
            entityName(curve.id, curve.uuid),
            static_cast<int>(HydraulicLinkPumpEfficiencyInputType::Curve));
        this->combo_efficiency_curve->setItemData(
            this->combo_efficiency_curve->count() - 1, curve.uuid, uuid_role);
        if (curve.uuid == pump.efficiency_curve_uuid)
            selected_curve_exists = true;
    }

    if (pump.efficiency_input_type == HydraulicLinkPumpEfficiencyInputType::Curve &&
        !pump.efficiency_curve_uuid.isNull() && !selected_curve_exists)
    {
        this->combo_efficiency_curve->addItem(
            QStringLiteral("[Missing Curve] %1").arg(
                pump.efficiency_curve_uuid.toString(QUuid::WithoutBraces)),
            static_cast<int>(HydraulicLinkPumpEfficiencyInputType::Curve));
        this->combo_efficiency_curve->setItemData(
            this->combo_efficiency_curve->count() - 1,
            pump.efficiency_curve_uuid, uuid_role);
    }

    int selected_index = 0;
    for (int index = 0; index < this->combo_efficiency_curve->count(); index++)
    {
        const HydraulicLinkPumpEfficiencyInputType item_type =
            static_cast<HydraulicLinkPumpEfficiencyInputType>(
                this->combo_efficiency_curve->itemData(index, value_role).toInt());
        const QUuid item_uuid =
            this->combo_efficiency_curve->itemData(index, uuid_role).toUuid();
        if (item_type == pump.efficiency_input_type &&
            (item_type != HydraulicLinkPumpEfficiencyInputType::Curve ||
             item_uuid == pump.efficiency_curve_uuid))
        {
            selected_index = index;
            break;
        }
    }
    this->combo_efficiency_curve->setCurrentIndex(selected_index);
}

void EntityInspectorPump::populatePricePatternCombo(const HydraulicLinkPump &pump)
{
    this->combo_price_pattern->clear();
    this->combo_price_pattern->addItem(
        "Global", static_cast<int>(HydraulicLinkPumpEnergyPriceInputType::Global));
    this->combo_price_pattern->addItem(
        "Constant", static_cast<int>(HydraulicLinkPumpEnergyPriceInputType::Constant));

    bool selected_pattern_exists = pump.price_pattern_uuid.isNull();
    const QList<HydraulicPatternTime> &patterns =
        this->hydraulic_data->networkHydraulic().patterns_time;
    for (const HydraulicPatternTime &pattern : patterns)
    {
        this->combo_price_pattern->addItem(
            entityName(pattern.id, pattern.uuid),
            static_cast<int>(HydraulicLinkPumpEnergyPriceInputType::Pattern));
        this->combo_price_pattern->setItemData(
            this->combo_price_pattern->count() - 1, pattern.uuid, uuid_role);
        if (pattern.uuid == pump.price_pattern_uuid)
            selected_pattern_exists = true;
    }

    if (pump.energy_price_input_type == HydraulicLinkPumpEnergyPriceInputType::Pattern &&
        !pump.price_pattern_uuid.isNull() && !selected_pattern_exists)
    {
        this->combo_price_pattern->addItem(
            QStringLiteral("[Missing Pattern] %1").arg(
                pump.price_pattern_uuid.toString(QUuid::WithoutBraces)),
            static_cast<int>(HydraulicLinkPumpEnergyPriceInputType::Pattern));
        this->combo_price_pattern->setItemData(
            this->combo_price_pattern->count() - 1, pump.price_pattern_uuid, uuid_role);
    }

    int selected_index = 0;
    for (int index = 0; index < this->combo_price_pattern->count(); index++)
    {
        const HydraulicLinkPumpEnergyPriceInputType item_type =
            static_cast<HydraulicLinkPumpEnergyPriceInputType>(
                this->combo_price_pattern->itemData(index, value_role).toInt());
        const QUuid item_uuid = this->combo_price_pattern->itemData(index, uuid_role).toUuid();
        if (item_type == pump.energy_price_input_type &&
            (item_type != HydraulicLinkPumpEnergyPriceInputType::Pattern ||
             item_uuid == pump.price_pattern_uuid))
        {
            selected_index = index;
            break;
        }
    }
    this->combo_price_pattern->setCurrentIndex(selected_index);
}
