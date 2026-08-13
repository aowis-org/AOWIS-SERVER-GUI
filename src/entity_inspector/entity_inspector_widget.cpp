#include "entity_inspector_widget.h"

#include "../rest_client.h"

#include <cmath>
#include <optional>

#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMessageBox>
#include <QPixmap>
#include <QSignalBlocker>
#include <QTimer>

namespace
{
constexpr int pattern_mode_role = Qt::UserRole;
constexpr int pattern_uuid_role = Qt::UserRole + 1;

QString formatSimulationElapsedTime(quint64 time_elapsed_s)
{
    const quint64 days = time_elapsed_s / 86400;
    const quint64 remainder_after_days = time_elapsed_s % 86400;
    const quint64 hours = remainder_after_days / 3600;
    const quint64 minutes = (remainder_after_days % 3600) / 60;
    const quint64 seconds = remainder_after_days % 60;

    const QString clock = QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));

    if (days == 0)
        return clock;

    return QStringLiteral("%1d %2").arg(days).arg(clock);
}

QString formatSimulationNumber(double value, int decimals, const QString &unit)
{
    if (!std::isfinite(value))
        return QStringLiteral("—");

    QString text = QString::number(value, 'f', decimals);
    while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    if (text == QStringLiteral("-0"))
        text = QStringLiteral("0");

    return text + unit;
}

template<typename ResultType>
const ResultType *simulationResultByUuid(const QList<ResultType> &results, const QUuid &uuid)
{
    for (const ResultType &result : results)
    {
        if (result.uuid == uuid)
            return &result;
    }

    return nullptr;
}

QString pumpStateText(HydraulicSimulationPumpState state)
{
    switch (state)
    {
    case HydraulicSimulationPumpState::CannotSupplyHead:
        return QStringLiteral("Cannot supply head");
    case HydraulicSimulationPumpState::Closed:
        return QStringLiteral("Closed");
    case HydraulicSimulationPumpState::Open:
        return QStringLiteral("Open");
    case HydraulicSimulationPumpState::CannotSupplyFlow:
        return QStringLiteral("Cannot supply flow");
    }

    return QStringLiteral("Unknown");
}

QString pipeRoughnessText(const HydraulicSimulationResultLinkPipe &result)
{
    if (result.roughness_hw.has_value())
    {
        return formatSimulationNumber(result.roughness_hw.value(), 3, QString())
            + QStringLiteral(" (Hazen-Williams)");
    }
    if (result.roughness_dw_mm.has_value())
    {
        return formatSimulationNumber(result.roughness_dw_mm.value(), 6, QStringLiteral(" mm"))
            + QStringLiteral(" (Darcy-Weisbach)");
    }
    if (result.roughness_cm.has_value())
    {
        return formatSimulationNumber(result.roughness_cm.value(), 6, QString())
            + QStringLiteral(" (Chezy-Manning)");
    }

    return QStringLiteral("—");
}

}

EntityInspectorWidget::EntityInspectorWidget(HydraulicData *hydraulic_data, QWidget *parent)
    : QWidget(parent),
    hydraulic_data(hydraulic_data),
    tabs(new QTabWidget(this)),
    layout_main(new QVBoxLayout(this)),
    label_title(new QLabel(this)),
    
    scroll_overview(new QScrollArea(this)),
    widget_overview(new QWidget()),
    layout_overview(new QVBoxLayout(this->widget_overview)),
    
    scroll_configuration(new QScrollArea(this)),
    widget_configuration(new QWidget()),
    layout_configuration(new QVBoxLayout(this->widget_configuration)),
    
    scroll_sim_meas(new QScrollArea(this)),
    widget_sim_meas(new QWidget()),
    layout_sim_meas(new QVBoxLayout(this->widget_sim_meas)),
    
    scroll_quality(new QScrollArea(this)),
    widget_quality(new QWidget()),
    layout_quality(new QVBoxLayout(this->widget_quality)),
    
    scroll_alerts(new QScrollArea(this)),
    widget_alerts(new QWidget()),
    layout_alerts(new QVBoxLayout(this->widget_alerts)),
    
    scroll_history(new QScrollArea(this)),
    widget_history(new QWidget()),
    layout_history(new QVBoxLayout(this->widget_history))
{
    this->scroll_overview->setWidgetResizable(true);
    this->scroll_overview->setWidget(this->widget_overview);
    
    this->scroll_configuration->setWidgetResizable(true);
    this->scroll_configuration->setWidget(this->widget_configuration);
    
    this->scroll_sim_meas->setWidgetResizable(true);
    this->scroll_sim_meas->setWidget(this->widget_sim_meas);
    
    this->scroll_quality->setWidgetResizable(true);
    this->scroll_quality->setWidget(this->widget_quality);
    
    this->scroll_alerts->setWidgetResizable(true);
    this->scroll_alerts->setWidget(this->widget_alerts);
    
    this->scroll_history->setWidgetResizable(true);
    this->scroll_history->setWidget(this->widget_history);
    
    this->tabs->setIconSize(QSize(40, 40));
    this->tabs->tabBar()->setStyleSheet(
        "QTabBar::tab"
        "{"
        "    max-width: 40px;"
        "    padding: 5px;"
        "}"
    );
    
    tabs->addTab(this->scroll_overview, QIcon(":/icon/inspector_dash.png"), "");
    this->tabs->setTabToolTip(this->tabs->count()-1, "Entity Overview");
    
    tabs->addTab(this->scroll_configuration, QIcon(":/icon/settings_2.png"), "");
    this->tabs->setTabToolTip(this->tabs->count()-1, "Configuration");
    
    tabs->addTab(this->scroll_sim_meas, QIcon(":/icon/sim_meas.png"), "");
    this->tabs->setTabToolTip(this->tabs->count()-1, "Simulation & Measurement");
    
    tabs->addTab(this->scroll_quality, QIcon(":/icon/inspector_quality.png"), "");
    this->tabs->setTabToolTip(this->tabs->count()-1, "Quality");
    
    tabs->addTab(this->scroll_alerts, QIcon(":/icon/alarm.png"), "");
    this->tabs->setTabToolTip(this->tabs->count()-1, "Alerts");
    
    tabs->addTab(this->scroll_history, QIcon(":/icon/history.png"), "");
    this->tabs->setTabToolTip(this->tabs->count()-1, "History");

    connect(this->tabs, &QTabWidget::currentChanged, this, &EntityInspectorWidget::signalCurrentTabChanged);
    
    this->layout_main->addWidget(this->label_title);
    this->layout_main->addWidget(this->tabs);
}

void EntityInspectorWidget::setCurrentTabIndex(int index)
{
    if (index < 0 || index >= this->tabs->count())
        return;

    this->tabs->setCurrentIndex(index);
}

QVBoxLayout *EntityInspectorWidget::layoutOverview()
{
    return this->layout_overview;
}
QVBoxLayout *EntityInspectorWidget::layoutConfiguration()
{
    return this->layout_configuration;
}
QVBoxLayout *EntityInspectorWidget::layoutSimMeas()
{
    return this->layout_sim_meas;
}
QVBoxLayout *EntityInspectorWidget::layoutQuality()
{
    return this->layout_quality;
}
QVBoxLayout *EntityInspectorWidget::layoutHistory()
{
    return this->layout_history;
}

void EntityInspectorWidget::setTitle(const QString &title)
{
    this->label_title->setText("<b>" + title.toHtmlEscaped() + "</b>");
}

void EntityInspectorWidget::addSimulationRow(QGridLayout *grid, int &row,
                                              SimulationField field,
                                              const QString &name,
                                              const QString &tooltip)
{
    QLabel *label_name = new QLabel(name);
    label_name->setWordWrap(true);
    if (!tooltip.isEmpty())
        label_name->setToolTip(tooltip);

    QLabel *label_value = new QLabel(QStringLiteral("—"));
    label_value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label_value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label_value->setMinimumWidth(105);
    if (!tooltip.isEmpty())
        label_value->setToolTip(tooltip);

    grid->addWidget(label_name, row, 0);
    grid->addWidget(label_value, row, 1);

    SimulationRowWidgets widgets;
    widgets.name = label_name;
    widgets.value = label_value;
    this->simulation_rows.insert(static_cast<int>(field), widgets);
    row++;
}

