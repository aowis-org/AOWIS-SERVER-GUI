#ifndef HYDRAULIC_DATA_H
#define HYDRAULIC_DATA_H

#include <optional>

#include <QDate>
#include <QDateTime>
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
#include <aowis/epanet/dummy/dummy_marburg_network_generator.h>

#include "_enums_structs.h"
#include "hydraulic_network_editor.h"
#include "network_render_snapshot.h"

struct HydraulicNodeCommonData
{
    QString id;
    QUuid uuid;
    CoordinateWGS84 coordinate_wgs84;
    HydraulicEntityMetadata metadata;
};

struct HydraulicLinkCommonData
{
    QString id;
    QUuid uuid;
    HydraulicEntityMetadata metadata;
};

class HydraulicData : public QObject
{
    Q_OBJECT
public:
    explicit HydraulicData(QObject *parent = nullptr);

    void loadProject();

    const NetworkHydraulic &networkHydraulic() const;
    const NetworkRenderSnapshot &networkRenderSnapshot() const;
    quint64 geometryRevision() const;
    quint64 visualRevision() const;
    QDateTime timeChangedLast() const;
    bool boundingBoxWgs84Valid() const;
    const CoordinateWGS84 &boundingBoxWgs84Minimum() const;
    const CoordinateWGS84 &boundingBoxWgs84Maximum() const;
    std::optional<HydraulicNodeJunction> junction(const QUuid &uuid) const;
    std::optional<HydraulicNodeReservoir> reservoir(const QUuid &uuid) const;
    std::optional<HydraulicNodeTank> tank(const QUuid &uuid) const;
    std::optional<HydraulicLinkPipe> pipe(const QUuid &uuid) const;
    std::optional<HydraulicLinkPump> pump(const QUuid &uuid) const;
    std::optional<HydraulicLinkValve> valve(const QUuid &uuid) const;
    std::optional<HydraulicNodeCommonData> nodeCommonData(InfrastructureEntity entity_type,
                                                           const QUuid &uuid) const;
    std::optional<HydraulicLinkCommonData> linkCommonData(InfrastructureEntity entity_type,
                                                           const QUuid &uuid) const;
    std::optional<InfrastructureEntity> nodeEntityType(const QUuid &uuid) const;

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

    bool setLinkId(const QUuid &uuid, const QString &id);
    bool setLinkModelRole(const QUuid &uuid, EntityModelRole model_role);
    bool setLinkDateAdded(const QUuid &uuid, const std::optional<QDate> &date_added);
    bool setLinkDateInstalled(const QUuid &uuid, const std::optional<QDate> &date_installed);
    bool setLinkEnabled(const QUuid &uuid, bool enabled);

    bool setJunctionElevationInputType(const QUuid &uuid,
                                        HydraulicNodeElevationInputType input_type);
    bool setJunctionElevationM(const QUuid &uuid, double elevation_m);
    bool setJunctionTerrainElevationM(const QUuid &uuid, double terrain_elevation_m);
    bool setJunctionElevationOffsetM(const QUuid &uuid, double elevation_offset_m);
    bool addJunctionDemand(const QUuid &uuid, const HydraulicNodeJunctionDemand &demand);
    bool removeJunctionDemand(const QUuid &uuid, int demand_index);
    bool setJunctionDemandCategoryName(const QUuid &uuid, int demand_index,
                                       const QString &category_name);
    bool setJunctionDemandBaseDemandM3PerH(const QUuid &uuid, int demand_index,
                                           double base_demand_m3_per_h);
    bool setJunctionDemandPatternMode(const QUuid &uuid, int demand_index,
                                      HydraulicTimePatternMode pattern_mode);
    bool setJunctionDemandPatternUuid(const QUuid &uuid, int demand_index,
                                      const QUuid &pattern_uuid);
    bool setJunctionDemandSourceMethod(const QUuid &uuid, int demand_index,
                                       HydraulicNodeJunctionDemandSourceMethod source_method);
    bool setJunctionDemandNote(const QUuid &uuid, int demand_index, const QString &note);
    bool setJunctionEmitterCoefficientM3PerHPerMExponent(
        const QUuid &uuid, double emitter_coefficient_m3_per_h_per_m_exponent);

