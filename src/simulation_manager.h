#ifndef SIMULATION_MANAGER_H
#define SIMULATION_MANAGER_H

#include <QObject>
#include <QApplication>
#include <QScreen>
#include <QTextBrowser>
#include <QFontDatabase>
#include <QDialog>
#include <QVBoxLayout>

#include <aowis/model/hydraulic/network_hydraulic.h>
#include <aowis/model/hydraulic/hydraulic_simulation_results.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

#include "hydraulic_data.h"

class SimulationManager : public QObject
{
    Q_OBJECT
public:
    explicit SimulationManager(HydraulicData *hydraulic_data, QObject *parent = nullptr);
    
    void run();
    void showEpanetLog();
    void exportEpanetNetwork();
    
private:
    HydraulicData *hydraulic_data = nullptr;
    QString epanet_log;

signals:
};

#endif // SIMULATION_MANAGER_H
