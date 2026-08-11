#include "hydraulic_data.h"
#include "network_render_snapshot_builder.h"
#include "network_symbology_values.h"

#include <cmath>

#include <QHash>

namespace
{
bool coordinateWgs84Valid(const CoordinateWGS84 &coordinate)
{
    return std::isfinite(coordinate.latitude_deg) &&
           coordinate.latitude_deg >= -90.0 && coordinate.latitude_deg <= 90.0 &&
           std::isfinite(coordinate.longitude_deg) &&
           coordinate.longitude_deg >= -180.0 && coordinate.longitude_deg <= 180.0;
}

void updateMinimumMaximum(double value, double &minimum, double &maximum, bool &initialized)
{
    if (!std::isfinite(value))
        return;

    if (!initialized)
    {
        minimum = value;
        maximum = value;
        initialized = true;
        return;
    }

    if (value < minimum)
        minimum = value;
    if (value > maximum)
        maximum = value;
}

}

HydraulicData::HydraulicData(QObject *parent)
    : QObject{parent},
      database_gui(new DatabaseGui(this)),
      network_editor(this->network_hydraulic)
{
    connect(this->database_gui, &DatabaseGui::signalReady, this, &HydraulicData::onDatabaseReady);
    
    connect(this->database_gui, &DatabaseGui::signalError, this, [](const QString &message)
    {
        qCritical() << "Could not initialize database:" << message;
    });
    
    DatabaseConfiguration configuration;
    this->database_gui->open(configuration);
}

void HydraulicData::onDatabaseReady()
{
    loadProject();
    
    //this->network_hydraulic = DummyNetworks::networkSimple();
    //this->network_hydraulic = DummyNetworks::networkTanks();
    //this->network_hydraulic = DummyMarburgNetworkGenerator::generate();
    //this->network_hydraulic = RandomHydraulicNetworkGenerator::generateFractal();
    rebuildBoundingBoxWgs84();
    markNetworkChanged(NetworkChange::Geometry);
    //this->network_hydraulic = DummyNetworks::networkOnMap();
    //this->network_hydraulic = DummyNetworks::networkTanksTimeline();
    
    emit signalNetworkLoaded();
}

void HydraulicData::loadProject()
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
             << this->project->uuid
             << this->project->name;
}

const NetworkHydraulic &HydraulicData::networkHydraulic() const
{
    return this->network_hydraulic;
}

const NetworkRenderSnapshot &HydraulicData::networkRenderSnapshot() const
{
    if (this->network_render_snapshot.geometry_revision != this->geometry_revision)
        rebuildNetworkRenderSnapshot();

    this->network_render_snapshot.visual_revision = this->visual_revision;
    return this->network_render_snapshot;
}

quint64 HydraulicData::geometryRevision() const
{
    return this->geometry_revision;
}

quint64 HydraulicData::visualRevision() const
{
    return this->visual_revision;
}

QDateTime HydraulicData::timeChangedLast() const
{
    return this->time_changed_last;
}

bool HydraulicData::boundingBoxWgs84Valid() const
{
    return this->bounding_box_wgs84_valid;
}

const CoordinateWGS84 &HydraulicData::boundingBoxWgs84Minimum() const
{
    return this->bounding_box_wgs84_minimum;
}

const CoordinateWGS84 &HydraulicData::boundingBoxWgs84Maximum() const
{
    return this->bounding_box_wgs84_maximum;
}

void HydraulicData::rebuildSymbologyMinMaxValues()
{
    this->node_elevation_m_minimum = 0.0;
    this->node_elevation_m_maximum = 0.0;
    this->node_base_demand_m3_per_h_minimum = 0.0;
    this->node_base_demand_m3_per_h_maximum = 0.0;
    this->node_total_demand_m3_per_h_minimum = 0.0;
    this->node_total_demand_m3_per_h_maximum = 0.0;
    this->node_demand_deficit_m3_per_h_minimum = 0.0;
    this->node_demand_deficit_m3_per_h_maximum = 0.0;
    this->node_emitter_flow_m3_per_h_minimum = 0.0;
    this->node_emitter_flow_m3_per_h_maximum = 0.0;
    this->node_leakage_m3_per_h_minimum = 0.0;
    this->node_leakage_m3_per_h_maximum = 0.0;
    this->node_head_m_minimum = 0.0;
    this->node_head_m_maximum = 0.0;
    this->node_pressure_m_minimum = 0.0;
    this->node_pressure_m_maximum = 0.0;
    this->node_chlorine_mg_per_l_minimum = 0.0;
    this->node_chlorine_mg_per_l_maximum = 0.0;
    this->node_river_water_percent_minimum = 0.0;
    this->node_river_water_percent_maximum = 0.0;
    this->node_lake_water_percent_minimum = 0.0;
    this->node_lake_water_percent_maximum = 0.0;
    this->link_diameter_mm_minimum = 0.0;
    this->link_diameter_mm_maximum = 0.0;
    this->link_length_m_minimum = 0.0;
    this->link_length_m_maximum = 0.0;
    this->link_roughness_hw_minimum = 0.0;
    this->link_roughness_hw_maximum = 0.0;
    this->link_roughness_dw_mm_minimum = 0.0;
    this->link_roughness_dw_mm_maximum = 0.0;
    this->link_roughness_cm_minimum = 0.0;
    this->link_roughness_cm_maximum = 0.0;
    this->link_flow_rate_m3_per_h_minimum = 0.0;
    this->link_flow_rate_m3_per_h_maximum = 0.0;
    this->link_velocity_m_per_s_minimum = 0.0;
    this->link_velocity_m_per_s_maximum = 0.0;
    this->link_head_loss_m_minimum = 0.0;
    this->link_head_loss_m_maximum = 0.0;
    this->link_leakage_m3_per_h_minimum = 0.0;
    this->link_leakage_m3_per_h_maximum = 0.0;
    this->link_chlorine_mg_per_l_minimum = 0.0;
    this->link_chlorine_mg_per_l_maximum = 0.0;
    this->link_river_water_percent_minimum = 0.0;
    this->link_river_water_percent_maximum = 0.0;
    this->link_lake_water_percent_minimum = 0.0;
    this->link_lake_water_percent_maximum = 0.0;
    this->heatmap_elevation_m_minimum = 0.0;
    this->heatmap_elevation_m_maximum = 0.0;
    this->heatmap_total_demand_m3_per_h_minimum = 0.0;
    this->heatmap_total_demand_m3_per_h_maximum = 0.0;
    this->heatmap_demand_deficit_m3_per_h_minimum = 0.0;
    this->heatmap_demand_deficit_m3_per_h_maximum = 0.0;
    this->heatmap_leakage_m3_per_h_minimum = 0.0;
    this->heatmap_leakage_m3_per_h_maximum = 0.0;
    this->heatmap_head_m_minimum = 0.0;
    this->heatmap_head_m_maximum = 0.0;
    this->heatmap_pressure_m_minimum = 0.0;
    this->heatmap_pressure_m_maximum = 0.0;
    this->heatmap_chlorine_mg_per_l_minimum = 0.0;
    this->heatmap_chlorine_mg_per_l_maximum = 0.0;
    this->heatmap_river_water_percent_minimum = 0.0;
    this->heatmap_river_water_percent_maximum = 0.0;
    this->heatmap_lake_water_percent_minimum = 0.0;
    this->heatmap_lake_water_percent_maximum = 0.0;

    bool node_elevation_m_initialized = false;
    bool node_base_demand_m3_per_h_initialized = false;
    bool link_diameter_mm_initialized = false;
    bool link_length_m_initialized = false;
    bool link_roughness_hw_initialized = false;
    bool link_roughness_dw_mm_initialized = false;
    bool link_roughness_cm_initialized = false;

    for (const HydraulicNodeJunction &junction : this->network_hydraulic.nodes_junctions)
    {
        updateMinimumMaximum(resolvedSymbologyElevationM(junction),
                             this->node_elevation_m_minimum,
                             this->node_elevation_m_maximum,
                             node_elevation_m_initialized);

        double base_demand_m3_per_h = 0.0;
        for (const HydraulicNodeJunctionDemand &demand : junction.demands)
            base_demand_m3_per_h += demand.base_demand_m3_per_h;

        updateMinimumMaximum(base_demand_m3_per_h,
                             this->node_base_demand_m3_per_h_minimum,
                             this->node_base_demand_m3_per_h_maximum,
                             node_base_demand_m3_per_h_initialized);
    }

    for (const HydraulicNodeReservoir &reservoir : this->network_hydraulic.nodes_reservoirs)
    {
        updateMinimumMaximum(resolvedSymbologyElevationM(reservoir),
                             this->node_elevation_m_minimum,
                             this->node_elevation_m_maximum,
                             node_elevation_m_initialized);
    }

    for (const HydraulicNodeTank &tank : this->network_hydraulic.nodes_tanks)
    {
        updateMinimumMaximum(resolvedSymbologyElevationM(tank),
                             this->node_elevation_m_minimum,
                             this->node_elevation_m_maximum,
                             node_elevation_m_initialized);
    }

    for (const HydraulicLinkPipe &pipe : this->network_hydraulic.links_pipes)
    {
        updateMinimumMaximum(pipe.diameter_mm,
                             this->link_diameter_mm_minimum,
                             this->link_diameter_mm_maximum,
                             link_diameter_mm_initialized);
        updateMinimumMaximum(pipe.length_measured_m.value_or(pipe.length_calculated_m),
                             this->link_length_m_minimum,
                             this->link_length_m_maximum,
                             link_length_m_initialized);
        updateMinimumMaximum(pipe.roughness_hw,
                             this->link_roughness_hw_minimum,
                             this->link_roughness_hw_maximum,
                             link_roughness_hw_initialized);
        updateMinimumMaximum(pipe.roughness_dw_mm,
                             this->link_roughness_dw_mm_minimum,
                             this->link_roughness_dw_mm_maximum,
                             link_roughness_dw_mm_initialized);
        updateMinimumMaximum(pipe.roughness_cm,
                             this->link_roughness_cm_minimum,
                             this->link_roughness_cm_maximum,
                             link_roughness_cm_initialized);
    }

    this->heatmap_elevation_m_minimum = this->node_elevation_m_minimum;
    this->heatmap_elevation_m_maximum = this->node_elevation_m_maximum;
    this->heatmap_total_demand_m3_per_h_minimum = this->node_total_demand_m3_per_h_minimum;
    this->heatmap_total_demand_m3_per_h_maximum = this->node_total_demand_m3_per_h_maximum;
    this->heatmap_demand_deficit_m3_per_h_minimum = this->node_demand_deficit_m3_per_h_minimum;
    this->heatmap_demand_deficit_m3_per_h_maximum = this->node_demand_deficit_m3_per_h_maximum;
    this->heatmap_leakage_m3_per_h_minimum = this->node_leakage_m3_per_h_minimum;
    this->heatmap_leakage_m3_per_h_maximum = this->node_leakage_m3_per_h_maximum;
    this->heatmap_head_m_minimum = this->node_head_m_minimum;
    this->heatmap_head_m_maximum = this->node_head_m_maximum;
    this->heatmap_pressure_m_minimum = this->node_pressure_m_minimum;
    this->heatmap_pressure_m_maximum = this->node_pressure_m_maximum;
    this->heatmap_chlorine_mg_per_l_minimum = this->node_chlorine_mg_per_l_minimum;
    this->heatmap_chlorine_mg_per_l_maximum = this->node_chlorine_mg_per_l_maximum;
    this->heatmap_river_water_percent_minimum = this->node_river_water_percent_minimum;
    this->heatmap_river_water_percent_maximum = this->node_river_water_percent_maximum;
    this->heatmap_lake_water_percent_minimum = this->node_lake_water_percent_minimum;
    this->heatmap_lake_water_percent_maximum = this->node_lake_water_percent_maximum;
}

