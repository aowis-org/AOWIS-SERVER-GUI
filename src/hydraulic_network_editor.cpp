#include "hydraulic_network_editor.h"
#include "geo_metric_projection.h"
#include <aowis/model/uuid.h>

namespace
{
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
