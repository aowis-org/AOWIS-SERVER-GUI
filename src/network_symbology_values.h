#ifndef NETWORK_SYMBOLOGY_VALUES_H
#define NETWORK_SYMBOLOGY_VALUES_H

#include <cmath>

#include <QHash>
#include <QtGlobal>
#include <QUuid>

#include <aowis/model/hydraulic/network_hydraulic.h>
#include <aowis/model/hydraulic/hydraulic_simulation_results.h>
#include <aowis/model/hydraulic/water_quality_simulation_results.h>

#include "_enums_structs.h"

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



inline QHash<QUuid, double> networkNodeSymbologyValues(
    const NetworkHydraulic &network_hydraulic, VisualNode visual_node)
{
    QHash<QUuid, double> values;

    switch (visual_node)
    {
    case VisualNode::Elevation:
        values.reserve(network_hydraulic.nodes_junctions.size() +
                       network_hydraulic.nodes_reservoirs.size() +
                       network_hydraulic.nodes_tanks.size());
        for (const HydraulicNodeJunction &junction : network_hydraulic.nodes_junctions)
            values.insert(junction.uuid, resolvedSymbologyElevationM(junction));
        for (const HydraulicNodeReservoir &reservoir : network_hydraulic.nodes_reservoirs)
            values.insert(reservoir.uuid, resolvedSymbologyElevationM(reservoir));
        for (const HydraulicNodeTank &tank : network_hydraulic.nodes_tanks)
            values.insert(tank.uuid, resolvedSymbologyElevationM(tank));
        break;
    case VisualNode::BaseDemand:
        values.reserve(network_hydraulic.nodes_junctions.size());
        for (const HydraulicNodeJunction &junction : network_hydraulic.nodes_junctions)
        {
            double base_demand_m3_per_h = 0.0;
            for (const HydraulicNodeJunctionDemand &demand : junction.demands)
                base_demand_m3_per_h += demand.base_demand_m3_per_h;
            values.insert(junction.uuid, base_demand_m3_per_h);
        }
        break;
    case VisualNode::None:
    case VisualNode::TotalDemand:
    case VisualNode::DemandDeficit:
    case VisualNode::EmitterFlow:
    case VisualNode::Leakage:
    case VisualNode::Head:
    case VisualNode::Pressure:
    case VisualNode::Chlorine:
    case VisualNode::RiverWater:
    case VisualNode::LakeWater:
    case VisualNode::WaterAge:
        break;
    }

    return values;
}

inline QHash<QUuid, double> networkLinkSymbologyValues(
    const NetworkHydraulic &network_hydraulic, VisualLink visual_link)
{
    QHash<QUuid, double> values;

    switch (visual_link)
    {
    case VisualLink::Diameter:
        values.reserve(network_hydraulic.links_pipes.size()
                       + network_hydraulic.links_valves.size());
        for (const HydraulicLinkPipe &pipe : network_hydraulic.links_pipes)
            values.insert(pipe.uuid, pipe.diameter_mm);
        for (const HydraulicLinkValve &valve : network_hydraulic.links_valves)
            values.insert(valve.uuid, valve.diameter_mm);
        break;
    case VisualLink::Length:
        values.reserve(network_hydraulic.links_pipes.size());
        for (const HydraulicLinkPipe &pipe : network_hydraulic.links_pipes)
            values.insert(pipe.uuid, pipe.length_measured_m.value_or(pipe.length_calculated_m));
        break;
    case VisualLink::Roughness:
        values.reserve(network_hydraulic.links_pipes.size());
        for (const HydraulicLinkPipe &pipe : network_hydraulic.links_pipes)
        {
            switch (network_hydraulic.options_hydraulic.headloss_formula)
            {
            case HydraulicHeadlossFormula::HazenWilliams:
                values.insert(pipe.uuid, pipe.roughness_hazen_williams);
                break;
            case HydraulicHeadlossFormula::DarcyWeisbach:
                values.insert(pipe.uuid, pipe.roughness_darcy_weisbach_mm);
                break;
            case HydraulicHeadlossFormula::ChezyManning:
                values.insert(pipe.uuid, pipe.roughness_chezy_manning);
                break;
            }
        }
        break;
    case VisualLink::None:
    case VisualLink::FlowRate:
    case VisualLink::Velocity:
    case VisualLink::HeadLoss:
    case VisualLink::Leakage:
    case VisualLink::Chlorine:
    case VisualLink::RiverWater:
    case VisualLink::LakeWater:
    case VisualLink::WaterAge:
        break;
    }

    return values;
}

