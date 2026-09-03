#ifndef SIMULATION_DIAGNOSTIC_H
#define SIMULATION_DIAGNOSTIC_H

#include <QUuid>

#include <aowis/model/hydraulic/hydraulic_simulation_diagnostics.h>

#include "common/_enums_structs.h"

using SimulationDiagnosticSeverity = HydraulicSimulationDiagnosticSeverity;
using SimulationDiagnostic = HydraulicSimulationDiagnostic;

struct SimulationDiagnosticEntityReference
{
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    QUuid uuid;
};

#endif // SIMULATION_DIAGNOSTIC_H
