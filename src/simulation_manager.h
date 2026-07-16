#ifndef SIMULATION_MANAGER_H
#define SIMULATION_MANAGER_H

#include <QObject>
#include <QApplication>
#include <QScreen>
#include <QTextBrowser>
#include <QFontDatabase>
#include <QDialog>
#include <QVBoxLayout>

#include "epanet_wrapper.h"

#include <aowis/model/hydraulic/network.h>
#include <aowis/model/hydraulic/simulation_result.h>
#include <aowis/model/hydraulic/epanet_status.h>

#include "network_data.h"

class SimulationManager : public QObject
{
    Q_OBJECT
public:
    explicit SimulationManager(NetworkData *data, QObject *parent = nullptr);
    
    void run();
    void showEpanetLog();
    
private:
    NetworkData *network_data = nullptr;
    QString epanet_log;

signals:
};

#endif // SIMULATION_MANAGER_H