inline QHash<QUuid, double> networkHeatmapSymbologyValues(
    const NetworkHydraulic &network_hydraulic, VisualHeatmap visual_heatmap)
{
    switch (visual_heatmap)
    {
    case VisualHeatmap::Elevation:
        return networkNodeSymbologyValues(network_hydraulic, VisualNode::Elevation);
    case VisualHeatmap::BaseDemand:
        return networkNodeSymbologyValues(network_hydraulic, VisualNode::BaseDemand);
    case VisualHeatmap::TotalDemand:
        return networkNodeSymbologyValues(network_hydraulic, VisualNode::TotalDemand);
    case VisualHeatmap::DemandDeficit:
        return networkNodeSymbologyValues(network_hydraulic, VisualNode::DemandDeficit);
    case VisualHeatmap::EmitterFlow:
        return networkNodeSymbologyValues(network_hydraulic, VisualNode::EmitterFlow);
    case VisualHeatmap::Leakage:
        return networkNodeSymbologyValues(network_hydraulic, VisualNode::Leakage);
    case VisualHeatmap::Head:
        return networkNodeSymbologyValues(network_hydraulic, VisualNode::Head);
    case VisualHeatmap::Pressure:
        return networkNodeSymbologyValues(network_hydraulic, VisualNode::Pressure);
    case VisualHeatmap::Chlorine:
        return networkNodeSymbologyValues(network_hydraulic, VisualNode::Chlorine);
    case VisualHeatmap::RiverWater:
        return networkNodeSymbologyValues(network_hydraulic, VisualNode::RiverWater);
    case VisualHeatmap::LakeWater:
        return networkNodeSymbologyValues(network_hydraulic, VisualNode::LakeWater);
    case VisualHeatmap::WaterAge:
    case VisualHeatmap::None:
        break;
    }

    return QHash<QUuid, double>();
}

inline bool nodeVisualUsesHydraulicSimulationResult(VisualNode visual_node)
{
    switch (visual_node)
    {
    case VisualNode::TotalDemand:
    case VisualNode::DemandDeficit:
    case VisualNode::EmitterFlow:
    case VisualNode::Leakage:
    case VisualNode::Head:
    case VisualNode::Pressure:
        return true;
    case VisualNode::None:
    case VisualNode::Elevation:
    case VisualNode::BaseDemand:
    case VisualNode::Chlorine:
    case VisualNode::RiverWater:
    case VisualNode::LakeWater:
    case VisualNode::WaterAge:
        return false;
    }

    return false;
}

inline bool linkVisualUsesHydraulicSimulationResult(VisualLink visual_link)
{
    switch (visual_link)
    {
    case VisualLink::FlowRate:
    case VisualLink::Velocity:
    case VisualLink::HeadLoss:
    case VisualLink::Leakage:
        return true;
    case VisualLink::None:
    case VisualLink::Diameter:
    case VisualLink::Length:
    case VisualLink::Roughness:
    case VisualLink::Chlorine:
    case VisualLink::RiverWater:
    case VisualLink::LakeWater:
    case VisualLink::WaterAge:
        return false;
    }

    return false;
}

inline bool heatmapVisualUsesHydraulicSimulationResult(VisualHeatmap visual_heatmap)
{
    switch (visual_heatmap)
    {
    case VisualHeatmap::TotalDemand:
    case VisualHeatmap::DemandDeficit:
    case VisualHeatmap::EmitterFlow:
    case VisualHeatmap::Leakage:
    case VisualHeatmap::Head:
    case VisualHeatmap::Pressure:
        return true;
    case VisualHeatmap::None:
    case VisualHeatmap::Elevation:
    case VisualHeatmap::BaseDemand:
    case VisualHeatmap::Chlorine:
    case VisualHeatmap::RiverWater:
    case VisualHeatmap::LakeWater:
    case VisualHeatmap::WaterAge:
        return false;
    }

    return false;
}

