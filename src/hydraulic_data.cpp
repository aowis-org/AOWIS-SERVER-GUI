#include "hydraulic_data.h"

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
    this->network_hydraulic = DummyNetworks::networkTanks();
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
             << this->project->project_id
             << this->project->name;
}

const NetworkHydraulic &HydraulicData::networkHydraulic() const
{
    return this->network_hydraulic;
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

QUuid HydraulicData::addJunction(const CoordinateWGS84 &coordinate)
{
    return this->network_editor.addJunction(coordinate);
}

QUuid HydraulicData::addReservoir(const CoordinateWGS84 &coordinate)
{
    return this->network_editor.addReservoir(coordinate);
}

QUuid HydraulicData::addTank(const CoordinateWGS84 &coordinate)
{
    return this->network_editor.addTank(coordinate);
}

QUuid HydraulicData::addPipe(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                             const QList<CoordinateWGS84> &intermediate_vertices)
{
    return this->network_editor.addPipe(node_uuid_from, node_uuid_to, intermediate_vertices);
}

QUuid HydraulicData::addPump(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                             const CoordinateWGS84 &center_coordinate)
{
    return this->network_editor.addPump(node_uuid_from, node_uuid_to, center_coordinate);
}

QUuid HydraulicData::addValve(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                              const CoordinateWGS84 &center_coordinate)
{
    return this->network_editor.addValve(node_uuid_from, node_uuid_to, center_coordinate);
}

bool HydraulicData::setNodeCoordinate(const QUuid &uuid, const CoordinateWGS84 &coordinate)
{
    return this->network_editor.setNodeCoordinate(uuid, coordinate);
}

bool HydraulicData::setPipeVertexCoordinate(const QUuid &pipe_uuid, int vertex_index,
                                            const CoordinateWGS84 &coordinate)
{
    return this->network_editor.setPipeVertexCoordinate(pipe_uuid, vertex_index, coordinate);
}

bool HydraulicData::setPipeVertices(const QUuid &pipe_uuid,
                                    const QList<CoordinateWGS84> &intermediate_vertices)
{
    return this->network_editor.setPipeVertices(pipe_uuid, intermediate_vertices);
}

bool HydraulicData::setPumpCenterCoordinate(const QUuid &pump_uuid,
                                            const CoordinateWGS84 &coordinate)
{
    return this->network_editor.setPumpCenterCoordinate(pump_uuid, coordinate);
}

bool HydraulicData::setValveCenterCoordinate(const QUuid &valve_uuid,
                                             const CoordinateWGS84 &coordinate)
{
    return this->network_editor.setValveCenterCoordinate(valve_uuid, coordinate);
}

QUuid HydraulicData::splitPipeAtVertex(const QUuid &pipe_uuid, int vertex_index,
                                       const QUuid &junction_uuid)
{
    return this->network_editor.splitPipeAtVertex(pipe_uuid, vertex_index, junction_uuid);
}

bool HydraulicData::undoPipeSplit(const QUuid &first_pipe_uuid, const QUuid &second_pipe_uuid,
                                  const QUuid &junction_uuid)
{
    return this->network_editor.undoPipeSplit(first_pipe_uuid, second_pipe_uuid, junction_uuid);
}

bool HydraulicData::deleteJunction(const QUuid &uuid)
{
    return this->network_editor.deleteJunction(uuid);
}

bool HydraulicData::deleteReservoir(const QUuid &uuid)
{
    return this->network_editor.deleteReservoir(uuid);
}

bool HydraulicData::deleteTank(const QUuid &uuid)
{
    return this->network_editor.deleteTank(uuid);
}

bool HydraulicData::deletePipe(const QUuid &uuid)
{
    return this->network_editor.deletePipe(uuid);
}

bool HydraulicData::deletePump(const QUuid &uuid)
{
    return this->network_editor.deletePump(uuid);
}

bool HydraulicData::deleteValve(const QUuid &uuid)
{
    return this->network_editor.deleteValve(uuid);
}

void HydraulicData::setDataTank(HydraulicNodeTank tank)
{
    
}
void HydraulicData::setDataJunction(HydraulicNodeJunction junction)
{
    
}
void HydraulicData::setDataPipe(HydraulicLinkPipe pipe)
{
    
}
void HydraulicData::setDataPump(HydraulicLinkPump pump)
{
    
}
void HydraulicData::setDataValve(HydraulicLinkValve valve)
{
    
}
void HydraulicData::setDataReservoir(HydraulicNodeReservoir reservoir)
{
    
}
