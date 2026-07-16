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
    initializeTestDB();
}

void Data::initializeTestDB()
{
    DatabaseShared *sharedDatabase = this->database_gui->sharedDatabase();
    
    if (sharedDatabase == nullptr)
    {
        qCritical() << "Shared database is not initialized";
        return;
    }
    
    const QString projectId =
        sharedDatabase->createProject(
            QStringLiteral("Test"),
            QStringLiteral("Test DB for Dev")
            );
    
    if (projectId.isEmpty())
    {
        qCritical() << "Could not create test project";
        return;
    }
    
    qDebug() << "Created test project:" << projectId;
}