void EntityInspectorWidget::addGroupSimulation()
{
    if (this->label_simulation_message != nullptr)
        return;

    GroupBoxCollapsible *group = new GroupBoxCollapsible("Simulation");
    QGridLayout *grid = new QGridLayout(group);
    int row = 0;

    this->label_simulation_message = new QLabel();
    this->label_simulation_message->setWordWrap(true);
    grid->addWidget(this->label_simulation_message, row++, 0, 1, 2);

    addSimulationRow(grid, row, SimulationField::ResultTime, "Time");

    switch (this->entity_type)
    {
    case InfrastructureEntity::Junction:
        addSimulationRow(grid, row, SimulationField::DemandRequested, "Requested Demand",
                         "Demand requested before pressure-driven demand reduction.");
        addSimulationRow(grid, row, SimulationField::DemandDelivered, "Delivered Demand",
                         "Consumer demand actually delivered at the selected timestep.");
        addSimulationRow(grid, row, SimulationField::DemandDeficit, "Demand Deficit");
        addSimulationRow(grid, row, SimulationField::TotalDemand, "Total Demand",
                         "Total junction outflow: delivered demand, emitter flow, and assigned pipe leakage.");
        addSimulationRow(grid, row, SimulationField::EmitterFlow, "Emitter Flow");
        addSimulationRow(grid, row, SimulationField::LeakageFlow, "Leakage Flow");
        addSimulationRow(grid, row, SimulationField::Head, "Head");
        addSimulationRow(grid, row, SimulationField::PressureHead, "Pressure Head");
        addSimulationRow(grid, row, SimulationField::ReferencedByControl, "Referenced by Control");
        break;
    case InfrastructureEntity::Reservoir:
        addSimulationRow(grid, row, SimulationField::NetDemand, "Net Demand",
                         "Negative means the reservoir supplies the network; positive means it receives water.");
        addSimulationRow(grid, row, SimulationField::Head, "Head");
        addSimulationRow(grid, row, SimulationField::PressureHead, "Pressure Head");
        addSimulationRow(grid, row, SimulationField::ReferencedByControl, "Referenced by Control");
        break;
    case InfrastructureEntity::Tank:
        addSimulationRow(grid, row, SimulationField::NetDemand, "Net Demand",
                         "Positive means the tank is filling; negative means it supplies the network.");
        addSimulationRow(grid, row, SimulationField::Head, "Head");
        addSimulationRow(grid, row, SimulationField::PressureHead, "Pressure Head");
        addSimulationRow(grid, row, SimulationField::WaterLevel, "Water Level");
        addSimulationRow(grid, row, SimulationField::Volume, "Volume");
        addSimulationRow(grid, row, SimulationField::MixingZoneVolume, "Mixing-Zone Volume");
        addSimulationRow(grid, row, SimulationField::ReferencedByControl, "Referenced by Control");
        break;
    case InfrastructureEntity::Pipe:
        addSimulationRow(grid, row, SimulationField::Flow, "Flow");
        addSimulationRow(grid, row, SimulationField::LeakageFlow, "Leakage Flow");
        addSimulationRow(grid, row, SimulationField::Velocity, "Velocity");
        addSimulationRow(grid, row, SimulationField::HeadLoss, "Head Loss");
        addSimulationRow(grid, row, SimulationField::UnitHeadLoss, "Unit Head Loss");
        addSimulationRow(grid, row, SimulationField::FrictionFactor, "Friction Factor");
        addSimulationRow(grid, row, SimulationField::Status, "Status");
        addSimulationRow(grid, row, SimulationField::Roughness, "Roughness",
                         "Effective roughness returned by the hydraulic engine for the configured headloss formula.");
        addSimulationRow(grid, row, SimulationField::ReferencedByControl, "Referenced by Control");
        break;
    case InfrastructureEntity::Pump:
        addSimulationRow(grid, row, SimulationField::Flow, "Flow");
        addSimulationRow(grid, row, SimulationField::Velocity, "Velocity");
        addSimulationRow(grid, row, SimulationField::Head, "Head Gain");
        addSimulationRow(grid, row, SimulationField::Status, "Status");
        addSimulationRow(grid, row, SimulationField::PumpState, "Operating State");
        addSimulationRow(grid, row, SimulationField::Speed, "Speed");
        addSimulationRow(grid, row, SimulationField::Efficiency, "Efficiency");
        addSimulationRow(grid, row, SimulationField::Power, "Power");
        addSimulationRow(grid, row, SimulationField::ReferencedByControl, "Referenced by Control");
        break;
    case InfrastructureEntity::Valve:
        addSimulationRow(grid, row, SimulationField::Flow, "Flow");
        addSimulationRow(grid, row, SimulationField::Velocity, "Velocity");
        addSimulationRow(grid, row, SimulationField::HeadLoss, "Head Loss");
        addSimulationRow(grid, row, SimulationField::Status, "Status");
        addSimulationRow(grid, row, SimulationField::ValveRegulating, "Regulating");
        addSimulationRow(grid, row, SimulationField::Setting, "Setting");
        addSimulationRow(grid, row, SimulationField::ReferencedByControl, "Referenced by Control");
        break;
    default:
        break;
    }

    grid->setColumnStretch(1, 1);
    layoutSimMeas()->addWidget(group);

    if (this->entity_type == InfrastructureEntity::Pump)
    {
        GroupBoxCollapsible *energy_group = new GroupBoxCollapsible("Simulation Energy Summary");
        QGridLayout *energy_grid = new QGridLayout(energy_group);
        int energy_row = 0;

        this->label_simulation_energy_message = new QLabel();
        this->label_simulation_energy_message->setWordWrap(true);
        energy_grid->addWidget(this->label_simulation_energy_message, energy_row++, 0, 1, 2);

        addSimulationRow(energy_grid, energy_row, SimulationField::TimeOnline, "Time Online");
        addSimulationRow(energy_grid, energy_row, SimulationField::AverageEfficiency, "Average Efficiency");
        addSimulationRow(energy_grid, energy_row, SimulationField::AverageSpecificPower,
                         "Average Specific Power");
        addSimulationRow(energy_grid, energy_row, SimulationField::AveragePower, "Average Power");
        addSimulationRow(energy_grid, energy_row, SimulationField::PeakPower, "Peak Power");
        addSimulationRow(energy_grid, energy_row, SimulationField::AverageCostPerDay,
                         "Average Energy Cost");
        energy_grid->setColumnStretch(1, 1);
        layoutSimMeas()->addWidget(energy_group);
    }

    connect(this->hydraulic_data, &HydraulicData::signalSimulationResultTimelineChanged,
            this, [this](bool)
    {
        refreshSimulation();
    });
    connect(this->hydraulic_data, &HydraulicData::signalCurrentSimulationResultChanged,
            this, [this](int)
    {
        refreshSimulation();
    });
    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded,
            this, &EntityInspectorWidget::refreshSimulation);

    refreshSimulation();
}

void EntityInspectorWidget::resetSimulationValues()
{
    for (const SimulationRowWidgets &widgets : this->simulation_rows)
    {
        if (widgets.value != nullptr)
            widgets.value->setText(QStringLiteral("—"));
    }
}

void EntityInspectorWidget::setSimulationText(SimulationField field, const QString &text)
{
    const int key = static_cast<int>(field);
    if (!this->simulation_rows.contains(key))
        return;

    QLabel *label = this->simulation_rows.value(key).value;
    if (label != nullptr)
        label->setText(text);
}

void EntityInspectorWidget::setSimulationValue(SimulationField field, double value,
                                                int decimals, const QString &unit)
{
    setSimulationText(field, formatSimulationNumber(value, decimals, unit));
}

void EntityInspectorWidget::setSimulationEntityAvailable(bool available)
{
    if (this->label_simulation_message == nullptr)
        return;

    this->label_simulation_message->setVisible(!available);
    if (!available)
        this->label_simulation_message->setText("No result for this entity at the selected timestep.");
}

void EntityInspectorWidget::refreshPumpEnergySummary(
    const HydraulicSimulationResultTimeline &timeline)
{
    if (this->label_simulation_energy_message == nullptr || timeline.results.isEmpty())
        return;

    const HydraulicSimulationResult &final_result = timeline.results.constLast();
    const HydraulicSimulationResultLinkPumpEnergyUsage *energy_usage = nullptr;
    for (const HydraulicSimulationResultLinkPumpEnergyUsage &candidate :
         final_result.links_pump_energy_usage)
    {
        if (candidate.pump_uuid == this->entity_uuid)
        {
            energy_usage = &candidate;
            break;
        }
    }

    const bool available = energy_usage != nullptr;
    this->label_simulation_energy_message->setVisible(!available);
    if (!available)
    {
        this->label_simulation_energy_message->setText(
            "No complete-run energy summary is available for this pump.");
        return;
    }

    setSimulationValue(SimulationField::TimeOnline, energy_usage->time_online_percent,
                       2, QStringLiteral(" %"));
    setSimulationValue(SimulationField::AverageEfficiency,
                       energy_usage->average_efficiency_percent, 2, QStringLiteral(" %"));
    setSimulationValue(SimulationField::AverageSpecificPower,
                       energy_usage->average_kw_per_flow_unit, 6,
                       QStringLiteral(" kW/(m³/h)"));
    setSimulationValue(SimulationField::AveragePower, energy_usage->average_power_kw,
                       3, QStringLiteral(" kW"));
    setSimulationValue(SimulationField::PeakPower, energy_usage->peak_power_kw,
                       3, QStringLiteral(" kW"));
    setSimulationValue(SimulationField::AverageCostPerDay,
                       energy_usage->average_cost_per_day, 4, QStringLiteral(" /day"));
}

