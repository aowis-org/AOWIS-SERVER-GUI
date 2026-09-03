#ifndef SIMULATION_DIAGNOSTICS_DIALOG_H
#define SIMULATION_DIAGNOSTICS_DIALOG_H

#include <QDialog>

class HydraulicData;
class QLabel;
class QListWidget;
class QTextBrowser;

class SimulationDiagnosticsDialog : public QDialog
{
public:
    explicit SimulationDiagnosticsDialog(HydraulicData *hydraulic_data, QWidget *parent = nullptr);

private:
    void refresh();
    void showDiagnosticDetails(int diagnostic_index);

    HydraulicData *hydraulic_data = nullptr;
    QLabel *label_result_validity = nullptr;
    QListWidget *list_diagnostics = nullptr;
    QTextBrowser *text_details = nullptr;
};

#endif // SIMULATION_DIAGNOSTICS_DIALOG_H