NetworkSymbologyRanges HydraulicData::symbologyRanges(
    const NetworkSymbologySettings &settings) const
{
    NetworkSymbologyRanges ranges;

    switch (settings.visual_node)
    {
    case VisualNode::Elevation:
        ranges.node_minimum = this->node_elevation_m_minimum;
        ranges.node_maximum = this->node_elevation_m_maximum;
        break;
    case VisualNode::BaseDemand:
        ranges.node_minimum = this->node_base_demand_m3_per_h_minimum;
        ranges.node_maximum = this->node_base_demand_m3_per_h_maximum;
        break;
    case VisualNode::TotalDemand:
        ranges.node_minimum = this->node_total_demand_m3_per_h_minimum;
        ranges.node_maximum = this->node_total_demand_m3_per_h_maximum;
        break;
    case VisualNode::DemandDeficit:
        ranges.node_minimum = this->node_demand_deficit_m3_per_h_minimum;
        ranges.node_maximum = this->node_demand_deficit_m3_per_h_maximum;
        break;
    case VisualNode::EmitterFlow:
        ranges.node_minimum = this->node_emitter_flow_m3_per_h_minimum;
        ranges.node_maximum = this->node_emitter_flow_m3_per_h_maximum;
        break;
    case VisualNode::Leakage:
        ranges.node_minimum = this->node_leakage_m3_per_h_minimum;
        ranges.node_maximum = this->node_leakage_m3_per_h_maximum;
        break;
    case VisualNode::Head:
        ranges.node_minimum = this->node_head_m_minimum;
        ranges.node_maximum = this->node_head_m_maximum;
        break;
    case VisualNode::Pressure:
        ranges.node_minimum = this->node_pressure_m_minimum;
        ranges.node_maximum = this->node_pressure_m_maximum;
        break;
    case VisualNode::Chlorine:
        ranges.node_minimum = this->node_chlorine_mg_per_l_minimum;
        ranges.node_maximum = this->node_chlorine_mg_per_l_maximum;
        break;
    case VisualNode::RiverWater:
        ranges.node_minimum = this->node_river_water_percent_minimum;
        ranges.node_maximum = this->node_river_water_percent_maximum;
        break;
    case VisualNode::LakeWater:
        ranges.node_minimum = this->node_lake_water_percent_minimum;
        ranges.node_maximum = this->node_lake_water_percent_maximum;
        break;
    case VisualNode::None:
        break;
    }

    switch (settings.visual_link)
    {
    case VisualLink::Diameter:
        ranges.link_minimum = this->link_diameter_mm_minimum;
        ranges.link_maximum = this->link_diameter_mm_maximum;
        break;
    case VisualLink::Length:
        ranges.link_minimum = this->link_length_m_minimum;
        ranges.link_maximum = this->link_length_m_maximum;
        break;
    case VisualLink::Roughness:
        ranges.link_minimum = this->link_roughness_hw_minimum;
        ranges.link_maximum = this->link_roughness_hw_maximum;
        break;
    case VisualLink::FlowRate:
        ranges.link_minimum = this->link_flow_rate_m3_per_h_minimum;
        ranges.link_maximum = this->link_flow_rate_m3_per_h_maximum;
        break;
    case VisualLink::Velocity:
        ranges.link_minimum = this->link_velocity_m_per_s_minimum;
        ranges.link_maximum = this->link_velocity_m_per_s_maximum;
        break;
    case VisualLink::HeadLoss:
        ranges.link_minimum = this->link_head_loss_m_minimum;
        ranges.link_maximum = this->link_head_loss_m_maximum;
        break;
    case VisualLink::Leakage:
        ranges.link_minimum = this->link_leakage_m3_per_h_minimum;
        ranges.link_maximum = this->link_leakage_m3_per_h_maximum;
        break;
    case VisualLink::Chlorine:
        ranges.link_minimum = this->link_chlorine_mg_per_l_minimum;
        ranges.link_maximum = this->link_chlorine_mg_per_l_maximum;
        break;
    case VisualLink::RiverWater:
        ranges.link_minimum = this->link_river_water_percent_minimum;
        ranges.link_maximum = this->link_river_water_percent_maximum;
        break;
    case VisualLink::LakeWater:
        ranges.link_minimum = this->link_lake_water_percent_minimum;
        ranges.link_maximum = this->link_lake_water_percent_maximum;
        break;
    case VisualLink::None:
        break;
    }

    switch (settings.visual_heatmap)
    {
    case VisualHeatmap::Elevation:
        ranges.heatmap_minimum = this->heatmap_elevation_m_minimum;
        ranges.heatmap_maximum = this->heatmap_elevation_m_maximum;
        break;
    case VisualHeatmap::BaseDemand:
        ranges.heatmap_minimum = this->node_base_demand_m3_per_h_minimum;
        ranges.heatmap_maximum = this->node_base_demand_m3_per_h_maximum;
        break;
    case VisualHeatmap::TotalDemand:
        ranges.heatmap_minimum = this->heatmap_total_demand_m3_per_h_minimum;
        ranges.heatmap_maximum = this->heatmap_total_demand_m3_per_h_maximum;
        break;
    case VisualHeatmap::DemandDeficit:
        ranges.heatmap_minimum = this->heatmap_demand_deficit_m3_per_h_minimum;
        ranges.heatmap_maximum = this->heatmap_demand_deficit_m3_per_h_maximum;
        break;
    case VisualHeatmap::EmitterFlow:
        ranges.heatmap_minimum = this->node_emitter_flow_m3_per_h_minimum;
        ranges.heatmap_maximum = this->node_emitter_flow_m3_per_h_maximum;
        break;
    case VisualHeatmap::Leakage:
        ranges.heatmap_minimum = this->heatmap_leakage_m3_per_h_minimum;
        ranges.heatmap_maximum = this->heatmap_leakage_m3_per_h_maximum;
        break;
    case VisualHeatmap::Head:
        ranges.heatmap_minimum = this->heatmap_head_m_minimum;
        ranges.heatmap_maximum = this->heatmap_head_m_maximum;
        break;
    case VisualHeatmap::Pressure:
        ranges.heatmap_minimum = this->heatmap_pressure_m_minimum;
        ranges.heatmap_maximum = this->heatmap_pressure_m_maximum;
        break;
    case VisualHeatmap::Chlorine:
        ranges.heatmap_minimum = this->heatmap_chlorine_mg_per_l_minimum;
        ranges.heatmap_maximum = this->heatmap_chlorine_mg_per_l_maximum;
        break;
    case VisualHeatmap::RiverWater:
        ranges.heatmap_minimum = this->heatmap_river_water_percent_minimum;
        ranges.heatmap_maximum = this->heatmap_river_water_percent_maximum;
        break;
    case VisualHeatmap::LakeWater:
        ranges.heatmap_minimum = this->heatmap_lake_water_percent_minimum;
        ranges.heatmap_maximum = this->heatmap_lake_water_percent_maximum;
        break;
    case VisualHeatmap::None:
        break;
    }

    return ranges;
}

double HydraulicData::nodeElevationMMinimum() const
{
    return this->node_elevation_m_minimum;
}

void HydraulicData::setNodeElevationMMinimum(double node_elevation_m_minimum)
{
    this->node_elevation_m_minimum = node_elevation_m_minimum;
}

double HydraulicData::nodeElevationMMaximum() const
{
    return this->node_elevation_m_maximum;
}

void HydraulicData::setNodeElevationMMaximum(double node_elevation_m_maximum)
{
    this->node_elevation_m_maximum = node_elevation_m_maximum;
}

double HydraulicData::nodeBaseDemandM3PerHMinimum() const
{
    return this->node_base_demand_m3_per_h_minimum;
}

void HydraulicData::setNodeBaseDemandM3PerHMinimum(double node_base_demand_m3_per_h_minimum)
{
    this->node_base_demand_m3_per_h_minimum = node_base_demand_m3_per_h_minimum;
}

