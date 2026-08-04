#include "hydraulic_data.h"

#include <QHash>

namespace
{
template <typename LinkType>
bool appendNetworkRenderLink(const LinkType &source, InfrastructureEntity entity_type, quint32 render_id,
                             const QList<NetworkRenderNode> &nodes,
                             const QHash<QUuid, qsizetype> &node_indices,
                             QList<NetworkRenderLink> &links)
{
    const QHash<QUuid, qsizetype>::const_iterator start_iterator =
        node_indices.constFind(source.node_uuid_from);
    const QHash<QUuid, qsizetype>::const_iterator end_iterator =
        node_indices.constFind(source.node_uuid_to);

    if (start_iterator == node_indices.cend() || end_iterator == node_indices.cend())
        return false;

    const NetworkRenderNode &start_node = nodes.at(start_iterator.value());
    const NetworkRenderNode &end_node = nodes.at(end_iterator.value());

    NetworkRenderLink link;
    link.render_id = render_id;
    link.id = source.id;
    link.uuid = source.uuid;
    link.entity_type = entity_type;
    link.start_node_render_id = start_node.render_id;
    link.end_node_render_id = end_node.render_id;
    link.vertices_wgs84.reserve(source.vertices.size() + 2);
    link.vertices_wgs84.append(start_node.coordinate_wgs84);

    for (const HydraulicLinkVertex &vertex : source.vertices)
        link.vertices_wgs84.append(vertex.coordinate_wgs84);

    link.vertices_wgs84.append(end_node.coordinate_wgs84);
    links.append(link);
    return true;
}
}

HydraulicData::HydraulicData(QObject *parent)
    : QObject{parent},
      database_gui(new DatabaseGui(this)),
      network_editor(this->network_hydraulic)
{
    connect(this->database_gui, &DatabaseGui::signalReady, this, &HydraulicData::onDatabaseReady);
    
    connect(this->database_gui, &DatabaseGui::signalError, this, [](const QString &message)
    {
        qCritical() << "Could not initialize database:" << message;
    });
    
    DatabaseConfiguration configuration;
    this->database_gui->open(configuration);
}

void HydraulicData::onDatabaseReady()
{
    loadProject();
    
    //this->network_hydraulic = DummyNetworks::networkSimple();
    //this->network_hydraulic = DummyNetworks::networkTanks();
    this->network_hydraulic = DummyMarburgNetworkGenerator::generateFractal();
    //this->network_hydraulic = RandomHydraulicNetworkGenerator::generateFractal();
    markNetworkChanged(NetworkChange::Geometry);
    //this->network_hydraulic = DummyNetworks::networkOnMap();
    //this->network_hydraulic = DummyNetworks::networkTanksTimeline();

    emit signalNetworkLoaded();
}

void HydraulicData::loadProject()
{
    DatabaseShared *sharedDatabase = this->database_gui->sharedDatabase();
    
    if (sharedDatabase == nullptr)
    {
        qCritical() << "Shared database is not initialized";
        return;
    }
    
    const QString configKey = QStringLiteral("development_test_project_id");
    
    const std::optional<QString> configuredProjectId =
        sharedDatabase->configValue(configKey);
    
    if (configuredProjectId.has_value())
    {
        const QUuid projectId(configuredProjectId.value());
        
        if (!projectId.isNull())
            this->project = sharedDatabase->projectById(projectId);
    }
    
    if (!this->project.has_value())
    {
        const QUuid projectId = sharedDatabase->createProject(
            QStringLiteral("Test"),
            QStringLiteral("Test DB for Dev")
            );
        
        if (projectId.isNull())
        {
            qCritical() << "Could not create test project";
            return;
        }
        
        if (!sharedDatabase->setConfigValue(
                configKey,
                projectId.toString(QUuid::WithoutBraces)))
        {
            qCritical() << "Could not store test project ID";
            return;
        }
        
        this->project = sharedDatabase->projectById(projectId);
    }
    
    if (!this->project.has_value())
    {
        qCritical() << "Could not retrieve test project";
        return;
    }
    
    qDebug() << "Test project:"
             << this->project->uuid
             << this->project->name;
}

