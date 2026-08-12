#ifndef SIMULATION_STATISTICS_DIALOG_H
#define SIMULATION_STATISTICS_DIALOG_H

#include <QDialog>

class HydraulicData;
class QTableWidget;
class QTreeWidget;

class SimulationStatisticsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SimulationStatisticsDialog(HydraulicData *hydraulic_data, QWidget *parent = nullptr);

private:
    HydraulicData *hydraulic_data = nullptr;
    QTreeWidget *tree_summary = nullptr;
    QTableWidget *table_timeline = nullptr;

    void refresh();
    void refreshSummary();
    void refreshTimeline();
    void syncCurrentResultSelection(int result_index);
};

#endif // SIMULATION_STATISTICS_DIALOG_H
