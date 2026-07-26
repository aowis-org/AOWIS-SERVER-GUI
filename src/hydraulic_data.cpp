#include "hydraulic_data.h"

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
             << this->project->projectId
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
        for (const Tank &tank : this->network_hydraulic.tanks)
        {
            if (tank.uuid == uuid)
            {
                emit signalSelectedTank(tank);
                return;
            }
        }
        
        break;
        
    case InfrastructureEntity::Reservoir:
        for (const Reservoir &reservoir : this->network_hydraulic.reservoirs)
        {
            if (reservoir.uuid == uuid)
            {
                emit signalSelectedReservoir(reservoir);
                return;
            }
        }
        
        break;
        
    case InfrastructureEntity::Junction:
        for (const Junction &junction : this->network_hydraulic.junctions)
        {
            if (junction.uuid == uuid)
            {
                emit signalSelectedJunction(junction);
                return;
            }
        }
        
        break;
        
    case InfrastructureEntity::Pipe:
        for (const Pipe &pipe : this->network_hydraulic.pipes)
        {
            if (pipe.uuid == uuid)
            {
                emit signalSelectedPipe(pipe);
                return;
            }
        }
        
        break;
        
    case InfrastructureEntity::Pump:
        for (const Pump &pump : this->network_hydraulic.pumps)
        {
            if (pump.uuid == uuid)
            {
                emit signalSelectedPump(pump);
                return;
            }
        }
        
        break;
    
    case InfrastructureEntity::Valve:
        for (const Valve &valve : this->network_hydraulic.valves)
        {
            if (valve.uuid == uuid)
            {
                emit signalSelectedValve(valve);
                return;
            }
        }
        
        break;
    
    case InfrastructureEntity::CustomerPoint:
        for (const CustomerPoint &customer_point : this->network_hydraulic.customer_points)
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

void HydraulicData::addTank()
{
    
}
void HydraulicData::deleteTank()
{
    
}

void HydraulicData::setDataTank(Tank tank)
{
    
}
void HydraulicData::setDataJunction(Junction junction)
{
    
}
void HydraulicData::setDataPipe(Pipe pipe)
{
    
}
void HydraulicData::setDataPump(Pump pump)
{
    
}
void HydraulicData::setDataValve(Valve valve)
{
    
}
void HydraulicData::setDataReservoir(Reservoir reservoir)
{
    
}