    bool setReservoirHeadInputType(const QUuid &uuid,
                                   HydraulicNodeElevationInputType input_type);
    bool setReservoirHeadM(const QUuid &uuid, double head_m);
    bool setReservoirTerrainElevationM(const QUuid &uuid, double terrain_elevation_m);
    bool setReservoirHeadOffsetM(const QUuid &uuid, double head_offset_m);
    bool setReservoirHeadPatternMode(const QUuid &uuid, HydraulicTimePatternMode pattern_mode);
    bool setReservoirHeadPatternUuid(const QUuid &uuid, const QUuid &pattern_uuid);

    bool setTankElevationInputType(const QUuid &uuid,
                                   HydraulicNodeTankElevationInputType input_type);
    bool setTankBottomElevationM(const QUuid &uuid, double bottom_elevation_m);
    bool setTankTerrainElevationM(const QUuid &uuid, double terrain_elevation_m);
    bool setTankBottomOffsetM(const QUuid &uuid, double bottom_offset_m);
    bool setTankWaterLevelInitialM(const QUuid &uuid, double water_level_initial_m);
    bool setTankWaterLevelMinimumM(const QUuid &uuid, double water_level_minimum_m);
    bool setTankWaterLevelMaximumM(const QUuid &uuid, double water_level_maximum_m);
    bool setTankGeometryInputType(const QUuid &uuid,
                                  HydraulicNodeTankGeometryInputType input_type);
    bool setTankDiameterM(const QUuid &uuid, double diameter_m);
    bool setTankCrossSectionAreaM2(const QUuid &uuid, double cross_section_area_m2);
    bool setTankVolumeAtMaximumLevelM3(const QUuid &uuid,
                                       double volume_at_maximum_level_m3);
    bool setTankMinimumVolumeM3(const QUuid &uuid, double minimum_volume_m3);
    bool setTankVolumeCurveUuid(const QUuid &uuid, const QUuid &volume_curve_uuid);
    bool setTankCanOverflow(const QUuid &uuid, bool can_overflow);

    bool setPipeInitialStatus(const QUuid &uuid, HydraulicLinkPipeInitialStatus initial_status);
    bool setPipeDiameterMm(const QUuid &uuid, double diameter_mm);
    bool setPipeMeasuredLengthM(const QUuid &uuid, const std::optional<double> &length_measured_m);
    bool setPipeMaterialId(const QUuid &uuid, const QString &material_id);
    bool setPipeRoughnessHw(const QUuid &uuid, double roughness_hw);
    bool setPipeRoughnessDwMm(const QUuid &uuid, double roughness_dw_mm);
    bool setPipeRoughnessCm(const QUuid &uuid, double roughness_cm);
    bool setPipeMinorLoss(const QUuid &uuid, double minor_loss);

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
    enum class NetworkChange
    {
        Visual,
        Geometry
    };

    std::optional<InfrastructureEntity> linkEntityType(const QUuid &uuid) const;
    void markNetworkChanged(NetworkChange change);
    bool emitNodeChangedIfSuccessful(const QUuid &uuid, bool successful, NetworkChange change = NetworkChange::Visual);
    bool emitLinkChangedIfSuccessful(const QUuid &uuid, bool successful, NetworkChange change = NetworkChange::Visual);
    void emitConnectedPipeChanges(const QUuid &node_uuid);
    bool extendBoundingBoxWgs84(const CoordinateWGS84 &coordinate);
    void updateBoundingBoxWgs84(const CoordinateWGS84 &coordinate_previous,
                                const CoordinateWGS84 &coordinate);
    void rebuildBoundingBoxWgs84();
    void rebuildNetworkRenderSnapshot() const;

    DatabaseGui *database_gui = nullptr;

    std::optional<Project> project;
    NetworkHydraulic network_hydraulic;
    HydraulicNetworkEditor network_editor;

    quint64 geometry_revision = 0;
    quint64 visual_revision = 0;
    QDateTime time_changed_last;
    CoordinateWGS84 bounding_box_wgs84_minimum{};
    CoordinateWGS84 bounding_box_wgs84_maximum{};
    bool bounding_box_wgs84_valid = false;