const NetworkHydraulic &HydraulicData::networkHydraulic() const
{
    return this->network_hydraulic;
}

const NetworkRenderSnapshot &HydraulicData::networkRenderSnapshot() const
{
    if (this->network_render_snapshot.geometry_revision != this->geometry_revision)
        rebuildNetworkRenderSnapshot();

    this->network_render_snapshot.visual_revision = this->visual_revision;
    return this->network_render_snapshot;
}

quint64 HydraulicData::geometryRevision() const
{
    return this->geometry_revision;
}

quint64 HydraulicData::visualRevision() const
{
    return this->visual_revision;
}

QDateTime HydraulicData::timeChangedLast() const
{
    return this->time_changed_last;
}


std::optional<HydraulicNodeJunction> HydraulicData::junction(const QUuid &uuid) const
{
    return this->network_editor.junction(uuid);
}

std::optional<HydraulicNodeReservoir> HydraulicData::reservoir(const QUuid &uuid) const
{
    return this->network_editor.reservoir(uuid);
}

std::optional<HydraulicNodeTank> HydraulicData::tank(const QUuid &uuid) const
{
    return this->network_editor.tank(uuid);
}

std::optional<HydraulicLinkPipe> HydraulicData::pipe(const QUuid &uuid) const
{
    return this->network_editor.pipe(uuid);
}

std::optional<HydraulicLinkPump> HydraulicData::pump(const QUuid &uuid) const
{
    return this->network_editor.pump(uuid);
}

std::optional<HydraulicLinkValve> HydraulicData::valve(const QUuid &uuid) const
{
    return this->network_editor.valve(uuid);
}

std::optional<HydraulicNodeCommonData> HydraulicData::nodeCommonData(
    InfrastructureEntity entity_type, const QUuid &uuid) const
{
    switch (entity_type)
    {
    case InfrastructureEntity::Junction:
    {
        const std::optional<HydraulicNodeJunction> node = junction(uuid);
        if (!node.has_value())
            return std::nullopt;

        return HydraulicNodeCommonData{node->id, node->uuid, node->coordinate_wgs84, node->metadata};
    }
    case InfrastructureEntity::Reservoir:
    {
        const std::optional<HydraulicNodeReservoir> node = reservoir(uuid);
        if (!node.has_value())
            return std::nullopt;

        return HydraulicNodeCommonData{node->id, node->uuid, node->coordinate_wgs84, node->metadata};
    }
    case InfrastructureEntity::Tank:
    {
        const std::optional<HydraulicNodeTank> node = tank(uuid);
        if (!node.has_value())
            return std::nullopt;

        return HydraulicNodeCommonData{node->id, node->uuid, node->coordinate_wgs84, node->metadata};
    }
    default:
        return std::nullopt;
    }
}

std::optional<HydraulicLinkCommonData> HydraulicData::linkCommonData(
    InfrastructureEntity entity_type, const QUuid &uuid) const
{
    switch (entity_type)
    {
    case InfrastructureEntity::Pipe:
    {
        const std::optional<HydraulicLinkPipe> link = pipe(uuid);
        if (!link.has_value())
            return std::nullopt;

        return HydraulicLinkCommonData{link->id, link->uuid, link->metadata};
    }
    case InfrastructureEntity::Pump:
    {
        const std::optional<HydraulicLinkPump> link = pump(uuid);
        if (!link.has_value())
            return std::nullopt;

        return HydraulicLinkCommonData{link->id, link->uuid, link->metadata};
    }
    case InfrastructureEntity::Valve:
    {
        const std::optional<HydraulicLinkValve> link = valve(uuid);
        if (!link.has_value())
            return std::nullopt;

        return HydraulicLinkCommonData{link->id, link->uuid, link->metadata};
    }
    default:
        return std::nullopt;
    }
}

