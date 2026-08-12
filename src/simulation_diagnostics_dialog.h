#ifndef SIMULATION_DIAGNOSTICS_DIALOG_H
#define SIMULATION_DIAGNOSTICS_DIALOG_H

#include <QDialog>

class HydraulicData;
class QTableWidget;
class QTextBrowser;

class SimulationDiagnosticsDialog : public QDialog
{
public:
    explicit SimulationDiagnosticsDialog(HydraulicData *hydraulic_data, QWidget *parent = nullptr);

private:
    void refresh();
    void showDiagnosticDetails(int diagnostic_index);

    HydraulicData *hydraulic_data = nullptr;
    QTableWidget *table_diagnostics = nullptr;
    QTextBrowser *text_details = nullptr;
};

#endif // SIMULATION_DIAGNOSTICS_DIALOG_H