double HydraulicData::nodeBaseDemandM3PerHMaximum() const
{
    return this->node_base_demand_m3_per_h_maximum;
}

void HydraulicData::setNodeBaseDemandM3PerHMaximum(double node_base_demand_m3_per_h_maximum)
{
    this->node_base_demand_m3_per_h_maximum = node_base_demand_m3_per_h_maximum;
}

double HydraulicData::nodeTotalDemandM3PerHMinimum() const
{
    return this->node_total_demand_m3_per_h_minimum;
}

void HydraulicData::setNodeTotalDemandM3PerHMinimum(double node_total_demand_m3_per_h_minimum)
{
    this->node_total_demand_m3_per_h_minimum = node_total_demand_m3_per_h_minimum;
}

double HydraulicData::nodeTotalDemandM3PerHMaximum() const
{
    return this->node_total_demand_m3_per_h_maximum;
}

void HydraulicData::setNodeTotalDemandM3PerHMaximum(double node_total_demand_m3_per_h_maximum)
{
    this->node_total_demand_m3_per_h_maximum = node_total_demand_m3_per_h_maximum;
}

double HydraulicData::nodeDemandDeficitM3PerHMinimum() const
{
    return this->node_demand_deficit_m3_per_h_minimum;
}

void HydraulicData::setNodeDemandDeficitM3PerHMinimum(double node_demand_deficit_m3_per_h_minimum)
{
    this->node_demand_deficit_m3_per_h_minimum = node_demand_deficit_m3_per_h_minimum;
}

double HydraulicData::nodeDemandDeficitM3PerHMaximum() const
{
    return this->node_demand_deficit_m3_per_h_maximum;
}

void HydraulicData::setNodeDemandDeficitM3PerHMaximum(double node_demand_deficit_m3_per_h_maximum)
{
    this->node_demand_deficit_m3_per_h_maximum = node_demand_deficit_m3_per_h_maximum;
}

double HydraulicData::nodeEmitterFlowM3PerHMinimum() const
{
    return this->node_emitter_flow_m3_per_h_minimum;
}

void HydraulicData::setNodeEmitterFlowM3PerHMinimum(double node_emitter_flow_m3_per_h_minimum)
{
    this->node_emitter_flow_m3_per_h_minimum = node_emitter_flow_m3_per_h_minimum;
}

double HydraulicData::nodeEmitterFlowM3PerHMaximum() const
{
    return this->node_emitter_flow_m3_per_h_maximum;
}

void HydraulicData::setNodeEmitterFlowM3PerHMaximum(double node_emitter_flow_m3_per_h_maximum)
{
    this->node_emitter_flow_m3_per_h_maximum = node_emitter_flow_m3_per_h_maximum;
}

double HydraulicData::nodeLeakageM3PerHMinimum() const
{
    return this->node_leakage_m3_per_h_minimum;
}

void HydraulicData::setNodeLeakageM3PerHMinimum(double node_leakage_m3_per_h_minimum)
{
    this->node_leakage_m3_per_h_minimum = node_leakage_m3_per_h_minimum;
}

double HydraulicData::nodeLeakageM3PerHMaximum() const
{
    return this->node_leakage_m3_per_h_maximum;
}

void HydraulicData::setNodeLeakageM3PerHMaximum(double node_leakage_m3_per_h_maximum)
{
    this->node_leakage_m3_per_h_maximum = node_leakage_m3_per_h_maximum;
}

double HydraulicData::nodeHeadMMinimum() const
{
    return this->node_head_m_minimum;
}

void HydraulicData::setNodeHeadMMinimum(double node_head_m_minimum)
{
    this->node_head_m_minimum = node_head_m_minimum;
}

double HydraulicData::nodeHeadMMaximum() const
{
    return this->node_head_m_maximum;
}

void HydraulicData::setNodeHeadMMaximum(double node_head_m_maximum)
{
    this->node_head_m_maximum = node_head_m_maximum;
}

double HydraulicData::nodePressureMMinimum() const
{
    return this->node_pressure_m_minimum;
}

void HydraulicData::setNodePressureMMinimum(double node_pressure_m_minimum)
{
    this->node_pressure_m_minimum = node_pressure_m_minimum;
}

double HydraulicData::nodePressureMMaximum() const
{
    return this->node_pressure_m_maximum;
}

void HydraulicData::setNodePressureMMaximum(double node_pressure_m_maximum)
{
    this->node_pressure_m_maximum = node_pressure_m_maximum;
}

double HydraulicData::nodeChlorineMgPerLMinimum() const
{
    return this->node_chlorine_mg_per_l_minimum;
}

void HydraulicData::setNodeChlorineMgPerLMinimum(double node_chlorine_mg_per_l_minimum)
{
    this->node_chlorine_mg_per_l_minimum = node_chlorine_mg_per_l_minimum;
}

double HydraulicData::nodeChlorineMgPerLMaximum() const
{
    return this->node_chlorine_mg_per_l_maximum;
}

void HydraulicData::setNodeChlorineMgPerLMaximum(double node_chlorine_mg_per_l_maximum)
{
    this->node_chlorine_mg_per_l_maximum = node_chlorine_mg_per_l_maximum;
}

double HydraulicData::nodeRiverWaterPercentMinimum() const
{
    return this->node_river_water_percent_minimum;
}

void HydraulicData::setNodeRiverWaterPercentMinimum(double node_river_water_percent_minimum)
{
    this->node_river_water_percent_minimum = node_river_water_percent_minimum;
}

double HydraulicData::nodeRiverWaterPercentMaximum() const
{
    return this->node_river_water_percent_maximum;
}

void HydraulicData::setNodeRiverWaterPercentMaximum(double node_river_water_percent_maximum)
{
    this->node_river_water_percent_maximum = node_river_water_percent_maximum;
}

double HydraulicData::nodeLakeWaterPercentMinimum() const
{
    return this->node_lake_water_percent_minimum;
}

void HydraulicData::setNodeLakeWaterPercentMinimum(double node_lake_water_percent_minimum)
{
    this->node_lake_water_percent_minimum = node_lake_water_percent_minimum;
}

double HydraulicData::nodeLakeWaterPercentMaximum() const
{
    return this->node_lake_water_percent_maximum;
}

void HydraulicData::setNodeLakeWaterPercentMaximum(double node_lake_water_percent_maximum)
{
    this->node_lake_water_percent_maximum = node_lake_water_percent_maximum;
}

double HydraulicData::linkDiameterMmMinimum() const
{
    return this->link_diameter_mm_minimum;
}

void HydraulicData::setLinkDiameterMmMinimum(double link_diameter_mm_minimum)
{
    this->link_diameter_mm_minimum = link_diameter_mm_minimum;
}

double HydraulicData::linkDiameterMmMaximum() const
{
    return this->link_diameter_mm_maximum;
}

void HydraulicData::setLinkDiameterMmMaximum(double link_diameter_mm_maximum)
{
    this->link_diameter_mm_maximum = link_diameter_mm_maximum;
}

double HydraulicData::linkLengthMMinimum() const
{
    return this->link_length_m_minimum;
}

void HydraulicData::setLinkLengthMMinimum(double link_length_m_minimum)
{
    this->link_length_m_minimum = link_length_m_minimum;
}

double HydraulicData::linkLengthMMaximum() const
{
    return this->link_length_m_maximum;
}

void HydraulicData::setLinkLengthMMaximum(double link_length_m_maximum)
{
    this->link_length_m_maximum = link_length_m_maximum;
}

double HydraulicData::linkRoughnessHwMinimum() const
{
    return this->link_roughness_hw_minimum;
}

void HydraulicData::setLinkRoughnessHwMinimum(double link_roughness_hw_minimum)
{
    this->link_roughness_hw_minimum = link_roughness_hw_minimum;
}

double HydraulicData::linkRoughnessHwMaximum() const
{
    return this->link_roughness_hw_maximum;
}

void HydraulicData::setLinkRoughnessHwMaximum(double link_roughness_hw_maximum)
{
    this->link_roughness_hw_maximum = link_roughness_hw_maximum;
}

double HydraulicData::linkRoughnessDwMmMinimum() const
{
    return this->link_roughness_dw_mm_minimum;
}

void HydraulicData::setLinkRoughnessDwMmMinimum(double link_roughness_dw_mm_minimum)
{
    this->link_roughness_dw_mm_minimum = link_roughness_dw_mm_minimum;
}

double HydraulicData::linkRoughnessDwMmMaximum() const
{
    return this->link_roughness_dw_mm_maximum;
}

void HydraulicData::setLinkRoughnessDwMmMaximum(double link_roughness_dw_mm_maximum)
{
    this->link_roughness_dw_mm_maximum = link_roughness_dw_mm_maximum;
}

double HydraulicData::linkRoughnessCmMinimum() const
{
    return this->link_roughness_cm_minimum;
}

void HydraulicData::setLinkRoughnessCmMinimum(double link_roughness_cm_minimum)
{
    this->link_roughness_cm_minimum = link_roughness_cm_minimum;
}

double HydraulicData::linkRoughnessCmMaximum() const
{
    return this->link_roughness_cm_maximum;
}

void HydraulicData::setLinkRoughnessCmMaximum(double link_roughness_cm_maximum)
{
    this->link_roughness_cm_maximum = link_roughness_cm_maximum;
}

double HydraulicData::linkFlowRateM3PerHMinimum() const
{
    return this->link_flow_rate_m3_per_h_minimum;
}

void HydraulicData::setLinkFlowRateM3PerHMinimum(double link_flow_rate_m3_per_h_minimum)
{
    this->link_flow_rate_m3_per_h_minimum = link_flow_rate_m3_per_h_minimum;
}

double HydraulicData::linkFlowRateM3PerHMaximum() const
{
    return this->link_flow_rate_m3_per_h_maximum;
}