std::optional<InfrastructureEntity> HydraulicData::nodeEntityType(const QUuid &uuid) const
{
    if (uuid.isNull())
        return std::nullopt;

    for (const HydraulicNodeJunction &junction : this->network_hydraulic.nodes_junctions)
    {
        if (junction.uuid == uuid)
            return InfrastructureEntity::Junction;
    }

    for (const HydraulicNodeReservoir &reservoir : this->network_hydraulic.nodes_reservoirs)
    {
        if (reservoir.uuid == uuid)
            return InfrastructureEntity::Reservoir;
    }

    for (const HydraulicNodeTank &tank : this->network_hydraulic.nodes_tanks)
    {
        if (tank.uuid == uuid)
            return InfrastructureEntity::Tank;
    }

    return std::nullopt;
}

std::optional<InfrastructureEntity> HydraulicData::linkEntityType(const QUuid &uuid) const
{
    if (uuid.isNull())
        return std::nullopt;

    for (const HydraulicLinkPipe &pipe : this->network_hydraulic.links_pipes)
    {
        if (pipe.uuid == uuid)
            return InfrastructureEntity::Pipe;
    }

    for (const HydraulicLinkPump &pump : this->network_hydraulic.links_pumps)
    {
        if (pump.uuid == uuid)
            return InfrastructureEntity::Pump;
    }

    for (const HydraulicLinkValve &valve : this->network_hydraulic.links_valves)
    {
        if (valve.uuid == uuid)
            return InfrastructureEntity::Valve;
    }

    return std::nullopt;
}

void HydraulicData::markNetworkChanged(NetworkChange change)
{
    if (change == NetworkChange::Geometry)
        ++this->geometry_revision;

    ++this->visual_revision;
    this->time_changed_last = QDateTime::currentDateTimeUtc();

    if (change == NetworkChange::Geometry)
        emit signalNetworkGeometryChanged(this->geometry_revision);
}

bool HydraulicData::emitNodeChangedIfSuccessful(const QUuid &uuid, bool successful, NetworkChange change)
{
    if (!successful)
        return false;

    markNetworkChanged(change);

    const std::optional<InfrastructureEntity> entity_type = nodeEntityType(uuid);
    if (!entity_type.has_value())
        return false;
    emit signalNodeChanged(entity_type.value(), uuid);
    return true;
}

bool HydraulicData::emitLinkChangedIfSuccessful(const QUuid &uuid, bool successful, NetworkChange change)
{
    if (!successful)
        return false;

    markNetworkChanged(change);

    const std::optional<InfrastructureEntity> entity_type = linkEntityType(uuid);
    if (!entity_type.has_value())
        return false;
    emit signalLinkChanged(entity_type.value(), uuid);
    return true;
}

void HydraulicData::emitConnectedPipeChanges(const QUuid &node_uuid)
{
    for (const HydraulicLinkPipe &pipe : this->network_hydraulic.links_pipes)
    {
        if (pipe.node_uuid_from == node_uuid || pipe.node_uuid_to == node_uuid)
            emit signalLinkChanged(InfrastructureEntity::Pipe, pipe.uuid);
    }
}

