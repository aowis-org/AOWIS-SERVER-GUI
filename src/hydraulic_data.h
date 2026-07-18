#ifndef HYDRAULIC_DATA_H
#define HYDRAULIC_DATA_H

#include <QObject>
#include <QUuid>

#include <QDebug>

#include "database_gui.h"

#include <aowis/model/project.h>
#include <aowis/model/hydraulic/network.h>

#include "dummy/dummy_networks.h"

#include "_enums_structs.h"

class HydraulicData : public QObject
{
    Q_OBJECT
public:
    explicit HydraulicData(QObject *parent = nullptr);
    
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
    void signalSelectedTank(const Tank &tank);
    void signalSelectedReservoir(const Reservoir &reservoir);
    void signalSelectedJunction(const Junction &junction);
    void signalSelectedPipe(const Pipe &pipe);
    //void signalSelectedPump(const Pump &pump);
};

#endif // HYDRAULIC_DATA_H
