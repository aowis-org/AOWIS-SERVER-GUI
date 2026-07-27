#ifndef SIMULATION_MANAGER_H
#define SIMULATION_MANAGER_H

#include <QObject>
#include <QApplication>
#include <QScreen>
#include <QTextBrowser>
#include <QFontDatabase>
#include <QDialog>
#include <QVBoxLayout>

#include <aowis/model/hydraulic/network.h>
#include <aowis/model/hydraulic/simulation_result.h>
#include <aowis/model/hydraulic/epanet_status.h>

#include <aowis/epanet/epanet_wrapper.h>

#include "hydraulic_data.h"

class SimulationManager : public QObject
{
    Q_OBJECT
public:
    explicit SimulationManager(HydraulicData *hydraulic_data, QObject *parent = nullptr);
    
    void run();
    void showEpanetLog();
    
private:
    HydraulicData *hydraulic_data = nullptr;
    QString epanet_log;

signals:
};

#endif // SIMULATION_MANAGER_H