void HydraulicData::rebuildNetworkRenderSnapshot() const
{
    NetworkRenderSnapshot snapshot;
    snapshot.geometry_revision = this->geometry_revision;
    snapshot.visual_revision = this->visual_revision;

    const qsizetype node_count = this->network_hydraulic.nodes_junctions.size() +
                                 this->network_hydraulic.nodes_reservoirs.size() +
                                 this->network_hydraulic.nodes_tanks.size();
    const qsizetype link_count = this->network_hydraulic.links_pipes.size() +
                                 this->network_hydraulic.links_pumps.size() +
                                 this->network_hydraulic.links_valves.size();
    snapshot.nodes.reserve(node_count);
    snapshot.links.reserve(link_count);

    QHash<QUuid, qsizetype> node_indices;
    node_indices.reserve(node_count);
    quint32 next_node_render_id = 1;

    for (const HydraulicNodeJunction &source : this->network_hydraulic.nodes_junctions)
    {
        NetworkRenderNode node;
        node.render_id = next_node_render_id++;
        node.id = source.id;
        node.uuid = source.uuid;
        node.entity_type = InfrastructureEntity::Junction;
        node.coordinate_wgs84 = source.coordinate_wgs84;
        node_indices.insert(node.uuid, snapshot.nodes.size());
        snapshot.nodes.append(node);
    }

    for (const HydraulicNodeReservoir &source : this->network_hydraulic.nodes_reservoirs)
    {
        NetworkRenderNode node;
        node.render_id = next_node_render_id++;
        node.id = source.id;
        node.uuid = source.uuid;
        node.entity_type = InfrastructureEntity::Reservoir;
        node.coordinate_wgs84 = source.coordinate_wgs84;
        node_indices.insert(node.uuid, snapshot.nodes.size());
        snapshot.nodes.append(node);
    }

    for (const HydraulicNodeTank &source : this->network_hydraulic.nodes_tanks)
    {
        NetworkRenderNode node;
        node.render_id = next_node_render_id++;
        node.id = source.id;
        node.uuid = source.uuid;
        node.entity_type = InfrastructureEntity::Tank;
        node.coordinate_wgs84 = source.coordinate_wgs84;
        node_indices.insert(node.uuid, snapshot.nodes.size());
        snapshot.nodes.append(node);
    }

    quint32 next_link_render_id = 1;

    for (const HydraulicLinkPipe &source : this->network_hydraulic.links_pipes)
    {
        if (appendNetworkRenderLink(source, InfrastructureEntity::Pipe, next_link_render_id,
                                    snapshot.nodes, node_indices, snapshot.links))
            ++next_link_render_id;
    }

    for (const HydraulicLinkPump &source : this->network_hydraulic.links_pumps)
    {
        if (appendNetworkRenderLink(source, InfrastructureEntity::Pump, next_link_render_id,
                                    snapshot.nodes, node_indices, snapshot.links))
            ++next_link_render_id;
    }

    for (const HydraulicLinkValve &source : this->network_hydraulic.links_valves)
    {
        if (appendNetworkRenderLink(source, InfrastructureEntity::Valve, next_link_render_id,
                                    snapshot.nodes, node_indices, snapshot.links))
            ++next_link_render_id;
    }

    this->network_render_snapshot = snapshot;
}

void HydraulicData::setSelectedUuid(InfrastructureEntity entity_type, const QUuid &uuid)
{
    switch (entity_type)
    {
    case InfrastructureEntity::Tank:
        for (const HydraulicNodeTank &tank : this->network_hydraulic.nodes_tanks)
        {
            if (tank.uuid == uuid)
            {
                emit signalSelectedTank(tank);
                return;
            }
        }
        
        break;
        
    case InfrastructureEntity::Reservoir:
        for (const HydraulicNodeReservoir &reservoir : this->network_hydraulic.nodes_reservoirs)
        {
            if (reservoir.uuid == uuid)
            {
                emit signalSelectedReservoir(reservoir);
                return;
            }
        }
        
        break;
        
    case InfrastructureEntity::Junction:
        for (const HydraulicNodeJunction &junction : this->network_hydraulic.nodes_junctions)
        {
            if (junction.uuid == uuid)
            {
                emit signalSelectedJunction(junction);
                return;
            }
        }
        
        break;
        
    case InfrastructureEntity::Pipe:
        for (const HydraulicLinkPipe &pipe : this->network_hydraulic.links_pipes)
        {
            if (pipe.uuid == uuid)
            {
                emit signalSelectedPipe(pipe);
                return;
            }
        }
        
        break;
        
    case InfrastructureEntity::Pump:
        for (const HydraulicLinkPump &pump : this->network_hydraulic.links_pumps)
        {
            if (pump.uuid == uuid)
            {
                emit signalSelectedPump(pump);
                return;
            }
        }
        
        break;
    
    case InfrastructureEntity::Valve:
        for (const HydraulicLinkValve &valve : this->network_hydraulic.links_valves)
        {
            if (valve.uuid == uuid)
            {
                emit signalSelectedValve(valve);
                return;
            }
        }
        
        break;
    
    case InfrastructureEntity::CustomerPoint:
        for (const NetworkHydraulicCustomerPoint &customer_point : this->network_hydraulic.customer_points)
        {
            if (customer_point.uuid == uuid)
            {
                emit signalSelectedCustomerPoint(customer_point);
                return;
            }
        }
        
    default:
        break;
    }
}

