#ifndef SIMULATION_STATISTICS_DIALOG_H
#define SIMULATION_STATISTICS_DIALOG_H

#include <QDialog>
#include <QString>

class HydraulicData;
class QTableWidget;
class QTabWidget;
class QTextBrowser;
class QTreeWidget;
class SimulationDiagnosticsWidget;

class SimulationStatisticsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SimulationStatisticsDialog(HydraulicData *hydraulic_data, QWidget *parent = nullptr);
    void setEpanetLog(const QString &epanet_log);
    void showEpanetLogTab();
    void showDiagnosticsTab();

private:
    HydraulicData *hydraulic_data = nullptr;
    SimulationDiagnosticsWidget *widget_diagnostics = nullptr;
    QTreeWidget *tree_summary = nullptr;
    QTableWidget *table_timeline = nullptr;
    QTabWidget *tabs = nullptr;
    QTextBrowser *text_epanet_log = nullptr;

    void refresh();
    void refreshSummary();
    void refreshTimeline();
    void syncCurrentResultSelection(int result_index);
};
#endif // SIMULATION_STATISTICS_DIALOG_H