void EntityInspectorWidget::refreshSimulation()
{
    if (this->label_simulation_message == nullptr)
        return;

    resetSimulationValues();
    if (this->label_simulation_energy_message != nullptr)
    {
        this->label_simulation_energy_message->show();
        this->label_simulation_energy_message->setText(
            "No valid simulation results available.");
    }

    if (this->hydraulic_data == nullptr || !this->hydraulic_data->hasSimulationResults())
    {
        this->label_simulation_message->show();
        this->label_simulation_message->setText("No valid simulation results available.");
        return;
    }

    const HydraulicSimulationResult *result = this->hydraulic_data->currentSimulationResult();
    const std::optional<HydraulicSimulationResultTimeline> &timeline =
        this->hydraulic_data->simulationResultTimeline();
    if (result == nullptr || !timeline.has_value())
    {
        this->label_simulation_message->show();
        this->label_simulation_message->setText("No simulation timestep is selected.");
        return;
    }

    setSimulationText(SimulationField::ResultTime,
                      formatSimulationElapsedTime(result->time_elapsed_s));

    bool entity_available = false;
    switch (this->entity_type)
    {
    case InfrastructureEntity::Junction:
    {
        const HydraulicSimulationResultNodeJunction *junction =
            simulationResultByUuid(result->nodes_junctions, this->entity_uuid);
        if (junction == nullptr)
            break;

        entity_available = true;
        setSimulationValue(SimulationField::DemandRequested,
                           junction->demand_requested_m3_per_h, 3, QStringLiteral(" m³/h"));
        setSimulationValue(SimulationField::DemandDelivered,
                           junction->demand_delivered_m3_per_h, 3, QStringLiteral(" m³/h"));
        setSimulationValue(SimulationField::DemandDeficit,
                           junction->demand_deficit_m3_per_h, 3, QStringLiteral(" m³/h"));
        setSimulationValue(SimulationField::TotalDemand,
                           junction->total_demand_m3_per_h, 3, QStringLiteral(" m³/h"));
        setSimulationValue(SimulationField::EmitterFlow,
                           junction->emitter_flow_m3_per_h, 3, QStringLiteral(" m³/h"));
        setSimulationValue(SimulationField::LeakageFlow,
                           junction->leakage_flow_m3_per_h, 3, QStringLiteral(" m³/h"));
        setSimulationValue(SimulationField::Head, junction->head_m, 3, QStringLiteral(" m"));
        setSimulationValue(SimulationField::PressureHead, junction->pressure_head_m,
                           3, QStringLiteral(" m"));
        setSimulationText(SimulationField::ReferencedByControl,
                          junction->appears_in_control ? QStringLiteral("Yes") : QStringLiteral("No"));
        break;
    }
    case InfrastructureEntity::Reservoir:
    {
        const HydraulicSimulationResultNodeReservoir *reservoir =
            simulationResultByUuid(result->nodes_reservoirs, this->entity_uuid);
        if (reservoir == nullptr)
            break;

        entity_available = true;
        setSimulationValue(SimulationField::NetDemand, reservoir->net_demand_m3_per_h,
                           3, QStringLiteral(" m³/h"));
        setSimulationValue(SimulationField::Head, reservoir->head_m, 3, QStringLiteral(" m"));
        setSimulationValue(SimulationField::PressureHead, reservoir->pressure_head_m,
                           3, QStringLiteral(" m"));
        setSimulationText(SimulationField::ReferencedByControl,
                          reservoir->appears_in_control ? QStringLiteral("Yes") : QStringLiteral("No"));
        break;
    }
    case InfrastructureEntity::Tank:
    {
        const HydraulicSimulationResultNodeTank *tank =
            simulationResultByUuid(result->nodes_tanks, this->entity_uuid);
        if (tank == nullptr)
            break;

        entity_available = true;
        setSimulationValue(SimulationField::NetDemand, tank->net_demand_m3_per_h,
                           3, QStringLiteral(" m³/h"));
        setSimulationValue(SimulationField::Head, tank->head_m, 3, QStringLiteral(" m"));
        setSimulationValue(SimulationField::PressureHead, tank->pressure_head_m,
                           3, QStringLiteral(" m"));
        setSimulationValue(SimulationField::WaterLevel, tank->water_level_m,
                           3, QStringLiteral(" m"));
        setSimulationValue(SimulationField::Volume, tank->volume_m3,
                           3, QStringLiteral(" m³"));
        setSimulationValue(SimulationField::MixingZoneVolume, tank->mixing_zone_volume_m3,
                           3, QStringLiteral(" m³"));
        setSimulationText(SimulationField::ReferencedByControl,
                          tank->appears_in_control ? QStringLiteral("Yes") : QStringLiteral("No"));
        break;
    }
    case InfrastructureEntity::Pipe:
    {
        const HydraulicSimulationResultLinkPipe *pipe =
            simulationResultByUuid(result->links_pipes, this->entity_uuid);
        if (pipe == nullptr)
            break;

        entity_available = true;
        setSimulationValue(SimulationField::Flow, pipe->flow_m3_per_h,
                           3, QStringLiteral(" m³/h"));
        setSimulationValue(SimulationField::LeakageFlow, pipe->leakage_flow_m3_per_h,
                           3, QStringLiteral(" m³/h"));
        setSimulationValue(SimulationField::Velocity, pipe->velocity_m_per_s,
                           3, QStringLiteral(" m/s"));
        setSimulationValue(SimulationField::HeadLoss, pipe->head_loss_m,
                           3, QStringLiteral(" m"));
        setSimulationValue(SimulationField::UnitHeadLoss,
                           pipe->unit_head_loss_m_per_km, 3, QStringLiteral(" m/km"));
        setSimulationValue(SimulationField::FrictionFactor, pipe->friction_factor, 6);
        setSimulationText(SimulationField::Status,
                          pipe->open ? QStringLiteral("Open") : QStringLiteral("Closed"));
        setSimulationText(SimulationField::Roughness, pipeRoughnessText(*pipe));
        setSimulationText(SimulationField::ReferencedByControl,
                          pipe->appears_in_control ? QStringLiteral("Yes") : QStringLiteral("No"));
        break;
    }
    case InfrastructureEntity::Pump:
    {
        const HydraulicSimulationResultLinkPump *pump =
            simulationResultByUuid(result->links_pumps, this->entity_uuid);
        if (pump == nullptr)
            break;

        entity_available = true;
        setSimulationValue(SimulationField::Flow, pump->flow_m3_per_h,
                           3, QStringLiteral(" m³/h"));
        setSimulationValue(SimulationField::Velocity, pump->velocity_m_per_s,
                           3, QStringLiteral(" m/s"));
        setSimulationValue(SimulationField::Head, pump->head_gain_m,
                           3, QStringLiteral(" m"));
        setSimulationText(SimulationField::Status,
                          pump->open ? QStringLiteral("Open") : QStringLiteral("Closed"));
        setSimulationText(SimulationField::PumpState, pumpStateText(pump->state));
        setSimulationValue(SimulationField::Speed, pump->speed, 3, QStringLiteral(" ×"));
        setSimulationValue(SimulationField::Efficiency, pump->efficiency_percent,
                           2, QStringLiteral(" %"));
        setSimulationValue(SimulationField::Power, pump->power_kw,
                           3, QStringLiteral(" kW"));
        setSimulationText(SimulationField::ReferencedByControl,
                          pump->appears_in_control ? QStringLiteral("Yes") : QStringLiteral("No"));
        break;
    }
    case InfrastructureEntity::Valve:
    {
        const HydraulicSimulationResultLinkValve *valve_result =
            simulationResultByUuid(result->links_valves, this->entity_uuid);
        if (valve_result == nullptr)
            break;

        entity_available = true;
        setSimulationValue(SimulationField::Flow, valve_result->flow_m3_per_h,
                           3, QStringLiteral(" m³/h"));
        setSimulationValue(SimulationField::Velocity, valve_result->velocity_m_per_s,
                           3, QStringLiteral(" m/s"));
        setSimulationValue(SimulationField::HeadLoss, valve_result->head_loss_m,
                           3, QStringLiteral(" m"));
        setSimulationText(SimulationField::Status,
                          valve_result->open ? QStringLiteral("Open") : QStringLiteral("Closed"));
        setSimulationText(SimulationField::ValveRegulating,
                          valve_result->active ? QStringLiteral("Yes") : QStringLiteral("No"));

        QString setting_unit;
        int setting_decimals = 3;
        const std::optional<HydraulicLinkValve> valve =
            this->hydraulic_data->valve(this->entity_uuid);
        if (valve.has_value())
        {
            switch (valve->type)
            {
            case HydraulicLinkValveType::PRV:
            case HydraulicLinkValveType::PSV:
            case HydraulicLinkValveType::PBV:
                setting_unit = QStringLiteral(" m");
                break;
            case HydraulicLinkValveType::FCV:
                setting_unit = QStringLiteral(" m³/h");
                break;
            case HydraulicLinkValveType::PCV:
                setting_unit = QStringLiteral(" %");
                setting_decimals = 2;
                break;
            case HydraulicLinkValveType::TCV:
            case HydraulicLinkValveType::GPV:
                break;
            }
        }
        setSimulationValue(SimulationField::Setting, valve_result->setting,
                           setting_decimals, setting_unit);
        setSimulationText(SimulationField::ReferencedByControl,
                          valve_result->appears_in_control ? QStringLiteral("Yes") : QStringLiteral("No"));
        break;
    }
    default:
        break;
    }

    setSimulationEntityAvailable(entity_available);
    if (this->entity_type == InfrastructureEntity::Pump)
        refreshPumpEnergySummary(timeline.value());
}

void EntityInspectorWidget::addGroupOverviewImage(const QString &icon_path, const QString &name)
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("General");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *picture = new QLabel();
    QPixmap pixmap(icon_path);
    
    picture->setPixmap(pixmap.scaledToHeight(
        Sizes::SidebarRightImageHeight,
        Qt::SmoothTransformation
        ));
    picture->setAlignment(Qt::AlignCenter);
    
    grid->addWidget(picture, 0, 0, 1, 2);
    
    this->layoutOverview()->addWidget(group);
}

void EntityInspectorWidget::addGroupGeneral(const QString &name)
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("General");
    QGridLayout *grid = new QGridLayout(group);

    QLabel *label_name = new QLabel("Name");
    this->line_name = new QLineEdit();
    this->line_name->setText(name);

    QLabel *label_model_role = new QLabel("Model Role");
    this->combo_model_role = new QComboBox();
    this->combo_model_role->addItem("[Unspecified]", static_cast<int>(EntityModelRole::Unspecified));
    this->combo_model_role->addItem("Existing Asset", static_cast<int>(EntityModelRole::ExistingAsset));
    this->combo_model_role->addItem("Planned Asset", static_cast<int>(EntityModelRole::PlannedAsset));
    this->combo_model_role->addItem("Virtual / Model-Only", static_cast<int>(EntityModelRole::VirtualModelElement));
    this->combo_model_role->addItem("Boundary Condition", static_cast<int>(EntityModelRole::BoundaryCondition));
    this->combo_model_role->addItem("Temporary / Testing", static_cast<int>(EntityModelRole::TemporaryTesting));
    this->combo_model_role->addItem("Retired Asset", static_cast<int>(EntityModelRole::RetiredAsset));
    this->combo_model_role->setToolTip(
        "Describes whether this entity represents a real asset, a planned asset, "
        "a model-only helper, a boundary condition, or a temporary/testing element."
    );

    const QDate date_unset(100, 1, 1);

    QLabel *label_date_added = new QLabel("Date Added");
    this->date_added = new QDateEdit();
    this->date_added->setCalendarPopup(true);
    this->date_added->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    this->date_added->setToolTip("Date the entity was added to the model. yyyy-MM-dd");
    this->date_added->setMinimumDate(date_unset);
    this->date_added->setSpecialValueText("[Not set]");
    this->date_added->setDate(date_unset);

    QLabel *label_date_installed = new QLabel("Installation<br>Date");
    this->date_installed = new QDateEdit();
    this->date_installed->setCalendarPopup(true);
    this->date_installed->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    this->date_installed->setToolTip("Date the physical asset was installed. yyyy-MM-dd");
    this->date_installed->setMinimumDate(date_unset);
    this->date_installed->setSpecialValueText("[Not set]");
    this->date_installed->setDate(date_unset);

    this->check_enabled = new QCheckBox("Enabled");
    this->check_enabled->setChecked(true);

    grid->addWidget(label_name, 0, 0);
    grid->addWidget(this->line_name, 0, 1);
    grid->addWidget(label_model_role, 1, 0);
    grid->addWidget(this->combo_model_role, 1, 1);
    grid->addWidget(label_date_added, 2, 0);
    grid->addWidget(this->date_added, 2, 1);
    grid->addWidget(label_date_installed, 3, 0);
    grid->addWidget(this->date_installed, 3, 1);
    grid->addWidget(this->check_enabled, 4, 0, 1, 2);

    layoutConfiguration()->addWidget(group);
}