inline QHash<QUuid, double> hydraulicNodeSymbologyValues(
    const HydraulicSimulationResult &result, VisualNode visual_node)
{
    QHash<QUuid, double> values;

    switch (visual_node)
    {
    case VisualNode::TotalDemand:
        values.reserve(result.nodes_junctions.size() + result.nodes_reservoirs.size()
                       + result.nodes_tanks.size());
        for (const HydraulicSimulationResultNodeJunction &junction : result.nodes_junctions)
            values.insert(junction.uuid, junction.total_demand_m3_per_h);
        for (const HydraulicSimulationResultNodeReservoir &reservoir : result.nodes_reservoirs)
            values.insert(reservoir.uuid, reservoir.net_demand_m3_per_h);
        for (const HydraulicSimulationResultNodeTank &tank : result.nodes_tanks)
            values.insert(tank.uuid, tank.net_demand_m3_per_h);
        break;
    case VisualNode::DemandDeficit:
        values.reserve(result.nodes_junctions.size());
        for (const HydraulicSimulationResultNodeJunction &junction : result.nodes_junctions)
            values.insert(junction.uuid, junction.demand_deficit_m3_per_h);
        break;
    case VisualNode::EmitterFlow:
        values.reserve(result.nodes_junctions.size());
        for (const HydraulicSimulationResultNodeJunction &junction : result.nodes_junctions)
            values.insert(junction.uuid, junction.emitter_flow_m3_per_h);
        break;
    case VisualNode::Leakage:
        values.reserve(result.nodes_junctions.size());
        for (const HydraulicSimulationResultNodeJunction &junction : result.nodes_junctions)
            values.insert(junction.uuid, junction.leakage_flow_m3_per_h);
        break;
    case VisualNode::Head:
        values.reserve(result.nodes_junctions.size() + result.nodes_reservoirs.size()
                       + result.nodes_tanks.size());
        for (const HydraulicSimulationResultNodeJunction &junction : result.nodes_junctions)
            values.insert(junction.uuid, junction.hydraulic_head_m);
        for (const HydraulicSimulationResultNodeReservoir &reservoir : result.nodes_reservoirs)
            values.insert(reservoir.uuid, reservoir.hydraulic_head_m);
        for (const HydraulicSimulationResultNodeTank &tank : result.nodes_tanks)
            values.insert(tank.uuid, tank.hydraulic_head_m);
        break;
    case VisualNode::Pressure:
        values.reserve(result.nodes_junctions.size() + result.nodes_reservoirs.size()
                       + result.nodes_tanks.size());
        for (const HydraulicSimulationResultNodeJunction &junction : result.nodes_junctions)
            values.insert(junction.uuid, junction.pressure_head_m);
        for (const HydraulicSimulationResultNodeReservoir &reservoir : result.nodes_reservoirs)
            values.insert(reservoir.uuid, reservoir.pressure_head_m);
        for (const HydraulicSimulationResultNodeTank &tank : result.nodes_tanks)
            values.insert(tank.uuid, tank.pressure_head_m);
        break;
    case VisualNode::None:
    case VisualNode::Elevation:
    case VisualNode::BaseDemand:
    case VisualNode::Chlorine:
    case VisualNode::RiverWater:
    case VisualNode::LakeWater:
    case VisualNode::WaterAge:
        break;
    }

    return values;
}

inline QHash<QUuid, double> hydraulicLinkSymbologyValues(
    const HydraulicSimulationResult &result, VisualLink visual_link)
{
    QHash<QUuid, double> values;

    switch (visual_link)
    {
    case VisualLink::FlowRate:
        values.reserve(result.links_pipes.size() + result.links_pumps.size()
                       + result.links_valves.size());
        for (const HydraulicSimulationResultLinkPipe &pipe : result.links_pipes)
            values.insert(pipe.uuid, pipe.flow_m3_per_h);
        for (const HydraulicSimulationResultLinkPump &pump : result.links_pumps)
            values.insert(pump.uuid, pump.flow_m3_per_h);
        for (const HydraulicSimulationResultLinkValve &valve : result.links_valves)
            values.insert(valve.uuid, valve.flow_m3_per_h);
        break;
    case VisualLink::Velocity:
        values.reserve(result.links_pipes.size() + result.links_pumps.size()
                       + result.links_valves.size());
        for (const HydraulicSimulationResultLinkPipe &pipe : result.links_pipes)
            values.insert(pipe.uuid, pipe.velocity_m_per_s);
        for (const HydraulicSimulationResultLinkPump &pump : result.links_pumps)
            values.insert(pump.uuid, pump.velocity_m_per_s);
        for (const HydraulicSimulationResultLinkValve &valve : result.links_valves)
            values.insert(valve.uuid, valve.velocity_m_per_s);
        break;
    case VisualLink::HeadLoss:
        values.reserve(result.links_pipes.size() + result.links_valves.size());
        for (const HydraulicSimulationResultLinkPipe &pipe : result.links_pipes)
            values.insert(pipe.uuid, pipe.head_loss_m);
        for (const HydraulicSimulationResultLinkValve &valve : result.links_valves)
            values.insert(valve.uuid, valve.head_loss_m);
        break;
    case VisualLink::Leakage:
        values.reserve(result.links_pipes.size());
        for (const HydraulicSimulationResultLinkPipe &pipe : result.links_pipes)
            values.insert(pipe.uuid, pipe.leakage_flow_m3_per_h);
        break;
    case VisualLink::None:
    case VisualLink::Diameter:
    case VisualLink::Length:
    case VisualLink::Roughness:
    case VisualLink::Chlorine:
    case VisualLink::RiverWater:
    case VisualLink::LakeWater:
    case VisualLink::WaterAge:
        break;
    }

    return values;
}

