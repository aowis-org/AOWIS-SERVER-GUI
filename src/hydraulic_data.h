#ifndef HYDRAULIC_DATA_H
#define HYDRAULIC_DATA_H

#include <optional>

#include <QDate>
#include <QList>
#include <QObject>
#include <QString>
#include <QUuid>

#include <QDebug>

#include <aowis/model/project.h>
#include <aowis/model/entity.h>
#include <aowis/model/gis.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

#include <aowis/db/database_gui.h>

#include <aowis/epanet/dummy/dummy_networks.h>

#include "_enums_structs.h"
#include "hydraulic_network_editor.h"

struct HydraulicNodeCommonData
{
    QString id;
    QUuid uuid;
    CoordinateWGS84 coordinate_wgs84;
    HydraulicEntityMetadata metadata;
};

class HydraulicData : public QObject
{
    Q_OBJECT
public:
    explicit HydraulicData(QObject *parent = nullptr);

    void loadProject();

    const NetworkHydraulic &networkHydraulic() const;
    std::optional<HydraulicNodeCommonData> nodeCommonData(InfrastructureEntity entity_type,
                                                           const QUuid &uuid) const;

    void setSelectedUuid(InfrastructureEntity entity_type, const QUuid &uuid);
    void requestNodeLocate(InfrastructureEntity entity_type, const QUuid &uuid);

    QUuid addJunction(const CoordinateWGS84 &coordinate);
    QUuid addReservoir(const CoordinateWGS84 &coordinate);
    QUuid addTank(const CoordinateWGS84 &coordinate);

    QUuid addPipe(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                  const QList<CoordinateWGS84> &intermediate_vertices);
    QUuid addPump(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                  const CoordinateWGS84 &center_coordinate);
    QUuid addValve(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                   const CoordinateWGS84 &center_coordinate);

    bool setNodeId(const QUuid &uuid, const QString &id);
    bool setNodeModelRole(const QUuid &uuid, EntityModelRole model_role);
    bool setNodeDateAdded(const QUuid &uuid, const std::optional<QDate> &date_added);
    bool setNodeDateInstalled(const QUuid &uuid, const std::optional<QDate> &date_installed);
    bool setNodeEnabled(const QUuid &uuid, bool enabled);
    bool setNodeCoordinate(const QUuid &uuid, const CoordinateWGS84 &coordinate);
    bool setPipeVertexCoordinate(const QUuid &pipe_uuid, int vertex_index,
                                 const CoordinateWGS84 &coordinate);
    bool setPipeVertices(const QUuid &pipe_uuid,
                         const QList<CoordinateWGS84> &intermediate_vertices);
    bool setPumpCenterCoordinate(const QUuid &pump_uuid, const CoordinateWGS84 &coordinate);
    bool setValveCenterCoordinate(const QUuid &valve_uuid, const CoordinateWGS84 &coordinate);

    QUuid splitPipeAtVertex(const QUuid &pipe_uuid, int vertex_index, const QUuid &junction_uuid);
    bool undoPipeSplit(const QUuid &first_pipe_uuid, const QUuid &second_pipe_uuid,
                       const QUuid &junction_uuid);

    bool deleteJunction(const QUuid &uuid);
    bool deleteReservoir(const QUuid &uuid);
    bool deleteTank(const QUuid &uuid);
    bool deletePipe(const QUuid &uuid);
    bool deletePump(const QUuid &uuid);
    bool deleteValve(const QUuid &uuid);

private:
    std::optional<InfrastructureEntity> nodeEntityType(const QUuid &uuid) const;
    bool emitNodeChangedIfSuccessful(const QUuid &uuid, bool successful);

    DatabaseGui *database_gui = nullptr;

    std::optional<Project> project;
    NetworkHydraulic network_hydraulic;
    HydraulicNetworkEditor network_editor;

private slots:
    void onDatabaseReady();

signals:
    void signalNetworkLoaded();
    void signalNodeChanged(InfrastructureEntity entity_type, const QUuid &uuid);
    void signalNodeLocateRequested(InfrastructureEntity entity_type, const QUuid &uuid);
    void signalSelectedTank(const HydraulicNodeTank &tank);
    void signalSelectedReservoir(const HydraulicNodeReservoir &reservoir);
    void signalSelectedJunction(const HydraulicNodeJunction &junction);
    void signalSelectedPipe(const HydraulicLinkPipe &pipe);
    void signalSelectedPump(const HydraulicLinkPump &pump);
    void signalSelectedValve(const HydraulicLinkValve &valve);
    void signalSelectedCustomerPoint(const NetworkHydraulicCustomerPoint &customer_point);
};

#endif // HYDRAULIC_DATA_H