void EntityInspectorWidget::addGroupEndpoints()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Endpoints");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *label_node_1 = new QLabel("Node 1");
    this->label_node_1_id = new QLabel();
    this->label_node_1_id->setTextInteractionFlags(Qt::TextSelectableByMouse);
    this->label_node_1_id->setAlignment(Qt::AlignCenter);
    this->button_node_1_locate = new QPushButton(QIcon(":/icon/geomarker.png"), "");
    this->button_node_1_locate->setIconSize(QSize(20, 20));
    this->button_node_1_locate->setToolTip("Find on Map");
    this->button_node_1_locate->setMaximumWidth(35);
    this->button_node_1_inspect = new QPushButton(QIcon(":/icon/target.png"), "");
    this->button_node_1_inspect->setIconSize(QSize(20, 20));
    this->button_node_1_inspect->setToolTip("Show in Inspector");
    this->button_node_1_inspect->setMaximumWidth(35);
    
    QLabel *label_node_2 = new QLabel("Node 2");
    this->label_node_2_id = new QLabel();
    this->label_node_2_id->setTextInteractionFlags(Qt::TextSelectableByMouse);
    this->label_node_2_id->setAlignment(Qt::AlignCenter);
    this->button_node_2_locate = new QPushButton(QIcon(":/icon/geomarker.png"), "");
    this->button_node_2_locate->setIconSize(QSize(20, 20));
    this->button_node_2_locate->setToolTip("Find on Map");
    this->button_node_2_locate->setMaximumWidth(35);
    this->button_node_2_inspect = new QPushButton(QIcon(":/icon/target.png"), "");
    this->button_node_2_inspect->setIconSize(QSize(20, 20));
    this->button_node_2_inspect->setToolTip("Show in Inspector");
    this->button_node_2_inspect->setMaximumWidth(35);
    
    grid->addWidget(label_node_1, 0, 0);
    grid->addWidget(this->label_node_1_id, 0, 1);
    grid->addWidget(this->button_node_1_locate, 0, 2);
    grid->addWidget(this->button_node_1_inspect, 0, 3);
    
    grid->addWidget(label_node_2, 1, 0);
    grid->addWidget(this->label_node_2_id, 1, 1);
    grid->addWidget(this->button_node_2_locate, 1, 2);
    grid->addWidget(this->button_node_2_inspect, 1, 3);
    grid->setColumnStretch(1, 1);

    connect(this->button_node_1_locate, &QPushButton::clicked, this, [this]()
    {
        locateHydraulicEndpoint(this->node_uuid_1);
    });
    connect(this->button_node_1_inspect, &QPushButton::clicked, this, [this]()
    {
        inspectHydraulicEndpoint(this->node_uuid_1);
    });
    connect(this->button_node_2_locate, &QPushButton::clicked, this, [this]()
    {
        locateHydraulicEndpoint(this->node_uuid_2);
    });
    connect(this->button_node_2_inspect, &QPushButton::clicked, this, [this]()
    {
        inspectHydraulicEndpoint(this->node_uuid_2);
    });
    connect(this->hydraulic_data, &HydraulicData::signalNodeChanged, this,
            [this](InfrastructureEntity, const QUuid &uuid_changed)
    {
        if (uuid_changed == this->node_uuid_1 || uuid_changed == this->node_uuid_2)
            refreshHydraulicEndpoints();
    });
    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this,
            [this]()
    {
        refreshHydraulicEndpoints();
    });

    refreshHydraulicEndpoints();
    
    this->layoutConfiguration()->addWidget(group);
}

void EntityInspectorWidget::addGroupPosition()
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
    
    this->button_find_on_map = new QPushButton("Find on Map");
    
    grid->addWidget(label_latitude, 0, 0);
    grid->addWidget(this->spin_latitude, 0, 1);
    grid->addWidget(label_longitude, 1, 0);
    grid->addWidget(this->spin_longitude, 1, 1);
    grid->addWidget(this->button_find_on_map, 2, 0, 1, 2);
    
    layoutConfiguration()->addWidget(group);
}

void EntityInspectorWidget::bindHydraulicNode(InfrastructureEntity entity_type, const QUuid &uuid,
                                               const QString &title_prefix)
{
    this->entity_type = entity_type;
    this->entity_uuid = uuid;
    this->entity_title_prefix = title_prefix;

    refreshHydraulicNode();

    connect(this->line_name, &QLineEdit::textEdited, this, [this](const QString &id)
    {
        this->hydraulic_data->setNodeId(this->entity_uuid, id);
    });
    connect(this->combo_model_role, &QComboBox::currentIndexChanged, this, [this](int)
    {
        const EntityModelRole model_role = static_cast<EntityModelRole>(
            this->combo_model_role->currentData().toInt());
        this->hydraulic_data->setNodeModelRole(this->entity_uuid, model_role);
    });
    connect(this->date_added, &QDateEdit::dateChanged, this, [this](const QDate &)
    {
        this->hydraulic_data->setNodeDateAdded(this->entity_uuid, optionalDate(this->date_added));
    });
    connect(this->date_installed, &QDateEdit::dateChanged, this, [this](const QDate &)
    {
        this->hydraulic_data->setNodeDateInstalled(
            this->entity_uuid, optionalDate(this->date_installed));
    });
    connect(this->check_enabled, &QCheckBox::toggled, this, [this](bool enabled)
    {
        this->hydraulic_data->setNodeEnabled(this->entity_uuid, enabled);
    });
    connect(this->spin_latitude, &QDoubleSpinBox::valueChanged, this, [this](double)
    {
        CoordinateWGS84 coordinate;
        coordinate.latitude_deg = this->spin_latitude->value();
        coordinate.longitude_deg = this->spin_longitude->value();
        this->hydraulic_data->setNodeCoordinate(this->entity_uuid, coordinate);
    });
    connect(this->spin_longitude, &QDoubleSpinBox::valueChanged, this, [this](double)
    {
        CoordinateWGS84 coordinate;
        coordinate.latitude_deg = this->spin_latitude->value();
        coordinate.longitude_deg = this->spin_longitude->value();
        this->hydraulic_data->setNodeCoordinate(this->entity_uuid, coordinate);
    });
    connect(this->button_find_on_map, &QPushButton::clicked, this, [this]()
    {
        this->hydraulic_data->requestNodeLocate(this->entity_type, this->entity_uuid);
    });
    connect(this->hydraulic_data, &HydraulicData::signalNodeChanged, this, [this](InfrastructureEntity entity_type_changed, const QUuid &uuid_changed)
    {
        if (entity_type_changed == this->entity_type && uuid_changed == this->entity_uuid)
        {
            refreshHydraulicNode();
            scheduleJunctionDemandsRefresh();
        }
    });
    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this, [this]()
    {
        refreshHydraulicNode();
        scheduleJunctionDemandsRefresh();
    });
}

void EntityInspectorWidget::bindHydraulicLink(InfrastructureEntity entity_type, const QUuid &uuid,
                                               const QString &title_prefix)
{
    this->entity_type = entity_type;
    this->entity_uuid = uuid;
    this->entity_title_prefix = title_prefix;

    refreshHydraulicLink();

    connect(this->line_name, &QLineEdit::textEdited, this, [this](const QString &id)
    {
        this->hydraulic_data->setLinkId(this->entity_uuid, id);
    });
    connect(this->combo_model_role, &QComboBox::currentIndexChanged, this, [this](int)
    {
        const EntityModelRole model_role = static_cast<EntityModelRole>(
            this->combo_model_role->currentData().toInt());
        this->hydraulic_data->setLinkModelRole(this->entity_uuid, model_role);
    });
    connect(this->date_added, &QDateEdit::dateChanged, this, [this](const QDate &)
    {
        this->hydraulic_data->setLinkDateAdded(this->entity_uuid, optionalDate(this->date_added));
    });
    connect(this->date_installed, &QDateEdit::dateChanged, this, [this](const QDate &)
    {
        this->hydraulic_data->setLinkDateInstalled(
            this->entity_uuid, optionalDate(this->date_installed));
    });
    connect(this->check_enabled, &QCheckBox::toggled, this, [this](bool enabled)
    {
        this->hydraulic_data->setLinkEnabled(this->entity_uuid, enabled);
    });
    connect(this->hydraulic_data, &HydraulicData::signalLinkChanged, this,
            [this](InfrastructureEntity entity_type_changed, const QUuid &uuid_changed)
    {
        if (entity_type_changed == this->entity_type && uuid_changed == this->entity_uuid)
            refreshHydraulicLink();
    });
    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this, [this]()
    {
        refreshHydraulicLink();
    });
}

void EntityInspectorWidget::refreshHydraulicGeneral(
    const QString &id, const HydraulicEntityMetadata &metadata)
{
    const QSignalBlocker name_blocker(this->line_name);
    const QSignalBlocker model_role_blocker(this->combo_model_role);
    const QSignalBlocker date_added_blocker(this->date_added);
    const QSignalBlocker date_installed_blocker(this->date_installed);
    const QSignalBlocker enabled_blocker(this->check_enabled);

    this->line_name->setText(id);
    const int model_role_index = this->combo_model_role->findData(
        static_cast<int>(metadata.model_role));
    this->combo_model_role->setCurrentIndex(model_role_index >= 0 ? model_role_index : 0);
    setOptionalDate(this->date_added, metadata.date_added);
    setOptionalDate(this->date_installed, metadata.date_installed);
    this->check_enabled->setChecked(metadata.enabled);
    setTitle(this->entity_title_prefix + " " + id);
}

void EntityInspectorWidget::refreshHydraulicNode()
{
    if (!this->hydraulic_data || this->entity_uuid.isNull())
        return;

    const std::optional<HydraulicNodeCommonData> node =
        this->hydraulic_data->nodeCommonData(this->entity_type, this->entity_uuid);
    if (!node.has_value())
        return;

    refreshHydraulicGeneral(node->id, node->metadata);

    const QSignalBlocker latitude_blocker(this->spin_latitude);
    const QSignalBlocker longitude_blocker(this->spin_longitude);
    this->spin_latitude->setValue(node->coordinate_wgs84.latitude_deg);
    this->spin_longitude->setValue(node->coordinate_wgs84.longitude_deg);

    refreshHydraulicNodeElevation();
}

void EntityInspectorWidget::refreshHydraulicLink()
{
    if (!this->hydraulic_data || this->entity_uuid.isNull())
        return;

    const std::optional<HydraulicLinkCommonData> link =
        this->hydraulic_data->linkCommonData(this->entity_type, this->entity_uuid);
    if (!link.has_value())
        return;

    refreshHydraulicGeneral(link->id, link->metadata);
    refreshHydraulicEndpoints();
}

void EntityInspectorWidget::refreshHydraulicEndpoints()
{
    if (!this->label_node_1_id || !this->label_node_2_id)
        return;

    this->node_uuid_1 = QUuid();
    this->node_uuid_2 = QUuid();

    if (this->hydraulic_data)
    {
        switch (this->entity_type)
        {
        case InfrastructureEntity::Pipe:
        {
            const std::optional<HydraulicLinkPipe> pipe = this->hydraulic_data->pipe(this->entity_uuid);
            if (pipe.has_value())
            {
                this->node_uuid_1 = pipe->node_uuid_from;
                this->node_uuid_2 = pipe->node_uuid_to;
            }
            break;
        }
        case InfrastructureEntity::Pump:
        {
            const std::optional<HydraulicLinkPump> pump = this->hydraulic_data->pump(this->entity_uuid);
            if (pump.has_value())
            {
                this->node_uuid_1 = pump->node_uuid_from;
                this->node_uuid_2 = pump->node_uuid_to;
            }
            break;
        }
        case InfrastructureEntity::Valve:
        {
            const std::optional<HydraulicLinkValve> valve = this->hydraulic_data->valve(this->entity_uuid);
            if (valve.has_value())
            {
                this->node_uuid_1 = valve->node_uuid_from;
                this->node_uuid_2 = valve->node_uuid_to;
            }
            break;
        }
        default:
            break;
        }
    }

    refreshHydraulicEndpoint(this->node_uuid_1, this->label_node_1_id,
                             this->button_node_1_locate, this->button_node_1_inspect);
    refreshHydraulicEndpoint(this->node_uuid_2, this->label_node_2_id,
                             this->button_node_2_locate, this->button_node_2_inspect);
}

