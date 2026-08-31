#ifndef SIMULATION_MANAGER_H
#define SIMULATION_MANAGER_H

#include <QObject>
#include <QByteArray>
#include <QApplication>
#include <QScreen>
#include <QTextBrowser>
#include <QFontDatabase>
#include <QDialog>
#include <QList>
#include <QPointer>
#include <QThread>
#include <QVBoxLayout>

#include <atomic>
#include <memory>
#include <aowis/model/hydraulic/network_hydraulic.h>
#include <aowis/model/hydraulic/hydraulic_simulation_results.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/hydraulic_types.h>
#include "hydraulic_data.h"

struct EpanetResultImport;
struct EpanetResultRun;

class SimulationManager : public QObject
{
    Q_OBJECT
public:
    explicit SimulationManager(HydraulicData *hydraulic_data, QObject *parent = nullptr);
    ~SimulationManager() override;

    void runOrStop(const QList<WaterQualityAnalysisType> &quality_analyses);
    void run(const QList<WaterQualityAnalysisType> &quality_analyses);
    void stop();
    void showSimulationStatistics();
    void showSimulationDiagnostics();
    void showEpanetLog();
    void importEpanetNetwork();
    void importEpanetNetworkResource(const QString &resource_path, const QString &file_name);
    void exportEpanetNetwork();

private slots:
    void importEpanetNetworkContent(const QString &file_name, const QByteArray &file_content);

private:
    HydraulicData *hydraulic_data = nullptr;
    QString epanet_log;
    QPointer<QDialog> dialog_simulation_statistics = nullptr;
    QPointer<QDialog> dialog_simulation_diagnostics = nullptr;
    QPointer<QThread> simulation_thread = nullptr;
    std::shared_ptr<std::atomic_bool> simulation_cancellation_flag;
    bool simulation_running = false;

    void finishSimulation(const EpanetResultRun &run_result);
    void finishEpanetNetworkImport(EpanetResultImport import_result, QWidget *parent_widget);

signals:
    void signalSimulationStarted();
    void signalSimulationStopRequested();
    void signalSimulationFinished(bool cancelled);
    void signalEpanetLogAvailabilityChanged(bool available);
    void signalEpanetNetworkImported();
};
#endif // SIMULATION_MANAGER_H