void HydraulicData::setLinkFlowRateM3PerHMaximum(double link_flow_rate_m3_per_h_maximum)
{
    this->link_flow_rate_m3_per_h_maximum = link_flow_rate_m3_per_h_maximum;
}

double HydraulicData::linkVelocityMPerSMinimum() const
{
    return this->link_velocity_m_per_s_minimum;
}

void HydraulicData::setLinkVelocityMPerSMinimum(double link_velocity_m_per_s_minimum)
{
    this->link_velocity_m_per_s_minimum = link_velocity_m_per_s_minimum;
}

double HydraulicData::linkVelocityMPerSMaximum() const
{
    return this->link_velocity_m_per_s_maximum;
}

void HydraulicData::setLinkVelocityMPerSMaximum(double link_velocity_m_per_s_maximum)
{
    this->link_velocity_m_per_s_maximum = link_velocity_m_per_s_maximum;
}

double HydraulicData::linkHeadLossMMinimum() const
{
    return this->link_head_loss_m_minimum;
}

void HydraulicData::setLinkHeadLossMMinimum(double link_head_loss_m_minimum)
{
    this->link_head_loss_m_minimum = link_head_loss_m_minimum;
}

double HydraulicData::linkHeadLossMMaximum() const
{
    return this->link_head_loss_m_maximum;
}

void HydraulicData::setLinkHeadLossMMaximum(double link_head_loss_m_maximum)
{
    this->link_head_loss_m_maximum = link_head_loss_m_maximum;
}

double HydraulicData::linkLeakageM3PerHMinimum() const
{
    return this->link_leakage_m3_per_h_minimum;
}

void HydraulicData::setLinkLeakageM3PerHMinimum(double link_leakage_m3_per_h_minimum)
{
    this->link_leakage_m3_per_h_minimum = link_leakage_m3_per_h_minimum;
}

double HydraulicData::linkLeakageM3PerHMaximum() const
{
    return this->link_leakage_m3_per_h_maximum;
}

void HydraulicData::setLinkLeakageM3PerHMaximum(double link_leakage_m3_per_h_maximum)
{
    this->link_leakage_m3_per_h_maximum = link_leakage_m3_per_h_maximum;
}

double HydraulicData::linkChlorineMgPerLMinimum() const
{
    return this->link_chlorine_mg_per_l_minimum;
}

void HydraulicData::setLinkChlorineMgPerLMinimum(double link_chlorine_mg_per_l_minimum)
{
    this->link_chlorine_mg_per_l_minimum = link_chlorine_mg_per_l_minimum;
}

double HydraulicData::linkChlorineMgPerLMaximum() const
{
    return this->link_chlorine_mg_per_l_maximum;
}

void HydraulicData::setLinkChlorineMgPerLMaximum(double link_chlorine_mg_per_l_maximum)
{
    this->link_chlorine_mg_per_l_maximum = link_chlorine_mg_per_l_maximum;
}

double HydraulicData::linkRiverWaterPercentMinimum() const
{
    return this->link_river_water_percent_minimum;
}

void HydraulicData::setLinkRiverWaterPercentMinimum(double link_river_water_percent_minimum)
{
    this->link_river_water_percent_minimum = link_river_water_percent_minimum;
}

double HydraulicData::linkRiverWaterPercentMaximum() const
{
    return this->link_river_water_percent_maximum;
}

void HydraulicData::setLinkRiverWaterPercentMaximum(double link_river_water_percent_maximum)
{
    this->link_river_water_percent_maximum = link_river_water_percent_maximum;
}

double HydraulicData::linkLakeWaterPercentMinimum() const
{
    return this->link_lake_water_percent_minimum;
}

void HydraulicData::setLinkLakeWaterPercentMinimum(double link_lake_water_percent_minimum)
{
    this->link_lake_water_percent_minimum = link_lake_water_percent_minimum;
}

double HydraulicData::linkLakeWaterPercentMaximum() const
{
    return this->link_lake_water_percent_maximum;
}

void HydraulicData::setLinkLakeWaterPercentMaximum(double link_lake_water_percent_maximum)
{
    this->link_lake_water_percent_maximum = link_lake_water_percent_maximum;
}

double HydraulicData::heatmapElevationMMinimum() const
{
    return this->heatmap_elevation_m_minimum;
}

void HydraulicData::setHeatmapElevationMMinimum(double heatmap_elevation_m_minimum)
{
    this->heatmap_elevation_m_minimum = heatmap_elevation_m_minimum;
}

double HydraulicData::heatmapElevationMMaximum() const
{
    return this->heatmap_elevation_m_maximum;
}

void HydraulicData::setHeatmapElevationMMaximum(double heatmap_elevation_m_maximum)
{
    this->heatmap_elevation_m_maximum = heatmap_elevation_m_maximum;
}

double HydraulicData::heatmapTotalDemandM3PerHMinimum() const
{
    return this->heatmap_total_demand_m3_per_h_minimum;
}

void HydraulicData::setHeatmapTotalDemandM3PerHMinimum(double heatmap_total_demand_m3_per_h_minimum)
{
    this->heatmap_total_demand_m3_per_h_minimum = heatmap_total_demand_m3_per_h_minimum;
}

double HydraulicData::heatmapTotalDemandM3PerHMaximum() const
{
    return this->heatmap_total_demand_m3_per_h_maximum;
}

void HydraulicData::setHeatmapTotalDemandM3PerHMaximum(double heatmap_total_demand_m3_per_h_maximum)
{
    this->heatmap_total_demand_m3_per_h_maximum = heatmap_total_demand_m3_per_h_maximum;
}

double HydraulicData::heatmapDemandDeficitM3PerHMinimum() const
{
    return this->heatmap_demand_deficit_m3_per_h_minimum;
}

void HydraulicData::setHeatmapDemandDeficitM3PerHMinimum(double heatmap_demand_deficit_m3_per_h_minimum)
{
    this->heatmap_demand_deficit_m3_per_h_minimum = heatmap_demand_deficit_m3_per_h_minimum;
}

double HydraulicData::heatmapDemandDeficitM3PerHMaximum() const
{
    return this->heatmap_demand_deficit_m3_per_h_maximum;
}

void HydraulicData::setHeatmapDemandDeficitM3PerHMaximum(double heatmap_demand_deficit_m3_per_h_maximum)
{
    this->heatmap_demand_deficit_m3_per_h_maximum = heatmap_demand_deficit_m3_per_h_maximum;
}

double HydraulicData::heatmapLeakageM3PerHMinimum() const
{
    return this->heatmap_leakage_m3_per_h_minimum;
}

void HydraulicData::setHeatmapLeakageM3PerHMinimum(double heatmap_leakage_m3_per_h_minimum)
{
    this->heatmap_leakage_m3_per_h_minimum = heatmap_leakage_m3_per_h_minimum;
}

double HydraulicData::heatmapLeakageM3PerHMaximum() const
{
    return this->heatmap_leakage_m3_per_h_maximum;
}

void HydraulicData::setHeatmapLeakageM3PerHMaximum(double heatmap_leakage_m3_per_h_maximum)
{
    this->heatmap_leakage_m3_per_h_maximum = heatmap_leakage_m3_per_h_maximum;
}

double HydraulicData::heatmapHeadMMinimum() const
{
    return this->heatmap_head_m_minimum;
}

void HydraulicData::setHeatmapHeadMMinimum(double heatmap_head_m_minimum)
{
    this->heatmap_head_m_minimum = heatmap_head_m_minimum;
}

double HydraulicData::heatmapHeadMMaximum() const
{
    return this->heatmap_head_m_maximum;
}

void HydraulicData::setHeatmapHeadMMaximum(double heatmap_head_m_maximum)
{
    this->heatmap_head_m_maximum = heatmap_head_m_maximum;
}

double HydraulicData::heatmapPressureMMinimum() const
{
    return this->heatmap_pressure_m_minimum;
}

void HydraulicData::setHeatmapPressureMMinimum(double heatmap_pressure_m_minimum)
{
    this->heatmap_pressure_m_minimum = heatmap_pressure_m_minimum;
}

double HydraulicData::heatmapPressureMMaximum() const
{
    return this->heatmap_pressure_m_maximum;
}

void HydraulicData::setHeatmapPressureMMaximum(double heatmap_pressure_m_maximum)
{
    this->heatmap_pressure_m_maximum = heatmap_pressure_m_maximum;
}

double HydraulicData::heatmapChlorineMgPerLMinimum() const
{
    return this->heatmap_chlorine_mg_per_l_minimum;
}

void HydraulicData::setHeatmapChlorineMgPerLMinimum(double heatmap_chlorine_mg_per_l_minimum)
{
    this->heatmap_chlorine_mg_per_l_minimum = heatmap_chlorine_mg_per_l_minimum;
}

double HydraulicData::heatmapChlorineMgPerLMaximum() const
{
    return this->heatmap_chlorine_mg_per_l_maximum;
}

void HydraulicData::setHeatmapChlorineMgPerLMaximum(double heatmap_chlorine_mg_per_l_maximum)
{
    this->heatmap_chlorine_mg_per_l_maximum = heatmap_chlorine_mg_per_l_maximum;
}

double HydraulicData::heatmapRiverWaterPercentMinimum() const
{
    return this->heatmap_river_water_percent_minimum;
}

void HydraulicData::setHeatmapRiverWaterPercentMinimum(double heatmap_river_water_percent_minimum)
{
    this->heatmap_river_water_percent_minimum = heatmap_river_water_percent_minimum;
}

double HydraulicData::heatmapRiverWaterPercentMaximum() const
{
    return this->heatmap_river_water_percent_maximum;
}

void HydraulicData::setHeatmapRiverWaterPercentMaximum(double heatmap_river_water_percent_maximum)
{
    this->heatmap_river_water_percent_maximum = heatmap_river_water_percent_maximum;
}

double HydraulicData::heatmapLakeWaterPercentMinimum() const
{
    return this->heatmap_lake_water_percent_minimum;
}

