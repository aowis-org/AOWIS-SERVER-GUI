#ifndef NETWORK_DATA_H
#define NETWORK_DATA_H

#include <QObject>

#include <QDebug>

#include "database_gui.h"

#include <aowis/model/project.h>
#include <aowis/model/hydraulic/network.h>

#include "dummy/dummy_networks.h"

class NetworkData : public QObject
{
    Q_OBJECT
public:
    explicit NetworkData(QObject *parent = nullptr);
    
    void loadProject();
    
    const NetworkHydraulic &networkHydraulic() const;
    
    
private:
    DatabaseGui *database_gui = nullptr;
    
    std::optional<Project> project;
    NetworkHydraulic network_hydraulic;
    
private slots:
    void onDatabaseReady();
    
signals:
    Tank signalTankSelected();
    Reservoir signalReservoirSelected();
    Junction signalJunctionSelected();
    Pipe signalPipeSelected();
};

#endif // NETWORK_DATA_H