void HydraulicData::requestNodeLocate(InfrastructureEntity entity_type, const QUuid &uuid)
{
    if (!nodeCommonData(entity_type, uuid).has_value())
        return;

    emit signalNodeLocateRequested(entity_type, uuid);
}

QUuid HydraulicData::addJunction(const CoordinateWGS84 &coordinate)
{
    const QUuid uuid = this->network_editor.addJunction(coordinate);
    if (!uuid.isNull())
        markNetworkChanged(NetworkChange::Geometry);
    return uuid;
}

QUuid HydraulicData::addReservoir(const CoordinateWGS84 &coordinate)
{
    const QUuid uuid = this->network_editor.addReservoir(coordinate);
    if (!uuid.isNull())
        markNetworkChanged(NetworkChange::Geometry);
    return uuid;
}

QUuid HydraulicData::addTank(const CoordinateWGS84 &coordinate)
{
    const QUuid uuid = this->network_editor.addTank(coordinate);
    if (!uuid.isNull())
        markNetworkChanged(NetworkChange::Geometry);
    return uuid;
}

QUuid HydraulicData::addPipe(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                             const QList<CoordinateWGS84> &intermediate_vertices)
{
    const QUuid uuid =
        this->network_editor.addPipe(node_uuid_from, node_uuid_to, intermediate_vertices);
    if (!uuid.isNull())
        markNetworkChanged(NetworkChange::Geometry);
    return uuid;
}

QUuid HydraulicData::addPump(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                             const CoordinateWGS84 &center_coordinate)
{
    const QUuid uuid =
        this->network_editor.addPump(node_uuid_from, node_uuid_to, center_coordinate);
    if (!uuid.isNull())
        markNetworkChanged(NetworkChange::Geometry);
    return uuid;
}

QUuid HydraulicData::addValve(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                              const CoordinateWGS84 &center_coordinate)
{
    const QUuid uuid =
        this->network_editor.addValve(node_uuid_from, node_uuid_to, center_coordinate);
    if (!uuid.isNull())
        markNetworkChanged(NetworkChange::Geometry);
    return uuid;
}

bool HydraulicData::setNodeId(const QUuid &uuid, const QString &id)
{
    return emitNodeChangedIfSuccessful(uuid, this->network_editor.setNodeId(uuid, id));
}

bool HydraulicData::setNodeModelRole(const QUuid &uuid, EntityModelRole model_role)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setNodeModelRole(uuid, model_role));
}

bool HydraulicData::setNodeDateAdded(const QUuid &uuid, const std::optional<QDate> &date_added)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setNodeDateAdded(uuid, date_added));
}

bool HydraulicData::setNodeDateInstalled(const QUuid &uuid,
                                           const std::optional<QDate> &date_installed)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setNodeDateInstalled(uuid, date_installed));
}

bool HydraulicData::setNodeEnabled(const QUuid &uuid, bool enabled)
{
    return emitNodeChangedIfSuccessful(uuid, this->network_editor.setNodeEnabled(uuid, enabled));
}

bool HydraulicData::setNodeCoordinate(const QUuid &uuid, const CoordinateWGS84 &coordinate)
{
    const std::optional<InfrastructureEntity> entity_type = nodeEntityType(uuid);
    if (!entity_type.has_value())
        return false;
    if (!this->network_editor.setNodeCoordinate(uuid, coordinate))
        return false;

    markNetworkChanged(NetworkChange::Geometry);
    emit signalNodeChanged(entity_type.value(), uuid);
    emitConnectedPipeChanges(uuid);
    return true;
}

bool HydraulicData::setLinkId(const QUuid &uuid, const QString &id)
{
    return emitLinkChangedIfSuccessful(uuid, this->network_editor.setLinkId(uuid, id));
}

