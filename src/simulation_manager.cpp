#include "simulation_manager.h"

SimulationManager::SimulationManager(QObject *parent)
    : QObject{parent}
{
    
}

void SimulationManager::run()
{
    // create the network for now here as dummies
    Reservoir reservoir;
    reservoir.id = "R1";
    reservoir.head_m = 30.0;
    
    Junction junction;
    junction.id = "J1";
    junction.elevation_m = 0.0;
    junction.demand_lps = 1.0;
    
    Pipe pipe;
    pipe.id = "P1";
    pipe.node_id_from = reservoir.id;
    pipe.node_id_to = junction.id;
    pipe.length_m = 100.0;
    pipe.diameter_mm = 150.0;
    pipe.roughness_hw = 130.0;
    pipe.minor_loss = 0.0;
    pipe.open = true;
    
    SimulationRequest request;
    request.reservoirs.append(reservoir);
    request.junctions.append(junction);
    request.pipes.append(pipe);
    
    EpanetWrapper *epanet = new EpanetWrapper(this);
    epanet->run(request);
    qDebug().noquote() << epanet->reportText();
}
