#ifndef HYDRAULIC_DATA_H
#define HYDRAULIC_DATA_H

#include <QObject>
#include <QUuid>

#include <QDebug>

#include <aowis/model/project.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

#include <aowis/db/database_gui.h>

#include <aowis/epanet/dummy/dummy_networks.h>

#include "_enums_structs.h"

class HydraulicData : public QObject
{
    Q_OBJECT
public:
    explicit HydraulicData(QObject *parent = nullptr);
    
    void loadProject();
    
    const NetworkHydraulic &networkHydraulic() const;
    
    void setSelectedUuid(InfrastructureEntity entity_type, const QUuid &uuid);
    
    void addTank();
    void deleteTank();
    
    void setDataTank(HydraulicNodeTank tank);
    void setDataJunction(HydraulicNodeJunction junction);
    void setDataPipe(HydraulicLinkPipe pipe);
    void setDataPump(HydraulicLinkPump pump);
    void setDataValve(HydraulicLinkValve valve);
    void setDataReservoir(HydraulicNodeReservoir reservoir);
    
private:
    DatabaseGui *database_gui = nullptr;
    
    std::optional<Project> project;
    NetworkHydraulic network_hydraulic;
    
private slots:
    void onDatabaseReady();
    
signals:
    void signalSelectedTank(const HydraulicNodeTank &tank);
    void signalSelectedReservoir(const HydraulicNodeReservoir &reservoir);
    void signalSelectedJunction(const HydraulicNodeJunction &junction);
    void signalSelectedPipe(const HydraulicLinkPipe &pipe);
    void signalSelectedPump(const HydraulicLinkPump &pump);
    void signalSelectedValve(const HydraulicLinkValve &valve);
    void signalSelectedCustomerPoint(const NetworkHydraulicCustomerPoint &customer_point);
};

#endif // HYDRAULIC_DATA_H