void HydraulicData::setHeatmapLakeWaterPercentMinimum(double heatmap_lake_water_percent_minimum)
{
    this->heatmap_lake_water_percent_minimum = heatmap_lake_water_percent_minimum;
}

double HydraulicData::heatmapLakeWaterPercentMaximum() const
{
    return this->heatmap_lake_water_percent_maximum;
}

void HydraulicData::setHeatmapLakeWaterPercentMaximum(double heatmap_lake_water_percent_maximum)
{
    this->heatmap_lake_water_percent_maximum = heatmap_lake_water_percent_maximum;
}

std::optional<HydraulicNodeJunction> HydraulicData::junction(const QUuid &uuid) const
{
    return this->network_editor.junction(uuid);
}

std::optional<HydraulicNodeReservoir> HydraulicData::reservoir(const QUuid &uuid) const
{
    return this->network_editor.reservoir(uuid);
}

std::optional<HydraulicNodeTank> HydraulicData::tank(const QUuid &uuid) const
{
    return this->network_editor.tank(uuid);
}

std::optional<HydraulicLinkPipe> HydraulicData::pipe(const QUuid &uuid) const
{
    return this->network_editor.pipe(uuid);
}

std::optional<HydraulicLinkPump> HydraulicData::pump(const QUuid &uuid) const
{
    return this->network_editor.pump(uuid);
}

std::optional<HydraulicLinkValve> HydraulicData::valve(const QUuid &uuid) const
{
    return this->network_editor.valve(uuid);
}

std::optional<HydraulicNodeCommonData> HydraulicData::nodeCommonData(
    InfrastructureEntity entity_type, const QUuid &uuid) const
{
    switch (entity_type)
    {
    case InfrastructureEntity::Junction:
    {
        const std::optional<HydraulicNodeJunction> node = junction(uuid);
        if (!node.has_value())
            return std::nullopt;

        return HydraulicNodeCommonData{node->id, node->uuid, node->coordinate_wgs84, node->metadata};
    }
    case InfrastructureEntity::Reservoir:
    {
        const std::optional<HydraulicNodeReservoir> node = reservoir(uuid);
        if (!node.has_value())
            return std::nullopt;

        return HydraulicNodeCommonData{node->id, node->uuid, node->coordinate_wgs84, node->metadata};
    }
    case InfrastructureEntity::Tank:
    {
        const std::optional<HydraulicNodeTank> node = tank(uuid);
        if (!node.has_value())
            return std::nullopt;

        return HydraulicNodeCommonData{node->id, node->uuid, node->coordinate_wgs84, node->metadata};
    }
    default:
        return std::nullopt;
    }
}

std::optional<HydraulicLinkCommonData> HydraulicData::linkCommonData(
    InfrastructureEntity entity_type, const QUuid &uuid) const
{
    switch (entity_type)
    {
    case InfrastructureEntity::Pipe:
    {
        const std::optional<HydraulicLinkPipe> link = pipe(uuid);
        if (!link.has_value())
            return std::nullopt;

        return HydraulicLinkCommonData{link->id, link->uuid, link->metadata};
    }
    case InfrastructureEntity::Pump:
    {
        const std::optional<HydraulicLinkPump> link = pump(uuid);
        if (!link.has_value())
            return std::nullopt;

        return HydraulicLinkCommonData{link->id, link->uuid, link->metadata};
    }
    case InfrastructureEntity::Valve:
    {
        const std::optional<HydraulicLinkValve> link = valve(uuid);
        if (!link.has_value())
            return std::nullopt;

        return HydraulicLinkCommonData{link->id, link->uuid, link->metadata};
    }
    default:
        return std::nullopt;
    }
}

std::optional<InfrastructureEntity> HydraulicData::nodeEntityType(const QUuid &uuid) const
{
    if (uuid.isNull())
        return std::nullopt;

    for (const HydraulicNodeJunction &junction : this->network_hydraulic.nodes_junctions)
    {
        if (junction.uuid == uuid)
            return InfrastructureEntity::Junction;
    }

    for (const HydraulicNodeReservoir &reservoir : this->network_hydraulic.nodes_reservoirs)
    {
        if (reservoir.uuid == uuid)
            return InfrastructureEntity::Reservoir;
    }

    for (const HydraulicNodeTank &tank : this->network_hydraulic.nodes_tanks)
    {
        if (tank.uuid == uuid)
            return InfrastructureEntity::Tank;
    }

    return std::nullopt;
}

std::optional<InfrastructureEntity> HydraulicData::linkEntityType(const QUuid &uuid) const
{
    if (uuid.isNull())
        return std::nullopt;

    for (const HydraulicLinkPipe &pipe : this->network_hydraulic.links_pipes)
    {
        if (pipe.uuid == uuid)
            return InfrastructureEntity::Pipe;
    }

    for (const HydraulicLinkPump &pump : this->network_hydraulic.links_pumps)
    {
        if (pump.uuid == uuid)
            return InfrastructureEntity::Pump;
    }

    for (const HydraulicLinkValve &valve : this->network_hydraulic.links_valves)
    {
        if (valve.uuid == uuid)
            return InfrastructureEntity::Valve;
    }

    return std::nullopt;
}

void HydraulicData::markNetworkChanged(NetworkChange change)
{
    rebuildSymbologyMinMaxValues();

    if (change == NetworkChange::Geometry)
        ++this->geometry_revision;

    ++this->visual_revision;
    this->time_changed_last = QDateTime::currentDateTimeUtc();

    if (change == NetworkChange::Geometry)
        emit signalNetworkGeometryChanged(this->geometry_revision);
}

bool HydraulicData::emitNodeChangedIfSuccessful(const QUuid &uuid, bool successful, NetworkChange change)
{
    if (!successful)
        return false;

    markNetworkChanged(change);

    const std::optional<InfrastructureEntity> entity_type = nodeEntityType(uuid);
    if (!entity_type.has_value())
        return false;
    emit signalNodeChanged(entity_type.value(), uuid);
    return true;
}

bool HydraulicData::emitLinkChangedIfSuccessful(const QUuid &uuid, bool successful, NetworkChange change)
{
    if (!successful)
        return false;

    markNetworkChanged(change);

    const std::optional<InfrastructureEntity> entity_type = linkEntityType(uuid);
    if (!entity_type.has_value())
        return false;
    emit signalLinkChanged(entity_type.value(), uuid);
    return true;
}

void HydraulicData::emitConnectedPipeChanges(const QUuid &node_uuid)
{
    for (const HydraulicLinkPipe &pipe : this->network_hydraulic.links_pipes)
    {
        if (pipe.node_uuid_from == node_uuid || pipe.node_uuid_to == node_uuid)
            emit signalLinkChanged(InfrastructureEntity::Pipe, pipe.uuid);
    }
}

bool HydraulicData::extendBoundingBoxWgs84(const CoordinateWGS84 &coordinate)
{
    if (!coordinateWgs84Valid(coordinate))
        return false;

    if (!this->bounding_box_wgs84_valid)
    {
        this->bounding_box_wgs84_minimum = coordinate;
        this->bounding_box_wgs84_maximum = coordinate;
        this->bounding_box_wgs84_valid = true;
        return true;
    }

    bool changed = false;

    if (coordinate.latitude_deg < this->bounding_box_wgs84_minimum.latitude_deg)
    {
        this->bounding_box_wgs84_minimum.latitude_deg = coordinate.latitude_deg;
        changed = true;
    }
    if (coordinate.longitude_deg < this->bounding_box_wgs84_minimum.longitude_deg)
    {
        this->bounding_box_wgs84_minimum.longitude_deg = coordinate.longitude_deg;
        changed = true;
    }
    if (coordinate.latitude_deg > this->bounding_box_wgs84_maximum.latitude_deg)
    {
        this->bounding_box_wgs84_maximum.latitude_deg = coordinate.latitude_deg;
        changed = true;
    }
    if (coordinate.longitude_deg > this->bounding_box_wgs84_maximum.longitude_deg)
    {
        this->bounding_box_wgs84_maximum.longitude_deg = coordinate.longitude_deg;
        changed = true;
    }

    return changed;
}

void HydraulicData::updateBoundingBoxWgs84(const CoordinateWGS84 &coordinate_previous,
                                           const CoordinateWGS84 &coordinate)
{
    if (!this->bounding_box_wgs84_valid)
    {
        if (extendBoundingBoxWgs84(coordinate))
            emit signalBoundingBoxWgs84Changed();
        return;
    }

    const bool coordinate_previous_valid = coordinateWgs84Valid(coordinate_previous);
    const bool coordinate_valid = coordinateWgs84Valid(coordinate);
    bool rebuild_required = false;

    if (coordinate_previous_valid)
    {
        const bool latitude_is_minimum =
            coordinate_previous.latitude_deg == this->bounding_box_wgs84_minimum.latitude_deg;
        const bool longitude_is_minimum =
            coordinate_previous.longitude_deg == this->bounding_box_wgs84_minimum.longitude_deg;
        const bool latitude_is_maximum =
            coordinate_previous.latitude_deg == this->bounding_box_wgs84_maximum.latitude_deg;
        const bool longitude_is_maximum =
            coordinate_previous.longitude_deg == this->bounding_box_wgs84_maximum.longitude_deg;

        if (!coordinate_valid)
        {
            rebuild_required = latitude_is_minimum || longitude_is_minimum ||
                               latitude_is_maximum || longitude_is_maximum;
        }
        else
        {
            rebuild_required =
                (latitude_is_minimum && coordinate.latitude_deg > coordinate_previous.latitude_deg) ||
                (longitude_is_minimum && coordinate.longitude_deg > coordinate_previous.longitude_deg) ||
                (latitude_is_maximum && coordinate.latitude_deg < coordinate_previous.latitude_deg) ||
                (longitude_is_maximum && coordinate.longitude_deg < coordinate_previous.longitude_deg);
        }
    }

    if (rebuild_required)
    {
        rebuildBoundingBoxWgs84();
        return;
    }

    if (extendBoundingBoxWgs84(coordinate))
        emit signalBoundingBoxWgs84Changed();
}

