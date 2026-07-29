#include "hydraulic_data.h"
#include "uuid_v7.h"

HydraulicData::HydraulicData(QObject *parent)
    : QObject{parent},
    database_gui(new DatabaseGui(this))
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
    
    //this->network_hydraulic request = DummyNetworks::networkSimple();
    this->network_hydraulic = DummyNetworks::networkTanks();
    //this->network_hydraulic = DummyNetworks::networkOnMap();
    //this->network_hydraulic = DummyNetworks::networkTanksTimeline();
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
    HydraulicNodeJunction junction;
    junction.uuid = createUuidV7();
    junction.latitude_deg = coordinate.latitude_deg;
    junction.longitude_deg = coordinate.longitude_deg;
    this->network_hydraulic.nodes_junctions.append(junction);
    return junction.uuid;
}

QUuid HydraulicData::addReservoir(const CoordinateWGS84 &coordinate)
{
    HydraulicNodeReservoir reservoir;
    reservoir.uuid = createUuidV7();
    reservoir.latitude_deg = coordinate.latitude_deg;
    reservoir.longitude_deg = coordinate.longitude_deg;
    this->network_hydraulic.nodes_reservoirs.append(reservoir);
    return reservoir.uuid;
}

QUuid HydraulicData::addTank(const CoordinateWGS84 &coordinate)
{
    HydraulicNodeTank tank;
    tank.uuid = createUuidV7();
    tank.latitude_deg = coordinate.latitude_deg;
    tank.longitude_deg = coordinate.longitude_deg;
    this->network_hydraulic.nodes_tanks.append(tank);
    return tank.uuid;
}

bool HydraulicData::deleteJunction(const QUuid &uuid)
{
    for (int i = 0; i < this->network_hydraulic.nodes_junctions.size(); i++)
    {
        if (this->network_hydraulic.nodes_junctions[i].uuid != uuid)
            continue;

        this->network_hydraulic.nodes_junctions.removeAt(i);
        return true;
    }

    return false;
}

void HydraulicData::deleteTank()
{
    
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