bool HydraulicData::setLinkModelRole(const QUuid &uuid, EntityModelRole model_role)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setLinkModelRole(uuid, model_role));
}

bool HydraulicData::setLinkDateAdded(const QUuid &uuid,
                                     const std::optional<QDate> &date_added)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setLinkDateAdded(uuid, date_added));
}

bool HydraulicData::setLinkDateInstalled(const QUuid &uuid,
                                         const std::optional<QDate> &date_installed)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setLinkDateInstalled(uuid, date_installed));
}

bool HydraulicData::setLinkEnabled(const QUuid &uuid, bool enabled)
{
    return emitLinkChangedIfSuccessful(uuid, this->network_editor.setLinkEnabled(uuid, enabled));
}

bool HydraulicData::setJunctionElevationInputType(
    const QUuid &uuid, HydraulicNodeElevationInputType input_type)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionElevationInputType(uuid, input_type));
}

bool HydraulicData::setJunctionElevationM(const QUuid &uuid, double elevation_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionElevationM(uuid, elevation_m));
}

bool HydraulicData::setJunctionTerrainElevationM(const QUuid &uuid, double terrain_elevation_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionTerrainElevationM(uuid, terrain_elevation_m));
}

bool HydraulicData::setJunctionElevationOffsetM(const QUuid &uuid, double elevation_offset_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionElevationOffsetM(uuid, elevation_offset_m));
}

bool HydraulicData::addJunctionDemand(const QUuid &uuid,
                                      const HydraulicNodeJunctionDemand &demand)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.addJunctionDemand(uuid, demand));
}

bool HydraulicData::removeJunctionDemand(const QUuid &uuid, int demand_index)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.removeJunctionDemand(uuid, demand_index));
}

bool HydraulicData::setJunctionDemandCategoryName(const QUuid &uuid, int demand_index,
                                                  const QString &category_name)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionDemandCategoryName(
                  uuid, demand_index, category_name));
}

bool HydraulicData::setJunctionDemandBaseDemandM3PerH(
    const QUuid &uuid, int demand_index, double base_demand_m3_per_h)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionDemandBaseDemandM3PerH(
                  uuid, demand_index, base_demand_m3_per_h));
}

bool HydraulicData::setJunctionDemandPatternMode(
    const QUuid &uuid, int demand_index, HydraulicTimePatternMode pattern_mode)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionDemandPatternMode(
                  uuid, demand_index, pattern_mode));
}

bool HydraulicData::setJunctionDemandPatternUuid(const QUuid &uuid, int demand_index,
                                                 const QUuid &pattern_uuid)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionDemandPatternUuid(
                  uuid, demand_index, pattern_uuid));
}

bool HydraulicData::setJunctionDemandSourceMethod(
    const QUuid &uuid, int demand_index, HydraulicNodeJunctionDemandSourceMethod source_method)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionDemandSourceMethod(
                  uuid, demand_index, source_method));
}

bool HydraulicData::setJunctionDemandNote(const QUuid &uuid, int demand_index,
                                          const QString &note)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionDemandNote(uuid, demand_index, note));
}

bool HydraulicData::setJunctionEmitterCoefficientM3PerHPerMExponent(
    const QUuid &uuid, double emitter_coefficient_m3_per_h_per_m_exponent)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionEmitterCoefficientM3PerHPerMExponent(
                  uuid, emitter_coefficient_m3_per_h_per_m_exponent));
}

bool HydraulicData::setReservoirHeadInputType(
    const QUuid &uuid, HydraulicNodeElevationInputType input_type)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setReservoirHeadInputType(uuid, input_type));
}

bool HydraulicData::setReservoirHeadM(const QUuid &uuid, double head_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setReservoirHeadM(uuid, head_m));
}

bool HydraulicData::setReservoirTerrainElevationM(const QUuid &uuid,
                                                  double terrain_elevation_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setReservoirTerrainElevationM(
                  uuid, terrain_elevation_m));
}

