#include "hydraulic_network_editor.h"
#include <aowis/model/uuid.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double pi = 3.141592653589793238462643383279502884;
constexpr double wgs84_semimajor_axis_m = 6378137.0;
constexpr double wgs84_flattening = 1.0 / 298.257223563;
constexpr double mean_earth_radius_m = 6371008.8;

double radians(double degrees)
{
    return degrees * pi / 180.0;
}

double haversineDistanceMeters(const CoordinateWGS84 &a, const CoordinateWGS84 &b)
{
    const double latitude_a = radians(a.latitude_deg);
    const double latitude_b = radians(b.latitude_deg);
    const double latitude_delta = latitude_b - latitude_a;
    const double longitude_delta = radians(b.longitude_deg - a.longitude_deg);
    const double sin_latitude = std::sin(latitude_delta / 2.0);
    const double sin_longitude = std::sin(longitude_delta / 2.0);
    const double haversine = sin_latitude * sin_latitude +
                             std::cos(latitude_a) * std::cos(latitude_b) *
                             sin_longitude * sin_longitude;
    const double bounded_haversine = std::clamp(haversine, 0.0, 1.0);
    const double central_angle = 2.0 * std::atan2(
        std::sqrt(bounded_haversine), std::sqrt(1.0 - bounded_haversine));
    return mean_earth_radius_m * central_angle;
}

double distanceMeters(const CoordinateWGS84 &a, const CoordinateWGS84 &b)
{
    if (a.latitude_deg == b.latitude_deg && a.longitude_deg == b.longitude_deg)
        return 0.0;

    const double semiminor_axis_m = (1.0 - wgs84_flattening) * wgs84_semimajor_axis_m;
    const double reduced_latitude_a = std::atan((1.0 - wgs84_flattening) * std::tan(radians(a.latitude_deg)));
    const double reduced_latitude_b = std::atan((1.0 - wgs84_flattening) * std::tan(radians(b.latitude_deg)));
    const double sin_reduced_a = std::sin(reduced_latitude_a);
    const double cos_reduced_a = std::cos(reduced_latitude_a);
    const double sin_reduced_b = std::sin(reduced_latitude_b);
    const double cos_reduced_b = std::cos(reduced_latitude_b);
    const double longitude_difference = radians(b.longitude_deg - a.longitude_deg);

    double lambda = longitude_difference;
    double previous_lambda = 0.0;
    double sin_sigma = 0.0;
    double cos_sigma = 0.0;
    double sigma = 0.0;
    double sin_alpha = 0.0;
    double cos_squared_alpha = 0.0;
    double cos_two_sigma_midpoint = 0.0;
    bool converged = false;

    for (int iteration = 0; iteration < 100; iteration++)
    {
        const double sin_lambda = std::sin(lambda);
        const double cos_lambda = std::cos(lambda);
        const double term_a = cos_reduced_b * sin_lambda;
        const double term_b = cos_reduced_a * sin_reduced_b - sin_reduced_a * cos_reduced_b * cos_lambda;
        sin_sigma = std::sqrt(term_a * term_a + term_b * term_b);
        if (sin_sigma == 0.0)
            return 0.0;

        cos_sigma = sin_reduced_a * sin_reduced_b + cos_reduced_a * cos_reduced_b * cos_lambda;
        sigma = std::atan2(sin_sigma, cos_sigma);
        sin_alpha = cos_reduced_a * cos_reduced_b * sin_lambda / sin_sigma;
        cos_squared_alpha = 1.0 - sin_alpha * sin_alpha;
        cos_two_sigma_midpoint = cos_squared_alpha == 0.0
                                     ? 0.0
                                     : cos_sigma - 2.0 * sin_reduced_a * sin_reduced_b / cos_squared_alpha;
        const double coefficient = wgs84_flattening / 16.0 * cos_squared_alpha *
                                   (4.0 + wgs84_flattening * (4.0 - 3.0 * cos_squared_alpha));
        previous_lambda = lambda;
        lambda = longitude_difference + (1.0 - coefficient) * wgs84_flattening * sin_alpha *
                 (sigma + coefficient * sin_sigma *
                  (cos_two_sigma_midpoint + coefficient * cos_sigma *
                   (-1.0 + 2.0 * cos_two_sigma_midpoint * cos_two_sigma_midpoint)));

        if (std::abs(lambda - previous_lambda) <= 1e-12)
        {
            converged = true;
            break;
        }
    }

    if (!converged)
        return haversineDistanceMeters(a, b);

    const double reduced_parameter = cos_squared_alpha *
                                     (wgs84_semimajor_axis_m * wgs84_semimajor_axis_m -
                                      semiminor_axis_m * semiminor_axis_m) /
                                     (semiminor_axis_m * semiminor_axis_m);
    const double coefficient_a = 1.0 + reduced_parameter / 16384.0 *
                                 (4096.0 + reduced_parameter *
                                  (-768.0 + reduced_parameter * (320.0 - 175.0 * reduced_parameter)));
    const double coefficient_b = reduced_parameter / 1024.0 *
                                 (256.0 + reduced_parameter *
                                  (-128.0 + reduced_parameter * (74.0 - 47.0 * reduced_parameter)));
    const double delta_sigma = coefficient_b * sin_sigma *
                               (cos_two_sigma_midpoint + coefficient_b / 4.0 *
                                (cos_sigma * (-1.0 + 2.0 * cos_two_sigma_midpoint * cos_two_sigma_midpoint) -
                                 coefficient_b / 6.0 * cos_two_sigma_midpoint *
                                 (-3.0 + 4.0 * sin_sigma * sin_sigma) *
                                 (-3.0 + 4.0 * cos_two_sigma_midpoint * cos_two_sigma_midpoint)));
    return semiminor_axis_m * coefficient_a * (sigma - delta_sigma);
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
    this->network.nodes_junctions.append(junction);
    return junction.uuid;
}

QUuid HydraulicNetworkEditor::addReservoir(const CoordinateWGS84 &coordinate)
{
    HydraulicNodeReservoir reservoir;
    reservoir.uuid = createUuidV7();
    reservoir.id = nextNodeId(QStringLiteral("R"));
    reservoir.coordinate_wgs84 = coordinate;
    this->network.nodes_reservoirs.append(reservoir);
    return reservoir.uuid;
}

QUuid HydraulicNetworkEditor::addTank(const CoordinateWGS84 &coordinate)
{
    HydraulicNodeTank tank;
    tank.uuid = createUuidV7();
    tank.id = nextNodeId(QStringLiteral("T"));
    tank.coordinate_wgs84 = coordinate;
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
    return removeEntityByUuid(this->network.nodes_junctions, uuid);
}

bool HydraulicNetworkEditor::deleteReservoir(const QUuid &uuid)
{
    return removeEntityByUuid(this->network.nodes_reservoirs, uuid);
}

bool HydraulicNetworkEditor::deleteTank(const QUuid &uuid)
{
    return removeEntityByUuid(this->network.nodes_tanks, uuid);
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
        length_m += distanceMeters(previous_coordinate, vertex.coordinate_wgs84);
        previous_coordinate = vertex.coordinate_wgs84;
    }

    length_m += distanceMeters(previous_coordinate, end_coordinate.value());
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
