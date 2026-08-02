#include "hydraulic_network_editor.h"
#include "geo_metric_projection.h"
#include <aowis/model/uuid.h>

namespace
{
template<typename Entity>
Entity *entityByUuid(QList<Entity> &entities, const QUuid &uuid)
{
    for (Entity &entity : entities)
    {
        if (entity.uuid == uuid)
            return &entity;
    }

    return nullptr;
}

template<typename Entity>
const Entity *entityByUuid(const QList<Entity> &entities, const QUuid &uuid)
{
    for (const Entity &entity : entities)
    {
        if (entity.uuid == uuid)
            return &entity;
    }

    return nullptr;
}

template<typename Entity>
bool removeEntityByUuid(QList<Entity> &entities, const QUuid &uuid)
{
    for (int i = 0; i < entities.size(); i++)
    {
        if (entities[i].uuid != uuid)
            continue;

        entities.removeAt(i);
        return true;
    }

    return false;
}

template<typename Mutation>
bool mutateNode(NetworkHydraulic &network, const QUuid &uuid, Mutation mutation)
{
    for (HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        if (junction.uuid != uuid)
            continue;

        mutation(junction);
        return true;
    }

    for (HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
    {
        if (reservoir.uuid != uuid)
            continue;

        mutation(reservoir);
        return true;
    }

    for (HydraulicNodeTank &tank : network.nodes_tanks)
    {
        if (tank.uuid != uuid)
            continue;

        mutation(tank);
        return true;
    }

    return false;
}

template<typename Mutation>
bool mutateLink(NetworkHydraulic &network, const QUuid &uuid, Mutation mutation)
{
    for (HydraulicLinkPipe &pipe : network.links_pipes)
    {
        if (pipe.uuid != uuid)
            continue;

        mutation(pipe);
        return true;
    }

    for (HydraulicLinkPump &pump : network.links_pumps)
    {
        if (pump.uuid != uuid)
            continue;

        mutation(pump);
        return true;
    }

    for (HydraulicLinkValve &valve : network.links_valves)
    {
        if (valve.uuid != uuid)
            continue;

        mutation(valve);
        return true;
    }

    return false;
}
}

HydraulicNetworkEditor::HydraulicNetworkEditor(NetworkHydraulic &network)
    : network(network)
{}

bool HydraulicNetworkEditor::hasNode(const QUuid &uuid) const
{
    return nodeCoordinate(uuid).has_value();
}

std::optional<CoordinateWGS84> HydraulicNetworkEditor::nodeCoordinate(const QUuid &uuid) const
{
    if (uuid.isNull())
        return std::nullopt;

    for (const HydraulicNodeJunction &junction : this->network.nodes_junctions)
    {
        if (junction.uuid == uuid)
            return junction.coordinate_wgs84;
    }

    for (const HydraulicNodeReservoir &reservoir : this->network.nodes_reservoirs)
    {
        if (reservoir.uuid == uuid)
            return reservoir.coordinate_wgs84;
    }

    for (const HydraulicNodeTank &tank : this->network.nodes_tanks)
    {
        if (tank.uuid == uuid)
            return tank.coordinate_wgs84;
    }

    return std::nullopt;
}

std::optional<HydraulicNodeJunction> HydraulicNetworkEditor::junction(const QUuid &uuid) const
{
    if (uuid.isNull())
        return std::nullopt;

    const HydraulicNodeJunction *junction = entityByUuid(this->network.nodes_junctions, uuid);
    if (junction == nullptr)
        return std::nullopt;

    return *junction;
}

std::optional<HydraulicNodeReservoir> HydraulicNetworkEditor::reservoir(const QUuid &uuid) const
{
    if (uuid.isNull())
        return std::nullopt;

    const HydraulicNodeReservoir *reservoir = entityByUuid(this->network.nodes_reservoirs, uuid);
    if (reservoir == nullptr)
        return std::nullopt;

    return *reservoir;
}

std::optional<HydraulicNodeTank> HydraulicNetworkEditor::tank(const QUuid &uuid) const
{
    if (uuid.isNull())
        return std::nullopt;

    const HydraulicNodeTank *tank = entityByUuid(this->network.nodes_tanks, uuid);
    if (tank == nullptr)
        return std::nullopt;

    return *tank;
}

std::optional<HydraulicLinkPipe> HydraulicNetworkEditor::pipe(const QUuid &uuid) const
{
    if (uuid.isNull())
        return std::nullopt;

    const HydraulicLinkPipe *pipe = entityByUuid(this->network.links_pipes, uuid);
    if (pipe == nullptr)
        return std::nullopt;

    return *pipe;
}

std::optional<HydraulicLinkPump> HydraulicNetworkEditor::pump(const QUuid &uuid) const
{
    if (uuid.isNull())
        return std::nullopt;

    const HydraulicLinkPump *pump = entityByUuid(this->network.links_pumps, uuid);
    if (pump == nullptr)
        return std::nullopt;

    return *pump;
}

std::optional<HydraulicLinkValve> HydraulicNetworkEditor::valve(const QUuid &uuid) const
{
    if (uuid.isNull())
        return std::nullopt;

    const HydraulicLinkValve *valve = entityByUuid(this->network.links_valves, uuid);
    if (valve == nullptr)
        return std::nullopt;

    return *valve;
}

QUuid HydraulicNetworkEditor::addJunction(const CoordinateWGS84 &coordinate)
{
    HydraulicNodeJunction junction;
    junction.uuid = createUuidV7();
    junction.id = nextNodeId(QStringLiteral("J"));
    junction.coordinate_wgs84 = coordinate;
    junction.metadata.date_added = QDate::currentDate();
    this->network.nodes_junctions.append(junction);
    return junction.uuid;
}

QUuid HydraulicNetworkEditor::addReservoir(const CoordinateWGS84 &coordinate)
{
    HydraulicNodeReservoir reservoir;
    reservoir.uuid = createUuidV7();
    reservoir.id = nextNodeId(QStringLiteral("R"));
    reservoir.coordinate_wgs84 = coordinate;
    reservoir.metadata.date_added = QDate::currentDate();
    this->network.nodes_reservoirs.append(reservoir);
    return reservoir.uuid;
}

QUuid HydraulicNetworkEditor::addTank(const CoordinateWGS84 &coordinate)
{
    HydraulicNodeTank tank;
    tank.uuid = createUuidV7();
    tank.id = nextNodeId(QStringLiteral("T"));
    tank.coordinate_wgs84 = coordinate;
    tank.metadata.date_added = QDate::currentDate();
    this->network.nodes_tanks.append(tank);
    return tank.uuid;
}

QUuid HydraulicNetworkEditor::addPipe(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                                      const QList<CoordinateWGS84> &intermediate_vertices)
{
    if (!hasNode(node_uuid_from) || !hasNode(node_uuid_to) || node_uuid_from == node_uuid_to)
        return QUuid();

    HydraulicLinkPipe pipe;
    pipe.uuid = createUuidV7();
    pipe.id = nextLinkId(QStringLiteral("P"));
    pipe.node_uuid_from = node_uuid_from;
    pipe.node_uuid_to = node_uuid_to;
    pipe.metadata.date_added = QDate::currentDate();

    for (const CoordinateWGS84 &coordinate : intermediate_vertices)
    {
        HydraulicLinkVertex vertex;
        vertex.coordinate_wgs84 = coordinate;
        pipe.vertices.append(vertex);
    }

    pipe.length_calculated_m = pipeLengthMeters(pipe.node_uuid_from, pipe.node_uuid_to, pipe.vertices);
    this->network.links_pipes.append(pipe);
    return pipe.uuid;
}

QUuid HydraulicNetworkEditor::addPump(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                                      const CoordinateWGS84 &center_coordinate)
{
    if (!hasNode(node_uuid_from) || !hasNode(node_uuid_to) || node_uuid_from == node_uuid_to)
        return QUuid();

    HydraulicLinkPump pump;
    pump.uuid = createUuidV7();
    pump.id = nextLinkId(QStringLiteral("PU"));
    pump.node_uuid_from = node_uuid_from;
    pump.node_uuid_to = node_uuid_to;
    pump.metadata.date_added = QDate::currentDate();

    HydraulicLinkVertex vertex;
    vertex.coordinate_wgs84 = center_coordinate;
    pump.vertices.append(vertex);

    this->network.links_pumps.append(pump);
    return pump.uuid;
}

QUuid HydraulicNetworkEditor::addValve(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                                       const CoordinateWGS84 &center_coordinate)
{
    if (!hasNode(node_uuid_from) || !hasNode(node_uuid_to) || node_uuid_from == node_uuid_to)
        return QUuid();

    HydraulicLinkValve valve;
    valve.uuid = createUuidV7();
    valve.id = nextLinkId(QStringLiteral("V"));
    valve.node_uuid_from = node_uuid_from;
    valve.node_uuid_to = node_uuid_to;
    valve.metadata.date_added = QDate::currentDate();

    HydraulicLinkVertex vertex;
    vertex.coordinate_wgs84 = center_coordinate;
    valve.vertices.append(vertex);

    this->network.links_valves.append(valve);
    return valve.uuid;
}

bool HydraulicNetworkEditor::setNodeId(const QUuid &uuid, const QString &id)
{
    return mutateNode(this->network, uuid, [&id](auto &node)
    {
        node.id = id;
    });
}

bool HydraulicNetworkEditor::setNodeModelRole(const QUuid &uuid, EntityModelRole model_role)
{
    return mutateNode(this->network, uuid, [model_role](auto &node)
    {
        node.metadata.model_role = model_role;
    });
}

bool HydraulicNetworkEditor::setNodeDateAdded(const QUuid &uuid, const std::optional<QDate> &date_added)
{
    return mutateNode(this->network, uuid, [&date_added](auto &node)
    {
        node.metadata.date_added = date_added;
    });
}

bool HydraulicNetworkEditor::setNodeDateInstalled(const QUuid &uuid, const std::optional<QDate> &date_installed)
{
    return mutateNode(this->network, uuid, [&date_installed](auto &node)
    {
        node.metadata.date_installed = date_installed;
    });
}

bool HydraulicNetworkEditor::setNodeEnabled(const QUuid &uuid, bool enabled)
{
    return mutateNode(this->network, uuid, [enabled](auto &node)
    {
        node.metadata.enabled = enabled;
    });
}

bool HydraulicNetworkEditor::setLinkId(const QUuid &uuid, const QString &id)
{
    return mutateLink(this->network, uuid, [&id](auto &link)
    {
        link.id = id;
    });
}

bool HydraulicNetworkEditor::setLinkModelRole(const QUuid &uuid, EntityModelRole model_role)
{
    return mutateLink(this->network, uuid, [model_role](auto &link)
    {
        link.metadata.model_role = model_role;
    });
}

bool HydraulicNetworkEditor::setLinkDateAdded(const QUuid &uuid,
                                               const std::optional<QDate> &date_added)
{
    return mutateLink(this->network, uuid, [&date_added](auto &link)
    {
        link.metadata.date_added = date_added;
    });
}

bool HydraulicNetworkEditor::setLinkDateInstalled(
    const QUuid &uuid, const std::optional<QDate> &date_installed)
{
    return mutateLink(this->network, uuid, [&date_installed](auto &link)
    {
        link.metadata.date_installed = date_installed;
    });
}

bool HydraulicNetworkEditor::setLinkEnabled(const QUuid &uuid, bool enabled)
{
    return mutateLink(this->network, uuid, [enabled](auto &link)
    {
        link.metadata.enabled = enabled;
    });
}

bool HydraulicNetworkEditor::setNodeCoordinate(const QUuid &uuid, const CoordinateWGS84 &coordinate)
{
    for (HydraulicNodeJunction &junction : this->network.nodes_junctions)
    {
        if (junction.uuid != uuid)
            continue;

        junction.coordinate_wgs84 = coordinate;
        recalculateConnectedPipeLengths(uuid);
        return true;
    }

    for (HydraulicNodeReservoir &reservoir : this->network.nodes_reservoirs)
    {
        if (reservoir.uuid != uuid)
            continue;

        reservoir.coordinate_wgs84 = coordinate;
        recalculateConnectedPipeLengths(uuid);
        return true;
    }

    for (HydraulicNodeTank &tank : this->network.nodes_tanks)
    {
        if (tank.uuid != uuid)
            continue;

        tank.coordinate_wgs84 = coordinate;
        recalculateConnectedPipeLengths(uuid);
        return true;
    }

    return false;
}

bool HydraulicNetworkEditor::setJunctionElevationInputType(
    const QUuid &uuid, HydraulicNodeElevationInputType input_type)
{
    HydraulicNodeJunction *junction = entityByUuid(this->network.nodes_junctions, uuid);
    if (junction == nullptr)
        return false;

    junction->elevation_input_type = input_type;
    return true;
}

bool HydraulicNetworkEditor::setJunctionElevationM(const QUuid &uuid, double elevation_m)
{
    HydraulicNodeJunction *junction = entityByUuid(this->network.nodes_junctions, uuid);
    if (junction == nullptr)
        return false;

    junction->elevation_m = elevation_m;
    return true;
}

bool HydraulicNetworkEditor::setJunctionTerrainElevationM(const QUuid &uuid,
                                                          double terrain_elevation_m)
{
    HydraulicNodeJunction *junction = entityByUuid(this->network.nodes_junctions, uuid);
    if (junction == nullptr)
        return false;

    junction->terrain_elevation_m = terrain_elevation_m;
    return true;
}

bool HydraulicNetworkEditor::setJunctionElevationOffsetM(const QUuid &uuid,
                                                         double elevation_offset_m)
{
    HydraulicNodeJunction *junction = entityByUuid(this->network.nodes_junctions, uuid);
    if (junction == nullptr)
        return false;

    junction->elevation_offset_m = elevation_offset_m;
    return true;
}

bool HydraulicNetworkEditor::addJunctionDemand(const QUuid &uuid,
                                               const HydraulicNodeJunctionDemand &demand)
{
    HydraulicNodeJunction *junction = entityByUuid(this->network.nodes_junctions, uuid);
    if (junction == nullptr)
        return false;

    junction->demands.append(demand);
    return true;
}

bool HydraulicNetworkEditor::removeJunctionDemand(const QUuid &uuid, int demand_index)
{
    HydraulicNodeJunction *junction = entityByUuid(this->network.nodes_junctions, uuid);
    if (junction == nullptr || demand_index < 0 || demand_index >= junction->demands.size())
        return false;

    junction->demands.removeAt(demand_index);
    return true;
}

bool HydraulicNetworkEditor::setJunctionDemandCategoryName(const QUuid &uuid, int demand_index,
                                                           const QString &category_name)
{
    HydraulicNodeJunction *junction = entityByUuid(this->network.nodes_junctions, uuid);
    if (junction == nullptr || demand_index < 0 || demand_index >= junction->demands.size())
        return false;

    junction->demands[demand_index].category_name = category_name;
    return true;
}

bool HydraulicNetworkEditor::setJunctionDemandBaseDemandM3PerH(
    const QUuid &uuid, int demand_index, double base_demand_m3_per_h)
{
    HydraulicNodeJunction *junction = entityByUuid(this->network.nodes_junctions, uuid);
    if (junction == nullptr || demand_index < 0 || demand_index >= junction->demands.size())
        return false;

    junction->demands[demand_index].base_demand_m3_per_h = base_demand_m3_per_h;
    return true;
}

bool HydraulicNetworkEditor::setJunctionDemandPatternMode(
    const QUuid &uuid, int demand_index, HydraulicTimePatternMode pattern_mode)
{
    HydraulicNodeJunction *junction = entityByUuid(this->network.nodes_junctions, uuid);
    if (junction == nullptr || demand_index < 0 || demand_index >= junction->demands.size())
        return false;

    junction->demands[demand_index].pattern_mode = pattern_mode;
    return true;
}

bool HydraulicNetworkEditor::setJunctionDemandPatternUuid(const QUuid &uuid, int demand_index,
                                                          const QUuid &pattern_uuid)
{
    HydraulicNodeJunction *junction = entityByUuid(this->network.nodes_junctions, uuid);
    if (junction == nullptr || demand_index < 0 || demand_index >= junction->demands.size())
        return false;

    junction->demands[demand_index].pattern_uuid = pattern_uuid;
    return true;
}

bool HydraulicNetworkEditor::setJunctionDemandSourceMethod(
    const QUuid &uuid, int demand_index, HydraulicNodeJunctionDemandSourceMethod source_method)
{
    HydraulicNodeJunction *junction = entityByUuid(this->network.nodes_junctions, uuid);
    if (junction == nullptr || demand_index < 0 || demand_index >= junction->demands.size())
        return false;

    junction->demands[demand_index].source_method = source_method;
    return true;
}

bool HydraulicNetworkEditor::setJunctionDemandNote(const QUuid &uuid, int demand_index,
                                                   const QString &note)
{
    HydraulicNodeJunction *junction = entityByUuid(this->network.nodes_junctions, uuid);
    if (junction == nullptr || demand_index < 0 || demand_index >= junction->demands.size())
        return false;

    junction->demands[demand_index].note = note;
    return true;
}

bool HydraulicNetworkEditor::setJunctionEmitterCoefficientM3PerHPerMExponent(
    const QUuid &uuid, double emitter_coefficient_m3_per_h_per_m_exponent)
{
    HydraulicNodeJunction *junction = entityByUuid(this->network.nodes_junctions, uuid);
    if (junction == nullptr)
        return false;

    junction->emitter_coefficient_m3_per_h_per_m_exponent =
        emitter_coefficient_m3_per_h_per_m_exponent;
    return true;
}

bool HydraulicNetworkEditor::setReservoirHeadInputType(
    const QUuid &uuid, HydraulicNodeElevationInputType input_type)
{
    HydraulicNodeReservoir *reservoir = entityByUuid(this->network.nodes_reservoirs, uuid);
    if (reservoir == nullptr)
        return false;

    reservoir->head_input_type = input_type;
    return true;
}

bool HydraulicNetworkEditor::setReservoirHeadM(const QUuid &uuid, double head_m)
{
    HydraulicNodeReservoir *reservoir = entityByUuid(this->network.nodes_reservoirs, uuid);
    if (reservoir == nullptr)
        return false;

    reservoir->head_m = head_m;
    return true;
}

bool HydraulicNetworkEditor::setReservoirTerrainElevationM(const QUuid &uuid,
                                                           double terrain_elevation_m)
{
    HydraulicNodeReservoir *reservoir = entityByUuid(this->network.nodes_reservoirs, uuid);
    if (reservoir == nullptr)
        return false;

    reservoir->terrain_elevation_m = terrain_elevation_m;
    return true;
}

bool HydraulicNetworkEditor::setReservoirHeadOffsetM(const QUuid &uuid, double head_offset_m)
{
    HydraulicNodeReservoir *reservoir = entityByUuid(this->network.nodes_reservoirs, uuid);
    if (reservoir == nullptr)
        return false;

    reservoir->head_offset_m = head_offset_m;
    return true;
}

bool HydraulicNetworkEditor::setReservoirHeadPatternMode(const QUuid &uuid, HydraulicTimePatternMode pattern_mode)
{
    HydraulicNodeReservoir *reservoir = entityByUuid(this->network.nodes_reservoirs, uuid);
    if (reservoir == nullptr)
        return false;

    reservoir->head_pattern_mode = pattern_mode;
    return true;
}

bool HydraulicNetworkEditor::setReservoirHeadPatternUuid(const QUuid &uuid,
                                                         const QUuid &pattern_uuid)
{
    HydraulicNodeReservoir *reservoir = entityByUuid(this->network.nodes_reservoirs, uuid);
    if (reservoir == nullptr)
        return false;

    reservoir->head_pattern_uuid = pattern_uuid;
    return true;
}

bool HydraulicNetworkEditor::setTankElevationInputType(
    const QUuid &uuid, HydraulicNodeTankElevationInputType input_type)
{
    HydraulicNodeTank *tank = entityByUuid(this->network.nodes_tanks, uuid);
    if (tank == nullptr)
        return false;

    tank->elevation_input_type = input_type;
    return true;
}

bool HydraulicNetworkEditor::setTankBottomElevationM(const QUuid &uuid, double bottom_elevation_m)
{
    HydraulicNodeTank *tank = entityByUuid(this->network.nodes_tanks, uuid);
    if (tank == nullptr)
        return false;

    tank->bottom_elevation_m = bottom_elevation_m;
    return true;
}

bool HydraulicNetworkEditor::setTankTerrainElevationM(const QUuid &uuid,
                                                      double terrain_elevation_m)
{
    HydraulicNodeTank *tank = entityByUuid(this->network.nodes_tanks, uuid);
    if (tank == nullptr)
        return false;

    tank->terrain_elevation_m = terrain_elevation_m;
    return true;
}

bool HydraulicNetworkEditor::setTankBottomOffsetM(const QUuid &uuid, double bottom_offset_m)
{
    HydraulicNodeTank *tank = entityByUuid(this->network.nodes_tanks, uuid);
    if (tank == nullptr)
        return false;

    tank->bottom_offset_m = bottom_offset_m;
    return true;
}

bool HydraulicNetworkEditor::setTankWaterLevelInitialM(const QUuid &uuid,
                                                       double water_level_initial_m)
{
    HydraulicNodeTank *tank = entityByUuid(this->network.nodes_tanks, uuid);
    if (tank == nullptr)
        return false;

    tank->water_level_initial_m = water_level_initial_m;
    return true;
}

bool HydraulicNetworkEditor::setTankWaterLevelMinimumM(const QUuid &uuid,
                                                       double water_level_minimum_m)
{
    HydraulicNodeTank *tank = entityByUuid(this->network.nodes_tanks, uuid);
    if (tank == nullptr)
        return false;

    tank->water_level_minimum_m = water_level_minimum_m;
    return true;
}

bool HydraulicNetworkEditor::setTankWaterLevelMaximumM(const QUuid &uuid,
                                                       double water_level_maximum_m)
{
    HydraulicNodeTank *tank = entityByUuid(this->network.nodes_tanks, uuid);
    if (tank == nullptr)
        return false;

    tank->water_level_maximum_m = water_level_maximum_m;
    return true;
}

bool HydraulicNetworkEditor::setTankGeometryInputType(
    const QUuid &uuid, HydraulicNodeTankGeometryInputType input_type)
{
    HydraulicNodeTank *tank = entityByUuid(this->network.nodes_tanks, uuid);
    if (tank == nullptr)
        return false;

    tank->geometry_input_type = input_type;
    return true;
}

bool HydraulicNetworkEditor::setTankDiameterM(const QUuid &uuid, double diameter_m)
{
    HydraulicNodeTank *tank = entityByUuid(this->network.nodes_tanks, uuid);
    if (tank == nullptr)
        return false;

    tank->diameter_m = diameter_m;
    return true;
}

bool HydraulicNetworkEditor::setTankCrossSectionAreaM2(const QUuid &uuid,
                                                       double cross_section_area_m2)
{
    HydraulicNodeTank *tank = entityByUuid(this->network.nodes_tanks, uuid);
    if (tank == nullptr)
        return false;

    tank->cross_section_area_m2 = cross_section_area_m2;
    return true;
}

bool HydraulicNetworkEditor::setTankVolumeAtMaximumLevelM3(
    const QUuid &uuid, double volume_at_maximum_level_m3)
{
    HydraulicNodeTank *tank = entityByUuid(this->network.nodes_tanks, uuid);
    if (tank == nullptr)
        return false;

    tank->volume_at_maximum_level_m3 = volume_at_maximum_level_m3;
    return true;
}

bool HydraulicNetworkEditor::setTankMinimumVolumeM3(const QUuid &uuid, double minimum_volume_m3)
{
    HydraulicNodeTank *tank = entityByUuid(this->network.nodes_tanks, uuid);
    if (tank == nullptr)
        return false;

    tank->minimum_volume_m3 = minimum_volume_m3;
    return true;
}

bool HydraulicNetworkEditor::setTankVolumeCurveUuid(const QUuid &uuid,
                                                    const QUuid &volume_curve_uuid)
{
    HydraulicNodeTank *tank = entityByUuid(this->network.nodes_tanks, uuid);
    if (tank == nullptr)
        return false;

    tank->volume_curve_uuid = volume_curve_uuid;
    return true;
}

bool HydraulicNetworkEditor::setTankCanOverflow(const QUuid &uuid, bool can_overflow)
{
    HydraulicNodeTank *tank = entityByUuid(this->network.nodes_tanks, uuid);
    if (tank == nullptr)
        return false;

    tank->can_overflow = can_overflow;
    return true;
}

bool HydraulicNetworkEditor::setPipeInitialStatus(
    const QUuid &uuid, HydraulicLinkPipeInitialStatus initial_status)
{
    HydraulicLinkPipe *pipe = entityByUuid(this->network.links_pipes, uuid);
    if (pipe == nullptr)
        return false;

    pipe->initial_status = initial_status;
    return true;
}

bool HydraulicNetworkEditor::setPipeDiameterMm(const QUuid &uuid, double diameter_mm)
{
    HydraulicLinkPipe *pipe = entityByUuid(this->network.links_pipes, uuid);
    if (pipe == nullptr)
        return false;

    pipe->diameter_mm = diameter_mm;
    return true;
}

bool HydraulicNetworkEditor::setPipeMeasuredLengthM(
    const QUuid &uuid, const std::optional<double> &length_measured_m)
{
    HydraulicLinkPipe *pipe = entityByUuid(this->network.links_pipes, uuid);
    if (pipe == nullptr)
        return false;

    pipe->length_measured_m = length_measured_m;
    return true;
}

bool HydraulicNetworkEditor::setPipeMaterialId(const QUuid &uuid, const QString &material_id)
{
    HydraulicLinkPipe *pipe = entityByUuid(this->network.links_pipes, uuid);
    if (pipe == nullptr)
        return false;

    pipe->material_id = material_id;
    return true;
}

bool HydraulicNetworkEditor::setPipeRoughnessHw(const QUuid &uuid, double roughness_hw)
{
    HydraulicLinkPipe *pipe = entityByUuid(this->network.links_pipes, uuid);
    if (pipe == nullptr)
        return false;

    pipe->roughness_hw = roughness_hw;
    return true;
}

bool HydraulicNetworkEditor::setPipeRoughnessDwMm(const QUuid &uuid, double roughness_dw_mm)
{
    HydraulicLinkPipe *pipe = entityByUuid(this->network.links_pipes, uuid);
    if (pipe == nullptr)
        return false;

    pipe->roughness_dw_mm = roughness_dw_mm;
    return true;
}

bool HydraulicNetworkEditor::setPipeRoughnessCm(const QUuid &uuid, double roughness_cm)
{
    HydraulicLinkPipe *pipe = entityByUuid(this->network.links_pipes, uuid);
    if (pipe == nullptr)
        return false;

    pipe->roughness_cm = roughness_cm;
    return true;
}

bool HydraulicNetworkEditor::setPipeMinorLoss(const QUuid &uuid, double minor_loss)
{
    HydraulicLinkPipe *pipe = entityByUuid(this->network.links_pipes, uuid);
    if (pipe == nullptr)
        return false;

    pipe->minor_loss = minor_loss;
    return true;
}

bool HydraulicNetworkEditor::setPipeVertexCoordinate(const QUuid &pipe_uuid, int vertex_index,
                                                       const CoordinateWGS84 &coordinate)
{
    for (HydraulicLinkPipe &pipe : this->network.links_pipes)
    {
        if (pipe.uuid != pipe_uuid)
            continue;
        if (vertex_index < 0 || vertex_index >= pipe.vertices.size())
            return false;

        pipe.vertices[vertex_index].coordinate_wgs84 = coordinate;
        pipe.length_calculated_m = pipeLengthMeters(pipe.node_uuid_from, pipe.node_uuid_to, pipe.vertices);
        return true;
    }

    return false;
}

bool HydraulicNetworkEditor::setPipeVertices(const QUuid &pipe_uuid,
                                               const QList<CoordinateWGS84> &intermediate_vertices)
{
    for (HydraulicLinkPipe &pipe : this->network.links_pipes)
    {
        if (pipe.uuid != pipe_uuid)
            continue;

        pipe.vertices.clear();
        pipe.vertices.reserve(intermediate_vertices.size());
        for (const CoordinateWGS84 &coordinate : intermediate_vertices)
        {
            HydraulicLinkVertex vertex;
            vertex.coordinate_wgs84 = coordinate;
            pipe.vertices.append(vertex);
        }

        pipe.length_calculated_m = pipeLengthMeters(pipe.node_uuid_from, pipe.node_uuid_to, pipe.vertices);
        return true;
    }

    return false;
}

bool HydraulicNetworkEditor::setPumpCenterCoordinate(const QUuid &pump_uuid, const CoordinateWGS84 &coordinate)
{
    for (HydraulicLinkPump &pump : this->network.links_pumps)
    {
        if (pump.uuid != pump_uuid)
            continue;

        if (pump.vertices.isEmpty())
            pump.vertices.append(HydraulicLinkVertex());
        pump.vertices[0].coordinate_wgs84 = coordinate;
        return true;
    }

    return false;
}

bool HydraulicNetworkEditor::setValveCenterCoordinate(const QUuid &valve_uuid, const CoordinateWGS84 &coordinate)
{
    for (HydraulicLinkValve &valve : this->network.links_valves)
    {
        if (valve.uuid != valve_uuid)
            continue;

        if (valve.vertices.isEmpty())
            valve.vertices.append(HydraulicLinkVertex());
        valve.vertices[0].coordinate_wgs84 = coordinate;
        return true;
    }

    return false;
}

QUuid HydraulicNetworkEditor::splitPipeAtVertex(const QUuid &pipe_uuid, int vertex_index,
                                                const QUuid &junction_uuid)
{
    if (!hasNode(junction_uuid))
        return QUuid();

    for (int i = 0; i < this->network.links_pipes.size(); i++)
    {
        const HydraulicLinkPipe original_pipe = this->network.links_pipes[i];
        if (original_pipe.uuid != pipe_uuid)
            continue;
        if (vertex_index < 0 || vertex_index >= original_pipe.vertices.size())
            return QUuid();

        HydraulicLinkPipe first_pipe = original_pipe;
        first_pipe.node_uuid_to = junction_uuid;
        first_pipe.vertices = original_pipe.vertices.mid(0, vertex_index);
        first_pipe.length_calculated_m = pipeLengthMeters(first_pipe.node_uuid_from, first_pipe.node_uuid_to,
                                                          first_pipe.vertices);

        HydraulicLinkPipe second_pipe = original_pipe;
        second_pipe.uuid = createUuidV7();
        second_pipe.id = nextLinkId(QStringLiteral("P"));
        second_pipe.node_uuid_from = junction_uuid;
        second_pipe.vertices = original_pipe.vertices.mid(vertex_index + 1);
        second_pipe.length_calculated_m = pipeLengthMeters(second_pipe.node_uuid_from, second_pipe.node_uuid_to,
                                                           second_pipe.vertices);

        if (original_pipe.length_measured_m.has_value())
        {
            const double total_calculated_length_m = first_pipe.length_calculated_m + second_pipe.length_calculated_m;
            if (total_calculated_length_m > 0.0)
            {
                first_pipe.length_measured_m = original_pipe.length_measured_m.value() *
                                               first_pipe.length_calculated_m / total_calculated_length_m;
                second_pipe.length_measured_m = original_pipe.length_measured_m.value() -
                                                first_pipe.length_measured_m.value();
            }
            else
            {
                first_pipe.length_measured_m.reset();
                second_pipe.length_measured_m.reset();
            }
        }

        this->network.links_pipes[i] = first_pipe;
        this->network.links_pipes.insert(i + 1, second_pipe);
        return second_pipe.uuid;
    }

    return QUuid();
}

bool HydraulicNetworkEditor::undoPipeSplit(const QUuid &first_pipe_uuid, const QUuid &second_pipe_uuid,
                                            const QUuid &junction_uuid)
{
    int first_pipe_index = -1;
    int second_pipe_index = -1;

    for (int i = 0; i < this->network.links_pipes.size(); i++)
    {
        const QUuid uuid = this->network.links_pipes[i].uuid;
        if (uuid == first_pipe_uuid)
            first_pipe_index = i;
        else if (uuid == second_pipe_uuid)
            second_pipe_index = i;
    }

    if (first_pipe_index < 0 || second_pipe_index < 0)
        return false;

    const std::optional<CoordinateWGS84> junction_coordinate = nodeCoordinate(junction_uuid);
    if (!junction_coordinate.has_value())
        return false;

    const HydraulicLinkPipe first_pipe = this->network.links_pipes[first_pipe_index];
    const HydraulicLinkPipe second_pipe = this->network.links_pipes[second_pipe_index];
    if (first_pipe.node_uuid_to != junction_uuid || second_pipe.node_uuid_from != junction_uuid)
        return false;

    HydraulicLinkPipe restored_pipe = first_pipe;
    restored_pipe.node_uuid_to = second_pipe.node_uuid_to;

    HydraulicLinkVertex junction_vertex;
    junction_vertex.coordinate_wgs84 = junction_coordinate.value();
    restored_pipe.vertices.append(junction_vertex);
    for (const HydraulicLinkVertex &vertex : second_pipe.vertices)
        restored_pipe.vertices.append(vertex);

    if (first_pipe.length_measured_m.has_value() && second_pipe.length_measured_m.has_value())
    {
        restored_pipe.length_measured_m = first_pipe.length_measured_m.value() +
                                          second_pipe.length_measured_m.value();
    }
    else
    {
        restored_pipe.length_measured_m.reset();
    }

    restored_pipe.length_calculated_m = pipeLengthMeters(restored_pipe.node_uuid_from,
                                                          restored_pipe.node_uuid_to,
                                                          restored_pipe.vertices);
    this->network.links_pipes.removeAt(second_pipe_index);

    for (int i = 0; i < this->network.links_pipes.size(); i++)
    {
        if (this->network.links_pipes[i].uuid != first_pipe_uuid)
            continue;

        this->network.links_pipes[i] = restored_pipe;
        return true;
    }

    return false;
}

bool HydraulicNetworkEditor::deleteJunction(const QUuid &uuid)
{
    if (!removeEntityByUuid(this->network.nodes_junctions, uuid))
        return false;

    deleteConnectedLinks(uuid);
    return true;
}

bool HydraulicNetworkEditor::deleteReservoir(const QUuid &uuid)
{
    if (!removeEntityByUuid(this->network.nodes_reservoirs, uuid))
        return false;

    deleteConnectedLinks(uuid);
    return true;
}

bool HydraulicNetworkEditor::deleteTank(const QUuid &uuid)
{
    if (!removeEntityByUuid(this->network.nodes_tanks, uuid))
        return false;

    deleteConnectedLinks(uuid);
    return true;
}

bool HydraulicNetworkEditor::deletePipe(const QUuid &uuid)
{
    return removeEntityByUuid(this->network.links_pipes, uuid);
}

bool HydraulicNetworkEditor::deletePump(const QUuid &uuid)
{
    return removeEntityByUuid(this->network.links_pumps, uuid);
}

bool HydraulicNetworkEditor::deleteValve(const QUuid &uuid)
{
    return removeEntityByUuid(this->network.links_valves, uuid);
}

void HydraulicNetworkEditor::deleteConnectedLinks(const QUuid &node_uuid)
{
    for (int i = this->network.links_pipes.size() - 1; i >= 0; i--)
    {
        const HydraulicLinkPipe &pipe = this->network.links_pipes[i];
        if (pipe.node_uuid_from == node_uuid || pipe.node_uuid_to == node_uuid)
            this->network.links_pipes.removeAt(i);
    }

    for (int i = this->network.links_pumps.size() - 1; i >= 0; i--)
    {
        const HydraulicLinkPump &pump = this->network.links_pumps[i];
        if (pump.node_uuid_from == node_uuid || pump.node_uuid_to == node_uuid)
            this->network.links_pumps.removeAt(i);
    }

    for (int i = this->network.links_valves.size() - 1; i >= 0; i--)
    {
        const HydraulicLinkValve &valve = this->network.links_valves[i];
        if (valve.node_uuid_from == node_uuid || valve.node_uuid_to == node_uuid)
            this->network.links_valves.removeAt(i);
    }
}

QString HydraulicNetworkEditor::nextNodeId(const QString &prefix) const
{
    for (int number = 1; ; number++)
    {
        const QString candidate = prefix + QString::number(number);
        bool used = false;

        for (const HydraulicNodeJunction &junction : this->network.nodes_junctions)
            used = used || junction.id == candidate;
        for (const HydraulicNodeReservoir &reservoir : this->network.nodes_reservoirs)
            used = used || reservoir.id == candidate;
        for (const HydraulicNodeTank &tank : this->network.nodes_tanks)
            used = used || tank.id == candidate;

        if (!used)
            return candidate;
    }
}

QString HydraulicNetworkEditor::nextLinkId(const QString &prefix) const
{
    for (int number = 1; ; number++)
    {
        const QString candidate = prefix + QString::number(number);
        bool used = false;

        for (const HydraulicLinkPipe &pipe : this->network.links_pipes)
            used = used || pipe.id == candidate;
        for (const HydraulicLinkPump &pump : this->network.links_pumps)
            used = used || pump.id == candidate;
        for (const HydraulicLinkValve &valve : this->network.links_valves)
            used = used || valve.id == candidate;

        if (!used)
            return candidate;
    }
}

double HydraulicNetworkEditor::pipeLengthMeters(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                                                  const QList<HydraulicLinkVertex> &vertices) const
{
    const std::optional<CoordinateWGS84> start_coordinate = nodeCoordinate(node_uuid_from);
    const std::optional<CoordinateWGS84> end_coordinate = nodeCoordinate(node_uuid_to);
    if (!start_coordinate.has_value() || !end_coordinate.has_value())
        return 0.0;

    double length_m = 0.0;
    CoordinateWGS84 previous_coordinate = start_coordinate.value();

    for (const HydraulicLinkVertex &vertex : vertices)
    {
        length_m += GeoMetricProjection::distanceMeters(previous_coordinate, vertex.coordinate_wgs84);
        previous_coordinate = vertex.coordinate_wgs84;
    }

    length_m += GeoMetricProjection::distanceMeters(previous_coordinate, end_coordinate.value());
    return length_m;
}

void HydraulicNetworkEditor::recalculateConnectedPipeLengths(const QUuid &node_uuid)
{
    for (HydraulicLinkPipe &pipe : this->network.links_pipes)
    {
        if (pipe.node_uuid_from != node_uuid && pipe.node_uuid_to != node_uuid)
            continue;

        pipe.length_calculated_m = pipeLengthMeters(pipe.node_uuid_from, pipe.node_uuid_to, pipe.vertices);
    }
}