bool HydraulicData::setReservoirHeadOffsetM(const QUuid &uuid, double head_offset_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setReservoirHeadOffsetM(uuid, head_offset_m));
}

bool HydraulicData::setReservoirHeadPatternMode(const QUuid &uuid, HydraulicTimePatternMode pattern_mode)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setReservoirHeadPatternMode(uuid, pattern_mode));
}

bool HydraulicData::setReservoirHeadPatternUuid(const QUuid &uuid, const QUuid &pattern_uuid)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setReservoirHeadPatternUuid(uuid, pattern_uuid));
}

bool HydraulicData::setTankElevationInputType(
    const QUuid &uuid, HydraulicNodeTankElevationInputType input_type)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankElevationInputType(uuid, input_type));
}

bool HydraulicData::setTankBottomElevationM(const QUuid &uuid, double bottom_elevation_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankBottomElevationM(uuid, bottom_elevation_m));
}

bool HydraulicData::setTankTerrainElevationM(const QUuid &uuid, double terrain_elevation_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankTerrainElevationM(uuid, terrain_elevation_m));
}

bool HydraulicData::setTankBottomOffsetM(const QUuid &uuid, double bottom_offset_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankBottomOffsetM(uuid, bottom_offset_m));
}

bool HydraulicData::setTankWaterLevelInitialM(const QUuid &uuid, double water_level_initial_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankWaterLevelInitialM(uuid, water_level_initial_m));
}

bool HydraulicData::setTankWaterLevelMinimumM(const QUuid &uuid, double water_level_minimum_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankWaterLevelMinimumM(uuid, water_level_minimum_m));
}

bool HydraulicData::setTankWaterLevelMaximumM(const QUuid &uuid, double water_level_maximum_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankWaterLevelMaximumM(uuid, water_level_maximum_m));
}

bool HydraulicData::setTankGeometryInputType(
    const QUuid &uuid, HydraulicNodeTankGeometryInputType input_type)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankGeometryInputType(uuid, input_type));
}

bool HydraulicData::setTankDiameterM(const QUuid &uuid, double diameter_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankDiameterM(uuid, diameter_m));
}

bool HydraulicData::setTankCrossSectionAreaM2(const QUuid &uuid, double cross_section_area_m2)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankCrossSectionAreaM2(uuid, cross_section_area_m2));
}

bool HydraulicData::setTankVolumeAtMaximumLevelM3(
    const QUuid &uuid, double volume_at_maximum_level_m3)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankVolumeAtMaximumLevelM3(
                  uuid, volume_at_maximum_level_m3));
}

bool HydraulicData::setTankMinimumVolumeM3(const QUuid &uuid, double minimum_volume_m3)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankMinimumVolumeM3(uuid, minimum_volume_m3));
}

bool HydraulicData::setTankVolumeCurveUuid(const QUuid &uuid, const QUuid &volume_curve_uuid)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankVolumeCurveUuid(uuid, volume_curve_uuid));
}

bool HydraulicData::setTankCanOverflow(const QUuid &uuid, bool can_overflow)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankCanOverflow(uuid, can_overflow));
}

bool HydraulicData::setPipeInitialStatus(
    const QUuid &uuid, HydraulicLinkPipeInitialStatus initial_status)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setPipeInitialStatus(uuid, initial_status));
}

bool HydraulicData::setPipeDiameterMm(const QUuid &uuid, double diameter_mm)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setPipeDiameterMm(uuid, diameter_mm));
}

bool HydraulicData::setPipeMeasuredLengthM(
    const QUuid &uuid, const std::optional<double> &length_measured_m)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setPipeMeasuredLengthM(uuid, length_measured_m));
}

bool HydraulicData::setPipeMaterialId(const QUuid &uuid, const QString &material_id)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setPipeMaterialId(uuid, material_id));
}

bool HydraulicData::setPipeRoughnessHw(const QUuid &uuid, double roughness_hw)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setPipeRoughnessHw(uuid, roughness_hw));
}

bool HydraulicData::setPipeRoughnessDwMm(const QUuid &uuid, double roughness_dw_mm)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setPipeRoughnessDwMm(uuid, roughness_dw_mm));
}

