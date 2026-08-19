#ifndef NETWORK_SYMBOLOGY_VALUES_H
#define NETWORK_SYMBOLOGY_VALUES_H

#include <QHash>
#include <QUuid>

#include <aowis/model/hydraulic/network_hydraulic.h>
#include <aowis/model/hydraulic/water_quality_simulation_results.h>

inline double resolvedSymbologyElevationM(const HydraulicNodeJunction &junction)
{
    if (junction.elevation_input_type ==
        HydraulicNodeElevationInputType::TerrainElevationAndOffset)
    {
        return junction.terrain_elevation_m + junction.elevation_offset_m;
    }

    return junction.elevation_m;
}

inline double resolvedSymbologyElevationM(const HydraulicNodeReservoir &reservoir)
{
    if (reservoir.head_input_type ==
        HydraulicNodeElevationInputType::TerrainElevationAndOffset)
    {
        return reservoir.terrain_elevation_m + reservoir.hydraulic_head_offset_m;
    }

    return reservoir.hydraulic_head_m;
}

inline double resolvedSymbologyElevationM(const HydraulicNodeTank &tank)
{
    if (tank.elevation_input_type ==
        HydraulicNodeTankElevationInputType::TerrainElevationAndOffset)
    {
        return tank.terrain_elevation_m + tank.bottom_offset_m;
    }

    return tank.bottom_elevation_m;
}

inline QHash<QUuid, double> waterAgeNodeSymbologyValues(
    const WaterQualitySimulationResult &result)
{
    QHash<QUuid, double> values;
    values.reserve(result.nodes_junctions.size() + result.nodes_reservoirs.size()
                   + result.nodes_tanks.size());

    for (const WaterQualitySimulationResultNodeJunction &junction : result.nodes_junctions)
        values.insert(junction.uuid, junction.water_age_h);
    for (const WaterQualitySimulationResultNodeReservoir &reservoir : result.nodes_reservoirs)
        values.insert(reservoir.uuid, reservoir.water_age_h);
    for (const WaterQualitySimulationResultNodeTank &tank : result.nodes_tanks)
        values.insert(tank.uuid, tank.water_age_h);

    return values;
}

inline QHash<QUuid, double> waterAgeLinkSymbologyValues(
    const WaterQualitySimulationResult &result)
{
    QHash<QUuid, double> values;
    values.reserve(result.links_pipes.size() + result.links_pumps.size()
                   + result.links_valves.size());

    for (const WaterQualitySimulationResultLinkPipe &pipe : result.links_pipes)
        values.insert(pipe.uuid, pipe.water_age_h);
    for (const WaterQualitySimulationResultLinkPump &pump : result.links_pumps)
        values.insert(pump.uuid, pump.water_age_h);
    for (const WaterQualitySimulationResultLinkValve &valve : result.links_valves)
        values.insert(valve.uuid, valve.water_age_h);

    return values;
}

#endif // NETWORK_SYMBOLOGY_VALUES_H
