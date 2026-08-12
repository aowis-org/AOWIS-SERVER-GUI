#ifndef SIMULATION_MANAGER_H
#define SIMULATION_MANAGER_H

#include <QObject>
#include <QApplication>
#include <QScreen>
#include <QTextBrowser>
#include <QFontDatabase>
#include <QDialog>
#include <QPointer>
#include <QProgressDialog>
#include <QThread>
#include <QVBoxLayout>

#include <aowis/model/hydraulic/network_hydraulic.h>
#include <aowis/model/hydraulic/hydraulic_simulation_results.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

#include "hydraulic_data.h"

struct EpanetResultRun;

class SimulationManager : public QObject
{
    Q_OBJECT
public:
    explicit SimulationManager(HydraulicData *hydraulic_data, QObject *parent = nullptr);
    ~SimulationManager() override;
    
    void run();
    void showSimulationStatistics();
    void showSimulationDiagnostics();
    void showEpanetLog();
    void exportEpanetNetwork();
    
private:
    HydraulicData *hydraulic_data = nullptr;
    QString epanet_log;
    QPointer<QDialog> dialog_simulation_statistics = nullptr;
    QPointer<QDialog> dialog_simulation_diagnostics = nullptr;
    QPointer<QDialog> dialog_epanet_log = nullptr;
    QPointer<QProgressDialog> dialog_simulation_progress = nullptr;
    QPointer<QThread> simulation_thread = nullptr;
    bool simulation_running = false;
    QPointer<QTextBrowser> widget_epanet_log = nullptr;

    void showSimulationProgress();
    void closeSimulationProgress();
    void finishSimulation(const EpanetResultRun &run_result);

signals:
    void signalEpanetLogAvailabilityChanged(bool available);
};

#endif // SIMULATION_MANAGER_H