bool HydraulicData::setPipeRoughnessCm(const QUuid &uuid, double roughness_cm)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setPipeRoughnessCm(uuid, roughness_cm));
}

bool HydraulicData::setPipeMinorLoss(const QUuid &uuid, double minor_loss)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setPipeMinorLoss(uuid, minor_loss));
}

bool HydraulicData::setPipeVertexCoordinate(const QUuid &pipe_uuid, int vertex_index,
                                            const CoordinateWGS84 &coordinate)
{
    return emitLinkChangedIfSuccessful(
        pipe_uuid,
        this->network_editor.setPipeVertexCoordinate(pipe_uuid, vertex_index, coordinate),
        NetworkChange::Geometry);
}

bool HydraulicData::setPipeVertices(const QUuid &pipe_uuid,
                                    const QList<CoordinateWGS84> &intermediate_vertices)
{
    return emitLinkChangedIfSuccessful(
        pipe_uuid, this->network_editor.setPipeVertices(pipe_uuid, intermediate_vertices),
        NetworkChange::Geometry);
}

bool HydraulicData::setPumpCenterCoordinate(const QUuid &pump_uuid,
                                            const CoordinateWGS84 &coordinate)
{
    return emitLinkChangedIfSuccessful(
        pump_uuid, this->network_editor.setPumpCenterCoordinate(pump_uuid, coordinate),
        NetworkChange::Geometry);
}

bool HydraulicData::setValveCenterCoordinate(const QUuid &valve_uuid,
                                             const CoordinateWGS84 &coordinate)
{
    return emitLinkChangedIfSuccessful(
        valve_uuid, this->network_editor.setValveCenterCoordinate(valve_uuid, coordinate),
        NetworkChange::Geometry);
}

QUuid HydraulicData::splitPipeAtVertex(const QUuid &pipe_uuid, int vertex_index,
                                       const QUuid &junction_uuid)
{
    const QUuid second_pipe_uuid =
        this->network_editor.splitPipeAtVertex(pipe_uuid, vertex_index, junction_uuid);
    if (!second_pipe_uuid.isNull())
        markNetworkChanged(NetworkChange::Geometry);
    return second_pipe_uuid;
}

bool HydraulicData::undoPipeSplit(const QUuid &first_pipe_uuid, const QUuid &second_pipe_uuid,
                                  const QUuid &junction_uuid)
{
    const bool successful =
        this->network_editor.undoPipeSplit(first_pipe_uuid, second_pipe_uuid, junction_uuid);
    if (successful)
        markNetworkChanged(NetworkChange::Geometry);
    return successful;
}

bool HydraulicData::deleteJunction(const QUuid &uuid)
{
    const bool successful = this->network_editor.deleteJunction(uuid);
    if (successful)
        markNetworkChanged(NetworkChange::Geometry);
    return successful;
}

bool HydraulicData::deleteReservoir(const QUuid &uuid)
{
    const bool successful = this->network_editor.deleteReservoir(uuid);
    if (successful)
        markNetworkChanged(NetworkChange::Geometry);
    return successful;
}

bool HydraulicData::deleteTank(const QUuid &uuid)
{
    const bool successful = this->network_editor.deleteTank(uuid);
    if (successful)
        markNetworkChanged(NetworkChange::Geometry);
    return successful;
}

bool HydraulicData::deletePipe(const QUuid &uuid)
{
    const bool successful = this->network_editor.deletePipe(uuid);
    if (successful)
        markNetworkChanged(NetworkChange::Geometry);
    return successful;
}

bool HydraulicData::deletePump(const QUuid &uuid)
{
    const bool successful = this->network_editor.deletePump(uuid);
    if (successful)
        markNetworkChanged(NetworkChange::Geometry);
    return successful;
}

bool HydraulicData::deleteValve(const QUuid &uuid)
{
    const bool successful = this->network_editor.deleteValve(uuid);
    if (successful)
        markNetworkChanged(NetworkChange::Geometry);
    return successful;
}