void HydraulicData::rebuildBoundingBoxWgs84()
{
    this->bounding_box_wgs84_minimum = CoordinateWGS84{};
    this->bounding_box_wgs84_maximum = CoordinateWGS84{};
    this->bounding_box_wgs84_valid = false;

    for (const HydraulicNodeJunction &junction : this->network_hydraulic.nodes_junctions)
        extendBoundingBoxWgs84(junction.coordinate_wgs84);
    for (const HydraulicNodeReservoir &reservoir : this->network_hydraulic.nodes_reservoirs)
        extendBoundingBoxWgs84(reservoir.coordinate_wgs84);
    for (const HydraulicNodeTank &tank : this->network_hydraulic.nodes_tanks)
        extendBoundingBoxWgs84(tank.coordinate_wgs84);

    for (const HydraulicLinkPipe &pipe : this->network_hydraulic.links_pipes)
    {
        for (const HydraulicLinkVertex &vertex : pipe.vertices)
            extendBoundingBoxWgs84(vertex.coordinate_wgs84);
    }
    for (const HydraulicLinkPump &pump : this->network_hydraulic.links_pumps)
    {
        for (const HydraulicLinkVertex &vertex : pump.vertices)
            extendBoundingBoxWgs84(vertex.coordinate_wgs84);
    }
    for (const HydraulicLinkValve &valve : this->network_hydraulic.links_valves)
    {
        for (const HydraulicLinkVertex &vertex : valve.vertices)
            extendBoundingBoxWgs84(vertex.coordinate_wgs84);
    }

    emit signalBoundingBoxWgs84Changed();
}

void HydraulicData::rebuildNetworkRenderSnapshot() const
{
    this->network_render_snapshot = buildNetworkRenderSnapshot(
        this->network_hydraulic, this->geometry_revision, this->visual_revision);
}

void HydraulicData::setSelectedUuid(InfrastructureEntity entity_type, const QUuid &uuid)
{
    switch (entity_type)
    {
    case InfrastructureEntity::Tank:
        for (const HydraulicNodeTank &tank : this->network_hydraulic.nodes_tanks)
        {
            if (tank.uuid == uuid)
            {
                emit signalSelectedTank(tank);
                return;
            }
        }
        
        break;
        
    case InfrastructureEntity::Reservoir:
        for (const HydraulicNodeReservoir &reservoir : this->network_hydraulic.nodes_reservoirs)
        {
            if (reservoir.uuid == uuid)
            {
                emit signalSelectedReservoir(reservoir);
                return;
            }
        }
        
        break;
        
    case InfrastructureEntity::Junction:
        for (const HydraulicNodeJunction &junction : this->network_hydraulic.nodes_junctions)
        {
            if (junction.uuid == uuid)
            {
                emit signalSelectedJunction(junction);
                return;
            }
        }
        
        break;
        
    case InfrastructureEntity::Pipe:
        for (const HydraulicLinkPipe &pipe : this->network_hydraulic.links_pipes)
        {
            if (pipe.uuid == uuid)
            {
                emit signalSelectedPipe(pipe);
                return;
            }
        }
        
        break;
        
    case InfrastructureEntity::Pump:
        for (const HydraulicLinkPump &pump : this->network_hydraulic.links_pumps)
        {
            if (pump.uuid == uuid)
            {
                emit signalSelectedPump(pump);
                return;
            }
        }
        
        break;
    
    case InfrastructureEntity::Valve:
        for (const HydraulicLinkValve &valve : this->network_hydraulic.links_valves)
        {
            if (valve.uuid == uuid)
            {
                emit signalSelectedValve(valve);
                return;
            }
        }
        
        break;
    
    case InfrastructureEntity::CustomerPoint:
        for (const NetworkHydraulicCustomerPoint &customer_point : this->network_hydraulic.customer_points)
        {
            if (customer_point.uuid == uuid)
            {
                emit signalSelectedCustomerPoint(customer_point);
                return;
            }
        }
        
    default:
        break;
    }
}

void HydraulicData::requestNodeLocate(InfrastructureEntity entity_type, const QUuid &uuid)
{
    if (!nodeCommonData(entity_type, uuid).has_value())
        return;

    emit signalNodeLocateRequested(entity_type, uuid);
}

QUuid HydraulicData::addJunction(const CoordinateWGS84 &coordinate)
{
    const QUuid uuid = this->network_editor.addJunction(coordinate);
    if (!uuid.isNull())
    {
        if (extendBoundingBoxWgs84(coordinate))
            emit signalBoundingBoxWgs84Changed();
        markNetworkChanged(NetworkChange::Geometry);
    }
    return uuid;
}

QUuid HydraulicData::addReservoir(const CoordinateWGS84 &coordinate)
{
    const QUuid uuid = this->network_editor.addReservoir(coordinate);
    if (!uuid.isNull())
    {
        if (extendBoundingBoxWgs84(coordinate))
            emit signalBoundingBoxWgs84Changed();
        markNetworkChanged(NetworkChange::Geometry);
    }
    return uuid;
}

QUuid HydraulicData::addTank(const CoordinateWGS84 &coordinate)
{
    const QUuid uuid = this->network_editor.addTank(coordinate);
    if (!uuid.isNull())
    {
        if (extendBoundingBoxWgs84(coordinate))
            emit signalBoundingBoxWgs84Changed();
        markNetworkChanged(NetworkChange::Geometry);
    }
    return uuid;
}

QUuid HydraulicData::addPipe(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                             const QList<CoordinateWGS84> &intermediate_vertices)
{
    const QUuid uuid =
        this->network_editor.addPipe(node_uuid_from, node_uuid_to, intermediate_vertices);
    if (!uuid.isNull())
    {
        bool bounding_box_changed = false;
        for (const CoordinateWGS84 &coordinate : intermediate_vertices)
            bounding_box_changed = extendBoundingBoxWgs84(coordinate) || bounding_box_changed;
        if (bounding_box_changed)
            emit signalBoundingBoxWgs84Changed();
        markNetworkChanged(NetworkChange::Geometry);
    }
    return uuid;
}

QUuid HydraulicData::addPump(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                             const CoordinateWGS84 &center_coordinate)
{
    const QUuid uuid =
        this->network_editor.addPump(node_uuid_from, node_uuid_to, center_coordinate);
    if (!uuid.isNull())
    {
        if (extendBoundingBoxWgs84(center_coordinate))
            emit signalBoundingBoxWgs84Changed();
        markNetworkChanged(NetworkChange::Geometry);
    }
    return uuid;
}

QUuid HydraulicData::addValve(const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                              const CoordinateWGS84 &center_coordinate)
{
    const QUuid uuid =
        this->network_editor.addValve(node_uuid_from, node_uuid_to, center_coordinate);
    if (!uuid.isNull())
    {
        if (extendBoundingBoxWgs84(center_coordinate))
            emit signalBoundingBoxWgs84Changed();
        markNetworkChanged(NetworkChange::Geometry);
    }
    return uuid;
}

bool HydraulicData::setNodeId(const QUuid &uuid, const QString &id)
{
    return emitNodeChangedIfSuccessful(uuid, this->network_editor.setNodeId(uuid, id));
}

bool HydraulicData::setNodeModelRole(const QUuid &uuid, EntityModelRole model_role)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setNodeModelRole(uuid, model_role));
}

bool HydraulicData::setNodeDateAdded(const QUuid &uuid, const std::optional<QDate> &date_added)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setNodeDateAdded(uuid, date_added));
}

bool HydraulicData::setNodeDateInstalled(const QUuid &uuid,
                                           const std::optional<QDate> &date_installed)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setNodeDateInstalled(uuid, date_installed));
}

bool HydraulicData::setNodeEnabled(const QUuid &uuid, bool enabled)
{
    return emitNodeChangedIfSuccessful(uuid, this->network_editor.setNodeEnabled(uuid, enabled));
}

bool HydraulicData::setNodeCoordinate(const QUuid &uuid, const CoordinateWGS84 &coordinate)
{
    const std::optional<InfrastructureEntity> entity_type = nodeEntityType(uuid);
    if (!entity_type.has_value())
        return false;
    const std::optional<CoordinateWGS84> coordinate_previous =
        this->network_editor.nodeCoordinate(uuid);
    if (!coordinate_previous.has_value())
        return false;
    if (!this->network_editor.setNodeCoordinate(uuid, coordinate))
        return false;

    updateBoundingBoxWgs84(coordinate_previous.value(), coordinate);
    markNetworkChanged(NetworkChange::Geometry);
    emit signalNodeChanged(entity_type.value(), uuid);
    emitConnectedPipeChanges(uuid);
    return true;
}

bool HydraulicData::setLinkId(const QUuid &uuid, const QString &id)
{
    return emitLinkChangedIfSuccessful(uuid, this->network_editor.setLinkId(uuid, id));
}

bool HydraulicData::setLinkModelRole(const QUuid &uuid, EntityModelRole model_role)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setLinkModelRole(uuid, model_role));
}

bool HydraulicData::setLinkDateAdded(const QUuid &uuid,
                                     const std::optional<QDate> &date_added)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setLinkDateAdded(uuid, date_added));
}

bool HydraulicData::setLinkDateInstalled(const QUuid &uuid,
                                         const std::optional<QDate> &date_installed)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setLinkDateInstalled(uuid, date_installed));
}

bool HydraulicData::setLinkEnabled(const QUuid &uuid, bool enabled)
{
    return emitLinkChangedIfSuccessful(uuid, this->network_editor.setLinkEnabled(uuid, enabled));
}