void EntityInspectorWidget::refreshHydraulicEndpoint(
    const QUuid &node_uuid, QLabel *label_node_id,
    QPushButton *button_locate, QPushButton *button_inspect)
{
    if (!label_node_id || !button_locate || !button_inspect)
        return;

    QString label_text = "[Not set]";
    QString tooltip;
    bool available = false;

    if (!node_uuid.isNull() && this->hydraulic_data)
    {
        const std::optional<InfrastructureEntity> node_type =
            this->hydraulic_data->nodeEntityType(node_uuid);
        if (node_type.has_value())
        {
            const std::optional<HydraulicNodeCommonData> node =
                this->hydraulic_data->nodeCommonData(node_type.value(), node_uuid);
            if (node.has_value())
            {
                label_text = node->id.isEmpty() ? "[Unnamed Node]" : node->id;
                tooltip = node_uuid.toString(QUuid::WithoutBraces);
                available = true;
            }
        }

        if (!available)
        {
            label_text = "[Missing Node]";
            tooltip = node_uuid.toString(QUuid::WithoutBraces);
        }
    }

    label_node_id->setText(label_text);
    label_node_id->setToolTip(tooltip);
    button_locate->setEnabled(available);
    button_inspect->setEnabled(available);
}

void EntityInspectorWidget::locateHydraulicEndpoint(const QUuid &node_uuid)
{
    if (!this->hydraulic_data || node_uuid.isNull())
        return;

    const std::optional<InfrastructureEntity> node_type =
        this->hydraulic_data->nodeEntityType(node_uuid);
    if (!node_type.has_value())
        return;

    this->hydraulic_data->requestNodeLocate(node_type.value(), node_uuid);
}

void EntityInspectorWidget::inspectHydraulicEndpoint(const QUuid &node_uuid)
{
    if (!this->hydraulic_data || node_uuid.isNull())
        return;

    const std::optional<InfrastructureEntity> node_type =
        this->hydraulic_data->nodeEntityType(node_uuid);
    if (!node_type.has_value())
        return;

    this->hydraulic_data->setSelectedUuid(node_type.value(), node_uuid);
}

std::optional<QDate> EntityInspectorWidget::optionalDate(const QDateEdit *date_edit) const
{
    if (!date_edit || date_edit->date() == date_edit->minimumDate())
        return std::nullopt;

    return date_edit->date();
}

void EntityInspectorWidget::setOptionalDate(QDateEdit *date_edit, const std::optional<QDate> &date)
{
    if (!date_edit)
        return;

    date_edit->setDate(date.has_value() ? date.value() : date_edit->minimumDate());
}

void EntityInspectorWidget::addGroupElevation()
{
    QString group_title;
    QString absolute_input_text;
    QString offset_label_text;
    QString value_label_text;
    QString offset_tooltip;

    switch (this->entity_type)
    {
    case InfrastructureEntity::Junction:
        group_title = "Elevation";
        absolute_input_text = "Total Elevation";
        offset_label_text = "Elevation Offset";
        value_label_text = "Total Elevation";
        offset_tooltip =
            "Distance from <i>Terrain Elevation</i>.<br>Positive: Above Ground.<br>Negative: Below Ground.";
        break;
    case InfrastructureEntity::Reservoir:
        group_title = "Head";
        absolute_input_text = "Total Head";
        offset_label_text = "Head Offset";
        value_label_text = "Total Head";
        offset_tooltip =
            "Hydraulic head relative to <i>Terrain Elevation</i>. Positive values are above terrain.";
        break;
    case InfrastructureEntity::Tank:
        group_title = "Bottom Elevation";
        absolute_input_text = "Bottom Elevation";
        offset_label_text = "Bottom Offset";
        value_label_text = "Bottom Elevation";
        offset_tooltip =
            "Tank bottom relative to terrain. Positive = above ground, negative = below ground.";
        break;
    default:
        return;
    }

    this->group_elevation = new GroupBoxCollapsible(group_title);
    QGridLayout *grid = new QGridLayout(this->group_elevation);
    
    this->combo_elevation_input_type = new QComboBox();
    this->combo_elevation_input_type->addItem(
        absolute_input_text,
        static_cast<int>(HydraulicNodeElevationInputType::AbsoluteElevation));
    this->combo_elevation_input_type->addItem(
        "Terrain Elevation + Offset",
        static_cast<int>(HydraulicNodeElevationInputType::TerrainElevationAndOffset));
    
    this->button_terrain_elevation = new QPushButton("Terrain Elevation from GIS");
    this->button_terrain_elevation->setToolTip(
        "Uses terrain elevation from GIS/DEM data.<br>Accuracy depends on the dataset and local terrain."
        );
    this->label_terrain_elevation_status = new QLabel();
    this->label_terrain_elevation_status->setWordWrap(true);
    this->label_terrain_elevation_status->hide();

    if (!this->terrain_elevation_client)
    {
#ifdef Q_OS_WASM
        // The OpenTopoData public service does not allow browser cross-origin requests.
        this->terrain_elevation_client = new RESTClient("https://api.open-meteo.com", this);
#else
        this->terrain_elevation_client = new RESTClient("https://api.opentopodata.org", this);
#endif
        connect(this->terrain_elevation_client, &RESTClient::requestFinished, this,
                &EntityInspectorWidget::handleTerrainElevationResponse);
        connect(this->terrain_elevation_client, &RESTClient::requestError, this,
                &EntityInspectorWidget::handleTerrainElevationError);
    }
    
    this->label_terrain_elevation = new QLabel("Terrain Elevation");
    this->spin_terrain_elevation = new QDoubleSpinBox;
    this->spin_terrain_elevation->setRange(-10000.0, 10000.0);
    this->spin_terrain_elevation->setDecimals(3);
    this->spin_terrain_elevation->setSingleStep(0.10);
    this->spin_terrain_elevation->setSuffix(" m");
    
    this->label_elevation_offset = new QLabel(offset_label_text);
    this->label_elevation_offset->setWordWrap(true);
    this->spin_elevation_offset = new QDoubleSpinBox;
    this->spin_elevation_offset->setRange(-10000.0, 10000.0);
    this->spin_elevation_offset->setDecimals(3);
    this->spin_elevation_offset->setSingleStep(0.10);
    this->spin_elevation_offset->setSuffix(" m");
    this->spin_elevation_offset->setToolTip(offset_tooltip);

    this->label_elevation_value = new QLabel(value_label_text);
    this->label_elevation_value->setWordWrap(true);
    this->spin_elevation_value = new QDoubleSpinBox;
    this->spin_elevation_value->setRange(-10000.0, 10000.0);
    this->spin_elevation_value->setDecimals(3);
    this->spin_elevation_value->setSingleStep(0.10);
    this->spin_elevation_value->setSuffix(" m");

    grid->addWidget(this->combo_elevation_input_type, 0, 0, 1, 2);
    grid->addWidget(this->button_terrain_elevation, 1, 0, 1, 2);
    grid->addWidget(this->label_terrain_elevation_status, 2, 0, 1, 2);
    grid->addWidget(this->label_terrain_elevation, 3, 0);
    grid->addWidget(this->spin_terrain_elevation, 3, 1);
    grid->addWidget(this->label_elevation_offset, 4, 0);
    grid->addWidget(this->spin_elevation_offset, 4, 1);
    grid->addWidget(this->label_elevation_value, 5, 0);
    grid->addWidget(this->spin_elevation_value, 5, 1);

    if (this->entity_type == InfrastructureEntity::Reservoir)
    {
        QLabel *label_head_pattern = new QLabel("Head Pattern");
        this->combo_head_pattern = new QComboBox();
        this->combo_head_pattern->setToolTip("Select a time pattern for variable reservoir head, or Constant for a fixed head.");
        grid->addWidget(label_head_pattern, 6, 0);
        grid->addWidget(this->combo_head_pattern, 6, 1);

        connect(this->combo_head_pattern, &QComboBox::currentIndexChanged, this, [this](int)
        {
            const HydraulicTimePatternMode pattern_mode = static_cast<HydraulicTimePatternMode>(this->combo_head_pattern->currentData(pattern_mode_role).toInt());
            const QUuid pattern_uuid = this->combo_head_pattern->currentData(pattern_uuid_role).toUuid();

            this->hydraulic_data->setReservoirHeadPatternMode(this->entity_uuid, pattern_mode);
            this->hydraulic_data->setReservoirHeadPatternUuid(this->entity_uuid, pattern_uuid);
        });
    }
    
    connect(this->button_terrain_elevation, &QPushButton::clicked, this,
            &EntityInspectorWidget::requestTerrainElevation);
    connect(this->combo_elevation_input_type, &QComboBox::currentIndexChanged, this, &EntityInspectorWidget::onElevationInputTypeChanged);
    connect(this->spin_elevation_value, &QDoubleSpinBox::valueChanged, this, &EntityInspectorWidget::onElevationValueChanged);
    connect(this->spin_terrain_elevation, &QDoubleSpinBox::valueChanged, this, &EntityInspectorWidget::onTerrainElevationChanged);
    connect(this->spin_elevation_offset, &QDoubleSpinBox::valueChanged, this, &EntityInspectorWidget::onElevationOffsetChanged);
    
    connect(this->group_elevation, &GroupBoxCollapsible::signalExpanded, this, &EntityInspectorWidget::onGroupExpand);
    
    this->layoutConfiguration()->addWidget(this->group_elevation);

    refreshHydraulicNodeElevation();
}

void EntityInspectorWidget::onGroupExpand(GroupBoxCollapsible *group)
{
    if (group == this->group_elevation)
        updateElevationModeUi();
}

