#ifndef HYDRAULIC_NETWORK_EDITOR_H
#define HYDRAULIC_NETWORK_EDITOR_H

#include <optional>

#include <QDate>
#include <QList>
#include <QString>
#include <QUuid>

#include <aowis/model/entity.h>
#include <aowis/model/gis.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

class HydraulicNetworkEditor
{
public:
    explicit HydraulicNetworkEditor(NetworkHydraulic &network);

    bool hasNode(const QUuid &uuid) const;
    std::optional<CoordinateWGS84> nodeCoordinate(const QUuid &uuid) const;

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
    void deleteConnectedLinks(const QUuid &node_uuid);
    QString nextNodeId(const QString &prefix) const;
    QString nextLinkId(const QString &prefix) const;
    double pipeLengthMeters(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                            const QList<HydraulicLinkVertex> &vertices) const;
    void recalculateConnectedPipeLengths(const QUuid &node_uuid);

    NetworkHydraulic &network;
};

#endif // HYDRAULIC_NETWORK_EDITOR_H
