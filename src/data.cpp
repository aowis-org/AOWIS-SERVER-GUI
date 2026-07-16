#include "data.h"

Data::Data(QObject *parent)
    : QObject{parent},
    database_gui(new DatabaseGui(this))
{
    connect(
        this->database_gui,
        &DatabaseGui::signalReady,
        this,
        &Data::onDatabaseReady
        );
    
    connect(
        this->database_gui,
        &DatabaseGui::signalError,
        this,
        [](const QString &message)
        {
            qCritical() << "Could not initialize database:" << message;
        }
        );
    
    DatabaseConfiguration configuration;
    this->database_gui->open(configuration);
}

void Data::onDatabaseReady()
{
    getProject();
}

void Data::getProject()
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
