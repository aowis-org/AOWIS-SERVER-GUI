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
    constexpr double FlowDirectionEpsilonM3PerH = 1e-9;
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