void EntityInspectorWidget::refreshHydraulicNodeElevation()
{
    if (!this->hydraulic_data || this->entity_uuid.isNull() ||
        !this->combo_elevation_input_type || !this->spin_terrain_elevation ||
        !this->spin_elevation_offset || !this->spin_elevation_value)
        return;

    HydraulicNodeElevationInputType input_type = HydraulicNodeElevationInputType::AbsoluteElevation;
    double value_m = 0.0;
    double terrain_elevation_m = 0.0;
    double offset_m = 0.0;
    HydraulicTimePatternMode head_pattern_mode = HydraulicTimePatternMode::Constant;
    QUuid head_pattern_uuid;

    switch (this->entity_type)
    {
    case InfrastructureEntity::Junction:
    {
        const std::optional<HydraulicNodeJunction> junction =
            this->hydraulic_data->junction(this->entity_uuid);
        if (!junction.has_value())
            return;

        input_type = junction->elevation_input_type;
        value_m = junction->elevation_m;
        terrain_elevation_m = junction->terrain_elevation_m;
        offset_m = junction->elevation_offset_m;
        break;
    }
    case InfrastructureEntity::Reservoir:
    {
        const std::optional<HydraulicNodeReservoir> reservoir =
            this->hydraulic_data->reservoir(this->entity_uuid);
        if (!reservoir.has_value())
            return;

        input_type = reservoir->head_input_type;
        value_m = reservoir->head_m;
        terrain_elevation_m = reservoir->terrain_elevation_m;
        offset_m = reservoir->head_offset_m;
        head_pattern_mode = reservoir->head_pattern_mode;
        head_pattern_uuid = reservoir->head_pattern_uuid;
        break;
    }
    case InfrastructureEntity::Tank:
    {
        const std::optional<HydraulicNodeTank> tank =
            this->hydraulic_data->tank(this->entity_uuid);
        if (!tank.has_value())
            return;

        input_type = tank->elevation_input_type;
        value_m = tank->bottom_elevation_m;
        terrain_elevation_m = tank->terrain_elevation_m;
        offset_m = tank->bottom_offset_m;
        break;
    }
    default:
        return;
    }

    const QSignalBlocker input_type_blocker(this->combo_elevation_input_type);
    const QSignalBlocker terrain_blocker(this->spin_terrain_elevation);
    const QSignalBlocker offset_blocker(this->spin_elevation_offset);
    const QSignalBlocker value_blocker(this->spin_elevation_value);

    const int input_type_index = this->combo_elevation_input_type->findData(
        static_cast<int>(input_type));
    this->combo_elevation_input_type->setCurrentIndex(input_type_index >= 0 ? input_type_index : 0);
    this->spin_terrain_elevation->setValue(terrain_elevation_m);
    this->spin_elevation_offset->setValue(offset_m);
    this->spin_elevation_value->setValue(
        input_type == HydraulicNodeElevationInputType::TerrainElevationAndOffset
            ? terrain_elevation_m + offset_m
            : value_m);

    if (this->combo_head_pattern)
        populateTimePatternCombo(this->combo_head_pattern, head_pattern_mode, head_pattern_uuid);

    updateElevationModeUi();
}

void EntityInspectorWidget::updateElevationModeUi()
{
    if (!this->combo_elevation_input_type)
        return;

    const HydraulicNodeElevationInputType input_type =
        static_cast<HydraulicNodeElevationInputType>(
            this->combo_elevation_input_type->currentData().toInt());
    const bool uses_terrain =
        input_type == HydraulicNodeElevationInputType::TerrainElevationAndOffset;

    this->button_terrain_elevation->setVisible(uses_terrain);
    this->button_terrain_elevation->setEnabled(
        uses_terrain && !this->terrain_elevation_request_active);
    if (this->label_terrain_elevation_status)
        this->label_terrain_elevation_status->setVisible(
            uses_terrain && !this->label_terrain_elevation_status->text().isEmpty());
    this->label_terrain_elevation->setVisible(uses_terrain);
    this->spin_terrain_elevation->setVisible(uses_terrain);
    this->label_elevation_offset->setVisible(uses_terrain);
    this->spin_elevation_offset->setVisible(uses_terrain);
    this->spin_elevation_value->setReadOnly(uses_terrain);

    if (uses_terrain)
    {
        this->spin_elevation_value->setToolTip(
            "Calculated automatically from <i>Terrain Elevation</i> + <i>Offset</i>");
        updateCalculatedElevation();
    }
    else
    {
        this->spin_elevation_value->setToolTip("");
    }
}

void EntityInspectorWidget::updateCalculatedElevation()
{
    if (!this->combo_elevation_input_type || !this->spin_elevation_value)
        return;

    const HydraulicNodeElevationInputType input_type =
        static_cast<HydraulicNodeElevationInputType>(
            this->combo_elevation_input_type->currentData().toInt());
    if (input_type != HydraulicNodeElevationInputType::TerrainElevationAndOffset)
        return;

    const QSignalBlocker value_blocker(this->spin_elevation_value);
    this->spin_elevation_value->setValue(
        this->spin_terrain_elevation->value() + this->spin_elevation_offset->value());
}

void EntityInspectorWidget::requestTerrainElevation()
{
    if (!this->hydraulic_data || this->entity_uuid.isNull() ||
        !this->terrain_elevation_client || this->terrain_elevation_request_active)
        return;

    const std::optional<HydraulicNodeCommonData> node =
        this->hydraulic_data->nodeCommonData(this->entity_type, this->entity_uuid);
    if (!node.has_value())
    {
        handleTerrainElevationError(
            "The node no longer exists in the hydraulic model.");
        return;
    }

    const double latitude_deg = node->coordinate_wgs84.latitude_deg;
    const double longitude_deg = node->coordinate_wgs84.longitude_deg;
    if (!std::isfinite(latitude_deg) || latitude_deg < -90.0 || latitude_deg > 90.0 ||
        !std::isfinite(longitude_deg) || longitude_deg < -180.0 || longitude_deg > 180.0)
    {
        handleTerrainElevationError(
            "The node does not have a valid WGS84 position.");
        return;
    }

    this->terrain_elevation_request_entity_uuid = this->entity_uuid;
    this->terrain_elevation_request_coordinate = node->coordinate_wgs84;
    if (this->label_terrain_elevation_status)
        this->label_terrain_elevation_status->clear();
    setTerrainElevationRequestActive(true);

#ifdef Q_OS_WASM
    const QString endpoint = QStringLiteral("/v1/elevation?latitude=%1&longitude=%2")
#else
    const QString endpoint = QStringLiteral("/v1/srtm30m,aster30m?locations=%1,%2")
#endif
        .arg(latitude_deg, 0, 'f', 8)
        .arg(longitude_deg, 0, 'f', 8);
    this->terrain_elevation_client->get(endpoint);
}

void EntityInspectorWidget::handleTerrainElevationResponse(const QByteArray &data)
{
    if (!this->terrain_elevation_request_active)
        return;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        handleTerrainElevationError(
            QStringLiteral("The terrain elevation service returned invalid JSON: %1")
                .arg(parse_error.errorString()));
        return;
    }

    const QJsonObject response = document.object();
#ifdef Q_OS_WASM
    if (response.value(QStringLiteral("error")).toBool())
    {
        const QString api_error = response.value(QStringLiteral("reason")).toString();
        handleTerrainElevationError(
            api_error.isEmpty()
                ? QStringLiteral("Open-Meteo did not return a successful response.")
                : api_error);
        return;
    }

    const QJsonArray elevations = response.value(QStringLiteral("elevation")).toArray();
    if (elevations.isEmpty() || !elevations.first().isDouble())
    {
        handleTerrainElevationError(
            "Open-Meteo returned no terrain elevation for this position.");
        return;
    }

    const double elevation_m = elevations.first().toDouble();
    const QString dataset = QStringLiteral("Copernicus DEM GLO-90 via Open-Meteo");
#else
    if (response.value(QStringLiteral("status")).toString() != QStringLiteral("OK"))
    {
        const QString api_error = response.value(QStringLiteral("error")).toString();
        handleTerrainElevationError(
            api_error.isEmpty()
                ? QStringLiteral("OpenTopoData did not return a successful response.")
                : api_error);
        return;
    }

    const QJsonArray results = response.value(QStringLiteral("results")).toArray();
    if (results.isEmpty() || !results.first().isObject())
    {
        handleTerrainElevationError(
            "OpenTopoData returned no elevation result for this position.");
        return;
    }

    const QJsonObject result = results.first().toObject();
    const QJsonValue elevation_value = result.value(QStringLiteral("elevation"));
    if (!elevation_value.isDouble())
    {
        handleTerrainElevationError(
            "No terrain elevation is available for this position in the configured DEM datasets.");
        return;
    }

    const double elevation_m = elevation_value.toDouble();
    const QString dataset = result.value(QStringLiteral("dataset")).toString();
#endif

    if (this->entity_uuid != this->terrain_elevation_request_entity_uuid)
    {
        setTerrainElevationRequestActive(false);
        return;
    }

    const std::optional<HydraulicNodeCommonData> node =
        this->hydraulic_data->nodeCommonData(this->entity_type, this->entity_uuid);
    if (!node.has_value())
    {
        handleTerrainElevationError(
            "The node no longer exists in the hydraulic model.");
        return;
    }

    constexpr double coordinate_tolerance = 1e-10;
    const bool position_changed =
        std::abs(node->coordinate_wgs84.latitude_deg -
                 this->terrain_elevation_request_coordinate.latitude_deg) >
            coordinate_tolerance ||
        std::abs(node->coordinate_wgs84.longitude_deg -
                 this->terrain_elevation_request_coordinate.longitude_deg) >
            coordinate_tolerance;
    if (position_changed)
    {
        handleTerrainElevationError(
            "The node position changed while the terrain elevation was being retrieved. Please try again.");
        return;
    }

    if (!std::isfinite(elevation_m) || !setTerrainElevation(elevation_m))
    {
        handleTerrainElevationError(
            "The retrieved terrain elevation could not be written to the hydraulic model.");
        return;
    }

    const QString dataset_suffix = dataset.isEmpty()
        ? QString()
        : QStringLiteral(" (%1)").arg(dataset);
    this->button_terrain_elevation->setToolTip(
        QStringLiteral(
            "Uses terrain elevation from GIS/DEM data.<br>"
            "Accuracy depends on the dataset and local terrain.<br>"
            "Last result: %1 m%2")
            .arg(elevation_m, 0, 'f', 3)
            .arg(dataset_suffix));
    if (this->label_terrain_elevation_status)
        this->label_terrain_elevation_status->setText(
            QStringLiteral("Retrieved %1 m%2.")
                .arg(elevation_m, 0, 'f', 3)
                .arg(dataset_suffix));
    setTerrainElevationRequestActive(false);
}

void EntityInspectorWidget::handleTerrainElevationError(const QString &error)
{
    if (this->label_terrain_elevation_status)
        this->label_terrain_elevation_status->clear();

    setTerrainElevationRequestActive(false);
    showTerrainElevationErrorMessage(error);
}

