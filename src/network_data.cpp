#include "network_data.h"

NetworkData::NetworkData(QObject *parent)
    : QObject{parent},
    database_gui(new DatabaseGui(this))
{
    connect(this->database_gui, &DatabaseGui::signalReady, this, &NetworkData::onDatabaseReady);
    
    connect(this->database_gui, &DatabaseGui::signalError, this, [](const QString &message)
    {
        qCritical() << "Could not initialize database:" << message;
    });
    
    DatabaseConfiguration configuration;
    this->database_gui->open(configuration);
}

void NetworkData::onDatabaseReady()
{
    loadProject();
    
    //this->network_hydraulic request = DummyNetworks::networkSimple();
    //this->network_hydraulic = DummyNetworks::networkTanks();
    this->network_hydraulic = DummyNetworks::networkOnMap();
    //this->network_hydraulic = DummyNetworks::networkTanksTimeline();
}

void NetworkData::loadProject()
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

const NetworkHydraulic &NetworkData::networkHydraulic() const
{
    return this->network_hydraulic;
}

void NetworkData::setSelectedUuid(InfrastructureEntity entity_type, const QUuid &uuid)
{
    switch (entity_type)
    {
    case InfrastructureEntity::Tank:
        for (const Tank &tank : this->network_hydraulic.tanks)
        {
            if (tank.uuid == uuid)
            {
                emit signalTankSelected(tank);
                return;
            }
        }
        
        break;
        
    case InfrastructureEntity::Reservoir:
        for (const Reservoir &reservoir : this->network_hydraulic.reservoirs)
        {
            if (reservoir.uuid == uuid)
            {
                emit signalReservoirSelected(reservoir);
                return;
            }
        }
        
        break;
        
    case InfrastructureEntity::Junction:
        for (const Junction &junction : this->network_hydraulic.junctions)
        {
            if (junction.uuid == uuid)
            {
                emit signalJunctionSelected(junction);
                return;
            }
        }
        
        break;
        
    case InfrastructureEntity::Pipe:
        for (const Pipe &pipe : this->network_hydraulic.pipes)
        {
            if (pipe.uuid == uuid)
            {
                emit signalPipeSelected(pipe);
                return;
            }
        }
        
        break;
        
    default:
        break;
    }
}
