#ifndef NETWORK_DATA_H
#define NETWORK_DATA_H

#include <QObject>
#include <QUuid>

#include <QDebug>

#include "database_gui.h"

#include <aowis/model/project.h>
#include <aowis/model/hydraulic/network.h>

#include "dummy/dummy_networks.h"

#include "_enums_structs.h"

class NetworkData : public QObject
{
    Q_OBJECT
public:
    explicit NetworkData(QObject *parent = nullptr);
    
    void loadProject();
    
    const NetworkHydraulic &networkHydraulic() const;
    
    void setSelectedUuid(InfrastructureEntity entity_type, const QUuid &uuid);
    
private:
    DatabaseGui *database_gui = nullptr;
    
    std::optional<Project> project;
    NetworkHydraulic network_hydraulic;
    
private slots:
    void onDatabaseReady();
    
signals:
    void signalTankSelected(const Tank &tank);
    void signalReservoirSelected(const Reservoir &reservoir);
    void signalJunctionSelected(const Junction &junction);
    void signalPipeSelected(const Pipe &pipe);
};

#endif // NETWORK_DATA_H