void EntityInspectorWidget::showTerrainElevationErrorMessage(const QString &error)
{
    if (this->terrain_elevation_message_box)
        this->terrain_elevation_message_box->close();

    QWidget *parent_window = this->window();
    if (!parent_window)
        parent_window = this;

    QMessageBox *message_box = new QMessageBox(
        QMessageBox::Warning,
        "Terrain Elevation from GIS",
        "The terrain elevation could not be retrieved.",
        QMessageBox::Ok,
        parent_window);
    message_box->setInformativeText(error);
    message_box->setAttribute(Qt::WA_DeleteOnClose);

#ifdef Q_OS_WASM
    message_box->setWindowModality(Qt::NonModal);
    message_box->adjustSize();
    const QPoint initial_parent_center = parent_window->mapToGlobal(
        parent_window->rect().center());
    message_box->move(
        initial_parent_center.x() - message_box->width() / 2,
        initial_parent_center.y() - message_box->height() / 2);
#else
    message_box->setWindowModality(Qt::WindowModal);
#endif

    this->terrain_elevation_message_box = message_box;
    message_box->open();

#ifdef Q_OS_WASM
    QPointer<QMessageBox> guarded_message_box = message_box;
    QPointer<QWidget> guarded_parent_window = parent_window;
    QTimer::singleShot(0, message_box, [guarded_message_box, guarded_parent_window]()
    {
        if (!guarded_message_box || !guarded_parent_window)
            return;

        guarded_message_box->adjustSize();
        const QPoint parent_center = guarded_parent_window->mapToGlobal(
            guarded_parent_window->rect().center());
        guarded_message_box->move(
            parent_center.x() - guarded_message_box->width() / 2,
            parent_center.y() - guarded_message_box->height() / 2);
    });
#endif
}

void EntityInspectorWidget::setTerrainElevationRequestActive(bool active)
{
    this->terrain_elevation_request_active = active;

    if (!this->button_terrain_elevation)
        return;

    this->button_terrain_elevation->setText(
        active ? "Retrieving Terrain Elevation..." : "Terrain Elevation from GIS");
    updateElevationModeUi();
}

void EntityInspectorWidget::onElevationInputTypeChanged(int index)
{
    Q_UNUSED(index)

    const HydraulicNodeElevationInputType input_type =
        static_cast<HydraulicNodeElevationInputType>(
            this->combo_elevation_input_type->currentData().toInt());
    updateElevationModeUi();
    setElevationInputType(input_type);
}

void EntityInspectorWidget::onElevationValueChanged(double value_m)
{
    const HydraulicNodeElevationInputType input_type =
        static_cast<HydraulicNodeElevationInputType>(
            this->combo_elevation_input_type->currentData().toInt());
    if (input_type == HydraulicNodeElevationInputType::AbsoluteElevation)
        setElevationValue(value_m);
}

void EntityInspectorWidget::onTerrainElevationChanged(double terrain_elevation_m)
{
    setTerrainElevation(terrain_elevation_m);
    updateCalculatedElevation();
}

void EntityInspectorWidget::onElevationOffsetChanged(double offset_m)
{
    setElevationOffset(offset_m);
    updateCalculatedElevation();
}

bool EntityInspectorWidget::setElevationInputType(HydraulicNodeElevationInputType input_type)
{
    switch (this->entity_type)
    {
    case InfrastructureEntity::Junction:
        return this->hydraulic_data->setJunctionElevationInputType(this->entity_uuid, input_type);
    case InfrastructureEntity::Reservoir:
        return this->hydraulic_data->setReservoirHeadInputType(this->entity_uuid, input_type);
    case InfrastructureEntity::Tank:
        return this->hydraulic_data->setTankElevationInputType(this->entity_uuid, input_type);
    default:
        return false;
    }
}

bool EntityInspectorWidget::setElevationValue(double value_m)
{
    switch (this->entity_type)
    {
    case InfrastructureEntity::Junction:
        return this->hydraulic_data->setJunctionElevationM(this->entity_uuid, value_m);
    case InfrastructureEntity::Reservoir:
        return this->hydraulic_data->setReservoirHeadM(this->entity_uuid, value_m);
    case InfrastructureEntity::Tank:
        return this->hydraulic_data->setTankBottomElevationM(this->entity_uuid, value_m);
    default:
        return false;
    }
}

bool EntityInspectorWidget::setTerrainElevation(double terrain_elevation_m)
{
    switch (this->entity_type)
    {
    case InfrastructureEntity::Junction:
        return this->hydraulic_data->setJunctionTerrainElevationM(
            this->entity_uuid, terrain_elevation_m);
    case InfrastructureEntity::Reservoir:
        return this->hydraulic_data->setReservoirTerrainElevationM(
            this->entity_uuid, terrain_elevation_m);
    case InfrastructureEntity::Tank:
        return this->hydraulic_data->setTankTerrainElevationM(
            this->entity_uuid, terrain_elevation_m);
    default:
        return false;
    }
}

bool EntityInspectorWidget::setElevationOffset(double offset_m)
{
    switch (this->entity_type)
    {
    case InfrastructureEntity::Junction:
        return this->hydraulic_data->setJunctionElevationOffsetM(this->entity_uuid, offset_m);
    case InfrastructureEntity::Reservoir:
        return this->hydraulic_data->setReservoirHeadOffsetM(this->entity_uuid, offset_m);
    case InfrastructureEntity::Tank:
        return this->hydraulic_data->setTankBottomOffsetM(this->entity_uuid, offset_m);
    default:
        return false;
    }
}

void EntityInspectorWidget::onHeadlossFormulaChanged(HeadlossFormulas formulas)
{
    Q_UNUSED(formulas)
}

void EntityInspectorWidget::addGroupDemands()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Demands / Emitter");
    QGridLayout *grid = new QGridLayout(group);

    this->label_demands_summary = new QLabel();
    this->label_demands_summary->setWordWrap(true);

    QLabel *label_emitter_coefficient = new QLabel("Emitter<br>Coefficient");
    this->spin_emitter_coefficient = new QDoubleSpinBox();
    this->spin_emitter_coefficient->setRange(-1000000000.0, 1000000000.0);
    this->spin_emitter_coefficient->setDecimals(6);
    this->spin_emitter_coefficient->setSingleStep(0.001);
    this->spin_emitter_coefficient->setSuffix(QStringLiteral(" m³/h/mⁿ"));
    this->spin_emitter_coefficient->setAlignment(Qt::AlignRight);
    this->spin_emitter_coefficient->setToolTip(
        "Coefficient C in Q = C · pⁿ. Flow Q is stored in m³/h, pressure head p in m, "
        "and n is the global emitter exponent."
    );

    QPushButton *button_editor = new QPushButton("Open Editor");
    connect(button_editor, &QPushButton::clicked, this, [this]()
    {
        openDemandsEditor();
    });
    connect(this->spin_emitter_coefficient, &QDoubleSpinBox::valueChanged, this, [this](double value)
    {
        this->hydraulic_data->setJunctionEmitterCoefficientM3PerHPerMExponent(
            this->entity_uuid, value);
    });

    grid->addWidget(this->label_demands_summary, 0, 0, 1, 2);
    grid->addWidget(label_emitter_coefficient, 1, 0);
    grid->addWidget(this->spin_emitter_coefficient, 1, 1);
    grid->addWidget(button_editor, 2, 0, 1, 2);

    this->layoutConfiguration()->addWidget(group);
    refreshJunctionDemands();
}

void EntityInspectorWidget::scheduleJunctionDemandsRefresh()
{
    if (this->entity_type != InfrastructureEntity::Junction ||
        this->junction_demands_refresh_pending)
        return;

    this->junction_demands_refresh_pending = true;
    QTimer::singleShot(0, this, [this]()
    {
        this->junction_demands_refresh_pending = false;
        refreshJunctionDemands();
    });
}

void EntityInspectorWidget::refreshJunctionDemands()
{
    if (!this->hydraulic_data || this->entity_type != InfrastructureEntity::Junction ||
        this->entity_uuid.isNull())
        return;

    const std::optional<HydraulicNodeJunction> junction =
        this->hydraulic_data->junction(this->entity_uuid);
    if (!junction.has_value())
        return;

    if (this->label_demands_summary)
    {
        double total_base_demand_m3_per_h = 0.0;
        for (const HydraulicNodeJunctionDemand &demand : junction->demands)
            total_base_demand_m3_per_h += demand.base_demand_m3_per_h;

        const int demand_count = junction->demands.size();
        this->label_demands_summary->setText(
            QStringLiteral("%1 demand categor%2\nTotal base demand: %3 m³/h")
                .arg(demand_count)
                .arg(demand_count == 1 ? QStringLiteral("y") : QStringLiteral("ies"))
                .arg(total_base_demand_m3_per_h, 0, 'f', 3)
        );
    }

    if (this->spin_emitter_coefficient)
    {
        const QSignalBlocker emitter_blocker(this->spin_emitter_coefficient);
        this->spin_emitter_coefficient->setValue(
            junction->emitter_coefficient_m3_per_h_per_m_exponent);
    }

    if (!this->table_demands)
        return;

    if (this->table_demands->rowCount() != junction->demands.size())
    {
        rebuildJunctionDemandRows(junction.value());
        return;
    }

    for (int demand_index = 0; demand_index < junction->demands.size(); demand_index++)
        updateJunctionDemandRow(demand_index, junction->demands.at(demand_index));
}

void EntityInspectorWidget::rebuildJunctionDemandRows(const HydraulicNodeJunction &junction)
{
    if (!this->table_demands)
        return;

    this->table_demands->setUpdatesEnabled(false);
    this->table_demands->clearContents();
    this->table_demands->setRowCount(junction.demands.size());

    for (int demand_index = 0; demand_index < junction.demands.size(); demand_index++)
        addJunctionDemandRow(demand_index, junction.demands.at(demand_index));

    this->table_demands->setUpdatesEnabled(true);
}