bool HydraulicData::setJunctionElevationInputType(
    const QUuid &uuid, HydraulicNodeElevationInputType input_type)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionElevationInputType(uuid, input_type));
}

bool HydraulicData::setJunctionElevationM(const QUuid &uuid, double elevation_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionElevationM(uuid, elevation_m));
}

bool HydraulicData::setJunctionTerrainElevationM(const QUuid &uuid, double terrain_elevation_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionTerrainElevationM(uuid, terrain_elevation_m));
}

bool HydraulicData::setJunctionElevationOffsetM(const QUuid &uuid, double elevation_offset_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionElevationOffsetM(uuid, elevation_offset_m));
}

bool HydraulicData::addJunctionDemand(const QUuid &uuid,
                                      const HydraulicNodeJunctionDemand &demand)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.addJunctionDemand(uuid, demand));
}

bool HydraulicData::removeJunctionDemand(const QUuid &uuid, int demand_index)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.removeJunctionDemand(uuid, demand_index));
}

bool HydraulicData::setJunctionDemandCategoryName(const QUuid &uuid, int demand_index,
                                                  const QString &category_name)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionDemandCategoryName(
                  uuid, demand_index, category_name));
}

bool HydraulicData::setJunctionDemandBaseDemandM3PerH(
    const QUuid &uuid, int demand_index, double base_demand_m3_per_h)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionDemandBaseDemandM3PerH(
                  uuid, demand_index, base_demand_m3_per_h));
}

bool HydraulicData::setJunctionDemandPatternMode(
    const QUuid &uuid, int demand_index, HydraulicTimePatternMode pattern_mode)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionDemandPatternMode(
                  uuid, demand_index, pattern_mode));
}

bool HydraulicData::setJunctionDemandPatternUuid(const QUuid &uuid, int demand_index,
                                                 const QUuid &pattern_uuid)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionDemandPatternUuid(
                  uuid, demand_index, pattern_uuid));
}

bool HydraulicData::setJunctionDemandSourceMethod(
    const QUuid &uuid, int demand_index, HydraulicNodeJunctionDemandSourceMethod source_method)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionDemandSourceMethod(
                  uuid, demand_index, source_method));
}

bool HydraulicData::setJunctionDemandNote(const QUuid &uuid, int demand_index,
                                          const QString &note)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionDemandNote(uuid, demand_index, note));
}

bool HydraulicData::setJunctionEmitterCoefficientM3PerHPerMExponent(
    const QUuid &uuid, double emitter_coefficient_m3_per_h_per_m_exponent)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setJunctionEmitterCoefficientM3PerHPerMExponent(
                  uuid, emitter_coefficient_m3_per_h_per_m_exponent));
}

bool HydraulicData::setReservoirHeadInputType(
    const QUuid &uuid, HydraulicNodeElevationInputType input_type)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setReservoirHeadInputType(uuid, input_type));
}

bool HydraulicData::setReservoirHeadM(const QUuid &uuid, double head_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setReservoirHeadM(uuid, head_m));
}

bool HydraulicData::setReservoirTerrainElevationM(const QUuid &uuid,
                                                  double terrain_elevation_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setReservoirTerrainElevationM(
                  uuid, terrain_elevation_m));
}

bool HydraulicData::setReservoirHeadOffsetM(const QUuid &uuid, double head_offset_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setReservoirHeadOffsetM(uuid, head_offset_m));
}

bool HydraulicData::setReservoirHeadPatternMode(const QUuid &uuid, HydraulicTimePatternMode pattern_mode)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setReservoirHeadPatternMode(uuid, pattern_mode));
}

bool HydraulicData::setReservoirHeadPatternUuid(const QUuid &uuid, const QUuid &pattern_uuid)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setReservoirHeadPatternUuid(uuid, pattern_uuid));
}

bool HydraulicData::setTankElevationInputType(
    const QUuid &uuid, HydraulicNodeTankElevationInputType input_type)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankElevationInputType(uuid, input_type));
}

bool HydraulicData::setTankBottomElevationM(const QUuid &uuid, double bottom_elevation_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankBottomElevationM(uuid, bottom_elevation_m));
}

bool HydraulicData::setTankTerrainElevationM(const QUuid &uuid, double terrain_elevation_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankTerrainElevationM(uuid, terrain_elevation_m));
}

bool HydraulicData::setTankBottomOffsetM(const QUuid &uuid, double bottom_offset_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankBottomOffsetM(uuid, bottom_offset_m));
}

bool HydraulicData::setTankWaterLevelInitialM(const QUuid &uuid, double water_level_initial_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankWaterLevelInitialM(uuid, water_level_initial_m));
}

bool HydraulicData::setTankWaterLevelMinimumM(const QUuid &uuid, double water_level_minimum_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankWaterLevelMinimumM(uuid, water_level_minimum_m));
}

bool HydraulicData::setTankWaterLevelMaximumM(const QUuid &uuid, double water_level_maximum_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankWaterLevelMaximumM(uuid, water_level_maximum_m));
}

bool HydraulicData::setTankGeometryInputType(
    const QUuid &uuid, HydraulicNodeTankGeometryInputType input_type)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankGeometryInputType(uuid, input_type));
}

bool HydraulicData::setTankDiameterM(const QUuid &uuid, double diameter_m)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankDiameterM(uuid, diameter_m));
}

bool HydraulicData::setTankCrossSectionAreaM2(const QUuid &uuid, double cross_section_area_m2)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankCrossSectionAreaM2(uuid, cross_section_area_m2));
}

bool HydraulicData::setTankVolumeAtMaximumLevelM3(
    const QUuid &uuid, double volume_at_maximum_level_m3)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankVolumeAtMaximumLevelM3(
                  uuid, volume_at_maximum_level_m3));
}

bool HydraulicData::setTankMinimumVolumeM3(const QUuid &uuid, double minimum_volume_m3)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankMinimumVolumeM3(uuid, minimum_volume_m3));
}

bool HydraulicData::setTankVolumeCurveUuid(const QUuid &uuid, const QUuid &volume_curve_uuid)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankVolumeCurveUuid(uuid, volume_curve_uuid));
}

bool HydraulicData::setTankCanOverflow(const QUuid &uuid, bool can_overflow)
{
    return emitNodeChangedIfSuccessful(
        uuid, this->network_editor.setTankCanOverflow(uuid, can_overflow));
}

bool HydraulicData::setPipeInitialStatus(
    const QUuid &uuid, HydraulicLinkPipeInitialStatus initial_status)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setPipeInitialStatus(uuid, initial_status));
}

bool HydraulicData::setPipeDiameterMm(const QUuid &uuid, double diameter_mm)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setPipeDiameterMm(uuid, diameter_mm));
}

bool HydraulicData::setPipeMeasuredLengthM(
    const QUuid &uuid, const std::optional<double> &length_measured_m)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setPipeMeasuredLengthM(uuid, length_measured_m));
}

bool HydraulicData::setPipeMaterialId(const QUuid &uuid, const QString &material_id)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setPipeMaterialId(uuid, material_id));
}

bool HydraulicData::setPipeRoughnessHw(const QUuid &uuid, double roughness_hw)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setPipeRoughnessHw(uuid, roughness_hw));
}

bool HydraulicData::setPipeRoughnessDwMm(const QUuid &uuid, double roughness_dw_mm)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setPipeRoughnessDwMm(uuid, roughness_dw_mm));
}

bool HydraulicData::setPipeRoughnessCm(const QUuid &uuid, double roughness_cm)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setPipeRoughnessCm(uuid, roughness_cm));
}

bool HydraulicData::setPipeMinorLoss(const QUuid &uuid, double minor_loss)
{
    return emitLinkChangedIfSuccessful(
        uuid, this->network_editor.setPipeMinorLoss(uuid, minor_loss));
}

bool HydraulicData::setPipeVertexCoordinate(const QUuid &pipe_uuid, int vertex_index,
                                            const CoordinateWGS84 &coordinate)
{
    const std::optional<HydraulicLinkPipe> pipe = this->network_editor.pipe(pipe_uuid);
    if (!pipe.has_value() || vertex_index < 0 || vertex_index >= pipe->vertices.size())
        return false;

    const CoordinateWGS84 coordinate_previous = pipe->vertices.at(vertex_index).coordinate_wgs84;
    if (!this->network_editor.setPipeVertexCoordinate(pipe_uuid, vertex_index, coordinate))
        return false;

    updateBoundingBoxWgs84(coordinate_previous, coordinate);
    return emitLinkChangedIfSuccessful(pipe_uuid, true, NetworkChange::Geometry);
}

bool HydraulicData::setPipeVertices(const QUuid &pipe_uuid,
                                    const QList<CoordinateWGS84> &intermediate_vertices)
{
    if (!this->network_editor.setPipeVertices(pipe_uuid, intermediate_vertices))
        return false;

    rebuildBoundingBoxWgs84();
    return emitLinkChangedIfSuccessful(pipe_uuid, true, NetworkChange::Geometry);
}

bool HydraulicData::setPumpCenterCoordinate(const QUuid &pump_uuid,
                                            const CoordinateWGS84 &coordinate)
{
    const std::optional<HydraulicLinkPump> pump = this->network_editor.pump(pump_uuid);
    if (!pump.has_value())
        return false;

    std::optional<CoordinateWGS84> coordinate_previous;
    if (!pump->vertices.isEmpty())
        coordinate_previous = pump->vertices.first().coordinate_wgs84;
    if (!this->network_editor.setPumpCenterCoordinate(pump_uuid, coordinate))
        return false;

    if (coordinate_previous.has_value())
        updateBoundingBoxWgs84(coordinate_previous.value(), coordinate);
    else if (extendBoundingBoxWgs84(coordinate))
        emit signalBoundingBoxWgs84Changed();

    return emitLinkChangedIfSuccessful(pump_uuid, true, NetworkChange::Geometry);
}