    double node_elevation_m_minimum = 0.0;
    double node_elevation_m_maximum = 0.0;
    double node_base_demand_m3_per_h_minimum = 0.0;
    double node_base_demand_m3_per_h_maximum = 0.0;
    double node_total_demand_m3_per_h_minimum = 0.0;
    double node_total_demand_m3_per_h_maximum = 0.0;
    double node_demand_deficit_m3_per_h_minimum = 0.0;
    double node_demand_deficit_m3_per_h_maximum = 0.0;
    double node_emitter_flow_m3_per_h_minimum = 0.0;
    double node_emitter_flow_m3_per_h_maximum = 0.0;
    double node_leakage_m3_per_h_minimum = 0.0;
    double node_leakage_m3_per_h_maximum = 0.0;
    double node_head_m_minimum = 0.0;
    double node_head_m_maximum = 0.0;
    double node_pressure_m_minimum = 0.0;
    double node_pressure_m_maximum = 0.0;
    double node_chlorine_mg_per_l_minimum = 0.0;
    double node_chlorine_mg_per_l_maximum = 0.0;
    double node_river_water_percent_minimum = 0.0;
    double node_river_water_percent_maximum = 0.0;
    double node_lake_water_percent_minimum = 0.0;
    double node_lake_water_percent_maximum = 0.0;

    double link_diameter_mm_minimum = 0.0;
    double link_diameter_mm_maximum = 0.0;
    double link_length_m_minimum = 0.0;
    double link_length_m_maximum = 0.0;
    double link_roughness_hw_minimum = 0.0;
    double link_roughness_hw_maximum = 0.0;
    double link_flow_rate_m3_per_h_minimum = 0.0;
    double link_flow_rate_m3_per_h_maximum = 0.0;
    double link_velocity_m_per_s_minimum = 0.0;
    double link_velocity_m_per_s_maximum = 0.0;
    double link_head_loss_m_minimum = 0.0;
    double link_head_loss_m_maximum = 0.0;
    double link_leakage_m3_per_h_minimum = 0.0;
    double link_leakage_m3_per_h_maximum = 0.0;
    double link_chlorine_mg_per_l_minimum = 0.0;
    double link_chlorine_mg_per_l_maximum = 0.0;
    double link_river_water_percent_minimum = 0.0;
    double link_river_water_percent_maximum = 0.0;
    double link_lake_water_percent_minimum = 0.0;
    double link_lake_water_percent_maximum = 0.0;

    double heatmap_elevation_m_minimum = 0.0;
    double heatmap_elevation_m_maximum = 0.0;
    double heatmap_total_demand_m3_per_h_minimum = 0.0;
    double heatmap_total_demand_m3_per_h_maximum = 0.0;
    double heatmap_demand_deficit_m3_per_h_minimum = 0.0;
    double heatmap_demand_deficit_m3_per_h_maximum = 0.0;
    double heatmap_leakage_m3_per_h_minimum = 0.0;
    double heatmap_leakage_m3_per_h_maximum = 0.0;
    double heatmap_head_m_minimum = 0.0;
    double heatmap_head_m_maximum = 0.0;
    double heatmap_pressure_m_minimum = 0.0;
    double heatmap_pressure_m_maximum = 0.0;
    double heatmap_chlorine_mg_per_l_minimum = 0.0;
    double heatmap_chlorine_mg_per_l_maximum = 0.0;
    double heatmap_river_water_percent_minimum = 0.0;
    double heatmap_river_water_percent_maximum = 0.0;
    double heatmap_lake_water_percent_minimum = 0.0;
    double heatmap_lake_water_percent_maximum = 0.0;

    mutable NetworkRenderSnapshot network_render_snapshot;

private slots:
    void onDatabaseReady();

signals:
    void signalNetworkLoaded();
    void signalNetworkGeometryChanged(quint64 geometry_revision);
    void signalBoundingBoxWgs84Changed();
    void signalNodeChanged(InfrastructureEntity entity_type, const QUuid &uuid);
    void signalLinkChanged(InfrastructureEntity entity_type, const QUuid &uuid);
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