void EntityInspectorWidget::addJunctionDemandRow(
    int demand_index, const HydraulicNodeJunctionDemand &demand)
{
    if (!this->table_demands)
        return;

    QLineEdit *line_category = new QLineEdit(this->table_demands);
    QDoubleSpinBox *spin_base_demand = new QDoubleSpinBox(this->table_demands);
    spin_base_demand->setRange(-1000000000.0, 1000000000.0);
    spin_base_demand->setDecimals(6);
    spin_base_demand->setSingleStep(0.1);
    spin_base_demand->setSuffix(QStringLiteral(" m³/h"));
    spin_base_demand->setAlignment(Qt::AlignRight);

    QComboBox *combo_pattern = new QComboBox(this->table_demands);

    QComboBox *combo_source = new QComboBox(this->table_demands);
    combo_source->addItem(
        "Manual Estimation",
        static_cast<int>(HydraulicNodeJunctionDemandSourceMethod::ManualEstimation));
    combo_source->addItem(
        "Meter Data",
        static_cast<int>(HydraulicNodeJunctionDemandSourceMethod::MeterData));
    combo_source->addItem(
        "Scenario",
        static_cast<int>(HydraulicNodeJunctionDemandSourceMethod::Scenario));

    QLineEdit *line_note = new QLineEdit(this->table_demands);
    QPushButton *button_delete = new QPushButton(QIcon(":/icon/remove.png"), "", this->table_demands);
    button_delete->setToolTip("Delete this demand category");
    button_delete->setMaximumWidth(35);

    this->table_demands->setCellWidget(demand_index, 0, line_category);
    this->table_demands->setCellWidget(demand_index, 1, spin_base_demand);
    this->table_demands->setCellWidget(demand_index, 2, combo_pattern);
    this->table_demands->setCellWidget(demand_index, 3, combo_source);
    this->table_demands->setCellWidget(demand_index, 4, line_note);
    this->table_demands->setCellWidget(demand_index, 5, button_delete);

    updateJunctionDemandRow(demand_index, demand);

    connect(line_category, &QLineEdit::textEdited, this, [this, demand_index](const QString &category_name)
    {
        this->hydraulic_data->setJunctionDemandCategoryName(
            this->entity_uuid, demand_index, category_name);
    });
    connect(spin_base_demand, &QDoubleSpinBox::valueChanged, this, [this, demand_index](double base_demand_m3_per_h)
    {
        this->hydraulic_data->setJunctionDemandBaseDemandM3PerH(
            this->entity_uuid, demand_index, base_demand_m3_per_h);
    });
    connect(combo_pattern, &QComboBox::currentIndexChanged, this, [this, demand_index, combo_pattern](int)
    {
        const HydraulicTimePatternMode pattern_mode =
            static_cast<HydraulicTimePatternMode>(
                combo_pattern->currentData(pattern_mode_role).toInt());
        const QUuid pattern_uuid = combo_pattern->currentData(pattern_uuid_role).toUuid();

        this->hydraulic_data->setJunctionDemandPatternMode(
            this->entity_uuid, demand_index, pattern_mode);
        this->hydraulic_data->setJunctionDemandPatternUuid(
            this->entity_uuid, demand_index, pattern_uuid);
    });
    connect(combo_source, &QComboBox::currentIndexChanged, this, [this, demand_index, combo_source](int)
    {
        const HydraulicNodeJunctionDemandSourceMethod source_method =
            static_cast<HydraulicNodeJunctionDemandSourceMethod>(
                combo_source->currentData().toInt());
        this->hydraulic_data->setJunctionDemandSourceMethod(
            this->entity_uuid, demand_index, source_method);
    });
    connect(line_note, &QLineEdit::textEdited, this, [this, demand_index](const QString &note)
    {
        this->hydraulic_data->setJunctionDemandNote(this->entity_uuid, demand_index, note);
    });
    connect(button_delete, &QPushButton::clicked, this, [this, demand_index]()
    {
        this->hydraulic_data->removeJunctionDemand(this->entity_uuid, demand_index);
    });
}

void EntityInspectorWidget::updateJunctionDemandRow(
    int demand_index, const HydraulicNodeJunctionDemand &demand)
{
    if (!this->table_demands || demand_index < 0 || demand_index >= this->table_demands->rowCount())
        return;

    QLineEdit *line_category = qobject_cast<QLineEdit *>(
        this->table_demands->cellWidget(demand_index, 0));
    QDoubleSpinBox *spin_base_demand = qobject_cast<QDoubleSpinBox *>(
        this->table_demands->cellWidget(demand_index, 1));
    QComboBox *combo_pattern = qobject_cast<QComboBox *>(
        this->table_demands->cellWidget(demand_index, 2));
    QComboBox *combo_source = qobject_cast<QComboBox *>(
        this->table_demands->cellWidget(demand_index, 3));
    QLineEdit *line_note = qobject_cast<QLineEdit *>(
        this->table_demands->cellWidget(demand_index, 4));

    if (line_category && line_category->text() != demand.category_name)
    {
        const QSignalBlocker category_blocker(line_category);
        line_category->setText(demand.category_name);
    }

    if (spin_base_demand)
    {
        const QSignalBlocker base_demand_blocker(spin_base_demand);
        spin_base_demand->setValue(demand.base_demand_m3_per_h);
    }

    if (combo_pattern)
    {
        populateTimePatternCombo(
            combo_pattern, demand.pattern_mode, demand.pattern_uuid);
    }

    if (combo_source)
    {
        const int source_index = combo_source->findData(static_cast<int>(demand.source_method));
        const QSignalBlocker source_blocker(combo_source);
        combo_source->setCurrentIndex(source_index >= 0 ? source_index : 0);
    }

    if (line_note && line_note->text() != demand.note)
    {
        const QSignalBlocker note_blocker(line_note);
        line_note->setText(demand.note);
    }
}

void EntityInspectorWidget::populateTimePatternCombo(
    QComboBox *combo_pattern, HydraulicTimePatternMode pattern_mode,
    const QUuid &pattern_uuid)
{
    if (!combo_pattern || !this->hydraulic_data)
        return;

    const QList<HydraulicPatternTime> &patterns =
        this->hydraulic_data->networkHydraulic().patterns_time;
    QString pattern_signature = QString::number(static_cast<int>(pattern_mode));
    pattern_signature += QLatin1Char(':');
    pattern_signature += pattern_uuid.toString(QUuid::WithoutBraces);
    pattern_signature += QLatin1Char('\n');

    for (const HydraulicPatternTime &pattern : patterns)
    {
        pattern_signature += pattern.uuid.toString(QUuid::WithoutBraces);
        pattern_signature += QLatin1Char(':');
        pattern_signature += pattern.id;
        pattern_signature += QLatin1Char('\n');
    }

    bool pattern_exists = false;
    if (pattern_mode == HydraulicTimePatternMode::TimePattern && !pattern_uuid.isNull())
    {
        for (const HydraulicPatternTime &pattern : patterns)
        {
            if (pattern.uuid == pattern_uuid)
            {
                pattern_exists = true;
                break;
            }
        }
    }

    const QSignalBlocker pattern_blocker(combo_pattern);
    if (combo_pattern->property("aowisPatternSignature").toString() != pattern_signature)
    {
        combo_pattern->clear();

        combo_pattern->addItem("Constant");
        combo_pattern->setItemData(
            combo_pattern->count() - 1,
            static_cast<int>(HydraulicTimePatternMode::Constant), pattern_mode_role);
        combo_pattern->setItemData(
            combo_pattern->count() - 1, QUuid(), pattern_uuid_role);

        combo_pattern->addItem("[Select Pattern]");
        combo_pattern->setItemData(
            combo_pattern->count() - 1,
            static_cast<int>(HydraulicTimePatternMode::TimePattern), pattern_mode_role);
        combo_pattern->setItemData(
            combo_pattern->count() - 1, QUuid(), pattern_uuid_role);

        for (const HydraulicPatternTime &pattern : patterns)
        {
            const QString pattern_name = pattern.id.isEmpty()
                ? pattern.uuid.toString(QUuid::WithoutBraces)
                : pattern.id;
            combo_pattern->addItem(pattern_name);
            combo_pattern->setItemData(
                combo_pattern->count() - 1,
                static_cast<int>(HydraulicTimePatternMode::TimePattern), pattern_mode_role);
            combo_pattern->setItemData(
                combo_pattern->count() - 1, pattern.uuid, pattern_uuid_role);
        }

        if (pattern_mode == HydraulicTimePatternMode::TimePattern &&
            !pattern_uuid.isNull() && !pattern_exists)
        {
            combo_pattern->addItem(
                QStringLiteral("[Missing Pattern] %1").arg(
                    pattern_uuid.toString(QUuid::WithoutBraces)));
            combo_pattern->setItemData(
                combo_pattern->count() - 1,
                static_cast<int>(HydraulicTimePatternMode::TimePattern), pattern_mode_role);
            combo_pattern->setItemData(
                combo_pattern->count() - 1, pattern_uuid, pattern_uuid_role);
        }

        combo_pattern->setProperty("aowisPatternSignature", pattern_signature);
    }

    int selected_index = -1;
    for (int item_index = 0; item_index < combo_pattern->count(); item_index++)
    {
        const HydraulicTimePatternMode item_mode =
            static_cast<HydraulicTimePatternMode>(
                combo_pattern->itemData(item_index, pattern_mode_role).toInt());
        const QUuid item_uuid = combo_pattern->itemData(item_index, pattern_uuid_role).toUuid();

        const bool mode_matches = item_mode == pattern_mode;
        const bool uuid_matches = pattern_mode == HydraulicTimePatternMode::Constant ||
            item_uuid == pattern_uuid;

        if (mode_matches && uuid_matches)
        {
            selected_index = item_index;
            break;
        }
    }

    combo_pattern->setCurrentIndex(selected_index >= 0 ? selected_index : 0);
}

void EntityInspectorWidget::openDemandsEditor()
{
    if (this->dialog_demands)
    {
        this->dialog_demands->raise();
        this->dialog_demands->activateWindow();
        return;
    }

    this->dialog_demands = new QDialog(this);
    this->dialog_demands->setWindowTitle("Junction Demands");
    this->dialog_demands->resize(950, 420);
    this->dialog_demands->setAttribute(Qt::WA_DeleteOnClose);

    QGridLayout *grid = new QGridLayout(this->dialog_demands);

    this->table_demands = new QTableWidget(this->dialog_demands);
    this->table_demands->setColumnCount(6);
    this->table_demands->setHorizontalHeaderLabels(
        QStringList{"Category", "Base Demand", "Pattern", "Source / Method", "Note", ""});
    this->table_demands->verticalHeader()->setVisible(false);
    this->table_demands->setSelectionMode(QAbstractItemView::NoSelection);
    this->table_demands->horizontalHeader()->setStretchLastSection(false);
    this->table_demands->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    this->table_demands->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    this->table_demands->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    this->table_demands->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    this->table_demands->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    this->table_demands->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    this->table_demands->setColumnWidth(0, 150);
    this->table_demands->setColumnWidth(2, 150);
    this->table_demands->setColumnWidth(5, 35);

    QPushButton *button_demand = new QPushButton("Add Demand");
    connect(button_demand, &QPushButton::clicked, this, [this]()
    {
        HydraulicNodeJunctionDemand demand;
        this->hydraulic_data->addJunctionDemand(this->entity_uuid, demand);
    });

    QPushButton *button_patterns = new QPushButton("Manage Patterns");

    grid->addWidget(this->table_demands, 0, 0, 1, 2);
    grid->addWidget(button_demand, 1, 0);
    grid->addWidget(button_patterns, 1, 1);

    connect(this->dialog_demands, &QObject::destroyed, this, [this]()
    {
        this->dialog_demands = nullptr;
        this->table_demands = nullptr;
    });

    refreshJunctionDemands();
    this->dialog_demands->show();
}

void EntityInspectorWidget::addGroupHistory()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("History");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    this->layoutHistory()->addWidget(group);
}

void EntityInspectorWidget::addStretches()
{
    this->layoutOverview()->addStretch();
    this->layoutConfiguration()->addStretch();
    this->layoutSimMeas()->addStretch();
    this->layoutQuality()->addStretch();
    this->layoutHistory()->addStretch();
}