inline QHash<QUuid, double> hydraulicHeatmapSymbologyValues(
    const HydraulicSimulationResult &result, VisualHeatmap visual_heatmap)
{
    switch (visual_heatmap)
    {
    case VisualHeatmap::TotalDemand:
        return hydraulicNodeSymbologyValues(result, VisualNode::TotalDemand);
    case VisualHeatmap::DemandDeficit:
        return hydraulicNodeSymbologyValues(result, VisualNode::DemandDeficit);
    case VisualHeatmap::EmitterFlow:
        return hydraulicNodeSymbologyValues(result, VisualNode::EmitterFlow);
    case VisualHeatmap::Leakage:
        return hydraulicNodeSymbologyValues(result, VisualNode::Leakage);
    case VisualHeatmap::Head:
        return hydraulicNodeSymbologyValues(result, VisualNode::Head);
    case VisualHeatmap::Pressure:
        return hydraulicNodeSymbologyValues(result, VisualNode::Pressure);
    case VisualHeatmap::None:
    case VisualHeatmap::Elevation:
    case VisualHeatmap::BaseDemand:
    case VisualHeatmap::Chlorine:
    case VisualHeatmap::RiverWater:
    case VisualHeatmap::LakeWater:
    case VisualHeatmap::WaterAge:
        break;
    }

    return QHash<QUuid, double>();
}

inline qint8 hydraulicFlowDirection(double flow_m3_per_h)
{
    // EPANET always solves internally in US customary units (cubic feet per
    // second) regardless of the network's declared display units, and treats
    // any flow below its own internal TINY threshold (1.E-6 cfs, see
    // external/epanet/src/types.h) as numerically meaningless solver noise
    // rather than a real flow (see e.g. hydstatus.c's pump/tank status
    // checks). Converted to this field's m3/h units, that noise floor is
    // roughly 1.E-6 * 101.94 =~ 1.E-4 m3/h. A network that cannot pass any
    // real flow (e.g. a dead-ended branch, or a tank that cannot accept
    // inflow) still converges with residual flows on that order, which used
    // to render as a confident directional arrow. Use a dead band comfortably
    // above that noise floor so only flow EPANET itself considers meaningful
    // is shown as directional.
    constexpr double FlowDirectionEpsilonM3PerH = 1.0e-3;
    if (!std::isfinite(flow_m3_per_h) ||
        std::abs(flow_m3_per_h) <= FlowDirectionEpsilonM3PerH)
    {
        return 0;
    }

    return flow_m3_per_h > 0.0 ? qint8(1) : qint8(-1);
}

inline QHash<QUuid, qint8> hydraulicLinkFlowDirections(
    const HydraulicSimulationResult &result)
{
    QHash<QUuid, qint8> directions;
    directions.reserve(result.links_pipes.size() + result.links_pumps.size()
                       + result.links_valves.size());

    for (const HydraulicSimulationResultLinkPipe &pipe : result.links_pipes)
    {
        const qint8 direction = hydraulicFlowDirection(pipe.flow_m3_per_h);
        if (direction != 0)
            directions.insert(pipe.uuid, direction);
    }
    for (const HydraulicSimulationResultLinkPump &pump : result.links_pumps)
    {
        const qint8 direction = hydraulicFlowDirection(pump.flow_m3_per_h);
        if (direction != 0)
            directions.insert(pump.uuid, direction);
    }
    for (const HydraulicSimulationResultLinkValve &valve : result.links_valves)
    {
        const qint8 direction = hydraulicFlowDirection(valve.flow_m3_per_h);
        if (direction != 0)
            directions.insert(valve.uuid, direction);
    }

    return directions;
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
