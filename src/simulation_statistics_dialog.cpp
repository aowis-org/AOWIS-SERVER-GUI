#include "simulation_statistics_dialog.h"

#include "hydraulic_data.h"

#include <cmath>

#include <QAbstractItemView>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace
{
struct StatisticExtreme
{
    double value = 0.0;
    int result_index = -1;
};

struct IntegerStatisticExtreme
{
    qint64 value = 0;
    int result_index = -1;
};

struct AggregateStatistics
{
    IntegerStatisticExtreme hydraulic_iterations;
    StatisticExtreme relative_error;
    StatisticExtreme maximum_head_error_m;
    StatisticExtreme maximum_flow_change_m3_per_h;
    IntegerStatisticExtreme deficient_nodes;
    StatisticExtreme demand_reduction_percent;
    StatisticExtreme leakage_loss_percent;
};

QString formatElapsedTime(quint64 time_elapsed_s)
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

QString formatDouble(double value)
{
    if (!std::isfinite(value))
        return QStringLiteral("—");

    const double absolute_value = std::abs(value);
    if (absolute_value != 0.0 && (absolute_value < 0.0001 || absolute_value >= 1000000.0))
        return QString::number(value, 'e', 4);

    QString text = QString::number(value, 'f', 6);
    while (text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text;
}

QString statisticTime(const QList<HydraulicSimulationResult> &results, int result_index)
{
    if (result_index < 0 || result_index >= results.size())
        return QStringLiteral("—");

    return formatElapsedTime(results.at(result_index).time_elapsed_s);
}

void updateMaximum(StatisticExtreme &extreme, double value, int result_index)
{
    if (extreme.result_index < 0 || value > extreme.value)
    {
        extreme.value = value;
        extreme.result_index = result_index;
    }
}

void updateMaximum(IntegerStatisticExtreme &extreme, qint64 value, int result_index)
{
    if (extreme.result_index < 0 || value > extreme.value)
    {
        extreme.value = value;
        extreme.result_index = result_index;
    }
}

AggregateStatistics aggregateStatistics(const QList<HydraulicSimulationResult> &results)
{
    AggregateStatistics aggregate;

    for (int result_index = 0; result_index < results.size(); ++result_index)
    {
        const HydraulicSimulationResultStatistics &statistics = results.at(result_index).statistics;
        updateMaximum(aggregate.hydraulic_iterations, statistics.hydraulic_iterations, result_index);
        updateMaximum(aggregate.relative_error, statistics.relative_error, result_index);
        updateMaximum(aggregate.maximum_head_error_m, statistics.maximum_head_error_m, result_index);
        updateMaximum(aggregate.maximum_flow_change_m3_per_h, statistics.maximum_flow_change_m3_per_h, result_index);
        updateMaximum(aggregate.deficient_nodes, statistics.deficient_nodes, result_index);
        updateMaximum(aggregate.demand_reduction_percent, statistics.demand_reduction_percent, result_index);
        updateMaximum(aggregate.leakage_loss_percent, statistics.leakage_loss_percent, result_index);
    }

    return aggregate;
}

QTreeWidgetItem *addSummaryGroup(QTreeWidget *tree, const QString &title)
{
    QTreeWidgetItem *group = new QTreeWidgetItem(tree, QStringList{title});
    group->setFirstColumnSpanned(true);

    QFont font = group->font(0);
    font.setBold(true);
    group->setFont(0, font);
    return group;
}

void addSummaryValue(QTreeWidgetItem *group, const QString &name, const QString &value, const QString &time = QString())
{
    QTreeWidgetItem *item = new QTreeWidgetItem(group);
    item->setText(0, name);
    item->setText(1, value);
    item->setText(2, time);
    item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
}

QTableWidgetItem *numericTableItem(const QString &text)
{
    QTableWidgetItem *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}
}

SimulationStatisticsDialog::SimulationStatisticsDialog(HydraulicData *hydraulic_data, QWidget *parent)
    : QDialog(parent),
    hydraulic_data(hydraulic_data),
    tree_summary(new QTreeWidget(this)),
    table_timeline(new QTableWidget(this))
{
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMaximizeButtonHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Simulation Statistics"));
    setModal(false);
    resize(1050, 650);

    this->tree_summary->setColumnCount(3);
    this->tree_summary->setHeaderLabels(QStringList{tr("Statistic"), tr("Value"), tr("Occurred at")});
    this->tree_summary->setRootIsDecorated(false);
    this->tree_summary->setAlternatingRowColors(true);
    this->tree_summary->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    this->tree_summary->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    this->tree_summary->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    this->table_timeline->setColumnCount(8);
    this->table_timeline->setHorizontalHeaderLabels(QStringList{
        tr("Elapsed"),
        tr("Iterations"),
        tr("Relative\nerror"),
        tr("Max head\nerror [m]"),
        tr("Max flow\nchange [m³/h]"),
        tr("Deficient\nnodes"),
        tr("Demand\nreduction [%]"),
        tr("Leakage\nloss [%]")
    });
    this->table_timeline->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->table_timeline->setSelectionBehavior(QAbstractItemView::SelectRows);
    this->table_timeline->setSelectionMode(QAbstractItemView::SingleSelection);
    this->table_timeline->setAlternatingRowColors(true);
    this->table_timeline->verticalHeader()->setVisible(false);
    this->table_timeline->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    this->table_timeline->horizontalHeader()->setMinimumHeight(this->table_timeline->horizontalHeader()->fontMetrics().height() * 2 + 10);
    this->table_timeline->horizontalHeader()->setStretchLastSection(true);

    QTabWidget *tabs = new QTabWidget(this);
    tabs->addTab(this->tree_summary, tr("Summary"));
    tabs->addTab(this->table_timeline, tr("By timestep"));

    QLabel *hint = new QLabel(
        tr("Summary values show the worst hydraulic value across the complete timeline."),
        this);
    hint->setWordWrap(true);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(tabs);

    connect(this->hydraulic_data, &HydraulicData::signalSimulationResultTimelineChanged, this, [this](bool)
    {
        refresh();
    });
    connect(this->hydraulic_data, &HydraulicData::signalCurrentSimulationResultChanged,
            this, &SimulationStatisticsDialog::syncCurrentResultSelection);

    refresh();
}

void SimulationStatisticsDialog::refresh()
{
    refreshSummary();
    refreshTimeline();
    syncCurrentResultSelection(this->hydraulic_data->currentSimulationResultIndex());
}

void SimulationStatisticsDialog::refreshSummary()
{
    this->tree_summary->clear();

    const std::optional<HydraulicSimulationResultTimeline> &timeline_optional = this->hydraulic_data->simulationResultTimeline();
    if (!this->hydraulic_data->hasSimulationResults() || !timeline_optional.has_value())
    {
        QTreeWidgetItem *group = addSummaryGroup(this->tree_summary, tr("Simulation"));
        addSummaryValue(group, tr("Result"), tr("No valid simulation results available"));
        group->setExpanded(true);
        return;
    }

    const HydraulicSimulationResultTimeline &timeline = timeline_optional.value();
    const QList<HydraulicSimulationResult> &results = timeline.results;
    const AggregateStatistics aggregate = aggregateStatistics(results);

    QTreeWidgetItem *simulation_group = addSummaryGroup(this->tree_summary, tr("Simulation"));
    addSummaryValue(simulation_group, tr("Status"), timeline.status.success ? tr("Successful") : tr("Failed"));
    addSummaryValue(simulation_group, tr("Result timesteps"), QString::number(results.size()));
    addSummaryValue(simulation_group, tr("Duration"), formatElapsedTime(results.constLast().time_elapsed_s));
    addSummaryValue(
        simulation_group,
        tr("Start UTC"),
        timeline.simulation_start_utc.isValid()
            ? timeline.simulation_start_utc.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss 'UTC'"))
            : QStringLiteral("—"));

    QTreeWidgetItem *hydraulics_group = addSummaryGroup(this->tree_summary, tr("Hydraulic convergence"));
    addSummaryValue(hydraulics_group, tr("Maximum iterations"),
                    QString::number(aggregate.hydraulic_iterations.value),
                    statisticTime(results, aggregate.hydraulic_iterations.result_index));
    addSummaryValue(hydraulics_group, tr("Maximum relative error"),
                    formatDouble(aggregate.relative_error.value),
                    statisticTime(results, aggregate.relative_error.result_index));
    addSummaryValue(hydraulics_group, tr("Maximum head error"),
                    tr("%1 m").arg(formatDouble(aggregate.maximum_head_error_m.value)),
                    statisticTime(results, aggregate.maximum_head_error_m.result_index));
    addSummaryValue(hydraulics_group, tr("Maximum flow change"),
                    tr("%1 m³/h").arg(formatDouble(aggregate.maximum_flow_change_m3_per_h.value)),
                    statisticTime(results, aggregate.maximum_flow_change_m3_per_h.result_index));

    QTreeWidgetItem *demand_group = addSummaryGroup(this->tree_summary, tr("Pressure-dependent demand"));
    addSummaryValue(demand_group, tr("Maximum deficient nodes"),
                    QString::number(aggregate.deficient_nodes.value),
                    statisticTime(results, aggregate.deficient_nodes.result_index));
    addSummaryValue(demand_group, tr("Maximum demand reduction"),
                    tr("%1 %").arg(formatDouble(aggregate.demand_reduction_percent.value)),
                    statisticTime(results, aggregate.demand_reduction_percent.result_index));

    QTreeWidgetItem *leakage_group = addSummaryGroup(this->tree_summary, tr("Leakage"));
    addSummaryValue(leakage_group, tr("Maximum leakage loss"),
                    tr("%1 %").arg(formatDouble(aggregate.leakage_loss_percent.value)),
                    statisticTime(results, aggregate.leakage_loss_percent.result_index));


    for (int top_level_index = 0; top_level_index < this->tree_summary->topLevelItemCount(); ++top_level_index)
        this->tree_summary->topLevelItem(top_level_index)->setExpanded(true);
}

void SimulationStatisticsDialog::refreshTimeline()
{
    const QSignalBlocker blocker(this->table_timeline);
    this->table_timeline->setRowCount(0);

    const std::optional<HydraulicSimulationResultTimeline> &timeline_optional = this->hydraulic_data->simulationResultTimeline();
    if (!this->hydraulic_data->hasSimulationResults() || !timeline_optional.has_value())
        return;

    const QList<HydraulicSimulationResult> &results = timeline_optional->results;
    this->table_timeline->setRowCount(results.size());

    for (int result_index = 0; result_index < results.size(); ++result_index)
    {
        const HydraulicSimulationResult &result = results.at(result_index);
        const HydraulicSimulationResultStatistics &statistics = result.statistics;

        this->table_timeline->setItem(result_index, 0, new QTableWidgetItem(formatElapsedTime(result.time_elapsed_s)));
        this->table_timeline->setItem(result_index, 1, numericTableItem(QString::number(statistics.hydraulic_iterations)));
        this->table_timeline->setItem(result_index, 2, numericTableItem(formatDouble(statistics.relative_error)));
        this->table_timeline->setItem(result_index, 3, numericTableItem(formatDouble(statistics.maximum_head_error_m)));
        this->table_timeline->setItem(result_index, 4, numericTableItem(formatDouble(statistics.maximum_flow_change_m3_per_h)));
        this->table_timeline->setItem(result_index, 5, numericTableItem(QString::number(statistics.deficient_nodes)));
        this->table_timeline->setItem(result_index, 6, numericTableItem(formatDouble(statistics.demand_reduction_percent)));
        this->table_timeline->setItem(result_index, 7, numericTableItem(formatDouble(statistics.leakage_loss_percent)));
    }
}

void SimulationStatisticsDialog::syncCurrentResultSelection(int result_index)
{
    const QSignalBlocker blocker(this->table_timeline);

    if (result_index < 0 || result_index >= this->table_timeline->rowCount())
    {
        this->table_timeline->clearSelection();
        return;
    }

    this->table_timeline->selectRow(result_index);
    this->table_timeline->scrollToItem(this->table_timeline->item(result_index, 0), QAbstractItemView::EnsureVisible);
}