bool HydraulicData::setValveCenterCoordinate(const QUuid &valve_uuid,
                                             const CoordinateWGS84 &coordinate)
{
    const std::optional<HydraulicLinkValve> valve = this->network_editor.valve(valve_uuid);
    if (!valve.has_value())
        return false;

    std::optional<CoordinateWGS84> coordinate_previous;
    if (!valve->vertices.isEmpty())
        coordinate_previous = valve->vertices.first().coordinate_wgs84;
    if (!this->network_editor.setValveCenterCoordinate(valve_uuid, coordinate))
        return false;

    if (coordinate_previous.has_value())
        updateBoundingBoxWgs84(coordinate_previous.value(), coordinate);
    else if (extendBoundingBoxWgs84(coordinate))
        emit signalBoundingBoxWgs84Changed();

    return emitLinkChangedIfSuccessful(valve_uuid, true, NetworkChange::Geometry);
}

bool HydraulicData::applyGeometryBatch(const HydraulicGeometryBatch &batch)
{
    if (batch.isEmpty())
        return true;

    const bool snapshot_was_current =
        this->network_render_snapshot.geometry_revision == this->geometry_revision;
    const HydraulicGeometryBatchResult result = this->network_editor.applyGeometryBatch(batch);
    if (!result.successful)
        return false;

    rebuildBoundingBoxWgs84();
    markNetworkChanged(NetworkChange::Geometry);

    if (snapshot_was_current)
    {
        QSet<quint32> moved_node_render_ids;
        for (NetworkRenderNode &node : this->network_render_snapshot.nodes)
        {
            const auto iterator = batch.node_coordinates.constFind(node.uuid);
            if (iterator == batch.node_coordinates.cend())
                continue;
            node.coordinate_wgs84 = iterator.value();
            moved_node_render_ids.insert(node.render_id);
        }

        QHash<QUuid, const HydraulicLinkPipe *> pipes_by_uuid;
        pipes_by_uuid.reserve(result.affected_pipe_uuids.size());
        for (const HydraulicLinkPipe &pipe : this->network_hydraulic.links_pipes)
        {
            if (result.affected_pipe_uuids.contains(pipe.uuid))
                pipes_by_uuid.insert(pipe.uuid, &pipe);
        }

        QHash<QUuid, const HydraulicLinkPump *> pumps_by_uuid;
        if (!batch.pump_center_coordinates.isEmpty() || !moved_node_render_ids.isEmpty())
        {
            pumps_by_uuid.reserve(this->network_hydraulic.links_pumps.size());
            for (const HydraulicLinkPump &pump : this->network_hydraulic.links_pumps)
                pumps_by_uuid.insert(pump.uuid, &pump);
        }

        QHash<QUuid, const HydraulicLinkValve *> valves_by_uuid;
        if (!batch.valve_center_coordinates.isEmpty() || !moved_node_render_ids.isEmpty())
        {
            valves_by_uuid.reserve(this->network_hydraulic.links_valves.size());
            for (const HydraulicLinkValve &valve : this->network_hydraulic.links_valves)
                valves_by_uuid.insert(valve.uuid, &valve);
        }

        for (NetworkRenderLink &link : this->network_render_snapshot.links)
        {
            const bool endpoint_moved =
                moved_node_render_ids.contains(link.start_node_render_id) ||
                moved_node_render_ids.contains(link.end_node_render_id);
            const qsizetype start_index = qsizetype(link.start_node_render_id) - 1;
            const qsizetype end_index = qsizetype(link.end_node_render_id) - 1;
            if (start_index < 0 || end_index < 0 ||
                start_index >= this->network_render_snapshot.nodes.size() ||
                end_index >= this->network_render_snapshot.nodes.size())
            {
                continue;
            }

            if (link.entity_type == InfrastructureEntity::Pipe)
            {
                const auto source_iterator = pipes_by_uuid.constFind(link.uuid);
                if (source_iterator == pipes_by_uuid.cend())
                    continue;
                const HydraulicLinkPipe *source = source_iterator.value();
                link.vertices_wgs84.clear();
                link.vertices_wgs84.reserve(source->vertices.size() + 2);
                link.vertices_wgs84.append(
                    this->network_render_snapshot.nodes.at(start_index).coordinate_wgs84);
                for (const HydraulicLinkVertex &vertex : source->vertices)
                    link.vertices_wgs84.append(vertex.coordinate_wgs84);
                link.vertices_wgs84.append(
                    this->network_render_snapshot.nodes.at(end_index).coordinate_wgs84);
                continue;
            }

            if (link.entity_type == InfrastructureEntity::Pump &&
                (endpoint_moved || batch.pump_center_coordinates.contains(link.uuid)))
            {
                const auto source_iterator = pumps_by_uuid.constFind(link.uuid);
                if (source_iterator == pumps_by_uuid.cend())
                    continue;
                const HydraulicLinkPump *source = source_iterator.value();
                link.vertices_wgs84.clear();
                link.vertices_wgs84.reserve(source->vertices.size() + 2);
                link.vertices_wgs84.append(
                    this->network_render_snapshot.nodes.at(start_index).coordinate_wgs84);
                for (const HydraulicLinkVertex &vertex : source->vertices)
                    link.vertices_wgs84.append(vertex.coordinate_wgs84);
                link.vertices_wgs84.append(
                    this->network_render_snapshot.nodes.at(end_index).coordinate_wgs84);
            }
            else if (link.entity_type == InfrastructureEntity::Valve &&
                     (endpoint_moved || batch.valve_center_coordinates.contains(link.uuid)))
            {
                const auto source_iterator = valves_by_uuid.constFind(link.uuid);
                if (source_iterator == valves_by_uuid.cend())
                    continue;
                const HydraulicLinkValve *source = source_iterator.value();
                link.vertices_wgs84.clear();
                link.vertices_wgs84.reserve(source->vertices.size() + 2);
                link.vertices_wgs84.append(
                    this->network_render_snapshot.nodes.at(start_index).coordinate_wgs84);
                for (const HydraulicLinkVertex &vertex : source->vertices)
                    link.vertices_wgs84.append(vertex.coordinate_wgs84);
                link.vertices_wgs84.append(
                    this->network_render_snapshot.nodes.at(end_index).coordinate_wgs84);
            }
        }

        this->network_render_snapshot.geometry_revision = this->geometry_revision;
        this->network_render_snapshot.visual_revision = this->visual_revision;
    }

    for (auto iterator = batch.node_coordinates.cbegin(); iterator != batch.node_coordinates.cend(); ++iterator)
    {
        const std::optional<InfrastructureEntity> entity_type = nodeEntityType(iterator.key());
        if (entity_type.has_value())
            emit signalNodeChanged(entity_type.value(), iterator.key());
    }
    for (auto iterator = batch.pump_center_coordinates.cbegin(); iterator != batch.pump_center_coordinates.cend(); ++iterator)
        emit signalLinkChanged(InfrastructureEntity::Pump, iterator.key());
    for (auto iterator = batch.valve_center_coordinates.cbegin(); iterator != batch.valve_center_coordinates.cend(); ++iterator)
        emit signalLinkChanged(InfrastructureEntity::Valve, iterator.key());
    for (const QUuid &pipe_uuid : result.affected_pipe_uuids)
        emit signalLinkChanged(InfrastructureEntity::Pipe, pipe_uuid);

    return true;
}

QUuid HydraulicData::splitPipeAtVertex(const QUuid &pipe_uuid, int vertex_index,
                                       const QUuid &junction_uuid)
{
    const QUuid second_pipe_uuid =
        this->network_editor.splitPipeAtVertex(pipe_uuid, vertex_index, junction_uuid);
    if (!second_pipe_uuid.isNull())
        markNetworkChanged(NetworkChange::Geometry);
    return second_pipe_uuid;
}

bool HydraulicData::undoPipeSplit(const QUuid &first_pipe_uuid, const QUuid &second_pipe_uuid,
                                  const QUuid &junction_uuid)
{
    const bool successful =
        this->network_editor.undoPipeSplit(first_pipe_uuid, second_pipe_uuid, junction_uuid);
    if (successful)
        markNetworkChanged(NetworkChange::Geometry);
    return successful;
}

bool HydraulicData::deleteJunction(const QUuid &uuid)
{
    const bool successful = this->network_editor.deleteJunction(uuid);
    if (successful)
    {
        rebuildBoundingBoxWgs84();
        markNetworkChanged(NetworkChange::Geometry);
    }
    return successful;
}

bool HydraulicData::deleteReservoir(const QUuid &uuid)
{
    const bool successful = this->network_editor.deleteReservoir(uuid);
    if (successful)
    {
        rebuildBoundingBoxWgs84();
        markNetworkChanged(NetworkChange::Geometry);
    }
    return successful;
}

bool HydraulicData::deleteTank(const QUuid &uuid)
{
    const bool successful = this->network_editor.deleteTank(uuid);
    if (successful)
    {
        rebuildBoundingBoxWgs84();
        markNetworkChanged(NetworkChange::Geometry);
    }
    return successful;
}

bool HydraulicData::deletePipe(const QUuid &uuid)
{
    const bool successful = this->network_editor.deletePipe(uuid);
    if (successful)
    {
        rebuildBoundingBoxWgs84();
        markNetworkChanged(NetworkChange::Geometry);
    }
    return successful;
}

bool HydraulicData::deletePump(const QUuid &uuid)
{
    const bool successful = this->network_editor.deletePump(uuid);
    if (successful)
    {
        rebuildBoundingBoxWgs84();
        markNetworkChanged(NetworkChange::Geometry);
    }
    return successful;
}

bool HydraulicData::deleteValve(const QUuid &uuid)
{
    const bool successful = this->network_editor.deleteValve(uuid);
    if (successful)
    {
        rebuildBoundingBoxWgs84();
        markNetworkChanged(NetworkChange::Geometry);
    }
    return successful;
}
