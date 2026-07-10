#ifndef SIMULATION_MANAGER_H
#define SIMULATION_MANAGER_H

#include <QObject>

#include "epanet_wrapper.h"
#include "model/simulation_request.h"
#include "model/simulation_result.h"
#include "model/epanet_status.h"

class SimulationManager : public QObject
{
    Q_OBJECT
public:
    explicit SimulationManager(QObject *parent = nullptr);
    
    void run();

signals:
};

#endif // SIMULATION_MANAGER_H
